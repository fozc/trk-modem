#!/usr/bin/env python3
"""
http_port_drop_test.py — Random TCP port drop after HTTP GET

Tests how a device's HTTP server (e.g. the Smart Breaker modem web server)
copes with a client that closes its TCP connection at a random moment shortly
after an HTTP GET response has been received.

Flow, per iteration:
  1. Open a fresh TCP connection to <host>:<port>.
  2. Send an HTTP/1.1 GET request for <path>.
  3. Receive the response (status line + headers + body), unless --phase says
     to drop earlier.
  4. Wait a random duration drawn from the configured [min, max] interval.
  5. Drop the connection: a hard TCP RST (SO_LINGER 0) by default, or a
     graceful FIN with --graceful.
  6. Log timing details, then repeat for --count iterations.

This stresses the server's socket bookkeeping: leaked sockets, half-closed
states, and resource exhaustion under repeated abrupt disconnects all surface
here. Useful for verifying modem firmware robustness.

Target forms accepted (positional, no scheme needed):
  192.168.1.1                         host=192.168.1.1, port=80, path=/
  192.168.1.1:8080                    host, port=8080
  192.168.1.1/index.html              host, path=/index.html
  192.168.1.1:8080/status.json        host, port, path
  abc.com/abc.html                    hostname + path

Usage:
  python http_port_drop_test.py 192.168.1.1/index.html --min 0.5 --max 3.0 --count 50
  python http_port_drop_test.py 192.168.1.1 --path /abc.html --port 80 --rst --count 0
"""

import argparse
import random
import signal
import socket
import struct
import sys
import time
from typing import Optional

# ── Defaults ────────────────────────────────────────────────────────

DEFAULT_PORT        = 80
DEFAULT_MIN_DELAY   = 1.0      # seconds
DEFAULT_MAX_DELAY   = 5.0      # seconds
DEFAULT_COUNT       = 10       # iterations (0 = run forever)
DEFAULT_TIMEOUT     = 10.0     # seconds per connect / recv op
DEFAULT_PAUSE       = 1.0      # seconds between iterations
MAX_HEADER_BYTES    = 65536
RECV_CHUNK          = 4096
USER_AGENT          = "troika-port-drop-test/1.0"

# ── Global stop flag (set by Ctrl-C handler) ─────────────────────────

_stop = False


def _handle_sigint(_signum, _frame) -> None:
    global _stop
    _stop = True


# ── Logging ──────────────────────────────────────────────────────────

def _ts() -> str:
    now = time.time()
    base = time.strftime("%H:%M:%S", time.localtime(now))
    return f"{base}.{int((now % 1) * 1000):03d}"


def log(msg: str) -> None:
    print(f"[{_ts()}] {msg}", flush=True)


# ── Target parsing ───────────────────────────────────────────────────

def parse_target(target: str, port: Optional[int], path: Optional[str]
                 ) -> tuple:
    """
    Split a bare target string into (host, port, path). A leading scheme,
    an explicit :port, and a path component are all optional.
    """
    s = target.strip()
    if "://" in s:
        s = s.split("://", 1)[1]

    slash = s.find("/")
    if slash >= 0:
        host_part, path_part = s[:slash], s[slash:]
    else:
        host_part, path_part = s, ""

    if ":" in host_part and not host_part.endswith(":"):
        h, _, p = host_part.rpartition(":")
        host = h
        try:
            parsed_port = int(p)
        except ValueError:
            parsed_port = DEFAULT_PORT
    else:
        host = host_part
        parsed_port = DEFAULT_PORT

    if port is not None:
        parsed_port = port
    if not path_part:
        path_part = "/"
    if path is not None:
        path_part = path

    if not host:
        raise ValueError("empty host in target")
    return host, parsed_port, path_part


# ── HTTP client (raw socket) ─────────────────────────────────────────

def build_get_request(host: str, port: int, path: str) -> bytes:
    host_header = host if port == 80 else f"{host}:{port}"
    # keep-alive so the socket stays open after the response and can be
    # dropped at a random moment. Override with --close-header if the
    # server only tolerates Connection: close.
    lines = [
        f"GET {path} HTTP/1.1",
        f"Host: {host_header}",
        f"User-Agent: {USER_AGENT}",
        "Connection: keep-alive",
        "Accept: */*",
        "",
        "",
    ]
    return "\r\n".join(lines).encode("latin-1")


def _recv_until(sock: socket.socket, delimiter: bytes,
                max_bytes: int, buf: bytes) -> tuple:
    """Read until `delimiter` appears in the buffer. Returns (head+buf, ok)."""
    while delimiter not in buf:
        if len(buf) >= max_bytes:
            return buf, False
        chunk = sock.recv(RECV_CHUNK)
        if not chunk:
            return buf, False
        buf += chunk
    return buf, True


def _parse_headers(head: bytes) -> tuple:
    """Return (status_line, {lowered_header: value})."""
    text   = head.decode("latin-1", "replace")
    parts  = text.split("\r\n")
    status = parts[0] if parts else ""
    headers: dict = {}
    for line in parts[1:]:
        if ":" in line:
            k, _, v = line.partition(":")
            headers[k.strip().lower()] = v.strip()
    return status, headers


def _read_chunked(sock: socket.socket, carry: bytes) -> bytes:
    body = b""
    buf  = carry
    while True:
        while b"\r\n" not in buf:
            chunk = sock.recv(RECV_CHUNK)
            if not chunk:
                return body
            buf += chunk
        size_line, _, buf = buf.partition(b"\r\n")
        try:
            size = int(size_line.split(b";")[0].strip(), 16)
        except ValueError:
            return body
        if size == 0:
            return body
        while len(buf) < size + 2:
            chunk = sock.recv(RECV_CHUNK)
            if not chunk:
                return body + buf[:size]
            buf += chunk
        body += buf[:size]
        buf = buf[size + 2:]  # skip data + trailing CRLF


def recv_response(sock: socket.socket, timeout: float,
                  headers_only: bool = False) -> tuple:
    """
    Receive an HTTP response on a keep-alive socket.
    Returns (status_line, headers, body_bytes, ok).
    ok=False means the connection closed before the header terminator.
    With headers_only=True the body is not drained (bytes already read past
    the terminator are returned in body_bytes).
    Socket failures (RST, timeout, ...) propagate to the caller.
    """
    sock.settimeout(timeout)
    buf, ok = _recv_until(sock, b"\r\n\r\n", MAX_HEADER_BYTES, b"")
    if not ok:
        status, headers = _parse_headers(buf)
        return status, headers, b"", False
    head, _, rest = buf.partition(b"\r\n\r\n")
    status, headers = _parse_headers(head)
    if headers_only:
        return status, headers, rest, True

    te = headers.get("transfer-encoding", "").lower()
    cl = headers.get("content-length")
    body = rest
    if "chunked" in te:
        body = _read_chunked(sock, rest)
    elif cl is not None:
        try:
            need = int(cl)
        except ValueError:
            need = 0
        while len(body) < need:
            chunk = sock.recv(min(65536, need - len(body)))
            if not chunk:
                break
            body += chunk
    # else: no length hint — leave whatever landed in `body` already.
    return status, headers, body, True


# ── Connection drop ──────────────────────────────────────────────────

def drop_connection(sock: socket.socket, graceful: bool) -> str:
    """
    Close the socket. With graceful=False a TCP RST is forced via
    SO_LINGER (onoff=1, linger=0); otherwise a normal close sends FIN.
    Returns a short label for logging.
    """
    if not graceful:
        linger = struct.pack("ii", 1, 0)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
        except OSError:
            pass
        try:
            sock.close()
        except OSError:
            pass
        return "RST"
    try:
        sock.close()
    except OSError:
        pass
    return "FIN"


# ── Single test iteration ────────────────────────────────────────────

def run_once(host: str, port: int, path: str,
             min_delay: float, max_delay: float, timeout: float,
             graceful: bool, phase: str, connection_header: str
             ) -> str:
    """
    Run one connect → (optional GET) → random wait → drop cycle.
    With phase='connect-only' no data is sent at all.
    Returns 'DROP', 'RESET', 'NO_RESPONSE', or 'CONNECT_FAIL'.
    """
    req = build_get_request(host, port, path)
    if connection_header:
        req = req.replace(b"Connection: keep-alive",
                          f"Connection: {connection_header}".encode("latin-1"))

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        t0 = time.monotonic()
        sock.connect((host, port))
    except (ConnectionError, OSError) as exc:
        log(f"  connect failed: {exc}")
        try:
            sock.close()
        except OSError:
            pass
        return "CONNECT_FAIL"

    local = sock.getsockname()
    send_request = phase != "connect-only"
    log(f"  connected {local[0]}:{local[1]} -> {host}:{port}"
        + (f"  (GET {path})" if send_request else "  (no data - connect only)"))

    # Send the GET unless this is a connect-only iteration.
    if send_request:
        try:
            sock.sendall(req)
        except (ConnectionError, OSError) as exc:
            log(f"  send failed: {exc}")
            drop_connection(sock, graceful)
            return "NO_RESPONSE"

    # Optionally read part/all of the response. A forced remote close
    # (e.g. WinError 10054) is a test signal, not a crash.
    status, body = "(none)", b""
    if phase in ("after-headers", "after-response"):
        try:
            status, headers, body, ok = recv_response(
                sock, timeout, headers_only=(phase == "after-headers"))
        except ConnectionResetError:
            log("  remote sent RST while reading response")
            drop_connection(sock, graceful)
            return "RESET"
        except (ConnectionError, OSError) as exc:
            log(f"  connection error while reading: {exc}")
            drop_connection(sock, graceful)
            return "NO_RESPONSE"
        if not ok:
            log(f"  no complete response headers (status={status!r}); dropping anyway")
            drop_connection(sock, graceful)
            return "NO_RESPONSE"

    elapsed_ms = (time.monotonic() - t0) * 1000.0
    delay = random.uniform(min_delay, max_delay)
    # Hold timer counts from: connect (connect-only), send (after-send),
    # headers (after-headers) or full response (after-response).
    if phase == "connect-only":
        log(f"  holding {delay:5.2f}s (counting from connect, no data sent)")
    elif phase == "after-send":
        log(f"  request sent in {elapsed_ms:6.1f} ms, "
            f"holding {delay:5.2f}s (counting from send)")
    else:
        note = "  (body not drained)" if phase == "after-headers" else ""
        log(f"  response in {elapsed_ms:7.1f} ms  status={status!r}  "
            f"body={len(body)} B  holding {delay:5.2f}s{note}")

    time.sleep(delay)  # hold the socket open, then drop at the random instant
    label = drop_connection(sock, graceful)
    log(f"  dropped ({label}) after holding {delay:.2f}s")
    return "DROP"


# ── Main ────────────────────────────────────────────────────────────

def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="HTTP GET + random TCP port-drop robustness test.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("target", nargs="?", default=None,
                   help="host[:port]/path  (e.g. 192.168.1.1/index.html)")
    p.add_argument("--host", default=None, help="override host from target")
    p.add_argument("--port", type=int, default=None, help="override port")
    p.add_argument("--path", default=None, help="override path")
    p.add_argument("--min", dest="min_delay", type=float,
                   default=DEFAULT_MIN_DELAY,
                   help="minimum random delay before drop (seconds)")
    p.add_argument("--max", dest="max_delay", type=float,
                   default=DEFAULT_MAX_DELAY,
                   help="maximum random delay before drop (seconds)")
    p.add_argument("--count", type=int, default=DEFAULT_COUNT,
                   help="iterations (0 = run forever until Ctrl-C)")
    p.add_argument("--pause", type=float, default=DEFAULT_PAUSE,
                   help="pause between iterations (seconds)")
    p.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                   help="per-operation socket timeout (seconds)")
    p.add_argument("--graceful", action="store_true",
                   help="drop with FIN instead of a hard RST")
    p.add_argument("--phase", default="after-response",
                   choices=["connect-only", "after-send",
                            "after-headers", "after-response"],
                   help="when the hold timer starts counting: connect "
                        "(no data sent), send, headers received, or full "
                        "response received")
    p.add_argument("--connection-header", default="",
                   help="override Connection header (e.g. 'close')")
    p.add_argument("--seed", type=int, default=None,
                   help="seed the RNG for reproducible delay sequences")
    args = p.parse_args(argv)

    raw = args.target or args.host
    if not raw:
        p.error("a target is required (positional or --host)")
    if args.min_delay < 0 or args.max_delay < args.min_delay:
        p.error("--min must be >= 0 and --max must be >= --min")

    if args.seed is not None:
        random.seed(args.seed)

    try:
        host, port, path = parse_target(raw, args.port, args.path)
    except ValueError as exc:
        p.error(f"bad target: {exc}")

    signal.signal(signal.SIGINT, _handle_sigint)

    drop_mode = "FIN (graceful)" if args.graceful else "RST (hard)"
    log("=" * 64)
    log(f"Target : {host}:{port}{path}")
    log(f"Drop   : {drop_mode}, phase={args.phase}")
    log(f"Delay  : random[{args.min_delay:.2f}, {args.max_delay:.2f}] s")
    log(f"Count  : {'infinite' if args.count == 0 else args.count}"
        f", pause={args.pause:.2f}s")
    log("=" * 64)

    stats = {"DROP": 0, "RESET": 0, "NO_RESPONSE": 0, "CONNECT_FAIL": 0}
    i = 0
    while not _stop and (args.count == 0 or i < args.count):
        i += 1
        log(f"--- iteration {i} ---")
        outcome = run_once(
            host, port, path,
            args.min_delay, args.max_delay, args.timeout,
            args.graceful, args.phase, args.connection_header)
        stats[outcome] = stats.get(outcome, 0) + 1
        if _stop:
            break
        if args.count == 0 or i < args.count:
            time.sleep(args.pause)

    log("-" * 64)
    log(f"Done. iterations={i}  "
        f"dropped={stats.get('DROP', 0)}  "
        f"reset={stats.get('RESET', 0)}  "
        f"no_response={stats.get('NO_RESPONSE', 0)}  "
        f"connect_fail={stats.get('CONNECT_FAIL', 0)}")
    if _stop:
        log("Interrupted by user.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
