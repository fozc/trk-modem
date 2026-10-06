#!/usr/bin/env python3
"""RF hub (MH) simulator - CLI entry point.

Speaks the BOLATeX Teslim4 R1 SCP interface on a PC COM port wired to
the modem's USART3 RF line (PC4/PC5, 230400 8N1). Full command set,
proactive notification generators, wire-level fault injection and a
TCP control channel for scripted/live driving.

Usage (daily "virtual hub"):
  python rf_hub_sim.py COM10

HIL mode (driven by a scenario file + control channel):
  python rf_hub_sim.py COM10 --scenario case.json --control 127.0.0.1:7788

Options:
  --baud 230400        line rate (must match the DUT)
  --noboot             do not send BOOT_NOTIFY at start
  --control HOST:PORT  enable TCP control channel (default off)
  --scenario FILE      JSON timeline scenario (see sim/scenario.py)
  --trace FILE         append JSONL trace (all frames + notes)
  --quiet              no per-frame console output

Wiring: adapter RX <- PC4 (DUT TX), adapter TX -> PC5 (DUT RX),
GND <-> GND. A 3.3 V USB-TTL adapter is required (not RS-485).
"""

import argparse
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sim import scp_codec as sc                     # noqa: E402
from sim.control import ControlServer, Dispatcher   # noqa: E402
from sim.faults import FaultEngine                  # noqa: E402
from sim.hub_model import HubModel                  # noqa: E402
from sim.scenario import ScenarioExecutor           # noqa: E402
from sim.trace import Tracer                        # noqa: E402


def start_watchdog(tracer, stall_limit_s=4.0):
    """Exit the process when the main loop stalls in a stuck serial read.

    Under burst load the Windows USB-serial driver can stop firing read
    timeouts (observed on the bench: both USB adapters lock up, the
    device itself stays healthy). A hard os._exit releases the COM port
    so run_hil can restart this process; the fresh BOOT re-brings the
    DUT inventory automatically.
    """
    import os
    import threading

    def monitor():
        last = time.monotonic()
        while True:
            time.sleep(1.0)
            now = time.monotonic()
            if now - last > stall_limit_s:
                tracer.note("watchdog: main loop stalled %.1f s, exiting" %
                            (now - last))
                tracer.close()
                os._exit(86)
            if now - last < 1.5:
                last = now

    thread = threading.Thread(target=monitor, daemon=True)
    thread.start()


def parser_state_counts(hub):
    """Received-request count for the heartbeat note."""
    return getattr(hub, "rx_count", 0)


def open_serial(port, baud):
    try:
        import serial
    except ImportError:
        print("pyserial is required: pip install pyserial",
              file=sys.stderr)
        raise SystemExit(2)
    return serial.Serial(port, baud, bytesize=8, parity="N",
                         stopbits=1, timeout=0.02)


def main():
    parser = argparse.ArgumentParser(
        description="RF hub (MH) simulator, BOLATeX Teslim4 R1")
    parser.add_argument("port", help="COM port wired to the RF line")
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--noboot", action="store_true")
    parser.add_argument("--control", default=None,
                        help="HOST:PORT control channel (127.0.0.1:7788)")
    parser.add_argument("--scenario", default=None,
                        help="JSON timeline scenario file")
    parser.add_argument("--trace", default=None,
                        help="JSONL trace output file")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    ser = open_serial(args.port, args.baud)
    tracer = Tracer(args.trace, verbose=not args.quiet)
    lock = threading.RLock()
    write_lock = threading.Lock()

    def serial_write(chunk):
        with write_lock:
            ser.write(chunk)

    def hub_send(pkt, kind):
        tracer.tx(pkt, kind)
        faults.send_packet(pkt, kind)

    def hub_on_event(ev):
        if ev.get("event") == "rx":
            tracer.record(ev)
        elif ev.get("event") == "note":
            tracer.note(ev["text"])

    hub = HubModel(hub_send, on_event=hub_on_event)
    faults = FaultEngine(serial_write,
                         on_note=lambda text: tracer.note(text))

    def on_rx(parsed):
        with lock:
            hub.handle_packet(parsed.pkt)

    parser = sc.ScpParser(sc.ADDR_HUB, on_rx)

    dispatcher = Dispatcher(hub, faults, lock)
    control = None
    if args.control:
        host, _, port = args.control.rpartition(":")
        control = ControlServer(hub, faults, host or "127.0.0.1",
                                int(port), lock=lock,
                                on_start=lambda p: print(
                                    "control: 127.0.0.1:%d" % p,
                                    file=sys.stderr))

    scenario = None
    if args.scenario:
        scenario = ScenarioExecutor(args.scenario, dispatcher)
        scenario.start()

    boot_in_scenario = args.scenario is not None and (
        any(s.get("do") == "boot" for s in scenario.spec.get("steps", []))
        or any(s.get("do") == "boot" for s in scenario.spec.get("setup",
                                                                [])))
    if not args.noboot and not boot_in_scenario:
        hub.start_boot()

    print("=== RF HUB SIMULATOR (Teslim4 R1) ===", file=sys.stderr)
    print("port=%s baud=%d control=%s scenario=%s trace=%s" %
          (args.port, args.baud, args.control or "off",
           args.scenario or "-", args.trace or "-"), file=sys.stderr)

    try:
        start_watchdog(tracer)
        last_heartbeat = 0
        while True:
            now = int(time.monotonic() * 1000)
            if now - last_heartbeat >= 5000:
                last_heartbeat = now
                tracer.note("heartbeat rx=%d tx=%d disc=%d" % (
                    parser_state_counts(hub), tracer.index,
                    len(parser.discarded_chunks)))
            try:
                data = ser.read(4096)
            except Exception as read_err:            # noqa: BLE001
                tracer.note("serial read error: %r" % (read_err,))
                raise
            if data:
                base = int(time.monotonic() * 1000)
                for i, byte in enumerate(data):
                    parser.feed(byte, now_ms=base + i)
                    if parser.state == sc.ScpParser.READY:
                        parser.packet_done()
            with lock:
                hub.tick()
                if scenario is not None:
                    scenario.tick()
            faults.pump()
            while parser.discarded_chunks:
                _, chunk = parser.discarded_chunks.pop(0)
                tracer.raw_discard(chunk, now)
    except KeyboardInterrupt:
        pass
    finally:
        if control is not None:
            control.stop()
        tracer.close()
        ser.close()
        print("simulator closed", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
