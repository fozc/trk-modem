"""Web yoklamasi: kisa omurlu istekler + token + /syslogs artimli takip.

oracle.WebDevice (test/system/iec104_master/oracle.py) yeniden kullanilir:
login + ?t= token + 4 deneme x 4 s retry - canli cihazda kanitlanmis
istemci. Istek basi baglanti acilir; ortak oturum ve tek cihaz slotu
ayni kilitle korunur. TCP drop denemesi normal istek bittikten sonra
ayri baglanti acar; sonraki poll toparlanmayi dogrular.

Faz 1 dogrulamasi: HTTP 200 + JSON cozumlemesi + gurultu olcumu.
Endpoint basina tam alan-kumesi/aralik semasi Faz 2'de modbus_map_check
ile birlikte gelir. /syslogs 'offset' parametresinin "ilk N kaydi atla"
yorumu kullanilir; ilk okuma taban sayilir, sonrasi artimla anomali
tarayicisina verilir (canli kabule dek dogrulanacak varsayim - README).
"""

import hashlib
import json
import socket
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extimport import load_module  # noqa: E402

ORACLE_PATH = REPO_ROOT / "test/system/iec104_master/oracle.py"
DROP_PATH = REPO_ROOT / "test/system/http_port_drop_test.py"

POLL_FIXED = ["/status/board", "/r?fwVersion"]
POLL_RANDOM = ["/monitor/rf/", "/status/rf-group", "/discovery/rf"] + \
    ["/monitor/rf/%d" % index for index in range(7)] + \
    ["/faults?feeder=%d" % feeder for feeder in range(1, 5)]

# Kanitli alan kumeleri: web_device/run_tests.py:309-313 (board) ve
# http_handlers.c:2011-2042 (/r?fwVersion); /faults en az tc/pc tasir.
# Anahtar = sorgu kirpilip sondaki "/" atilmis taban yol.
SCHEMA_REQUIRED = {
    "/status/board": ["PanelVoltaji", "DcVoltaji", "GirisAkimi", "GirisGucu",
                      "AkuGucu", "Kaynak", "TelemetriYasi", "AlarmMaskesi",
                      "KartDurum", "KartDurum2", "PowerValidFields",
                      "BatterySOC", "ChargeState", "3V3", "3V8", "5V",
                      "TDIE", "ADCValid", "ADCSampleAgeMs"],
    "/r": ["version", "buildDate", "hardware"],
    "/faults": ["tc", "pc"],
}


def schema_missing(path, parsed):
    """Endpoint icin kanitli zorunlu alanlarin eksikleri (yalniz bilinenler)."""
    base = path.split("?")[0].rstrip("/")
    fields = SCHEMA_REQUIRED.get(base)
    if not fields:
        return []
    if not isinstance(parsed, dict):
        return list(fields)
    return [field for field in fields if field not in parsed]


class WebProbe:
    """Tek web istemcisi: login, periyodik GET seti, syslogs takibi, drop kaosu."""

    def __init__(self, host, writer, detector, rng, login_timeout_s=45):
        self.host = host
        self.writer = writer
        self.detector = detector
        self.rng = rng
        self.oracle = load_module("device_monitor_oracle", ORACLE_PATH)
        self.drop = load_module("device_monitor_http_drop", DROP_PATH)
        self.web = self.oracle.WebDevice(host, timeout=login_timeout_s)
        self.token_ok = False
        self.consecutive_failures = 0
        self.syslogs_total = None
        # Login is also called within _request; both own the same session.
        self.lock = threading.RLock()

    def ensure_login(self):
        """Gerekirse (ilk kez ya da 401 sonrasi) login; token taze tutar."""
        with self.lock:
            password = self.oracle.derive_admin_password(self.host)
            self.web.login("admin", password)
            self.token_ok = True

    def _request(self, path):
        """Basarili istek -> (status, body); hata firlatir (cagiran kaydeder)."""
        with self.lock:
            if not self.token_ok:
                self.ensure_login()
            started = time.monotonic()
            try:
                status, body = self.oracle.WebDevice.request(self.web, path)
            except Exception:
                self.token_ok = False
                raise
            elapsed_ms = round((time.monotonic() - started) * 1000.0, 1)
        return status, body, elapsed_ms

    def poll(self):
        """Bir yoklama turu: sabit endpoint'ler + rastgele secim."""
        targets = list(POLL_FIXED) + [self.rng.choice(POLL_RANDOM)]
        for path in targets:
            record = {"ep": path}
            try:
                status, body, elapsed_ms = self._request(path)
                record.update({"status": status, "ms": elapsed_ms,
                               "len": len(body),
                               "sha16": hashlib.sha256(
                                   body.encode("ascii", "replace")
                               ).hexdigest()[:16]})
                if status == 200:
                    try:
                        parsed = json.loads(body)
                        record["json_ok"] = True
                        missing = schema_missing(path, parsed)
                        if missing:
                            record["schema_missing"] = missing
                            self.detector.report(
                                "web_schema", "warning", "web",
                                "%s eksik alanlar: %s" % (path, missing))
                    except ValueError:
                        record["json_ok"] = False
                        self.detector.report(
                            "web_bad_json", "warning", "web",
                            "%s: govde JSON degil (%d bayt)" %
                            (path, len(body)))
                else:
                    self._note_failure("%s -> HTTP %d" % (path, status))
            except Exception as error:      # noqa: BLE001 (problar hata yutmaz, kaydeder)
                record.update({"status": None, "error": repr(error)[:200]})
                self._note_failure("%s: %r" % (path, error))
            self.writer.write(record)
            if record.get("status") is not None:
                self.consecutive_failures = 0

    def syslogs_tail(self):
        """/syslogs artimli okuma: yeni recs metnini anomali tarayicisina verir."""
        offset = self.syslogs_total if self.syslogs_total is not None else 0
        record = {"ep": "/syslogs", "offset": offset}
        try:
            status, body, elapsed_ms = self._request(
                "/syslogs?offset=%d&limit=100" % offset)
            record.update({"status": status, "ms": elapsed_ms})
            if status == 200:
                data = json.loads(body)
                total = int(data.get("t", offset))
                recs = data.get("recs", "")
                record.update({"t": total, "recs_len": len(recs)})
                if self.syslogs_total is None:
                    self.syslogs_total = total      # ilk okuma: taban
                    record["baseline"] = True
                elif total != self.syslogs_total:
                    self.detector.scan_elog(recs)
                    self.syslogs_total = total
        except Exception as error:      # noqa: BLE001
            record.update({"status": None, "error": repr(error)[:200]})
        self.writer.write(record)

    def drop_chaos(self):
        """Tam HTTP yanitindan sonra baglantiyi RST/FIN ile kapat.

        Normal isteklerle ayni anda slot acilmaz. Sonraki poll
        baglantinin kesilmesinden sonra toparlanmayi dogrular.
        """
        graceful = self.rng.random() < 0.5
        detail = ""
        response_complete = False
        try:
            with self.lock:
                with socket.create_connection((self.host, 80), timeout=8) as client:
                    client.sendall(
                        self.drop.build_get_request(self.host, 80, "/loading.html"))
                    status, headers, body, complete = self.drop.recv_response(client, 15.0)
                    length = int(headers.get("content-length", "-1"))
                    if not complete or length < 0 or len(body) != length:
                        raise OSError("incomplete loading.html response")
                    if " 200 " not in status:
                        raise OSError("loading.html -> %s" % status)
                    response_complete = True
                    time.sleep(self.rng.uniform(0.2, 2.0))
                    self.drop.drop_connection(client, graceful)
        except (OSError, ValueError) as error:
            detail = repr(error)[:200]
        self.writer.write({"action": "tcp_drop", "graceful": graceful,
                           "phase": "after-response", "response_complete": response_complete,
                           "error": detail or None})

    def _note_failure(self, detail):
        self.consecutive_failures += 1
        if self.consecutive_failures >= 3:
            self.detector.report("web_failures", "critical", "web",
                                 "3+ ardisik basarisizlik; son: %s" % detail)

    def config_snapshot(self, path="/config/iec104"):
        """Yapilandirma anlik goruntusu (admin GET) -> dict ya da None."""
        try:
            status, body, _ = self._request(path)
            if status == 200:
                return json.loads(body)
        except Exception:           # noqa: BLE001 (yapilandirma okunamazsa None)
            pass
        return None

    def board_snapshot(self):
        """/status/board -> dict ya da None (capraz yuzey kontrolleri icin)."""
        try:
            status, body, _ = self._request("/status/board")
            if status == 200:
                return json.loads(body)
        except Exception:           # noqa: BLE001
            pass
        return None

    def monitor_line(self, feeder_no):
        """/monitor/rf/N (0 tabanli indeks) -> dict ya da None.

        Modbus fider n (1..4) monitördeki n-1 indeksine karsilik gelir.
        """
        try:
            status, body, _ = self._request("/monitor/rf/%d" % (feeder_no - 1))
            if status == 200:
                return json.loads(body)
        except Exception:           # noqa: BLE001
            pass
        return None

    def close(self):
        pass
