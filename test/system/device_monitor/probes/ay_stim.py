"""AY kartlari uzerinden guvenli RF olay stimulasyonu (Faz 3).

AY test konsolundan yalniz `test evt` / `test evt sim` gonderilir
(doc/HW_TEST_VE_DEBUG_REHBERI.md A.2.1: olay 137 uretir; sim varyanti
kalici sayaca isler). `test trip` KULLANILMAZ - komut onay katmani
(BLOCKS) hem beyaz liste hem kara liste ile reddeder.

Butce: AY basina koşu başina en fazla --ay-max-events olay ve iki olay
arasi en az --ay-min-gap bekleme. AY yanitinda RED reddi gorulurse o
AY durdurulur ve anomali bildirilir (dergi yarisi/RED kurallari).
AY->fider eslemesi istege bagli JSON ile verilir; verilmezse zincir
tum fiderleri tarar.
"""

import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO_ROOT / "tools/hwtest"))
import serial_io  # noqa: E402  (tools/hwtest; ortak konsol oturumu)

ALLOWED_COMMANDS = ("test evt", "test evt sim")
BLOCKED_SUBSTRINGS = ("trip", "clear", "reset", "fltlog", "elog", "xmodem")
RED_MARKERS = ("RED", "dolu", "reddedildi")


def command_allowed(cmd):
    """AY komut onay katmani: beyaz liste tam eslesme + kara liste."""
    cmd = cmd.strip().lower()
    if cmd not in ALLOWED_COMMANDS:
        return False
    return not any(blocked in cmd for blocked in BLOCKED_SUBSTRINGS)


def evaluate_budget(events_sent, max_events, last_event_at, min_gap_s,
                    disabled_reason, now_mono):
    """Butce karari (saf): (izin, gerekce). AY kapanis durumu kalici."""
    if disabled_reason is not None:
        return False, disabled_reason
    if events_sent >= max_events:
        return False, "butce doldu (%d olay)" % events_sent
    if last_event_at is not None and \
            now_mono - last_event_at < min_gap_s:
        return False, "min gap bekleniyor"
    return True, "ok"


class AyUnit:
    """Tek AY konsol oturumu + butce durumu."""

    def __init__(self, port, baud, log_dir, max_events, min_gap_s):
        self.port = port
        self.max_events = max_events
        self.min_gap_s = min_gap_s
        self.events_sent = 0
        self.disabled_reason = None
        self.last_event_at = None
        self.session = serial_io.ConsoleSession(
            port, baud=baud, logfile=Path(log_dir) / ("%s.log" % port))

    def may_stimulate(self, now_mono=None):
        """Butce + bekleme + RED durumu denetimi -> (izin, gerekce)."""
        now_mono = time.monotonic() if now_mono is None else now_mono
        allowed, reason = evaluate_budget(
            self.events_sent, self.max_events, self.last_event_at,
            self.min_gap_s, self.disabled_reason, now_mono)
        if not allowed and reason.startswith("butce"):
            self.disabled_reason = reason
        return allowed, reason

    def stimulate(self, variant):
        """Onaylanmis tek olay gonderimi -> (gonderilen_metin, yanit)."""
        cmd = "test evt" + (" sim" if variant == "sim" else "")
        if not command_allowed(cmd):
            raise RuntimeError("AY komutu reddedildi: %r" % cmd)
        result = self.session.send_and_wait(cmd, r"137|RED|dolu|ERROR",
                                            timeout_s=15.0)
        self.events_sent += 1
        self.last_event_at = time.monotonic()
        echo = result[1] if result else "<<zaman asimi>>"
        if any(marker in echo for marker in RED_MARKERS):
            self.disabled_reason = "RED/dergi reddi: %s" % echo.strip()
        return cmd, echo

    def close(self):
        self.session.close()


class AyStim:
    """Tum AY'lerin sahibi: rastgele AY + varyant secimi ve butce."""

    def __init__(self, ports, baud, log_dir, detector, rng,
                 max_events=20, min_gap_s=180.0, mapping=None):
        self.units = [AyUnit(port, baud, log_dir, max_events, min_gap_s)
                      for port in ports]
        self.detector = detector
        self.rng = rng
        self.mapping = mapping or {}
        self.lock = threading.Lock()

    def stimulate_random(self):
        """Butcesi musait rastgele bir AY'ye olay gonder -> sonuc sozlugu."""
        with self.lock:
            candidates = [(unit, unit.may_stimulate())
                          for unit in self.units]
            ready = [unit for unit, (allowed, _) in candidates if allowed]
            if not ready:
                reasons = {unit.port: reason for unit, (_, reason)
                           in candidates}
                return {"ok": False, "reasons": reasons}
            unit = self.rng.choice(ready)
            variant = "sim" if self.rng.random() < 0.3 else "evt"
            cmd, echo = unit.stimulate(variant)
            result = {"ok": True, "port": unit.port, "cmd": cmd,
                      "echo": echo.strip()[:200],
                      "events_sent": unit.events_sent,
                      "feeder": self.mapping.get(unit.port)}
            if unit.disabled_reason is not None:
                self.detector.report("ay_stim_disabled", "warning", "ay",
                                     "%s: %s" % (unit.port,
                                                 unit.disabled_reason))
                result["disabled"] = unit.disabled_reason
            return result

    def close(self):
        for unit in self.units:
            unit.close()
