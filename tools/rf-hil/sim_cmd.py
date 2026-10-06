#!/usr/bin/env python3
"""Tiny client for the rf-hub simulator control channel.

Usage:
  python sim_cmd.py 127.0.0.1:7788 status
  python sim_cmd.py 7788 inject_trip line=1 phase=2
  python sim_cmd.py 7788 add_events count=3 code=1 line=1
  python sim_cmd.py 7788 fault action=drop_next count=1

Arguments after the address are key=value pairs; "do" is the first
bare word (or do=...). Example replies are printed as JSON.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sim.control import ControlClient  # noqa: E402


def parse_args(words):
    request = {}
    for word in words:
        if "=" in word:
            key, _, value = word.partition("=")
            try:
                value = int(value)
            except ValueError:
                pass
            request[key] = value
        elif "do" not in request:
            request["do"] = word
    return request


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    target = sys.argv[1]
    if ":" not in target:
        target = "127.0.0.1:" + target
    host, _, port = target.rpartition(":")
    client = ControlClient(host or "127.0.0.1", int(port))
    try:
        reply = client.call(**parse_args(sys.argv[2:]))
    finally:
        client.close()
    print(json.dumps(reply, indent=1, sort_keys=True))
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
