#!/usr/bin/env python3
"""Device oracle for the IEC104 master suite.

Reads the ACTUAL device configuration instead of assuming defaults:
- web /config/iec104: port, CA/OA, T0-T3, K/W and the full per-line IOA
  map (inUse, IOA_{R,S,T}_* bases, fault bases);
- web /config/rf: RF store feeder configuration;
- console (optional): gsm status IP cross-check, rf online state,
  iec104evtlog unsent interval.

Expected GI IOA sets are derived from these values (revise plan item 8:
doc tables alone are stale; the live config is the oracle).
"""

import http.client
import json
import re
import time
import urllib.parse
import urllib.request
from http.cookiejar import CookieJar

# The device GSM HTTP server closes its single client socket after every
# response (W-01 hardening); a follow-up request must retry through the
# re-listen window (same lesson as configure_dut.py).
REQUEST_ATTEMPTS = 4
REQUEST_RETRY_PAUSE_S = 4.0


class WebDevice:
    """Minimal web client: login + authenticated GET/POST."""

    def __init__(self, host, timeout=45):
        self.host = host
        self.timeout = timeout
        self.jar = CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar))
        self.token = None

    def request(self, path, data=None, method=None):
        url = "http://%s%s" % (self.host, path)
        if self.token:
            sep = "&" if "?" in path else "?"
            url += sep + "t=" + urllib.parse.quote(self.token)
        body = None
        if data is not None:
            body = json.dumps(data, separators=(",", ":")).encode("ascii")
        last_error = None
        for attempt in range(REQUEST_ATTEMPTS):
            req = urllib.request.Request(
                url, data=body,
                headers={"Content-Type": "application/json"},
                method=method or ("POST" if body else "GET"))
            try:
                with self.opener.open(req, timeout=self.timeout) as resp:
                    return resp.status, \
                        resp.read().decode("ascii", "replace")
            except (http.client.RemoteDisconnected, ConnectionError,
                    OSError) as error:
                last_error = error
                if attempt + 1 < REQUEST_ATTEMPTS:
                    time.sleep(REQUEST_RETRY_PAUSE_S)
        raise RuntimeError("%s -> %s" % (path, last_error))

    def login(self, username, password):
        status, payload = self.request(
            "/auth/login", {"username": username, "password": password})
        data = json.loads(payload)
        if status != 200 or data.get("success") is False:
            raise RuntimeError("login rejected: %s" % payload)
        self.token = data.get("token") or data.get("session")
        return True


IOA_KEY_SETS = {
    "anlik": ["IOA_R_AnlikAkim", "IOA_S_AnlikAkim", "IOA_T_AnlikAkim"],
    "enerji": ["IOA_R_EnerjiVarYok", "IOA_S_EnerjiVarYok",
               "IOA_T_EnerjiVarYok"],
    "yuk": ["IOA_R_YukAkimiVarYok", "IOA_S_YukAkimiVarYok",
            "IOA_T_YukAkimiVarYok"],
    "rf": ["IOA_R_RfhabVarYok", "IOA_S_RfhabVarYok",
           "IOA_T_RfhabVarYok"],
    "trip": ["IOA_R_TripFailed", "IOA_S_TripFailed",
             "IOA_T_TripFailed"],
}


def _line_list(data, key):
    values = data.get(key, [])
    return [int(value) for value in values]


def read_oracle(web):
    """Collect the IEC104 + RF store configuration snapshot."""
    status, payload = web.request("/config/iec104")
    if status != 200:
        raise RuntimeError("/config/iec104 HTTP %d" % status)
    cfg = json.loads(payload)

    lines = []
    count = len(cfg.get("Hatlar", {}).get("inUse", []))
    for index in range(count):
        line = {
            "index": index,
            "in_use": bool(cfg["Hatlar"]["inUse"][index]),
            "ioa": {},
        }
        for group, keys in IOA_KEY_SETS.items():
            line["ioa"][group] = [int(cfg["Hatlar"][keys[phase]][index])
                                  for phase in range(3)]
        line["temp_fault_base"] = int(
            cfg["Hatlar"]["TemporaryFaultBase"][index])
        line["perm_fault_base"] = int(
            cfg["Hatlar"]["PermanentFaultBase"][index])
        lines.append(line)

    rf = {}
    try:
        status, payload = web.request("/config/rf")
        if status == 200:
            rf = json.loads(payload)
    except Exception:              # noqa: BLE001 (RF store optional here)
        rf = {}

    return {
        "host": web.host,
        "port": int(cfg.get("Port", 2404)),
        "ca": int(cfg.get("CommonAddr", 1)),
        "oa": int(cfg.get("OriginatorAddr", 1)),
        "t0": int(cfg.get("T0", 90)), "t1": int(cfg.get("T1", 45)),
        "t2": int(cfg.get("T2", 30)), "t3": int(cfg.get("T3", 60)),
        "k": int(cfg.get("K", 64)), "w": int(cfg.get("W", 24)),
        "lines": lines,
        "rf_store": rf,
    }


def expected_ioa_sets(oracle):
    """Expected monitored IOA sets per interrogation scope.

    station (QOI 20) = currents + all status points of in-use lines;
    group 1 (21) = currents; group 2 (22) = status points;
    groups 3/24 depend on stored fault records -> no IOA requirement.
    Returns (station_set, group1_set, group2_set, fault_space).
    """
    currents = set()
    states = set()
    fault_space = set()
    for line in oracle["lines"]:
        if not line["in_use"]:
            continue
        for phase in range(3):
            currents.add(line["ioa"]["anlik"][phase])
            states.add(line["ioa"]["enerji"][phase])
            states.add(line["ioa"]["yuk"][phase])
            states.add(line["ioa"]["rf"][phase])
            states.add(line["ioa"]["trip"][phase])
        # Fault record fields live in a strided region above the base
        # (base .. base+180 per feeder); accept the base block as the
        # IOA space for replay/group3-4 content checks.
        fault_space.add(line["temp_fault_base"])
        fault_space.add(line["perm_fault_base"])
    return currents | states, currents, states, fault_space


def store_feeders(rf_store):
    """Active feeder list (1-based) from /config/rf inUse."""
    return [index + 1 for index, flag in
            enumerate(rf_store.get("inUse", [])) if flag]


def build_rf_store_config(feeders, zone):
    """Minimal POST body for /config/rf (configure_dut.py scheme)."""
    lines = 7
    eui = []
    for line in range(lines):
        for phase in range(3):
            eui.append("%02X%02X%02X%02X%02X%02X%02X%02X" % (
                0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9, 0xF0 | (line & 0x0F),
                ((phase + 1) & 0x0F) * 16 + 0x0A))
    in_use = [False] * lines
    hat = [0] * lines
    for feeder in feeders:
        in_use[feeder - 1] = True
        hat[feeder - 1] = feeder
    return {"inUse": in_use, "HatID": hat, "ZoneID": [zone] * lines,
            "R_DEVICEID": [eui[i * 3] for i in range(lines)],
            "S_DEVICEID": [eui[i * 3 + 1] for i in range(lines)],
            "T_DEVICEID": [eui[i * 3 + 2] for i in range(lines)]}


def derive_admin_password(host):
    """IP-derived admin password (doc/Web_Giris_Sifresi_Plani.md)."""
    return "admin%d" % (int(host.rpartition(".")[2]) + 1)


LINES_TOTAL = 7  # MAX_POWER_LINE_COUNT


def build_iec104_lines_config(feeders):
    """POST /config/iec104 body enabling lines with default IOA formulas.

    The POST seeds current values only for already-in-use lines, so the
    full IOA arrays must accompany the inUse change (nvram.c factory
    formulas: base 1000+i*100, currents +30..32, enerji +40..42,
    yuk +50..52, rfhab +60..62, trip +70..72; fault bases 100000/200000
    + i*1000).
    """
    base = [1000 + index * 100 for index in range(LINES_TOTAL)]
    arrays = {group: [[0] * LINES_TOTAL for _ in range(3)]
              for group in ("anlik", "enerji", "yuk", "rf", "trip")}
    offsets = {"anlik": 30, "enerji": 40, "yuk": 50, "rf": 60, "trip": 70}
    in_use = [False] * LINES_TOTAL
    for feeder in feeders:
        index = feeder - 1
        in_use[index] = True
        for phase in range(3):
            for group, offset in offsets.items():
                arrays[group][phase][index] = base[index] + offset + phase
    keys = {"anlik": "IOA_%s_AnlikAkim", "enerji": "IOA_%s_EnerjiVarYok",
            "yuk": "IOA_%s_YukAkimiVarYok", "rf": "IOA_%s_RfhabVarYok",
            "trip": "IOA_%s_TripFailed"}
    phases = ("R", "S", "T")
    hatlar = {
        "inUse": in_use,
        "TemporaryFaultBase": [100000 + index * 1000
                               for index in range(LINES_TOTAL)],
        "PermanentFaultBase": [200000 + index * 1000
                               for index in range(LINES_TOTAL)],
    }
    for group, pattern in keys.items():
        for phase, letter in enumerate(phases):
            hatlar[pattern % letter] = arrays[group][phase]
    return {"Hatlar": hatlar}


def iec104_lines_empty(snap):
    """True when no IEC104 line is in use (GI would carry no data)."""
    return not any(line["in_use"] for line in snap["lines"])


def console_snapshot(console, console_io):
    """Optional console state: gsm IP, rf online, evtlog status."""
    snapshot = {"gsm_ip": None, "rf_online": None, "evtlog": None}
    if console is None:
        return snapshot
    gsm_text = console_io.run_cmd(console, "gsm status", settle_s=6.0)
    snapshot["gsm_ip"] = console_io.parse_gsm_ip(gsm_text)
    rf_text = console_io.run_cmd(console, "rf status", settle_s=5.0)
    snapshot["rf_online"] = console_io.parse_rf_online(rf_text)
    evt_text = console_io.run_cmd(console, "iec104evtlog status",
                                  settle_s=4.0)
    snapshot["evtlog"] = console_io.parse_evtlog_status(evt_text)
    return snapshot


TS_RE = re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}")


def format_oracle_summary(oracle):
    """One-line summary for logs and manifest."""
    active = [line["index"] + 1 for line in oracle["lines"]
              if line["in_use"]]
    return "port=%d ca=%d t1=%d t2=%d t3=%d k=%d w=%d lines=%s" % (
        oracle["port"], oracle["ca"], oracle["t1"], oracle["t2"],
        oracle["t3"], oracle["k"], oracle["w"], active or "none")
