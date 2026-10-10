#!/usr/bin/env python3
"""Central gate for the IEC104 master suite (device-level).

The suite itself lives in test/system/iec104_master/ (device/network
tests belong there per test/README.md layout rules). This wrapper:

- ALWAYS runs the hardware-less self-tests (14 APDU golden checks +
  13 case-level regression checks incl. CA/COT/type and replay retention);
- IEC104_DEVICE_HOST (or argv[1]) unset  -> device suite SKIPs, exit 0;
- set -> delegate to the system suite, preferring its .venv python
  (c104 lives only there; never installed into the system python).

Exit code: self-test failure -> 1; otherwise whatever the suite returns.
"""

import os
import subprocess
import sys
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
SYSTEM = TEST_ROOT / "system" / "iec104_master"
SUITE = SYSTEM / "run_suite.py"
SELFTEST = SYSTEM / "test_apdu_selftest.py"


def suite_python():
    """Prefer the suite venv (has c104); fall back to this python."""
    for candidate in (SYSTEM / ".venv" / "Scripts" / "python.exe",
                      SYSTEM / ".venv" / "bin" / "python"):
        if candidate.exists():
            return str(candidate)
    return sys.executable


def main():
    for selftest in (SELFTEST,
                     SYSTEM / "test_cases_selftest.py"):
        result = subprocess.call([sys.executable, str(selftest)])
        if result:
            return 1

    host = (len(sys.argv) > 1 and sys.argv[1]) or \
        os.environ.get("IEC104_DEVICE_HOST")
    if not host:
        print("SKIP: IEC104_DEVICE_HOST ayarli degil "
              "(cihaz hedefi acikca verilmeli)")
        return 0

    command = [suite_python(), str(SUITE), "--host", host]
    port = os.environ.get("IEC104_DEVICE_PORT")
    if port:
        command += ["--port", port]
    if os.environ.get("REQUIRE_DEVICE") == "1":
        command += ["--require-device"]
    extra = os.environ.get("IEC104_DEVICE_ARGS")
    if extra:
        command += extra.split()
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
