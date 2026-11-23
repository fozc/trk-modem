"""Bench acceptance for IEC104 relay SBO and timestamped digital inputs."""

import argparse
import datetime
from pathlib import Path
import re
import time

import apdu
import console_io
from evidence import JsonlEvidence
from oracle import DIN_IOAS
from wire_master import WireMaster


RELAY_IOAS = (10002, 10003)
SINGLE_COMMAND = 45


def receive_asdu(wire, matches, since, timeout=15):
    def matches_frame(frame):
        asdu = frame[3].get("asdu")
        return (frame[1] == "rx" and asdu and asdu["ca"] == wire.ca
                and matches(asdu))

    found, frame = wire.wait(matches_frame, timeout, t0=since)
    if not found:
        raise RuntimeError("Expected IEC104 ASDU did not arrive")
    return frame[0], frame[3]["asdu"]


def read_din(wire, ioas=DIN_IOAS):
    started = time.monotonic()
    wire.interrogate(20)

    def is_gi(asdu):
        return (asdu["type"] == apdu.TYPE_C_IC_NA_1
                and asdu.get("objects", []) == [{"ioa": 0, "qoi": 20}])

    confirmed_at, confirmation = receive_asdu(
        wire, lambda asdu: is_gi(asdu) and asdu["cot"] == 7, started)
    if confirmation["pn"]:
        raise RuntimeError("Station interrogation was rejected")
    terminated_at, _ = receive_asdu(
        wire, lambda asdu: is_gi(asdu) and asdu["cot"] == 10
        and not asdu["pn"], confirmed_at)

    states = {}
    for received_at, direction, _, frame in wire.since(confirmed_at):
        asdu = frame.get("asdu")
        if (direction != "rx" or received_at >= terminated_at or not asdu
                or asdu["cot"] != 20 or asdu["ca"] != wire.ca):
            continue
        for obj in asdu.get("objects", []):
            ioa = obj["ioa"]
            if ioa not in ioas:
                continue
            if (asdu["type"] != apdu.TYPE_M_SP_NA_1 or asdu["pn"]
                    or obj["siq"]["iv"] or ioa in states):
                raise RuntimeError(f"Invalid or duplicate DIN object at IOA {ioa}")
            states[ioa] = obj["siq"]["value"]
    if set(states) != set(ioas):
        raise RuntimeError(f"Missing DIN IOAs: {sorted(set(ioas) - states.keys())}")
    return {ioa: states[ioa] for ioa in ioas}


def check_din_changes(wire, states, timeout):
    for channel, (ioa, original) in enumerate(states.items(), start=1):
        for expected in (1 - original, original):
            started = time.monotonic()
            print(f"DIN{channel} (IOA {ioa}): set logical state to {expected}",
                  flush=True)

            def is_event(asdu):
                return (asdu["type"] == apdu.TYPE_M_SP_TB_1
                        and asdu["cot"] == 3
                        and any(obj["ioa"] == ioa for obj in asdu.get("objects", [])))

            _, event = receive_asdu(wire, is_event, started, timeout)
            point = next(obj for obj in event["objects"] if obj["ioa"] == ioa)
            stamp = point["time"]
            if (event["pn"] or point["siq"]["iv"] or stamp["iv"]
                    or point["siq"]["value"] != expected
                    or not 0 <= stamp["ms"] < 60000):
                raise RuntimeError(f"Invalid DIN event at IOA {ioa}: {point}")
            seconds, milliseconds = divmod(stamp["ms"], 1000)
            event_time = datetime.datetime(
                stamp["year"], stamp["month"], stamp["day"], stamp["hour"],
                stamp["min"], seconds, milliseconds * 1000)
            print(f"PASS DIN{channel}={expected}, RTU time {event_time}", flush=True)
    if read_din(wire, tuple(states)) != states:
        raise RuntimeError("DIN states after the test differ from the initial GI")


def relay_states(console):
    output = console_io.run_cmd(console, "relay status", settle_s=1)
    states = {int(ch): state == "ON" for ch, state in
              re.findall(r"\[RELAY\] CH([12]): (ON|OFF)\b", output)}
    if states.keys() != {1, 2}:
        raise RuntimeError("RTU console did not return both relay states")
    return states


def relay_command(wire, ioa, state, *, select, negative=False):
    sco = int(state) | (0x80 if select else 0)
    started = time.monotonic()
    wire.command(SINGLE_COMMAND, ioa.to_bytes(3, "little") + bytes((sco,)))
    _, reply = receive_asdu(wire, lambda asdu: asdu["type"] == SINGLE_COMMAND,
                            started)
    expected_object = {"ioa": ioa, "raw_rest_hex": f"{sco:02x}"}
    if (reply["cot"] != 7 or reply.get("objects") != [expected_object]
            or reply["pn"] != int(negative)):
        raise RuntimeError(f"Unexpected Type 45 confirmation: {reply}")


def check_relays(wire, console, ioas=RELAY_IOAS):
    initial = relay_states(console)
    for channel, ioa in enumerate(ioas, start=1):
        desired = not initial[channel]
        try:
            relay_command(wire, ioa, desired, select=False, negative=True)
            if relay_states(console) != initial:
                raise RuntimeError("Unselected Execute changed a relay")
            relay_command(wire, ioa, desired, select=True)
            if relay_states(console) != initial:
                raise RuntimeError("Select changed a relay")
            relay_command(wire, ioa, desired, select=False)
            if relay_states(console) != {**initial, channel: desired}:
                raise RuntimeError(f"Execute did not change only relay {channel}")
            relay_command(wire, ioa, desired, select=False, negative=True)
        finally:
            current = relay_states(console)
            for changed_channel, changed_ioa in enumerate(ioas, start=1):
                previous = initial[changed_channel]
                if current[changed_channel] != previous:
                    relay_command(wire, changed_ioa, previous, select=True)
                    relay_command(wire, changed_ioa, previous, select=False)
            if relay_states(console) != initial:
                raise RuntimeError("Initial relay states could not be restored")
        print(f"PASS relay {channel} SBO, initial commanded state restored")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=2404)
    parser.add_argument("--ca", type=int, required=True)
    parser.add_argument("--console")
    parser.add_argument("--relay-ioas", nargs=2, type=int, default=RELAY_IOAS)
    parser.add_argument("--din-ioas", nargs=4, type=int, default=DIN_IOAS)
    parser.add_argument("--relay-test", action="store_true",
                        help="Change relay outputs on an isolated test bench")
    parser.add_argument("--din-changes", action="store_true",
                        help="Observe manually toggled inputs on the test bench")
    parser.add_argument("--din-timeout", type=float, default=30)
    args = parser.parse_args()
    if args.relay_test and not args.console:
        parser.error("--relay-test requires an RTU --console")
    if not 1 <= args.ca <= 65534 or not 1 <= args.port <= 65535:
        parser.error("CA and TCP port must be valid")
    if args.din_timeout <= 0:
        parser.error("--din-timeout must be positive")
    ioas = {*args.relay_ioas, *args.din_ioas}
    if len(ioas) != 6 or any(not 1 <= ioa <= 0xffffff for ioa in ioas):
        parser.error("Relay and DIN IOAs must be unique 24-bit nonzero addresses")

    run_dir = (Path(__file__).resolve().parents[2] / "build" / "iec104-master"
               / datetime.datetime.now().strftime("relay-din-%Y%m%d-%H%M%S-%f"))
    evidence = JsonlEvidence(run_dir / "apdu.jsonl")
    wire = WireMaster(args.host, args.port, args.ca, evidence)
    console = None
    try:
        if not wire.connect(timeout_s=15):
            raise RuntimeError("STARTDT_CON did not arrive")
        states = read_din(wire, args.din_ioas)
        print(f"PASS DIN general interrogation: {states}")
        if args.din_changes:
            check_din_changes(wire, states, args.din_timeout)
        if args.relay_test:
            console = console_io.open_console(args.console, run_dir / "console.log")
            check_relays(wire, console, args.relay_ioas)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    finally:
        wire.stop()
        if console is not None:
            console.close()
        evidence.close()
        print(f"APDU evidence: {evidence.path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
