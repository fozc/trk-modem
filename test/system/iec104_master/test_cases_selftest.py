#!/usr/bin/env python3
"""Hardware-less case-level regression tests (fake master, real cases).

Reproduces the 2026-10-09 review finding: i02 used to PASS a station
interrogation answered with the WRONG common address (CA=99), wrong
per-query COT (24 instead of 20) and current points carried as
single-points instead of measurements. The fixed case must FAIL each of
those scripts and PASS a correct response. No hardware, no network.
"""

import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import apdu  # noqa: E402
import cases  # noqa: E402

FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print("PASS: %s" % name)
    else:
        FAILURES.append(name)
        print("FAIL: %s %s" % (name, detail))


# ---------------------------------------------------------------------------
# frame builders (wire bytes -> parsed tuples, same shape Master104 records)
# ---------------------------------------------------------------------------


def i_frame(vs, vr, asdu_bytes):
    raw = bytes([0x68, 4 + len(asdu_bytes),
                 (vs << 1) & 0xFF, (vs << 1) >> 8,
                 (vr << 1) & 0xFF, (vr << 1) >> 8]) + asdu_bytes
    return (time.monotonic(), "rx", raw, apdu.parse_apdu(raw))


def asdu_header(type_id, cot, pn, ca, body, count=1):
    return bytes([type_id, count, (cot & 0x3F) | (pn << 6),
                  0, ca & 0xFF, ca >> 8]) + body


def ic_body(qoi):
    return bytes([0, 0, 0, qoi])


def me_tf(ioa, value, qds):
    return (bytes([ioa & 0xFF, (ioa >> 8) & 0xFF, (ioa >> 16) & 0xFF]) +
            struct.pack("<f", value) + bytes([qds]) +
            bytes(7))


def sp_tb(ioa, siq):
    return (bytes([ioa & 0xFF, (ioa >> 8) & 0xFF, (ioa >> 16) & 0xFF]) +
            bytes([siq]) + bytes(7))


class FakeMaster:
    """Minimal Master104 stand-in: scripted responses, real waits."""

    def __init__(self):
        self.frames = []
        self.vs = 0
        self.is_connected = False
        self.connect_count = 0
        # Frames appended on the Nth connect() (i07 reconnect window).
        self.pending_script = None
        self.pending_on_connect = 3

    def _push(self, asdu_bytes):
        self.frames.append(i_frame(self.vs, 0, asdu_bytes))
        self.vs += 1

    def connect(self, timeout_s=20.0):
        self.is_connected = True
        self.connect_count += 1
        if (self.pending_script is not None and
                self.connect_count == self.pending_on_connect):
            for asdu_bytes in self.pending_script:
                self._push(asdu_bytes)
        return True

    def disconnect(self):
        self.is_connected = False

    def interrogate(self, qoi_value):
        return True

    def wait(self, predicate, timeout_s, t0=None):
        start = t0 if t0 is not None else 0.0
        for frame in self.frames:
            if frame[0] >= start and predicate(frame):
                return True, frame
        return False, None

    def since(self, t0):
        return [f for f in self.frames if f[0] >= t0]


class Args:
    clock_sync = False
    mutate_eventlog = False
    evtlog_count = 5


def make_ctx(scripted_frames):
    master = FakeMaster()
    master.frames = scripted_frames
    currents = {1030, 1031, 1032, 1130, 1131, 1132}
    states = (set(range(1040, 1043)) | set(range(1050, 1053)) |
              set(range(1060, 1063)) | set(range(1070, 1073)) |
              set(range(1140, 1143)) | set(range(1150, 1153)) |
              set(range(1160, 1163)) | set(range(1170, 1173)))
    oracle = {"ca": 1, "t3": 60, "lines": []}
    ctx = cases.Ctx(master, oracle, None, {"rf_online": False},
                    Args(), {"station": currents | states,
                             "group1": currents, "group2": states,
                             "snapshot": set()}, None)
    return ctx, currents, states


def correct_station_script(ca, currents, states):
    """ACT_CON -> currents (M_ME_TF_1) -> states (M_SP_TB_1) -> ACT_TERM.

    Objects are chunked 3-per-frame like the device does (APDU <= 253 B).
    """
    frames = [i_frame(0, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 7, 0, ca, ic_body(20)))]
    vs = 1

    def add_frames(type_id, objects):
        nonlocal vs
        for index in range(0, len(objects), 3):
            chunk = objects[index:index + 3]
            frames.append(i_frame(vs, 0,
                                  bytes([type_id, len(chunk), 20, 0,
                                         ca & 0xFF, ca >> 8]) +
                                  b"".join(chunk)))
            vs += 1

    add_frames(36, [me_tf(ioa, 0.0, 0x80)
                    for ioa in sorted(currents)])
    add_frames(30, [sp_tb(ioa, 0x80) for ioa in sorted(states)])
    frames.append(i_frame(vs, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 10, 0, ca, ic_body(20))))
    return frames


def load_script(master, frames):
    """Re-stamp scripted frames into the future so case t0 filters pass."""
    base = time.monotonic() + 100.0
    master.frames = [(base + index * 0.01, direction, raw, parsed)
                     for index, (_, direction, raw, parsed)
                     in enumerate(frames)]


def main():
    # --- correct script: PASS (positive control) ----------------------
    ctx, currents, states = make_ctx([])
    load_script(ctx.master, correct_station_script(1, currents, states))
    verdict, lines = cases.i02_gi_station(ctx)
    check("i02_correct_script_passes", verdict == "PASS",
          "verdict=%s lines=%s" % (verdict, lines))

    # --- review repro 1: wrong CA (99) --------------------------------
    ctx, _, _ = make_ctx([])
    load_script(ctx.master, correct_station_script(99, currents, states))
    verdict, lines = cases.i02_gi_station(ctx)
    joined = " | ".join(lines)
    check("i02_wrong_ca_fails",
          verdict == "FAIL" and "CA=99" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    # --- review repro 2: per-query COT 24 in station reply ------------
    frames = correct_station_script(1, currents, states)
    patched = []
    for frame in frames:
        raw = bytearray(frame[2])
        if raw[6] in (36, 30):
            raw[8] = 24
        patched.append((frame[0], frame[1], bytes(raw),
                        apdu.parse_apdu(bytes(raw))))
    ctx, _, _ = make_ctx([])
    load_script(ctx.master, patched)
    verdict, lines = cases.i02_gi_station(ctx)
    joined = " | ".join(lines)
    check("i02_wrong_cot_fails",
          verdict == "FAIL" and "COT=24" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    # --- review repro 3: current point as single-point -----------------
    frames = [i_frame(0, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 7, 0, 1, ic_body(20)))]
    frames.append(i_frame(1, 0, bytes([36, 3, 20, 0, 1, 0]) +
                    b"".join(me_tf(ioa, 0.0, 0x80)
                             for ioa in sorted(currents)[0:3])))
    frames.append(i_frame(2, 0, bytes([30, 3, 20, 0, 1, 0]) +
                    b"".join(sp_tb(ioa, 0x80)
                             for ioa in sorted(currents)[3:6])))
    frames.append(i_frame(3, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 10, 0, 1, ic_body(20))))
    ctx, _, _ = make_ctx([])
    ctx.expected["group1"] = set(currents)
    ctx.expected["station"] = set(currents)
    load_script(ctx.master, frames)
    verdict, lines = cases.i02_gi_station(ctx)
    joined = " | ".join(lines)
    check("i02_current_as_singlepoint_fails",
          verdict == "FAIL" and "tip" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    # --- review repro 4: no measurement at all (vacuous quality) ------
    frames = [i_frame(0, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 7, 0, 1, ic_body(20)))]
    frames.append(i_frame(1, 0, bytes([30, 6, 20, 0, 1, 0]) +
                    b"".join(sp_tb(ioa, 0x80)
                             for ioa in sorted(currents))))
    frames.append(i_frame(2, 0, asdu_header(
        apdu.TYPE_C_IC_NA_1, 10, 0, 1, ic_body(20))))
    ctx, _, _ = make_ctx([])
    ctx.expected["group1"] = set(currents)
    ctx.expected["station"] = set(currents)
    load_script(ctx.master, frames)
    verdict, lines = cases.i02_gi_station(ctx)
    joined = " | ".join(lines)
    check("i02_no_measurement_fails",
          verdict == "FAIL" and "olcum" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    # --- edge: confirm frame WITHOUT objects must not crash -----------
    # (count=0 confirm: old predicate indexed objects[0] -> ERROR)
    frames = correct_station_script(1, currents, states)
    raw0 = bytearray(frames[0][2])
    raw0[7] = 0                      # VSQ count=0 -> no objects decoded
    frames[0] = (frames[0][0], frames[0][1], bytes(raw0),
                 apdu.parse_apdu(bytes(raw0)))
    ctx, _, _ = make_ctx([])
    load_script(ctx.master, frames)
    verdict, lines = cases.i02_gi_station(ctx)
    check("i02_objectless_confirm_no_crash", verdict == "FAIL",
          "verdict=%s lines=%s" % (verdict, lines))

    # --- edge: malformed I-frame inside the window is ignored ---------
    # (undecodable ASDU must not TypeError in the window filter)
    frames = correct_station_script(1, currents, states)
    junk = i_frame(99, 0, bytes([0x24, 0x01]))    # 2-byte ASDU -> None
    frames.insert(2, junk)
    ctx, _, _ = make_ctx([])
    load_script(ctx.master, frames)
    verdict, lines = cases.i02_gi_station(ctx)
    check("i02_malformed_frame_ignored", verdict == "PASS",
          "verdict=%s lines=%s" % (verdict, lines))

    # --- i07: retention across a mid-replay link break (de5604c) -----
    # seqs 204..200 injected (next_seq 205); window1 delivers 204,203,
    # the cut drops the link, window2 must deliver the rest.
    import console_io

    def replay_asdu(seq):
        current = 100.0 + (seq % 900) / 10.0
        duration = float(100 + (seq % 50) * 20)
        return (bytes([36, 2, 3, 0, 1, 0]) +
                me_tf(1030, current, 0x00) +
                me_tf(1031, duration, 0x00))

    def i07_ctx(window2_seqs):
        ctx, _, _ = make_ctx([])
        ctx.args.mutate_eventlog = True
        ctx.console = object()          # guard only; run_cmd is patched
        ctx.master.pending_script = [replay_asdu(s) for s in window2_seqs]
        # Window1 frames must land after the case's second connect (t0),
        # which happens after the 2 s post-drain sleep: stamp +5 s.
        base = time.monotonic() + 5.0
        stamped = []
        for index, seq in enumerate((204, 203)):
            frame = i_frame(0, 0, replay_asdu(seq))
            stamped.append((base + index * 0.05, frame[1], frame[2],
                            frame[3]))
        ctx.master.frames = stamped
        statuses = [
            "kayitli: 100, next_seq: 200, unsent : 0 [0..0]",
            "",
            "kayitli: 105, next_seq: 205, unsent : 5 [200..204]",
            "kayitli: 105, next_seq: 205, unsent : 0 [0..0]",
        ]
        state = {"index": 0}
        real_run_cmd = console_io.run_cmd

        def fake_run_cmd(console, cmd, settle_s=0.0):
            text = statuses[state["index"]] if "status" in cmd else ""
            state["index"] += 1
            return text

        console_io.run_cmd = fake_run_cmd
        return ctx, real_run_cmd

    ctx, real_run_cmd = i07_ctx([202, 201, 200])
    verdict, lines = cases.i07_replay_retention(ctx)
    console_io.run_cmd = real_run_cmd
    joined = " | ".join(lines)
    check("i07_retention_passes",
          verdict == "PASS" and "birlesik tumul 5/5" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    ctx, real_run_cmd = i07_ctx([202, 200])       # seq 201 lost forever
    verdict, lines = cases.i07_replay_retention(ctx)
    console_io.run_cmd = real_run_cmd
    joined = " | ".join(lines)
    check("i07_lost_record_fails",
          verdict == "FAIL" and "kayit kayboldu" in joined and
          "201" in joined,
          "verdict=%s lines=%s" % (verdict, lines))

    total = 9
    print("\n%d/%d OK" % (total - len(FAILURES), total))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
