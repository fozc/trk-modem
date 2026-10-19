"""Production RF store/alarm to IEC104 wire cases on the actual RTU.

The MH/AY producer is simulated, the RTU transport, Flash, replay and
GSM socket are real. This does not replace genuine current injection.
"""
import json
import sys
import time
from pathlib import Path

from cases import case, SkipCase, wait_upload_complete, refresh


def _connect(ctx):
    if not getattr(ctx, "iec104_host", None):
        raise SkipCase("Requires --104-host and active IEC104 lines")
    suite = Path(__file__).resolve().parents[1] / "iec104_master"
    sys.path.append(str(suite))
    from oracle import WebDevice, derive_admin_password, read_oracle
    from master104 import Master104
    from evidence import JsonlEvidence
    web = WebDevice(ctx.iec104_host)
    web.login("admin", derive_admin_password(ctx.iec104_host))
    snapshot = read_oracle(web)
    if not snapshot["lines"][0]["in_use"]:
        raise AssertionError("IEC104 line one must be active")
    (ctx.outdir / "iec104-oracle.json").write_text(
        json.dumps(snapshot, indent=2), encoding="utf-8")
    recorder = JsonlEvidence(ctx.outdir / "iec104-apdu.jsonl")
    master = Master104(ctx.iec104_host, snapshot["port"], snapshot["ca"], recorder)
    master.start()
    if not master.connect(timeout_s=45):
        master.stop()
        recorder.close()
        raise AssertionError("SCADA STARTDT failed")
    return master, recorder, snapshot


def _drained(ctx, timeout=90):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        refresh(ctx)
        reply = ctx.console.send_and_wait(
            "iec104evtlog status", r"unsent\s*:\s*(\d+)", timeout_s=8)
        if (reply and int(reply[2].group(1)) == 0 and
                ctx.sim.call(do="status")["state"]["ring"]["pending"] == 0):
            return
        time.sleep(1)
    raise AssertionError("MH or IEC104 backlog did not drain")


def _source_boot(ctx):
    boot = (int(time.monotonic()) & 0xffff) or 1
    uptime = ctx.sim.call(do="status")["state"]["uptime_s"]
    ctx.sim.call(do="live_value", line=1, phase=1,
                 values={"boot_counter": boot, "uptime_s": uptime})
    time.sleep(6)
    refresh(ctx)
    return boot


@case("s1_rf_fault_spontaneous",
      "RF 142 store to spontaneous SCADA without GI or reconnect",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def s1_fault(ctx):
    wait_upload_complete(ctx)
    master, recorder, snapshot = _connect(ctx)
    try:
        _drained(ctx)
        boot = _source_boot(ctx)
        started = time.monotonic()
        ctx.sim.call(do="add_events", count=1, code=142, line=1, phase=1,
                     boot_counter=boot)
        ok, frame = master.wait(lambda f: f[1] == "rx" and
                                (f[3].get("asdu") or {}).get("type") == 36 and
                                (f[3].get("asdu") or {}).get("cot") == 3,
                                90, t0=started)
        assert ok, "New RF event did not arrive spontaneously"
        asdu = frame[3]["asdu"]
        objects = asdu["objects"]
        assert asdu["ca"] == snapshot["ca"]
        assert len(objects) == 2
        assert abs(objects[0]["value"] - 1250) < 0.051
        assert abs(objects[1]["value"] - 120) < 0.5
        assert all("time" in obj and obj["time"]["iv"] == 0 for obj in objects)
        _drained(ctx)
        assert not [f for f in master.since(started) if f[1] == "tx" and
                    (f[3].get("asdu") or {}).get("type") == 100]
        return "RF142 -> durable store -> COT3 1250 A/120 ms -> master ACK; both queues empty"
    finally:
        master.stop()
        recorder.close()


@case("s2_rf_alarm_active_clear",
      "RF105 alarm ACTIVE and fresh LIVE clear reach SCADA",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def s2_alarm(ctx):
    wait_upload_complete(ctx)
    master, recorder, snapshot = _connect(ctx)
    try:
        _drained(ctx)
        boot = _source_boot(ctx)
        ioa = snapshot["lines"][0]["ioa"]["trip"][0]
        started = time.monotonic()
        ctx.sim.call(do="add_events", count=1, code=105, line=1, phase=1,
                     boot_counter=boot)

        def alarm(frame, active):
            asdu = frame[3].get("asdu") or {}
            return (frame[1] == "rx" and asdu.get("type") == 30 and
                    asdu.get("cot") == 3 and asdu.get("ca") == snapshot["ca"] and
                    any(obj["ioa"] == ioa and obj["siq"]["value"] == active
                        for obj in asdu.get("objects", [])))

        ok, active = master.wait(lambda f: alarm(f, 1), 90, t0=started)
        assert ok, "ACTIVE alarm not received"
        ctx.sim.call(do="live_value", line=1, phase=1,
                     values={"trip_failed": 0, "uptime_s": 600})
        ok, _ = master.wait(lambda f: alarm(f, 0), 90, t0=active[0] + 0.001)
        assert ok, "CLEARED alarm not received after fresh LIVE"
        _drained(ctx)
        return "RF105 -> ACTIVE -> fresh LIVE -> CLEARED at IOA=%d; queues empty" % ioa
    finally:
        master.stop()
        recorder.close()
