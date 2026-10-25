"""Reject historical approval and fixture rollback in real verification entry."""
import re
import unittest
from unittest.mock import patch

import gsm_firmware_transfer as transfer


class Console:
    def __init__(self, approved=101, fixture=False, command=False):
        self.lines = []
        self.rebooted = False
        self.approved = approved
        self.fixture = fixture
        self.command = command

    def all_lines(self):
        return list(self.lines)

    def new_lines(self, since):
        return self.lines[since:]

    def send(self, command):
        if command == "reset":
            self.rebooted = True
            self.lines.append((0, "SPI flash: AT25SF321B"))
            if self.fixture:
                self.lines.append((0, "GSMTEST FIXTURE ACTIVE"))
        elif command.startswith("bootlog dump"):
            self.lines.append((0, "100 26/10/25 10:00:00 FW_INSTALL_OK installed"))
            sequence = self.approved if self.rebooted else 99
            if sequence is not None:
                self.lines.append((0, "%d 26/10/25 10:00:01 FW_APPROVED approved" % sequence))
        elif command == "gsmtest status":
            self.lines.append((0, "GSMTEST mode=0" if self.command else "Command not found"))

    def wait_for(self, pattern, timeout_s, since):
        del timeout_s
        for timestamp, line in self.lines[since:]:
            match = re.search(pattern, line)
            if match:
                return timestamp, line, match
        return None

    def send_and_wait(self, command, pattern, timeout_s):
        start = len(self.lines)
        self.send(command)
        return self.wait_for(pattern, timeout_s, start)


class FirmwareEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.enterContext(patch.object(transfer.time, "sleep"))
        self.network = self.enterContext(patch.object(transfer, "get_page"))
        self.probe = self.enterContext(patch.object(transfer, "IecProbe"))

    def test_new_approval_requires_reboot_and_network_verification(self):
        console = Console()
        transfer.verify_approved_boot(console, "192.0.2.1")
        self.assertTrue(console.rebooted)
        self.network.assert_called_once_with("192.0.2.1", console, timeout=180)
        self.probe.return_value.ping.assert_called_once()
        self.probe.return_value.close.assert_called_once()

    def test_older_approval_does_not_approve_new_installation(self):
        with self.assertRaisesRegex(AssertionError, "older installation"):
            transfer.verify_approved_boot(Console(approved=99), "192.0.2.1")

    def test_install_sequence_itself_is_not_a_new_approval(self):
        with self.assertRaisesRegex(AssertionError, "older installation"):
            transfer.verify_approved_boot(Console(approved=100), "192.0.2.1")

    def test_missing_approval_fails(self):
        with self.assertRaisesRegex(AssertionError, "missing persistent"):
            transfer.verify_approved_boot(Console(approved=None), "192.0.2.1")

    def test_fixture_rollback_fails_even_with_new_approval(self):
        with self.assertRaisesRegex(AssertionError, "test fixture"):
            transfer.verify_approved_boot(Console(fixture=True), "192.0.2.1")

    def test_leftover_fault_command_fails(self):
        with self.assertRaisesRegex(AssertionError, "fault command remains"):
            transfer.verify_approved_boot(Console(command=True), "192.0.2.1")


if __name__ == "__main__":
    unittest.main()
