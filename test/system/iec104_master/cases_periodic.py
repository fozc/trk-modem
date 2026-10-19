"""Opt-in periodic publication acceptance through the actual c104 peer."""
import json
import time

from cases_wire import wait_listener


def validate_frame(frame, currents, states, ca, oa):
    if frame[1] == "unexpected" and "INVALID_COT" in str(frame[3]):
        raise AssertionError("Independent master rejected the COT/type combination")
    asdu = frame[3].get("asdu") or {}
    if frame[1] != "rx" or asdu.get("cot") != 3:
        return set()
    if asdu.get("ca") != ca or asdu.get("oa") != oa:
        raise AssertionError("Periodic CA/OA mismatch")
    seen = set()
    for obj in asdu.get("objects", []):
        ioa = obj["ioa"]
        expected_type = 36 if ioa in currents else 30 if ioa in states else None
        if expected_type is None or asdu.get("type") != expected_type:
            raise AssertionError("Unexpected periodic IOA/type")
        quality = obj.get("qds") if expected_type == 36 else obj.get("siq")
        if quality is None or "time" not in obj:
            raise AssertionError("Periodic quality or time tag missing")
        seen.add(ioa)
    return seen


def p01_periodic(ctx):
    if not getattr(ctx.args, "periodic_test", False) or ctx.console is None:
        return "SKIP", ["Requires --periodic-test and --console"]
    currents = set()
    states = set()
    for line in ctx.oracle["lines"]:
        if line["in_use"]:
            currents.update(line["ioa"]["anlik"])
            for family in ("enerji", "yuk", "rf"):
                states.update(line["ioa"][family])
    expected = currents | states
    if not expected:
        return "SKIP", ["No active IEC104 lines"]
    web = ctx.web
    status, body = web.request("/config/iec104")
    if status != 200:
        raise RuntimeError("Periodic baseline config unavailable")
    saved = json.loads(body)
    target = dict(saved, PeriodicSend=5)
    (ctx.outdir / "periodic-config-before.json").write_text(
        body, encoding="utf-8")
    ctx.master.stop()
    wait_listener(ctx)
    try:
        if web.request("/config/iec104", target)[0] != 200:
            raise RuntimeError("Periodic test config rejected")
        wait_listener(ctx)
        ctx.master.start()
        if not ctx.master.connect(timeout_s=30):
            return "FAIL", ["Periodic STARTDT failed"]
        started = time.monotonic()
        seen = set()
        cycles = 0
        consumed = 0
        completed_at = []
        deadline = started + 35
        while time.monotonic() < deadline:
            frames = ctx.master.since(started)
            for frame in frames[consumed:]:
                asdu = frame[3].get("asdu") or {}
                if frame[1] == "rx" and asdu.get("cot") == 3:
                    # Fault/alarm and closing snapshots also use COT=3.
                    # A periodic packet contains only the configured families.
                    ioas = {obj["ioa"] for obj in asdu.get("objects", [])}
                    if not ioas or not ioas <= expected:
                        continue
                seen |= validate_frame(frame, currents, states,
                                       ctx.oracle["ca"], ctx.oracle["oa"])
                if seen == expected:
                    cycles += 1
                    completed_at.append(frame[0])
                    seen.clear()
            consumed = len(frames)
            if cycles >= 2:
                if any(frame[1] == "tx" and
                       (frame[3].get("asdu") or {}).get("type") == 100
                       for frame in frames):
                    return "FAIL", ["Refresh was solicited by a GI"]
                if completed_at[1] - completed_at[0] < 4:
                    return "FAIL", ["Refresh cycles arrived before the configured interval"]
                return "PASS", ["Two COT=3 time-tagged refresh cycles; %d IOAs; no GI" %
                                len(expected)]
            time.sleep(0.1)
        return "FAIL", ["Periodic cycles=%d, missing=%s" %
                        (cycles, sorted(expected - seen))]
    finally:
        ctx.master.stop()
        status, _ = web.request("/config/iec104", saved)
        read_status, body = web.request("/config/iec104")
        if status != 200 or read_status != 200 or json.loads(body) != saved:
            raise RuntimeError("Periodic configuration restoration failed")


def p02_periodic_web_config(ctx):
    if not getattr(ctx.args, "periodic_test", False):
        return "SKIP", ["Requires --periodic-test"]
    web = ctx.web
    status, body = web.request("/config/iec104")
    if status != 200:
        raise RuntimeError("Periodic baseline config unavailable")
    saved = json.loads(body)
    try:
        for update, interval in (({"PeriodicSend": 0}, 0),
                                 ({"Port": saved["Port"]}, 0),
                                 ({"PeriodicSend": 5}, 5)):
            status, _ = web.request("/config/iec104", update)
            if status != 200:
                return "FAIL", ["Periodic update rejected"]
            status, body = web.request("/config/iec104")
            if status != 200 or json.loads(body) != dict(saved, PeriodicSend=interval):
                return "FAIL", ["Periodic interval or unrelated configuration changed"]
        return "PASS", ["Web disable=0, partial POST keeps 0, enable=5; GET exact match"]
    finally:
        status, _ = web.request("/config/iec104", saved)
        read_status, body = web.request("/config/iec104")
        if status != 200 or read_status != 200 or json.loads(body) != saved:
            raise RuntimeError("Periodic configuration restoration failed")


CASES = {
    "p01_periodic": (p01_periodic, "Time-tagged periodic cycles (--periodic-test)"),
    "p02_periodic_web_config": (p02_periodic_web_config,
                              "Web disable/enable and partial POST (--periodic-test)"),
}
