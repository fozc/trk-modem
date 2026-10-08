"""Host regressions for HIL acceptance/provenance; no ports are opened."""
import json
import struct
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import identity
import run_hil


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        data = bytearray(257)
        data[:8] = b"*EFW\x01\x60\x64\x01"
        struct.pack_into("<III", data, 12, 100, 0x12345678, 1)
        data[24:31] = b"abcdef0"
        self.image = self.root / "sample.efw"
        self.image.write_bytes(data)
        self.expected = identity.read_image_identity(self.image)
        run_hil.OBSERVED.clear()

    def test_artifact_records_full_hash_and_rejects_bad_header_or_size(self):
        self.assertEqual(64, len(self.expected["sha256"]))
        for offset, value in ((4, 0), (5, 0x40), (20, 2)):
            data = bytearray(self.image.read_bytes())
            data[offset] = value
            bad = self.root / "bad.efw"
            bad.write_bytes(data)
            with self.assertRaises(identity.IdentityError):
                identity.read_image_identity(bad)

    def test_runtime_metadata_must_match_each_field_and_compiled_profile(self):
        observed = identity.parse_dut_identity(
            "RTU image: crc=0x12345678 size=100 git=abcdef0 profile=1")
        identity.verify_dut_identity(observed, self.expected, 1)
        for key, value in (("app_crc", 0), ("app_size", 101),
                           ("git_commit", "old0000"), ("transport_profile", 0)):
            with self.assertRaises(identity.IdentityError):
                identity.verify_dut_identity(dict(observed, **{key: value}),
                                             self.expected, 1)
        with self.assertRaises(identity.IdentityError):
            identity.parse_dut_identity("hub up=1s fw=SIM-T4R1")

    def test_incomplete_or_empty_selections_never_return_success(self):
        for value in ("DEFERRED", "SKIP", "ERROR", "BLOCKED", "ASSUMPTION"):
            self.assertEqual(2, identity.result_exit_code({"case": (value, [])}))
        self.assertEqual(2, identity.result_exit_code({}))
        self.assertEqual(1, identity.result_exit_code({"case": ("FAIL", [])}))
        self.assertEqual(0, identity.result_exit_code({"case": ("PASS", [])}))

    def test_bad_dut_identity_blocks_case_before_its_assertions(self):
        args = SimpleNamespace(expected_image=self.expected, transport_profile=1, console="fake")
        session = SimpleNamespace(send_and_wait=lambda *a, **k:
            (0, "RTU image: crc=0x12345678 size=100 git=abcdef0 profile=0", None),
            all_lines=lambda: [], close=lambda: None)
        sim = SimpleNamespace(start=lambda: None, close=lambda: None)
        executed = []
        spec = dict(scenario={}, run=lambda ctx: executed.append(True))
        with patch.object(run_hil, "SimHandle", return_value=sim), \
             patch("console.ConsoleSession", return_value=session):
            verdict, _ = run_hil.run_case("identity_probe", spec, args, self.root)
        self.assertEqual("BLOCKED", verdict)
        self.assertEqual([], executed)

    def test_changed_image_after_case_cannot_be_reported_as_pass(self):
        args = SimpleNamespace(expected_image=self.expected, transport_profile=1, console="fake")
        replies = iter(("RTU image: crc=0x12345678 size=100 git=abcdef0 profile=1",
                        "RTU image: crc=0x99999999 size=100 git=abcdef0 profile=1"))
        session = SimpleNamespace(send_and_wait=lambda *a, **k: (0, next(replies), None),
                                  send=lambda line: None,
                                  all_lines=lambda: [], close=lambda: None)
        sim = SimpleNamespace(start=lambda: None, close=lambda: None,
                              call=lambda **k: dict(state={}))
        with patch.object(run_hil, "SimHandle", return_value=sim), \
             patch("console.ConsoleSession", return_value=session):
            verdict, _ = run_hil.run_case("identity_probe", dict(scenario={}, run=lambda c: None),
                                          args, self.root)
        self.assertEqual("BLOCKED", verdict)

    def test_missing_artifact_blocks_run_without_opening_ports(self):
        arguments = ["run_hil.py", "--case", "a1_boot_from_start",
                     "--outdir", str(self.root / "run")]
        with patch("sys.argv", arguments), patch.object(run_hil, "run_case") as case:
            self.assertEqual(2, run_hil.main())
            case.assert_not_called()
        manifest = json.loads((self.root / "run/manifest.json").read_text())
        self.assertFalse(manifest["complete"])
        self.assertIsNone(manifest["expected_transport_profile"])
        self.assertIsNone(manifest["expected_image"])
        self.assertNotIn("git_commit", manifest)

    def test_verified_case_records_both_identity_observations(self):
        args = SimpleNamespace(expected_image=self.expected, transport_profile=1,
                               console="fake")
        text = "RTU image: crc=0x12345678 size=100 git=abcdef0 profile=1"
        session = SimpleNamespace(send_and_wait=lambda *a, **k: (0, text, None),
                                  send=lambda line: None,
                                  all_lines=lambda: [], close=lambda: None)
        sim = SimpleNamespace(start=lambda: None, close=lambda: None,
                              call=lambda **k: dict(state={}))
        with patch.object(run_hil, "SimHandle", return_value=sim), \
             patch("console.ConsoleSession", return_value=session):
            verdict, _ = run_hil.run_case("identity_probe", dict(scenario={}, run=lambda c: None),
                                          args, self.root)
        self.assertEqual("PASS", verdict)
        self.assertTrue(run_hil.OBSERVED["dut_images"]["identity_probe"]["before_verified"])
        self.assertTrue(run_hil.OBSERVED["dut_images"]["identity_probe"]["after_verified"])

    def test_duplicate_selection_cannot_overwrite_a_failed_case(self):
        with patch("sys.argv", ["run_hil.py", "--case", "a1_boot_from_start",
                                 "--case", "a1_boot_from_start"]), \
             patch.object(run_hil, "run_case") as case:
            with self.assertRaises(SystemExit) as error:
                run_hil.main()
            self.assertEqual(2, error.exception.code)
            case.assert_not_called()

    def test_production_status_handler_reports_compiled_profile_and_boot_metadata(self):
        root = run_hil.REPO_ROOT
        source = (root / "Application/rf/rf_shell.c").read_text(encoding="utf-8")
        match = re.search(r"static int rf_shell_status\([^;]+?\)\n\{.*?\n\}",
                          source, re.S)
        self.assertIsNotNone(match)
        preamble = r'''
#include <stdio.h>
#include <stdbool.h>
#include "boot.h"
#include "rf_hil_transport.h"
#define SHELL_LOG(...) printf(__VA_ARGS__)
static fw_info_t installed = {.size=100U, .fw_crc=0x12345678U,
                             .short_commit_hash="abcdef0"};
const fw_info_t *boot_get_installed_fw_info(void) { return &installed; }
static bool scp_is_free(void) { return true; }
static uint8_t rf_comm_get_hub_major(void) { return 1U; }
static uint8_t rf_inventory_get_status(void) { return 0U; }
typedef struct { const char *reason; uint32_t total_ms;
                 uint32_t reason_ms; } rf_inventory_wait_t;
static void rf_inventory_get_wait(rf_inventory_wait_t *out)
{ out->reason = "none"; out->total_ms = 0U; out->reason_ms = 0U; }
static const char *inventory_status_name(uint8_t status)
{ (void)status; return "ready"; }
static uint8_t rf_discovery_get_count(void) { return 0U; }
'''
        text = preamble + match[0] + "\nint main(void) { return rf_shell_status(0, NULL); }\n"
        path = self.root / "status.c"
        path.write_text(text, encoding="ascii")
        for profile in (0, 1):
            executable = self.root / ("status%d.exe" % profile)
            build = subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-DRF_SCP_OVER_MODBUS_PORT=%d" % profile,
                            "-I" + str(root / "Application"),
                            "-I" + str(root / "Application/rf"),
                            "-I" + str(root / "Application/app_ipc"),
                            "-I" + str(root / "Application/libs"),
                            "-I" + str(root / "Application/libefw"),
                            str(path), "-o", str(executable)],
                           capture_output=True, text=True)
            self.assertEqual(0, build.returncode, build.stderr)
            output = subprocess.check_output([str(executable)], text=True)
            observed = identity.parse_dut_identity(output)
            identity.verify_dut_identity(observed, self.expected, profile)


if __name__ == "__main__":
    unittest.main()
