"""Bench GSM injection tests. Requires the temporary signed GSM fixture.

Uses the existing serial helper. Never sends PIN/PUK or writes user settings.
Each reset test must finish with real HTTP and IEC104 traffic again.
"""
import argparse
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/hwtest"))
sys.path.insert(0, str(ROOT / "test/system"))
from serial_io import ConsoleSession
from gsm_disconnect_test import get_page, IecProbe


def arm(console, mode):
    console.send("su admin")
    time.sleep(0.3)
    for _ in range(30):
        start = len(console.all_lines())
        console.send("gsmtest " + mode)
        hit = console.wait_for(r"GSMTEST: (armed .*|busy, not armed)", 3, since=start)
        if hit and "armed " + mode in hit[1]:
            return start
        time.sleep(0.2)
    raise AssertionError("could not arm " + mode)


def expect(console, pattern, start, timeout=30):
    hit = console.wait_for(pattern, timeout, since=start)
    if hit is None:
        raise AssertionError("missing device evidence: " + pattern)
    return hit


def network(console, host, response_timeout=8):
    body = get_page(host, console, timeout=180, response_timeout=response_timeout)
    probe = IecProbe(host)
    try:
        probe.ping()
    finally:
        probe.close()
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--console", default="COM18")
    parser.add_argument("--case", choices=("transport", "initfail", "busy", "stall", "prompt", "ack", "pdp", "detach", "sim"), required=True)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    console = ConsoleSession(args.console, logfile=args.log)
    try:
        console.send("su admin")
        start = len(console.all_lines())
        console.send("gsmtest status")
        expect(console, r"GSMTEST: mode=", start, 5)
        reference = network(console, args.host)
        print("PASS baseline HTTP/IEC", flush=True)
        if args.case == "transport":
            for mode in ("silent", "hold", "dmabusy", "tc"):
                start = arm(console, mode)
                console.send("gsm at +CSQ")
                if mode in ("silent", "hold"):
                    expect(console, r"TIMEOUT", start, 30)
                    expect(console, r"RX fault expired", start, 35)
                elif mode == "dmabusy":
                    expect(console, r"HAL DMA busy injected", start, 15)
                else:
                    expect(console, r"Sent: at", start, 15)
                time.sleep(4)
                if network(console, args.host) != reference:
                    raise AssertionError("HTTP body changed after " + mode)
                print("PASS %s: actual HTTP/IEC recovered" % mode, flush=True)
        elif args.case in ("busy", "stall"):
            start = arm(console, args.case)
            if args.case == "busy":
                expect(console, r"Soft recovery \(1/2\)", start)
                start = arm(console, "agebusy")
                expect(console, r"Soft recovery \(2/2\)", start)
                start = arm(console, "agebusy")
                expect(console, r"Hard reset - 2 soft recoveries exhausted", start)
            else:
                expect(console, r"kurtarma 1/2", start)
                for number in (2, 3):
                    time.sleep(35)
                    start = arm(console, "age")
                    expect(console, r"kurtarma %d/2" % number, start)
                expect(console, r"kurtarma tukendi - hard reset", start)
            expect(console, r"GSMTEST FIXTURE ACTIVE", start, 50)
            if network(console, args.host) != reference:
                raise AssertionError("HTTP body changed after MCU reset")
            print("PASS %s escalation and post-reset HTTP/IEC" % args.case, flush=True)
        elif args.case == "initfail":
            start = arm(console, "initfail")
            expect(console, r"init error injected", start, 100)
            expect(console, r"cold boot \(2/2\)", start, 400)
            expect(console, r"Waiting for EWDT reset", start, 250)
            expect(console, r"GSMTEST FIXTURE ACTIVE", start, 250)
            if network(console, args.host) != reference:
                raise AssertionError("HTTP body changed after init exhaustion")
            print("PASS init exhaustion and post-reset HTTP/IEC", flush=True)
        elif args.case in ("pdp", "detach"):
            start = len(console.all_lines())
            command = "#SGACT=1,0" if args.case == "pdp" else "+CGATT=0"
            console.send("gsm at " + command)
            expect(console, r"Sent: at", start, 20)
            expect(console, r"^OK$", start, 40)
            if network(console, args.host) != reference:
                raise AssertionError("HTTP body changed after " + args.case)
            print("PASS %s: autonomous HTTP/IEC recovery" % args.case, flush=True)
        elif args.case == "sim":
            start = len(console.all_lines())
            console.send("gsm at #SIMDET?")
            hit = expect(console, r"^#SIMDET: (\d),", start, 20)
            original_mode = hit[2].group(1)
            try:
                start = len(console.all_lines())
                console.send("gsm at #SIMDET=0")
                # SIMDET changes logical detection mode, not SIMIN contacts.
                # This modem reports QSS=1; only a real QSS=0 proves removal.
                expect(console, r"AT URC: #QSS: [01]", start, 30)
                time.sleep(25)
            finally:
                console.send("su admin")
                time.sleep(0.3)
                restore_start = len(console.all_lines())
                console.send("gsm at #SIMDET=" + original_mode)
                expect(console, r"^OK$", restore_start, 30)
                restore_start = len(console.all_lines())
                console.send("gsm at #SIMDET?")
                expect(console, r"^#SIMDET: " + original_mode + ",", restore_start, 30)
            if network(console, args.host) != reference:
                raise AssertionError("HTTP body changed after simulated SIM removal")
            print("PASS SIMDET mode cycle, original mode verified, HTTP/IEC (not physical SIM removal)", flush=True)
        else:
            start = arm(console, args.case)
            try:
                wait = 45 if args.case == "ack" else 8
                if network(console, args.host, response_timeout=wait) != reference:
                    raise AssertionError("HTTP body changed after " + args.case)
            finally:
                expect(console, r"dropped response", start, 10)
            print("PASS %s: HTTP/IEC recovered" % args.case, flush=True)
    finally:
        console.close()


if __name__ == "__main__":
    main()
