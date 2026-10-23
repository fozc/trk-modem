"""Offline regression checks for the live-device runner; no network access."""

import copy
import json
import unittest
from unittest.mock import patch

import run_roundtrip as runner


def initial_iec():
    config = dict.fromkeys(runner.IEC_SCALARS, 1)
    config.update(PeriodicSend=900, T0=90, T1=45, T2=30, T3=60,
                  K=64, W=24, OriginatorAddr=1, CommonAddr=1, SBO=False,
                  SBOTimeout=45, ScadaIPAdresi="192.168.1.1",
                  AkuUyarisi=5000, ModemReset=5001)
    config["Hatlar"] = {
        key: [1000 + index * 100]
        for index, key in enumerate(runner.IEC_LINE_ARRAYS)}
    config["Hatlar"]["inUse"] = [False]
    return config


class Bench:
    """Storage boundary preserves production's disabled-line IOA semantics."""

    def __init__(self, inject=False, ignore_timeout=False, active=False):
        self.initial = initial_iec()
        self.initial["Hatlar"]["inUse"] = [active]
        self.config = copy.deepcopy(self.initial)
        self.inject = inject
        self.ignore_timeout = ignore_timeout
        self.pending_error = False

    def apply(self, payload):
        for key, value in payload.items():
            if key != "Hatlar" and not (
                    key == "SBOTimeout" and self.ignore_timeout):
                self.config[key] = copy.deepcopy(value)
        if "Hatlar" in payload:
            for key, value in payload["Hatlar"].items():
                if key in ("inUse", "TemporaryFaultBase", "PermanentFaultBase") \
                        or payload["Hatlar"]["inUse"][0]:
                    self.config["Hatlar"][key] = copy.deepcopy(value)

    def get(self, _path):
        if self.pending_error:
            self.pending_error = False
            raise RuntimeError("lost reply after IOA mutation")
        return 200, copy.deepcopy(self.config)

    def post(self, _path, payload):
        self.apply(payload)
        if self.inject and payload["Hatlar"]["inUse"] == [True] and \
                payload["Hatlar"]["IOA_R_AnlikAkim"][0] > 6000:
            self.inject = False
            self.pending_error = True
        return 200, {"success": True}

    def post_ok(self, _path, payload):
        self.apply(payload)
        return True

    def request(self, method, _path, body=None):
        if method == "POST":
            self.apply(json.loads(body))
            return 200, '{"success":true}'
        return 200, json.dumps(self.config)


class RunnerTests(unittest.TestCase):
    def run_iec(self, bench):
        events = []
        with patch.object(runner, "get_json", bench.get), \
                patch.object(runner, "post_json", bench.post), \
                patch.object(runner, "post_ok", bench.post_ok), \
                patch.object(runner, "rq", bench.request), \
                patch.object(runner, "reject_field"), \
                patch.object(runner, "require", side_effect=lambda name, ok,
                             detail="": events.append((name, ok))):
            try:
                runner.phase_c_iec104()
            finally:
                self.events = events

    def test_ignored_sbo_timeout_is_a_failure(self):
        bench = Bench(ignore_timeout=True)
        self.run_iec(bench)
        self.assertIn(("field_iec104_SBOTimeout_transfer", False), self.events)
        self.assertEqual(bench.initial, bench.config)

    def test_iec_ioas_restore_after_lost_mutation_reply(self):
        for active in (False, True):
            with self.subTest(active=active):
                bench = Bench(inject=True, active=active)
                with self.assertRaisesRegex(RuntimeError, "lost reply"):
                    self.run_iec(bench)
                self.assertEqual(bench.initial, bench.config)
                self.assertIn(("field_iec104_Hatlar_ioa_restored", True),
                              self.events)

    def test_successful_iec_run_restores_initial_state(self):
        bench = Bench()
        self.run_iec(bench)
        self.assertEqual(bench.initial, bench.config)
        self.assertTrue(all(ok for _, ok in self.events))

    def test_failed_restore_is_reported(self):
        with patch.object(runner, "post_ok", return_value=False), \
                patch.object(runner, "require") as report:
            self.assertFalse(runner.restore_iec_config(initial_iec(), 0))
        report.assert_called_once_with("iec104_restore_ioas_accepted", False)

    def test_rf_post_requires_explicit_opt_in(self):
        with patch.object(runner, "ALLOW_RF_APPLY", False), \
                patch.object(runner, "post_ok") as post, \
                patch.object(runner, "get_json") as get:
            runner.phase_c_rf()
        post.assert_not_called()
        get.assert_not_called()

    def test_escaped_string_preserves_non_bmp_unicode(self):
        value = "quote\" slash\\ \n \U0001f600"
        self.assertEqual(value, json.loads('"' + runner.escaped(value) + '"'))

    def test_device_snapshots_do_not_replay_clock_commands(self):
        with patch.object(runner, "rq", return_value=(200, '{}')) as request:
            runner.post_json("/config/device", {"Time": 1, "TimeZone": 3})
        self.assertEqual({"TimeZone": 3}, json.loads(request.call_args.args[2]))
        escaped = runner.body_with_escaped({"Time": 1, "NtpServer": "abc"},
                                          "NtpServer")
        self.assertNotIn("Time", json.loads(escaped))

    def test_full_ioa_range_is_rejected_before_mutation(self):
        with self.assertRaisesRegex(ValueError, "No free IOA test range"):
            runner.free_offset([[0xFFFFFF]])

    def test_auth_retry_exhaustion_has_a_clear_error(self):
        with patch.object(runner, "SESSION") as session, \
                patch.object(runner, "login"):
            session.request.return_value = (401, "unauthorized")
            with self.assertRaisesRegex(RuntimeError, "authentication retry"):
                runner.request("GET", "/config/device")

    def test_final_state_transport_failure_fails_the_run(self):
        runner.FAILURES.clear()
        try:
            replies = [(200, "{}") for _ in range(4)]
            replies.append(RuntimeError("final read failed"))
            with patch("sys.argv", ["run_roundtrip.py"]), \
                    patch.object(runner, "connect"), \
                    patch.object(runner, "login"), \
                    patch.object(runner, "TOKEN", "test"), \
                    patch.object(runner, "SESSION"), \
                    patch.object(runner, "rq", side_effect=replies), \
                    patch.object(runner, "phase_a_schemas"), \
                    patch.object(runner, "verbatim_roundtrip"), \
                    patch.object(runner, "phase_c_device"), \
                    patch.object(runner, "phase_c_iec104"), \
                    patch.object(runner, "phase_c_modbus"), \
                    patch.object(runner, "phase_c_rf"), \
                    patch.object(runner, "phase_d_structural"):
                self.assertEqual(1, runner.main())
            self.assertIn("final_state_verified", runner.FAILURES)
        finally:
            runner.FAILURES.clear()


if __name__ == "__main__":
    unittest.main()
