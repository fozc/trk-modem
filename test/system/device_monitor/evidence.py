"""Anomali aninda kanit paketi toplayici.

Her bulgu sinifi icin (60 s varsayilan cooldown ile) sirali komut pili
calistirilir ve ciktisi evidence/<seq>-<sinif>/ altina yazilir:

- console.txt      : komut pili ciktilari (komut basligiyle)
- recent_console.txt: son N konsol satiri (olay oncesi baglam)
- metrics.json     : o anki metrics kopyasi
- <ad>.tail        : tail_files ile verilen JSONL'lerin son satirlari

Komut pili kanitli yuzeylerden secildi: rstsrc (reset_source.c:197-203),
elog dump (elog.c:764-821), bootlog dump (app_boot_log.c:330-385), stats
(runtime_stats_shell.c:23-96), rf status (rf_shell.c:146-189), pwrboard
show (power_board_scp.c:443-484), gsm status (gsm_shell.c:335-459),
ps + info (shell.c). settle sureleri console_io.run_cmd kullanimina gore.
"""

import re
import shutil
import threading
import time
from pathlib import Path

COMMAND_BATTERY = [
    # rstsrc burada DEGIL: reset_source_shell_init() app_main'den cagrilmadigi
    # icin komut bu imajda kayitli degil ("Command not found", accept-4 kaniti);
    # reset nedeni elog dump icindeki RESET_CAUSE kayitlarindan okunur.
    ("elog info", 4.0),
    ("elog dump 20", 6.0),
    ("bootlog dump", 6.0),
    ("stats", 3.0),
    ("rf status", 5.0),
    ("pwrboard show", 4.0),
    ("gsm status", 7.0),
    ("ps", 3.0),
    ("info", 3.0),
]

SAFE_LABEL = re.compile(r"[^a-z0-9_-]+")


class EvidenceCollector:
    """Konsol komut pili + baglam dosyalari ile kanit dizini uretir."""

    def __init__(self, console, evidence_root, tail_files=None,
                 metrics=None, cooldown_s=60.0, tail_lines=60,
                 recent_lines=400):
        self.console = console
        self.root = Path(evidence_root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.tail_files = dict(tail_files or {})
        self.metrics = metrics
        self.cooldown_s = cooldown_s
        self.tail_lines = tail_lines
        self.recent_lines = recent_lines
        self.lock = threading.Lock()
        self.last_collect = {}
        self.sequence = 0

    def collect(self, label, detail=""):
        """Sinif icin kanit topla; cooldown'daysa None doner."""
        now = time.monotonic()
        with self.lock:
            previous = self.last_collect.get(label)
            if previous is not None and now - previous < self.cooldown_s:
                return None
            self.last_collect[label] = now
            self.sequence += 1
            seq = self.sequence
        safe = SAFE_LABEL.sub("-", label.lower())[:40] or "anomaly"
        target = self.root / ("%03d-%s" % (seq, safe))
        target.mkdir(parents=True, exist_ok=False)
        blocks = []
        for cmd, settle_s in COMMAND_BATTERY:
            started = time.time()
            try:
                text = self.console.command(cmd, settle_s=settle_s)
            except Exception as error:      # noqa: BLE001 (kanit eksiksiz kalmali)
                text = "<<evidence-command-error: %r>>" % (error,)
            blocks.append("$ %s\n%s\n" % (cmd, text))
            print("[evidence %s] %s (%.1fs)" % (label, cmd,
                                                time.time() - started),
                  flush=True)
        (target / "console.txt").write_text(
            "# evidence %s: %s\n# %s\n\n%s" %
            (label, detail[:300], time.strftime("%Y-%m-%d %H:%M:%S"),
             "\n".join(blocks)), encoding="utf-8")
        lines = self.console.lines_snapshot(limit=self.recent_lines)
        (target / "recent_console.txt").write_text(
            "\n".join(text for _, text in lines) + "\n", encoding="utf-8")
        if self.metrics is not None:
            self.metrics.save()
            shutil.copyfile(self.metrics.path, target / "metrics.json")
        for name, path in self.tail_files.items():
            source = Path(path)
            if source.exists():
                with open(source, "r", encoding="utf-8", errors="replace") as handle:
                    tail = handle.readlines()[-self.tail_lines:]
                (target / ("%s.tail" % name)).write_text(
                    "".join(tail), encoding="utf-8")
        return target
