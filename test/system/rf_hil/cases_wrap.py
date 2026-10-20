"""Ring overwrite during an in-flight read on the real DUT."""
import struct
import time

from cases import case, wait_upload_complete, refresh
from cases_r2 import wait_drained


def check_consumes(trace, since_ms, floor, total):
    requests = [r for r in trace.rx_requests(0x48)
                if r["t_ms"] > since_ms]
    assert requests, "no consume after overwrite"
    targets = [struct.unpack("<I", bytes.fromhex(r["data_hex"]))[0]
               for r in requests]
    assert all(floor <= target <= total for target in targets), targets
    assert targets == sorted(targets), "consume cursor moved backwards"
    assert targets[-1] == total, "retained suffix not fully consumed"
    assert trace.tx_frames(0x44, kind="reply"), "no range payload"
    return targets


@case("d5_wrap_during_read",
      "Three ring overwrites during in-flight reads; rescan and drain")
def wrap_during_read(ctx):
    wait_upload_complete(ctx)
    wait_drained(ctx)
    initial = ctx.sim.call(do="status")["state"]["ring"]
    started = time.monotonic()
    for cycle in range(3):
        before = refresh(ctx)
        prior_reads = len(before.rx_requests(0x44))
        # The simulator records logical TX before delaying it on the wire.
        # Delay every reply briefly so the overlapping producer is certain
        # to run before the DUT can receive the range it just requested.
        ctx.sim.call(do="fault", action="delay_all", args={"ms": 600})
        try:
            ctx.sim.call(do="add_events", count=12, code=137, line=1,
                         phase=1, boot_counter=7)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                trace = refresh(ctx)
                if len(trace.rx_requests(0x44)) > prior_reads:
                    break
                time.sleep(.02)
            else:
                raise AssertionError("no in-flight range before overwrite")
            assert trace.tx_frames(0x44, kind="reply"), "no delayed range"
            ctx.sim.call(do="add_events", count=110, code=137, line=1,
                         phase=1, boot_counter=7)
            ring = ctx.sim.call(do="status")["state"]["ring"]
            cutoff = int(time.monotonic() * 1000)
            floor = ring["total"] - ring["pending"]
            assert ring["pending"] == 99, ring
            assert ring["total"] > initial["total"] + 100, ring
            trace = wait_drained(ctx, timeout_s=240)
            targets = check_consumes(trace, cutoff, floor, ring["total"])
            ctx.evidence.append("cycle=%d floor=%d total=%d wrap=%d "
                                "consume_first=%d last=%d" %
                                (cycle + 1, floor, ring["total"],
                                 ring["wrap"], targets[0], targets[-1]))
        finally:
            # Do not clear queued replies; only stop delaying future ones.
            ctx.sim.call(do="fault", action="delay_all", args={"ms": 0})
    return "three overlapping overwrites drained in %.1f s" % (
        time.monotonic() - started)
