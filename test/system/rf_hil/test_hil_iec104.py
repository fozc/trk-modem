"""The RF-to-SCADA hardware verdict must reject missing/wrong wire data."""
import struct
import re
import sys
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import cases_iec104

sys.path.append(str(Path(__file__).resolve().parents[1] / "iec104_master"))
import apdu


class Master:
    def __init__(self, asdus):
        self.frames = [(time.monotonic() + 1 + i, "rx", b"",
                        {"asdu": apdu.parse_asdu(body)})
                       for i, body in enumerate(asdus)]

    def wait(self, predicate, timeout, t0):
        return next(((True, f) for f in self.frames if f[0] >= t0 and
                     predicate(f)), (False, None))

    def since(self, started):
        return [f for f in self.frames if f[0] >= started]

    def stop(self):
        pass


class HILWireTests(unittest.TestCase):
    def drain(self, pending, stuck=False, missing=False):
        clock = SimpleNamespace(now=0.0)
        console = Mock()

        def status(command, pattern, timeout_s):
            if missing:
                return None
            count = pending if stuck else max(0, pending - int(clock.now / 11))
            text = "unsent : %d" % count
            return clock.now, text, re.search(pattern, text)

        console.send_and_wait.side_effect = status
        ctx = SimpleNamespace(console=console, evidence=[],
                              sim=SimpleNamespace(call=lambda **kwargs:
                                                  {"state": {"ring": {"pending": 0}}}))
        master = SimpleNamespace(confirm_interval_s=10)

        def advance(seconds):
            clock.now += seconds
            self.assertLess(clock.now, 500, "unbounded backlog wait")

        with patch.object(cases_iec104, "refresh"), \
                patch.object(cases_iec104.time, "monotonic",
                             side_effect=lambda: clock.now), \
                patch.object(cases_iec104.time, "sleep", side_effect=advance):
            cases_iec104._drained(ctx, master)
        return clock.now

    def test_backlog_larger_than_ninety_second_ack_window_can_drain(self):
        self.assertGreaterEqual(self.drain(20), 220)

    def test_stuck_backlog_has_a_fixed_finite_deadline(self):
        with self.assertRaisesRegex(AssertionError, "backlog did not drain"):
            self.drain(2, stuck=True)

    def test_missing_status_cannot_be_assumed_empty(self):
        with self.assertRaises(AssertionError):
            self.drain(0, missing=True)

    def fault(self, current=1250, duration=120, invalid_time=False, cot=3):
        stamp = bytes((0, 0, 0x80 if invalid_time else 0, 12, 19, 10, 26))
        body = bytes((36, 2, cot, 0, 1, 0))
        for ioa, value in ((1030, current), (1031, duration)):
            body += ioa.to_bytes(3, "little") + struct.pack("<f", value) + b"\0" + stamp
        return body

    def execute(self, function, bodies):
        ctx = SimpleNamespace(sim=Mock())
        master = Master(bodies)
        snapshot = {"ca": 1, "lines": [{"ioa": {"trip": [1070, 1071, 1072]}}]}
        with patch.object(cases_iec104, "wait_upload_complete"):
            with patch.object(cases_iec104, "_drained"):
                with patch.object(cases_iec104, "_source_boot", return_value=7):
                    with patch.object(cases_iec104, "_connect",
                                      return_value=(master, Mock(), snapshot)):
                        return function(ctx)

    def test_correct_new_fault_passes(self):
        self.assertIn("COT3", self.execute(cases_iec104.s1_fault, [self.fault()]))

    def test_wrong_values_or_timestamp_fail(self):
        for body in (self.fault(current=0), self.fault(duration=0),
                     self.fault(invalid_time=True)):
            with self.subTest(body=body), self.assertRaises(AssertionError):
                self.execute(cases_iec104.s1_fault, [body])

    def test_solicited_or_missing_event_cannot_pass_spontaneous_case(self):
        for bodies in ([], [self.fault(cot=21)]):
            with self.subTest(bodies=bodies), self.assertRaises(AssertionError):
                self.execute(cases_iec104.s1_fault, bodies)

    def test_active_and_cleared_are_both_required(self):
        def alarm(value):
            return bytes((30, 1, 3, 0, 1, 0)) + (1070).to_bytes(3, "little") + bytes((value,)) + bytes(7)
        self.assertIn("CLEARED", self.execute(cases_iec104.s2_alarm,
                                              [alarm(1), alarm(0)]))
        with self.assertRaises(AssertionError):
            self.execute(cases_iec104.s2_alarm, [alarm(1)])


if __name__ == "__main__":
    unittest.main()
