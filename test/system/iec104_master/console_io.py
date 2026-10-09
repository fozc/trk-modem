#!/usr/bin/env python3
"""Console helpers for the IEC104 master suite (COM16-style shell).

The interactive session lives in tools/hwtest/serial_io.py (same helper
the RF HIL harness uses); this module adds the shell commands and output
parsers the IEC104 suite needs: gsm status (IP + listener rows), rf
status (hub online), iec104evtlog status/test and console wall-clock
timestamp prefixes for the clock-sync apply check.
"""

import re
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "hwtest"))

from serial_io import ConsoleSession  # noqa: E402


def run_cmd(console, line, settle_s=3.0):
    """Send a shell command and return ONLY the lines it produced."""
    with console.lock:
        start = len(console.lines)
    console.send(line)
    time.sleep(settle_s)
    with console.lock:
        produced = console.lines[start:]
    return "\n".join(text for _, text in produced)


def parse_gsm_ip(text):
    """Extract 'IP : a.b.c.d' from gsm status output."""
    match = re.search(r"IP\s*:\s*(\d+\.\d+\.\d+\.\d+)", text)
    return match.group(1) if match else None


def parse_evtlog_status(text):
    """Parse iec104evtlog status -> dict or None on missing output."""
    if "next_seq" not in text:
        return None
    result = {"next_seq": None, "stored": None, "unsent": None,
              "unsent_low": None, "unsent_high": None}
    match = re.search(r"next_seq\s*:\s*(\d+)", text)
    if match:
        result["next_seq"] = int(match.group(1))
    match = re.search(r"kayitli\s*:\s*(\d+)", text)
    if match:
        result["stored"] = int(match.group(1))
    match = re.search(r"unsent\s*:\s*(\d+)(?:\s*\[(\d+)\.\.(\d+)\])?",
                      text)
    if match:
        result["unsent"] = int(match.group(1))
        if match.group(2) is not None:
            result["unsent_low"] = int(match.group(2))
            result["unsent_high"] = int(match.group(3))
    return result


def parse_rf_online(text):
    """RF oturumunun canli olup olmadigi (bool veya None).

    'RF Link: BOSTA' -> False: hub'a periyodik GET_STATUS gidiyor OLABILIR
    ama canli veri oturumu yok (hub up= satiri yine gorunur - canli
    yakalamada birlikte goruldu). Oturum acik gorunuyorsa True, sinyal
    yoksa None.
    """
    link = re.search(r"RF Link\s*:\s*(\S+)", text)
    if link and link.group(1).upper() in ("BOSTA", "BOS"):
        return False
    if re.search(r"hub\s+up=\d+s", text, re.IGNORECASE):
        return True
    return None


# Console log lines carry a wall-clock prefix when cslog prints them.
# The format is YY/MM/DD (verified live: 26/10/09 15:34:08 on
# 2026-10-09), not DD/MM/YY.
TIMESTAMP_RE = re.compile(r"(\d{2})/(\d{2})/(\d{2}) (\d{2}):(\d{2}):(\d{2})")


def latest_console_time(console, window_lines=80):
    """Newest device wall-clock timestamp seen on console (or None)."""
    entries = console.lines[-window_lines:]
    latest = None
    for _, text in entries:
        for match in TIMESTAMP_RE.finditer(text):
            year, month, day, hour, minute, sec = (
                int(part) for part in match.groups())
            latest = (2000 + year, month, day, hour, minute, sec)
    return latest


def open_console(port, logfile=None):
    """Open a ConsoleSession; caller must close()."""
    return ConsoleSession(port, baud=230400, timeout=0.1, logfile=logfile)
