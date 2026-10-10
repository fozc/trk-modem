#!/usr/bin/env python3
"""IEC104 master acceptance suite orchestrator (device-level).

Runs the i01..i07 case catalog from cases.py against a live device over
its GSM IEC-104 listener (default port 2404), using the c104 library
(pinned 2.2.1, bench venv) as an independent master-side oracle.

Verdicts and exit codes (plan item 1):
  - summary prints PASS/FAIL/SKIP/DEFERRED counts and an explicit
    verdict line: ACCEPTANCE PASS (only PASS), PARTIAL (SKIP/DEFERRED
    present) or FAIL;
  - any FAIL -> exit 1;
  - --require-device: unreachable device or any SKIP/DEFERRED -> exit 1
    (a requested acceptance run must not slide through on skips);
  - without --host the suite SKIPs with exit 0 (no default IP, plan
    item 2 - the integration wrapper applies the same rule).

Usage:
  python run_suite.py --host 188.59.76.59 [--console COM16]
      [--require-device] [--setup-feeders] [--clock-sync]
      [--mutate-eventlog] [--evtlog-count 5] [--case i01_...] [--list]
"""

import argparse
import os
import platform
import subprocess
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _run_with_venv(args):
    """Ensure the suite runs where c104 is importable.

    Returns None when c104 is already importable in this interpreter
    (run in-process); otherwise runs the suite under the .venv python as
    a child process and returns its exit code, or None-ish skip codes
    when no venv exists. (os.execv is NOT usable on Windows here: the
    parent exits immediately and the child detaches from the console.)
    """
    try:
        import c104  # noqa: F401
        return None
    except ImportError:
        pass
    candidates = [HERE / ".venv" / "Scripts" / "python.exe",
                  HERE / ".venv" / "bin" / "python"]
    for candidate in candidates:
        if candidate.exists():
            command = [str(candidate), str(Path(__file__).resolve())]
            command += sys.argv[1:]
            return subprocess.call(command)
    print("SKIP: c104 kurulu degil (bkz. README.md; .venv yok)")
    return 1 if args.require_device else 0


def _git_state():
    def git(*args):
        try:
            return subprocess.check_output(
                ["git"] + list(args), cwd=str(HERE.parents[2]),
                text=True, stderr=subprocess.DEVNULL).strip()
        except Exception:            # noqa: BLE001
            return "?"
    return {"commit": git("rev-parse", "HEAD"),
            "dirty": bool(git("status", "--porcelain"))}


def main():
    parser = argparse.ArgumentParser(
        description="IEC104 master acceptance suite (c104)", add_help=True)
    parser.add_argument("--host", default=None,
                        help="cihaz IP (yok: SKIP; varsayilan IP YOK)")
    parser.add_argument("--port", type=int, default=None,
                        help="IEC104 port (varsayilan: /config/iec104)")
    parser.add_argument("--console", default=None,
                        help="cihaz konsol COM port (or. COM16)")
    parser.add_argument("--require-device", action="store_true",
                        help="cihaz erisilemez veya vaka SKIP kalirsa "
                             "exit 1")
    parser.add_argument("--setup-feeders", action="store_true",
                        help="RF store bossa fider 1,2 yapilandir "
                             "(NVRAM degisikligi)")
    parser.add_argument("--clock-sync", action="store_true",
                        help="i04 saat senkronu uygula (cihaz RTC "
                             "degisir)")
    parser.add_argument("--mutate-eventlog", action="store_true",
                        help="i06 sentetik kayit enjeksiyonu (Flash/"
                             "NVRAM degisikligi)")
    parser.add_argument("--evtlog-count", type=int, default=5)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default=None,
                        help="yoksa IP-turevli admin sifresi")
    parser.add_argument("--case", action="append", default=None)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--outdir", default=None)
    args = parser.parse_args()

    import cases
    import console_io
    import oracle as oracle_mod

    if args.list:
        for name, (_, desc) in sorted(cases.CASES.items()):
            print("%-24s %s" % (name, desc))
        return 0

    if args.case and len(set(args.case)) != len(args.case):
        parser.error("tekrarli --case secimi kanit uzerine yazar")

    if not args.host:
        print("SKIP: hedef verilmedi (--host / IEC104_DEVICE_HOST)")
        return 1 if args.require_device else 0

    child_exit = _run_with_venv(args)
    if child_exit is not None:
        return child_exit

    # c104-dependent imports must stay below the re-exec gate
    import evidence
    import master104

    results = {}
    console = None
    master = None
    try:
        # ---- oracle (web + console) ------------------------------------
        try:
            password = args.password or \
                oracle_mod.derive_admin_password(args.host)
            web = oracle_mod.WebDevice(args.host)
            web.login(args.user, password)
            snap = oracle_mod.read_oracle(web)
        except Exception as error:        # noqa: BLE001
            print("BLOCKED: cihaz oracle adimi basarisiz: %s" % error)
            return 1 if args.require_device else 0

        port = args.port or snap["port"]

        stamp = time.strftime("%Y%m%d-%H%M%S")
        outdir = Path(args.outdir) if args.outdir else (
            HERE.parents[1] / "build" / "iec104-master" / stamp)
        outdir.mkdir(parents=True, exist_ok=True)

        if args.console:
            try:
                console = console_io.open_console(
                    args.console, logfile=str(outdir / "console.log"))
            except Exception as error:    # noqa: BLE001
                print("UYARI: konsol acilamadi (%s); konsol vakalari "
                      "SKIP olacak" % error)
                console = None
        console_state = oracle_mod.console_snapshot(console, console_io)
        if console_state.get("gsm_ip") and \
                console_state["gsm_ip"] != args.host:
            print("UYARI: konsol IP %s != hedef %s" %
                  (console_state["gsm_ip"], args.host))

        feeders = oracle_mod.store_feeders(snap["rf_store"])
        if args.setup_feeders:
            wanted = [1, 2]
            if not feeders:
                web.request("/config/rf",
                            oracle_mod.build_rf_store_config(wanted, 1))
                print("--setup-feeders: RF store fider=%s yazildi" % wanted)
            if oracle_mod.iec104_lines_empty(snap):
                web.request("/config/iec104",
                            oracle_mod.build_iec104_lines_config(wanted))
                print("--setup-feeders: IEC104 Hatlar=%s acildi" % wanted)
            snap = oracle_mod.read_oracle(web)
            feeders = oracle_mod.store_feeders(snap["rf_store"])
            print("--setup-feeders: RF fiderler=%s, IEC104 hatlar=%s" % (
                feeders,
                [line["index"] + 1 for line in snap["lines"]
                 if line["in_use"]]))
        snap["feeders"] = feeders

        expected_sets = oracle_mod.expected_ioa_sets(snap)
        # Closing snapshot after a replay drain = RF comm states plus
        # trip-failed points of in-use lines (iec104.c snapshot sender).
        snapshot = set()
        for line in snap["lines"]:
            if line["in_use"]:
                snapshot.update(line["ioa"]["rf"])
                snapshot.update(line["ioa"]["trip"])
        expected = {"station": expected_sets[0],
                    "group1": expected_sets[1],
                    "group2": expected_sets[2],
                    "fault": expected_sets[3],
                    "snapshot": snapshot}

        # ---- run --------------------------------------------------------
        jsonl = evidence.JsonlEvidence(outdir / "apdu.jsonl")

        master = master104.Master104(
            args.host, port, snap["ca"], jsonl,
            keep_alive_s=snap["t3"] + 30)
        master.start()
        ctx = cases.Ctx(master, snap, console, console_state, args,
                        expected, outdir)

        selected = args.case or list(cases.CASES)
        for name in selected:
            if name not in cases.CASES:
                print("bilinmeyen vaka: %s (--list)" % name)
                return 2
            print("=== CASE %s ===" % name, flush=True)
            try:
                verdict, lines = cases.CASES[name][0](ctx)
            except Exception:            # noqa: BLE001
                traceback.print_exc()
                verdict, lines = "ERROR", [traceback.format_exc()[-400:]]
            results[name] = verdict
            print("%s: %s" % (verdict, name), flush=True)
            for line in lines:
                print("    %s" % line, flush=True)

        master.stop()
        master = None
        jsonl.close()

        # ---- summary ----------------------------------------------------
        counts = {key: sum(1 for value in results.values() if value == key)
                  for key in ("PASS", "FAIL", "SKIP", "DEFERRED", "ERROR")}
        fails = counts["FAIL"] + counts["ERROR"]
        if fails:
            verdict_line = "FAIL"
        elif counts["PASS"] == len(results) and results:
            verdict_line = "ACCEPTANCE PASS"
        else:
            verdict_line = "PARTIAL (SKIP/DEFERRED var)"
        print("\nPASS=%d FAIL=%d SKIP=%d DEFERRED=%d ERROR=%d -> %s" % (
            counts["PASS"], counts["FAIL"], counts["SKIP"],
            counts["DEFERRED"], counts["ERROR"], verdict_line))

        import c104 as c104_mod
        manifest = {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "git": _git_state(),
            "host": args.host, "port": port,
            "python": platform.python_version(),
            "c104": getattr(c104_mod, "__version__", "2.2.1(pinned)"),
            "oracle": snap, "console_state": console_state,
            "results": results, "verdict": verdict_line,
        }
        evidence.write_manifest(outdir, manifest)
        print("rapor: %s" % (outdir / "manifest.json"))

        if fails:
            return 1
        if args.require_device and (counts["SKIP"] or counts["DEFERRED"]):
            return 1
        return 0
    finally:
        if master is not None:
            master.stop()
        if console is not None:
            console.close()


if __name__ == "__main__":
    raise SystemExit(main())
