"""Serve embedded UI resources locally, with delayed application chunks.

This is a browser preview transport, not a modem timing simulation.
Run from any folder; open http://127.0.0.1:8769/.
Local fixture credentials: admin / preview.
"""
import argparse
import json
import hashlib
from urllib.parse import parse_qs, urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", type=int, default=8769)
parser.add_argument("--chunk-delay", type=float, default=0.5)
parser.add_argument("--rf-preview", action="store_true",
                    help="Serve a synthetic NOT_LIVE RF result (no device access)")
parser.add_argument("--board-preview", action="store_true",
                    help="Serve synthetic board, network and DIP data")
parser.add_argument("--config-preview", action="store_true",
                    help="Serve portable synthetic configuration fixtures")
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]


def resource(name):
    source = (root / f"Application/web-server/{name}_html.c").read_text(
        encoding="utf-8").split("};")[0]
    return bytes(int(value, 16) for value in re.findall(
        r"0x([0-9a-fA-F]{2})", source))


index = resource("index")
loading = resource("loading")
package_etag = '"' + hashlib.sha256(bytes([1]) + index).hexdigest() + '"'


class PreviewHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        # Local test fixture only; never use device credentials here.
        if urlsplit(self.path).path != "/auth/login":
            self.send_error(404)
            return
        try:
            credentials = json.loads(self.rfile.read(
                int(self.headers.get("Content-Length", "0"))))
        except (ValueError, UnicodeError):
            credentials = {}
        valid = credentials == {"username": "admin", "password": "preview"}
        data = {"success": True, "token": "0123456789abcdef0123456789abcdef",
                "role": "admin"} if valid else {
                    "success": False, "error": "Invalid credentials"}
        body = json.dumps(data).encode("utf-8")
        self.send_response(200 if valid else 401)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if args.config_preview and path == "/syslogs":
            query = parse_qs(urlsplit(self.path).query)
            if query.get("t") != ["0123456789abcdef0123456789abcdef"]:
                self.send_error(401)
                return
            offset = max(0, int(query.get("offset", [0])[0]))
            limit = max(0, min(17, int(query.get("limit", [30])[0])))
            logs = [f"#{index} TS:2026-10-22 10:00:00 LVL:1 CODE:TEST INFO:preview event"
                    for index in range(1, 238)]
            payload = json.dumps({"t": len(logs), "recs": "\n".join(
                logs[offset:offset + limit])}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if args.config_preview and path.startswith("/config/"):
            if parse_qs(urlsplit(self.path).query).get("t") != [
                    "0123456789abcdef0123456789abcdef"]:
                self.send_error(401)
                return
            fixtures = json.loads((Path(__file__).with_name(
                "profile_fixtures.json")).read_text(encoding="utf-8"))
            config = next((item["profile"]["config"] for item in fixtures
                           if item["domain"] == path.removeprefix("/config/")),
                          None)
            if config is None:
                self.send_error(404)
                return
            if path == "/config/rf":
                config = {"success": True, "data": config}
            payload = json.dumps(config).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if args.board_preview and path == "/status/board":
            if parse_qs(urlsplit(self.path).query).get("t") != [
                    "0123456789abcdef0123456789abcdef"]:
                self.send_error(401)
                return
            # Synthetic UI fixture; no GPIO, radio or power-board access.
            data = {"GsmRAT":4,"GsmCEREG":1,"GsmCREG":0,"GsmCGREG":0,
                    "GsmRsrp":51,"GsmRsrq":20,"GsmRscp":255,
                    "GsmRxlev":99,"GsmSig":21,"PowerValidFields":32767,
                    "KartDurum":131,"KartDurum2":5,"TelemetriYasi":3,
                    "AlarmMaskesi":0,"DIN":[1,0,0,1],"RLY":[0,1],
                    "DIP":[1,0],"PanelVoltaji":18600,"DcVoltaji":24000,
                    "GirisAkimi":420,"GirisGucu":1008,"AkuGucu":52,
                    "Kaynak":2,"BataryaVoltaji":13200,"BataryaAkimi":40,
                    "BatteryTemp":235,"ChargePertance":825,"BatterySOC":825,
                    "BatterySOH":970,"ChargeState":2,"Capacity":7,
                    "BatteryCapacityUnknown":0,"3V3":3302,"3V8":3801,
                    "5V":5005,"Temp":28,"TempMax":32,"TempMin":24,
                    "TDIE":35,"TDIEMax":38,"TDIEMin":30}
            body = json.dumps(data).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if args.rf_preview and path in ("/config/rf", "/status/rf-group"):
            if parse_qs(urlsplit(self.path).query).get("t") != [
                    "0123456789abcdef0123456789abcdef"]:
                self.send_error(401)
                return
            data = {"inUse": [True, False, False, False], "HatID": [3,0,0,0],
                    "R_DEVICEID": ["0011223344556601"],
                    "S_DEVICEID": ["0011223344556602"],
                    "T_DEVICEID": ["0011223344556603"]} if path == "/config/rf" else {
                        "State":"failed", "BatchState":"stopped", "Line":1,
                        "Feeder":3, "BatchLine":1, "GroupId":17,
                        "Targets":1, "Applied":0, "Skipped":0,
                        "GroupStarted":True, "StopReason":"group_error",
                        "InventoryState":"ready", "InventoryPending":False,
                        "HasReport":True, "MHState":4, "Reason":1,
                        "MemberBitmap":4, "WritesAcked":3, "CanAbort":False,
                        "SaveBlocked":False, "MatchesDesired":True}
            body = json.dumps(data).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        if path not in ("/", "/index.html", "/loading.html", "/index.html.gz"):
            self.send_error(404)
            return
        raw = path == "/index.html.gz"
        if raw and parse_qs(urlsplit(self.path).query).get("t") != [
                "0123456789abcdef0123456789abcdef"]:
            self.send_error(401)
            return
        if raw and self.headers.get("If-None-Match") == package_etag:
            self.send_response(304)
            self.send_header("ETag", package_etag)
            self.send_header("Cache-Control", "private, no-cache")
            self.end_headers()
            return
        body = loading if path in ("/", "/loading.html") else index
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", "application/gzip" if raw else
                         "text/html; charset=utf-8")
        if not raw:
            self.send_header("Content-Encoding", "gzip")
        else:
            self.send_header("ETag", package_etag)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            for offset in range(0, len(body), 1024):
                self.wfile.write(body[offset:offset + 1024])
                self.wfile.flush()
                if raw:
                    time.sleep(args.chunk_delay)
        except (BrokenPipeError, ConnectionResetError):
            pass


print(f"Preview: http://127.0.0.1:{args.port}/", flush=True)
ThreadingHTTPServer(("127.0.0.1", args.port), PreviewHandler).serve_forever()
