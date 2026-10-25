"""Long GSM HTTP/IEC104 bench run on the normal production image.

No settings, process commands, SIM credentials or firmware writes.
Metrics are replaced after every cycle; serial capture is append-only.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

from gsm_disconnect_test import get_page, IecProbe

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/hwtest"))
from serial_io import ConsoleSession


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--console", default="COM18")
    parser.add_argument("--hours", type=float, default=24)
    parser.add_argument("--interval", type=float, default=30)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()
    if args.hours <= 0 or args.interval < 1:
        parser.error("hours must be positive; interval must be >= 1")
    args.outdir.mkdir(parents=True, exist_ok=False)
    metrics = {"state": "running", "started": time.time(), "pid": os.getpid(),
               "target_hours": args.hours, "cycles": 0, "failures": [],
               "max_cycle_seconds": 0, "unexpected_resets": 0}
    output = args.outdir / "metrics.json"

    def save():
        metrics["updated"] = time.time()
        metrics["elapsed_hours"] = (time.time() - metrics["started"]) / 3600
        temporary = output.with_suffix(".tmp")
        temporary.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
        temporary.replace(output)

    console = ConsoleSession(args.console, logfile=args.outdir / "serial.log")
    probe = None
    cursor = 0
    try:
        reference = get_page(args.host, console, timeout=100)
        probe = IecProbe(args.host)
        consecutive = 0
        save()
        while time.time() - metrics["started"] < args.hours * 3600:
            started = time.monotonic()
            try:
                if probe is None:
                    probe = IecProbe(args.host)
                probe.ping()
                if get_page(args.host, console) != reference:
                    raise AssertionError("HTTP response body changed")
                probe.ping()
                consecutive = 0
                metrics["cycles"] += 1
            except Exception as error:
                consecutive += 1
                metrics["failures"].append({"at": time.time(), "error": repr(error)})
                if probe is not None:
                    probe.close()
                probe = None
                if consecutive >= 5:
                    raise AssertionError("five consecutive failed cycles") from error
            lines = console.all_lines()
            for _, line in lines[cursor:]:
                if any(marker in line for marker in
                       ("Powering on GSM", "HardFault", "GSMTEST FIXTURE", "Waiting for EWDT")):
                    metrics["unexpected_resets"] += 1
                    metrics["failures"].append({"at": time.time(), "error": line})
            cursor = len(lines)
            elapsed = time.monotonic() - started
            metrics["max_cycle_seconds"] = max(metrics["max_cycle_seconds"], elapsed)
            save()
            print("cycles=%d failures=%d elapsed=%.3fh" %
                  (metrics["cycles"], len(metrics["failures"]), metrics["elapsed_hours"]), flush=True)
            time.sleep(max(0, args.interval - elapsed))
        metrics["state"] = "passed" if not metrics["failures"] else "failed"
    except Exception as error:
        metrics["state"] = "failed"
        metrics["failures"].append({"at": time.time(), "error": repr(error)})
        raise
    finally:
        if probe is not None:
            probe.close()
        console.close()
        save()


if __name__ == "__main__":
    main()
