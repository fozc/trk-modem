"""IEC104 surekli master worker (Faz 2): format + kadans + GI dogrulama.

Iki katman:

- FrameValidator : saf dogrulayici (c104 bagimliligi YOK) - her rx
  I-frame'in tip/IOA/CA/OA/CP56/tip-ail dogrulamasi, periyodik cevrim
  muhasebesi, kadans ve saat sapmasi denetimi, GI yanit dogrulamasi.
  Sukret kurallari (cases_periodic.validate_frame,
  cases._check_gi_frames, cases._ioas) parametre olarak enjekte edilir;
  self-test ayni kurallari sentetik APDU'larla sinar.
- Iec104MasterWorker : c104 tasiyici - master104.Master104 uzerinde
  tek surekli baglanti + yeniden baglanma + zamanlanmis GI/saat-senkron
  turlari + tam APDU JSONL kaniti (JsonlEvidence).

Periyodik aileler sukret p01 ile ayni: anlik olcumler + enerji/yuk/rf
durumlari. Trip ve ariza tabanlari (100000+) periyodik yayina girmez;
ariza uzayindan gelen spontaneler ayri "fault_event" kaydi uretir
(Faz 3 zincir dogrulayicisinin girdisi).

Tek istemci kurali: 2404'te tek master - bu worker acikken Faz 1
IecProbeTask koşmaz (run_monitor ikisinden birini secer).
"""

import datetime
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
IEC104_DIR = REPO_ROOT / "test/system/iec104_master"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extimport import load_module  # noqa: E402

PERIODIC_STATE_FAMILIES = ("enerji", "yuk", "rf")
TIME_SKEW_TOLERANCE_S = 180.0
CADENCE_GRACE_S = 60.0
FAULT_IOA_FLOOR = 100000


class MasterUnavailable(Exception):
    """c104/venv eksik; run_monitor probe kipine duser ya da hizli hata verir."""


def load_master_modules():
    """c104 + suite modullerini yukle; c104 yoksa MasterUnavailable."""
    try:
        import c104  # noqa: F401
    except ImportError as error:
        raise MasterUnavailable(
            "c104 kurulu degil (venv tarifi: "
            "py -3.13 -m venv test/system/iec104_master/.venv && "
            "test/system/iec104_master/.venv/Scripts/pip install "
            "--only-binary=:all: c104==2.2.1 \"pyserial>=3.5,<4\"): %r"
            % (error,))
    return {
        "c104": sys.modules["c104"],
        "apdu": load_module("device_monitor_apdu", IEC104_DIR / "apdu.py"),
        "master104": load_module(
            "device_monitor_master104", IEC104_DIR / "master104.py",
            extra_sys_path=[IEC104_DIR]),
        "cases": load_module(
            "device_monitor_cases", IEC104_DIR / "cases.py", extra_sys_path=[IEC104_DIR]),
        "cases_periodic": load_module(
            "device_monitor_cases_periodic", IEC104_DIR / "cases_periodic.py",
            extra_sys_path=[IEC104_DIR]),
        "oracle": load_module("device_monitor_oracle_full", IEC104_DIR / "oracle.py"),
        "evidence": load_module("device_monitor_jsonl_evidence",
                                IEC104_DIR / "evidence.py"),
    }


class FrameValidator:
    """Saf I-frame dogrulayici: c104 olmadan da calisir (self-test)."""

    def __init__(self, ca, oa, currents, states, battery_ioa,
                 trip_ioas=(), fault_space=(), periodic_send_s=0.0,
                 skew_tolerance_s=TIME_SKEW_TOLERANCE_S,
                 suite_validate_frame=None, suite_check_gi_frames=None,
                 suite_ioas=None):
        self.ca = ca
        self.oa = oa
        self.currents = set(currents)
        self.states = set(states)
        self.battery_ioa = battery_ioa
        self.trip_ioas = set(trip_ioas)
        self.fault_space = set(fault_space)
        self.periodic_send_s = periodic_send_s
        self.skew_tolerance_s = skew_tolerance_s
        self.suite_validate_frame = suite_validate_frame
        self.suite_check_gi_frames = suite_check_gi_frames
        self.suite_ioas = suite_ioas
        self.periodic_expected = self.currents | self.states
        self.reset()

    def reset(self):
        self.seen = set()
        self.last_cycle_mono = None
        self.cycles = 0
        self.connected_since = None

    # -- spontane/periyodik akis -------------------------------------

    def feed(self, frame, now_mono=None, now_wall=None):
        """Tek rx frame -> (records, findings).

        frame: master104 cergevesi (t, 'rx', raw, parsed).
        records: iec104.jsonl'e yazilacak sozlukler.
        findings: (sinif, siddet, detay) demetleri.
        """
        records = []
        findings = []
        now_mono = time.monotonic() if now_mono is None else now_mono
        now_wall = datetime.datetime.now() if now_wall is None else now_wall
        parsed = frame[3]
        if parsed is None or parsed.get("kind") != "I":
            return records, findings
        asdu = parsed.get("asdu")
        if asdu is None:
            findings.append(("iec104_bad_asdu", "warning",
                             "I-frame ASDU cozumlenemedi: %s" %
                             parsed.get("asdu_hex", "")[:60]))
            return records, findings
        cot = asdu.get("cot")
        if cot == 4:
            # M_EI (COT=4) baglanti kurulumunda normaldir; bagliyken
            # gelmesi cihazin 104 yiginini yeniden baslattigini gosterir.
            if self.connected_since is not None and \
                    now_mono - self.connected_since > 60.0:
                findings.append(("iec104_stack_restart", "critical",
                                 "bagliyken M_EI/COT=4 geldi "
                                 "(tip=%s oa=%s)" % (asdu.get("type"),
                                                     asdu.get("oa"))))
            return records, findings
        if cot != 3:
            return records, findings
        ioas = {obj.get("ioa") for obj in asdu.get("objects", [])}
        if ioas and ioas <= self.periodic_expected:
            self._feed_periodic(frame, records, findings, now_mono, now_wall)
        elif ioas and all(ioa is not None and ioa >= FAULT_IOA_FLOOR
                          for ioa in ioas):
            # Ariza kaydi IOA uzayi: gecici 100000+ ve kalici 200000+
            # tabanlarindan yukari (nvram fabrika formulleri; izlenen
            # noktalar 10000'in altinda kalir).
            records.append({"action": "fault_event", "type": asdu.get("type"),
                            "ca": asdu.get("ca"),
                            "ioas": sorted(v for v in ioas if v is not None)[:24]})
        else:
            records.append({"action": "spont_other",
                            "type": asdu.get("type"), "ca": asdu.get("ca"),
                            "ioas": sorted(v for v in ioas if v is not None)[:24]})
        return records, findings

    def _feed_periodic(self, frame, records, findings, now_mono, now_wall):
        try:
            fresh = self.suite_validate_frame(
                frame, self.currents, self.states, self.ca, self.oa)
        except AssertionError as error:
            findings.append(("iec104_format", "critical",
                             "periyodik format ihlali: %s" % error))
            return
        self.seen |= fresh
        if not self.seen >= self.periodic_expected:
            return
        cycle_ms = None
        if self.last_cycle_mono is not None:
            cycle_ms = round((now_mono - self.last_cycle_mono) * 1000.0, 1)
        self.cycles += 1
        self.last_cycle_mono = now_mono
        records.append({"action": "periodic_cycle", "cycle_ms": cycle_ms,
                        "ioas": len(self.periodic_expected),
                        "cycles": self.cycles})
        self._check_time_skew(frame, findings, now_wall)
        self.seen.clear()

    def _check_time_skew(self, frame, findings, now_wall):
        for obj in (frame[3].get("asdu") or {}).get("objects", []):
            stamp = obj.get("time")
            if not stamp:
                continue
            try:
                device_time = datetime.datetime(
                    stamp["year"], stamp["month"], stamp["day"],
                    stamp["hour"], stamp["min"], int(stamp["ms"] / 1000.0))
            except ValueError:
                findings.append(("iec104_time_invalid", "warning",
                                 "CP56 takvim degeri gecersiz: %s" % stamp))
                return
            skew = abs((now_wall - device_time).total_seconds())
            if skew > self.skew_tolerance_s:
                findings.append(("iec104_time_skew", "warning",
                                 "cihaz saati %.0f s sapmis (%s)" %
                                 (skew, device_time.isoformat())))
            return

    def check_cadence(self, now_mono=None):
        """Periyodik yayın duraklamasi: 3x periyot + 60 s tavandan sonra bulgu."""
        now_mono = time.monotonic() if now_mono is None else now_mono
        findings = []
        if self.periodic_send_s <= 0:
            return findings
        limit = 3.0 * self.periodic_send_s + CADENCE_GRACE_S
        if self.last_cycle_mono is not None:
            silent = now_mono - self.last_cycle_mono
            base = "son cevrimden %.0f s"
        elif self.connected_since is not None:
            silent = now_mono - self.connected_since
            base = "baglantidan beri cevrim yok, %.0f s"
        else:
            return findings
        if silent > limit:
            findings.append(("iec104_periodic_stalled", "warning",
                             base % silent + " (periyot=%ss, sinir=%.0fs)" %
                             (self.periodic_send_s, limit)))
        return findings

    # -- GI dogrulama --------------------------------------------------

    def verify_gi(self, detail):
        """GI istasyon (QOI 20) yaniti -> (ok, record, findings)."""
        findings = []
        record = {"action": "gi"}
        failure = self.suite_check_gi_frames(detail, 20, self.ca,
                                             self.currents, self.states,
                                             self.battery_ioa)
        if failure:
            findings.append(("iec104_gi_mismatch", "critical",
                             "GI cerceve ihlali: %s" % failure))
        ioas = self.suite_ioas(detail.get("data", []))
        station_expected = (self.periodic_expected | self.trip_ioas |
                            ({self.battery_ioa} - {0}))
        missing = sorted(station_expected - ioas)
        extra = sorted(ioas - station_expected)
        ok = failure is None and not missing and not extra
        record.update({"ok": ok, "frames": len(detail.get("data", [])),
                       "ioa_count": len(ioas),
                       "missing": missing[:20], "extra": extra[:20]})
        if missing:
            findings.append(("iec104_gi_missing", "critical",
                             "%d IOA GI yanitinda yok: %s" %
                             (len(missing), missing[:10])))
        if extra:
            findings.append(("iec104_gi_extra", "critical",
                             "%d beklenmedik IOA: %s" %
                             (len(extra), extra[:10])))
        return ok, record, findings


class Iec104MasterWorker:
    """c104 tasiyici: surekli baglanti + yeniden baglanma + GI/saat turlari."""

    def __init__(self, host, snap, periodic_send_s, writer, detector,
                 apdu_evidence_path, gi_period_s=900.0,
                 clock_sync_period_s=3600.0):
        modules = load_master_modules()
        self.modules = modules
        self.writer = writer
        self.detector = detector
        self.host = host
        self.apdu = modules["apdu"]

        currents = set()
        states = set()
        trip = set()
        for line in snap["lines"]:
            if not line["in_use"]:
                continue
            currents.update(line["ioa"]["anlik"])
            for family in PERIODIC_STATE_FAMILIES:
                states.update(line["ioa"][family])
            trip.update(line["ioa"]["trip"])
        _, _, _, fault_space = modules["oracle"].expected_ioa_sets(snap)

        self.validator = FrameValidator(
            snap["ca"], snap["oa"], currents, states,
            int(snap.get("battery_ioa", 0) or 0), trip_ioas=trip,
            fault_space=fault_space, periodic_send_s=periodic_send_s,
            suite_validate_frame=modules["cases_periodic"].validate_frame,
            suite_check_gi_frames=modules["cases"]._check_gi_frames,
            suite_ioas=modules["cases"]._ioas)
        self.evidence = modules["evidence"].JsonlEvidence(apdu_evidence_path)
        self.has_in_use_lines = any(line["in_use"] for line in snap["lines"])
        self.master = modules["master104"].Master104(
            host, snap["port"], snap["ca"], evidence=self.evidence,
            keep_alive_s=snap["t3"] + 30)
        self.running = False
        self.monitor_thread = None
        self.cursor = time.monotonic()
        self.ever_connected = False

    # -- yasam dongusu -------------------------------------------------

    def start(self):
        self.master.start()
        self.running = True
        self.monitor_thread = threading.Thread(target=self._monitor,
                                               daemon=True,
                                               name="device-monitor-iec104")
        self.monitor_thread.start()

    def stop(self):
        self.running = False
        if self.monitor_thread is not None:
            self.monitor_thread.join(timeout=10.0)
        self.master.stop()
        self.evidence.close()

    def _monitor(self):
        while self.running:
            if not self.master.is_connected:
                # Ilk baglanti kurulmadan once 'koptu' raporu yanlis
                # pozitiftir: yalniz en az bir kez baglanmis sonra
                # dusen baglanti raporlanir.
                if self.ever_connected:
                    self.detector.report(
                        "iec104_master_disconnected", "warning", "iec104",
                        "master baglantisi kapandi; yeniden baglaniyor")
                if not self._reconnect():
                    time.sleep(8.0)
                    continue
                self.ever_connected = True
                self.validator.connected_since = time.monotonic()
                self.cursor = time.monotonic()
            frames = self.master.since(self.cursor)
            self.cursor = time.monotonic()
            for frame in frames:
                self._consume(frame)
            self._trim_master_frames()
            for label, severity, detail in \
                    self.validator.check_cadence(time.monotonic()):
                self.detector.report(label, severity, "iec104", detail)
            time.sleep(0.25)

    def _reconnect(self):
        if self.master.is_connected:
            return True
        try:
            if self.master.connect(timeout_s=30.0):
                return True
        except Exception:           # noqa: BLE001 (baglanma hatasi asagida raporlanir)
            pass
        # connect() STARTDT_CON beklerken baglanti kurulmus olabilir
        # (zaten acik baglantida CON bir daha gelmez). Asagidaki
        # stop/start döngüsü boyle bir saglikli baglantiyi yikmasin -
        # ilk-baglanti kapanmasinin istemci tarafi kok nedeni buydu
        # (.temp_files/iec104-first-link-soak-original/calls.jsonl).
        if self.master.is_connected:
            return True
        try:
            self.master.stop()
            self.master.start()
            return bool(self.master.connect(timeout_s=30.0))
        except Exception as error:  # noqa: BLE001
            self.detector.report("iec104_master_down", "critical", "iec104",
                                 "yeniden baglanma basarisiz: %r" % error)
            return False

    def _consume(self, frame):
        direction = frame[1]
        if direction == "state":
            return
        if direction == "unexpected":
            self.detector.report(
                "iec104_unexpected", "warning", "iec104",
                "master beklenmedik mesaj: %s" % frame[3])
            return
        if direction != "rx":
            return
        records, findings = self.validator.feed(frame)
        for record in records:
            self.writer.write(record)
        for label, severity, detail in findings:
            self.detector.report(label, severity, "iec104", detail)

    def _trim_master_frames(self):
        """Master104.frames sinirsiz buyur; en yeni 500 cerceve kalir.

        since(t0) zaman filtresiyle calisir; eski cerceve silinmesi
        yalniz gecmis t0 sorgularini etkiler (kullanmiyoruz).
        """
        with self.master._lock:  # noqa: SLF001 (suite sarmalayicisinda trim API yok)
            del self.master.frames[:-500]

    # -- zamanlanmis turlar (scheduler gorevleri) ----------------------

    def gi_tour(self):
        """GI istasyon turu; kullanmda hat yoksa suite semantiğiyle SKIP.

        oracle.iec104_lines_empty: hattsız cihazda GI veri tasimaz - bos
        beklenen kume 'missing' yanlis alarmi uretir (suite bu durumda
        GI vakalarini atlar). Skip kaydi ok=None tasir: basari sayisi
        ve skip sayisi ayri raporlanir.
        """
        if not self.has_in_use_lines:
            self.writer.write({"action": "gi", "ok": None,
                               "skip": "no_in_use_lines"})
            return
        t0 = time.monotonic()
        # _collect_interrogation yalniz ACT_CON/ACT_TERM bekler; istegi
        # suite (i02) gibi once biz gonderiyoruz.
        if not self.master.interrogate(20):
            self.detector.report("iec104_gi_failed", "critical", "iec104",
                                 "GI istegi gonderilemedi (STARTED yarisi)")
            self.writer.write({"action": "gi", "ok": False,
                               "stage": "send",
                               "ms": round((time.monotonic() - t0) * 1000, 1)})
            return
        ok, detail = self.modules["cases"]._collect_interrogation(
            self.master, t0, 20, data_deadline_s=30.0)
        if not ok:
            self.detector.report("iec104_gi_failed", "critical", "iec104",
                                 "GI turu %s asamasinda tukendi" %
                                 detail.get("stage"))
            self.writer.write({"action": "gi", "ok": False,
                               "stage": detail.get("stage"),
                               "ms": round((time.monotonic() - t0) * 1000, 1)})
            return
        verdict_ok, record, findings = self.validator.verify_gi(detail)
        record["ms"] = round((time.monotonic() - t0) * 1000.0, 1)
        self.writer.write(record)
        for label, severity, detail_text in findings:
            self.detector.report(label, severity, "iec104", detail_text)

    def clock_sync_tour(self):
        t0 = time.monotonic()
        ok = False
        if self.master.clock_sync():
            ok, _ = self.master.wait(
                lambda f: self.modules["cases"].is_i_asdu(
                    f, "rx", self.apdu.TYPE_C_CS_NA_1, cot=7),
                12.0, t0=t0)
        self.writer.write({"action": "clock_sync", "ok": ok,
                           "ms": round((time.monotonic() - t0) * 1000.0, 1)})
        if not ok:
            self.detector.report("iec104_clocksync_failed", "warning",
                                 "iec104", "saat senkronu ACT_CON gelmedi")
