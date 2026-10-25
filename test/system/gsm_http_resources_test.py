"""Compare complete GSM HTTP resources with the actual embedded byte arrays."""
import argparse
import gzip
from pathlib import Path
import re
import socket
import sys
import time
import urllib.parse

from gsm_disconnect_test import ConsoleSession, IecProbe, get_page, wait_listener
import http_port_drop_test as drop

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test/system/iec104_master"))
from oracle import WebDevice, derive_admin_password


def embedded_bytes(name):
    source = (ROOT / "Application/web-server" / (name + ".c")).read_text(encoding="utf-8")
    match = re.search(r"static const uint8_t " + name + r"_data\[\] = \{(.*?)\};", source, re.S)
    if match is None:
        raise AssertionError("embedded resource array was not found")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match[1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--console", default="COM18")
    parser.add_argument("--log", required=True)
    args = parser.parse_args()
    console = ConsoleSession(args.console, logfile=args.log)
    probe = None
    try:
        get_page(args.host, console, timeout=100)
        web = WebDevice(args.host)
        web.login("admin", derive_admin_password(args.host))
        wait_listener(console, len(console.all_lines()), 40)
        probe = IecProbe(args.host)
        for path, name in (("/loading.html", "loading_html"),
                           ("/index.html", "index_html"),
                           ("/fw_update.html", "fw_update_html")):
            probe.ping()
            expected = embedded_bytes(name)
            url = path + "?t=" + urllib.parse.quote(web.token)
            began = time.monotonic()
            with socket.create_connection((args.host, 80), timeout=10) as client:
                client.sendall(drop.build_get_request(args.host, 80, url))
                status, headers, body, complete = drop.recv_response(client, 45)
                assert " 200 " in status and complete, status
                assert len(body) == int(headers["content-length"]), "body length mismatch"
                assert body == expected, "GSM body differs from embedded bytes: " + path
                gzip.decompress(body)
                elapsed = time.monotonic() - began
                closed_at = len(console.all_lines())
            wait_listener(console, closed_at, 40)
            probe.ping()
            print("PASS %s: %d exact bytes, gzip CRC valid, %.3fs receive, IEC OK" %
                  (path, len(body), elapsed), flush=True)
    finally:
        if probe is not None:
            probe.close()
        console.close()


if __name__ == "__main__":
    main()
