#!/usr/bin/env python3
"""Selected-field JSON round-trip verification on the live device (bench only).

Complements run_tests.py after the web endpoints moved to the common JSON
core (Application/json). Everything runs through the real HTTP server over
the GSM link; no console access is needed:

- GET schema checks for status/config/monitor/discovery responses.
- Semantic config round-trip: read-only metadata and Time are excluded
  where needed; remaining keys may be sent in bounded groups.
- Selected writable fields are changed, read back and restored.
- Link-critical fields have rejection or same-value escape checks only;
  those checks do not prove that a new valid value would be applied.
- RF POST is optional and CAN start real apply operations. RF value
  mutations remain outside this script; host tests cover parser storage.

Mutations attempt and verify restoration in finally blocks. RF POST needs
--allow-rf-apply because even unchanged settings can start RF operations.
Rejection and same-value escape checks do not prove valid-value persistence.

Usage: python run_roundtrip.py [host]
Host default: env WEB_DEVICE_HOST or 188.59.76.59.
"""

import argparse
import json
import os
import sys
import time

import run_tests
from run_tests import DeviceSession, check, FAILURES

TOKEN = ""
SESSION = None
ALLOW_RF_APPLY = False


def connect():
    """(Re)acquire the single web slot with the settle+retry discipline
    the GSM server needs after a reset or a dropped connection."""
    global SESSION
    last = None
    for attempt in range(6):
        if attempt:
            time.sleep(8.0)
        try:
            if SESSION is not None:
                SESSION.close()
            SESSION = DeviceSession()
            return
        except (OSError, RuntimeError) as err:
            last = err
    raise RuntimeError("cannot hold the device web slot: %r" % last)


def login():
    global TOKEN
    for _ in range(4):
        status, body = SESSION.request(
            "POST", "/auth/login",
            '{"username":"admin","password":"admin60"}')
        if status == 200:
            TOKEN = json.loads(body).get("token", "")
            if TOKEN:
                return
        connect()
    raise RuntimeError("login failed")


def request(method, path, body=None):
    """One request with reconnect+relogin on transport errors (the
    server may close the single slot after an error response)."""
    last = "authentication retry exhausted"
    for attempt in range(4):
        try:
            status, raw = SESSION.request(method, path, body)
            if status in (401, 403) and "/auth/login" not in path:
                login()
                continue
            return status, raw
        except (OSError, RuntimeError) as err:
            last = err
            time.sleep(1.5 if 0 == attempt else 8.0)
            connect()
            login()
    raise RuntimeError("request failed after retries: %s %s (%r)"
                       % (method, path, last))


def rq(method, path, body=None):
    sep = "&" if "?" in path else "?"
    return request(method, path + (sep + "t=" + TOKEN if TOKEN else ""),
                   body)


def get_json(path):
    status, body = rq("GET", path)
    return status, (json.loads(body) if status == 200 else None)


def post_json(path, payload):
    if path == "/config/device":
        # Time is a live clock command, not a value to replay from a snapshot.
        payload = {key: value for key, value in payload.items() if key != "Time"}
    status, body = rq("POST", path, json.dumps(payload))
    parsed = None
    try:
        parsed = json.loads(body)
    except ValueError:
        parsed = None
    return status, parsed


def post_ok(path, payload):
    status, parsed = post_json(path, payload)
    return status == 200 and parsed is not None and parsed.get("success") \
        is True


def require(name, condition, detail=""):
    check(name, condition, detail)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


# ----------------------------------------------------------------------
# Phase A - GET schemas (every field the writers emit)
# ----------------------------------------------------------------------

DEVICE_RW = ["WebArayuzuPortu", "WebIlkVeriZamanAsimi",
             "WebBostaKalmaZamanAsimi", "IEC104IlkVeriZamanAsimi",
             "IEC104BostaKalmaZamanAsimi", "SimKartPin", "SimKartAPN",
             "SimKartAPNSifresi", "SimKartAPNUsername", "NtpServer",
             "NtpServerPortu", "Time", "TimeZone",
             "PeriyodikModemResetPeriyodu", "DevreyeAlinmaZamani",
             "PreserveOutputStates"]
DEVICE_RO = ["SeriNumarasi", "CihazKoordinati", "UretimTarihi", "LifeTime",
             "RunTime", "ModemYazilimVeriyonu", "KurulumTarihi",
             "RFYazilimVeriyonu"]

IEC_SCALARS = ["PeriodicSend", "Port", "ScadaIPAdresi", "T0", "T1", "T2",
               "T3", "K", "W", "OriginatorAddr", "CommonAddr", "SBO",
               "SBOTimeout", "AkuUyarisi", "ModemReset"]
IEC_LINE_ARRAYS = ["TemporaryFaultBase", "PermanentFaultBase",
                   "IOA_R_AnlikAkim", "IOA_S_AnlikAkim", "IOA_T_AnlikAkim",
                   "IOA_R_EnerjiVarYok", "IOA_S_EnerjiVarYok",
                   "IOA_T_EnerjiVarYok", "IOA_R_YukAkimiVarYok",
                   "IOA_S_YukAkimiVarYok", "IOA_T_YukAkimiVarYok",
                   "IOA_R_RfhabVarYok", "IOA_S_RfhabVarYok",
                   "IOA_T_RfhabVarYok", "IOA_R_TripFailed",
                   "IOA_S_TripFailed", "IOA_T_TripFailed"]

RF_BLOCK = ["HatID", "ZoneID", "CalismaModu", "SistemNominalAkimi",
            "SetEdilebilirActirmaEsikAkimi", "SetEdilebilirAcmaArizaSayisi",
            "ArtimliAkimEsigi", "HatKopukHatBosta",
            "OluHatAkimiDogrulamaSuresi", "YenilenmeSifirlamaSuresi",
            "HatFrekansi", "IsSafety", "ThresholdMs", "TMemDeadSec",
            "InrushTimerMs", "InrushMultiplier", "SyncTripDelayMs",
            "TripPulseDurationMs", "TripMode", "Inrush100HzRatio",
            "ClpEnabled", "ClpMultiplier", "ClpDurationMs", "VtripTarget"]

BOARD_FIELDS = ["PanelVoltaji", "DcVoltaji", "GirisAkimi", "GirisGucu",
                "AkuGucu", "Kaynak", "TelemetriYasi", "AlarmMaskesi",
                "KartDurum", "KartDurum2", "PowerValidFields", "BatterySOC",
                "ChargeState", "3V3", "3V8", "5V", "TDIE", "ADCValid",
                "ADCSampleAgeMs"]


def phase_a_schemas():
    # /status/board
    status, board = get_json("/status/board")
    missing = [k for k in BOARD_FIELDS if k not in (board or {})]
    require("schema_board_all_fields", status == 200 and not missing,
            "HTTP %d missing=%s" % (status, missing))
    numeric = ["PanelVoltaji", "DcVoltaji", "GirisAkimi", "GirisGucu",
               "AkuGucu", "BatterySOC", "3V3", "3V8", "5V", "TDIE"]
    bad = [k for k in numeric if not is_number(board.get(k))]
    require("schema_board_numeric_types", status == 200 and not bad,
            "not numeric=%s" % bad)

    # /config/device
    status, device = get_json("/config/device")
    missing = [k for k in DEVICE_RW + DEVICE_RO if k not in (device or {})]
    require("schema_device_all_fields", status == 200 and not missing,
            "HTTP %d missing=%s" % (status, missing))

    # /config/iec104
    status, iec = get_json("/config/iec104")
    missing = [k for k in IEC_SCALARS if k not in (iec or {})]
    lines = (iec or {}).get("Hatlar", {})
    missing += [k for k in IEC_LINE_ARRAYS + ["inUse"] if k not in lines]
    require("schema_iec104_all_fields", status == 200 and not missing,
            "HTTP %d missing=%s" % (status, missing))
    array_len_bad = [k for k in IEC_LINE_ARRAYS
                     if len(lines.get(k, [])) != len(lines.get("inUse", []))]
    require("schema_iec104_array_lengths",
            status == 200 and not array_len_bad and len(lines.get("inUse",
                                                                  [])) > 0,
            "mismatched=%s" % array_len_bad)

    # /config/modbus
    status, modbus = get_json("/config/modbus")
    expect = ["BaudRate", "CihazID", "RegisterMap", "SonHataKodu",
              "SonHataZamani", "StopBits"]
    require("schema_modbus_all_fields",
            status == 200 and sorted((modbus or {}).keys()) == sorted(expect),
            "HTTP %d keys=%s" % (status, sorted((modbus or {}).keys())))

    # /config/rf
    status, rf = get_json("/config/rf")
    missing = [k for k in RF_BLOCK + ["inUse", "R_DEVICEID", "S_DEVICEID",
                                      "T_DEVICEID"]
               if k not in (rf or {})]
    require("schema_rf_all_fields", status == 200 and not missing,
            "HTTP %d missing=%s" % (status, missing))
    n_lines = len((rf or {}).get("inUse", []))
    len_bad = [k for k in RF_BLOCK + ["R_DEVICEID", "S_DEVICEID",
                                      "T_DEVICEID"]
               if len((rf or {}).get(k, [])) != n_lines]
    require("schema_rf_array_lengths",
            status == 200 and n_lines > 0 and not len_bad,
            "lines=%d mismatched=%s" % (n_lines, len_bad))

    # /status/rf-group
    status, group = get_json("/status/rf-group")
    missing = [k for k in ["State", "BatchState", "Targets", "Applied",
                           "StopReason", "SaveBlocked", "MatchesDesired"]
               if k not in (group or {})]
    require("schema_rf_group_all_fields", status == 200 and not missing,
            "HTTP %d missing=%s" % (status, missing))

    # /monitor/rf/0: lines[] = {LineId, Phases:[{Phase, ...}]}
    status, mon = get_json("/monitor/rf/0")
    lines = (mon or {}).get("lines", [])
    required_phase = {"Phase", "HasData"}
    detailed_phase = {"Eui64", "Online", "TripFailedAlarm",
                      "TripFailureLatched", "UptimeStalled", "LiveAgeMs",
                      "Live", "Trip", "AnomalyLive", "AnomalyTrip"}
    live_fields = {"Source", "Seq", "Uptime", "BootCounter", "State",
                   "FaultCount", "Irms", "VTrip", "VRec", "Temp", "RSSI",
                   "Flags", "FsmError", "TripFailed", "LogPending",
                   "LogWrapLow"}
    phase_ok = bool(lines)
    for line in lines:
        phases = line.get("Phases", [])
        phase_ok &= {"LineId", "FaultCountsSinceStartup", "Maintenance"} <= \
                    line.keys() and len(phases) == 3
        for phase in phases:
            phase_ok &= required_phase <= phase.keys()
            if phase.get("HasData"):
                phase_ok &= detailed_phase <= phase.keys()
                if phase.get("Live") is not None:
                    phase_ok &= live_fields <= phase["Live"].keys()
                if phase.get("Trip") is not None:
                    phase_ok &= {"Event", "FaultCount", "InstantAmps", "RxMs"} \
                                <= phase["Trip"].keys()
                for key in ("AnomalyLive", "AnomalyTrip"):
                    phase_ok &= {"Active", "Window", "Total"} <= \
                                phase.get(key, {}).keys()
    require("schema_monitor_rf_fields", status == 200 and phase_ok,
            "HTTP %d lines=%d (conditional fields checked when present)" %
            (status, len(lines)))

    status, discovery = get_json("/discovery/rf")
    entries = (discovery or {}).get("data", {}).get("Unassigned")
    discovery_ok = isinstance(entries, list) and all(
        isinstance(entry, dict) and {"EUI64", "FiderID"} <= entry.keys()
        for entry in entries)
    require("schema_rf_discovery", status == 200 and discovery_ok,
            "HTTP %d" % status)

    # /faults (feeder query), /syslogs, /fw_status
    status, faults = get_json("/faults?feeder=1")
    require("schema_faults_valid_json", status == 200 and
            isinstance(faults, dict) and "feeder" in faults, "HTTP %d" % status)

    status, logs = get_json("/syslogs")
    require("schema_syslogs_valid_json", status == 200 and
            isinstance(logs, dict) and isinstance(logs.get("recs"), str)
            and "t" in logs, "HTTP %d" % status)

    status, fwst = get_json("/fw_status")
    require("schema_fw_status_fields", status == 200 and
            "active" in (fwst or {}), "HTTP %d" % status)


# ----------------------------------------------------------------------
# Phase B - verbatim full-body POST round-trip
# ----------------------------------------------------------------------

VOLATILE = {"Time", "RunTime", "LifeTime", "SonHataKodu", "SonHataZamani"}

# The HTTP layer rejects bodies above MAX_CONTENT_LENGTH (4608 B,
# http_request_parser.c) with a bare 400; large verbatim bodies are
# chunked by top-level key so every field is still posted back.
BODY_LIMIT = 4500


def post_verbatim(endpoint, payload, label):
    """Post the payload back, split into <=BODY_LIMIT chunks if needed."""
    keys = list(payload.keys())
    chunks = [[]]
    for key in keys:
        item = len(json.dumps({key: payload[key]})) + 1
        used = sum(len(json.dumps({k: payload[k]})) + 1
                   for k in chunks[-1])
        if used + item > BODY_LIMIT and chunks[-1]:
            chunks.append([])
        chunks[-1].append(key)
    good = True
    for chunk in chunks:
        part = {key: payload[key] for key in chunk}
        if not post_ok(endpoint, part):
            good = False
    require("roundtrip_%s_post_verbatim" % label, good,
            "POST accepted=%s (parts=%d)" % (good, len(chunks)))


def deep_equal(expected, actual, path=""):
    """Field-by-field compare; returns a list of mismatch descriptions."""
    diffs = []
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in set(expected) | set(actual):
            if key in VOLATILE:
                continue
            if key not in actual:
                diffs.append("%s.%s missing" % (path, key))
            elif key not in expected:
                diffs.append("%s.%s unexpected" % (path, key))
            else:
                diffs += deep_equal(expected[key], actual[key],
                                    "%s.%s" % (path, key))
    elif isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            diffs.append("%s len %d != %d" % (path, len(expected),
                                              len(actual)))
        else:
            for index, (one, two) in enumerate(zip(expected, actual)):
                diffs += deep_equal(one, two, "%s[%d]" % (path, index))
    elif expected != actual:
        diffs.append("%s %r != %r" % (path, expected, actual))
    return diffs


def verbatim_roundtrip(endpoint, name, exclude=()):
    status, body = rq("GET", endpoint)
    require("roundtrip_%s_get" % name, status == 200, "HTTP %d" % status)
    if status != 200:
        return
    payload = json.loads(body)
    # RegisterMap is compile-time-fixed read-only metadata larger than the
    # body limit; it is not a POSTable field. Its nested-array skip path
    # is proven with a compact body separately.
    postable = {key: value for key, value in payload.items()
                if key not in exclude}
    post_verbatim(endpoint, postable, name)
    status, after = get_json(endpoint)
    diffs = deep_equal(payload, after or {}) if status == 200 else ["HTTP %d"
                                                                   % status]
    require("roundtrip_%s_stable" % name, not diffs, "; ".join(diffs[:4]))


# ----------------------------------------------------------------------
# Phase C - per-field mutation / escape / rejection proofs
# ----------------------------------------------------------------------

def mutated(payload, changes):
    body = json.loads(json.dumps(payload))
    for key, value in changes.items():
        if "." in key:
            outer, inner = key.split(".", 1)
            body[outer][inner] = value
        else:
            body[key] = value
    return body


def verify_fields(endpoint, changes, label):
    """POST the mutation, verify every changed field in a fresh GET."""
    _, before = get_json(endpoint)
    ok = post_ok(endpoint, mutated(before, changes))
    _, after = get_json(endpoint)
    bad = []
    for key, value in changes.items():
        actual = after
        for part in key.split("."):
            actual = (actual or {}).get(part)
        if actual != value:
            bad.append("%s=%r!=%r" % (key, actual, value))
    require("field_%s_transfer" % label, ok and not bad,
            "ok=%s bad=%s" % (ok, bad[:4]))
    return before


def reject_field(endpoint, base, changes, label, code=400):
    status, parsed = post_json(endpoint, mutated(base, changes))
    require("field_%s_invalid_rejected" % label, status == code,
            "HTTP %d %s" % (status, (parsed or {}).get("error", "")[:40]))


def device_config_fields(device):
    """One VALID alternate value per writable field; None entries are
    restored-only fields proven via rejection in phase C part 2."""
    alternates = {
        "WebIlkVeriZamanAsimi": device["WebIlkVeriZamanAsimi"] + 5,
        "WebBostaKalmaZamanAsimi": device["WebBostaKalmaZamanAsimi"] + 5,
        "IEC104IlkVeriZamanAsimi":
            device["IEC104IlkVeriZamanAsimi"] + 5,
        "IEC104BostaKalmaZamanAsimi":
            device["IEC104BostaKalmaZamanAsimi"] + 5,
        "NtpServerPortu": 124 if device["NtpServerPortu"] != 124 else 123,
        "TimeZone": 4 if device["TimeZone"] != 4 else 3,
        "DevreyeAlinmaZamani": device["DevreyeAlinmaZamani"] + 1,
        "PreserveOutputStates": not device["PreserveOutputStates"],
    }
    reset = device["PeriyodikModemResetPeriyodu"]
    if 0 == reset:
        alternates["PeriyodikModemResetPeriyodu"] = 3600
    elif reset <= 2583600:
        alternates["PeriyodikModemResetPeriyodu"] = reset + 3600
    else:
        alternates["PeriyodikModemResetPeriyodu"] = 2592000
    return alternates


def escaped(text):
    """Same string, every char as a \\uXXXX escape (parse-path proof)."""
    return json.dumps(text, ensure_ascii=True)[1:-1]


def body_with_escaped(snapshot, field):
    """Same object, but one string field is \\uXXXX-escaped. Order kept."""
    parts = []
    for key, value in snapshot.items():
        if key == "Time":
            continue
        if key == field:
            parts.append('"%s": "%s"' % (key, escaped(value)))
        else:
            parts.append(json.dumps(key) + ": " + json.dumps(value))
    return "{" + ", ".join(parts) + "}"


def phase_c_device():
    snapshot = None
    try:
        _, snapshot = get_json("/config/device")

        # 1) every mutatable writable field, verified and restored one by one
        for field, value in device_config_fields(snapshot).items():
            before = verify_fields("/config/device", {field: value},
                                   "device_" + field)
            require("device_restore_accepted",
                    post_ok("/config/device", before))

        # 2) link-critical fields: validator rejection + escaped strings
        reject_field("/config/device", snapshot,
                     {"WebArayuzuPortu": 0}, "device_WebArayuzuPortu")
        reject_field("/config/device", snapshot,
                     {"NtpServerPortu": 0}, "device_NtpServerPortu")
        reject_field("/config/device", snapshot,
                     {"SimKartPin": 999}, "device_SimKartPin")
        reject_field("/config/device", snapshot,
                     {"TimeZone": 99}, "device_TimeZone")
        reject_field("/config/device", snapshot,
                     {"PeriyodikModemResetPeriyodu": 100},
                     "device_reset_period_bounds")

        for field in ("SimKartAPN", "SimKartAPNSifresi",
                      "SimKartAPNUsername", "NtpServer"):
            value = snapshot[field]
            body = body_with_escaped(snapshot, field)
            status, _ = post_json_raw("/config/device", body)
            _, after = get_json("/config/device")
            require("field_device_%s_same_value_escape_accepted" % field,
                    status == 200 and after.get(field) == value,
                    "HTTP %d value=%r" % (status, after.get(field)))
    finally:
        if snapshot is not None:
            require("device_final_restore_accepted",
                    post_ok("/config/device", snapshot))


def post_json_raw(path, body):
    status, raw = rq("POST", path, body)
    try:
        parsed = json.loads(raw)
    except ValueError:
        parsed = None
    return status, parsed


def free_offset(samples, step=1000):
    """A IOA value above every used address (avoids overlap validation)."""
    highest = 0
    for value in samples:
        if isinstance(value, list):
            highest = max(highest, max((v for v in value
                                        if isinstance(v, int)), default=0))
        elif isinstance(value, int):
            highest = max(highest, value)
    if highest + step + 115 > 0xFFFFFF:
        raise ValueError("No free IOA test range above the current addresses")
    return highest + step


def restore_iec_config(snapshot, index=None):
    """Restore IOAs before disabling a temporarily enabled line."""
    if index is not None and not snapshot["Hatlar"]["inUse"][index]:
        enabled = mutated(snapshot, {})
        enabled["Hatlar"]["inUse"][index] = True
        if not post_ok("/config/iec104", enabled):
            require("iec104_restore_ioas_accepted", False)
            return False
    if not post_ok("/config/iec104", snapshot):
        require("iec104_restore_snapshot_accepted", False)
        return False
    status, after = get_json("/config/iec104")
    diffs = deep_equal(snapshot, after or {})
    good = status == 200 and not diffs
    require("field_iec104_Hatlar_ioa_restored", good,
            "HTTP %d %s" % (status, "; ".join(diffs[:4])))
    return good


def phase_c_iec104():
    snapshot = None
    ioa_restore_index = None
    try:
        _, snapshot = get_json("/config/iec104")
        lines = snapshot["Hatlar"]

        # Scalars (Port and ScadaIPAdresi are link-critical: rejection only)
        t2, t1 = snapshot["T2"], snapshot["T1"]
        scalars = {
            "PeriodicSend": snapshot["PeriodicSend"] - 60
            if snapshot["PeriodicSend"] > 60 else
            snapshot["PeriodicSend"] + 60,
            "T0": snapshot["T0"] + 1 if snapshot["T0"] < 255 else 1,
            "T1": snapshot["T1"] + 1 if snapshot["T1"] < 255 else 2,
            "T3": snapshot["T3"] + 1 if snapshot["T3"] < 65525 else 20,
            "K": snapshot["K"] + 1 if snapshot["K"] < 255 else 1,
            "W": snapshot["W"] + 1 if snapshot["W"] < 255 else 1,
            "OriginatorAddr": snapshot["OriginatorAddr"] + 1
            if snapshot["OriginatorAddr"] < 255 else 0,
            "CommonAddr": snapshot["CommonAddr"] + 1
            if snapshot["CommonAddr"] not in (65535, 254) else 1,
            "SBO": not snapshot["SBO"],
            "SBOTimeout": 30 if snapshot["SBOTimeout"] != 30 else 31,
        }
        # T2 must stay in 1..T1-1; skip when no different valid value exists.
        if t2 + 1 < t1 or (t2 > 1 and t2 - 1 != t2):
            scalars["T2"] = t2 + 1 if t2 + 1 < t1 else t2 - 1
        for field, value in scalars.items():
            payload = mutated(snapshot, {field: value})
            if "SBO" == field and value and payload["SBOTimeout"] > 300:
                payload["SBOTimeout"] = 30
            before = get_json("/config/iec104")[1]
            ok = post_ok("/config/iec104", payload)
            _, after = get_json("/config/iec104")
            actual = after.get(field)
            good = ok and actual == payload[field]
            require("field_iec104_%s_transfer" % field, good,
                    "ok=%s %r" % (ok, actual))
            rq("POST", "/config/iec104", json.dumps(before))

        # AkuUyarisi / ModemReset point addresses -> free window
        used = [lines[key] for key in IEC_LINE_ARRAYS] + \
               [lines["inUse"]] + [snapshot["AkuUyarisi"],
                                   snapshot["ModemReset"]]
        offset = free_offset(used)
        for field in ("AkuUyarisi", "ModemReset"):
            before = verify_fields("/config/iec104", {field: offset},
                                   "iec104_" + field)
            rq("POST", "/config/iec104", json.dumps(before))

        # The 15 IOA arrays are only persisted for IN-USE lines
        # (set_iec_config writes in_use/bases first, then skips the IOA
        # block with `continue` for out-of-use lines). Mutate an already
        # active line in place; when every line is out of use, prove the
        # transfer in one short window: enable line 0 + shift the 15
        # IOA point arrays to free addresses in a single POST, verify,
        # restore originals while still enabled, then disable again.
        # The fault BASE arrays stay original: a base address reserves a
        # contiguous record range and would overlap the shifted points.
        active = [i for i, flag in enumerate(lines["inUse"]) if flag]
        index = active[0] if active else 0
        ioa_points = [name for name in IEC_LINE_ARRAYS
                      if name.startswith("IOA_")]
        ioa_shift = mutated(snapshot, {})
        if not active:
            ioa_shift["Hatlar"]["inUse"][0] = True
        for position, field in enumerate(ioa_points):
            ioa_shift["Hatlar"][field][index] = offset + 100 + position
        # Arm cleanup before sending: a lost reply may hide a successful write.
        ioa_restore_index = index
        status, _ = post_json("/config/iec104", ioa_shift)
        _, after = get_json("/config/iec104")
        bad = []
        if not active and after["Hatlar"]["inUse"][0] is not True:
            bad.append("inUse[0]=%r" % after["Hatlar"]["inUse"][0])
        for position, field in enumerate(ioa_points):
            want = offset + 100 + position
            got = after["Hatlar"][field][index]
            if got != want:
                bad.append("%s=%r!=%r" % (field, got, want))
        require("field_iec104_Hatlar_ioa_transfer",
                status == 200 and not bad,
                "HTTP %d %s" % (status, "; ".join(bad[:4])))

        if not restore_iec_config(snapshot, ioa_restore_index):
            raise RuntimeError("IEC104 restoration failed")
        ioa_restore_index = None

        # Link-critical + validator rejections
        reject_field("/config/iec104", snapshot,
                     {"Port": 0}, "iec104_Port")
        reject_field("/config/iec104", snapshot,
                     {"ScadaIPAdresi": "999.1.1.1"}, "iec104_ScadaIPAdresi")
        reject_field("/config/iec104", snapshot,
                     {"PeriodicSend": 100000}, "iec104_PeriodicSend_bounds")
        reject_field("/config/iec104", snapshot,
                     {"T1": 0}, "iec104_T1_min")
        reject_field("/config/iec104", snapshot,
                     {"T3": 10}, "iec104_T3_bounds")
        reject_field("/config/iec104", snapshot,
                     {"CommonAddr": 0}, "iec104_CommonAddr")

        # Escaped-encoding parse proof for the SCADA address string
        value = snapshot["ScadaIPAdresi"]
        body = body_with_escaped(snapshot, "ScadaIPAdresi")
        status, _ = post_json_raw("/config/iec104", body)
        _, after = get_json("/config/iec104")
        require("field_iec104_ScadaIPAdresi_same_value_escape_accepted",
                status == 200 and after["ScadaIPAdresi"] == value,
                "HTTP %d value=%r" % (status, after["ScadaIPAdresi"]))
    finally:
        if snapshot is not None:
            restore_iec_config(snapshot, ioa_restore_index)


def phase_c_modbus():
    snapshot = None
    try:
        _, snapshot = get_json("/config/modbus")
        base = {k: snapshot[k] for k in ("CihazID", "BaudRate", "StopBits")}
        other = {"CihazID": 40 if base["CihazID"] != 40 else 41,
                 "BaudRate": 38400 if base["BaudRate"] != 38400 else 9600,
                 "StopBits": 2 if base["StopBits"] != 2 else 1}
        for field, value in other.items():
            payload = dict(base)
            payload[field] = value
            ok = post_ok("/config/modbus", payload)
            _, after = get_json("/config/modbus")
            require("field_modbus_%s_transfer" % field,
                    ok and after[field] == value,
                    "ok=%s %r" % (ok, after.get(field)))
            rq("POST", "/config/modbus", json.dumps(base))
        reject_field("/config/modbus", base,
                     {"CihazID": 0}, "modbus_CihazID_zero")
        reject_field("/config/modbus", base,
                     {"CihazID": 248}, "modbus_CihazID_range")
        reject_field("/config/modbus", base,
                     {"BaudRate": 111111}, "modbus_BaudRate_invalid")

        # Read-only RegisterMap posted as a compact unknown key: nested
        # object/array-in-array values must be skipped, not rejected.
        nested = dict(base)
        nested["RegisterMap"] = {"AddressBase": 40000, "FeederCount": 4,
                                 "Phase": [["anlik_akim", 1, 2, "FLOAT32",
                                            "A"]]}
        status, _ = post_json("/config/modbus", nested)
        _, after = get_json("/config/modbus")
        require("field_modbus_RegisterMap_nested_skip",
                status == 200 and after["CihazID"] == base["CihazID"],
                "HTTP %d" % status)
    finally:
        if snapshot is not None:
            payload = {k: snapshot[k]
                       for k in ("CihazID", "BaudRate", "StopBits")}
            rq("POST", "/config/modbus", json.dumps(payload))


def phase_c_rf():
    """Verbatim round-trip + range rejection; no value mutations (see
    module docstring). Range validation is only enforced for in-use
    lines, so rejections target the first in-use index."""
    if not ALLOW_RF_APPLY:
        print("SKIP: RF config POST requires --allow-rf-apply; "
              "even unchanged settings can start RF apply")
        return
    _, snapshot = get_json("/config/rf")
    ok = post_ok("/config/rf", snapshot)
    _, after = get_json("/config/rf")
    diffs = deep_equal(snapshot, after or {})
    require("field_rf_verbatim_all_keys", ok and not diffs,
            "ok=%s %s" % (ok, "; ".join(diffs[:4])))

    in_use = [i for i, flag in enumerate(snapshot["inUse"]) if flag]
    if not in_use:
        print("NOTE: no in-use RF line; range rejections not applicable")
        return
    index = in_use[0]

    payload = mutated(snapshot, {})
    payload["ArtimliAkimEsigi"][index] = 9999.0  # spec R2: 1..2200 A/s
    status, _ = post_json("/config/rf", payload)
    _, unchanged = get_json("/config/rf")
    require("field_rf_ArtimliAkimEsigi_range_rejected",
            status == 400 and unchanged["ArtimliAkimEsigi"] ==
            snapshot["ArtimliAkimEsigi"], "HTTP %d" % status)

    payload = mutated(snapshot, {})
    payload["HatFrekansi"][index] = 55  # {50,60} only
    status, _ = post_json("/config/rf", payload)
    require("field_rf_HatFrekansi_set_rejected", status == 400,
            "HTTP %d" % status)


def phase_d_structural():
    # Malformed JSON is rejected by every config endpoint without a write
    for endpoint in ("/config/device", "/config/iec104", "/config/modbus",
                     "/config/rf"):
        status, _ = post_json_raw(endpoint, '{"broken": ')
        require("structural_%s_malformed_400" % endpoint.split("/")[2],
                status == 400, "HTTP %d" % status)


def main():
    global SESSION, TOKEN, ALLOW_RF_APPLY
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default=os.environ.get(
        "WEB_DEVICE_HOST", "188.59.76.59"))
    parser.add_argument("--allow-rf-apply", action="store_true",
                        help="Allow RF Save/apply, including unchanged config")
    args = parser.parse_args()
    host = args.host
    ALLOW_RF_APPLY = args.allow_rf_apply
    run_tests.HOST = host
    try:
        connect()
    except RuntimeError as err:
        print("SKIP: device %s:80 not reachable (%s)" % (host, err))
        return 0
    snapshots = {}
    try:
        login()
        if not TOKEN:
            require("login_ok", False, "no token")
            return 1
        require("login_ok", True, "")

        # Reference state for the final restoration proof.
        for endpoint in ("/config/device", "/config/iec104",
                         "/config/modbus", "/config/rf"):
            status, body = rq("GET", endpoint)
            snapshots[endpoint] = json.loads(body) if status == 200 else None

        phase_a_schemas()
        for endpoint, name, exclude in (
                ("/config/device", "device", ("Time",)),
                ("/config/iec104", "iec104", ()),
                ("/config/modbus", "modbus", ("RegisterMap",)),
                ("/config/rf", "rf", ())):
            if name == "rf" and not ALLOW_RF_APPLY:
                print("SKIP: RF round-trip requires --allow-rf-apply")
                continue
            verbatim_roundtrip(endpoint, name, exclude)
        phase_c_device()
        phase_c_iec104()
        phase_c_modbus()
        phase_c_rf()
        phase_d_structural()
    finally:
        # Every mutation must be undone: compare the final state with
        # the initial snapshot (volatile tick fields excluded).
        try:
            for endpoint in ("/config/device", "/config/iec104",
                             "/config/modbus", "/config/rf"):
                name = endpoint.split("/")[2]
                status, body = rq("GET", endpoint)
                final = json.loads(body) if status == 200 else None
                diffs = deep_equal(snapshots.get(endpoint), final or {}) \
                    if snapshots.get(endpoint) is not None and status == 200 \
                    else ["HTTP %d" % status]
                require("final_%s_restored" % name, not diffs,
                        "; ".join(diffs[:6]))
        except (OSError, RuntimeError, ValueError) as err:
            require("final_state_verified", False, repr(err))
        SESSION.close()
    print("RESULT: %d failure(s)" % len(FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
