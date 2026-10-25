"""Device test: drop GSM HTTP connections and verify clean recovery.

Uses the existing TCP drop helpers and serial ConsoleSession. No settings,
relay commands or firmware chunks are written. Logs belong under test/build.
"""

import argparse
import gzip
from pathlib import Path
import socket
import sys
import time

import http_port_drop_test as drop

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "hwtest"))
from serial_io import ConsoleSession


def wait_listener(console, start, timeout=25):
    deadline = time.monotonic() + timeout
    cursor = start
    command = ""
    while time.monotonic() < deadline:
        lines = console.all_lines()
        for _, line in lines[cursor:]:
            if "AT TX>" in line:
                command = line.split("AT TX>", 1)[1].strip()
            if "AT RX<" in line:
                if "#SS: 1,4" in line:
                    return
                if command == "AT#SL=1,1,80,0" and "[OK]" in line:
                    return
        cursor = len(lines)
        time.sleep(0.05)
    raise AssertionError("web listener did not return to listening")


def get_page(host, console, timeout=25, response_timeout=8):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, 80), timeout=4) as client:
                client.sendall(drop.build_get_request(host, 80, "/loading.html"))
                status, headers, body, complete = drop.recv_response(client, response_timeout)
                if not complete or " 200 " not in status:
                    raise AssertionError("unexpected HTTP status: " + status)
                if len(body) != int(headers["content-length"]):
                    raise AssertionError("incomplete HTTP body")
                if headers.get("content-encoding") == "gzip":
                    gzip.decompress(body)
                closed_at = len(console.all_lines())
            wait_listener(console, closed_at)
            return body
        except (OSError, AssertionError) as error:
            last_error = error
            time.sleep(0.5)
    raise AssertionError("HTTP did not recover: %r" % last_error)


class IecProbe:
    """Keep a second socket alive with U-frames; no process commands."""

    def __init__(self, host):
        self.client = socket.create_connection((host, 2404), timeout=10)
        self.client.settimeout(10)
        self.buffer = b""

    def ping(self):
        self.client.sendall(bytes.fromhex("68 04 43 00 00 00"))
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            while len(self.buffer) >= 2:
                if self.buffer[0] != 0x68:
                    raise AssertionError("invalid IEC104 frame")
                length = self.buffer[1] + 2
                if len(self.buffer) < length:
                    break
                frame, self.buffer = self.buffer[:length], self.buffer[length:]
                if frame == bytes.fromhex("68 04 83 00 00 00"):
                    return
                if frame == bytes.fromhex("68 04 43 00 00 00"):
                    self.client.sendall(bytes.fromhex("68 04 83 00 00 00"))
            data = self.client.recv(256)
            if not data:
                raise AssertionError("IEC104 connection closed during HTTP test")
            self.buffer += data
        raise AssertionError("IEC104 TESTFR confirmation missing")

    def close(self):
        self.client.close()


def drop_post_body(host, console, graceful):
    body = b"{" + b" " * 4094 + b"}"
    request = ("POST /auth/login HTTP/1.1\r\nHost: %s\r\n"
               "Content-Type: application/json\r\nContent-Length: 4096\r\n"
               "Connection: keep-alive\r\n\r\n" % host).encode() + body
    with socket.create_connection((host, 80), timeout=8) as client:
        start = len(console.all_lines())
        client.sendall(request)
        hit = console.wait_for(r"Body complete.*4096|body_length=4096",
                               timeout_s=12, since=start)
        if hit is None:
            raise AssertionError("4096-byte body completion not observed")
        drop.drop_connection(client, graceful)


def check_trace(lines):
    carrier_lost = False
    confirmed_closed = False
    closed_count = 0
    for line in lines.splitlines():
        if "AT URC: NO CARRIER:1" in line:
            carrier_lost = True
        if "AT TX> AT#SA=1,1" in line:
            carrier_lost = False
        if "AT RX<" in line and "#SS: 1,0" in line:
            confirmed_closed = True
            closed_count += 1
        if "AT TX> AT#SH=1" in line and confirmed_closed:
            raise AssertionError("SH sent after confirmed SS=0")
        if "AT TX> AT#SL=1," in line:
            confirmed_closed = False
        if "AT TX> AT#SSENDEXT=1," in line and carrier_lost:
            raise AssertionError("SSENDEXT sent after carrier loss before new SA")
    for marker in ("Powering on GSM", "HardFault", "SEND_DATA timeout"):
        if marker in lines:
            raise AssertionError("unexpected recovery: " + marker)
    if closed_count == 0:
        raise AssertionError("no confirmed socket closure observed")
    return closed_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--console", default="COM18")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--log", default="test/build/gsm-disconnect-device.log")
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    Path(args.log).parent.mkdir(parents=True, exist_ok=True)
    console = ConsoleSession(args.console, logfile=args.log)
    iec = None
    checks = 0
    try:
        reference = get_page(args.host, console)
        time.sleep(1)
        iec = IecProbe(args.host)
        iec.ping()
        print("PASS: baseline HTTP body and IEC104 TESTFR", flush=True)
        start = len(console.all_lines())
        for cycle in range(args.rounds):
            for graceful in (True, False):
                for phase in ("connect-only", "after-send", "after-headers"):
                    result = drop.run_once(args.host, 80, "/", 0, 0, 12,
                                           graceful, phase, "")
                    if result != "DROP":
                        raise AssertionError("drop failed: " + result)
                    began = time.monotonic()
                    if get_page(args.host, console) != reference:
                        raise AssertionError("stale or corrupted HTTP response")
                    iec.ping()
                    checks += 1
                    print("PASS: round=%d %s %s, HTTP verified and listening in %.3fs, IEC OK"
                          % (cycle + 1, "FIN" if graceful else "RST", phase,
                             time.monotonic() - began), flush=True)
                    time.sleep(1)
                drop_post_body(args.host, console, graceful)
                if get_page(args.host, console) != reference:
                    raise AssertionError("stale response after POST abort")
                iec.ping()
                checks += 1
                print("PASS: round=%d %s after 4096-byte POST, HTTP/IEC OK"
                      % (cycle + 1, "FIN" if graceful else "RST"), flush=True)
                time.sleep(1)
        lines = "\n".join(line for _, line in console.all_lines()[start:])
        closed_count = check_trace(lines)
        print("PASS: %d confirmed closures; no stale SSENDEXT or redundant SH"
              % closed_count, flush=True)
        print("PASS: %d disconnect/reconnect scenarios; no modem restart" % checks,
              flush=True)
        return 0
    finally:
        if iec is not None:
            iec.close()
        console.close()


if __name__ == "__main__":
    sys.exit(main())
