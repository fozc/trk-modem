"""Exercise actual device-case verdicts with scripted transport responses."""
from types import SimpleNamespace
from unittest.mock import Mock, patch
import time
import socket
import unittest

import cases_wire
import cases
import oracle


class NegativeCases(unittest.TestCase):
    def run_negative(self, positive=False, missing=False, wrong_cot=False):
        class Wire:
            def __init__(self, *args, **kwargs):
                self.sent = []

            def connect(self):
                return True

            def command(self, type_id, payload, cot, ca):
                rejection = (44 if type_id in (45, 46, 48, 49, 50) else
                             46 if ca is not None else 45 if cot == 20 else
                             47 if type_id in (100, 103) else 7)
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
        self.assertEqual(16, len(lines))

    def test_positive_ack_cannot_pass_negative_case(self):
        self.assertEqual("FAIL", self.run_negative(positive=True)[0])

    def test_missing_response_cannot_pass_negative_case(self):
        self.assertEqual("FAIL", self.run_negative(missing=True)[0])

    def test_wrong_rejection_reason_cannot_pass(self):
        self.assertEqual("FAIL", self.run_negative(wrong_cot=True)[0])

    def run_flagged(self, positive=False, wrong_test=False, jump=False):
        wire = Mock()
        sent = {}

        def command(type_id, payload, cot=6):
            sent.update(type=type_id, payload=payload, cot=cot)

        def interrogate(qoi):
            sent.update(type=100, payload=bytes((0, 0, 0, qoi)), cot=6)

        def response(predicate, timeout, t0=None):
            type_id, cause = sent["type"], sent["cot"]
            expected = 10 if type_id == 100 else 45 if cause == 3 else 7
            frame = (time.monotonic(), "rx", bytes(12) + sent["payload"],
                     {"kind": "I", "asdu": {"type": type_id, "pn": 0 if positive or type_id == 100 else 1,
                               "cot": expected, "test": 1 if wrong_test else cause >> 7}})
            return (True, frame) if predicate(frame) else (False, None)

        wire.command.side_effect = command
        wire.interrogate.side_effect = interrogate
        wire.wait.side_effect = response
        ctx = SimpleNamespace(console=object())
        before, after = (2026, 10, 26, 14, 0, 0), (2026, 10, 26, 12 if jump else 14, 0, 1)
        with patch.object(cases_wire, "_open", return_value=wire):
            with patch.object(cases_wire.console_io, "run_cmd"):
                with patch.object(cases_wire.console_io, "latest_console_time",
                                  side_effect=[before, after]):
                    return cases_wire.j09_flagged_commands(ctx)[0]

    def test_flagged_case_requires_negative_reply_preserved_flag_and_rtc(self):
        self.assertEqual("PASS", self.run_flagged())
        self.assertEqual("FAIL", self.run_flagged(positive=True))
        self.assertEqual("FAIL", self.run_flagged(wrong_test=True))
        self.assertEqual("FAIL", self.run_flagged(jump=True))

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
    def test_battery_is_a_separate_untimed_point_in_group_two(self):
        snap = {"lines": [], "battery_ioa": 10000}
        station, currents, states, _ = oracle.expected_ioa_sets(snap)
        self.assertEqual({10000}, station)
        self.assertEqual(set(), currents)
        self.assertEqual({10000}, states)
        detail = {"data": [(0, "rx", b"", {"asdu": {
            "ca": 1, "cot": 22, "type": 1,
            "objects": [{"ioa": 10000}]}})]}
        self.assertIsNone(cases._check_gi_frames(
            detail, 22, 1, currents, states, 10000))
        detail["data"][0][3]["asdu"]["type"] = 30
        self.assertIsNotNone(cases._check_gi_frames(
            detail, 22, 1, currents, states, 10000))

    def test_group_case_runs_all_four_queries_with_battery_point(self):
        ctx = SimpleNamespace(oracle={"ca": 1, "battery_ioa": 10000,
            "lines": []}, master=SimpleNamespace(interrogate=Mock(return_value=True)),
            expected={"station": {10000}, "group1": set(), "group2": {10000}})
        battery = (0, "rx", b"", {"asdu": {"ca": 1, "cot": 22,
            "type": 1, "objects": [{"ioa": 10000}]}})
        empty = {"data": []}
        replies = [(True, empty), (True, {"data": [battery]}),
                   (True, empty), (True, empty)]
        with patch.object(cases, "_collect_interrogation", side_effect=replies):
            verdict, _ = cases.i03_gi_groups(ctx)
        self.assertEqual("PASS", verdict)
        self.assertEqual([21, 22, 23, 24],
            [call.args[0] for call in ctx.master.interrogate.call_args_list])

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

    def test_partial_ack_case_rejects_extended_deadline_and_restores_config(self):
        for extends_t1 in (False, True):
            now = [0.0]
            saved = {"K": 64, "W": 32, "T1": 15, "T2": 10, "PeriodicSend": 0}
            posts = []

            class Wire:
                def __init__(self):
                    self.receive_sn = 1
                    self.responses = []
                    self.closed_at = float("inf")
                    self.last_type = None

                @property
                def is_connected(self):
                    return now[0] < self.closed_at

                def command(self, type_id, payload, acknowledge_sn=None):
                    self.last_type = type_id

                def acknowledge(self):
                    pass

                def wait(self, predicate, timeout, t0=None):
                    if self.last_type is not None:
                        frame = (now[0], "rx", b"", {"kind": "I",
                            "vs": len(self.responses) + 1,
                            "asdu": {"type": 105, "cot": 7, "pn": 1}})
                        self.last_type = None
                        self.responses.append(frame)
                        self.receive_sn += 1
                        return True, frame
                    now[0] += timeout
                    return False, None

                def _send(self, raw):
                    self.raw_ack = raw
                    self.closed_at = now[0] + 20 if extends_t1 else self.responses[1][0] + 20

                def stop(self):
                    pass

            wire = Wire()

            def request(path, data=None):
                if data is not None:
                    posts.append(data)
                return 200, cases_wire.json.dumps(saved)

            ctx = SimpleNamespace(args=SimpleNamespace(reset_test=True), console=object(),
                                  web=SimpleNamespace(request=request),
                                  master=SimpleNamespace(stop=Mock()))
            with patch.object(cases_wire, "_open", return_value=wire):
                with patch.object(cases_wire, "wait_listener"):
                    with patch.object(cases_wire.time, "monotonic", side_effect=lambda: now[0]):
                        verdict, _ = cases_wire.j11_partial_ack_deadline(ctx)
            self.assertEqual("FAIL" if extends_t1 else "PASS", verdict)
            self.assertEqual(bytes.fromhex("680401000400"), wire.raw_ack)
            self.assertEqual(saved, posts[-1])
            self.assertEqual(2, posts[0]["K"])

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
