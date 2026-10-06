#!/usr/bin/env python3
"""Thin console adapter for the HIL harness.

The interactive session lives in the shared tools/hwtest/serial_io.py
(plan v1.2 section 5.1: one common console helper, capture behaviour
unchanged); this module only re-exports it next to the harness code.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "hwtest"))

from serial_io import ConsoleSession, NOISE  # noqa: F401,E402
