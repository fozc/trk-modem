#!/usr/bin/env python3
"""Assertion helpers for the RF HIL harness.

Two oracle families:
  - TraceChecks: the simulator's JSONL trace of what the DUT sent and
    received (request order, SEQ rules, body fields, timing).
  - ConsoleChecks: the DUT console output (shell command responses and
    the [RF ST->RF]/[RF RF->ST] frame log lines).
"""

import json
import re


class CheckError(AssertionError):
    pass


def load_trace(path):
    records = []
    with open(path, encoding="ascii") as source:
        for line in source:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


class TraceChecks:
    def __init__(self, records):
        self.records = records
        self.rx = [r for r in records if r.get("event") == "rx"]
        self.tx = [r for r in records if r.get("event") == "tx"]

    # -- primitives ---------------------------------------------------

    def rx_requests(self, cmd=None, name=None):
        out = self.rx
        if cmd is not None:
            out = [r for r in out if r["cmd"] == cmd]
        if name is not None:
            out = [r for r in out if r.get("name") == name]
        return out

    def tx_frames(self, cmd=None, kind=None):
        out = self.tx
        if cmd is not None:
            out = [r for r in out if r["cmd"] == cmd]
        if kind is not None:
            out = [r for r in out if r.get("kind") == kind]
        return out

    def seqs(self, cmd):
        return [r["seq"] for r in self.rx_requests(cmd)]

    # -- assertions ---------------------------------------------------

    def expect(self, condition, message, evidence=None):
        if not condition:
            raise CheckError(message + (" | %s" % (evidence,)
                                        if evidence else ""))

    def assert_request_count(self, cmd, minimum=1, maximum=None):
        found = self.rx_requests(cmd)
        self.expect(len(found) >= minimum,
                    "cmd 0x%02X seen %d times, expected >= %d" %
                    (cmd, len(found), minimum))
        if maximum is not None:
            self.expect(len(found) <= maximum,
                        "cmd 0x%02X seen %d times, expected <= %d" %
                        (cmd, len(found), maximum))
        return found

    def assert_retry_same_seq(self, cmd):
        """Some request was repeated with the same SEQ (timeout retry)."""
        seqs = self.seqs(cmd)
        duplicates = sorted({s for s in seqs if seqs.count(s) > 1})
        self.expect(duplicates,
                    "cmd 0x%02X never repeated with the same SEQ "
                    "(seqs=%s)" % (cmd, seqs[:20]))
        return duplicates

    def assert_all_new_seqs(self, cmd):
        """Every request used a fresh SEQ (no same-SEQ retransmission)."""
        seqs = self.seqs(cmd)
        duplicates = sorted({s for s in seqs if seqs.count(s) > 1})
        self.expect(not duplicates,
                    "cmd 0x%02X repeated SEQs %s (expected fresh SEQs)" %
                    (cmd, duplicates))

    def assert_order(self, cmd_sequence):
        """Requests appear in the given relative order (subsequence)."""
        wanted = list(cmd_sequence)
        pos = 0
        for record in self.rx:
            if pos < len(wanted) and record["cmd"] == wanted[pos]:
                pos += 1
        self.expect(pos == len(wanted),
                    "request order incomplete at step %d/%d, wanted %s" %
                    (pos, len(wanted), ["0x%02X" % c for c in wanted]))

    def assert_gap_bounds(self, cmd_a, cmd_b, min_ms=None, max_ms=None):
        """Time between the last a and the first later b."""
        a_list = self.rx_requests(cmd_a)
        b_list = self.rx_requests(cmd_b)
        self.expect(a_list and b_list, "missing cmds for gap check")
        a_t = a_list[-1]["t_ms"]
        later = [r for r in b_list if r["t_ms"] >= a_t]
        self.expect(later, "no %02X after last %02X" % (cmd_b, cmd_a))
        gap = later[0]["t_ms"] - a_t
        if min_ms is not None:
            self.expect(gap >= min_ms, "gap %d ms < %d ms" % (gap, min_ms))
        if max_ms is not None:
            self.expect(gap <= max_ms, "gap %d ms > %d ms" % (gap, max_ms))
        return gap

    def first_after(self, cmd, t_ms):
        for record in self.rx_requests(cmd):
            if record["t_ms"] >= t_ms:
                return record
        return None


class ConsoleChecks:
    """Pattern helpers over console lines (see console.ConsoleSession)."""

    RF_TX = re.compile(r"\[RF ST->RF\] cmd=0x([0-9A-Fa-f]{2}) "
                       r"seq=(\d+)")
    RF_RX = re.compile(r"\[RF RF->ST\] (\w+) (\w+) seq=(\d+) len=(\d+)")
    RF_HUB = re.compile(r"\[RF\] hub up=(\d+)s fw=(\S+)")

    def __init__(self, lines):
        self.lines = [text for (_, text) in lines]

    def text(self):
        return "\n".join(self.lines)

    def expect(self, condition, message, evidence=None):
        if not condition:
            raise CheckError(message + (" | %s" % (evidence,)
                                        if evidence else ""))

    def assert_contains(self, pattern):
        rx = re.compile(pattern)
        found = [line for line in self.lines if rx.search(line)]
        self.expect(found, "console output missing /%s/" % pattern,
                    evidence=self.lines[-8:])
        return found

    def assert_not_contains(self, pattern):
        rx = re.compile(pattern)
        found = [line for line in self.lines if rx.search(line)]
        self.expect(not found, "console output unexpectedly has /%s/" %
                    pattern)

    def rf_tx_cmds(self):
        out = []
        for line in self.lines:
            match = self.RF_TX.search(line)
            if match:
                out.append((int(match.group(1), 16), int(match.group(2))))
        return out

    def rf_rx_frames(self):
        out = []
        for line in self.lines:
            match = self.RF_RX.search(line)
            if match:
                out.append((match.group(1), match.group(2),
                            int(match.group(3)), int(match.group(4))))
        return out

    def hub_alive(self):
        found = [m.groups() for line in self.lines
                 for m in [self.RF_HUB.search(line)] if m]
        return found
