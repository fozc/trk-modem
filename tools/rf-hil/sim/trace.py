"""JSONL trace + console logging for the RF hub simulator."""

import json
import sys
import time

from . import scp_codec as sc


class Tracer:
    def __init__(self, path=None, verbose=True):
        self.file = open(path, "a", encoding="ascii") if path else None
        self.verbose = verbose
        self.index = 0

    def close(self):
        if self.file:
            self.file.close()
            self.file = None

    def _write(self, record):
        record["t"] = time.strftime("%Y-%m-%dT%H:%M:%S")
        record["t_ms"] = int(time.monotonic() * 1000)
        if self.file:
            self.file.write(json.dumps(record) + "\n")
            self.file.flush()
        if self.verbose:
            print(format_trace_line(record), file=sys.stderr, flush=True)

    def record(self, ev):
        """Write a pre-built event dict (e.g. hub on_event rx record)."""
        self._write(ev)

    def rx(self, pkt):
        self.index += 1
        self._write({"event": "rx", "idx": self.index, "dir": "RTU->MH",
                     "cmd": pkt.cmd, "name": pkt.name(),
                     "type": pkt.type, "type_name": pkt.type_name(),
                     "seq": pkt.seq, "len": len(pkt.data),
                     "data_hex": pkt.data.hex()})

    def tx(self, pkt, kind):
        self.index += 1
        self._write({"event": "tx", "idx": self.index,
                     "dir": "MH->RTU" if pkt.dst == sc.ADDR_RTU else
                     "MH->%02X" % pkt.dst,
                     "kind": kind, "cmd": pkt.cmd, "name": pkt.name(),
                     "type": pkt.type, "type_name": pkt.type_name(),
                     "seq": pkt.seq, "len": len(pkt.data),
                     "data_hex": pkt.data.hex()})

    def note(self, text, **extra):
        record = {"event": "note", "text": text}
        record.update(extra)
        self._write(record)

    def raw_discard(self, chunk, t_ms):
        self._write({"event": "rx_discard", "data_hex": bytes(chunk).hex(),
                     "len": len(chunk)})


def format_trace_line(record):
    if record["event"] == "note":
        return "[SIM %s] %s" % (record["t"], record["text"])
    if record["event"] == "rx_discard":
        return ("[SIM %s] RX-DISCARD %d bytes (invalid/noise): %s" %
                (record["t"], record["len"], record["data_hex"][:60]))
    arrow = "RX<-" if record["event"] == "rx" else "TX->"
    return ("[%s %s #%04d] %-14s %-5s seq=%3d len=%3d %s" %
            (record["t"], arrow, record.get("idx", 0), record["name"],
             record["type_name"], record["seq"], record["len"],
             record["data_hex"][:48]))
