#!/usr/bin/env python3
"""Real-MH observation smoke suite (production profile 0).

The HIL suite drives a PC simulator that plays the MH; this suite is its
independent counterpart against the REAL Modem RF Hub and real ayirici
cards. The real hub cannot be commanded from the bench, so every check is
observational: enable verbose RF logging on the DUT console and verify
what the real link actually delivers.

Checks (see README.md):
  m1_hub_fw        GET_STATUS reply names the expected firmware
  m2_inventory     inventory is loaded on the DUT
  m3_live_boot     LIVE_DATA flows and carries Boot_Counter (bytes 31-32)
  m4_ring_consume  a 0x47 bell is followed by a 0x48 consume
  m5_epoch_201     (--epoch) renewal writes event 201 (+120) records
  m6_bootless      (--reset-dut) DUT reboot rebuilds without BOOT_NOTIFY

Prerequisites: production image (RTU image profile=0), the real MH
attached to USART3, console port open. The runner BLOCKS on profile!=0 -
the HIL bench image ignores the real hub entirely.

Usage:
  python run_smoke.py --console COM16 [--fw 022555bf] [--duration 60]
      [--104-host <device_ip>] [--epoch] [--reset-dut] [--case m1_hub_fw]
      [--list] [--outdir DIR]
"""

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "hwtest"))

from serial_io import ConsoleSession  # noqa: E402

PROGRAMMER = "STM32_Programmer_CLI.exe"

CASES = {}


def case(name, desc):
    def register(function):
        CASES[name] = (function, desc)
        return function
    return register


def frame_bytes(line_text):
    """Parse the hex payload of an 'RF RX[n]: [.. ..]' console line."""
    match = re.search(r"RF RX\[\d+\]:\s*\[([0-9A-Fa-f ]+)\]", line_text)
    if match is None:
        return None
    try:
        return bytes(int(part, 16) for part in match.group(1).split())
    except ValueError:
        return None


def live_bodies(raw_frames):
    """33-byte LIVE_DATA bodies from raw console frames.

    Wire layout: [dst, src, type, cmd, seq, len, body...]. Body: src(1)
    + seq(4) + live(28); Boot_Counter is live bytes 26-27 = body 31-32.
    """
    bodies = []
    for frame in raw_frames:
        if len(frame) < 39:
            continue
        for offset in range(len(frame) - 38):
            if (2 == frame[offset] and 1 == frame[offset + 1] and
                    2 == frame[offset + 2] and 0x11 == frame[offset + 3] and
                    33 == frame[offset + 5]):
                bodies.append(frame[offset + 6:offset + 39])
                break
    return bodies


def log_records(raw_frames):
    """(cmd, record bytes) from 0x42/0x44 ACK replies on the console."""
    found = []
    for frame in raw_frames:
        for offset in range(max(1, len(frame) - 62)):
            if (2 != frame[offset] or 1 != frame[offset + 1] or
                    3 != frame[offset + 2]):
                continue
            cmd = frame[offset + 3]
            if cmd not in (0x42, 0x44):
                continue
            length = frame[offset + 5]
            body = frame[offset + 6:offset + 6 + length]
            for index in range(0, len(body) - 59, 60):
                found.append((cmd, body[index:index + 60]))
    return found


def sent_cmds(lines):
    """Commands the DUT sent: '[RF ST->RF] cmd=0xNN ...' console lines."""
    result = []
    for text in lines:
        match = re.search(r"ST->RF\] cmd=0x([0-9A-Fa-f]{2})", text)
        if match:
            result.append(int(match.group(1), 16))
    return result


class Ctx:
    def __init__(self, args, console, outdir):
        self.args = args
        self.console = console
        self.outdir = outdir
        self.evidence = []


def run_lines(ctx, command, settle_s=3.0):
    """Send a shell command, return its fresh output lines."""
    ctx.console.mark()
    ctx.console.send(command)
    time.sleep(settle_s)
    return [text for _, text in ctx.console.new_lines()]


def observe(ctx, seconds):
    """Collect console lines for a passive observation window."""
    ctx.console.mark()
    time.sleep(seconds)
    return [text for _, text in ctx.console.new_lines()]


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------


@case("m1_hub_fw", "GET_STATUS yanitinda beklenen MH firmware kimligi")
def m1_hub_fw(ctx):
    lines = run_lines(ctx, "rf status", settle_s=4.0)
    ctx.evidence += [text for text in lines if "hub" in text.lower()][:4]
    fw_lines = [text for text in lines if "hub up=" in text]
    if not fw_lines:
        fw_lines = [text for text in observe(ctx, ctx.args.duration)
                    if "hub up=" in text]
    if not fw_lines:
        return "SKIP", ["pencerede GET_STATUS yaniti gorulmedi "
                        "(gercek MH bagli mi?)"]
    match = re.search(r"hub up=\d+s fw=(\S+)", fw_lines[-1])
    if match is None:
        return "FAIL", ["fw alani ayristirilamadi: %s" % fw_lines[-1]]
    if match.group(1) != ctx.args.fw:
        return "FAIL", ["MH firmware %s (beklenen %s)" %
                        (match.group(1), ctx.args.fw)]
    return "PASS", ["MH firmware=%s" % match.group(1)]


@case("m2_inventory", "RTU envanter yukleme durumu YUKLU")
def m2_inventory(ctx):
    status = run_lines(ctx, "rf status", settle_s=4.0)
    joined = "\n".join(status)
    if re.search(r"\bEnvanter:\s*YUKLU\b", joined) is None:
        return "FAIL", ["Envanter Yuklu degil: %s" %
                        [t for t in status if "Envanter" in t][:2]]
    # rf inv starts a new upload; it is not an inventory listing command.
    ctx.evidence += status[:8]
    return "PASS", ["RTU envanter durumu YUKLU; fiziksel AY/EUI "
                    "eslesmesi ve cihaz sayisi bu vakayla dogrulanmaz"]


@case("m3_live_boot", "LIVE_DATA akiyor + Boot_Counter tasiyor")
def m3_live_boot(ctx):
    lines = observe(ctx, ctx.args.duration)
    live_lines = [text for text in lines if "LIVE_DATA" in text]
    if not live_lines:
        return "FAIL", ["%.0f s pencerede LIVE_DATA yok (dunku VINCI "
                        "timetag-IV koku de buydu)" % ctx.args.duration]
    frames = [frame for frame in (frame_bytes(text) for text in lines)
              if frame is not None]
    bodies = live_bodies(frames)
    if not bodies:
        return "FAIL", ["LIVE_DATA satiri var ama ham cerceve ayristirilamadi"]
    per_src = {}
    for body in bodies:
        per_src.setdefault(body[0], set()).add(
            body[31] | (body[32] << 8))
    zero = {src: counters for src, counters in per_src.items()
            if 0 in counters}
    if zero:
        return "FAIL", ["Boot_Counter=0 tasiyan kaynaklar: %s "
                        "(BQ-19: sifir bilinmiyor demektir)" %
                        sorted(zero)]
    return "PASS", ["%d canli cerceve, %d kaynak, Boot_Counter>0 hepsinde" %
                    (len(bodies), len(per_src))]


@case("m4_ring_consume", "0x47 zili 0x48 tuketmesiyle sonlanir")
def m4_ring_consume(ctx):
    lines = observe(ctx, ctx.args.duration)
    bells = [text for text in lines if "LOG_AVAILABLE" in text]
    consumed = [cmd for cmd in sent_cmds(lines) if 0x48 == cmd]
    if consumed:
        return "PASS", ["0x48 tuketmesi goruldu (%d istek)" % len(consumed)]
    if bells:
        return "FAIL", ["0x47 zili geldi (%d) ama 0x48 tuketmesi yok" %
                        len(bells)]
    return "SKIP", ["bekleyen kayit yok: zil/tuketme gozlenmedi (zorlanamaz)"]


@case("m5_epoch_201", "(--epoch) yenileme 201 + 120 kayitlari yazar")
def m5_epoch_201(ctx):
    if not ctx.args.epoch:
        return "SKIP", ["--epoch ile acilir (MH'ye 0x2A gonderir)"]
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send(ctx.args.admin_pass)
    time.sleep(0.3)
    ctx.console.send("rf epoch 1")
    deadline = time.monotonic() + 100.0
    records = []
    while time.monotonic() < deadline:
        fresh = [text for _, text in ctx.console.new_lines()]
        records += log_records(
            [f for f in (frame_bytes(t) for t in fresh)
             if f is not None])
        codes = [rec[7] for _, rec in records]
        if 201 in codes:
            break
        time.sleep(2.0)
    codes = [rec[7] for _, rec in records]
    if 201 not in codes:
        return "FAIL", ["%.0f s icinde olay 201 kaydi okunmadi (kodlar=%s)" %
                        (100.0, sorted(set(codes))[:10])]
    return "PASS", ["olay 201 okundu; 120 sayisi=%d; kayit=%d" %
                    (codes.count(120), len(codes))]


@case("m6_bootless", "(--reset-dut) DUT reset: BOOT'suz kurulum")
def m6_bootless(ctx):
    if not ctx.args.reset_dut:
        return "SKIP", ["--reset-dut ile acilir (cihazi resetler)"]
    subprocess.run([PROGRAMMER, "-c", "port=SWD mode=UnderReset reset=SWRST",
                    "-rst"], capture_output=True, text=True, timeout=60)
    time.sleep(8.0)
    ctx.console.send("rf log verbose")
    deadline = time.monotonic() + 90.0
    lines = []
    while time.monotonic() < deadline:
        lines += [text for _, text in ctx.console.new_lines()]
        if "envanter yuklendi" in "\n".join(lines):
            break
        time.sleep(1.0)
    joined = "\n".join(lines)
    if "BOOT_NOTIFY" in joined:
        return "FAIL", ["BOOT_NOTIFY geldi: MH de yeniden baslamis olmali "
                        "(test penceresi MH ayakta iken tekrar)"]
    if "envanter yuklendi" not in joined:
        return "FAIL", ["90 s icinde envanter yuklenmedi"]
    cmds = sent_cmds(lines)
    if 0x05 not in cmds:
        return "FAIL", ["0x05 INVENTORY_END gozlenmedi"]
    return "PASS", ["BOOT'suz kurulum: gonderilen komutlar=%s" %
                    ["0x%02X" % c for c in cmds[:12]]]


@case("m7_asdu_quality",
      "(--104-host) canli fiderlerin ASDU kalite/timetag'i gecerli")
def m7_asdu_quality(ctx):
    """Live points must carry valid measurement quality AND timestamp.

    Closes the 2026-10-10 review point: m3 (LIVE_DATA on the wire) alone
    does not prove the SCADA side sees valid data. With live traffic on
    the console, the station interrogation must contain at least one
    non-invalid measurement, and every non-invalid measurement must also
    carry a valid (iv=0) CP56 timestamp - the exact VINCI timetag-IV
    symptom class."""
    if not ctx.args.host_104:
        return "SKIP", ["--104-host <ip> ile acilir (c104 venv gerekir)"]
    live_lines = [text for text in observe(ctx, 12.0)
                  if "LIVE_DATA" in text]
    if not live_lines:
        return "SKIP", ["12 s pencerede LIVE_DATA yok: kalite kontrolu "
                        "icin once canli akis gerekir (m3'u incele)"]
    try:
        suite_root = REPO_ROOT / "test" / "system" / "iec104_master"
        sys.path.insert(0, str(suite_root))
        import master104  # noqa: E402
        import apdu  # noqa: E402
    except ImportError as error:
        return "SKIP", ["c104 yok (%r): test/system/iec104_master/.venv "
                        "kurulu olmali" % error]

    class NullEvidence:
        def write(self, record):
            pass

    master = master104.Master104(ctx.args.host_104, ctx.args.port_104,
                                 1, NullEvidence(), keep_alive_s=90)
    master.start()
    try:
        if not master.connect(timeout_s=45.0):
            return "FAIL", ["104 baglantisi kurulamadi (STARTDT_CON yok)"]
        master.interrogate(20)
        t0 = time.monotonic()
        ok, term = master.wait(
            lambda f: f[1] == "rx" and f[3] and
            (f[3].get("asdu") or {}).get("type") ==
            apdu.TYPE_C_IC_NA_1 and
            (f[3].get("asdu") or {}).get("cot") == 10, 30.0, t0=t0)
        measurements = []
        deadline = time.monotonic() + (term[0] - t0 if ok else 30.0) + 1.0
        for frame in master.since(t0):
            if deadline < frame[0]:
                break
            asdu = frame[3].get("asdu") or {}
            if ("rx" == frame[1] and
                    apdu.TYPE_M_ME_TF_1 == asdu.get("type")):
                for obj in asdu.get("objects", []):
                    measurements.append(obj)
        if not measurements:
            return "FAIL", ["GI yanitinda olcum (M_ME_TF_1) noktasi yok"]
        valid = [obj for obj in measurements
                 if not (obj.get("qds") or {}).get("iv")]
        if not valid:
            return "FAIL", ["canli akis var ama tum olcum noktalari IV "
                            "(VINCI timetag-IV belirtisi ayni)"]
        bad_stamp = [obj["ioa"] for obj in valid
                     if (obj.get("time") or {}).get("iv")]
        if bad_stamp:
            return "FAIL", ["gecerli olcum + gecersiz zaman damgasi "
                            "iv=1: IOA=%s" % sorted(bad_stamp)[:8]]
        return "PASS", ["%d olcum noktasi: %d gecerli (kalite+damga), "
                        "%d IV" % (len(measurements), len(valid),
                                   len(measurements) - len(valid))]
    finally:
        master.disconnect()
        master.stop()


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------


def dut_profile(console):
    console.mark()
    console.send("rf status")
    time.sleep(3.0)
    for _, text in console.new_lines():
        match = re.search(r"RTU image:.*profile=(\d)", text)
        if match:
            return int(match.group(1))
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Gercek MH gozlem duman suiti (uretim profili 0)")
    parser.add_argument("--console", default="COM16")
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--fw", default="022555bf",
                        help="beklenen MH firmware kimligi")
    parser.add_argument("--admin-pass", default="admin")
    parser.add_argument("--duration", type=int, default=60,
                        help="gozlem penceresi (s)")
    parser.add_argument("--epoch", action="store_true")
    parser.add_argument("--reset-dut", action="store_true")
    parser.add_argument("--104-host", dest="host_104", default=None,
                        help="m7: c104 master icin cihaz IP'si (venv)")
    parser.add_argument("--104-port", dest="port_104", type=int,
                        default=2404)
    parser.add_argument("--case", action="append", default=None)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--outdir", default=None)
    parser.add_argument("--require-device", action="store_true",
                        help="SKIP/BLOCKED durumlarini da basarisiz say")
    args = parser.parse_args()

    if args.list:
        for name, (_, desc) in sorted(CASES.items()):
            print("%-24s %s" % (name, desc))
        return 0

    outdir = Path(args.outdir) if args.outdir else (
        REPO_ROOT / "test" / "build" / "rf-real-mh" /
        time.strftime("%Y%m%d-%H%M%S"))
    outdir.mkdir(parents=True, exist_ok=True)

    selected = args.case or list(CASES)
    for name in selected:
        if name not in CASES:
            parser.error("bilinmeyen vaka: %s (--list)" % name)
    if len(set(selected)) != len(selected):
        parser.error("tekrarli --case secimi kanit uzerine yazar")

    console = ConsoleSession(args.console, baud=args.baud,
                             logfile=str(outdir / "console.log"))
    profile = dut_profile(console)
    results = {}
    lines_out = []
    if profile is None:
        verdict = "BLOCKED"
        detail = "DUT yanit vermiyor (konsol/ag ayarlarini kontrol et)"
        for name in selected:
            results[name] = (verdict, [detail])
    elif 0 != profile:
        verdict = "BLOCKED"
        detail = ("RTU image profile=%d: gercek MH testi uretim profili "
                  "(0) ister - HIL imaji gercek hubu yok sayar" % profile)
        for name in selected:
            results[name] = (verdict, [detail])
    else:
        console.send("cslog on")
        console.send("rf log verbose")
        ctx = Ctx(args, console, outdir)
        for name in selected:
            function, _ = CASES[name]
            started = time.strftime("%H:%M:%S")
            try:
                verdict, detail = function(ctx)
            except Exception as error:  # noqa: BLE001 - evidence over crash
                verdict, detail = "ERROR", ["%r" % error]
            results[name] = (verdict, detail)
            lines_out.append("%s %s %s" % (started, name, verdict))
    console.close()

    counts = {}
    for verdict, _ in results.values():
        counts[verdict] = counts.get(verdict, 0) + 1
    report = outdir / "report.md"
    with report.open("w", encoding="utf-8") as handle:
        handle.write("# Gercek MH Duman Raporu\n\nTarih: %s\n\n"
                     "| Vaka | Sonuc |\n|---|---|\n" %
                     time.strftime("%Y-%m-%d %H:%M:%S"))
        for name, (verdict, detail) in results.items():
            handle.write("| %s | %s |\n" % (name, verdict))
        handle.write("\n## Detaylar\n\n")
        for name, (verdict, detail) in results.items():
            handle.write("\n### %s - %s\n\n" % (name, verdict))
            for line in detail:
                handle.write("- %s\n" % line)
    (outdir / "results.json").write_text(
        json.dumps({name: {"verdict": verdict, "detail": detail}
                    for name, (verdict, detail) in results.items()},
                   indent=1), encoding="ascii")
    for line in lines_out:
        print(line)
    print("sayim: %s" % counts)
    print("rapor: %s" % report)
    failed = counts.get("FAIL", 0) + counts.get("ERROR", 0)
    if args.require_device:
        failed += counts.get("SKIP", 0) + counts.get("BLOCKED", 0)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
