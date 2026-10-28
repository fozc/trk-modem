#!/usr/bin/env python3
"""Central gate for the soak harness (device-level long run).

The harness itself lives in test/system/soak/ (device/network tests
belong there per test/README.md layout rules). This wrapper:

- ALWAYS runs the hardware-less self-tests (rotation, scheduler,
  anomaly rules, evidence battery, modbus classifier, imports);
- SOAK_DEVICE=1 -> launch run_soak.py against the bench (SOAK_HOURS
  default 1 h acceptance window; SOAK_HOST/SOAK_CONSOLE/SOAK_RS485/
  SOAK_SEED/SOAK_OUTDIR map to run_soak arguments);
- unset -> device run SKIPs, exit 0 (REQUIRE_DEVICE=1 turns SKIP into 1).

Exit code: self-test failure -> 1; otherwise whatever the soak returns.
"""

import os
import subprocess
import sys
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
SYSTEM = TEST_ROOT / "system" / "soak"


def main():
    result = subprocess.call([sys.executable, str(SYSTEM / "selftest.py")])
    if result:
        return 1
    if os.environ.get("SOAK_DEVICE") != "1":
        print("SKIP: SOAK_DEVICE=1 ayarli degil "
              "(uzun koşu acikca istenmelidir)")
        return 1 if os.environ.get("REQUIRE_DEVICE") == "1" else 0
    command = [sys.executable, str(SYSTEM / "run_soak.py"),
               os.environ.get("SOAK_HOST", "auto"),
               "--hours", os.environ.get("SOAK_HOURS", "1")]
    mapping = (("SOAK_CONSOLE", "--console"),
               ("SOAK_RS485", "--rs485"),
               ("SOAK_SEED", "--seed"),
               ("SOAK_OUTDIR", "--outdir"))
    for env_name, flag in mapping:
        value = os.environ.get(env_name)
        if value:
            command += [flag, value]
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
