"""Uctan uca olay zinciri dogrulayici (Faz 3).

AY `test evt` tetiklemesinden sonra her tuketici yuzeyde yeni kayit
beklenir (RF teslimi tekil ~48-55 s oldugundan varsayilan son teslim
tarihi 120 s):

  (1) faults_web : /faults?feeder=N icerigi degisiyor (L*T/L*P kayit
      listeleri; evt->gecici, evt sim->kalici tarafinda buyume)
  (2) fltlog     : konsol `fltlog dump` kuyrugu degisiyor
  (3) iec104     : iec104.jsonl'ye fault_event kaydi duser (master
      kipinde; prob kipinde bu halka 'skip' sayilir)
  (4) modbus     : fider blogu temp/perm fault sayaclari artyor
      (uyari sinifi halka - kanitli ama baska yuzeyden okuma)

AY->fider eslemesi yoksa tum fiderler taranir; ilk degisen fider zincirin
fideri sayilir. Her halkanin suresi olculur; (1)-(3) halkalarindan biri
tukanirsa chain_break anomalisi + kanit paketi tetiklenir.
"""

import json
import time
from pathlib import Path

from probes.modbus_map_check import (FEEDER_PERM_COUNTS, decode_feeder_header,
                                     parse_map_constants)


def faults_signature(payload):
    """/faults JSON'undan degisim imzasi: kayit listesi uzunluklari."""
    if not isinstance(payload, dict):
        return None
    parts = []
    for key in sorted(payload):
        if key.upper() in ("L1T", "L2T", "L3T", "L1P", "L2P", "L3P"):
            value = payload[key]
            if isinstance(value, str):
                parts.append("%s:%d" % (key, value.count("\n")))
            else:
                parts.append("%s:%r" % (key, value))
    return ";".join(parts) if parts else json.dumps(payload, sort_keys=True)[:200]


def fltlog_signature(text):
    """fltlog dump cikis imzasi: son 1500 karakter (kuyruk degisimi)."""
    return text.strip()[-1500:]


def jsonl_records_since(path, since_wall, action=None, max_lines=400):
    """JSONL dosyasinin son kayitlarindan wall >= since_wall olanlari."""
    source = Path(path)
    if not source.exists():
        return []
    with open(source, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()[-max_lines:]
    result = []
    for line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if action is not None and record.get("action") != action:
            continue
        if record.get("wall", 0.0) >= since_wall - 2.0:
            result.append(record)
    return result


class ChainVerifier:
    """Tek olayin tum yuzeylere varisini olcup dogrular."""

    CORE_LINKS = ("faults_web", "fltlog", "iec104")

    def __init__(self, ay_stim, web_probe, console, writer, detector,
                 iec104_jsonl_path, modbus_probe=None, feeder_count=4,
                 deadline_s=120.0, poll_s=10.0):
        self.ay_stim = ay_stim
        self.web = web_probe
        self.console = console
        self.writer = writer
        self.detector = detector
        self.iec104_path = iec104_jsonl_path
        self.modbus = modbus_probe
        self.feeder_count = feeder_count
        self.deadline_s = deadline_s
        self.poll_s = poll_s
        self.map = parse_map_constants() if modbus_probe is not None else None

    def run(self):
        """Bir zincir turu: tetikle -> halkalari bekle -> raporla."""
        trigger = self.ay_stim.stimulate_random()
        if not trigger.get("ok"):
            self.writer.write({"action": "chain", "ok": False,
                               "reasons": trigger.get("reasons")})
            return
        started_wall = time.time()
        started_mono = time.monotonic()
        feeders = [trigger["feeder"]] if trigger.get("feeder") else \
            list(range(1, self.feeder_count + 1))
        baselines = self._baselines(feeders)
        links = {name: {"ok": False, "ms": None}
                 for name in ("faults_web", "fltlog", "iec104", "modbus")}
        detected_feeder = trigger.get("feeder")
        deadline = started_mono + self.deadline_s
        while time.monotonic() < deadline:
            elapsed = time.monotonic() - started_mono
            if not links["faults_web"]["ok"]:
                changed = self._faults_changed(feeders, baselines["faults"])
                if changed is not None:
                    links["faults_web"] = {"ok": True,
                                           "ms": round(elapsed * 1000, 1)}
                    detected_feeder = detected_feeder or changed
                    feeders = [changed]
            if not links["fltlog"]["ok"] and \
                    self._fltlog_changed(baselines["fltlog"]):
                links["fltlog"]["ok"] = True
                links["fltlog"]["ms"] = round(elapsed * 1000, 1)
            if not links["iec104"]["ok"] and \
                    jsonl_records_since(self.iec104_path, started_wall,
                                        action="fault_event"):
                links["iec104"]["ok"] = True
                links["iec104"]["ms"] = round(elapsed * 1000, 1)
            if not links["modbus"]["ok"] and \
                    self._modbus_counts_grew(baselines["modbus"], feeders):
                links["modbus"]["ok"] = True
                links["modbus"]["ms"] = round(elapsed * 1000, 1)
            if all(link["ok"] or name == "modbus"
                   for name, link in links.items()):
                break
            time.sleep(self.poll_s)
        record = {"action": "chain", "ok": True, "ay": trigger["port"],
                  "cmd": trigger["cmd"], "feeder": detected_feeder,
                  "deadline_s": self.deadline_s, "links": links,
                  "ms": round((time.monotonic() - started_mono) * 1000, 1)}
        self.writer.write(record)
        broken = [name for name in self.CORE_LINKS if not links[name]["ok"]]
        if broken:
            self.detector.report(
                "chain_break", "critical", "chain",
                "AY olayi (%s) yuzeylere ulasmadi: %s (fider=%s)" %
                (trigger["cmd"], broken, detected_feeder))

    # -- taban ve degisim denetimleri ----------------------------------

    def _baselines(self, feeders):
        faults = {}
        for feeder in feeders:
            faults[feeder] = faults_signature(self._faults_payload(feeder))
        modbus = {}
        if self.modbus is not None:
            for feeder in feeders:
                modbus[feeder] = self._feeder_counts(feeder)
        return {"faults": faults,
                "fltlog": fltlog_signature(self._fltlog_dump()),
                "modbus": modbus}

    def _faults_payload(self, feeder):
        try:
            status, body, _ = self.web._request("/faults?feeder=%d" % feeder)
            if status == 200:
                return json.loads(body)
        except Exception:               # noqa: BLE001 (zincir turu toleranli)
            pass
        return None

    def _faults_changed(self, feeders, baselines):
        for feeder in feeders:
            if faults_signature(self._faults_payload(feeder)) != \
                    baselines.get(feeder):
                return feeder
        return None

    def _fltlog_dump(self):
        try:
            return self.console.command("fltlog dump", settle_s=4.0)
        except Exception:               # noqa: BLE001
            return ""

    def _fltlog_changed(self, baseline):
        dump = self._fltlog_dump()
        return bool(dump) and fltlog_signature(dump) != baseline

    def _feeder_counts(self, feeder):
        if self.modbus is None:
            return None
        base = self.map["line_base"] + \
            (feeder - 1) * self.map["line_stride"]
        regs, ok = self.modbus.read_range(base, FEEDER_PERM_COUNTS + 3)
        if not ok:
            return None
        decoded = decode_feeder_header(regs)
        return (decoded["temp_counts"], decoded["perm_counts"])

    def _modbus_counts_grew(self, baselines, feeders):
        for feeder in feeders:
            before = baselines.get(feeder)
            if before is None:
                continue
            after = self._feeder_counts(feeder)
            if after is None:
                continue
            if any(new > old for new, old in zip(after[0] + after[1],
                                                 before[0] + before[1])):
                return True
        return False
