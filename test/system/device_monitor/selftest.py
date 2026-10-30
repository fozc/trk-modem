#!/usr/bin/env python3
"""Device monitor cihazsiz self-test'leri.

Donanim ACMAZ: konsol/web/modbus portlarina baglanmaz; yalniz saf
mantik (rotasyon, scheduler, anomali kurallari, trim defteri, kanit
pili, modbus siniflandirici) ve modul importlari denetlenir.
`python selftest.py` ile tek basina koşar; integration sarmalayici
(test/integration/device_monitor) her koşuda once bunu calistirir.
"""

import json
import random
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

RESULTS = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print("%s: %s %s" % (status, name, detail), flush=True)
    RESULTS.append((name, bool(condition)))


def test_recorder_rotation():
    from recorder import JsonlWriter, Metrics, RotatingRawLog
    with tempfile.TemporaryDirectory() as tmp:
        raw = RotatingRawLog(Path(tmp) / "serial.log", max_bytes=200, keep=2)
        for _ in range(6):
            raw.write(b"x" * 150)
        raw.close()
        base = Path(tmp) / "serial.log"
        check("rotation_files", base.exists() and
              base.with_suffix(".log.1").exists() and
              base.with_suffix(".log.2").exists() and not
              base.with_suffix(".log.3").exists(),
              "rotations=%d" % raw.rotations)
        check("rotation_count", raw.rotations == 2,
              "rotations=%d" % raw.rotations)
        writer = JsonlWriter(Path(tmp) / "web.jsonl")
        entry = writer.write({"ep": "/status/board", "status": 200})
        writer.close()
        check("jsonl_fields", "wall" in entry and "mono" in entry)
        metrics = Metrics(Path(tmp) / "metrics.json", {"state": "running"})
        metrics.inc("cycles")
        metrics["state"] = "passed"
        metrics.save()
        data = json.loads((Path(tmp) / "metrics.json").read_text())
        check("metrics_atomic", data["cycles"] == 1 and
              data["state"] == "passed" and "updated" in data)


def test_scheduler():
    from scheduler import Scheduler
    rng_a = random.Random(42)
    rng_b = random.Random(42)
    sched_a = Scheduler(rng_a)
    sched_b = Scheduler(rng_b)
    sched_a.add_random("drop", 600.0, lambda: None, min_gap_s=30.0)
    sched_b.add_random("drop", 600.0, lambda: None, min_gap_s=30.0)
    seq_a = [sched_a.tasks["drop"].next_delay_s() for _ in range(20)]
    seq_b = [sched_b.tasks["drop"].next_delay_s() for _ in range(20)]
    check("scheduler_determinism", seq_a == seq_b,
          "ilk=%s" % [round(v, 3) for v in seq_a[:3]])
    check("scheduler_min_gap", min(seq_a) >= 30.0)
    errors = []
    runs = []
    sched = Scheduler(random.Random(1), on_error=lambda n, e, t:
                      errors.append((n, e)),
                      on_success=lambda n: runs.append(n))
    state = {"boom": 0}
    def boom():
        state["boom"] += 1
        if state["boom"] < 3:
            raise ValueError("sentinel")
    sched.add_periodic("boom", 0.05, boom)
    deadline = time.monotonic() + 1.2
    while time.monotonic() < deadline:
        sched.run_once()
        time.sleep(0.05)
    sched.stop()
    check("scheduler_error_isolation",
          len(errors) == 2 and state["boom"] >= 3 and len(runs) >= 1,
          "errors=%d runs=%d calls=%d" %
          (len(errors), len(runs), state["boom"]))


def test_anomaly():
    from anomaly import AnomalyDetector
    from recorder import JsonlWriter
    with tempfile.TemporaryDirectory() as tmp:
        writer = JsonlWriter(Path(tmp) / "anomalies.jsonl")
        findings = []
        detector = AnomalyDetector(writer, on_finding=findings.append,
                                   cooldown_s=60.0)
        check("marker_hit", detector.scan_line("x 12:00:00 HardFault at 0x..."))
        check("marker_miss", not detector.scan_line("normal satir"))
        detector.scan_line("yine HardFault")
        check("marker_cooldown", len(findings) == 1,
              "findings=%d" % len(findings))
        check("marker_suppressed",
              detector.stats()["suppressed"].get("hardfault") == 1)
        detector.scan_elog("3#41 TS:2026-10-28 10:00:00 LVL:1 "
                           "CODE:HARDFAULT INFO:pc=0x08001234")
        detector.scan_elog("4#42 TS:2026-10-28 10:00:01 LVL:1 "
                           "CODE:RESET_CAUSE INFO:IWDG raw=0x...")
        detector.scan_elog("5#43 TS:2026-10-28 10:00:02 LVL:1 "
                           "CODE:RESET_CAUSE INFO:POR|PIN normal")
        detector.scan_elog("6#44 TS:1970-01-01 00:00:00 LVL:1 "
                           "CODE:RF_CFG_COMMIT INFO:...")
        classes = [item["class"] for item in findings]
        check("elog_rules", "elog_hardfault" in classes and
              "elog_time_lost" in classes and
              classes.count("elog_reset_abnormal") == 1,
              "classes=%s" % classes)
        writer.close()


def test_trim():
    from console_worker import trim_lines
    lines = [(float(i), "line%d" % i) for i in range(10)]
    cursor = trim_lines(lines, 8, 4)
    check("trim_bounds", len(lines) == 4 and cursor == 2,
          "len=%d cursor=%d" % (len(lines), cursor))
    check("trim_content", lines[0][1] == "line6" and lines[-1][1] == "line9")
    lines2 = [(1.0, "a"), (2.0, "b")]
    cursor2 = trim_lines(lines2, 1, 50)
    check("trim_noop", len(lines2) == 2 and cursor2 == 1)


class FakeConsole:
    """EvidenceCollector icin konsol taklidi: sabit cikti + halka."""

    def __init__(self):
        self.commands = []

    def command(self, cmd, settle_s=3.0):
        self.commands.append(cmd)
        return "out-of: " + cmd

    def lines_snapshot(self, limit=None):
        items = [(1.0, "once"), (2.0, "HardFault satiri")]
        return items[-limit:] if limit else items


def test_evidence():
    from evidence import COMMAND_BATTERY, EvidenceCollector
    from recorder import Metrics
    with tempfile.TemporaryDirectory() as tmp:
        metrics = Metrics(Path(tmp) / "metrics.json", {"state": "running"})
        tail = Path(tmp) / "web.jsonl"
        tail.write_text('{"ep":1}\n{"ep":2}\n', encoding="utf-8")
        console = FakeConsole()
        collector = EvidenceCollector(console, Path(tmp) / "evidence",
                                      tail_files={"web": tail},
                                      metrics=metrics, cooldown_s=60.0)
        first = collector.collect("hardfault", "sentinel detay")
        second = collector.collect("hardfault", "tekrar")
        check("evidence_dir", first is not None and first.is_dir(),
              str(first))
        check("evidence_cooldown", second is None)
        text = (first / "console.txt").read_text(encoding="utf-8")
        check("evidence_battery", all(
            ("$ " + cmd) in text for cmd, _ in COMMAND_BATTERY),
              "commands=%d" % len(console.commands))
        check("evidence_context",
              (first / "recent_console.txt").read_text(
                  encoding="utf-8").strip().endswith("HardFault satiri"))
        check("evidence_tail",
              (first / "web.tail").read_text(encoding="utf-8") ==
              '{"ep":1}\n{"ep":2}\n')
        check("evidence_metrics", (first / "metrics.json").exists())


def test_modbus_classify():
    from extimport import load_module
    modbus = load_module("selftest_modbus", HERE.parent.parent /
                         "integration/modbus_device/run_tests.py")
    from probes.modbus_probe import classify_response
    slave = 23
    body = bytes([slave, 3, 8]) + bytes([0, 11, 0, 22, 0, 33, 0, 44])
    crc = modbus.crc16(body)
    good_resp = body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])
    verdict = classify_response(good_resp, slave, 4, modbus.crc16)
    check("modbus_read_ok", verdict["ok"] and verdict["kind"] == "read" and
          verdict["values"] == [11, 22, 33, 44], str(verdict))
    exc_body = bytes([slave, 0x83, 2])
    crc_e = modbus.crc16(exc_body)
    verdict = classify_response(exc_body + bytes([crc_e & 0xFF,
                                                  (crc_e >> 8) & 0xFF]),
                                slave, 4, modbus.crc16)
    check("modbus_exception", not verdict["ok"] and
          verdict["kind"] == "exception" and verdict["code"] == 2)
    verdict = classify_response(good_resp[:-2] + b"\x00\x00", slave, 4,
                                modbus.crc16)
    check("modbus_crc_fail", not verdict["ok"] and verdict["kind"] == "crc")
    verdict = classify_response(b"", slave, 4, modbus.crc16)
    check("modbus_timeout", not verdict["ok"] and verdict["kind"] == "timeout")


def test_module_imports():
    try:
        import console_worker  # noqa: F401
        import probes.web_probe  # noqa: F401
        import probes.iec104_probe  # noqa: F401
        import probes.iec104_master_worker  # noqa: F401
        import probes.modbus_map_check  # noqa: F401
        import run_monitor  # noqa: F401
        check("module_imports", True)
    except Exception as error:     # noqa: BLE001
        check("module_imports", False, repr(error))


# ---- Faz 2: Modbus sabit harita -------------------------------------------

SYSTEM_DIR = HERE.parent / "iec104_master"


def test_map_constants():
    from probes.modbus_map_check import parse_map_constants
    constants = parse_map_constants()
    expected = {"line_base": 0, "line_stride": 1000, "line_count": 4,
                "sys_base": 9000, "sys_count": 17, "pwr_base": 9200,
                "pwr_count": 38, "bms_base": 9300, "bms_count": 79,
                "gsm_base": 9400, "gsm_count": 10, "ctrl_aku": 10000,
                "ctrl_reset": 10001, "ctrl_relay1": 10002,
                "ctrl_relay2": 10003, "fault_records": 15}
    wrong = {key: (constants[key], value) for key, value
             in expected.items() if constants[key] != value}
    check("map_constants", not wrong, "farklar=%s" % wrong)


def _synthetic_feeder_regs():
    """Gecerli bir fider baslik blogu (82 kayit) - modbus_map.h duzeni."""
    regs = [0] * 82
    regs[0] = 0x03                       # fider kalite: DEFINED|ANY_LIVE
    for phase in range(3):
        base = 4 + phase * 24
        regs[base] = 0x7F                # tum kalite bitleri
        regs[base + 1] = 0x4148          # 12.5F -> high word (0x41480000)
        regs[base + 2] = 0x0000          # low word
        regs[base + 3], regs[base + 4] = 0x69, 0x5B7C35  # U32 epoch
        regs[base + 5] = 1               # enerji
        regs[base + 8] = 1               # yuk
        regs[base + 11] = 1              # rf
        regs[base + 14] = 0              # trip_failed
        regs[base + 17], regs[base + 18] = 0x41F0, 0x0000  # 30.0F trip_v
        regs[base + 19], regs[base + 20] = 0x40A0, 0x0000  # 5.0F harvest
        regs[base + 21] = 25             # sicaklik
        regs[base + 22] = 0xFFD2         # rssi -46
        regs[base + 23] = 3              # fault_count
    regs[76:79] = [15, 14, 13]           # temp counts
    regs[79:82] = [0, 1, 2]              # perm counts
    return regs


def test_map_decode_and_checks():
    import struct
    from probes.modbus_map_check import (check_feeder, cross_check_relays,
                                         cross_check_rf, decode_feeder_header,
                                         regs_to_float32)
    regs = _synthetic_feeder_regs()
    decoded = decode_feeder_header(regs)
    check("map_decode_feeder",
          decoded["fider_kalite"] == 0x03 and
          decoded["faz"][0]["kalite"] == 0x7F and
          decoded["faz"][1]["irms_a"] == 12.5 and
          decoded["faz"][2]["sicaklik_c"] == 25 and
          decoded["faz"][0]["rssi"] == -46 and
          decoded["temp_counts"] == [15, 14, 13],
          "irms=%s" % decoded["faz"][1]["irms_a"])
    check("map_decode_float32",
          abs(regs_to_float32(0x4120, 0x0000) - 10.0) < 0.01 and
          abs(regs_to_float32(
              *struct.unpack(">HH", struct.pack(">f", -3.25))) + 3.25) < 1e-6)
    check("map_check_clean", not check_feeder(1, decoded, 15))
    broken = decode_feeder_header(_synthetic_feeder_regs())
    broken["faz"][0]["enerji"] = 5
    broken["faz"][1]["kalite"] = 0x80 | 0x03
    broken["temp_counts"][0] = 16
    labels = {item[0] for item in check_feeder(1, broken, 15)}
    check("map_check_violations",
          "modbus_map_value" in labels and
          "modbus_map_quality" in labels, "labels=%s" % labels)
    nan_regs = _synthetic_feeder_regs()
    nan_regs[4 + 1], nan_regs[4 + 2] = 0x7F80, 0x0001    # NaN float
    nan_decoded = decode_feeder_header(nan_regs)
    labels = {item[0] for item in check_feeder(1, nan_decoded, 15)}
    check("map_check_nan", "modbus_map_value" in labels, "labels=%s" % labels)
    findings = cross_check_relays([0, 1], {"RLY": [0, 1]})
    check("map_cross_relay_ok", not findings)
    findings = cross_check_relays([0, 1], {"RLY": [1, 1]})
    check("map_cross_relay_mismatch",
          any(item[0] == "modbus_web_relay_mismatch" for item in findings))
    monitor = {"lines": [{"Phases": [{"Online": 1}, {"Online": 0},
                                      {"Online": 0}]}]}
    check("map_cross_rf_online",
          not cross_check_rf(1, decoded, monitor))
    offline = {"lines": [{"Phases": [{"Online": 0}, {"Online": 0},
                                     {"Online": 0}]}]}
    findings = cross_check_rf(1, decoded, offline)
    check("map_cross_rf_mismatch",
          any(item[0] == "modbus_rf_state_mismatch" for item in findings))
    dead = decode_feeder_header(_synthetic_feeder_regs())
    dead["fider_kalite"] = 0x01
    findings = cross_check_rf(1, dead, monitor)
    check("map_cross_rf_dead",
          any(item[0] == "modbus_rf_state_mismatch" for item in findings))


def test_web_schema():
    from probes.web_probe import schema_missing
    board = {field: 0 for field in
             ("PanelVoltaji", "DcVoltaji", "GirisAkimi", "GirisGucu",
              "AkuGucu", "Kaynak", "TelemetriYasi", "AlarmMaskesi",
              "KartDurum", "KartDurum2", "PowerValidFields", "BatterySOC",
              "ChargeState", "3V3", "3V8", "5V", "TDIE", "ADCValid",
              "ADCSampleAgeMs")}
    check("web_schema_ok",
          schema_missing("/status/board", board) == [] and
          schema_missing("/r?fwVersion", {"version": "1", "buildDate": "2",
                                          "hardware": "3"}) == [] and
          schema_missing("/faults?feeder=2", {"tc": [], "pc": []}) == [])
    del board["BatterySOC"]
    check("web_schema_missing",
          schema_missing("/status/board", board) == ["BatterySOC"] and
          schema_missing("/monitor/rf/0", {}) == [])


# ---- Faz 2: IEC104 FrameValidator (sukret kurallariyla) --------------------


def _cp56(now=None):
    """datetime -> 7 bayt CP56Time2a (IV=0 gecerli damga)."""
    import datetime
    now = datetime.datetime.now() if now is None else now
    msec = now.second * 1000 + now.microsecond // 1000
    return bytes([msec & 0xFF, (msec >> 8) & 0xFF,
                  (now.minute & 0x3F), now.hour & 0x1F,
                  now.day & 0x1F, now.month & 0x0F,
                  (now.year - 2000) & 0x7F])


def _i_frame(apdu_mod, type_id, cot, ca, oa, object_blobs):
    asdu = bytes([type_id, len(object_blobs), cot, oa,
                  ca & 0xFF, (ca >> 8) & 0xFF]) + b"".join(object_blobs)
    ctrl = bytes([0x00, 0x00, 0x00, 0x00])       # vs=0 vr=0
    body = ctrl + asdu
    return bytes([0x68, 4 + len(asdu)]) + body


def _blob(apdu_mod, ioa, tail):
    return ioa.to_bytes(3, "little") + tail


def test_frame_validator():
    import datetime
    from extimport import load_module
    apdu_mod = load_module("selftest_apdu", SYSTEM_DIR / "apdu.py")
    cases_mod = load_module("selftest_cases", SYSTEM_DIR / "cases.py",
                            extra_sys_path=[SYSTEM_DIR])
    cases_periodic = load_module("selftest_cases_periodic",
                                 SYSTEM_DIR / "cases_periodic.py",
                                 extra_sys_path=[SYSTEM_DIR])
    from probes.iec104_master_worker import FrameValidator

    currents = {1001, 1002, 1003}
    states = {1010, 1011, 1012, 1020, 1021, 1022, 1030, 1031, 1032}
    import struct

    def currents_frame(stamp=None):
        blobs = [_blob(apdu_mod, ioa,
                       struct.pack("<f", 12.5) + b"\x00" + _cp56(stamp))
                 for ioa in sorted(currents)]
        return _i_frame(apdu_mod, 36, 3, 1, 1, blobs)

    def states_frame(stamp=None):
        blobs = [_blob(apdu_mod, ioa, b"\x01" + _cp56(stamp))
                 for ioa in sorted(states)]
        return _i_frame(apdu_mod, 30, 3, 1, 1, blobs)

    def as_frame(raw):
        parsed = apdu_mod.parse_apdu(raw)
        return (0.0, "rx", raw, parsed)

    validator = FrameValidator(
        1, 1, currents, states, 0, periodic_send_s=5,
        suite_validate_frame=cases_periodic.validate_frame,
        suite_check_gi_frames=cases_mod._check_gi_frames,
        suite_ioas=cases_mod._ioas)
    validator.connected_since = 0.0
    records, findings = validator.feed(as_frame(currents_frame()))
    check("fv_partial_no_cycle",
          not records and not findings and
          validator.seen == currents)
    records, findings = validator.feed(as_frame(states_frame()))
    check("fv_cycle_complete",
          any(r["action"] == "periodic_cycle" for r in records) and
          not findings, "records=%s" % records)
    # Suret: tamamlayan cercevenin zamani 2020'de -> skew bulgusu
    validator.seen = set(currents)
    old = datetime.datetime(2020, 1, 2, 3, 4, 5)
    validator.seen = set()
    validator.feed(as_frame(currents_frame(old)))
    records, findings = validator.feed(as_frame(states_frame(old)))
    check("fv_time_skew",
          any(item[0] == "iec104_time_skew" for item in findings),
          "findings=%s" % findings)
    # Format ihlali: akim IOA'sina tip 30 gonderilir
    validator.seen = set()
    bad = _i_frame(apdu_mod, 30, 3, 1, 1,
                   [_blob(apdu_mod, 1001, b"\x01" + _cp56())])
    _, findings = validator.feed(as_frame(bad))
    check("fv_format_violation",
          any(item[0] == "iec104_format" for item in findings),
          "findings=%s" % findings)
    # Ariza uzayi ayrisimi
    fault = _i_frame(apdu_mod, 30, 3, 1, 1,
                     [_blob(apdu_mod, 100500, b"\x01" + _cp56())])
    records, _ = validator.feed(as_frame(fault))
    check("fv_fault_event",
          any(r["action"] == "fault_event" for r in records))
    # Kadans: son cevrimden 100 s gecti (periyot 5 s, sinir 75 s)
    validator.last_cycle_mono = time.monotonic() - 100.0
    stalled = validator.check_cadence()
    check("fv_cadence_stall",
          any(item[0] == "iec104_periodic_stalled" for item in stalled))
    validator.periodic_send_s = 0
    check("fv_cadence_disabled", not validator.check_cadence())
    # M_EI: baglanti kurulumunda sessiz, bagliyken kritik
    mei = _i_frame(apdu_mod, 70, 4, 1, 0, [_blob(apdu_mod, 0, b"\x00")])
    validator.connected_since = time.monotonic() - 300.0
    _, findings = validator.feed(as_frame(mei))
    check("fv_stack_restart",
          any(item[0] == "iec104_stack_restart" for item in findings))
    validator.connected_since = time.monotonic()
    _, findings = validator.feed(as_frame(mei))
    check("fv_mei_startup_ok", not findings)
    # GI dogrulama: eksiksiz istasyon seti ve eksik durum
    gi_currents = _i_frame(apdu_mod, 13, 20, 1, 1,
                           [_blob(apdu_mod, ioa,
                                  struct.pack("<f", 1.0) + b"\x00")
                            for ioa in sorted(currents)])
    gi_states = _i_frame(apdu_mod, 1, 20, 1, 1,
                         [_blob(apdu_mod, ioa, b"\x01")
                          for ioa in sorted(states)])
    ok, record, findings = validator.verify_gi(
        {"data": [as_frame(gi_currents), as_frame(gi_states)]})
    check("fv_gi_ok", ok and not findings and record["ioa_count"] == 12,
          "record=%s findings=%s" % (record, findings))
    ok, record, findings = validator.verify_gi(
        {"data": [as_frame(gi_currents)]})
    check("fv_gi_missing",
          not ok and any(item[0] == "iec104_gi_missing" for item in findings))


# ---- Faz 3: AY guvenligi, zincir yardimcilari, RF gecisleri, ozet --------


def test_ay_guard_and_budget():
    from probes.ay_stim import command_allowed, evaluate_budget
    check("ay_allow_evt", command_allowed("test evt") and
          command_allowed("test evt sim"))
    check("ay_block_trip", not command_allowed("test trip") and
          not command_allowed("test evt; test trip") and
          not command_allowed("help") and
          not command_allowed("test evt extra"))
    check("ay_budget_initial",
          evaluate_budget(0, 20, None, 180.0, None, 100.0) == (True, "ok"))
    check("ay_budget_gap",
          evaluate_budget(1, 20, 100.0, 180.0, None, 150.0) ==
          (False, "min gap bekleniyor"))
    check("ay_budget_gap_over",
          evaluate_budget(1, 20, 100.0, 180.0, None, 300.0) == (True, "ok"))
    check("ay_budget_exhausted",
          evaluate_budget(20, 20, None, 180.0, None, 100.0)[0] is False)
    check("ay_budget_disabled",
          evaluate_budget(0, 20, None, 180.0, "RED", 100.0) ==
          (False, "RED"))


def test_chain_helpers():
    from chains.event_chain import (faults_signature,
                                    jsonl_records_since)
    before = {"feeder": 1, "L1T": "kayit1\nkayit2\n", "L2T": "",
              "L3T": "", "L1P": "", "L2P": "", "L3P": "",
              "tc": [1, 0, 0], "pc": [0, 0, 0]}
    after_new = dict(before, L1T="kayit1\nkayit2\nkayit3\n", tc=[2, 0, 0])
    check("chain_faults_signature",
          faults_signature(before) != faults_signature(after_new) and
          faults_signature(before) == faults_signature(dict(before)))
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "iec104.jsonl"
        now = time.time()
        rows = [{"action": "fault_event", "wall": now - 50, "ioas": [100500]},
                {"action": "fault_event", "wall": now + 1, "ioas": [100501]},
                {"action": "periodic_cycle", "wall": now + 2}]
        with open(path, "w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(json.dumps(row) + "\n")
        found = jsonl_records_since(path, now, action="fault_event")
        check("chain_jsonl_since",
              len(found) == 1 and found[0]["ioas"] == [100501] and
              not jsonl_records_since(path, now + 10,
                                      action="fault_event") and
              not jsonl_records_since(Path(tmp) / "yok.jsonl", now))


def test_rf_transitions():
    from probes.rf_probe import diff_rf_state, extract_rf_state
    # Gerclesen bicim (rf_monitor_json.c:243-287)
    def line(online, fsm=0, stalled=0, trip=0, degraded=0):
        return {"lines": [{"LineId": 1, "Phases": [
            {"Phase": 1, "Online": online, "FsmError": fsm,
             "UptimeStalled": stalled, "TripFailedAlarm": trip},
            {"Phase": 2, "Online": online, "FsmError": fsm,
             "UptimeStalled": stalled, "TripFailedAlarm": trip},
            {"Phase": 3, "Online": online, "FsmError": fsm,
             "UptimeStalled": stalled, "TripFailedAlarm": trip}],
            "Maintenance": {"EventStoreDegraded": degraded}}]}
    before = extract_rf_state(line(1))
    after = extract_rf_state(line(0, fsm=3, stalled=1, trip=1, degraded=1))
    labels = {item[0] for item in diff_rf_state(1, before, after)}
    check("rf_transitions",
          labels == {"rf_offline", "rf_fsm_error", "rf_uptime_stalled",
                     "rf_trip_alarm", "rf_store_degraded"},
          "labels=%s" % labels)
    check("rf_no_transition", not diff_rf_state(1, before, before) and
          not diff_rf_state(1, None, after) and
          extract_rf_state({"baska": 1}) is None and
          extract_rf_state({"lines": []}) is None)
    from probes.modbus_map_check import cross_check_rf
    decoded = {"fider_kalite": 0x03, "faz": [{"kalite": 0x7F}] * 3}
    phases_ok = {"lines": [{"Phases": [{"Online": 1}] * 3}]}
    check("rf_cross_shape_guard",
          cross_check_rf(1, decoded, {"eski": 1}) == [] and
          cross_check_rf(1, decoded, phases_ok) == [] and
          len(cross_check_rf(1, decoded, line(0))) == 1 and
          cross_check_rf(1, decoded, line(1)) == [] and
          len(cross_check_rf(1, dict(decoded, fider_kalite=0x01),
                              line(1))) == 1)



def test_summarize():
    from summarize import build_summary
    with tempfile.TemporaryDirectory() as tmp:
        run_dir = Path(tmp)
        (run_dir / "manifest.json").write_text(json.dumps({
            "git_short": "abc1234", "git_dirty_files": 2, "host": "1.2.3.4",
            "seed": 7, "iec104_mode": "master"}), encoding="utf-8")
        (run_dir / "metrics.json").write_text(json.dumps({
            "state": "failed", "elapsed_hours": 1.25,
            "anomalies_critical": 1, "anomalies_warning": 2,
            "harness_errors": 0}), encoding="utf-8")
        with open(run_dir / "anomalies.jsonl", "w", encoding="utf-8") as out:
            out.write(json.dumps({"class": "hardfault", "severity":
                                  "critical", "wall": 1000.0}) + "\n")
            out.write(json.dumps({"class": "hardfault", "severity":
                                  "critical", "wall": 1100.0}) + "\n")
            out.write(json.dumps({"class": "web_failures", "severity":
                                  "critical", "wall": 1200.0}) + "\n")
        with open(run_dir / "chains.jsonl", "w", encoding="utf-8") as out:
            out.write(json.dumps({"action": "chain", "ok": True, "links": {
                "faults_web": {"ok": True, "ms": 52000.0},
                "fltlog": {"ok": True, "ms": 54000.0},
                "iec104": {"ok": False, "ms": None},
                "modbus": {"ok": True, "ms": 60000.0}}}) + "\n")
        with open(run_dir / "iec104.jsonl", "w", encoding="utf-8") as out:
            out.write(json.dumps({"action": "periodic_cycle",
                                  "cycle_ms": 60000.0}) + "\n")
            out.write(json.dumps({"action": "gi", "ok": True}) + "\n")
            out.write(json.dumps({"action": "gi", "ok": None,
                                  "skip": "no_in_use_lines"}) + "\n")
        evidence = run_dir / "evidence" / "001-hardfault"
        evidence.mkdir(parents=True)
        text = build_summary(run_dir)
        check("summarize_content",
              "abc1234" in text and "hardfault" in text and "| 2 |" in text
              and "1 basarili, 0 basarisiz, 1 skip" in text
              and "001-hardfault" in text and
              "60" in text,
              text[:400])


def test_worker_reconnect_and_gi():
    from types import SimpleNamespace
    from probes.iec104_master_worker import Iec104MasterWorker

    calls = []
    records = []
    findings = []
    worker = Iec104MasterWorker.__new__(Iec104MasterWorker)
    worker.master = SimpleNamespace(is_connected=True)
    check("reconnect_keeps_open_connection", worker._reconnect())

    def connect(timeout_s):
        calls.append(("connect", timeout_s))
        worker.master.is_connected = True
        return False

    worker.master.is_connected = False
    worker.master.connect = connect
    check("reconnect_keeps_connection_opened_during_wait",
          worker._reconnect() and calls == [("connect", 30.0)])
    worker.writer = SimpleNamespace(write=records.append)
    worker.detector = SimpleNamespace(report=lambda *args: findings.append(args))
    worker.has_in_use_lines = False
    worker.gi_tour()
    check("gi_no_lines_is_skip", records[-1]["ok"] is None and
          records[-1]["skip"] == "no_in_use_lines")

    worker.has_in_use_lines = True
    worker.master.interrogate = lambda qualifier: False
    worker.gi_tour()
    check("gi_send_failure_is_recorded", records[-1]["ok"] is False and
          records[-1]["stage"] == "send" and findings[-1][0] == "iec104_gi_failed")

    calls.clear()
    worker.master.interrogate = lambda qualifier: calls.append("send") or True

    def collect(master, start, qualifier, data_deadline_s):
        calls.append("collect")
        return True, {"stage": "done"}

    worker.modules = {"cases": SimpleNamespace(_collect_interrogation=collect)}
    worker.validator = SimpleNamespace(verify_gi=lambda detail:
                                      (True, {"action": "gi", "ok": True}, []))
    worker.gi_tour()
    check("gi_sends_before_collecting", calls == ["send", "collect"] and records[-1]["ok"] is True)


def test_setup_failure_runs_shutdown():
    from run_monitor import DeviceMonitor

    calls = []
    monitor = DeviceMonitor.__new__(DeviceMonitor)
    monitor.abort_reason = None

    def fail_setup():
        raise RuntimeError("test setup failure")

    monitor.setup = fail_setup
    monitor.shutdown = lambda: calls.append("shutdown")
    try:
        monitor.run()
    except RuntimeError:
        check("setup_failure_closes_resources", calls == ["shutdown"] and
              "test setup failure" in monitor.abort_reason)
    else:
        check("setup_failure_closes_resources", False, "setup failure was swallowed")


def main():
    tests = [
        test_recorder_rotation,
        test_scheduler,
        test_anomaly,
        test_trim,
        test_evidence,
        test_modbus_classify,
        test_module_imports,
        test_map_constants,
        test_map_decode_and_checks,
        test_web_schema,
        test_frame_validator,
        test_ay_guard_and_budget,
        test_chain_helpers,
        test_rf_transitions,
        test_summarize,
        test_worker_reconnect_and_gi,
        test_setup_failure_runs_shutdown,
    ]
    for test in tests:
        test()
    failed = [name for name, ok in RESULTS if not ok]
    print("\n%d/%d gecti" % (len(RESULTS) - len(failed), len(RESULTS)))
    if failed:
        print("KALAN: %s" % ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
