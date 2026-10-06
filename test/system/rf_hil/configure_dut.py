#!/usr/bin/env python3
"""Configure the DUT RF feeder store over the web API (setup step).

The shell has no RF-store write command; the store feeds the inventory
upload and rf cfg-apply, so HIL cases C/D/E/F need feeders configured.
This script logs in as admin and POSTs /config/rf with a minimal,
range-safe config: the requested feeders x 3 phases with deterministic
test EUI-64s (00:12:4B:00:38:C9:Fx:y - same scheme the simulator and
harness use). Assertions stay console+trace based; HTTP is setup only.

Usage:
  python configure_dut.py --host 188.59.76.59 --feeders 1,2 [--zone 1]
  python configure_dut.py --host ... --clear     # all lines off
Exit: 0 ok, 1 http/config error, 2 usage.
"""

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request
from http.cookiejar import CookieJar

LINES = 7  # MAX_POWER_LINE_COUNT


def eui_hex(line, phase):
    return "%02X%02X%02X%02X%02X%02X%02X%02X" % (
        0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9, 0xF0 | (line & 0x0F),
        (phase & 0x0F) * 16 + 0x0A)


class Device:
    def __init__(self, host, timeout=60):
        self.base = "http://%s" % host
        self.timeout = timeout
        self.jar = CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar))
        self.token = None

    def request(self, path, data=None, method=None):
        url = self.base + path
        body = None
        headers = {"Content-Type": "application/json"}
        if data is not None:
            # Device parser is hand-rolled: no space after ':' allowed.
            body = json.dumps(data, separators=(",", ":")).encode("ascii")
        req = urllib.request.Request(url, data=body, headers=headers,
                                     method=method or
                                     ("POST" if body else "GET"))
        with self.opener.open(req, timeout=self.timeout) as resp:
            payload = resp.read().decode("ascii", "replace")
            return resp.status, payload

    def login(self, username, password):
        status, payload = self.request("/auth/login",
                                       {"username": username,
                                        "password": password})
        if status != 200:
            raise RuntimeError("login HTTP %d" % status)
        data = {}
        try:
            data = json.loads(payload)
        except ValueError:
            pass
        # login endpoint answers 200 even on failure - check the body
        if data.get("success") is False or "Invalid" in payload:
            raise RuntimeError("login rejected: %s" % payload.strip())
        self.token = data.get("token") or data.get("session")
        return True

    def authed(self, path):
        if self.token:
            sep = "&" if "?" in path else "?"
            path = path + sep + "t=" + urllib.parse.quote(self.token)
        return path


def build_config(feeders, zone):
    in_use = [False] * LINES
    hat = [0] * LINES
    zone_arr = [zone] * LINES
    r = [""] * LINES
    s = [""] * LINES
    t = [""] * LINES
    for line in feeders:
        idx = line - 1
        in_use[idx] = True
        hat[idx] = line
        r[idx] = eui_hex(line, 1)
        s[idx] = eui_hex(line, 2)
        t[idx] = eui_hex(line, 3)
    return {"inUse": in_use, "HatID": hat, "ZoneID": zone_arr,
            "R_DEVICEID": r, "S_DEVICEID": s, "T_DEVICEID": t}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--feeders", default="1,2",
                        help="comma separated feeder numbers 1..7")
    parser.add_argument("--zone", type=int, default=1)
    parser.add_argument("--clear", action="store_true")
    parser.add_argument("--user", default="admin")
    parser.add_argument("--pass", dest="password", default=None,
                        help="default: IP-derived (admin<last octet+1> "
                             "per doc/Web_Giris_Sifresi_Plani.md)")
    args = parser.parse_args()

    if args.password is None:
        last_octet = int(args.host.rpartition(".")[2])
        args.password = "admin%d" % (last_octet + 1)
        print("derived admin password from IP: %s" % args.password)

    device = Device(args.host)
    device.login(args.user, args.password)
    print("login ok (token=%s)" % ("yes" if device.token else "cookie"))

    if args.clear:
        config = build_config([], args.zone)
    else:
        feeders = [int(f) for f in args.feeders.split(",") if f.strip()]
        config = build_config(feeders, args.zone)

    # the on-device GSM HTTP server resets itself after each response;
    # give it time and retry through the reset window
    import http.client
    time.sleep(2.0)
    status = None
    payload = ""
    for attempt in range(4):
        try:
            status, payload = device.request(device.authed("/config/rf"),
                                             config)
            break
        except (http.client.RemoteDisconnected, ConnectionError,
                OSError) as err:
            print("POST attempt %d failed (%s); retrying..." %
                  (attempt + 1, type(err).__name__))
            time.sleep(5.0)
    if status is None:
        print("POST /config/rf kept failing", file=sys.stderr)
        return 1
    print("POST /config/rf -> HTTP %d %s" % (status, payload.strip()))
    if status != 200:
        return 1

    time.sleep(1.0)
    status, payload = device.request(device.authed("/config/rf"))
    if status == 200:
        data = json.loads(payload)
        active = [i + 1 for i, flag in enumerate(data.get("inUse", []))
                  if flag]
        print("GET /config/rf inUse lines: %s" % active)
        expected = [] if args.clear else [
            int(f) for f in args.feeders.split(",") if f.strip()]
        if sorted(active) != sorted(expected):
            print("MISMATCH: expected %s" % expected, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
