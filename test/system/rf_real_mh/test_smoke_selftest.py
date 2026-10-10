"""Exercise observation-only inventory checks without a physical device."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
import importlib.util
import os
import sys
import unittest
from unittest.mock import patch

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
