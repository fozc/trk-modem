"""Recovery HIL cases using existing DUT commands and simulator controls."""
import time

from cases import case, wait_upload_complete, refresh
from cases_v12 import _wait_group_state


@case("d2_lost_bell_60s_poll",
      "No LOG_AVAILABLE: periodic HEAD retrieves and consumes the event",
      scenario={"setup": [{"do": "set_knob", "name": "log_bell_enabled",
                           "value": False}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def lost_bell(ctx):
    wait_upload_complete(ctx)
    time.sleep(2)
    ctx.sim.call(do="add_events", count=1, code=1, line=1,
                 boot_counter=int(time.time()) & 0xFFFF)
    deadline = time.monotonic() + 75
    while time.monotonic() < deadline:
        refresh(ctx)
        if ctx.sim.call(do="status")["state"]["ring"]["pending"] == 0:
            break
        time.sleep(0.5)
    else:
        raise AssertionError("Periodic poll did not consume the event")
    tc = refresh(ctx)
    tc.expect(not tc.tx_frames(0x47), "Unexpected LOG_AVAILABLE notification")
    heads = tc.rx_requests(0x40)
    ranges = tc.rx_requests(0x44)
    tc.expect(heads and ranges, "Missing HEAD or RANGE")
    gap = ranges[0]["t_ms"] - heads[0]["t_ms"]
    tc.expect(58000 <= gap <= 75000, "Poll gap outside bounds: %d ms" % gap)
    return "Event consumed without notification; poll gap=%d ms" % gap


@case("c3_stale_uptime",
      "Repeated LIVE uptime invalidates current while RF remains online",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def stalled_uptime(ctx):
    wait_upload_complete(ctx)
    time.sleep(7)
    ctx.sim.call(do="set_knob", name="live_enabled", value=False)
    time.sleep(0.5)
    tc = refresh(ctx)
    lives = [r for r in tc.tx_frames(0x11)
             if bytes.fromhex(r["data_hex"])[0] == 5]
    tc.expect(lives, "No LIVE for feeder 1 phase 1")
    data = lives[-1]["data_hex"]
    ctx.sim.call(do="raw_notify", cmd=0x11, data_hex=data)
    time.sleep(0.5)
    reply = ctx.console.send_and_wait(
        "rf live 1 1", r"Irms gecersiz", timeout_s=10)
    tc.expect(reply, "Repeated uptime kept current valid")
    ctx.evidence.append(reply[1])
    online = ctx.console.send_and_wait(
        "rf live 1 1", r"RF VAR, has_live=1", timeout_s=10)
    tc.expect(online, "Invalid current incorrectly made RF offline")
    ctx.evidence.append(online[1])
    return "Current invalid; RF online"


@case("e5_reboot_mid_cfg",
      "MH reboot after STAGED stops the group without automatic replay",
      scenario={"setup": [{"do": "set_knob", "name": "cfg_delivered_ms",
                           "value": 60000},
                          {"do": "set_knob", "name": "cfg_applied_ms",
                           "value": 120000}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def reboot_during_group(ctx):
    wait_upload_complete(ctx)
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send("admin")
    time.sleep(0.3)
    ctx.console.send("rf cfg-apply 1 33")
    _wait_group_state(ctx, 1, timeout_s=20)
    writes_before = len(refresh(ctx).rx_requests(0x22))
    ctx.sim.call(do="reboot")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if len(refresh(ctx).rx_requests(0x05)) >= 2:
            break
        time.sleep(0.5)
    else:
        raise AssertionError("Inventory did not reload after MH reboot")
    reply = ctx.console.send_and_wait(
        "rf cfg-state", r"state=MH_RESTARTED", timeout_s=10)
    assert reply, "DUT did not mark the old group RESTARTED"
    ctx.evidence.append(reply[1])
    assert len(refresh(ctx).rx_requests(0x22)) == writes_before
    return "Old group RESTARTED; inventory restored; no automatic WRITE"
