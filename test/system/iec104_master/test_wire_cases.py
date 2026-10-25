"""Exercise actual device-case verdicts with scripted transport responses."""
from types import SimpleNamespace
from unittest.mock import Mock, patch
import time
import socket
import unittest

import cases_wire


class NegativeCases(unittest.TestCase):
    def run_negative(self, positive=False, missing=False, wrong_cot=False):
        class Wire:
            def __init__(self, *args, **kwargs):
                self.sent = []

            def connect(self):
                return True

            def command(self, type_id, payload, cot, ca):
                rejection = (44 if type_id in (45, 46, 48, 49, 50) else
                             46 if ca is not None else 45 if cot == 20 else 7)
                self.asdu = {"type": type_id, "pn": 0 if positive else 1,
                             "cot": 3 if wrong_cot else rejection}
                self.sent.append(type_id)

            def wait(self, predicate, timeout, t0):
                frame = (time.monotonic(), "rx", b"", {"asdu": self.asdu})
                if not missing and predicate(frame):
                    return True, frame
                return False, None

            def stop(self):
                pass

        ctx = SimpleNamespace(master=SimpleNamespace(host="unused", port=0,
                              evidence=None, stop=Mock()), oracle={"ca": 1})
        with patch.object(cases_wire, "WireMaster", Wire):
            return cases_wire.j01_negative_commands(ctx)

    def test_all_expected_rejections_pass(self):
        verdict, lines = self.run_negative()
        self.assertEqual("PASS", verdict)
        self.assertEqual(10, len(lines))

    def test_positive_ack_cannot_pass_negative_case(self):
        self.assertEqual("FAIL", self.run_negative(positive=True)[0])

    def test_missing_response_cannot_pass_negative_case(self):
        self.assertEqual("FAIL", self.run_negative(missing=True)[0])

    def test_wrong_rejection_reason_cannot_pass(self):
        self.assertEqual("FAIL", self.run_negative(wrong_cot=True)[0])

    def test_ack_cases_require_explicit_fixture_permission(self):
        ctx = SimpleNamespace(args=SimpleNamespace(mutate_eventlog=False))
        self.assertEqual("SKIP", cases_wire.j02_withheld_ack(ctx)[0])
        self.assertEqual("SKIP", cases_wire.j03_lost_ack_reconnect(ctx)[0])
        self.assertEqual("SKIP", cases_wire.j08_window_and_reset(ctx)[0])

    def test_handoff_waits_for_remote_listener_readiness(self):
        ctx = SimpleNamespace(master=SimpleNamespace(host="unused", port=0,
                              evidence=None, stop=Mock()), oracle={"ca": 1},
                              console=object())
        with patch.object(cases_wire, "WireMaster") as factory:
            factory.return_value.connect.return_value = True

            def status(*args, **kwargs):
                factory.assert_not_called()
                return "IEC104 LISTENING IDLE"

            with patch.object(cases_wire.console_io, "run_cmd", side_effect=status):
                cases_wire._open(ctx)
        factory.assert_called_once()

    def run_clock(self, positive=False, jump=False):
        wire = Mock()

        def response(predicate, timeout, t0):
            frame = (time.monotonic(), "rx", b"",
                     {"asdu": {"type": 103, "pn": 0 if positive else 1}})
            return (True, frame) if predicate(frame) else (False, None)

        wire.wait.side_effect = response
        ctx = SimpleNamespace(console=object())
        before = (2026, 10, 19, 14, 0, 0)
        after = (2026, 10, 19, 12 if jump else 14, 0, 1)
        with patch.object(cases_wire, "_open", return_value=wire):
            with patch.object(cases_wire.console_io, "run_cmd"):
                with patch.object(cases_wire.console_io, "latest_console_time",
                                  side_effect=[before, after]):
                    return cases_wire.j04_invalid_clock(ctx)[0]

    def test_invalid_clock_rejections_preserve_observed_rtc(self):
        self.assertEqual("PASS", self.run_clock())

    def test_positive_clock_response_or_rtc_jump_fails_case(self):
        self.assertEqual("FAIL", self.run_clock(positive=True))
        self.assertEqual("FAIL", self.run_clock(jump=True))


class LinkCases(unittest.TestCase):
    def test_start_stop_case_fails_early_stop_confirmation(self):
        wire = Mock()
        wire.wait.side_effect = [(True, None), (True, None), (True, None)]
        with patch.object(cases_wire, "_open", return_value=wire):
            self.assertEqual("FAIL", cases_wire.j05_start_stop_sequences(None)[0])

    def test_start_stop_case_requires_restart_and_completed_gi(self):
        for complete in (False, True):
            wire = Mock()
            wire.wait.side_effect = [(True, None), (True, None),
                                    (False, None), (True, None),
                                    (True, None), (complete, None)]
            with patch.object(cases_wire, "_open", return_value=wire):
                verdict, _ = cases_wire.j05_start_stop_sequences(None)
                self.assertEqual("PASS" if complete else "FAIL", verdict)
            wire.acknowledge.assert_called_once()
            wire.stop.assert_called_once()

    def test_malformed_case_requires_observed_connection_close(self):
        ctx = SimpleNamespace(oracle={"ca": 1})
        for connected in (False, True):
            wire = Mock(is_connected=connected)
            wire.wait.return_value = (True, None)
            wire.since.return_value = []
            with patch.object(cases_wire, "_open", return_value=wire):
                verdict, _ = cases_wire.j06_malformed_frame_headers(ctx)
                self.assertEqual("FAIL" if connected else "PASS", verdict)

    def test_coalesced_case_checks_actual_socket_bytes_and_ioa_set(self):
        for expected in ({9}, {10}):
            local, remote = socket.socketpair()
            remote.settimeout(0.2)
            # Real received APCI/ASDU bytes, with consecutive DUT N(S).
            remote.sendall(bytes.fromhex(
                "68040b000000"
                "680e0000000046010400010000000000"
                "680e0200020064010700010000000014"
                "680e0400020001011400010009000001"
                "680e0600020064010a00010000000014"))
            ctx = SimpleNamespace(master=SimpleNamespace(stop=Mock(),
                host="unused", port=0, evidence=None), oracle={"ca": 1},
                expected={"station": expected})
            try:
                with patch.object(cases_wire, "wait_listener"):
                    with patch.object(cases_wire.socket, "create_connection",
                                      return_value=local):
                        verdict, _ = cases_wire.j07_coalesced_startdt_interrogation(ctx)
                self.assertEqual("PASS" if expected == {9} else "FAIL", verdict)
                sent = remote.recv(1024)
                self.assertTrue(sent.startswith(bytes.fromhex(
                    "680407000000680e0000000064010600010000000014")))
            finally:
                local.close()
                remote.close()


if __name__ == "__main__":
    unittest.main()
