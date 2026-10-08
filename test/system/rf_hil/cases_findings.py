#!/usr/bin/env python3
"""Analysis-finding HIL cases (2026-10-08 report): F-02 lost-terminal-
notification poll recovery in ID_IN_USE, F-11 single automatic epoch
retry after a FAILED reason 5 configuration."""

import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cases import case, wait_upload_complete  # noqa: E402

PROGRAMMER = r"C:/ST/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"


def group_state(ctx):
    reply = ctx.console.send_and_wait(
        "rf cfg-state", r"state=(\w+)", timeout_s=10)
    assert reply, "cfg-state did not answer"
    ctx.evidence.append(reply[1])
    return reply[2].group(1)


def mh_report_state(ctx):
    reply = ctx.console.send_and_wait(
        "rf cfg-state", r"MH state=(\w+)", timeout_s=10)
    return reply[2].group(1) if reply else None


def count_console(ctx, pattern, seconds):
    ctx.console.mark()
    time.sleep(seconds)
    lines = ctx.console.new_lines()
    found = [text for _, text in lines if re.search(pattern, text)]
    ctx.evidence += found[:8]
    return len(found)


def wait_inventory_loaded(ctx, timeout_s=45):
    """Console-driven bring-up wait (trace history is not reusable
    after a DUT reset)."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ctx.console.send("rf status")
        if ctx.console.wait_for(r"Envanter: YUKLU", timeout_s=5):
            return
        time.sleep(1.0)
    raise AssertionError("inventory did not load after raw BOOT")


def dut_reset():
    subprocess.run([PROGRAMMER, "-c", "port=SWD mode=UnderReset reset=SWRST",
                    "-rst"], capture_output=True, text=True, timeout=60)


@case("f02_lost_terminal_notify_recovered_by_poll",
      "F-02: ID_IN_USE peer job with all 0x21 muted; 5 s 0x28 poll alone "
      "updates the report to APPLIED",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def f02_lost_notify(ctx):
    gid = 90
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    try:
        # Freeze a peer group in STAGED and mute every status notification.
        ctx.sim.call(do="set_knob", name="cfg_mute", value=1)
        ctx.sim.call(do="set_knob", name="cfg_no_deliver", value=1)
        ctx.sim.call(do="set_knob", name="cfg_delivered_fail_ms",
                     value=600000)

        ctx.console.send_and_wait(
            "rf cfg-apply 1 %d" % gid, r"queued", timeout_s=10)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if "WAITING" == group_state(ctx):
                break
            time.sleep(1.0)
        hub = ctx.sim.call(do="status")["state"]["group"]
        assert hub and hub["id"] == gid and hub["state"] == 1, \
            "peer job is not STAGED with the chosen id"

        # RTU-only reset: the hub keeps the STAGED job, the DUT forgets
        # it. One raw BOOT_NOTIFY rebuilds the chain on the DUT side.
        dut_reset()
        time.sleep(6.0)
        ctx.console.send("rf log verbose")
        ctx.sim.call(do="raw_notify", cmd=19, data_hex="01")
        wait_inventory_loaded(ctx)

        ctx.console.send_and_wait(
            "rf cfg-apply 1 %d" % gid, r"queued", timeout_s=10)
        state = group_state(ctx)
        assert "ID_IN_USE" == state, "expected ID_IN_USE, got %s" % state

        polls = count_console(ctx, r"cmd=0x28", 13)
        assert polls >= 2, "periodic 0x28 poll missing (%d in 13 s)" % polls

        # Terminal transition with notifications still muted: only the
        # poll can carry the APPLIED report to the DUT.
        ctx.sim.call(do="set_knob", name="cfg_no_deliver", value=0)
        time.sleep(15.0)
        final = mh_report_state(ctx)
        assert final == "APPLIED", "poll did not recover: %r" % final
        return ("polls=%d in 13 s; report=APPLIED with no 0x21 on wire"
                % polls)
    finally:
        ctx.sim.call(do="set_knob", name="cfg_mute", value=0)
        ctx.sim.call(do="set_knob", name="cfg_no_deliver", value=0)


@case("f11_single_epoch_retry_after_timeout",
      "F-11/BQ-07: explicit MH-replacement epoch arms exactly one "
      "automatic 0x2A retry after FAILED reason 5",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def f11_epoch_retry(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    try:
        reply = ctx.console.send_and_wait("rf epoch 1", r"Epoch ACK",
                                          timeout_s=15)
        assert reply, "epoch refresh was not acknowledged"
        time.sleep(92.0)  # firmware-side 90 s epoch settle window

        ctx.sim.call(do="set_knob", name="cfg_force_fail_reason", value=5)
        ctx.console.mark()
        ctx.console.send_and_wait("rf cfg-apply 1 91", r"queued",
                                  timeout_s=10)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if "FAILED" == group_state(ctx):
                break
            time.sleep(1.0)
        assert "FAILED" == group_state(ctx), "forced failure missing"
        # The auto retry fires as soon as the command slot frees after
        # the FAILED report, so the observation window starts before
        # the apply itself.
        time.sleep(4.0)
        window = [text for _, text in ctx.console.new_lines()
                  if re.search(r"cmd=0x2A", text)]
        ctx.evidence += window[:4]
        retries = len(window)
        warn = any("MH replacement" in text
                   for _, text in ctx.console.new_lines())
        ctx.evidence.append("first window retries=%d warn=%s"
                            % (retries, warn))
        assert retries == 1, "expected one retry, got %d" % retries

        # A second reason-5 failure must not arm another retry.
        time.sleep(92.0)
        ctx.console.mark()
        ctx.console.send_and_wait("rf cfg-apply 1 92", r"queued",
                                  timeout_s=10)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if "FAILED" == group_state(ctx):
                break
            time.sleep(1.0)
        time.sleep(4.0)
        second = len([text for _, text in ctx.console.new_lines()
                      if re.search(r"cmd=0x2A", text)])
        assert second == 0, "unexpected extra epoch retry (%d)" % second
        return "retry=1 after first TIMEOUT, 0 after second"
    finally:
        ctx.sim.call(do="set_knob", name="cfg_force_fail_reason",
                     value=None)
