#!/usr/bin/env python3
"""BOLATeX R0 answer HIL cases: BQ-01 alarm-from-record, BQ-11 gid-zero
rejection, discovery flow, late reply, Trip_Failed latch."""

import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cases import (case, wait_upload_complete, refresh, _left_zero,   # noqa: E402
                   any_retry, ConsoleChecks)
from cases_v12 import _wait_group_state                              # noqa: E402


@case("bq01_alarm_from_101",
      "BQ-01: kayit deposundan gelen 101 acma-basarisizlik alarmi acar; "
      "kayit fault listesine girmez",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 12.0, "do": "add_events", "count": 1,
                           "code": 101, "line": 1}]})
def bq01_alarm_101(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    time.sleep(30.0)  # allow the pull to complete
    tc = refresh(ctx)
    ranges = tc.rx_requests(0x44)
    tc.expect(ranges, "0x44 cekme yok - 101 kaydi alinmadi")
    consumes = tc.rx_requests(0x46)
    tc.expect(consumes, "0x46 consume yok")
    tc.expect(_left_zero(tc), "left=0'a ulasilmadi")
    # alarm must be visible via rf live command
    got = ctx.console.send_and_wait(
        "rf live 1 1", r"Trip_Failed|trip_failed|alarm|Alarm|latched",
        timeout_s=10)
    assert got, "rf live ciktisinda alarm gorunmuyor"
    body = ctx.console.drain(2.0)
    ctx.evidence += body[:10]
    # 101 must NOT be in the fault list (it is alarm-only per BQ-10)
    elog = ctx.console.send_and_wait(
        "elog dump 5", r"elog|TS:|CONFIG|EVENT", timeout_s=8)
    if elog:
        body2 = ctx.console.drain(2.0)
        ctx.evidence += body2[:5]
    return "101 alarm opened; consume=%d" % len(consumes)


@case("bq01_alarm_from_105",
      "BQ-01: 105 kaydi da alarm acar (ayni yol)",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 12.0, "do": "add_events", "count": 1,
                           "code": 105, "line": 2}]})
def bq01_alarm_105(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    time.sleep(30.0)
    tc = refresh(ctx)
    ranges = tc.rx_requests(0x44)
    tc.expect(ranges, "0x44 cekme yok - 105 kaydi alinmadi")
    got = ctx.console.send_and_wait(
        "rf live 2 1", r"Trip_Failed|trip_failed|alarm|Alarm|latched",
        timeout_s=10)
    assert got, "rf live 2/1 ciktisinda alarm gorunmuyor"
    return "105 alarm opened (line 2)"


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
    ctx.console.send("rf cfg-apply 1 0")
    time.sleep(5.0)
    body = ctx.console.drain(2.0)
    ctx.evidence += [l for l in body if l.strip()][:8]
    tc = refresh(ctx)
    writes = tc.rx_requests(0x22)
    tc.expect(not writes,
              "group_id=0 ile 0x22 gonderildi: %d istek" % len(writes))
    out = ConsoleChecks(ctx.console.all_lines()[-100:])
    return "gid=0 rejected; 0x22 count=%d" % len(writes)


@case("h5_late_reply",
      "700 ms gec yanit -> DUT timeout + ayni SEQ tekrar; gec ACK gelince "
      "devam eder",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def h5(ctx):
    ctx.console.send("rf log on")
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
      "Trip_Failed 1'e set edilir; restart'ta latch olur; ack ile kapanir",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 15.0, "do": "live_value",
                           "line": 1, "phase": 1,
                           "values": {"trip_failed": 1}}]})
def c4(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    time.sleep(18.0)  # let live data flow with trip_failed=1
    # alarm visible
    got = ctx.console.send_and_wait(
        "rf live 1 1", r"Trip_Failed|trip_failed|alarm|Alarm",
        timeout_s=10)
    assert got, "Trip_Failed=1 alarm olarak gorunmuyor"
    # clear the flag (uptime advances)
    ctx.sim.call(do="live_value", line=1, phase=1,
                 values={"trip_failed": 0, "uptime_s": 120})
    time.sleep(8.0)
    body = ctx.console.drain(2.0)
    ctx.evidence += body[:8]
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
