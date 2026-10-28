"""Offline checks for portable IEC104 integration dependency discovery."""
import sys
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch

import run_tests as runner


class InterpreterDiscovery(unittest.TestCase):
    def check_layout(self, relative):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            interpreter = root / "test/system/iec104_master/.venv" / relative
            interpreter.parent.mkdir(parents=True)
            interpreter.touch()
            with patch.object(runner, "ROOT", root), \
                    patch.object(runner.importlib.util, "find_spec", return_value=None), \
                    patch.object(runner.subprocess, "call", return_value=0) as call:
                self.assertEqual(0, runner.main())
                self.assertEqual(str(interpreter), call.call_args.args[0][0])

    def test_windows_venv_is_discovered(self):
        self.check_layout("Scripts/python.exe")

    def test_linux_venv_is_discovered(self):
        self.check_layout("bin/python")

    def test_missing_dependency_fails_with_the_requirements_path(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runner, "ROOT", Path(directory)), \
                    patch.object(runner.importlib.util, "find_spec", return_value=None):
                with self.assertRaisesRegex(SystemExit, "iec104_stack/requirements.txt"):
                    runner.main()


class ConnectionStartup(unittest.TestCase):
    def test_open_muted_connection_gets_an_explicit_startdt(self):
        events = []

        class Connection:
            protocol_parameters = SimpleNamespace()
            is_connected = False
            is_muted = True

            def add_station(self, address):
                self.address = address

            def connect(self):
                events.append("connect")
                self.is_connected = True

            def unmute(self):
                events.append("unmute")
                self.is_muted = False
                return self.is_connected

        connection = Connection()
        client = SimpleNamespace(add_connection=lambda *a, **k: connection,
                                 start=lambda: events.append("start"))
        module = SimpleNamespace(Client=lambda **k: client,
                                 Init=SimpleNamespace(NONE=0))
        case = runner.StackIntegration()
        case.device = SimpleNamespace(port=1234, sent=[bytes(6) + bytes([70])])
        with patch.dict(sys.modules, {"c104": module}):
            case.connect_c104()
        self.assertEqual(["start", "connect", "unmute"], events)


if __name__ == "__main__":
    unittest.main()
