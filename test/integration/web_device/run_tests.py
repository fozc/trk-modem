#!/usr/bin/env python3
"""Device-in-the-loop web API checks (bench only; SKIP without hardware).

Drives the real device HTTP server over the GSM link: session handling,
board-status schema (including the 2026-10-08 power-board extras),
RF config/group status endpoints, the removed apply route and the
content-based ETag revalidation (If-None-Match -> 304).
No store-writing request is sent: the only POST body is an intentional
parse error, which must be rejected before any state changes.

Usage: python run_tests.py [host]
Host default: env WEB_DEVICE_HOST or 188.59.76.59.
"""

import json
import os
import socket
import sys
import time

HOST = (sys.argv[1] if len(sys.argv) > 1
        else os.environ.get("WEB_DEVICE_HOST", "188.59.76.59"))
PORT = 80
FAILURES = []


def probe():
    try:
        s = socket.create_connection((HOST, PORT), timeout=8)
        s.close()
        return True
    except OSError:
        return False


def http(method, path, body=None, timeout=45):
    last = None
    for _ in range(4):
        time.sleep(1.0)
        try:
            s = socket.create_connection((HOST, PORT), timeout=15)
            s.settimeout(timeout)
            req = (f"{method} {path} HTTP/1.1\r\nHost: {HOST}\r\n"
                   "Connection: close\r\n")
            if body is not None:
                req += (f"Content-Type: application/json\r\n"
                        f"Content-Length: {len(body)}\r\n")
            req += "\r\n"
            s.sendall(req.encode() + (body.encode() if body else b""))
            data = b""
            while True:
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                data += chunk
            s.close()
            head, _, payload = data.partition(b"\r\n\r\n")
            parts = head.split(b" ")
            if len(parts) > 1 and parts[1].isdigit():
                return int(parts[1]), payload.decode("utf-8", "replace")
            last = "no status line (%d bytes)" % len(data)
        except OSError as err:
            last = repr(err)
    raise RuntimeError("HTTP failed after retries: %s (%s %s)"
                       % (last, method, path))


class DeviceSession:
    """One persistent HTTP/1.1 connection for the whole suite.

    The GSM web server serves a single client at a time; a browser page
    left open keeps polling and holds that slot (see report W-01).
    Holding one keep-alive connection for every check avoids the race.
    Connect right after a device reset for the best chance to own it.
    """

    def __init__(self):
        self.sock = None
        last = None
        for _ in range(120):
            try:
                self.sock = socket.create_connection((HOST, PORT),
                                                     timeout=10)
                self.sock.settimeout(45)
                return
            except OSError as err:
                last = err
                time.sleep(0.5)
        raise RuntimeError("cannot hold the device web slot: %r" % last)

    def request(self, method, path, body=None):
        req = (f"{method} {path} HTTP/1.1\r\nHost: {HOST}\r\n"
               "Connection: keep-alive\r\n")
        if body is not None:
            req += (f"Content-Type: application/json\r\n"
                    f"Content-Length: {len(body)}\r\n")
        req += "\r\n"
        self.sock.sendall(req.encode() + (body.encode() if body else b""))
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("server closed mid-header")
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        length = 0
        for line in head.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1].strip())
        while len(rest) < length:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            rest += chunk
        status = int(head.split(b" ", 2)[1])
        return status, rest[:length].decode("utf-8", "replace")

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print("%s: %s %s" % (status, name, detail))
    if not condition:
        FAILURES.append(name)


def request_once(method, path, extra_headers=""):
    """One Connection:close request; returns (status, head, body).

    The ETag revalidation checks need response headers, which the
    keep-alive DeviceSession.request discards. Each call opens its own
    short-lived connection exactly like a revalidating browser fetch;
    the device serves one client at a time, so callers must not race
    this with an open DeviceSession.
    """
    last = None
    for _ in range(4):
        time.sleep(1.0)
        try:
            s = socket.create_connection((HOST, PORT), timeout=15)
            s.settimeout(45)
            req = (f"{method} {path} HTTP/1.1\r\nHost: {HOST}\r\n"
                   "Connection: close\r\n" + extra_headers + "\r\n")
            s.sendall(req.encode())
            data = b""
            while True:
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                data += chunk
            s.close()
            head, _, payload = data.partition(b"\r\n\r\n")
            parts = head.split(b" ", 2)
            if 2 <= len(parts) and parts[1].isdigit():
                return (int(parts[1]), head.decode("utf-8", "replace"),
                        payload.decode("utf-8", "replace"))
            last = "no status line (%d bytes)" % len(data)
        except OSError as err:
            last = repr(err)
    raise RuntimeError("HTTP failed after retries: %s (%s %s)"
                       % (last, method, path))


def main():
    # Hold the single web client slot; retry while the server settles
    # after a reset so an external page cannot re-grab it first.
    # A bench without the device simply exhausts the loop (SKIP).
    try:
        sess = DeviceSession()
    except RuntimeError as err:
        print("SKIP: device %s:%d not reachable (%s)" % (HOST, PORT, err))
        return 0
    http_req = sess.request

    st, body = http_req("POST", "/auth/login",
                        '{"username":"admin","password":"wrong"}')
    check("login_wrong_password_rejected",
          st == 401 and '"success":false' in body, "HTTP %d" % st)

    st, body = http_req("POST", "/auth/login",
                        '{"username":"admin","password":"admin60"}')
    check("login_ok", st == 200 and '"token"' in body, "HTTP %d" % st)
    token = json.loads(body).get("token", "")
    t = "t=" + token

    st, _ = http_req("GET", "/status/board")
    check("board_requires_token", st == 401, "HTTP %d" % st)

    st, body = http_req("GET", "/status/board?" + t)
    board = json.loads(body) if st == 200 else {}
    required = ["PanelVoltaji", "DcVoltaji", "GirisAkimi", "GirisGucu",
                "AkuGucu", "Kaynak", "TelemetriYasi", "AlarmMaskesi",
                "KartDurum", "KartDurum2", "PowerValidFields",
                "BatterySOC", "ChargeState", "3V3"]
    missing = [k for k in required if k not in board]
    check("board_schema_complete", st == 200 and not missing,
          "missing=%s" % missing)
    # Gecersiz alanlar artik degerini tasir; istemci PowerValidFields
    # ile isaretler (null davranisi 2026-10-11'de degisti).
    mask = board.get("PowerValidFields", 0)
    gated = ["GirisAkimi", "GirisGucu", "AkuGucu", "BatterySOC",
             "BatterySOH"]
    raw_visible = all(k in board and board[k] is not None
                      for k in gated)
    check("board_fields_carry_raw_values", raw_visible,
          "null gelen alanlar=%s" % [k for k in gated
                                     if board.get(k) is None])
    summary_bit = 1 << 0
    if mask & summary_bit and not mask & (1 << 2):
        check("board_mask_marks_charger_invalid",
              isinstance(board.get("GirisAkimi"), (int, float)),
              "GirisAkimi sayi degil")

    st, body = http_req("GET", "/config/rf?" + t)
    rf = json.loads(body) if st == 200 else {}
    check("rf_config_readable", st == 200 and "inUse" in rf
          and "ArtimliAkimEsigi" in rf, "HTTP %d" % st)

    st, body = http_req("GET", "/status/rf-group?" + t)
    group = json.loads(body) if st == 200 else {}
    group_keys = ["State", "BatchState", "Targets", "Applied",
                  "StopReason", "SaveBlocked", "MatchesDesired"]
    missing = [k for k in group_keys if k not in group]
    check("rf_group_schema_complete", st == 200 and not missing,
          "missing=%s" % missing)

    st, body = http_req("GET", "/monitor/rf/0?" + t)
    mon = json.loads(body) if st == 200 else {}
    lines = mon.get("lines", [])
    check("rf_monitor_schema", st == 200 and lines
          and "Phases" in lines[0], "HTTP %d" % st)

    st, body = http_req("POST", "/config/rf/apply/1/7?" + t)
    check("removed_apply_route_is_410", st == 410, "HTTP %d" % st)

    st, body = http_req("POST", "/config/rf?" + t,
                        '{"inUse": "garbage"}')
    check("rf_config_parse_error_is_400", st == 400, "HTTP %d" % st)

    st, body = http_req("GET", "/config/rf?" + t)
    after = json.loads(body) if st == 200 else {}
    check("rf_config_unchanged_after_parse_error",
          after.get("inUse") == rf.get("inUse"))

    # Fixed register map: the Modbus page carries only the serial-line
    # settings; register addresses are compile-time fixed (modbus_map.h).
    # The POST body carries only a stray unknown key: the handler seeds
    # CihazID/BaudRate from NVRAM, so a bench device keeps its settings.
    st, body = http_req("GET", "/config/modbus?" + t)
    modbus_before = json.loads(body) if st == 200 else {}
    legacy_payload = '{"Hat":{"inUse":[true],"ADDR_R_AnlikAkim":[40006]}}'
    st, body = http_req("POST", "/config/modbus?" + t, legacy_payload)
    check("modbus_config_post_ignores_unknown_keys", st == 200,
          "HTTP %d %s" % (st, body[:60]))
    st, body = http_req("GET", "/config/modbus?" + t)
    modbus_after = json.loads(body) if st == 200 else {}
    check("modbus_config_shape_is_fixed",
          sorted(modbus_after.keys()) == ["BaudRate", "CihazID", "RegisterMap",
                                          "SonHataKodu", "SonHataZamani"],
          str(sorted(modbus_after.keys())))
    register_map = modbus_after.get("RegisterMap", {})
    check("modbus_register_map_from_device",
          register_map.get("AddressBase") == 40000 and
          register_map.get("FeederCount") == 4 and
          ["anlik_akim", 1, 2, "FLOAT32", "A"] in
          register_map.get("Phase", []))
    check("modbus_config_settings_untouched",
          modbus_after.get("CihazID") == modbus_before.get("CihazID") and
          modbus_after.get("BaudRate") == modbus_before.get("BaudRate"),
          "%s -> %s" % (modbus_before.get("CihazID"),
                        modbus_after.get("CihazID")))


    sess.close()

    # Icerik ozeti tasiyan ETag ile kosullu GET: eslesen deger 304 +
    # bos govde, eslesmeyen deger tam sayfa dondurmelidir.
    st, head, body = request_once("GET", "/")
    etag = ""
    for line in head.split("\r\n"):
        if line.lower().startswith("etag:"):
            etag = line.split(":", 1)[1].strip()
    check("etag_present_on_root", st == 200 and etag.startswith('"'),
          "ETag=%s" % etag)

    if etag:
        st, _, body = request_once(
            "GET", "/", "If-None-Match: %s\r\n" % etag)
        check("etag_match_revalidates_304_empty",
              st == 304 and 0 == len(body),
              "HTTP %d, %d B" % (st, len(body)))

        st, _, body = request_once(
            "GET", "/", 'If-None-Match: "stale"\r\n')
        check("etag_mismatch_returns_full_body",
              st == 200 and 1000 < len(body),
              "HTTP %d, %d B" % (st, len(body)))

    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
