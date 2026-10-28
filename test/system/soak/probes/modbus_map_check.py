"""Modbus sabit harita okuma + yapisal dogrulama + capraz yuzey kontrolu.

Tek kaynak Application/modbus_map.h: blok tabanlari/uzunluklari ve
FAULT_RECORDS sayisi dosyadan regex ile OKUNUR (harita kayarsa burasi
kaymaz); faz uye ofsetleri basligin belgelenmis yerlesimiyle (24 kayit:
kalite, anlik_akim F32, anlik_zaman U32, enerji/yuk/rf/trip ikilileri,
trip/harvest gerilim F32, sicaklik I16, rssi I16, fault_count) sabit
tutanir - yerlesim degisirse baslikta _Static_assert devreye girer.

PDU adresi = mantiksal adres - 40000 (test sabitleriyle ayni kural:
RELAY1 PDU 10002 = mantiksal 50002).

Dogrulamalar (kanit temelli olanlar):
- yapisal: kalite bitleri yalniz tanimli maskeden; varyok/trip 0/1;
  F32 sonlu ve makul aralikta; sayaclar <= FAULT_RECORDS; role 0/1
- capraz yuzey: Modbus RLY (50002/50003) <-> web /status/board RLY[2]
  (kati esitlik; iki yuzey de bilinen bicim) - uyusmazlik critical
- capraz yuzey (uyari sinifi): fizer/faz kalite CANLI/FRESH bitleri <->
  web /monitor/rf/N Online - ornekleme zamani kaymasi mumkun oldugundan
  warning kalinir

Konsol `relay status` ciktisi bicimi bu tarafta dogrulanmadigi icin
iddiali parse YOK; ham metin yanina kaydedilir (insan incelemesi).
50001 modem-reset yazmaci yalniz OKUNUR; FC06 hic gonderilmez.
"""

import math
import re
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
MAP_HEADER = REPO_ROOT / "Application/modbus_map.h"

# Faz yerlesimi (modbus_phase_map_t, basligin belgelenmis duzeni; 24 kayit)
PHASE_QUALITY = 0
PHASE_IRMS = 1          # F32, +1..+2
PHASE_IRMS_TIME = 3     # U32, +3..+4
PHASE_ENERGY = 5
PHASE_ENERGY_TIME = 6
PHASE_LOAD = 8
PHASE_LOAD_TIME = 9
PHASE_RF = 11
PHASE_RF_TIME = 12
PHASE_TRIP_FAILED = 14
PHASE_TRIP_TIME = 15
PHASE_TRIP_V = 17       # F32
PHASE_HARVEST_V = 19    # F32
PHASE_TEMP = 21         # I16
PHASE_RSSI = 22         # I16
PHASE_FAULT_COUNT = 23
PHASE_REGS = 24

FEEDER_QUALITY = 0
FEEDER_PHASES_AT = 4
FEEDER_TEMP_COUNTS = 76   # +76..+78
FEEDER_PERM_COUNTS = 79   # +79..+81

PH_Q_MASK = 0x7F        # tanimli bitler: DEFINED..TIME_VALID
FD_Q_MASK = 0x03
FD_Q_ANY_LIVE = 0x02
PH_Q_LIVE_FRESH = 0x06  # HAS_LIVE | FRESH

LOGICAL_BASE = 40000


def parse_map_constants(text=None):
    """modbus_map.h'den blok sabitlerini oku -> dict (PDU adreslerine cevirir)."""
    if text is None:
        text = MAP_HEADER.read_text(encoding="utf-8", errors="replace")

    def grab(name):
        match = re.search(r"#define\s+%s\s+(\d+)U" % name, text)
        return int(match.group(1)) if match else None

    def pdu(value):
        return None if value is None else value - LOGICAL_BASE

    return {
        "line_base": pdu(grab("MODBUS_MAP_LINE_BASE")),
        "line_stride": grab("MODBUS_MAP_LINE_STRIDE"),
        "line_count": grab("MODBUS_MAP_LINE_COUNT"),
        "sys_base": pdu(grab("MODBUS_MAP_SYS_BASE")),
        "sys_count": grab("MODBUS_MAP_SYS_REG_COUNT"),
        "pwr_base": pdu(grab("MODBUS_MAP_PWR_BASE")),
        "pwr_count": grab("MODBUS_MAP_PWR_REG_COUNT"),
        "bms_base": pdu(grab("MODBUS_MAP_BMS_BASE")),
        "bms_count": grab("MODBUS_MAP_BMS_REG_COUNT"),
        "gsm_base": pdu(grab("MODBUS_MAP_GSM_BASE")),
        "gsm_count": grab("MODBUS_MAP_GSM_REG_COUNT"),
        "ctrl_aku": pdu(grab("MODBUS_MAP_CTRL_AKU_UYARISI")),
        "ctrl_reset": pdu(grab("MODBUS_MAP_CTRL_MODEM_RESET")),
        "ctrl_relay1": pdu(grab("MODBUS_MAP_CTRL_RELAY1")),
        "ctrl_relay2": pdu(grab("MODBUS_MAP_CTRL_RELAY2")),
        "fault_records": grab("MODBUS_MAP_FAULT_RECORDS"),
    }


def regs_to_float32(hi, lo):
    """Kayit cifti -> FLOAT32 (baslik: big-endian, high word first ABCD)."""
    return struct.unpack(">f", struct.pack(">HH", hi & 0xFFFF, lo & 0xFFFF))[0]


def regs_to_u32(hi, lo):
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def _int16(value):
    return value - 0x10000 if value & 0x8000 else value


def decode_phase(regs):
    """24 kayitlik faz alt blogu -> sozluk (kalite + cozumlenmis degerler)."""
    return {
        "kalite": regs[PHASE_QUALITY],
        "irms_a": round(regs_to_float32(regs[PHASE_IRMS],
                                        regs[PHASE_IRMS + 1]), 3),
        "anlik_zaman": regs_to_u32(regs[PHASE_IRMS_TIME],
                                   regs[PHASE_IRMS_TIME + 1]),
        "enerji": regs[PHASE_ENERGY],
        "yuk": regs[PHASE_LOAD],
        "rf": regs[PHASE_RF],
        "trip_failed": regs[PHASE_TRIP_FAILED],
        "trip_v": round(regs_to_float32(regs[PHASE_TRIP_V],
                                        regs[PHASE_TRIP_V + 1]), 3),
        "harvest_v": round(regs_to_float32(regs[PHASE_HARVEST_V],
                                           regs[PHASE_HARVEST_V + 1]), 3),
        "sicaklik_c": _int16(regs[PHASE_TEMP]),
        "rssi": _int16(regs[PHASE_RSSI]),
        "fault_count": regs[PHASE_FAULT_COUNT],
    }


def decode_feeder_header(regs):
    """Ilk 82 kayit: fider kalite + 3 faz + ariza sayaclari."""
    phases = [decode_phase(regs[FEEDER_PHASES_AT + index * PHASE_REGS:
                                FEEDER_PHASES_AT + (index + 1) * PHASE_REGS])
              for index in range(3)]
    return {
        "fider_kalite": regs[FEEDER_QUALITY],
        "faz": phases,
        "temp_counts": list(regs[FEEDER_TEMP_COUNTS:FEEDER_TEMP_COUNTS + 3]),
        "perm_counts": list(regs[FEEDER_PERM_COUNTS:FEEDER_PERM_COUNTS + 3]),
    }


def check_feeder(feeder_no, decoded, fault_records_max):
    """Yapisal yapi denetimi -> (sinif, siddet, detay) bulgulari."""
    findings = []
    label = "fider%d" % feeder_no
    quality = decoded["fider_kalite"]
    if quality & ~FD_Q_MASK:
        findings.append(("modbus_map_quality", "critical",
                         "%s fider kalite tanimsiz bit iceriyor: 0x%04X" %
                         (label, quality)))
    for index, phase in enumerate(decoded["faz"]):
        phase_label = "%s faz%d" % (label, index + 1)
        if phase["kalite"] & ~PH_Q_MASK:
            findings.append(("modbus_map_quality", "critical",
                             "%s kalite tanimsiz bit: 0x%04X" %
                             (phase_label, phase["kalite"])))
        for field in ("enerji", "yuk", "rf", "trip_failed"):
            if phase[field] not in (0, 1):
                findings.append(("modbus_map_value", "critical",
                                 "%s %s=%d (0/1 beklenir)" %
                                 (phase_label, field, phase[field])))
        for field in ("irms_a", "trip_v", "harvest_v"):
            value = phase[field]
            if not math.isfinite(value) or value < -100.0 or value > 5000.0:
                findings.append(("modbus_map_value", "critical",
                                 "%s %s=%s (sonlu/makul degil)" %
                                 (phase_label, field, value)))
        if not -60 <= phase["sicaklik_c"] <= 150:
            findings.append(("modbus_map_value", "warning",
                             "%s sicaklik=%d C" %
                             (phase_label, phase["sicaklik_c"])))
    for kind, counts in (("temp", decoded["temp_counts"]),
                         ("perm", decoded["perm_counts"])):
        for index, count in enumerate(counts):
            if count > fault_records_max:
                findings.append(("modbus_map_value", "critical",
                                 "%s %s_fault_count[%d]=%d > %d" %
                                 (label, kind, index, count,
                                  fault_records_max)))
    return findings


def cross_check_relays(modbus_relays, board):
    """Modbus 50002/50003 <-> /status/board RLY dizisi (bilinen bicimler)."""
    findings = []
    web_rly = board.get("RLY") if isinstance(board, dict) else None
    if not isinstance(web_rly, list) or len(web_rly) < 2:
        return findings
    for index, (modbus_value, web_value) in enumerate(
            zip(modbus_relays, web_rly[:2])):
        if modbus_value not in (0, 1):
            findings.append(("modbus_relay_value", "critical",
                             "role%d kayit degeri %d (0/1 beklenir)" %
                             (index + 1, modbus_value)))
        elif int(web_value) != modbus_value:
            findings.append(("modbus_web_relay_mismatch", "critical",
                             "role%d: modbus=%d web=%d" %
                             (index + 1, modbus_value, int(web_value))))
    return findings


def cross_check_rf(feeder_no, decoded, monitor_line):
    """Faz CANLI/FRESH bitleri <-> /monitor/rf/N Online (uyari sinifi).

    Gerclesen bicim (rf_monitor_json.c:243-287): lines[0].Phases[].
    Bicim taninmazsa sessiz gec (yanlis pozitif uretme).
    """
    findings = []
    online = 0
    if isinstance(monitor_line, dict):
        lines = monitor_line.get("lines")
        if not (isinstance(lines, list) and lines and
                isinstance(lines[0], dict)):
            return findings
        phases = lines[0].get("Phases")
        if not isinstance(phases, list):
            return findings
        online = sum(1 for phase in phases
                     if isinstance(phase, dict) and
                     phase.get("Online") in (1, True))
    else:
        return findings
    any_live = bool(decoded["fider_kalite"] & FD_Q_ANY_LIVE)
    fresh_phases = sum(1 for phase in decoded["faz"]
                       if phase["kalite"] & PH_Q_LIVE_FRESH)
    if online > 0 and not any_live:
        findings.append(("modbus_rf_state_mismatch", "warning",
                         "fider%d web %d faz Online ama modbus ANY_LIVE=0" %
                         (feeder_no, online)))
    elif online == 0 and fresh_phases > 0:
        findings.append(("modbus_rf_state_mismatch", "warning",
                         "fider%d modbus %d faz CANLI/FRESH ama web Online=0" %
                         (feeder_no, fresh_phases)))
    return findings


class ModbusMapCheck:
    """5 dakikada bir tam haritayi okuyup dogrulayan scheduler gorevi."""

    def __init__(self, probe, writer, detector, web_probe=None, console=None,
                 feeder_monitor_provider=None):
        self.probe = probe
        self.writer = writer
        self.detector = detector
        self.web_probe = web_probe
        self.console = console
        self.feeder_monitor_provider = feeder_monitor_provider
        self.map = parse_map_constants()
        missing = [key for key, value in self.map.items() if value is None]
        if missing:
            raise RuntimeError("modbus_map.h sabitleri okunamadi: %s" % missing)

    def run(self):
        """Bir tam-harita turu: tum read-only bloglar + capraz kontroller."""
        snapshot = {"action": "map_check"}
        board = self.web_probe.board_snapshot() if self.web_probe else None
        relay_text = self.console.command("relay status", settle_s=3.0) \
            if self.console else ""
        for feeder_no in range(1, self.map["line_count"] + 1):
            decoded = self._read_feeder(feeder_no)
            if decoded is not None:
                snapshot["feeder%d" % feeder_no] = decoded
                for finding in check_feeder(
                        feeder_no, decoded, self.map["fault_records"]):
                    self.detector.report(finding[0], finding[1], "modbus", finding[2])
        for name in ("sys", "pwr", "bms", "gsm"):
            base = self.map["%s_base" % name]
            count = self.map["%s_count" % name]
            regs, ok = self.probe.read_range(base, count)
            snapshot[name] = {"ok": ok, "raw_head": regs[:12] if regs else []}
            if not ok:
                self.detector.report("modbus_block_read", "critical",
                                     "modbus", "%s blogu okunamadi" % name)
        relay_regs = self._read_ctrl()
        if relay_regs is not None and board is not None:
            for finding in cross_check_relays(relay_regs[2:4], board):
                self.detector.report(finding[0], finding[1], "modbus", finding[2])
        if self.feeder_monitor_provider is not None:
            self._cross_rf(snapshot)
        snapshot["relay_console_raw"] = relay_text[:400]
        self.writer.write(snapshot)

    def _read_feeder(self, feeder_no):
        base = self.map["line_base"] + \
            (feeder_no - 1) * self.map["line_stride"]
        regs, ok = self.probe.read_range(base, FEEDER_PERM_COUNTS + 3)
        if not ok:
            self.detector.report("modbus_block_read", "critical", "modbus",
                                 "fider%d baslik blogu okunamadi" % feeder_no)
            return None
        return decode_feeder_header(regs)

    def _read_ctrl(self):
        start = self.map["ctrl_aku"]
        end = self.map["ctrl_relay2"]
        regs, ok = self.probe.read_range(start, end - start + 1)
        if not ok:
            self.detector.report("modbus_block_read", "critical", "modbus",
                                 "kontrol blogu (aku..role2) okunamadi")
            return None
        return regs

    def _cross_rf(self, snapshot):
        for feeder_no in range(1, self.map["line_count"] + 1):
            decoded = snapshot.get("feeder%d" % feeder_no)
            monitor = self.feeder_monitor_provider(feeder_no)
            if decoded is None or monitor is None:
                continue
            for finding in cross_check_rf(feeder_no, decoded, monitor):
                self.detector.report(finding[0], finding[1], "modbus", finding[2])
