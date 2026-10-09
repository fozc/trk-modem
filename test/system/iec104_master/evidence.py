#!/usr/bin/env python3
"""Evidence recording for the IEC104 master suite.

Every APDU crossing the wire is appended to a per-run JSONL file as
{t, dir, apdu_hex, asdu, frame} where t is a monotonic timestamp in
seconds and frame carries the parsed view from apdu.parse_apdu (U/S
frames keep asdu=null). A manifest.json closes each run with device
oracle snapshot and case verdicts.
"""

import json
import time
from pathlib import Path


class JsonlEvidence:
    """Append-only JSONL writer (one file per run)."""

    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("a", encoding="ascii")

    def write(self, record):
        record = dict(record)
        record.setdefault("t", round(time.monotonic(), 3))
        record.setdefault("wall", time.strftime("%H:%M:%S"))
        self._file.write(json.dumps(record, separators=(",", ":")) + "\n")
        self._file.flush()

    def close(self):
        self._file.close()


def write_manifest(outdir, manifest):
    """Write manifest.json (pretty) under outdir."""
    path = Path(outdir) / "manifest.json"
    path.write_text(json.dumps(manifest, indent=1, sort_keys=True,
                               default=str), encoding="ascii")
    return path
