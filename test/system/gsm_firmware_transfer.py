"""Real GSM firmware transfer, partial-body disconnect and signed install.

Only --apply requests installation. Uses existing web authentication,
Fletcher-16 protocol, serial capture and IEC104 probe infrastructure.
"""
import argparse
import json
from pathlib import Path
import socket
import sys
import time
import urllib.parse

from gsm_disconnect_test import ConsoleSession, IecProbe, get_page, wait_listener
import http_port_drop_test as drop

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test/system/iec104_master"))
from oracle import WebDevice, derive_admin_password


def fletcher16(data):
    first = second = 0
    for value in data:
        first = (first + value) % 255
        second = (second + first) % 255
    return (second << 8) | first


def verify_approved_boot(console, host):
    """Consume bound approval in BSL and require a newer persistent record."""
    def record_sequence(event):
        console.send("su admin")
        time.sleep(0.3)
        start = len(console.all_lines())
        console.send("bootlog dump 8")
        hit = console.wait_for(r"^\s*(\d+)\s+.*\s" + event + r"\s", 15, since=start)
        if hit is None:
            raise AssertionError("missing persistent " + event)
        return int(hit[2].group(1))

    installed = record_sequence("FW_INSTALL_OK")
    start = len(console.all_lines())
    console.send("reset")
    if console.wait_for(r"SPI flash: AT25SF321B", 40, since=start) is None:
        raise AssertionError("approval verification reboot missing")
    get_page(host, console, timeout=180)
    approved = record_sequence("FW_APPROVED")
    if approved <= installed:
        raise AssertionError("approval belongs to an older installation")
    if any("GSMTEST FIXTURE" in line for _, line in console.new_lines(since=start)):
        raise AssertionError("device returned to a test fixture")
    if console.send_and_wait("gsmtest status", r"Command not found", 5) is None:
        raise AssertionError("GSM fault command remains on the device")
    probe = IecProbe(host)
    try:
        probe.ping()
    finally:
        probe.close()
    print("PASS persistent normal-image approval: install seq=%d, approval seq=%d; no fault command; HTTP/IEC OK" %
          (installed, approved), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("image", type=Path)
    parser.add_argument("--console", default="COM18")
    parser.add_argument("--log", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    data = args.image.read_bytes()
    if args.image.suffix != ".efw" or not data:
        parser.error("a non-empty signed EFW package is required")
    console = ConsoleSession(args.console, logfile=args.log)
    probe = None
    try:
        get_page(args.host, console, timeout=100)
        web = WebDevice(args.host)
        web.login("admin", derive_admin_password(args.host))

        def request(path, body=None, method=None):
            status, payload = web.request(path, body, method)
            result = json.loads(payload)
            if status != 200 or result.get("status", "ok") != "ok":
                raise AssertionError("firmware endpoint rejected " + path.split("?", 1)[0])
            if path != "/fw_apply":
                # The response has completed. A listener event preceding
                # that response belongs to this request's connection.
                wait_listener(console, len(console.all_lines()), timeout=40)
            return result

        metadata = {"s": len(data), "h": fletcher16(data[:64])}
        result = request("/s", metadata)
        offset = result.get("received", 0)
        chunk_size = result["cs"]
        assert chunk_size == 4096, result
        probe = IecProbe(args.host)
        probe.ping()
        interrupted = False
        chunks = 0
        while offset < len(data):
            chunk = data[offset:offset + chunk_size]
            path = "/c?o=%d&cs=%d" % (offset, fletcher16(chunk))
            if not interrupted and chunks == 2 and len(chunk) == chunk_size:
                wait_start = len(console.all_lines())
                url = path + "&t=" + urllib.parse.quote(web.token)
                header = ("POST %s HTTP/1.1\r\nHost: %s\r\n"
                          "Content-Type: application/octet-stream\r\n"
                          "Content-Length: %d\r\n\r\n" %
                          (url, args.host, len(chunk))).encode("ascii")
                with socket.create_connection((args.host, 80), timeout=10) as client:
                    client.sendall(header + chunk[:len(chunk) // 2])
                    hit = console.wait_for(r"Request incomplete - waiting", 15, since=wait_start)
                    if hit is None:
                        raise AssertionError("partial firmware body was not observed")
                    close_start = len(console.all_lines())
                    drop.drop_connection(client, False)
                wait_listener(console, close_start, timeout=40)
                state = request("/fw_status")
                assert state["received"] == offset, state
                resumed = request("/s", metadata)
                assert resumed["received"] == offset, resumed
                probe.ping()
                interrupted = True
                print("PASS partial 4096-byte firmware chunk RST: offset preserved, resume confirmed, IEC OK", flush=True)
            request(path, chunk)
            offset += len(chunk)
            chunks += 1
            if chunks % 4 == 0:
                probe.ping()
            print("ACK firmware bytes=%d/%d" % (offset, len(data)), flush=True)
        assert interrupted, "image did not exercise partial chunk interruption"
        request("/f", {"s": len(data)})
        state = request("/fw_status")
        assert state.get("ready") and state["received"] == len(data), state
        probe.ping()
        print("PASS complete signed EFW verified and ready; IEC remained available", flush=True)
        if args.apply:
            probe.close()
            probe = None
            start = len(console.all_lines())
            request("/fw_apply", method="POST")
            if console.wait_for(r"SPI flash: AT25SF321B", 100, since=start) is None:
                raise AssertionError("post-install boot was not captured")
            get_page(args.host, console, timeout=180)
            verify_approved_boot(console, args.host)
            print("PASS GSM firmware install, approved boot and HTTP/IEC recovery", flush=True)
    finally:
        if probe is not None:
            probe.close()
        console.close()


if __name__ == "__main__":
    main()
