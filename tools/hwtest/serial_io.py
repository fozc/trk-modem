"""Seri konsol yardimcisi - donanim testleri icin (COM16 / LPUART1 230400 8N1).

Kullanim:
  python serial_io.py capture <port> <baud> <saniye> <cikti_dosyasi>

stdin'e yazilan satirlar (bos ve '#' ile baslayanlar haric) acilista
0.4 sn arayla cihaza gonderilir; ardindan <saniye> boyunca gelen tum
baytlar cikti dosyasina yazilir. '# ...' satirlari yorum sayilir.

Ornek:
  printf 'help\nbootlog dump\n' | python serial_io.py capture COM16 230400 15 out.log

Etkilesimli API (HIL harness gibi zaman senkron suruculer icin):
  from serial_io import ConsoleSession
  session = ConsoleSession("COM16", 230400, logfile="out.log")
  session.send("rf status")
  hit = session.wait_for(r"hub", timeout_s=10)
  session.close()

ConsoleSession tek oturumda hem komut hem ham kayit sahibidir; capture
CLI davranisi degismeden kalir.
"""
import re
import sys
import threading
import time
import serial

# Konsol arka plan gurultusu (AT trafigi / stack izleri) bekleme
# eslesmelerinde atlanir; capture ciktisinda filtre uygulanmaz.
NOISE = re.compile(r"AT TX|AT RX|\[Stack\]|SI\[")


class ConsoleSession:
    """Etkilesimli konsol oturumu: kalici port + arka plan okuyucu.

    capture modunun aksine komutlar her an gonderilebilir ve cikti
    desenleri zaman asimiyla beklenebilir; tum baytlar logfile'a aynen
    yazilir.
    """

    def __init__(self, port, baud=230400, timeout=0.1, logfile=None):
        self.ser = serial.Serial(port, baud, bytesize=8, parity="N",
                                 stopbits=1, timeout=timeout)
        self.log = open(logfile, "ab") if logfile else None
        self.lock = threading.Lock()
        self.lines = []           # (t_monotonic, text)
        self.cursor = 0           # wait_for ilk gormeye baslayacagi indeks
        self.running = True
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        time.sleep(0.3)           # acilis gurultusunun oturmasi
        self.mark()

    def _read(self):
        buf = bytearray()
        while self.running:
            try:
                data = self.ser.read(256)
            except Exception as err:          # noqa: BLE001
                # reader death must be observable: a marker line keeps
                # the harness from misclassifying it as a DUT failure
                with self.lock:
                    self.lines.append((time.monotonic(),
                                       "<<console-reader-error: %r>>"
                                       % (err,)))
                break
            if not data:
                continue
            if self.log:
                self.log.write(data)
                self.log.flush()
            buf += data
            while b"\n" in buf:
                raw, _, buf = buf.partition(b"\n")
                text = raw.decode("ascii", "replace").rstrip("\r")
                with self.lock:
                    self.lines.append((time.monotonic(), text))

    def close(self):
        self.running = False
        self.reader.join(timeout=1.0)
        if self.log:
            self.log.close()
        self.ser.close()

    # -- gozlem -------------------------------------------------------

    def mark(self):
        """mark()'tan sonraki satirlar wait_for icin 'yeni' sayilir."""
        with self.lock:
            self.cursor = len(self.lines)

    def new_lines(self, since=None):
        with self.lock:
            start = self.cursor if since is None else since
            return self.lines[start:]

    def all_lines(self):
        with self.lock:
            return list(self.lines)

    # -- etkilesim ----------------------------------------------------

    def send(self, line):
        self.ser.write((line + "\r").encode("ascii"))
        self.ser.flush()

    def wait_for(self, pattern, timeout_s=10.0, since=None):
        """Yeni satirlarda desen bekle; (t, satir, match) ya da None."""
        deadline = time.monotonic() + timeout_s
        rx = re.compile(pattern)
        with self.lock:
            start = len(self.lines) if since is None else since
        while time.monotonic() < deadline:
            with self.lock:
                candidates = self.lines[start:]
            for t, text in candidates:
                if NOISE.search(text):
                    continue
                match = rx.search(text)
                if match:
                    return t, text, match
            time.sleep(0.05)
        return None

    def send_and_wait(self, line, pattern, timeout_s=10.0):
        with self.lock:
            since = len(self.lines)
        self.send(line)
        return self.wait_for(pattern, timeout_s=timeout_s, since=since)

    def drain(self, seconds=1.0):
        """Sessizce bekle ve yeni satirlari dondur (ham yakalama)."""
        with self.lock:
            since = len(self.lines)
        time.sleep(seconds)
        with self.lock:
            return self.lines[since:]


def main():
    if len(sys.argv) < 6 or sys.argv[1] != "capture":
        print(__doc__)
        sys.exit(1)

    port, baud = sys.argv[2], int(sys.argv[3])
    seconds = float(sys.argv[4])
    outfile = sys.argv[5]
    lines = [l.rstrip("\n\r") for l in sys.stdin if l.strip() and not l.startswith("#")]

    ser = serial.Serial(port, baud, timeout=0.2)
    ser.reset_input_buffer()
    for line in lines:
        ser.write((line + "\r").encode())
        time.sleep(0.4)
    with open(outfile, "wb") as f:
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                f.write(data)
                f.flush()
    ser.close()

if __name__ == "__main__":
    main()
