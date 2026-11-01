"""Regression checks for host selection and isolated native ASan configuration."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HostTestTools(unittest.TestCase):
    def check_make_platform(self, values, expected):
        environment = {key: value for key, value in os.environ.items()
                       if key.upper() not in {"COMSPEC", "OS"}}
        environment.update(values)
        source = ("include " + (ROOT / "test/make/common.mk").as_posix() +
                  "\nshow:\n\t@echo host=$(HOST_WINDOWS) flags=$(TARGET_ABI_FLAGS)\n")
        result = subprocess.run(["make", "--no-print-directory", "--dry-run", "-f", "-", "show"],
                                input=source, env=environment, text=True, capture_output=True, check=True)
        self.assertEqual(expected, result.stdout.strip())

    def test_original_comspec_spelling_selects_windows_host_abi(self):
        self.check_make_platform({"ComSpec": "cmd.exe"}, "echo host=1 flags=")

    def test_python_normalized_comspec_selects_windows_host_abi(self):
        self.check_make_platform({"COMSPEC": "cmd.exe"}, "echo host=1 flags=")

    def test_windows_os_marker_is_a_fallback(self):
        self.check_make_platform({"OS": "Windows_NT"}, "echo host=1 flags=")

    def test_non_windows_keeps_existing_32_bit_test_policy(self):
        self.check_make_platform({}, "echo host= flags=-m32")

    def test_asan_configuration_preserves_normal_project_and_uses_its_own_cache(self):
        path = ROOT / "test/project.yml"
        before = path.read_bytes()
        subprocess.run(["ruby", str(ROOT / "test/scripts/run_asan.rb"), "--configure-only"],
                       check=True, capture_output=True, text=True)
        self.assertEqual(before, path.read_bytes())
        profile = (ROOT / "test/build/asan-project.yml").read_text(encoding="utf-8")
        self.assertIn(":build_root: build/ceedling-asan", profile)
        self.assertEqual(2, profile.count("-fsanitize=address"))
        self.assertIn("-fno-omit-frame-pointer", profile)
        self.assertIn("-Werror", profile)


if __name__ == "__main__":
    unittest.main()
