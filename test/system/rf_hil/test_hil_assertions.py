"""Host checks for the actual alarm HIL verdicts; no ports are opened."""
import re
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import cases_bq
import cases
import cases_findings
import cases_recovery
from checks import TraceChecks


class ResetBoundaryTests(unittest.TestCase):
    def test_reset_uses_under_reset_and_propagates_programmer_failure(self):
        error = cases_findings.subprocess.CalledProcessError(1, "programmer")
        with patch.object(cases_findings.subprocess, "run",
                          side_effect=error) as reset:
            with self.assertRaises(cases_findings.subprocess.CalledProcessError):
                cases_findings.dut_reset()
        command = reset.call_args.args[0]
        self.assertIn("mode=UR", command)
        self.assertIn("reset=SWRST", command)
        self.assertTrue(reset.call_args.kwargs["check"])


class LiveTimeoutVerdictTests(unittest.TestCase):
    def check_timeout(self, response, stalled=False):
        class Console:
            def __init__(self):
                self.fresh = []

            def send(self, command):
                pass

            def mark(self):
                self.fresh = []

            def drain(self, seconds):
                return []

            def all_lines(self):
                return [(0, "RF baglantisi : OFFLINE / TAZE LIVE YOK"),
                        (0, "Log wrap low : 0")] + self.fresh

            def new_lines(self):
                return self.fresh

            def send_and_wait(self, command, pattern, timeout_s):
                self.fresh = [(1, line) for line in response]
                for timestamp, line in self.fresh:
                    match = re.search(pattern, line)
                    if match:
                        return timestamp, line, match
                return None

        console = Console()
        ctx = SimpleNamespace(console=console, evidence=[],
                              sim=SimpleNamespace(call=lambda **kwargs: None))
        trace = TraceChecks([{"event": "tx", "cmd": 0x11,
                              "kind": "notify", "data_hex":
                              (bytes([5, 1, 0, 0, 0, 1]) + bytes(25) +
                               bytes([7, 0])).hex()}])
        with patch.object(cases, "wait_upload_complete"), \
                patch.object(cases, "refresh", return_value=trace), \
                patch.object(cases_recovery, "wait_upload_complete"), \
                patch.object(cases_recovery, "refresh", return_value=trace), \
                patch.object(cases.time, "sleep"):
            return (cases_recovery.stalled_uptime(ctx) if stalled
                    else cases.c2(ctx))

    def test_actual_timeout_case_accepts_current_offline_sample_output(self):
        result = self.check_timeout([
            "RF baglantisi : OFFLINE / TAZE LIVE YOK",
            "Son LIVE yasi : 36299 ms", "Log wrap low : 0"])
        self.assertIn("live stopped", result)

    def test_actual_timeout_case_rejects_online_even_with_a_sample(self):
        with self.assertRaises((AssertionError, cases.CheckError)):
            self.check_timeout(["RF baglantisi : ONLINE",
                                "Son LIVE yasi : 10 ms", "Log wrap low : 0"])

    def test_missing_sample_cannot_reuse_old_console_offline_lines(self):
        with self.assertRaises((AssertionError, cases.CheckError)):
            self.check_timeout(["RF baglantisi : OFFLINE / TAZE LIVE YOK",
                                "LIVE_DATA : Henuz alinmadi"])

    def test_actual_stalled_case_accepts_raw_or_nan_invalid_current(self):
        for measurement in ("12.500 A [GECERSIZ]", "GECERSIZ (NaN/Inf)"):
            with self.subTest(measurement=measurement):
                self.assertEqual("Current invalid; RF online",
                                 self.check_timeout([
                                     "RF baglantisi : ONLINE",
                                     "Irms : " + measurement,
                                     "Log wrap low : 0"], stalled=True))

    def test_actual_stalled_case_rejects_current_still_marked_valid(self):
        with self.assertRaises(cases.CheckError):
            self.check_timeout(["RF baglantisi : ONLINE",
                                "Irms : 12.500 A [GECERLI]",
                                "Log wrap low : 0"], stalled=True)

    def test_actual_stalled_case_rejects_offline_invalid_measurement(self):
        with self.assertRaises(cases.CheckError):
            self.check_timeout(["RF baglantisi : OFFLINE / TAZE LIVE YOK",
                                "Irms : 12.500 A [GECERSIZ]",
                                "Log wrap low : 0"], stalled=True)


class AlarmConsole:
    def __init__(self, delta, alarm=True, latched=0):
        self.count = 10
        self.delta = delta
        self.alarm = alarm
        self.latched = latched

    def send(self, command):
        return None

    def drain(self, seconds):
        return [(1, "background log"), (2, " ")]

    def send_and_wait(self, command, pattern, **kwargs):
        if command == "rf cfg-apply 1 0":
            text = "Config not started: check members"
        elif command.startswith("fltlog"):
            text = "Phase 1 - PERMANENT FAULTS (total: %d, showing: 1):" % self.count
        elif command == "iec104evtlog status":
            text = "next_seq : 21"
        elif command.startswith("iec104evtlog dump"):
            text = "seq=21 alarm F1 Ph1 active=%d" % self.alarm
        else:
            text = "  Acma alarmi        : 0 (latched=%d)" % self.latched
        match = re.search(pattern, text)
        return (0, text, match) if match else None


class AlarmSim:
    def __init__(self, console, has_range=True):
        self.console = console
        self.has_range = has_range

    def ensure_alive(self):
        return False

    def call(self, **request):
        if request["do"] == "add_events":
            self.console.count += self.console.delta
            return {"ok": True}
        return {"state": {"ring": {"pending": 0}}}


class AlarmVerdictTests(unittest.TestCase):
    def test_group_zero_handles_console_timestamp_tuples(self):
        from unittest.mock import patch
        ctx = SimpleNamespace(console=AlarmConsole(0), sim=AlarmSim(None),
                              evidence=[], trace_path="fake-trace.jsonl")
        records = [{"event": "rx", "cmd": 5}]
        with patch.object(cases, "load_trace_safe", return_value=records), \
             patch.object(cases_bq.time, "sleep"):
            result = cases_bq.bq11_gid_zero(ctx)
        self.assertIn("gid=0 rejected", result)
        self.assertEqual(["background log"], ctx.evidence)

    def run_event(self, delta, expected, alarm=True, latched=0, has_range=True):
        from unittest.mock import patch
        console = AlarmConsole(delta, alarm, latched)
        sim = AlarmSim(console, has_range)
        ctx = SimpleNamespace(console=console, sim=sim, evidence=[],
                              trace_path="fake-trace.jsonl")
        trace = TraceChecks([{"event": "rx", "cmd": 5}]
                            + ([{"event": "rx", "cmd": 0x44}]
                               if has_range else []))
        # Only trace I/O is replaced; production HIL assertions execute.
        with patch.object(cases, "load_trace_safe", return_value=trace.records):
            return cases_bq.check_alarm_event(ctx, 101, 1, expected)

    def test_permanent_event_requires_increment_and_persistent_alarm(self):
        self.assertIn("permanent delta=1", self.run_event(1, 1))

    def test_alarm_only_event_preserves_permanent_count(self):
        self.assertIn("permanent delta=0", self.run_event(0, 0))

    def test_missing_fault_increment_fails(self):
        with self.assertRaises(AssertionError):
            self.run_event(0, 1)

    def test_alarm_label_with_zero_value_cannot_pass(self):
        with self.assertRaises(AssertionError):
            self.run_event(1, 1, alarm=False)

    def test_boot_only_trace_cannot_pass(self):
        with self.assertRaises(AssertionError):
            self.run_event(1, 1, has_range=False)

    def test_unacknowledged_latch_cannot_pass(self):
        with self.assertRaises(AssertionError):
            self.run_event(1, 1, latched=1)

    def test_boot_consume_zero_cannot_hide_later_pending_records(self):
        trace = TraceChecks([
            {"event": "tx", "cmd": 0x46, "kind": "reply",
             "data_hex": "00000000"},
            {"event": "tx", "cmd": 0x46, "kind": "reply",
             "data_hex": "03000200"}])
        self.assertFalse(cases._left_zero(trace))

    def test_last_consume_zero_confirms_completion(self):
        trace = TraceChecks([
            {"event": "tx", "cmd": 0x46, "kind": "reply",
             "data_hex": "03000200"},
            {"event": "tx", "cmd": 0x46, "kind": "reply",
             "data_hex": "05000000"}])
        self.assertTrue(cases._left_zero(trace))


if __name__ == "__main__":
    unittest.main()
