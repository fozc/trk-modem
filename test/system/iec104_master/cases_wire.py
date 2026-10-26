"""Real-device negative commands and explicitly withheld IEC104 ACKs."""
import datetime
import json
import re
import socket
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
    for qoi in (20, 21, 22, 23, 24):
        checks.append((100, bytes((1, 0, 0, qoi)), 6, None, 47))
    clock = bytes((1, 0, 0, 0, 0, 23, 12, 11, 3, 26))
    checks.append((103, clock, 6, None, 47))
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


def j05_start_stop_sequences(ctx):
    """Duplicate STARTDT and STOPDT must preserve the TCP session state."""
    wire = _open(ctx, auto_ack=False)
    try:
        start = time.monotonic() - 5
        initialized, _ = wire.wait(lambda f: f[1] == "rx" and
            (f[3].get("asdu") or {}).get("type") == 70, 10, t0=start)
        if not initialized:
            return "FAIL", ["Initialization missing"]
        duplicate_at = time.monotonic()
        wire._send(bytes.fromhex("680407000000"))
        confirmed, _ = wire.wait(lambda f: f[1] == "rx" and
            f[3].get("u") == "STARTDT_CON", 10, t0=duplicate_at)
        if not confirmed:
            return "FAIL", ["Duplicate STARTDT confirmation missing"]
        stop_at = time.monotonic()
        wire._send(bytes.fromhex("680413000000"))
        early, _ = wire.wait(lambda f: f[1] == "rx" and
            f[3].get("u") == "STOPDT_CON", 1, t0=stop_at)
        if early:
            return "FAIL", ["STOPDT confirmed before pending I-frame ACK"]
        wire.acknowledge()
        stopped, _ = wire.wait(lambda f: f[1] == "rx" and
            f[3].get("u") == "STOPDT_CON", 10, t0=stop_at)
        if not stopped:
            return "FAIL", ["STOPDT did not complete after ACK"]
        wire.auto_ack = True
        restart_at = time.monotonic()
        wire._send(bytes.fromhex("680407000000"))
        started, _ = wire.wait(lambda f: f[1] == "rx" and
            f[3].get("u") == "STARTDT_CON", 10, t0=restart_at)
        if not started:
            return "FAIL", ["STARTDT after STOPDT failed"]
        wire.interrogate(21)
        done, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 100, cot=10, pn=0), 30, t0=restart_at)
        if not done:
            return "FAIL", ["GI after STOPDT/STARTDT did not complete"]
        return "PASS", ["Duplicate STARTDT, pending ACK then STOPDT_CON, "
                        "restart and GI; all received N(S) continuous"]
    finally:
        wire.stop()


def j06_malformed_frame_headers(ctx):
    """Malformed headers must close the session without command execution."""
    ca = ctx.oracle["ca"]
    command = bytes((105, 0, 6, 0, ca & 255, ca >> 8, 0, 0, 0, 2))
    invalid = [bytes.fromhex("55"), bytes.fromhex("6803"),
               bytes.fromhex("680400000200"),
               bytes.fromhex("6809000002006401060001"),
               bytes.fromhex("68050700000000")]
    for vsq in (0, 2, 0x82):
        body = bytes((command[0], vsq)) + command[2:]
        invalid.append(bytes((0x68, 14, 0, 0, 2, 0)) + body)
    for raw in invalid:
        wire = _open(ctx)
        try:
            initialized, _ = wire.wait(lambda f: f[1] == "rx" and
                (f[3].get("asdu") or {}).get("type") == 70,
                10, t0=time.monotonic() - 5)
            if not initialized:
                return "FAIL", ["Initialization missing before malformed input"]
            sent_at = time.monotonic()
            # Intentionally invalid input cannot pass the master's APDU decoder.
            wire.sock.sendall(raw)
            if wire.evidence is not None:
                wire.evidence.write({"dir": "tx", "apdu_hex": raw.hex(), "frame": None, "asdu": None})
            wire.wait(lambda f: False, 15, t0=sent_at)
            if wire.is_connected:
                return "FAIL", ["Malformed frame did not close: " + raw.hex()]
            if any(f[1] == "rx" and f[3].get("kind") == "I"
                   for f in wire.since(sent_at)):
                return "FAIL", ["Malformed input produced an ASDU reply"]
        finally:
            wire.stop()
    return "PASS", ["%d malformed header/VSQ inputs closed cleanly" % len(invalid)]


def j07_coalesced_startdt_interrogation(ctx):
    ctx.master.stop()
    wait_listener(ctx)
    wire = WireMaster(ctx.master.host, ctx.master.port, ctx.oracle["ca"],
                      ctx.master.evidence)
    try:
        wire.sock = socket.create_connection((wire.host, wire.port), 30)
        ca = wire.ca
        start = bytes.fromhex("680407000000")
        query = bytes((0x68, 14, 0, 0, 0, 0, 100, 1, 6, 0,
                       ca & 255, ca >> 8, 0, 0, 0, 20))
        sent_at = time.monotonic()
        wire.sock.sendall(start + query)
        wire._record("tx", start)
        wire._record("tx", query)
        wire.send_sn = 1
        done, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 100, cot=10, pn=0), 30, t0=sent_at)
        if not done:
            return "FAIL", ["GI coalesced with STARTDT was lost"]
        data = [f for f in wire.since(sent_at) if f[1] == "rx" and
                (f[3].get("asdu") or {}).get("cot") == 20]
        ioas = [obj["ioa"] for f in data
                for obj in f[3]["asdu"].get("objects", [])]
        if set(ioas) != ctx.expected["station"] or len(ioas) != len(set(ioas)):
            return "FAIL", ["Coalesced GI IOA set incomplete or repeated"]
        return "PASS", ["One TCP send: STARTDT + GI; %d unique IOAs" % len(ioas)]
    finally:
        wire.stop()


def j08_window_and_reset(ctx):
    """Full window cannot trigger reboot before ACT_CON is accepted."""
    if not getattr(ctx.args, "reset_test", False) or ctx.console is None:
        return "SKIP", ["Requires --reset-test and --console"]
    status, body = ctx.web.request("/config/iec104")
    if status != 200:
        raise RuntimeError("Window test configuration unavailable")
    saved = json.loads(body)
    (ctx.outdir / "window-config-before.json").write_text(body, encoding="utf-8")
    status, body = ctx.web.request("/config/device")
    if status != 200:
        raise RuntimeError("Device timezone unavailable")
    timezone = int(json.loads(body)["TimeZone"])
    ctx.master.stop()
    wait_listener(ctx)
    wire = None
    try:
        if ctx.web.request("/config/iec104", dict(saved, K=1, W=1,
                                                  PeriodicSend=0))[0] != 200:
            raise RuntimeError("Window test configuration rejected")
        wire = _open(ctx, auto_ack=False)
        initialized, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 70), 10, t0=time.monotonic() - 5)
        if not initialized:
            return "FAIL", ["Initialization missing"]
        blocked_at = time.monotonic()
        wire.command(105, bytes((0, 0, 0, 2)), acknowledge_sn=0)
        wire.wait(lambda f: False, 8, t0=blocked_at)
        if not wire.is_connected or any(apdu.frame_is_i_asdu(
                f, "rx", 105) for f in wire.since(blocked_at)):
            return "FAIL", ["Full window produced reset reply or reboot"]
        wire.auto_ack = True
        wire.acknowledge()
        replied, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 105, cot=7, pn=1), 15, t0=blocked_at)
        if not replied:
            return "FAIL", ["Deferred reset rejection missing after K opened"]
        gi_at = time.monotonic()
        wire.interrogate(20)
        done, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 100, cot=10, pn=0), 75, t0=gi_at)
        data = [f for f in wire.since(gi_at) if f[1] == "rx" and
                (f[3].get("asdu") or {}).get("cot") == 20]
        ioas = [obj["ioa"] for f in data
                for obj in f[3]["asdu"].get("objects", [])]
        if (not done or set(ioas) != ctx.expected["station"] or
                len(ioas) != len(set(ioas))):
            return "FAIL", ["K=1 GI incomplete or repeated"]
        if getattr(ctx.args, "mutate_eventlog", False):
            # The shell fixture only writes storage. A fresh STARTDT
            # arms the normal replay fallback after the fixture exists.
            wire.stop()
            console_io.run_cmd(ctx.console, "iec104evtlog test 1", settle_s=2)
            snapshot_at = time.monotonic()
            wire = _open(ctx)
            expected_snapshot = ctx.expected["snapshot"]
            def snapshot_ioas():
                return [obj["ioa"] for f in wire.since(snapshot_at)
                        if apdu.frame_is_i_asdu(f, "rx", 30, cot=3)
                        for obj in f[3]["asdu"].get("objects", [])
                        if obj["ioa"] in expected_snapshot]
            closed, _ = wire.wait(lambda f:
                bool(expected_snapshot) and
                expected_snapshot <= set(snapshot_ioas()), 45, t0=snapshot_at)
            if not closed or len(snapshot_ioas()) != len(expected_snapshot):
                return "FAIL", ["K=1 replay closing snapshot missing/repeated"]
        for qoi in (23, 24):
            query_at = time.monotonic()
            wire.interrogate(qoi)
            complete, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
                f, "rx", 100, cot=10, pn=0) and
                f[3]["asdu"]["objects"][0]["qoi"] == qoi,
                120, t0=query_at)
            if not complete:
                return "FAIL", ["K=1 fault query %d did not finish" % qoi]
        with ctx.console.lock:
            mark = len(ctx.console.lines)
        reset_at = time.monotonic()
        wire.command(105, bytes((0, 0, 0, 1)))
        confirmed, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 105, cot=7, pn=0), 10, t0=reset_at)
        if not confirmed:
            return "FAIL", ["Reset ACT_CON missing"]
        wire.wait(lambda f: False, 20, t0=reset_at)
        if wire.is_connected:
            return "FAIL", ["Confirmed reset did not close the connection"]
        from check_ntp_reset import ADJUSTED, verify_ntp
        if not ctx.console.wait_for(ADJUSTED, timeout_s=120, since=mark):
            return "FAIL", ["Fresh NTP synchronization after reset missing"]
        ctx.console.drain(3)
        result = verify_ntp([line for _, line in
            ctx.console.new_lines(since=mark)], timezone)
        (ctx.outdir / "reset-ntp.json").write_text(json.dumps(result, indent=2))
        return "PASS", ["K=1 retains reply until ACK opens window; %d unique GI IOAs; "
                        "QOI 23/24 complete; accepted reset followed by fresh NTP and RTC proof" % len(ioas)]
    finally:
        if wire is not None:
            wire.stop()
        # Reset clears the device's authentication session.
        from oracle import derive_admin_password
        ctx.web.token = None
        ctx.web.login(ctx.args.user, ctx.args.password or
                      derive_admin_password(ctx.master.host))
        status, _ = ctx.web.request("/config/iec104", saved)
        read_status, body = ctx.web.request("/config/iec104")
        if status != 200 or read_status != 200 or json.loads(body) != saved:
            raise RuntimeError("Window test configuration restoration failed")



def j09_flagged_commands(ctx):
    """Test/negative requests must not change RTC or reboot the device."""
    if ctx.console is None:
        return "SKIP", ["RTC preservation check requires --console"]
    wire = _open(ctx)
    try:
        console_io.run_cmd(ctx.console, "rf status", settle_s=2)
        before = console_io.latest_console_time(ctx.console)
        if before is None:
            return "FAIL", ["RTC baseline unavailable"]
        started = time.monotonic()
        timestamp = bytes((0, 0, 15, 10, 1, 1, 20))
        checks = [(105, bytes((0, 0, 0, 1)), 0x86, 7),
                  (105, bytes((0, 0, 0, 1)), 0x46, 7),
                  (103, bytes(3) + timestamp, 0x86, 7),
                  (103, bytes(3) + timestamp, 0x46, 7),
                  (103, bytes(3) + timestamp, 3, 45)]
        for type_id, payload, cause, expected_cot in checks:
            sent_at = time.monotonic()
            wire.command(type_id, payload, cot=cause)
            ok, frame = wire.wait(lambda f: apdu.frame_is_i_asdu(
                f, "rx", type_id), 15, t0=sent_at)
            if not ok:
                return "FAIL", ["Flagged command response missing"]
            asdu = frame[3]["asdu"]
            if (asdu["pn"] != 1 or asdu["cot"] != expected_cot or
                    asdu["test"] != (cause >> 7) or frame[2][12:] != payload):
                return "FAIL", ["Flagged command echoed wrong fields: " + str(asdu)]
        console_io.run_cmd(ctx.console, "rf status", settle_s=2)
        after = console_io.latest_console_time(ctx.console)
        if after is None:
            return "FAIL", ["RTC closing observation missing"]
        elapsed = (datetime.datetime(*after) - datetime.datetime(*before)).total_seconds()
        if not -2 <= elapsed <= time.monotonic() - started + 10:
            return "FAIL", ["Flagged clock changed RTC"]
        wire.interrogate(21)
        ok, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 100, cot=10, pn=0), 45)
        if not ok:
            return "FAIL", ["Device did not remain usable after flagged reset"]
        return "PASS", ["Five requests rejected, T flag/payload preserved, RTC unchanged, GI healthy"]
    finally:
        wire.stop()


def j10_stopped_connection_liveness(ctx):
    """STOPDT ends data transfer; TCP liveness and sequence state survive."""
    wire = _open(ctx)
    try:
        wire.wait(lambda f: apdu.frame_is_i_asdu(f, "rx", 70), 10,
                  t0=time.monotonic() - 5)
        stopped_at = time.monotonic()
        wire._send(bytes.fromhex("680413000000"))
        ok, _ = wire.wait(lambda f: f[1] == "rx" and
                         f[3].get("u") == "STOPDT_CON", 15, t0=stopped_at)
        if not ok:
            return "FAIL", ["STOPDT_CON missing"]
        ok, _ = wire.wait(lambda f: f[1] == "rx" and
                         f[3].get("u") == "TESTFR_ACT",
                         ctx.oracle["t3"] + 15, t0=stopped_at)
        if not ok:
            return "FAIL", ["Stopped connection did not send TESTFR_ACT"]
        restarted_at = time.monotonic()
        wire._send(bytes.fromhex("680407000000"))
        ok, _ = wire.wait(lambda f: f[1] == "rx" and
                         f[3].get("u") == "STARTDT_CON", 15, t0=restarted_at)
        if not ok:
            return "FAIL", ["Stopped connection did not resume"]
        wire.interrogate(21)
        ok, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 100, cot=10, pn=0), 45, t0=restarted_at)
        if not ok or any(apdu.frame_is_i_asdu(f, "rx", 70)
                         for f in wire.since(restarted_at)):
            return "FAIL", ["Resume lost GI or repeated initialization"]
        return "PASS", ["STOPDT -> TESTFR -> STARTDT -> GI, no duplicate Type 70"]
    finally:
        wire.stop()



def j11_partial_ack_deadline(ctx):
    """A partial ACK must not grant another T1 to the remaining frame."""
    if not getattr(ctx.args, "reset_test", False) or ctx.console is None:
        return "SKIP", ["Requires --reset-test and --console for temporary windows"]
    status, body = ctx.web.request("/config/iec104")
    if status != 200:
        return "FAIL", ["Window configuration unavailable"]
    saved = json.loads(body)
    ctx.master.stop()
    wait_listener(ctx)
    wire = None
    try:
        status, _ = ctx.web.request("/config/iec104",
                                   dict(saved, K=2, W=1, T1=20, T2=10, PeriodicSend=0))
        if status != 200:
            return "FAIL", ["Temporary window configuration rejected"]
        wire = _open(ctx)
        wire.wait(lambda f: False, 3)
        wire.acknowledge()
        wire.auto_ack = False
        base_ack = wire.receive_sn
        responses = []
        for index in range(2):
            sent_at = time.monotonic()
            wire.command(105, bytes((0, 0, 0, 2)), acknowledge_sn=base_ack)
            ok, frame = wire.wait(lambda f: apdu.frame_is_i_asdu(
                f, "rx", 105, cot=7, pn=1), 10, t0=sent_at)
            if not ok:
                return "FAIL", ["Missing pending-frame fixture"]
            responses.append(frame)
            if index == 0:
                wire.wait(lambda f: False, 2)
        wire.wait(lambda f: False, 12)
        if not wire.is_connected:
            return "FAIL", ["Connection closed before oldest frame deadline"]
        partial = ((responses[0][3]["vs"] + 1) & 0x7fff) << 1
        ack_at = time.monotonic()
        wire._send(bytes((0x68, 4, 1, 0, partial & 255, partial >> 8)))
        # Five seconds is an observation allowance for tick and network delivery,
        # not an extension of the device's configured T1.
        remaining = responses[1][0] + 25 - time.monotonic()
        wire.wait(lambda f: False, max(0, remaining))
        elapsed = time.monotonic() - responses[1][0]
        if wire.is_connected:
            return "FAIL", ["Partial ACK extended remaining frame T1"]
        return "PASS", ["Second frame closed after %.1f s; partial ACK at age %.1f s; T1=20" %
                        (elapsed, ack_at - responses[1][0])]
    finally:
        if wire is not None:
            wire.stop()
        status, _ = ctx.web.request("/config/iec104", saved)
        read_status, body = ctx.web.request("/config/iec104")
        if status != 200 or read_status != 200 or json.loads(body) != saved:
            raise RuntimeError("Partial-ACK configuration restoration failed")



def j12_deferred_clock_confirmation(ctx):
    """A clock confirmation survives a full K=1 window and preserves its ASDU."""
    if not (getattr(ctx.args, "reset_test", False) and
            getattr(ctx.args, "clock_sync", False)) or ctx.console is None:
        return "SKIP", ["Requires --reset-test, --clock-sync and --console"]
    status, body = ctx.web.request("/config/iec104")
    if status != 200:
        return "FAIL", ["Window configuration unavailable"]
    saved = json.loads(body)
    ctx.master.stop()
    wait_listener(ctx)
    wire = None
    try:
        if ctx.web.request("/config/iec104",
                           dict(saved, K=1, W=1, PeriodicSend=0))[0] != 200:
            return "FAIL", ["Temporary K=1 configuration rejected"]
        wire = _open(ctx, auto_ack=False)
        ok, _ = wire.wait(lambda f: apdu.frame_is_i_asdu(f, "rx", 70), 10,
                          t0=time.monotonic() - 5)
        if not ok:
            return "FAIL", ["Initialization fixture missing"]
        instant = datetime.datetime.now(datetime.timezone.utc)
        millis = instant.second * 1000 + instant.microsecond // 1000
        stamp = bytes((millis & 255, millis >> 8, instant.minute, instant.hour,
                       instant.day | ((instant.weekday() + 1) << 5),
                       instant.month, instant.year - 2000))
        payload = bytes(3) + stamp
        started = time.monotonic()
        wire.command(103, payload, acknowledge_sn=0)
        wire.wait(lambda f: False, 3, t0=started)
        if not wire.is_connected or any(apdu.frame_is_i_asdu(
                f, "rx", 103) for f in wire.since(started)):
            return "FAIL", ["Full K window was not respected"]
        wire.auto_ack = True
        wire.acknowledge()
        ok, frame = wire.wait(lambda f: apdu.frame_is_i_asdu(
            f, "rx", 103, cot=7, pn=0), 15, t0=started)
        if not ok or frame[2][12:] != payload:
            return "FAIL", ["Deferred clock confirmation missing or payload changed"]
        console_io.run_cmd(ctx.console, "rf status", settle_s=2)
        actual = console_io.latest_console_time(ctx.console)
        expected = instant.replace(tzinfo=None) + datetime.timedelta(
            seconds=time.monotonic() - started)
        if actual is None or abs((datetime.datetime(*actual) - expected).total_seconds()) > 10:
            return "FAIL", ["Deferred clock RTC application not observed"]
        return "PASS", ["K=1 held ACT_CON until ACK; original CP56 preserved; RTC application observed"]
    finally:
        if wire is not None:
            wire.stop()
        status, _ = ctx.web.request("/config/iec104", saved)
        read_status, body = ctx.web.request("/config/iec104")
        if status != 200 or read_status != 200 or json.loads(body) != saved:
            raise RuntimeError("Deferred-clock configuration restoration failed")


CASES = {
    "j01_negative_commands": (j01_negative_commands, "Unsupported/invalid commands"),
    "j02_withheld_ack": (j02_withheld_ack, "No consumption before explicit ACK"),
    "j03_lost_ack_reconnect": (j03_lost_ack_reconnect, "Missing ACK plus reconnect"),
    "j04_invalid_clock": (j04_invalid_clock, "Invalid clocks preserve RTC"),
    "j05_start_stop_sequences": (j05_start_stop_sequences,
                                "STARTDT/STOPDT and pending ACK sequence"),
    "j06_malformed_frame_headers": (j06_malformed_frame_headers,
                                    "Short/oversized headers and invalid VSQ"),
    "j07_coalesced_startdt_interrogation": (j07_coalesced_startdt_interrogation,
                                           "STARTDT and GI in one TCP send"),
    "j08_window_and_reset": (j08_window_and_reset,
                            "K=1 deferred reply, accepted reboot and NTP (--reset-test)"),
    "j09_flagged_commands": (j09_flagged_commands, "Test/negative commands preserve RTC and do not reboot"),
    "j10_stopped_connection_liveness": (j10_stopped_connection_liveness, "STOPDT keeps TESTFR and resume state"),
    "j11_partial_ack_deadline": (j11_partial_ack_deadline, "Partial ACK preserves original T1 (--reset-test)"),
    "j12_deferred_clock_confirmation": (j12_deferred_clock_confirmation, "K=1 preserves clock ACT_CON (--clock-sync/--reset-test)"),
}
