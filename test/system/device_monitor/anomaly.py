"""Anomali tespiti: konsol isaretleri + elog kural taramasi + genel rapor.

Kaynaklar (kanitli satirlar):
- gsm_soak.py:70-75     : Powering on GSM / HardFault / GSMTEST FIXTURE /
                          Waiting for EWDT
- gsm_disconnect_test.py:129-131 : SEND_DATA timeout
- power_panic.c:36-51   : "Power panic detected"
- app_main.c:201-215    : "[ELOG] Previous run ended in HARDFAULT"

elog kurallari /syslogs recs metninden (http_handlers.c:986-994 bicimi,
"CODE:<AD> INFO:...") taranir: HARDFAULT, anormal RESET_CAUSE nedenleri
(IWDG/WWDG/BOR/LPWR; reset_source.c:42-44), UART_TX_RECOVERY,
NVRAM_RECOVERED ve timestamp=0 (TS:1970). RESET_CAUSE her acilista
yazilir; yalniz ARTIM (yeni kayit) taramaya verilir - ilk okuma taban
sayilir (web_probe sorumlulugu).

Her bulgu anomalies.jsonl'e yazilir ve on_finding ile kanit toplayiciyi
tetikler. Sinif bazli cooldown ayni bulgunun seriyeni bastirir
(sayilir, tekrar yazilmaz).
"""

import re
import threading
import time

DEFAULT_MARKERS = [
    ("hardfault", "critical", "HardFault"),
    ("gsm_restart", "critical", "Powering on GSM"),
    ("ewdt_wait", "critical", "Waiting for EWDT"),
    ("gsm_fixture_image", "warning", "GSMTEST FIXTURE"),
    ("power_panic", "critical", "Power panic detected"),
    ("hardfault_previous_run", "critical",
     "Previous run ended in HARDFAULT"),
    ("send_data_timeout", "warning", "SEND_DATA timeout"),
]

# (sinif, siddet, desen) - elog recs satirlarina uygulanir.
DEFAULT_ELOG_RULES = [
    ("elog_hardfault", "critical", re.compile(r"CODE:HARDFAULT")),
    ("elog_reset_abnormal", "critical",
     re.compile(r"CODE:RESET_CAUSE.*INFO:.*(IWDG|WWDG|BOR|LPWR)")),
    ("elog_uart_recovery", "warning",
     re.compile(r"CODE:UART_TX_RECOVERY")),
    ("elog_nvram_recovered", "warning",
     re.compile(r"CODE:NVRAM_RECOVERED")),
    ("elog_time_lost", "warning", re.compile(r"TS:1970")),
]


class AnomalyDetector:
    """Isaret/kural tarayici + sinif bazli cooldown + anomalies.jsonl yazimi."""

    def __init__(self, writer, on_finding=None, cooldown_s=60.0,
                 markers=None, elog_rules=None):
        self.writer = writer
        self.on_finding = on_finding
        self.cooldown_s = cooldown_s
        self.markers = list(markers or DEFAULT_MARKERS)
        self.elog_rules = list(elog_rules or DEFAULT_ELOG_RULES)
        self.lock = threading.Lock()
        self.last_seen = {}
        self.suppressed = {}

    def scan_line(self, text):
        """Tek konsol satiri; eslesen isaret varsa bulgu uretilir."""
        for label, severity, marker in self.markers:
            if marker in text:
                self._emit(label, severity, "console", text)
                return True
        return False

    def scan_elog(self, recs_text):
        """Yeni /syslogs recs metni; eslesen kurallar bulgu uretir."""
        matched = False
        for line in recs_text.splitlines():
            for label, severity, pattern in self.elog_rules:
                if pattern.search(line):
                    self._emit(label, severity, "elog", line.strip())
                    matched = True
        return matched

    def report(self, label, severity, source, detail):
        """Problarin bildirdigi turetilmis bulgular (web/modbus/...)."""
        self._emit(label, severity, source, detail)

    def _emit(self, label, severity, source, detail):
        now = time.monotonic()
        with self.lock:
            previous = self.last_seen.get(label)
            if previous is not None and now - previous < self.cooldown_s:
                self.suppressed[label] = self.suppressed.get(label, 0) + 1
                return
            self.last_seen[label] = now
        finding = {"class": label, "severity": severity,
                   "source": source, "detail": detail[:500]}
        self.writer.write(finding)
        if self.on_finding is not None:
            try:
                self.on_finding(finding)
            except Exception:       # noqa: BLE001 (kanit hatasi taramayi durduramaz)
                pass

    def stats(self):
        with self.lock:
            return {"suppressed": dict(self.suppressed),
                    "classes": sorted(self.last_seen)}
