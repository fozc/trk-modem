#!/usr/bin/env python3
"""RF HIL orchestrator: runs cases against the live DUT + simulator.

Per case:
  1. start tools/rf-hil/rf_hub_sim.py on --rf-port with the case's
     scenario timeline, a TCP control channel and a JSONL trace;
  2. open the interactive console on --console (rf log verbose, admin);
  3. execute the case (drives the DUT, asserts on trace + console);
  4. write report.md with PASS/FAIL/BLOCKED/DEFERRED + evidence.

Usage:
  python test/system/rf_hil/run_hil.py --rf-port COM10 --console COM16 \
      --firmware-image installed.efw --transport-profile 1
  python ... --case a1_boot_from_start --case e1_cfg_apply_happy
  python ... --list

Exit codes: 0 all selected cases PASS, 1 FAIL only, 2 incomplete/environment error.
"""

import argparse
import json
import socket
import subprocess
import sys
import time
import traceback
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT = TEST_ROOT.parent

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(REPO_ROOT / "tools" / "rf-hil"))

import cases as cases_mod                        # noqa: E402
from sim.control import ControlClient            # noqa: E402


class SimHandle:
    """Control client that survives simulator restarts (watchdog exit).

    The sim exits with code 86 when its main loop stalls in a stuck
    Windows serial read; the case's polling loop calls ensure_alive()
    and the sim restarts. The scenario runs from t=0 again, so a
    restart costs a fresh BOOT + inventory re-upload (DUT handles this
    per spec 1.10). The trace file appends across restarts.
    """

    def __init__(self, args, scenario_path, control_port, trace_path,
                 sim_log_path):
        self.args = args
        self.scenario_path = scenario_path
        self.control_port = control_port
        self.trace_path = trace_path
        self.sim_log_path = sim_log_path
        self.proc = None
        self.log = None
        self.client = None
        self.restart_count = 0

    def start(self):
        self.log = open(self.sim_log_path, "ab")
        self.proc = subprocess.Popen(
            [sys.executable, str(self.args.sim), self.args.rf_port,
             "--baud", str(self.args.baud),
             "--scenario", str(self.scenario_path),
             "--control", "127.0.0.1:%d" % self.control_port,
             "--trace", str(self.trace_path),
             "--quiet"],
            stdout=self.log, stderr=self.log)
        if not control_ready(self.control_port):
            raise RuntimeError("control channel did not come up")
        self.client = ControlClient("127.0.0.1", self.control_port)

    def ensure_alive(self):
        """Restart on stall; True when a restart happened."""
        if self.proc is not None and self.proc.poll() is None:
            return False
        if self.restart_count >= 2:
            return False
        self.restart_count += 1
        print("    [sim restart #%d (prev exit=%s)]" % (
            self.restart_count,
            self.proc.returncode if self.proc else "?"), flush=True)
        try:
            self.client.close()
        except (AttributeError, OSError):
            pass
        time.sleep(1.0)
        self.start()
        return True

    def call(self, **request):
        try:
            return self.client.call(**request)
        except (ConnectionError, OSError):
            self.ensure_alive()
            return self.client.call(**request)

    def close(self):
        try:
            self.client.close()
        except (AttributeError, OSError):
            pass
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.log:
            self.log.close()


class CaseContext:
    def __init__(self, sim, console, outdir, trace_path):
        self.sim = sim
        self.console = console
        self.outdir = outdir
        self.trace_path = trace_path
        self.evidence = []


def control_ready(port, timeout_s=15.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            ControlClient("127.0.0.1", port, timeout=1.0).close()
            return True
        except (ConnectionRefusedError, OSError, socket.timeout):
            time.sleep(0.3)
    return False


from identity import IdentityError, read_image_identity, result_exit_code


def _verify_running_dut(session, args, name, stage):
    from identity import IMAGE_PATTERN, IdentityError, parse_dut_identity, verify_dut_identity
    reply = session.send_and_wait("rf status", IMAGE_PATTERN, timeout_s=10.0)
    if reply is None:
        raise IdentityError("DUT did not report installed image/profile")
    observed = parse_dut_identity(reply[1])
    OBSERVED.setdefault("dut_images", {}).setdefault(name, {})[stage] = observed
    verify_dut_identity(observed, args.expected_image, args.transport_profile)
    OBSERVED["dut_images"][name][stage + "_verified"] = True


def run_case(name, spec, args, outdir):
    if name in cases_mod.PLANNED:
        return "DEFERRED", [cases_mod.PLANNED[name]]
    import console as console_mod
    from identity import IdentityError
    OBSERVED.setdefault("dut_images", {})[name] = {}
    case_dir = outdir / name
    case_dir.mkdir(parents=True, exist_ok=True)
    trace_path = case_dir / "sim_trace.jsonl"
    scenario_path = case_dir / "scenario.json"
    scenario_path.write_text(
        json.dumps(spec["scenario"], indent=1), encoding="ascii")
    control_port = 7788 + (hash(name) % 200)

    sim = SimHandle(args, scenario_path, control_port, trace_path,
                    case_dir / "sim_console.log")
    try:
        sim.start()
        session = console_mod.ConsoleSession(
            args.console, logfile=str(case_dir / "console.log"))
        try:
            _observe_hub_version(session)
            _verify_running_dut(session, args, name, "before")
            # NVRAM cslog master switch may be off after a reflash;
            # without it all CSLOG RF frame output is suppressed.
            session.send("cslog on")
            ctx = CaseContext(sim, session, case_dir, trace_path)
            try:
                snapshots = {"start": _snapshot(sim)}
                detail = spec["run"](ctx)
                _verify_running_dut(session, args, name, "after")
                snapshots["end"] = _snapshot(sim)
                _write_json(case_dir / "snapshots.json", snapshots)
                return "PASS", ([detail] if detail else []) + ctx.evidence
            except IdentityError as error:
                return "BLOCKED", [str(error)]
            except cases_mod.SkipCase as skip:
                return "DEFERRED", [str(skip)]
            except (cases_mod.CheckError, AssertionError):
                tb = traceback.format_exc(limit=6)
                return "FAIL", tb.strip().splitlines()
            except Exception:
                tb = traceback.format_exc(limit=6)
                return "ERROR", tb.strip().splitlines()
        finally:
            _observe_hub_version(session)
            session.close()
            sim.close()
    except (RuntimeError, OSError) as env_err:
        return "BLOCKED", ["environment: %r" % (env_err,)]


OBSERVED = {}


def _observe_hub_version(session):
    """Record the peer version separately from the installed DUT identity."""
    if "hub_fw" in OBSERVED:
        return
    import re
    rx = re.compile(r"hub up=\d+s fw=(\S+)")
    for _, text in session.all_lines():
        match = rx.search(text)
        if match:
            OBSERVED["hub_fw"] = match.group(1)
            return


def _snapshot(sim):
    try:
        return sim.call(do="status").get("state")
    except (ConnectionError, OSError, AttributeError):
        return None


def _write_json(path, payload):
    import json
    path.write_text(json.dumps(payload, indent=1, default=str),
                    encoding="ascii")


def write_manifest(outdir, args, selected, results):
    """Plan v1.2 section 11: run identity, firmware/profile proof,
    environment, corpus hashes and per-case model snapshots."""
    import hashlib
    import json
    import platform
    import subprocess as sp

    def git(args2):
        try:
            return sp.run(["git"] + args2, cwd=str(REPO_ROOT),
                          capture_output=True, text=True,
                          timeout=10).stdout.strip()
        except (OSError, sp.TimeoutExpired):
            return "unavailable"

    pyserial_version = "unavailable"
    try:
        import serial
        pyserial_version = serial.VERSION
    except (ImportError, AttributeError):
        pass

    captures = {}
    capture_root = (REPO_ROOT / "doc" / "BOLATeX_Teslim4_R1_RTU_Arayuzu_MD"
                    / "Ornek_SCP_Akislari")
    for path in sorted(capture_root.glob("*.csv")):
        captures[path.name] = hashlib.sha256(
            path.read_bytes()).hexdigest()

    identity_verified = bool(selected) and all(
        OBSERVED.get("dut_images", {}).get(name, {}).get(stage + "_verified", False)
        for name in selected for stage in ("before", "after"))
    manifest = {
        "run_id": outdir.name,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "repository_at_start": getattr(args, "repository_at_start", None),
        "repository_at_end": dict(git_commit=git(["rev-parse", "HEAD"]),
                                  git_dirty=bool(git(["status", "--porcelain"]))),
        "expected_image": getattr(args, "expected_image", None),
        "identity_source": "installed_boot_metadata_and_compiled_profile",
        "expected_transport_profile": args.transport_profile,
        "observed_dut_images": OBSERVED.get("dut_images", {}),
        "identity_verified": identity_verified,
        "complete": result_exit_code(results) == 0 and identity_verified,
        "scope": "selected_cases",
        "python": platform.python_version(),
        "pyserial": pyserial_version,
        "rf_port": args.rf_port,
        "console_port": args.console,
        "baud": args.baud,
        "observed_hub_fw": OBSERVED.get("hub_fw", "not observed"),
        "capture_hashes": captures,
        "selected_cases": selected,
        "results": {name: verdict for name, (verdict, _) in
                    results.items()},
    }
    _write_json(outdir / "manifest.json", manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rf-port", default="COM10")
    parser.add_argument("--console", default="COM16")
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--sim", default=str(REPO_ROOT / "tools" /
                                             "rf-hil" / "rf_hub_sim.py"))
    parser.add_argument("--case", action="append", default=None,
                        help="case name (repeatable); default: all "
                             "non-planned")
    parser.add_argument("--with-planned", action="store_true",
                        help="also run the registered planned cases "
                             "(they are DEFERRED)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--outdir", default=None)
    parser.add_argument("--firmware-image", help="Installed application EFW artifact")
    parser.add_argument("--transport-profile", type=int, choices=(0, 1),
                        help="Expected compiled DUT profile: 0 USART3, 1 RS485 bench")
    args = parser.parse_args()
    OBSERVED.clear()

    cases_mod.register_planned()

    if args.list:
        for name, spec in cases_mod.CASES.items():
            print("%-28s %s" % (name, spec["desc"]))
        return 0

    if args.case and len(set(args.case)) != len(args.case):
        parser.error("Duplicate --case selections would overwrite case evidence")

    outdir = Path(args.outdir) if args.outdir else (
        TEST_ROOT / "build" / "hil-rf" /
        time.strftime("%Y%m%d-%H%M%S"))
    outdir.mkdir(parents=True, exist_ok=True)

    if args.case:
        selected = args.case
    else:
        selected = [n for n in cases_mod.CASES
                    if n not in cases_mod.PLANNED or args.with_planned]

    import subprocess
    def source_git(command):
        return subprocess.check_output(["git"] + command, cwd=REPO_ROOT,
                                       text=True).strip()
    args.repository_at_start = dict(git_commit=source_git(["rev-parse", "HEAD"]),
                                   git_dirty=bool(source_git(["status", "--porcelain"])))
    args.expected_image = None
    identity_error = None
    if any(name not in cases_mod.PLANNED for name in selected):
        try:
            if args.firmware_image is None or args.transport_profile is None:
                raise IdentityError("--firmware-image and --transport-profile are required")
            args.expected_image = read_image_identity(args.firmware_image)
        except (OSError, IdentityError) as error:
            identity_error = str(error)
    results = {}
    for name in selected:
        spec = cases_mod.CASES.get(name)
        if spec is None:
            print("unknown case: %s (--list)" % name, file=sys.stderr)
            return 2
        print("=== CASE %s ===" % name, flush=True)
        if identity_error is not None:
            verdict, evidence = "BLOCKED", [identity_error]
        else:
            verdict, evidence = run_case(name, spec, args, outdir)
        results[name] = (verdict, evidence)
        print("%s: %s" % (verdict, name), flush=True)
        for line in evidence[:12]:
            print("    %s" % (line,))

    report = outdir / "report.md"
    lines = ["# RF HIL Raporu", "",
             "Tarih: %s" % time.strftime("%Y-%m-%d %H:%M:%S"), "",
             "| Vaka | Sonuc |", "|---|---|"]
    for name, (verdict, _) in results.items():
        lines.append("| %s | %s |" % (name, verdict))
    lines += ["", "## Detaylar", ""]
    for name, (verdict, evidence) in results.items():
        lines.append("### %s - %s" % (name, verdict))
        lines.append("")
        if (outdir / name).exists():
            lines.append("Kanal izleri: `%s/`" % name)
        else:
            lines.append("Case not executed; no channel artifacts.")
        for line in evidence:
            lines.append("- %s" % (line,))
        lines.append("")
    report.write_text("\n".join(lines), encoding="ascii")

    manifest = write_manifest(outdir, args, selected, results)
    print("\nreport: %s" % report)
    print("manifest: %s (%s, %s)" % (outdir / "manifest.json",
                                     manifest["repository_at_start"]["git_commit"],
                                     manifest["expected_transport_profile"]))
    code = result_exit_code(results)
    return 2 if code == 0 and not manifest["identity_verified"] else code


if __name__ == "__main__":
    raise SystemExit(main())
