"""IEC104 canlılık probu: baglan + TESTFR + olc + kapat (Faz 1).

gsm_disconnect_test.IecProbe yeniden kullanilir (TESTFR_ACT/CON, ham
frame ayristirma; gsm_disconnect_test.py:63-93). Her tur YENI baglanti
acar: 2404 tek istemciye hizmet verdiginden surekli tutulan bir master
oturumu (Faz 2 c104 worker) ile cakismaz - Faz 2'de bu prob kalkar,
yerini surekli bagli master alir.
"""

import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extimport import load_module  # noqa: E402

GSM_DISCONNECT_PATH = REPO_ROOT / "test/system/gsm_disconnect_test.py"
SYSTEM_DIR = REPO_ROOT / "test/system"


class Iec104ProbeTask:
    """Her poll: taze IecProbe ile TESTFR gidiş-dönüş süresini olçer."""

    def __init__(self, host, writer, detector):
        self.host = host
        self.writer = writer
        self.detector = detector
        module = load_module("device_monitor_gsm_disconnect", GSM_DISCONNECT_PATH,
                             extra_sys_path=[SYSTEM_DIR])
        self.probe_class = module.IecProbe
        self.consecutive_failures = 0

    def poll(self):
        record = {"action": "testfr"}
        started = time.monotonic()
        probe = None
        try:
            probe = self.probe_class(self.host)
            probe.ping()
            record["ok"] = True
            record["ms"] = round((time.monotonic() - started) * 1000.0, 1)
            self.consecutive_failures = 0
        except Exception as error:      # noqa: BLE001 (kaydet + raporla)
            record["ok"] = False
            record["error"] = repr(error)[:200]
            self.consecutive_failures += 1
            if self.consecutive_failures >= 5:
                self.detector.report(
                    "iec104_failures", "critical", "iec104",
                    "5+ ardisik TESTFR basarisiz; son: %r" % error)
        finally:
            if probe is not None:
                try:
                    probe.close()
                except Exception:       # noqa: BLE001 (kapanis hatasi bilgi amacli)
                    pass
        self.writer.write(record)
