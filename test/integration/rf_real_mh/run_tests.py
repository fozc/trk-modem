"""Real-MH smoke suite wrapper (central host gate).

Always run hardware-less self-tests first. Device checks are environment-
gated like the other device suites: without
RF_REAL_MH_CONSOLE the wrapper SKIPs (exit 0) so the central suite stays
green on benches without the real hub. Set RF_REAL_MH_CONSOLE=COM16 (and
optionally RF_REAL_MH_HOST, RF_REAL_MH_ARGS, REQUIRE_DEVICE=1) to run.
"""

import os
import subprocess
import sys
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
RUNNER = TEST_ROOT / "system" / "rf_real_mh" / "run_smoke.py"
SELFTEST = RUNNER.with_name("test_smoke_selftest.py")


def main():
    if subprocess.call([sys.executable, str(SELFTEST)]) != 0:
        return 1
    console = os.environ.get("RF_REAL_MH_CONSOLE")
    if not console:
        print("SKIP: RF_REAL_MH_CONSOLE ayarli degil (gercek MH hedefi "
              "acikca verilmeli)")
        return 0
    command = [sys.executable, str(RUNNER), "--console", console]
    host = os.environ.get("RF_REAL_MH_HOST")
    if host:
        command += ["--104-host", host]
    extra = os.environ.get("RF_REAL_MH_ARGS", "")
    if extra:
        command += extra.split()
    if "1" == os.environ.get("REQUIRE_DEVICE"):
        command += ["--require-device"]
    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
