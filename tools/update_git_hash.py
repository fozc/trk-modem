#!/usr/bin/env python3
"""Update GIT_COMMIT_HASH in version.h with the current git short hash."""

import re
import subprocess
import sys
from pathlib import Path

VERSION_H = Path(__file__).resolve().parent.parent / "Application" / "version.h"
PATTERN = re.compile(r'(#define\s+GIT_COMMIT_HASH\s+\(")([^"]*?)("\))')


def get_git_short_hash() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        capture_output=True,
        text=True,
        check=True,
        cwd=VERSION_H.parent,
    )
    return result.stdout.strip()


def main() -> int:
    if not VERSION_H.is_file():
        print(f"ERROR: {VERSION_H} not found", file=sys.stderr)
        return 1

    short_hash = get_git_short_hash()
    text = VERSION_H.read_text(encoding="utf-8")

    new_text, count = PATTERN.subn(rf'\g<1>{short_hash}\g<3>', text)
    if count == 0:
        print("ERROR: GIT_COMMIT_HASH define not found in version.h", file=sys.stderr)
        return 1

    VERSION_H.write_text(new_text, encoding="utf-8")
    print(f"GIT_COMMIT_HASH updated to \"{short_hash}\" in {VERSION_H}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
