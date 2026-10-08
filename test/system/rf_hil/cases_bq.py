#!/usr/bin/env python3
"""BOLATeX R0 answer HIL cases: BQ-01 alarm-from-record, BQ-11 gid-zero
rejection, discovery flow, late reply, Trip_Failed latch."""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cases import case, wait_upload_complete, refresh, any_retry  # noqa: E402


def read_trip_state(ctx, line, phase=1):
    reply = ctx.console.send_and_wait(
        "rf live %d %d" % (line, phase),
        r"trip_failed=([01]), latched=([01])", timeout_s=10)
    assert reply, "RF phase state missing"
    ctx.evidence.append(reply[1])
    return tuple(int(value) for value in reply[2].groups())


def read_permanent_count(ctx, line):
    reply = ctx.console.send_and_wait(
        "fltlog dump %d" % line,
        r"Phase 1 - PERMANENT FAULTS \(total: (\d+),", timeout_s=10)
    assert reply, "Permanent fault count missing"
    ctx.evidence.append(reply[1])
    return int(reply[2].group(1))


def check_alarm_event(ctx, code, line, permanent_delta):
    wait_upload_complete(ctx)
    before = read_permanent_count(ctx, line)
    seq_reply = ctx.console.send_and_wait(
        "iec104evtlog status", r"next_seq\s*:\s*(\d+)", timeout_s=10)
    assert seq_reply, "IEC104 log sequence missing"
    first_seq = int(seq_reply[2].group(1)) & 0xFFFF
    # Separate simulated AY openings even when the MH process restarts.
    boot_counter = int(time.time()) & 0xFFFF
    ctx.sim.call(do="add_events", count=1, code=code, line=line, phase=1,
                 boot_counter=boot_counter)
    deadline = time.monotonic() + 75
    while time.monotonic() < deadline:
        refresh(ctx)
        if ctx.sim.call(do="status")["state"]["ring"]["pending"] == 0:
            break
        time.sleep(0.5)
    else:
        raise AssertionError("Injected event was not consumed")
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x44), "Injected event has no RANGE read")
    # A boot no-op CONSUME cannot satisfy the pending check above.
    assert read_permanent_count(ctx, line) == before + permanent_delta
    reply = ctx.console.send_and_wait(
        "iec104evtlog dump %d 65534" % first_seq,
        r"seq=\d+ alarm F%d Ph1 active=1" % line, timeout_s=10)
    assert reply, "Injected alarm missing from persistent IEC104 log"
    ctx.evidence.append(reply[1])
    # Receipt acknowledgement clears the latch, not a live failure.
    assert read_trip_state(ctx, line)[1] == 0, "Stored alarm still latched"
    return "%d persisted, acknowledged; permanent delta=%d" % (
        code, permanent_delta)


@case("bq01_alarm_from_101",
      "101: persistent alarm and permanent phase fault, then receipt ack",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def bq01_alarm_101(ctx):
    return check_alarm_event(ctx, 101, 1, 1)


@case("bq01_alarm_from_105",
      "105: persistent alarm only, then receipt ack",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def bq01_alarm_105(ctx):
    return check_alarm_event(ctx, 105, 2, 0)


@case("bq11_gid_zero_reject",
      "BQ-11: group_id=0 ile cfg-apply reddedilir (telde 0x22 gonderilmez)",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def bq11_gid_zero(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send("admin")
    time.sleep(0.3)
    rejected = ctx.console.send_and_wait(
        "rf cfg-apply 1 0", r"Config not started", timeout_s=10)
    assert rejected, "DUT did not reject group id zero"
    time.sleep(5.0)
    body = ctx.console.drain(2.0)
    ctx.evidence += [text for _, text in body if text.strip()][:8]
    tc = refresh(ctx)
    writes = tc.rx_requests(0x22)
    tc.expect(not writes,
              "group_id=0 ile 0x22 gonderildi: %d istek" % len(writes))
    return "gid=0 rejected; 0x22 count=%d" % len(writes)


@case("h5_late_reply",
      "700 ms gec yanit -> DUT timeout + ayni SEQ tekrar; gec ACK gelince "
      "devam eder",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def h5(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    ctx.sim.call(do="fault", action="delay_next", args={"ms": 700})
    time.sleep(25.0)
    tc = refresh(ctx)
    retries = any_retry(tc)
    tc.expect(retries, "gec yanit sonrasi ayni SEQ tekrar gorulmedi")
    # link must still be healthy after the late reply
    status_reqs = tc.rx_requests(0x01)
    tc.expect(len(status_reqs) >= 2,
              "gec yanit sonrasi GET_STATUS devam etmedi: %d" %
              len(status_reqs))
    return "late reply handled: retries=%s" % retries


@case("c4_trip_failed_latch",
      "Trip_Failed=1 persists after receipt ack; advancing LIVE 0 clears it",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 15.0, "do": "live_value",
                           "line": 1, "phase": 1,
                           "values": {"trip_failed": 1}}]})
def c4(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    time.sleep(18.0)  # let live data flow with trip_failed=1
    assert read_trip_state(ctx, 1) == (1, 0), "Ongoing alarm not acknowledged"
    # clear the flag (uptime advances)
    ctx.sim.call(do="live_value", line=1, phase=1,
                 values={"trip_failed": 0, "uptime_s": 120})
    time.sleep(8.0)
    assert read_trip_state(ctx, 1) == (0, 0), "Live clear did not close alarm"
    return "Trip_Failed set/clear cycle verified"


@case("b1_discovery",
      "0x14 DISCOVERY_REPORT -> rf disc kuyrugunda gorunur; atama bekler",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 15.0, "do": "inject_discovery",
                           "line": 9, "phase": 1, "rssi": -38}]})
def b1(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    time.sleep(22.0)
    tc = refresh(ctx)
    # verify the 0x14 was sent by the simulator
    disc = [r for r in tc.tx_frames(0x14) if r.get("kind") == "notify"]
    tc.expect(disc, "0x14 DISCOVERY_REPORT gonderilmedi")
    # check console for the discovery entry
    got = ctx.console.send_and_wait(
        "rf disc", r"kesif|Kesif|discovery|EUI|cihaz|kuyruguna",
        timeout_s=10)
    assert got, "rf disc ciktisinda kesif gorunmuyor"
    body = ctx.console.drain(2.0)
    ctx.evidence += body[:10]
    return "discovery reported; rf disc shows it"
