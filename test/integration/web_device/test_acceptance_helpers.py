"""Host checks for device acceptance tests without changing real hardware."""

import json
import unittest
from unittest.mock import patch

import run_tests


class DeviceOptIn(unittest.TestCase):
    def test_default_suite_never_opens_a_socket(self):
        with patch.dict(run_tests.os.environ, {}, clear=True), \
                patch.object(run_tests, "DeviceSession") as session:
            self.assertEqual(0, run_tests.main([]))
            session.assert_not_called()

    def test_explicit_host_still_requires_write_permission(self):
        with patch.dict(run_tests.os.environ, {}, clear=True), \
                patch.object(run_tests, "DeviceSession") as session:
            self.assertEqual(0, run_tests.main(["192.0.2.1"]))
            session.assert_not_called()

    def test_environment_host_still_requires_write_permission(self):
        with patch.dict(run_tests.os.environ, {"WEB_DEVICE_HOST": "192.0.2.1"}, clear=True), \
                patch.object(run_tests, "DeviceSession") as session:
            self.assertEqual(0, run_tests.main([]))
            session.assert_not_called()

    def test_required_device_cannot_silently_skip(self):
        with patch.dict(run_tests.os.environ, {"REQUIRE_DEVICE": "1"}, clear=True), \
                patch.object(run_tests, "DeviceSession") as session:
            self.assertEqual(1, run_tests.main([]))
            session.assert_not_called()


    def test_explicit_host_and_permission_reach_the_selected_session(self):
        with patch.dict(run_tests.os.environ, {}, clear=True), \
                patch.object(run_tests, "DeviceSession", side_effect=RuntimeError("offline")) as session:
            self.assertEqual(0, run_tests.main(["192.0.2.1", "--allow-config-writes"]))
            session.assert_called_once_with()
            self.assertEqual("192.0.2.1", run_tests.HOST)

    def test_required_but_unreachable_device_fails(self):
        with patch.dict(run_tests.os.environ, {"REQUIRE_DEVICE": "1"}, clear=True), \
                patch.object(run_tests, "DeviceSession", side_effect=RuntimeError("offline")):
            self.assertEqual(1, run_tests.main(["192.0.2.1", "--allow-config-writes"]))


class StopBitsCleanup(unittest.TestCase):
    def run_cycle(self, reject_change=False, reject_restore=False,
                  ignore_restore=False, apply_invalid=False):
        initial = {"CihazID": 31, "BaudRate": 9600, "StopBits": 1}
        active = dict(initial)
        writes = []
        closed = []

        class Session:
            def request(self, method, path, body=None):
                if path == "/auth/login":
                    return 200, '{"token":"test"}'
                if method == "GET":
                    return 200, json.dumps(active)
                values = json.loads(body)
                writes.append(values)
                if values.get("StopBits") == 3:
                    if apply_invalid:
                        active.update(values)
                    return 400, '{"success":false}'
                if reject_restore and values == initial:
                    return 500, '{"success":false}'
                if ignore_restore and values == initial:
                    return 200, '{"success":true}'
                active.update(values)
                if reject_change and values.get("StopBits") == 2:
                    return 200, '{"success":false}'
                return 200, '{"success":true}'

            def close(self):
                closed.append(True)

        with patch.object(run_tests, "DeviceSession", Session), \
                patch.object(run_tests.time, "sleep"):
            result = run_tests.test_modbus_stop_bits_cycle()
        return result, initial, active, writes, closed

    def test_success_keeps_slave_and_baud_and_restores_stop_bits(self):
        result, initial, active, writes, closed = self.run_cycle()
        self.assertTrue(result)
        self.assertEqual(initial, active)
        for values in writes:
            self.assertEqual(initial["CihazID"], values["CihazID"])
            self.assertEqual(initial["BaudRate"], values["BaudRate"])
        self.assertTrue(closed)

    def test_failure_after_write_still_restores_original_settings(self):
        result, initial, active, writes, closed = self.run_cycle(
            reject_change=True)
        self.assertFalse(result)
        self.assertEqual(initial, active)
        self.assertEqual(initial, writes[-1])
        self.assertTrue(closed)

    def test_failed_cleanup_cannot_report_pass(self):
        result, _, _, _, closed = self.run_cycle(reject_restore=True)
        self.assertFalse(result)
        self.assertTrue(closed)

    def test_success_response_without_restored_settings_cannot_pass(self):
        result, _, _, _, closed = self.run_cycle(ignore_restore=True)
        self.assertFalse(result)
        self.assertTrue(closed)

    def test_rejected_invalid_write_must_preserve_active_settings(self):
        result, initial, active, _, closed = self.run_cycle(
            apply_invalid=True)
        self.assertFalse(result)
        self.assertEqual(initial, active)
        self.assertTrue(closed)


if __name__ == "__main__":
    unittest.main()
