"""Exercise observation-only inventory checks without a physical device."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
import importlib.util
import os
import sys
import unittest
from unittest.mock import Mock, patch
from tempfile import TemporaryDirectory
import struct
import re
import time
import xml.etree.ElementTree as ET

import run_smoke

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    "rf_real_mh_gate", ROOT / "test/integration/rf_real_mh/run_tests.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class FakeConsole:
    def __init__(self, lines):
        self.lines = lines
        self.commands = []

    def mark(self):
        pass

    def send(self, command):
        self.commands.append(command)

    def new_lines(self):
        return [(0, line) for line in self.lines]


class SmokeTests(unittest.TestCase):
    def test_configuration_requires_applied_full_bitmap_and_matching_crc(self):
        base = ["RTU group=250 feeder=1 state=APPLIED writes=3 expected_crc=0xD07F",
                "MH state=APPLIED bitmap=0x07 reason=0 cfg_crc=0xD07F"]
        self.assertEqual("PASS", run_smoke.configuration_result(base,250)[0])
        for old,new in (("state=APPLIED","state=FAILED"), ("writes=3","writes=2"),
                        ("bitmap=0x07","bitmap=0x04"), ("reason=0","reason=1"),
                        ("cfg_crc=0xD07F","cfg_crc=0x0000")):
            with self.subTest(old=old):
                changed = [line.replace(old,new) for line in base]
                self.assertNotEqual("PASS", run_smoke.configuration_result(changed,250)[0])

    def test_wrong_group_or_ack_201_without_snapshot_cannot_pass(self):
        lines = ["ACK CFG_COMMIT seq=10", "201 campaign complete"]
        self.assertIsNone(run_smoke.configuration_result(lines,250))
        lines = ["RTU group=249 feeder=1 state=APPLIED writes=3 expected_crc=0x1234",
                 "MH state=APPLIED bitmap=0x07 reason=0 cfg_crc=0x1234"]
        self.assertIsNone(run_smoke.configuration_result(lines,250))

    def test_bootless_reset_uses_supported_under_reset_token_and_reports_failure(self):
        console = Mock()
        ctx = SimpleNamespace(args=SimpleNamespace(reset_dut=True),
                              console=console)
        def reset_failed(*args, **kwargs):
            console.mark.assert_called_once()
            return SimpleNamespace(returncode=1, stderr="reset failed")

        with patch.object(run_smoke.subprocess, "run",
                          side_effect=reset_failed) as reset:
            verdict, _ = run_smoke.m6_bootless(ctx)
        self.assertEqual("ERROR", verdict)
        command = reset.call_args.args[0]
        self.assertIn("mode=UR", command)
        self.assertIn("reset=SWRST", command)
        console.send.assert_not_called()

    def run_main_logging(self, acknowledgements):
        console = Mock()
        console.send_and_wait.side_effect = acknowledgements
        check = Mock(return_value=("PASS", ["observed"]))
        temporary_root = ROOT / ".temp_files"
        temporary_root.mkdir(exist_ok=True)
        with TemporaryDirectory(dir=temporary_root) as directory:
            argv = ["run_smoke.py", "--console", "TEST",
                    "--case", "logging_check", "--outdir", directory,
                    "--require-device"]
            with patch.object(sys, "argv", argv), \
                    patch.object(run_smoke, "ConsoleSession",
                                 return_value=console), \
                    patch.object(run_smoke, "dut_profile", return_value=0), \
                    patch.dict(run_smoke.CASES,
                               {"logging_check": (check, "test")}, clear=True), \
                    redirect_stdout(StringIO()):
                result = run_smoke.main()
            report = (Path(directory) / "results.json").read_text()
        return result, console, check, report

    def test_runner_waits_for_logging_acknowledgements_before_cases(self):
        result, console, check, _ = self.run_main_logging([True, True])
        self.assertEqual(0, result)
        self.assertEqual(["cslog on", "rf log verbose"],
                         [call.args[0] for call in
                          console.send_and_wait.call_args_list])
        check.assert_called_once()
        console.close.assert_called_once()

    def test_logging_setup_failure_blocks_cases_instead_of_false_live_failure(self):
        for acknowledgements in ([None], [True, None]):
            with self.subTest(acknowledgements=acknowledgements):
                result, console, check, report = self.run_main_logging(
                    acknowledgements)
                self.assertEqual(1, result)
                check.assert_not_called()
                self.assertIn('"BLOCKED"', report)
                console.close.assert_called_once()

    def quality(self, qds=0, timestamp=b"\x00\x00\x00\x00\x01\x01\x1a",
                terminate=True, cot=21):
        sys.path.insert(0, str(ROOT / "test/system/iec104_master"))
        import apdu
        queries = []

        class Master:
            def __init__(self, *args, **kwargs):
                self.frames = []

            def start(self):
                pass

            def connect(self, **kwargs):
                return True

            def interrogate(self, qoi):
                queries.append(qoi)
                bodies = [bytes.fromhex("64 01 07 00 01 00 00 00 00 15"),
                          bytes((36, 1, cot, 0, 1, 0, 6, 4, 0)) +
                          struct.pack("<f", 12.5) + bytes((qds,)) + timestamp]
                if terminate:
                    bodies.append(bytes.fromhex(
                        "64 01 0a 00 01 00 00 00 00 15"))
                start = time.monotonic()
                for index, body in enumerate(bodies):
                    raw = bytes((0x68, len(body) + 4, index * 2, 0, 0, 0)) + body
                    self.frames.append((start + index * 0.01, "rx", raw,
                                        apdu.parse_apdu(raw)))
                return True

            def wait(self, predicate, timeout_s, t0=None):
                return next(((True, frame) for frame in self.frames
                             if frame[0] >= t0 and predicate(frame)),
                            (False, None))

            def since(self, t0):
                return [frame for frame in self.frames if frame[0] >= t0]

            def disconnect(self):
                pass

            def stop(self):
                pass

        ctx = SimpleNamespace(args=SimpleNamespace(host_104="127.0.0.1",
                              port_104=2404))
        with patch.dict(sys.modules, {"master104": SimpleNamespace(Master104=Master)}):
            with patch.object(run_smoke, "observe", return_value=["LIVE_DATA"]):
                verdict = run_smoke.m7_asdu_quality(ctx)[0]
        return verdict, queries

    def test_quality_uses_timed_group_one(self):
        self.assertEqual(("PASS", [21]), self.quality())

    def test_quality_rejects_invalid_measurements_and_timestamps(self):
        self.assertEqual("FAIL", self.quality(qds=0x80)[0])
        self.assertEqual("FAIL", self.quality(
            timestamp=b"\x00\x00\x80\x00\x01\x01\x1a")[0])
        self.assertEqual("FAIL", self.quality(timestamp=b"")[0])

    def test_quality_requires_termination_and_matching_cot(self):
        self.assertEqual("FAIL", self.quality(terminate=False)[0])
        self.assertEqual("FAIL", self.quality(cot=20)[0])

    def test_powerboard_scp_build_keeps_i2c_disabled_in_both_configurations(self):
        project = ET.parse(ROOT / ".cproject").getroot()
        found = set()
        for config in project.findall(".//configuration"):
            name = config.get("name")
            if name not in ("Debug", "Release"):
                continue
            entries = config.findall("./sourceEntries/entry")
            application = [entry for entry in entries
                           if entry.get("name") == "Application"]
            self.assertEqual(1, len(application))
            self.assertIn("power_board/i2c_slave.c",
                          application[0].get("excluding", "").split("|"))
            found.add(name)
        self.assertEqual({"Debug", "Release"}, found)
        hal = (ROOT / "Core/Inc/stm32u3xx_hal_conf.h").read_text()
        self.assertIsNone(re.search(
            r"^\s*#define\s+HAL_I2C_MODULE_ENABLED(?:\s|$)", hal, re.M))

    @staticmethod
    def frame(cmd, body, seq=7, kind=3):
        header = bytes((2, 1, kind, cmd, seq, len(body), len(body) ^ 255))
        raw = header + body
        raw += struct.pack("<H", run_smoke._crc16_ccitt_false(raw))
        return "RF RX[%d]: [%s]" % (len(raw), raw.hex(" "))

    def consume(self, lines):
        ctx = SimpleNamespace(args=SimpleNamespace(duration=1))
        with patch.object(run_smoke, "observe", return_value=lines):
            return run_smoke.m4_ring_consume(ctx)[0]

    def test_consume_request_without_ack_is_not_success(self):
        self.assertEqual("FAIL", self.consume([
            "[RF ST->RF] cmd=0x48 seq=7 gonderildi"]))

    def test_empty_protection_ack_is_not_record_consumption(self):
        head = self.frame(0x40, struct.pack("<HHIH", 10, 1, 110, 10))
        ack = self.frame(0x48, struct.pack("<HHI", 10, 0, 110))
        self.assertEqual("SKIP", self.consume([
            head, "[RF ST->RF] cmd=0x48 seq=7 gonderildi", ack]))

    def test_matched_ack_after_nonempty_head_proves_ordinal_progress(self):
        head = self.frame(0x40, struct.pack("<HHIH", 10, 1, 110, 8))
        ack = self.frame(0x48, struct.pack("<HHI", 10, 0, 110))
        self.assertEqual("PASS", self.consume([
            head, "[RF ST->RF] cmd=0x48 seq=7 gonderildi", ack]))

    def test_wrong_seq_error_or_backward_ack_cannot_pass_consumption(self):
        head = self.frame(0x40, struct.pack("<HHIH", 10, 1, 110, 8))
        for ack in (self.frame(0x48, struct.pack("<HHI", 10, 0, 110), seq=8),
                    self.frame(0x48, bytes((2,)), kind=4),
                    self.frame(0x48, struct.pack("<HHI", 7, 3, 107)),
                    self.frame(0x48, struct.pack("<HHI", 9, 1, 110))):
            with self.subTest(ack=ack):
                self.assertEqual("FAIL", self.consume([
                    head, "[RF ST->RF] cmd=0x48 seq=7 gonderildi", ack]))

    def test_ack_without_initial_head_is_insufficient_evidence(self):
        self.assertEqual("FAIL", self.consume([
            "[RF ST->RF] cmd=0x48 seq=7 gonderildi",
            self.frame(0x48, struct.pack("<HHI", 10, 0, 110))]))

    def test_ring_wrap_ack_uses_ordinal_and_modular_slot(self):
        head = self.frame(0x40, struct.pack("<HHIH", 1, 0, 6553601, 99))
        ack = self.frame(0x48, struct.pack("<HHI", 1, 0, 6553601))
        self.assertEqual("PASS", self.consume([
            head, "[RF ST->RF] cmd=0x48 seq=7 gonderildi", ack]))

    def test_central_gate_runs_parser_regressions_before_device_selection(self):
        with patch.dict(os.environ, {}, clear=True):
            with patch.object(gate.subprocess, "call", return_value=0) as call:
                self.assertEqual(0, gate.main())
        paths = [Path(item.args[0][1]).name for item in call.call_args_list]
        self.assertEqual(["test_smoke_selftest.py", "test_parser_selftest.py"],
                         paths)

    def test_parser_regression_failure_stops_device_execution(self):
        with patch.dict(os.environ, {"RF_REAL_MH_CONSOLE": "COM16"}, clear=True):
            with patch.object(gate.subprocess, "call", side_effect=[0, 1]):
                with patch.object(gate.subprocess, "run") as run:
                    self.assertEqual(1, gate.main())
                    run.assert_not_called()

    @staticmethod
    def epoch_record(code, phase=0, boot=0, mask=3, feeder=1, zone=1):
        raw = bytearray(60)
        raw[7:11] = bytes((code, zone, feeder, phase))
        struct.pack_into("<H", raw, 41, mask)
        struct.pack_into("<H", raw, 49, boot)
        struct.pack_into("<H", raw, 56, 0 if code == 201 else 0x1787)
        struct.pack_into("<H", raw, 58,
                         run_smoke._crc16_ccitt_false(raw[:58]))
        return bytes(raw)

    def test_epoch_summary_does_not_require_120_or_matching_boot(self):
        summary = self.epoch_record(201)
        first = self.epoch_record(120, phase=1, boot=7)
        second = self.epoch_record(120, phase=2, boot=20)
        self.assertEqual(summary, run_smoke.epoch_campaign_summary([summary]))
        self.assertIsNone(run_smoke.epoch_campaign_summary([first, second]))
        self.assertEqual(summary, run_smoke.epoch_campaign_summary(
            [summary, first, first]))
        for bad in (self.epoch_record(120, phase=2, boot=19),
                    self.epoch_record(120, phase=2, boot=20, feeder=2),
                    self.epoch_record(120, phase=2, boot=20, zone=2)):
            self.assertEqual(summary, run_smoke.epoch_campaign_summary(
                [summary, first, bad]))

    def test_epoch_partial_masks_are_observations_not_final_success(self):
        marks = [self.epoch_record(120, phase=1, boot=7),
                 self.epoch_record(120, phase=2, boot=20)]
        for mask in (0, 1, 7):
            summary = self.epoch_record(201, mask=mask)
            self.assertEqual(summary, run_smoke.epoch_campaign_summary(
                [summary] + marks))
        for summary in (self.epoch_record(201, mask=8),
                        self.epoch_record(201, feeder=2),
                        self.epoch_record(120)):
            self.assertIsNone(run_smoke.epoch_campaign_summary([summary]))

    def run_epoch(self, records, ack_seq=7):
        live = bytearray(33)
        live[0] = 5
        struct.pack_into("<H", live, 31, 7)
        before = [self.frame(0x11, live, kind=2)]
        lines = ["[RF ST->RF] cmd=0x2A seq=7 gonderildi",
                 self.frame(0x2A, b"", seq=ack_seq)]
        lines += [self.frame(0x44, record) for record in records]
        ctx = SimpleNamespace(args=SimpleNamespace(epoch=True,
                              admin_pass="test"), console=FakeConsole(lines))
        with patch.object(run_smoke, "observe", return_value=before):
            with patch.object(run_smoke.time, "sleep"):
                with patch.object(run_smoke.time, "monotonic",
                                  side_effect=[0, 0, 101]):
                    return run_smoke.m5_epoch_201(ctx)[0]

    def test_actual_epoch_case_observes_summary_without_requiring_120(self):
        self.assertEqual("PASS", self.run_epoch([
            self.epoch_record(201, mask=1)]))

    def test_actual_epoch_case_passes_after_summary_and_matching_phase(self):
        self.assertEqual("PASS", self.run_epoch([
            self.epoch_record(201, mask=1),
            self.epoch_record(120, phase=1, boot=7)]))

    def test_actual_epoch_case_requires_matching_command_ack(self):
        self.assertEqual("FAIL", self.run_epoch([
            self.epoch_record(201, mask=1),
            self.epoch_record(120, phase=1, boot=7)], ack_seq=8))

    def check_inventory(self, lines):
        console = FakeConsole(lines)
        ctx = SimpleNamespace(console=console, evidence=[])
        with patch.object(run_smoke.time, "sleep"):
            verdict, details = run_smoke.m2_inventory(ctx)
        return console.commands, verdict, details

    def test_inventory_observation_never_reloads_or_reports_fake_count(self):
        commands, verdict, details = self.check_inventory(
            ["Envanter: YUKLU", "Inventory wait: none"])
        self.assertEqual("PASS", verdict)
        self.assertEqual(["rf status"], commands)
        self.assertNotIn("giris satiri", " ".join(details))

    def test_loaded_word_in_unrelated_output_is_not_inventory_success(self):
        commands, verdict, _ = self.check_inventory(
            ["Other cache: YUKLU", "Envanter: YUKLENIYOR"])
        self.assertEqual("FAIL", verdict)
        self.assertEqual(["rf status"], commands)

    def test_inventory_missing_or_error_state_is_not_success(self):
        for lines in ([], ["Envanter: HATA"], ["Envanter: BOS"]):
            with self.subTest(lines=lines):
                commands, verdict, _ = self.check_inventory(lines)
                self.assertEqual("FAIL", verdict)
                self.assertEqual(["rf status"], commands)

    def test_central_host_argument_is_accepted_by_actual_runner_parser(self):
        def run_command(command):
            with patch.object(sys, "argv", command[1:] + ["--list"]):
                with redirect_stdout(StringIO()):
                    result = run_smoke.main()
            self.assertEqual(0, result)
            return SimpleNamespace(returncode=result)

        environment = {"RF_REAL_MH_CONSOLE": "COM16",
                       "RF_REAL_MH_HOST": "127.0.0.1"}
        with patch.dict(os.environ, environment, clear=True):
            with patch.object(gate.subprocess, "call", return_value=0):
                with patch.object(gate.subprocess, "run", run_command):
                    self.assertEqual(0, gate.main())


if __name__ == "__main__":
    unittest.main()
