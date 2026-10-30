#!/usr/bin/env python3
"""Central gate for the device monitor (device-level long run).

The harness itself lives in test/system/device_monitor/ (device/network tests
belong there per test/README.md layout rules). This wrapper:

- ALWAYS runs the hardware-less self-tests (rotation, scheduler,
  anomaly rules, evidence battery, modbus classifier, imports);
- DEVICE_MONITOR_DEVICE=1 -> launch run_monitor.py against the bench (DEVICE_MONITOR_HOURS
  default 1 h acceptance window; DEVICE_MONITOR_HOST/DEVICE_MONITOR_CONSOLE/DEVICE_MONITOR_RS485/
  DEVICE_MONITOR_SEED/DEVICE_MONITOR_OUTDIR map to run_monitor arguments);
- unset -> device run SKIPs, exit 0 (REQUIRE_DEVICE=1 turns SKIP into 1).

Exit code: self-test failure -> 1; otherwise whatever the device monitor returns.
"""

import os
import subprocess
import sys
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
SYSTEM = TEST_ROOT / "system" / "device_monitor"


def main():
    result = subprocess.call([sys.executable, str(SYSTEM / "selftest.py")])
    if result:
        return 1
    if os.environ.get("DEVICE_MONITOR_DEVICE") != "1":
        print("SKIP: DEVICE_MONITOR_DEVICE=1 ayarli degil "
              "(uzun koşu acikca istenmelidir)")
        return 1 if os.environ.get("REQUIRE_DEVICE") == "1" else 0
    command = [sys.executable, str(SYSTEM / "run_monitor.py"),
               os.environ.get("DEVICE_MONITOR_HOST", "auto"),
               "--hours", os.environ.get("DEVICE_MONITOR_HOURS", "1")]
    mapping = (("DEVICE_MONITOR_CONSOLE", "--console"),
               ("DEVICE_MONITOR_RS485", "--rs485"),
               ("DEVICE_MONITOR_SEED", "--seed"),
               ("DEVICE_MONITOR_OUTDIR", "--outdir"))
    for env_name, flag in mapping:
        value = os.environ.get(env_name)
        if value:
            command += [flag, value]
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
