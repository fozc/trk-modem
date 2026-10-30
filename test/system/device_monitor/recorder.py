"""Kayit katmani: JSONL yazicilar, ham log rotasyonu, atomik metrics.

Device monitor'in tum cikti dosyalari bu katmandan gecer:

- JsonlWriter   : satir basina flush, otomatik wall/mono zaman damgasi
- RotatingRawLog: boyut esikli rotasyon (serial.log.1, .2, ...); dosya
                  benzeri arayuz ile ConsoleSession.log yerine gecer
- Metrics       : gsm_soak.py deseninde tmp+replace atomik yazim

JSONL dosyalari rotasyona girmez: 30 s kadansla 24 saat ~3 bin kayit
olcugundedir; rotasyon yalniz ham seri kaydi (serial.log) icindir.
"""

import json
import os
import threading
import time
from pathlib import Path


class JsonlWriter:
    """Append-only JSON Lines yazici; her satirdan sonra flush."""

    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.Lock()
        self.file = open(self.path, "a", encoding="utf-8")

    def write(self, record):
        """Kaydi zaman damgasiyla yaz; yazilan girisi dondurur."""
        entry = dict(record)
        entry.setdefault("wall", round(time.time(), 3))
        entry.setdefault("mono", round(time.monotonic(), 3))
        text = json.dumps(entry, ensure_ascii=True, default=str)
        with self.lock:
            self.file.write(text + "\n")
            self.file.flush()
        return entry

    def close(self):
        with self.lock:
            self.file.close()


class RotatingRawLog:
    """Dosya benzeri ham kayit (write/flush/close); esik asilinca dondurur.

    rotasyon: mevcut dosya kapanir, .keep-1 -> .keep ... .1 -> .2 kaydirilir,
    mevcut -> .1 dondurulur, yeni dosya acilir. ConsoleSession logfile
    parametresine dogrudan verilebilir (write/flush arayuzu yeterli).
    """

    def __init__(self, path, max_bytes=10 * 1024 * 1024, keep=5):
        self.path = Path(path)
        self.max_bytes = max_bytes
        self.keep = keep
        self.rotations = 0
        self.lock = threading.Lock()
        self.file = open(self.path, "ab")

    def write(self, data):
        with self.lock:
            if self.file.tell() >= self.max_bytes:
                self._rotate()
            self.file.write(data)

    def flush(self):
        with self.lock:
            self.file.flush()

    def close(self):
        with self.lock:
            self.file.close()

    def _rotate(self):
        """Cagiran tarafin lock'u tuttugu varsayilir."""
        self.file.close()
        for index in range(self.keep - 1, 0, -1):
            source = self.path.with_suffix(self.path.suffix + ".%d" % index)
            target = self.path.with_suffix(self.path.suffix + ".%d" % (index + 1))
            if source.exists():
                os.replace(source, target)
        rotated = self.path.with_suffix(self.path.suffix + ".1")
        os.replace(self.path, rotated)
        self.file = open(self.path, "ab")
        self.rotations += 1


class Metrics:
    """Atomik metrics.json: gsm_soak.py'deki tmp+replace deseni."""

    def __init__(self, path, initial=None):
        self.path = Path(path)
        self.lock = threading.Lock()
        self.data = dict(initial or {})

    def __getitem__(self, key):
        with self.lock:
            return self.data[key]

    def __setitem__(self, key, value):
        with self.lock:
            self.data[key] = value

    def inc(self, key, amount=1):
        with self.lock:
            self.data[key] = self.data.get(key, 0) + amount
            return self.data[key]

    def update(self, values):
        with self.lock:
            self.data.update(values)

    def snapshot(self):
        with self.lock:
            return dict(self.data)

    def save(self):
        with self.lock:
            payload = dict(self.data)
            payload["updated"] = time.time()
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(json.dumps(payload, indent=2, default=str),
                             encoding="utf-8")
        temporary.replace(self.path)
