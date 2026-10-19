"""Real-device negative commands and explicitly withheld IEC104 ACKs."""
import datetime
import re
import time

import apdu
import console_io
from wire_master import WireMaster


def _open(ctx, auto_ack=True):
    ctx.master.stop()
    wait_listener(ctx)
    wire = WireMaster(ctx.master.host, ctx.master.port, ctx.oracle["ca"],
                      ctx.master.evidence, auto_ack=auto_ack)
    if not wire.connect():
        wire.stop()
        raise RuntimeError("Wire master STARTDT_CON missing")
    return wire


def wait_listener(ctx):
    if getattr(ctx, "console", None) is not None:
        # A local TCP close does not prove the GSM listener has rearmed.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status = console_io.run_cmd(ctx.console, "gsm status", settle_s=2)
            if re.search(r"IEC104\s+LISTENING\s+IDLE", status):
                break
        else:
            raise RuntimeError("DUT IEC104 listener did not rearm")


def j01_negative_commands(ctx):
    """Unsupported controls and invalid reset requests must not execute."""
    wire = _open(ctx)
    checks = [(45, bytes(4), 6, None, 44),
              (46, bytes(4), 6, None, 44),
              (48, bytes(6), 6, None, 44),
              (49, bytes(6), 6, None, 44),
              (50, bytes(8), 6, None, 44),
              (105, bytes((0, 0, 0, 2)), 6, None, 7),
              (105, bytes((0, 0, 0, 255)), 6, None, 7),
              (105, bytes((253, 255, 255, 1)), 6, None, 7),
              (105, bytes((0, 0, 0, 1)), 20, None, 45),
              (105, bytes((0, 0, 0, 1)), 6,
               1 if ctx.oracle["ca"] == 65535 else ctx.oracle["ca"] + 1, 46)]
    lines = []
    try:
        for type_id, payload, cot, ca, rejection in checks:
            start = time.monotonic()
            wire.command(type_id, payload, cot=cot, ca=ca)
            ok, frame = wire.wait(lambda f: f[1] == "rx" and
                                 (f[3].get("asdu") or {}).get("type") ==
                                 type_id, 15, t0=start)
            if not ok:
                return "FAIL", lines + ["type=%d response missing" % type_id]
            asdu = frame[3]["asdu"]
            if asdu["pn"] != 1 or asdu["cot"] != rejection:
                return "FAIL", lines + ["Unexpected positive/wrong rejection: " +
                                        str(asdu)]
            lines.append("type=%d request COT=%d: PN=1 COT=%d" %
                         (type_id, cot, rejection))
        return "PASS", lines
    finally:
        wire.stop()


def _status(ctx):
    state = console_io.parse_evtlog_status(console_io.run_cmd(
        ctx.console, "iec104evtlog status", settle_s=2))
    if state is None or state["next_seq"] is None or state["unsent"] is None:
        raise RuntimeError("Event-log status unavailable")
    return state


def _matching_fault(frame, seq):
    asdu = frame[3].get("asdu") or {}
    objects = asdu.get("objects", [])
    return (frame[1] == "rx" and asdu.get("type") == apdu.TYPE_M_ME_TF_1
            and asdu.get("cot") == 3 and len(objects) == 2 and
            abs(objects[0]["value"] - (100 + seq % 900 / 10)) < 0.051 and
            abs(objects[1]["value"] - (100 + seq % 50 * 20)) < 0.5)


def _ack_retention(ctx, disconnect):
    if not ctx.args.mutate_eventlog or ctx.console is None:
        return "SKIP", ["Requires --mutate-eventlog and --console"]
    if not ctx.expected["station"]:
        return "SKIP", ["No active IEC104 lines"]
    ctx.master.stop()
    before = _status(ctx)
    if before["unsent"] != 0:
        return "SKIP", ["Drain existing backlog first: %d" % before["unsent"]]
    console_io.run_cmd(ctx.console, "iec104evtlog test 1", settle_s=2)
    added = _status(ctx)
    if added["next_seq"] != before["next_seq"] + 1 or added["unsent"] != 1:
        return "FAIL", ["Synthetic fixture was not persisted"]
    seq = before["next_seq"]
    wire = _open(ctx, auto_ack=False)
    try:
        start = time.monotonic()
        # Acknowledge initialization so its t1 does not overlap fallback.
        ok, _ = wire.wait(lambda f: f[1] == "rx" and
                          (f[3].get("asdu") or {}).get("type") == 70,
                          10, t0=start - 5)
        if not ok:
            return "FAIL", ["Initialization ASDU missing"]
        wire.acknowledge()
        initialized_sn = wire.receive_sn
        ok, measured = wire.wait(lambda f: _matching_fault(f, seq),
                                  40, t0=start)
        if not ok:
            return "FAIL", ["Automatic replay missing or content mismatch"]
        ok, _ = wire.wait(lambda f: f[1] == "rx" and
                          (f[3].get("asdu") or {}).get("type") == 30 and
                          (f[3].get("asdu") or {}).get("cot") == 3,
                          5, t0=measured[0])
        if not ok or _status(ctx)["unsent"] != 1:
            return "FAIL", ["Record consumed before complete master ACK"]
        if disconnect:
            wire.stop()
            if _status(ctx)["unsent"] != 1:
                return "FAIL", ["Unacknowledged record lost at disconnect"]
            wire = _open(ctx)
            start = time.monotonic()
            ok, _ = wire.wait(lambda f: _matching_fault(f, seq),
                              45, t0=start - 5)
            if not ok:
                return "FAIL", ["Unacknowledged record not replayed"]
            # Receive and ACK the second ASDU as well.
            wire.wait(lambda f: f[1] == "rx" and
                      (f[3].get("asdu") or {}).get("type") == 30,
                      10, t0=start - 5)
        else:
            # Do not piggyback an ACK for the fault while testing QRP=2.
            requested = time.monotonic()
            wire.command(105, bytes((0, 0, 0, 2)),
                         acknowledge_sn=initialized_sn)
            rejected, response = wire.wait(lambda f: f[1] == "rx" and
                (f[3].get("asdu") or {}).get("type") == 105,
                10, t0=requested)
            if (not rejected or response[3]["asdu"]["pn"] != 1 or
                    response[3]["asdu"]["cot"] != 7 or
                    _status(ctx)["unsent"] != 1):
                return "FAIL", ["QRP=2 did not reject while preserving pending"]
            wire.acknowledge()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if _status(ctx)["unsent"] == 0:
                return "PASS", ["seq=%d retained without ACK; %s; unsent=0" %
                                (seq, "replayed after link loss" if disconnect
                                 else "consumed only after explicit ACK")]
        return "FAIL", ["Record still pending after ACK"]
    finally:
        wire.stop()


def j02_withheld_ack(ctx):
    return _ack_retention(ctx, False)


def j03_lost_ack_reconnect(ctx):
    return _ack_retention(ctx, True)


def j04_invalid_clock(ctx):
    if ctx.console is None:
        return "SKIP", ["RTC preservation check requires --console"]
    wire = _open(ctx)
    try:
        console_io.run_cmd(ctx.console, "rf status", settle_s=2)
        before = console_io.latest_console_time(ctx.console)
        if before is None:
            return "SKIP", ["RTC baseline unavailable"]
        started = time.monotonic()
        invalid = (bytes((0, 0, 0x80, 12, 19, 10, 26)),
                   bytes((0, 0, 0, 24, 19, 10, 26)),
                   bytes((0, 0, 0, 12, 19, 0, 26)),
                   bytes((0, 0, 0, 12, 31, 2, 26)))
        for timestamp in invalid:
            start = time.monotonic()
            wire.command(103, bytes(3) + timestamp)
            ok, frame = wire.wait(lambda f: f[1] == "rx" and
                                 (f[3].get("asdu") or {}).get("type") == 103,
                                 15, t0=start)
            if not ok or frame[3]["asdu"]["pn"] != 1:
                return "FAIL", ["Invalid clock was not rejected"]
        console_io.run_cmd(ctx.console, "rf status", settle_s=2)
        after = console_io.latest_console_time(ctx.console)
        if after is None:
            return "FAIL", ["RTC closing observation missing"]
        advance = (datetime.datetime(*after) -
                   datetime.datetime(*before)).total_seconds()
        if not -2 <= advance <= time.monotonic() - started + 10:
            return "FAIL", ["RTC changed after invalid clock: %s -> %s" %
                            (before, after)]
        return "PASS", ["Four invalid clocks rejected; RTC advanced %.0f s" %
                        advance]
    finally:
        wire.stop()


CASES = {
    "j01_negative_commands": (j01_negative_commands, "Unsupported/invalid commands"),
    "j02_withheld_ack": (j02_withheld_ack, "No consumption before explicit ACK"),
    "j03_lost_ack_reconnect": (j03_lost_ack_reconnect, "Missing ACK plus reconnect"),
    "j04_invalid_clock": (j04_invalid_clock, "Invalid clocks preserve RTC"),
}
