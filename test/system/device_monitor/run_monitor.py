#!/usr/bin/env python3
"""Cihaz izleme ve uzun sureli dogrulama: konsol + web + Modbus + IEC104 + anomali/kanit.

Kullanim (bench, uretim imaji):
  python run_monitor.py auto --console COM18 --rs485 COM10 --hours 24

Guvenlik: gozlem + pasif kaos profilidir - config yazmasi, FW islemi,
elog/fltlog clear, cihaz reboot YOKTUR. Tek TCP drop kaosu yeni baglanti
uzerinden uzerine uygulanir. Cikti test/build/device_monitor/<zaman>/ altindadir.

Ayrintilar: test/system/device_monitor/README.md
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(REPO_ROOT / "tools/hwtest"))

import serial  # noqa: E402,T001  (surum bildirimi icin)
import serial_io  # noqa: E402,F401  (ConsoleSession console_worker icinde)
from console_worker import ConsoleWorker  # noqa: E402
from anomaly import AnomalyDetector  # noqa: E402
from evidence import EvidenceCollector  # noqa: E402
from recorder import JsonlWriter, Metrics  # noqa: E402
from scheduler import Scheduler  # noqa: E402
from extimport import load_module  # noqa: E402
from probes.web_probe import WebProbe  # noqa: E402
from probes.modbus_probe import ModbusProbe  # noqa: E402
from probes.iec104_probe import Iec104ProbeTask  # noqa: E402

CONSOLE_IO_PATH = REPO_ROOT / "test/system/iec104_master/console_io.py"
IEC104_MASTER_DIR = REPO_ROOT / "test/system/iec104_master"

BOOTS_PATTERN = re.compile(r"\bboots\s*[:=]?\s*(\d+)")
HARNESS_ABORT_ERRORS = 50


def c104_available():
    """Mevcut yorumlayicida c104 import edilebilir mi?"""
    try:
        import c104  # noqa: F401
        return True
    except ImportError:
        return False


def try_reexec_with_venv(argv):
    """c104 yoksa venv python'iyla kendini yeniden baslat (bir kez).

    venv hem c104 hem pyserial icerir (kurulum tarifi README'de);
    donus: True = exec basladi (donulmez), False = venv yok.
    """
    if os.environ.get("DEVICE_MONITOR_REEXEC") == "1":
        return False
    for candidate in (IEC104_MASTER_DIR / ".venv" / "Scripts" / "python.exe",
                      IEC104_MASTER_DIR / ".venv" / "bin" / "python"):
        if candidate.exists():
            os.environ["DEVICE_MONITOR_REEXEC"] = "1"
            print("[device_monitor] c104 yok; venv python ile yeniden basliyor: %s" %
                  candidate, flush=True)
            os.execv(str(candidate), [str(candidate),
                                      str(Path(__file__).resolve())] + argv)
    return False


def git_identity():
    """(short_hash, dirty dosya sayisi) - salt okunur git incelemesi."""
    try:
        short = subprocess.check_output(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL, text=True).strip()
        status = subprocess.check_output(
            ["git", "-C", str(REPO_ROOT), "status", "--porcelain"],
            stderr=subprocess.DEVNULL, text=True)
        return short, len([line for line in status.splitlines() if line.strip()])
    except (subprocess.CalledProcessError, OSError):
        return None, None


def load_config(path):
    """Istege bagli JSON ayar dosyasi: markers/intervals/drop_mean_s."""
    if not path:
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


class DeviceMonitor:
    """Tum kaynaklari kurar, zamanlar ve duzgun kapatir."""

    def __init__(self, args, config):
        self.args = args
        self.config = config
        self.outdir = args.outdir or (REPO_ROOT / "test/build/device_monitor" /
                                      time.strftime("%Y%m%d-%H%M%S"))
        self.outdir.mkdir(parents=True, exist_ok=False)
        self.rng = random.Random(args.seed)
        self.writers = {}
        for name in ("web", "modbus", "iec104", "health", "commands",
                     "anomalies", "rf", "chains"):
            self.writers[name] = JsonlWriter(self.outdir / ("%s.jsonl" % name))
        self.metrics = Metrics(self.outdir / "metrics.json", {
            "state": "running", "started": time.time(), "pid": None,
            "target_hours": args.hours, "seed": args.seed,
            "anomalies_critical": 0, "anomalies_warning": 0,
            "anomalies_suppressed": 0, "harness_errors": 0})
        self.detector = AnomalyDetector(self.writers["anomalies"])
        extra_markers = config.get("markers", [])
        if extra_markers:
            self.detector.markers.extend(
                (label, severity, text) for label, severity, text in extra_markers)
        self.console = None
        self.evidence = None
        self.scheduler = None
        self.probes = {}
        self.host = args.host
        self.stop_requested = False
        self.abort_reason = None
        self.consecutive_harness_errors = 0
        self.last_boots = None

    # -- kurulum ------------------------------------------------------

    def setup_console(self):
        self.console = ConsoleWorker(
            self.args.console, self.args.baud,
            self.outdir / "serial.log",
            on_line=lambda t, text: self.detector.scan_line(text),
            command_writer=self.writers["commands"])
        self.evidence = EvidenceCollector(
            self.console, self.outdir / "evidence",
            tail_files={name: self.outdir / ("%s.jsonl" % name)
                        for name in ("web", "modbus", "iec104", "rf",
                                     "chains", "anomalies")},
            metrics=self.metrics)
        self.detector.on_finding = self.handle_finding
        self.console.start()

    def resolve_host(self):
        """host='auto' ise konsoldan gsm status ile IP cozer."""
        if self.host != "auto":
            return self.host
        console_io = load_module("device_monitor_console_io", CONSOLE_IO_PATH,
                                 extra_sys_path=[IEC104_MASTER_DIR])
        text = self.console.command("gsm status", settle_s=7.0)
        ip = console_io.parse_gsm_ip(text)
        if not ip:
            raise RuntimeError("gsm status ciktisinda IP yok; "
                               "--host ile acikca verin")
        return ip

    def write_manifest(self):
        short, dirty = git_identity()
        rf_status = self.console.command("rf status", settle_s=5.0)
        manifest = {
            "created": time.time(),
            "git_short": short, "git_dirty_files": dirty,
            "python": sys.version.split()[0],
            "pyserial": serial.VERSION,
            "host": self.host, "console": self.args.console,
            "rs485": {"port": self.args.rs485,
                      "baud": self.args.rs485_baud,
                      "slave": self.args.rs485_slave},
            "seed": self.args.seed, "hours": self.args.hours,
            "iec104_mode": self.args.iec104_mode,
            "ay_ports": [p.strip() for p in self.args.ay.split(",")
                         if p.strip()],
            "ay_mapping": self.args.ay_mapping or None,
            "config": self.config, "rf_status_raw": rf_status,
        }
        (self.outdir / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=True),
            encoding="utf-8")

    # -- gorevler -----------------------------------------------------

    def handle_finding(self, finding):
        if finding["severity"] == "critical":
            self.metrics.inc("anomalies_critical")
        else:
            self.metrics.inc("anomalies_warning")
        print("[ANOMALY] %s: %s" % (finding["class"],
                                    finding["detail"][:160]), flush=True)
        if self.evidence is not None:
            self.evidence.collect(finding["class"], finding["detail"])

    def on_task_error(self, name, error, tb_text):
        self.consecutive_harness_errors += 1
        self.metrics.inc("harness_errors")
        self.writers["anomalies"].write({
            "class": "harness_error", "severity": "warning",
            "source": name, "detail": (repr(error) + " | " +
                                       tb_text.strip().splitlines()[-1])[:500]})
        if self.consecutive_harness_errors >= HARNESS_ABORT_ERRORS:
            # Worker thread icinden raise kaybolur: ana dongu bu bayragi
            # izler ve duzgun kapanisla dururur.
            self.abort_reason = ("%d ardisik harness hatasi; son: %s:%r" %
                                 (HARNESS_ABORT_ERRORS, name, error))
            print("[device_monitor] ABORT: %s" % self.abort_reason, flush=True)

    def on_task_success(self, name):
        self.consecutive_harness_errors = 0

    def setup_iec104(self):
        """IEC104 kipini kur: c104 master (surekli) ya da Faz 1 TESTFR probu."""
        if self.args.skip_iec104:
            return
        if self.args.iec104_mode == "probe":
            self.probes["iec104"] = Iec104ProbeTask(
                self.host, self.writers["iec104"], self.detector)
            return
        from probes.iec104_master_worker import Iec104MasterWorker
        if "web" not in self.probes:
            raise RuntimeError("iec104 master kipi web oracle'i gerektirir "
                               "(--skip-web ile master kullanilamaz)")
        web = self.probes["web"]
        oracle = load_module("device_monitor_oracle", CONSOLE_IO_PATH.parent / "oracle.py")
        snap = None
        cfg = None
        # Once login (oracle baglantiyi kendisi acar; token'siz config
        # istegi cihaz tarafindan RST'lenir - W-01 tek istemci davranisi),
        # sonra oracle + config okuma; GSM slot yerlesme penceresi icin
        # 3 deneme x 10 s settle.
        for attempt in range(3):
            try:
                web.ensure_login()
                snap = oracle.read_oracle(web.web)
                cfg = web.config_snapshot("/config/iec104")
                if snap and cfg is not None:
                    break
            except Exception as error:      # noqa: BLE001 (kurulum toleransi)
                print("[device_monitor] oracle deneme %d basarisiz: %r" %
                      (attempt + 1, error), flush=True)
                time.sleep(10.0)
        if snap is None or cfg is None:
            if self.args.iec104_mode == "master":
                raise RuntimeError(
                    "oracle okunamadi (master kipi vazgecmiyor); "
                    "web baglantisini ve slotu kontrol edin")
            print("[device_monitor] oracle okunamadi -> IEC104 TESTFR probuna dusuldu",
                  flush=True)
            self.detector.report("iec104_oracle_unavailable", "warning",
                                 "iec104", "oracle kurulamadi; prob kipine "
                                 "dusuldu")
            self.args.iec104_mode = "probe"
            self.probes["iec104"] = Iec104ProbeTask(
                self.host, self.writers["iec104"], self.detector)
            return
        periodic_send = int(cfg.get("PeriodicSend", 0) or 0)
        worker = Iec104MasterWorker(
            self.host, snap, periodic_send, self.writers["iec104"],
            self.detector, self.outdir / "iec104-apdu.jsonl",
            gi_period_s=self.args.gi_period,
            clock_sync_period_s=self.args.clock_sync_period)
        self.metrics["iec104_master"] = {
            "ca": snap["ca"], "oa": snap["oa"], "port": snap["port"],
            "t3": snap["t3"], "periodic_send_s": periodic_send,
            "in_use_lines": sum(1 for line in snap["lines"]
                                if line["in_use"])}
        worker.start()
        self.probes["iec104_master"] = worker

    def task_console_health(self):
        # rstsrc bu imajda kayitli degil (reset_source_shell_init cagrilmiyor);
        # boots degisimi stats'tan, reset nedeni elog'dan izlenir.
        for cmd in ("stats", "rf status", "pwrboard show"):
            text = self.console.command(cmd, settle_s=4.0)
            self.writers["health"].write({"cmd": cmd, "out": text})
            if cmd == "stats":
                match = BOOTS_PATTERN.search(text)
                if match:
                    boots = int(match.group(1))
                    if self.last_boots is not None and boots != self.last_boots:
                        self.detector.report(
                            "boot_detected", "critical", "console",
                            "boots %d -> %d (reset oldu)" %
                            (self.last_boots, boots))
                    self.last_boots = boots

    def task_stats_history(self):
        text = self.console.command("stats history 0", settle_s=5.0)
        self.writers["health"].write({"cmd": "stats history 0",
                                      "out": text[-4000:]})
        rows = [line for line in text.splitlines() if "," in line]
        if rows:
            with open(self.outdir / "health_history.csv", "a",
                      encoding="utf-8") as handle:
                handle.write("\n".join(rows) + "\n")

    def build_scheduler(self):
        intervals = self.config.get("intervals", {})
        scheduler = Scheduler(self.rng, on_error=self.on_task_error,
                              on_success=self.on_task_success)
        if "web" in self.probes:
            web = self.probes["web"]
            scheduler.add_periodic("web_poll",
                                   float(intervals.get("web_poll", 30)),
                                   web.poll, jitter_s=6.0)
            scheduler.add_periodic("syslogs_tail",
                                   float(intervals.get("syslogs_tail", 120)),
                                   web.syslogs_tail, jitter_s=10.0)
            scheduler.add_random("tcp_drop",
                                 float(self.config.get(
                                     "drop_mean_s",
                                     self.args.drop_mean_s)),
                                 web.drop_chaos, min_gap_s=120.0)
        if "modbus" in self.probes:
            scheduler.add_periodic("modbus_poll",
                                   float(intervals.get("modbus_poll", 15)),
                                   self.probes["modbus"].poll, jitter_s=4.0)
            if getattr(self, "map_check", None) is not None:
                scheduler.add_periodic(
                    "modbus_map",
                    float(intervals.get("modbus_map", 300)),
                    self.map_check.run, jitter_s=15.0)
        if "iec104_master" in self.probes:
            master = self.probes["iec104_master"]
            scheduler.add_periodic("iec104_gi", self.args.gi_period,
                                   master.gi_tour, jitter_s=30.0)
            scheduler.add_periodic("iec104_clock_sync",
                                   self.args.clock_sync_period,
                                   master.clock_sync_tour, jitter_s=60.0)
        elif "iec104" in self.probes:
            scheduler.add_periodic("iec104_poll",
                                   float(intervals.get("iec104_poll", 60)),
                                   self.probes["iec104"].poll, jitter_s=15.0)
        if "rf" in self.probes:
            scheduler.add_periodic("rf_poll",
                                   float(intervals.get("rf_poll", 60)),
                                   self.probes["rf"].poll, jitter_s=10.0)
        if getattr(self, "chain", None) is not None:
            scheduler.add_random("event_chain",
                                 float(self.config.get(
                                     "chain_mean_s", self.args.chain_mean_s)),
                                 self.chain.run,
                                 min_gap_s=self.args.chain_min_gap)
        scheduler.add_periodic("console_health",
                               float(intervals.get("console_health", 300)),
                               self.task_console_health, jitter_s=15.0)
        scheduler.add_periodic("stats_history",
                               float(intervals.get("stats_history", 1800)),
                               self.task_stats_history, jitter_s=30.0)
        return scheduler

    # -- ana dongu ----------------------------------------------------

    def run(self):
        try:
            try:
                self.setup()
            except Exception as error:  # noqa: BLE001 (kurulum hatasi: durum kaybi olmasin)
                self.abort_reason = "kurulum hatasi: %r" % (error,)
                raise
            try:
                self.main_loop()
            except KeyboardInterrupt:
                print("[device_monitor] Ctrl+C - duzgun kapatma", flush=True)
        finally:
            self.shutdown()

    def setup(self):
        self.setup_console()
        self.host = self.resolve_host()
        self.metrics["host"] = self.host
        self.write_manifest()
        if not self.args.skip_web:
            self.probes["web"] = WebProbe(self.host, self.writers["web"],
                                          self.detector, self.rng)
        if not self.args.skip_modbus:
            self.probes["modbus"] = ModbusProbe(
                self.args.rs485, self.writers["modbus"], self.detector,
                self.rng, baud=self.args.rs485_baud,
                slave=self.args.rs485_slave)
        self.setup_iec104()
        if "modbus" in self.probes:
            from probes.modbus_map_check import ModbusMapCheck
            web_probe = self.probes.get("web")
            self.map_check = ModbusMapCheck(
                self.probes["modbus"], self.writers["modbus"], self.detector,
                web_probe=web_probe, console=self.console,
                feeder_monitor_provider=web_probe.monitor_line
                if web_probe is not None else None)
        if "web" in self.probes and not self.args.skip_rf_probe:
            from probes.rf_probe import RfProbe
            self.probes["rf"] = RfProbe(self.probes["web"],
                                        self.writers["rf"], self.detector,
                                        feeder_count=4)
        if self.args.ay:
            from probes.ay_stim import AyStim
            from chains.event_chain import ChainVerifier
            if "web" not in self.probes:
                raise RuntimeError("olay zinciri web yuzeyi gerektirir "
                                   "(--skip-web ile --ay kullanilamaz)")
            ports = [port.strip() for port in self.args.ay.split(",")
                     if port.strip()]
            (self.outdir / "ay").mkdir(exist_ok=True)
            self.probes["ay"] = AyStim(
                ports, self.args.baud, self.outdir / "ay", self.detector,
                self.rng, max_events=self.args.ay_max_events,
                min_gap_s=self.args.ay_min_gap,
                mapping=json.loads(self.args.ay_mapping)
                if self.args.ay_mapping else {})
            self.chain = ChainVerifier(
                self.probes["ay"], self.probes["web"], self.console,
                self.writers["chains"], self.detector,
                self.outdir / "iec104.jsonl",
                modbus_probe=self.probes.get("modbus"),
                feeder_count=4)
        self.scheduler = self.build_scheduler()
        self.metrics["pid"] = os.getpid()

    def main_loop(self):
        deadline = None if self.args.hours <= 0 else \
            time.monotonic() + self.args.hours * 3600.0
        next_status = time.monotonic() + 30.0
        while not self.stop_requested and self.abort_reason is None and \
                (deadline is None or time.monotonic() < deadline):
            self.scheduler.run_once()
            if time.monotonic() >= next_status:
                next_status = time.monotonic() + 30.0
                self.refresh_metrics()
                self.metrics.save()
                print("[device_monitor] %.2fh anomalies(c=%d w=%d) tasks=%s" %
                      (self.metrics["elapsed_hours"],
                       self.metrics["anomalies_critical"],
                       self.metrics["anomalies_warning"],
                       {name: values["runs"] for name, values
                        in self.scheduler.stats().items()}), flush=True)
            time.sleep(0.2)

    def refresh_metrics(self):
        tasks = self.scheduler.stats() if self.scheduler is not None else {}
        self.metrics.update({
            "elapsed_hours": round(
                (time.time() - self.metrics["started"]) / 3600.0, 3),
            "anomalies_suppressed": sum(
                self.detector.stats()["suppressed"].values()),
            "tasks": {name: {"runs": value["runs"], "errors": value["errors"]}
                      for name, value in tasks.items()}})

    def shutdown(self):
        if self.scheduler is not None:
            self.scheduler.stop(wait_s=30.0)
        for probe in self.probes.values():
            closer = getattr(probe, "close", None) or getattr(probe, "stop",
                                                              None)
            if closer is not None:
                try:
                    closer()
                except Exception:       # noqa: BLE001 (kapanis zorla)
                    pass
        if self.console is not None:
            self.console.close()
        for writer in self.writers.values():
            writer.close()
        self.refresh_metrics()
        critical = self.metrics["anomalies_critical"]
        if self.metrics["state"] == "running":
            self.metrics["state"] = "failed" if (critical or
                                                 self.abort_reason) \
                else "passed"
        self.metrics.save()
        try:
            from summarize import build_summary
            (self.outdir / "summary.md").write_text(
                build_summary(self.outdir), encoding="utf-8")
        except Exception as error:         # noqa: BLE001 (ozet en iyi effort)
            print("[device_monitor] summary uretilemedi: %r" % error, flush=True)
        print("[device_monitor] bitti: state=%s critical=%d warning=%d outdir=%s" %
              (self.metrics["state"], critical,
               self.metrics["anomalies_warning"], self.outdir), flush=True)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default="auto",
                        help="cihaz IP ya da 'auto' (konsoldan gsm status)")
    parser.add_argument("--console", default="COM18",
                        help="modem konsol portu (varsayilan COM18)")
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--rs485", default="COM10",
                        help="RS-485 adapter portu")
    parser.add_argument("--rs485-baud", type=int, default=115200)
    parser.add_argument("--rs485-slave", type=int, default=23)
    parser.add_argument("--hours", type=float, default=24.0,
                        help="koşu suresi saat; 0 = suresiz")
    parser.add_argument("--seed", type=int, default=None,
                        help="rastgelelik tohumu (tekrar uretilebilirlik)")
    parser.add_argument("--outdir", type=Path, default=None)
    parser.add_argument("--config", type=Path, default=None,
                        help="JSON ayar: markers/intervals/drop_mean_s")
    parser.add_argument("--skip-web", action="store_true")
    parser.add_argument("--skip-modbus", action="store_true")
    parser.add_argument("--skip-iec104", action="store_true")
    parser.add_argument("--iec104-mode", choices=("auto", "master", "probe"),
                        default="auto",
                        help="auto: c104 varsa surekli master, yoksa prob")
    parser.add_argument("--gi-period", type=float, default=900.0,
                        help="GI turu periyodu (s, yalniz master kipinde)")
    parser.add_argument("--clock-sync-period", type=float, default=3600.0,
                        help="saat senkron turu periyodu (s, master kipi)")
    parser.add_argument("--drop-mean-s", type=float, default=600.0,
                        help="TCP drop kaosu ortalama araligi (s)")
    parser.add_argument("--ay", default="",
                        help="AY konsol portlari (virgulle; bos = kapali)")
    parser.add_argument("--ay-mapping", default="",
                        help='JSON: {"COM15": 1} port->fider eslemesi')
    parser.add_argument("--ay-max-events", type=int, default=20,
                        help="AY basina koşu butcesi (olay)")
    parser.add_argument("--ay-min-gap", type=float, default=180.0,
                        help="AY basina iki olay arasi min sure (s)")
    parser.add_argument("--chain-mean-s", type=float, default=300.0,
                        help="Zincir tetikleme ortalama araligi (s)")
    parser.add_argument("--chain-min-gap", type=float, default=240.0)
    parser.add_argument("--skip-rf-probe", action="store_true",
                        help="/monitor/rf durum yoklamasini kapat")
    args = parser.parse_args(argv)
    if args.seed is None:
        args.seed = int(time.time())
    return args


def resolve_iec104_mode(args, argv):
    """auto/master kipinde c104 hazirligi: re-exec ya da kip dusurme."""
    if args.iec104_mode == "probe" or args.skip_iec104:
        return "probe" if not args.skip_iec104 else "off"
    if c104_available():
        return args.iec104_mode
    if try_reexec_with_venv(argv):
        return None              # exec basladi; buraya donulmez (guvenlik)
    if args.iec104_mode == "master":
        raise SystemExit("HATA: c104 yok ve venv bulunamadi; "
                         "kurulum tarifi test/system/iec104_master/README.md")
    print("[device_monitor] c104 yok -> IEC104 Faz 1 TESTFR probuna dusuldu",
          flush=True)
    return "probe"


def main(argv=None):
    argv = list(sys.argv[1:]) if argv is None else list(argv)
    args = parse_args(argv)
    mode = resolve_iec104_mode(args, argv)
    if mode is None:
        return 2
    args.iec104_mode = mode
    config = load_config(args.config)
    supervisor = DeviceMonitor(args, config)
    supervisor.run()
    return 0 if supervisor.metrics["state"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
