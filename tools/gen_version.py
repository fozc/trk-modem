#!/usr/bin/env python3
"""Generate Application/version_gen.h with the current git short hash.

Runs from the pre-build step of both firmware configurations, next to the
html_to_c.py calls. The file is only rewritten when the hash actually
changes, so an unchanged commit does not bump the timestamp and trigger
needless recompiles of the version.h consumers.
"""

import os
import subprocess
import sys


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(root, "Application", "version_gen.h")

    try:
        hash_val = subprocess.run(
            ["git", "rev-parse", "--short=7", "HEAD"],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        hash_val = "nogit"

    content = (
        "/* Uretilmis dosya (tools/gen_version.py, pre-build adimi). "
        "Elle degistirme. */\n"
        '#define GIT_COMMIT_HASH "' + hash_val + '"\n'
    )

    try:
        with open(out_path, "r", encoding="ascii") as f:
            if f.read() == content:
                return 0
    except OSError:
        pass

    with open(out_path, "w", encoding="ascii", newline="\n") as f:
        f.write(content)
    return 0


if __name__ == "__main__":
    sys.exit(main())
