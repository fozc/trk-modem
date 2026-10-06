"""RF HIL simulator hardware-less self-tests (central host gate).

Runs test/system/rf_hil/test_sim_selftest.py from the central host
suite: codec golden vectors, hub model behaviour and fault engine --
no serial port is opened (plan v1.2 section 12, Faz 0).
"""

import subprocess
import sys
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
SELFTEST = TEST_ROOT / "system" / "rf_hil" / "test_sim_selftest.py"


def main():
    result = subprocess.run([sys.executable, str(SELFTEST)],
                            timeout=300)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
