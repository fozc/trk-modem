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
        if self.path != "/auth/login":
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
