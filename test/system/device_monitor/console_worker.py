"""Modem konsolunun tek sahibi: surekli kayit + komut kilidi + satir halkasi.

ConsoleSession (tools/hwtest/serial_io.py) portu acar ve tum baytlari
logfile'a yazar; burada logfile bir RotatingRawLog'dur (boyut bazli
donusum). Okunan satirlar 60 ms'de bir toplanir:

- sinirli bellek halkasi (kanit paketlerindeki 'son N satir' icin),
- trim_lines ile ConsoleSession.lines listesinin sinirsiz buyumesi onlenir,
- her satir on_line callback'ine verilir (anomali tarayici).

Komutlar command() ile kilit altinda cosulur; cikti komut gunlugune
(JSONL) yazilir. Trim, komut penceresi acikken yapilmaz (cmd_lock): komut
bastigi satir indeksi kaymasiniz. ConsoleSession.wait_for/send_and_wait
bilincli olarak KULLANILMAZ: bekleme mantigi buradaki indeks defteriyle
celisemez.
"""

import sys
import threading
import time
from collections import deque
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools/hwtest"))
import serial_io  # noqa: E402  (tools/hwtest; skill kurali: yeniden yazma)

from recorder import RotatingRawLog  # noqa: E402


def trim_lines(lines, cursor, keep):
    """En yeni 'keep' satiri birak; cursor'u ayni satiri gosterecek sekil kaydir.

    ConsoleSession'in kendi cursor'u bu degisiklikten etkilenmez; wait_for
    kullanilmadigi icin indeks kaymasi yalniz bu isleyicin defterini ilgilendirir.
    """
    remove = len(lines) - keep
    if remove <= 0:
        return cursor
    del lines[:remove]
    return max(0, cursor - remove)


class ConsoleWorker:
    """Tek konsol oturumu: ham kayit + satir izleme + sirali komut calistirma."""

    def __init__(self, port, baud, log_path, on_line=None,
                 ring_size=2000, keep_session_lines=4000,
                 max_log_bytes=10 * 1024 * 1024, log_keep=5,
                 command_writer=None):
        self.raw_log = RotatingRawLog(log_path, max_bytes=max_log_bytes,
                                      keep=log_keep)
        self.session = serial_io.ConsoleSession(port, baud=baud, logfile=None)
        self.session.log = self.raw_log
        self.ring = deque(maxlen=ring_size)
        self.on_line = on_line
        self.keep_session_lines = keep_session_lines
        self.command_writer = command_writer
        self.cmd_lock = threading.Lock()
        self.index = 0
        self.running = True
        self.trim_counter = 0
        self.collector = threading.Thread(target=self._collect,
                                          daemon=True,
                                          name="device-monitor-console")

    def start(self):
        self.collector.start()

    def _collect(self):
        while self.running:
            with self.session.lock:
                fresh = self.session.lines[self.index:]
                self.index = len(self.session.lines)
            for t_mono, text in fresh:
                self.ring.append((t_mono, text))
                if self.on_line is not None:
                    self.on_line(t_mono, text)
            self.trim_counter += 1
            if self.trim_counter >= 500:
                self.trim_counter = 0
                with self.cmd_lock:
                    with self.session.lock:
                        self.index = trim_lines(self.session.lines, self.index,
                                                self.keep_session_lines)
            time.sleep(0.06)

    def command(self, cmd, settle_s=3.0):
        """Konsol komutu cos: cikti satirlari tek metin olarak doner.

        Komut penceresi acikken trim durur; cikti, komuttan SONRA gelen
        satirlarla sinirlidir (console_io.run_cmd deseni).
        """
        with self.cmd_lock:
            with self.session.lock:
                start = len(self.session.lines)
            self.session.send(cmd)
            time.sleep(settle_s)
            with self.session.lock:
                produced = self.session.lines[start:]
            text = "\n".join(item for _, item in produced)
            if self.command_writer is not None:
                self.command_writer.write({"cmd": cmd, "out": text})
            return text

    def lines_snapshot(self, limit=None):
        """Halkadan en yeni satirlar (t_mono, text); kanit paketleri icin."""
        items = list(self.ring)
        return items[-limit:] if limit else items

    def close(self):
        self.running = False
        self.collector.join(timeout=1.0)
        self.session.close()
        self.raw_log.close()
