"""Modbus RTU yoklamasi: rastgele gecerli okuma + zaman/istisna kaydi.

El yapimi RTU istemcisi (test/integration/modbus_device/run_tests.py)
yeniden kullanilir - crc16/fc03/request dogrulanmis kod; device_fault
suresi ayni dosyayi import eden ilk ornek. Yazma YOK: gozlem profili
yalnizca FC03 okur (yaz-geri-al profili ayri onayla gelir).

Faz 1 okuma araliklari modbus test sabitlerinden (AKU=10000,
RELAY1=10002, RELAY2=10003); tam sabit harita cozumlemesi + web/konsol
capraz dogrulamasi Faz 2 modbus_map_check'te.
"""

import sys
import threading
import time
from pathlib import Path

import serial

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extimport import load_module  # noqa: E402

MODBUS_PATH = REPO_ROOT / "test/integration/modbus_device/run_tests.py"

# Faz 1: kanitlanmis test sabitleri cevresinde guvenli araliklar.
FAZ1_RANGES = [(10000, 4), (10002, 2), (10000, 1)]


def classify_response(resp, slave, count, crc16):
    """Ham FC03 yanitini siniflandir: ok/kind detay sozlugu.

    modbus_device.valid_frame ile ayni katilikta (adres + CRC) ama ayri
    (modul global SLAVE'e bagli kalmamak icin parametreli).
    """
    if len(resp) < 4:
        return {"ok": False, "kind": "timeout", "raw": resp.hex()[:80]}
    if resp[0] != slave or \
            crc16(resp[:-2]) != ((resp[-1] << 8) | resp[-2]):
        return {"ok": False, "kind": "crc", "raw": resp.hex()[:80]}
    if resp[1] == 0x83:
        code = resp[2] if len(resp) > 2 else None
        return {"ok": False, "kind": "exception", "code": code}
    if resp[1] == 3 and len(resp) == 5 + 2 * count and resp[2] == 2 * count:
        values = [(resp[3 + 2 * i] << 8) | resp[4 + 2 * i]
                  for i in range(count)]
        return {"ok": True, "kind": "read", "values": values}
    return {"ok": False, "kind": "malformed", "raw": resp.hex()[:80]}


class ModbusProbe:
    """RS-485 uzerinden FC03 yoklama; her islem modbus.jsonl'ye yazilir."""

    def __init__(self, port, writer, detector, rng,
                 baud=115200, stopbits=1, slave=23, timeout_s=1.0):
        self.writer = writer
        self.detector = detector
        self.rng = rng
        self.modbus = load_module("device_monitor_modbus_device", MODBUS_PATH)
        self.modbus.SLAVE = slave
        self.modbus.RESPONSE_TIMEOUT_S = timeout_s
        self.ser = serial.Serial(port, baud, bytesize=8, parity="N",
                                 stopbits=stopbits, timeout=0.005)
        self.consecutive_failures = 0
        self.lock = threading.Lock()

    def read_range(self, start, count):
        """FC03 okuma -> (values | None, ok); her islem modbus.jsonl'ye yazilir."""
        # FC03 has no transaction ID: hold ownership through the response.
        with self.lock:
            req = self.modbus.fc03(self.modbus.SLAVE, start, count)
            resp, first_ms, complete_ms = self.modbus.request(
                self.ser, req, 5 + 2 * count)
            verdict = classify_response(resp, self.modbus.SLAVE, count,
                                        self.modbus.crc16)
            record = {"fc": 3, "start": start, "count": count,
                      "first_ms": round(first_ms, 1) if first_ms else None,
                      "complete_ms": round(complete_ms, 1)}
            record.update(verdict)
            self.writer.write(record)
            if verdict["ok"]:
                self.consecutive_failures = 0
                return verdict["values"], True
            self.consecutive_failures += 1
            report_failure = self.consecutive_failures >= 5
        if report_failure:
            self.detector.report(
                "modbus_failures", "critical", "modbus",
                "5+ ardisik basarisiz FC03; son: %s (%s..+%d)" %
                (verdict["kind"], start, count))
        return None, False

    def poll(self):
        """Rastgele bir gecerli aralik oku (Faz 1 hafif yoklama)."""
        start, count = self.rng.choice(FAZ1_RANGES)
        values, _ = self.read_range(start, count)
        return values

    def close(self):
        with self.lock:
            self.ser.close()
