#!/usr/bin/env python3
"""
fw_update_tcp.py — Smart Breaker RFWU Firmware Update GUI

Protocol summary (raw_tcp_fw_update.h):
  Packet = header(12) + data(0..1028) + crc32(4)
  Header: magic(4 LE) + cmd(1) + flags(1) + data_len(2 LE) + seq(4 LE)
  Magic : 0x55574652  ("RFWU" on wire, LE)
  CRC-32: standard zlib/Ethernet polynomial over header+data

Usage:
  python fw_update_tcp.py
"""

import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext
import os
import queue
import socket
import struct
import threading
import time
import zlib

# ── Protocol constants ───────────────────────────────────────────────

RFWU_MAGIC      = 0x55574652  # "RFWU" LE on wire
RFWU_CHUNK_SIZE = 1024

CMD_HELLO   = 0x01
CMD_DATA    = 0x02
CMD_FINISH  = 0x03
CMD_QUERY   = 0x04
CMD_REBOOT  = 0x05
CMD_ABORT   = 0x06
CMD_APPLY   = 0x07

RESP_ACK    = 0x81
RESP_NACK   = 0x82
RESP_STATUS = 0x83

NACK_ERRORS = {
    0x01: "Auth failed",
    0x02: "Packet CRC error",
    0x03: "Offset gap",
    0x04: "Data overflow",
    0x05: "Flash write error",
    0x06: "No active session",
    0x07: "Size mismatch",
}

SOCKET_TIMEOUT = 30.0  # seconds per operation

# ── Packet codec ─────────────────────────────────────────────────────

def _crc32(data: bytes) -> int:
    # efw_crc_finalize() uses reflect-only (no final XOR 0xFFFFFFFF).
    # zlib.crc32 is standard CRC-32 which includes the final XOR.
    # We undo it to match the MCU implementation.
    return (zlib.crc32(data) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def build_packet(cmd: int, data: bytes = b"", seq: int = 0) -> bytes:
    header = struct.pack("<IBBHI", RFWU_MAGIC, cmd, 0, len(data), seq)
    crc    = _crc32(header + data)
    return header + data + struct.pack("<I", crc)


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Connection closed by remote")
        buf += chunk
    return buf


def recv_packet(sock: socket.socket) -> tuple:
    """Return (cmd: int, data: bytes).  Raises on CRC mismatch or disconnect."""
    header = _recv_exact(sock, 12)
    magic, cmd, _flags, data_len, _seq = struct.unpack("<IBBHI", header)
    if magic != RFWU_MAGIC:
        raise ValueError(f"Invalid magic: 0x{magic:08X}")
    data      = _recv_exact(sock, data_len) if data_len > 0 else b""
    crc_bytes = _recv_exact(sock, 4)
    expected  = struct.unpack("<I", crc_bytes)[0]
    computed  = _crc32(header + data)
    if computed != expected:
        raise ValueError(f"CRC mismatch: got 0x{computed:08X}, expected 0x{expected:08X}")
    return cmd, data


def parse_key(key_str: str) -> int:
    """
    Accept:
      "SMAR"       — 4 ASCII chars  → big-endian uint32 = 0x534D4152
      "0x534D4152" — hex literal    → uint32
      "1397049682" — decimal        → uint32
    The 4-char ASCII interpretation matches the MCU default (0x534D4152 = "SMAR").
    """
    s = key_str.strip()
    if s.lower().startswith("0x"):
        return int(s, 16) & 0xFFFFFFFF
    if len(s) == 4:
        return struct.unpack(">I", s.encode("ascii"))[0]
    return int(s) & 0xFFFFFFFF


def compute_auth_token(shared_key: int, total_size: int) -> int:
    """auth_token = CRC32(shared_key_LE_bytes) XOR total_size"""
    key_bytes = struct.pack("<I", shared_key)
    return (_crc32(key_bytes) ^ total_size) & 0xFFFFFFFF


def compute_file_hash(fw_data: bytes) -> int:
    """CRC-32 of the first min(1024, len) bytes — used as resume identity."""
    return _crc32(fw_data[:min(1024, len(fw_data))]) & 0xFFFFFFFF


# ── Background workers ───────────────────────────────────────────────

class TransferWorker:
    """Runs in a background thread; communicates with the GUI via a Queue."""

    def __init__(self, sock: socket.socket, fw_data: bytes,
                 shared_key: int, q: queue.Queue,
                 force_restart: bool = False) -> None:
        self._sock          = sock
        self.fw_data        = fw_data
        self.shared_key     = shared_key
        self.q              = q
        self._stop          = threading.Event()
        self._force_restart = force_restart
        self._tx_count:   int = 0
        self._nack_count: int = 0

    def stop(self) -> None:
        self._stop.set()

    # -- helpers --------------------------------------------------------

    def _log(self, msg: str) -> None:
        self.q.put(("log", msg))

    def _progress(self, sent: int, total: int) -> None:
        self.q.put(("progress", sent, total))

    # Command names for logging
    _CMD_NAMES = {
        CMD_HELLO: "HELLO", CMD_DATA: "DATA", CMD_FINISH: "FINISH",
        CMD_QUERY: "QUERY", CMD_REBOOT: "REBOOT", CMD_ABORT: "ABORT",
    }
    _RESP_NAMES = {
        RESP_ACK: "ACK", RESP_NACK: "NACK", RESP_STATUS: "STATUS",
    }

    def _transact(self, cmd: int, data: bytes = b"", seq: int = 0) -> tuple:
        assert self._sock is not None
        self._sock.sendall(build_packet(cmd, data, seq))
        resp_cmd, resp_data = recv_packet(self._sock)
        return resp_cmd, resp_data

    # -- main -----------------------------------------------------------

    def run(self) -> None:
        try:
            self._do_transfer()
        except (ConnectionError, OSError) as exc:
            if not self._stop.is_set():
                self.q.put(("phase", "ERROR"))
                self.q.put(("remote_disconnect", str(exc)))
        except Exception as exc:
            if not self._stop.is_set():
                self.q.put(("phase", "ERROR"))
                self.q.put(("error", str(exc)))

    def _do_transfer(self) -> None:
        total_size  = len(self.fw_data)
        file_hash   = compute_file_hash(self.fw_data)
        auth_token  = compute_auth_token(self.shared_key, total_size)

        # ── Optional force restart ────────────────────────────────────
        if self._force_restart:
            self._log("TX  ABORT  (force restart — clearing MCU session)")
            try:
                self._transact(CMD_ABORT)
            except Exception:
                pass  # NACK if no active session — that is fine

        # ── HELLO ───────────────────────────────────────────────────
        self.q.put(("phase", "HELLO"))
        hello_payload = struct.pack("<III", total_size, file_hash, auth_token)
        self._log(
            f"TX  HELLO  total={total_size:,}  hash=0x{file_hash:08X}  "
            f"token=0x{auth_token:08X}")
        resp_cmd, resp_data = self._transact(CMD_HELLO, hello_payload)

        if resp_cmd == RESP_NACK:
            code = resp_data[0] if resp_data else 0
            self._log(
                f"RX  NACK  [{NACK_ERRORS.get(code, f'err=0x{code:02X}')}]")
            raise RuntimeError(
                f"HELLO rejected: {NACK_ERRORS.get(code, f'err=0x{code:02X}')}")

        if resp_cmd != RESP_ACK or len(resp_data) < 6:
            raise RuntimeError(f"Unexpected HELLO response: 0x{resp_cmd:02X}")

        resume_offset, chunk_size = struct.unpack_from("<IH", resp_data)
        self._log(
            f"RX  ACK   resume_offset={resume_offset:,}  "
            f"chunk_size={chunk_size}")

        if resume_offset > 0:
            self._log(
                f"Resuming from {resume_offset:,} / {total_size:,} bytes "
                f"({resume_offset * 100 // total_size}%)")
        else:
            self._log(f"Starting new transfer ({total_size:,} bytes)…")

        self._progress(resume_offset, total_size)
        self.q.put(("start_offset", resume_offset))  # for ETA calculation        self.q.put(("phase", "RESUMED" if resume_offset > 0 else "DATA"))
        # ── DATA chunks ─────────────────────────────────────────────
        offset = resume_offset
        seq    = 0

        while offset < total_size:
            if self._stop.is_set():
                self._log("Transfer stopped. MCU session preserved — reconnect to resume.")
                return

            chunk   = self.fw_data[offset : offset + chunk_size]
            payload = struct.pack("<I", offset) + chunk
            pct     = offset * 100.0 / total_size
            self._log(
                f"TX  DATA   seq={seq:04d}  offset={offset:>9,}  "
                f"len={len(chunk):4d}  ({pct:5.1f}%)")

            resp_cmd, resp_data = self._transact(CMD_DATA, payload, seq)
            seq += 1

            if resp_cmd == RESP_NACK:
                code = resp_data[0] if resp_data else 0
                self._nack_count += 1
                if code == 0x03 and len(resp_data) >= 5:   # GAP — MCU corrects us
                    corrected = struct.unpack_from("<I", resp_data, 1)[0]
                    self._log(
                        f"RX  NACK  [GAP] expected={corrected:,}  "
                        f"rewinding…")
                    self.q.put(("pkt_stats", self._tx_count, self._nack_count))
                    offset = corrected
                    continue
                err_str = NACK_ERRORS.get(code, f'err=0x{code:02X}')
                self._log(f"RX  NACK  [{err_str}]")
                self.q.put(("pkt_stats", self._tx_count, self._nack_count))
                raise RuntimeError(
                    f"DATA rejected at offset {offset:,}: {err_str}")

            if resp_cmd != RESP_ACK or len(resp_data) < 4:
                raise RuntimeError(f"Unexpected DATA response: 0x{resp_cmd:02X}")

            received = struct.unpack_from("<I", resp_data)[0]
            self._log(f"RX  ACK   received={received:,}")
            offset   = received  # advance to MCU-confirmed offset
            self._progress(offset, total_size)
            self._tx_count += 1
            self.q.put(("pkt_stats", self._tx_count, self._nack_count))

        # ── FINISH ──────────────────────────────────────────────────
        self.q.put(("phase", "FINISH"))
        self._log(f"TX  FINISH  total={total_size:,}")
        resp_cmd, resp_data = self._transact(CMD_FINISH, struct.pack("<I", total_size))

        if resp_cmd == RESP_NACK:
            code    = resp_data[0] if resp_data else 0
            err_str = NACK_ERRORS.get(code, f'err=0x{code:02X}')
            self._log(f"RX  NACK  [{err_str}]")
            raise RuntimeError(f"FINISH rejected: {err_str}")

        if len(resp_data) >= 4:
            confirmed = struct.unpack_from("<I", resp_data)[0]
            self._log(f"RX  ACK   confirmed={confirmed:,}  Transfer complete!")
        else:
            self._log("RX  ACK   Transfer complete!")

        self._progress(total_size, total_size)
        self.q.put(("phase", "DONE"))
        self.q.put(("done",))


class _OneShot:
    """Base class for one-shot (non-transfer) command workers."""

    def __init__(self, sock: socket.socket, q: queue.Queue) -> None:
        self._sock = sock
        self.q     = q

    def _transact(self, cmd: int, data: bytes = b"") -> tuple:
        self._sock.sendall(build_packet(cmd, data))
        return recv_packet(self._sock)


class StatusWorker(_OneShot):
    def run(self) -> None:
        try:
            cmd, data = self._transact(CMD_QUERY)
            if cmd == RESP_STATUS and len(data) >= 17:
                active, whead, nv_total, nv_rx, nv_hash = struct.unpack_from("<BIIII", data)
                self.q.put(("status_result", bool(active), whead,
                            nv_total, nv_rx, nv_hash))
            elif cmd == RESP_STATUS and len(data) >= 9:
                active, rx, total = struct.unpack_from("<BII", data)
                self.q.put(("status_result", bool(active), rx, total, 0, 0))
            else:
                self.q.put(("error", f"Unexpected status response: 0x{cmd:02X}"))
        except (ConnectionError, OSError) as exc:
            self.q.put(("remote_disconnect", str(exc)))
        except Exception as exc:
            self.q.put(("error", str(exc)))


class RebootWorker(_OneShot):
    def run(self) -> None:
        try:
            self._sock.sendall(build_packet(CMD_REBOOT))
            try:
                recv_packet(self._sock)   # MCU may reset before replying — that is fine
            except Exception:
                pass
            self.q.put(("log", "Reboot command sent."))
            self.q.put(("disconnected",))  # connection closed after device reboot
        except Exception as exc:
            self.q.put(("error", str(exc)))


class ApplyWorker(_OneShot):
    def run(self) -> None:
        try:
            self._sock.sendall(build_packet(CMD_APPLY))
            try:
                resp_cmd, resp_data = recv_packet(self._sock)
                if resp_cmd == RESP_NACK:
                    code = resp_data[0] if resp_data else 0
                    err_str = NACK_ERRORS.get(code, f"err=0x{code:02X}")
                    self.q.put(("error", f"Apply rejected: {err_str}"))
                    return
            except Exception:
                pass  # MCU may reset before replying
            self.q.put(("log", "Apply command sent. Device entering bootloader update mode."))
            self.q.put(("disconnected",))
        except Exception as exc:
            self.q.put(("error", str(exc)))


class ConnectWorker:
    """Opens a persistent TCP connection and passes the socket to the GUI via the queue."""

    def __init__(self, host: str, port: int, q: queue.Queue) -> None:
        self.host = host
        self.port = port
        self.q    = q

    def run(self) -> None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(SOCKET_TIMEOUT)
            sock.connect((self.host, self.port))
            self.q.put(("connected", sock))
        except Exception as exc:
            self.q.put(("connect_error", str(exc)))


class SocketWatcher:
    """
    Monitors an idle (non-transferring) socket for remote closure.
    Uses select() with a short timeout so stop() can interrupt it quickly
    without consuming any data bytes from the socket.
    """

    def __init__(self, sock: socket.socket, q: queue.Queue) -> None:
        self._sock   = sock
        self.q       = q
        self._stop   = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._stop.clear()
        self._thread = threading.Thread(target=self.run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """Signal the watcher to stop and wait for it to exit (max 0.5 s)."""
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=0.5)
        self._thread = None

    def run(self) -> None:
        import select as _select
        while not self._stop.is_set():
            try:
                readable, _, _ = _select.select([self._sock], [], [], 0.2)
                if not readable:
                    continue  # timeout — check stop flag and loop
                if self._stop.is_set():
                    return
                data = self._sock.recv(256)  # drain up to 256 bytes
                if self._stop.is_set():
                    return
                if data == b"":
                    # TCP FIN received — remote closed the connection
                    self.q.put(("remote_disconnect",
                                "Connection closed by remote"))
                    return
                # Unexpected data while idle (e.g. delayed MCU response):
                # silently discard and keep watching — do NOT disconnect.
                # The watcher's only job is to detect socket closure.
            except (ConnectionError, OSError) as exc:
                if not self._stop.is_set():
                    self.q.put(("remote_disconnect", str(exc)))
                return
            except Exception:
                return  # watcher stopped deliberately


# ── GUI ──────────────────────────────────────────────────────────────

class App(tk.Tk):
    _PAD = 8

    def __init__(self) -> None:
        super().__init__()
        self.title("Smart Breaker — RFWU Firmware Update")
        self.resizable(True, True)

        self._q:            queue.Queue           = queue.Queue()
        self._worker:        TransferWorker | None = None
        self._worker_thread: threading.Thread | None = None
        self._watcher:       SocketWatcher  | None = None
        self._fw_data:       bytes | None          = None
        self._start_time:    float                 = 0.0
        self._start_offset: int                   = 0
        self._last_sent:    int                   = 0   # last confirmed offset
        self._total_size:   int                   = 0   # declared transfer size
        self._sock:         socket.socket | None  = None
        self._is_connected:      bool           = False
        self._force_restart_var: tk.BooleanVar = tk.BooleanVar(value=False)
        self._phase_var:    tk.StringVar = tk.StringVar(value="● IDLE")
        self._elapsed_var:  tk.StringVar = tk.StringVar(value="")
        self._pkt_var:      tk.StringVar = tk.StringVar(value="Packets: —")
        self._nack_var:     tk.StringVar = tk.StringVar(value="NACKs: —")

        self._build_ui()
        self.columnconfigure(0, weight=1)
        self.rowconfigure(4, weight=1)
        self.minsize(600, 520)
        self._poll_queue()

    # ── UI construction ─────────────────────────────────────────────

    def _build_ui(self) -> None:
        p = self._PAD

        # connection row
        cf = ttk.LabelFrame(self, text=" Connection ", padding=p)
        cf.grid(row=0, column=0, padx=p, pady=(p, 0), sticky="ew")

        ttk.Label(cf, text="IP:").grid(row=0, column=0, sticky="e")
        self._ip_var = tk.StringVar(value="192.168.1.1")
        self._ip_entry = ttk.Entry(cf, textvariable=self._ip_var, width=16)
        self._ip_entry.grid(row=0, column=1, padx=(4, 12))

        ttk.Label(cf, text="Port:").grid(row=0, column=2, sticky="e")
        self._port_var = tk.StringVar(value="80")
        self._port_entry = ttk.Entry(cf, textvariable=self._port_var, width=6)
        self._port_entry.grid(row=0, column=3, padx=(4, 12))

        ttk.Label(cf, text="Key:").grid(row=0, column=4, sticky="e")
        self._key_var = tk.StringVar(value="SMAR")
        self._key_entry = ttk.Entry(cf, textvariable=self._key_var, width=14)
        self._key_entry.grid(row=0, column=5, padx=(4, 12))
        self._create_tooltip(self._key_entry,
            "4 ASCII chars (e.g. SMAR), hex (0x534D4152), or decimal.\n"
            "Must match RFWU shared_key stored in device NVRAM.")

        self._btn_connect = ttk.Button(cf, text="Connect", command=self._connect_tcp)
        self._btn_connect.grid(row=0, column=6, padx=(0, 6))

        self._conn_status_var = tk.StringVar(value="")
        ttk.Label(cf, textvariable=self._conn_status_var,
                  foreground="green", width=18).grid(row=0, column=7, sticky="w")

        # file row
        ff = ttk.LabelFrame(self, text=" Firmware File ", padding=p)
        ff.grid(row=1, column=0, padx=p, pady=(p, 0), sticky="ew")
        ff.columnconfigure(0, weight=1)

        self._file_var = tk.StringVar(value="")
        ttk.Entry(ff, textvariable=self._file_var, width=48, state="readonly").grid(
            row=0, column=0, padx=(0, 6), sticky="ew")
        ttk.Button(ff, text="Browse…", command=self._browse).grid(row=0, column=1)

        self._file_info_var = tk.StringVar(value="No file selected.")
        ttk.Label(ff, textvariable=self._file_info_var,
                  foreground="gray").grid(row=1, column=0, columnspan=2,
                                          sticky="w", pady=(2, 0))

        # action row
        af = ttk.Frame(self, padding=(p, p, p, 0))
        af.grid(row=2, column=0, sticky="ew")

        self._btn_status = ttk.Button(af, text="Check Status",
                                      command=self._check_status, state="disabled")
        self._btn_start  = ttk.Button(af, text="Start / Resume",
                                      command=self._start_transfer, state="disabled")
        self._btn_stop   = ttk.Button(af, text="Stop",
                                      command=self._stop, state="disabled")
        self._btn_abort  = ttk.Button(af, text="Abort",
                                      command=self._abort, state="disabled")
        self._btn_apply  = ttk.Button(af, text="Apply Firmware",
                                      command=self._apply, state="disabled")
        self._btn_reboot = ttk.Button(af, text="Reboot Device",
                                      command=self._reboot, state="disabled")

        self._btn_status.pack(side="left", padx=(0, 4))
        self._btn_start.pack(side="left",  padx=(0, 4))
        self._btn_stop.pack(side="left",   padx=(0, 4))
        self._btn_abort.pack(side="left",  padx=(0, 4))
        ttk.Separator(af, orient="vertical").pack(side="left", fill="y", padx=(8, 8))
        ttk.Checkbutton(af, text="Force restart (no resume)",
                        variable=self._force_restart_var).pack(side="left")
        self._btn_reboot.pack(side="right")
        self._btn_apply.pack(side="right",  padx=(0, 6))

        # progress row
        pf = ttk.LabelFrame(self, text=" Progress ", padding=p)
        pf.grid(row=3, column=0, padx=p, pady=(p, 0), sticky="ew")
        pf.columnconfigure(0, weight=1)

        # Phase indicator
        self._phase_lbl = tk.Label(pf, textvariable=self._phase_var,
                                   font=("Consolas", 10, "bold"),
                                   foreground="#888888", anchor="w")
        self._phase_lbl.grid(row=0, column=0, columnspan=2, sticky="w",
                             pady=(0, 4))

        self._pbar_var = tk.DoubleVar(value=0.0)
        ttk.Progressbar(pf, variable=self._pbar_var,
                        maximum=100.0,
                        mode="determinate").grid(
            row=1, column=0, columnspan=2, sticky="ew")

        self._stat_var = tk.StringVar(value="Ready.")
        ttk.Label(pf, textvariable=self._stat_var).grid(
            row=2, column=0, sticky="w", pady=(4, 0))

        self._eta_var = tk.StringVar(value="")
        ttk.Label(pf, textvariable=self._eta_var,
                  foreground="gray").grid(row=2, column=1, sticky="e", pady=(4, 0))

        # Transfer stats row
        sf = ttk.Frame(pf)
        sf.grid(row=3, column=0, columnspan=2, sticky="w", pady=(6, 0))
        ttk.Label(sf, textvariable=self._elapsed_var,
                  foreground="#555555").pack(side="left", padx=(0, 20))
        ttk.Label(sf, textvariable=self._pkt_var,
                  foreground="#555555").pack(side="left", padx=(0, 20))
        ttk.Label(sf, textvariable=self._nack_var,
                  foreground="#555555").pack(side="left")

        # log row
        lf = ttk.LabelFrame(self, text=" Log ", padding=p)
        lf.grid(row=4, column=0, padx=p, pady=p, sticky="nsew")

        self._log_box = scrolledtext.ScrolledText(
            lf, width=88, height=18, state="disabled",
            font=("Consolas", 9), wrap="word")
        self._log_box.pack(side="top", fill="both", expand=True)
        self._log_box.tag_configure("tx",   foreground="#1a6fb5")
        self._log_box.tag_configure("rx",   foreground="#217a3c")
        self._log_box.tag_configure("err",  foreground="#cc2222",
                                    font=("Consolas", 9, "bold"))
        self._log_box.tag_configure("done", foreground="#217a3c",
                                    font=("Consolas", 9, "bold"))
        self._log_box.tag_configure("warn", foreground="#b05a00")
        self._log_box.tag_configure("info", foreground="#555555")

        ttk.Button(lf, text="Clear Log",
                   command=self._clear_log).pack(side="right", pady=(4, 0))

    # ── Helpers ─────────────────────────────────────────────────────

    @staticmethod
    def _create_tooltip(widget: tk.Widget, text: str) -> None:
        tip: tk.Toplevel | None = None

        def show(event: tk.Event) -> None:  # type: ignore[type-arg]
            nonlocal tip
            tip = tk.Toplevel(widget)
            tip.wm_overrideredirect(True)
            tip.wm_geometry(f"+{event.x_root + 12}+{event.y_root + 12}")
            tk.Label(tip, text=text, justify="left",
                     background="#FFFFDD", relief="solid",
                     borderwidth=1, font=("TkDefaultFont", 8)).pack()

        def hide(_event: tk.Event) -> None:  # type: ignore[type-arg]
            nonlocal tip
            if tip:
                tip.destroy()
                tip = None

        widget.bind("<Enter>", show)
        widget.bind("<Leave>", hide)

    def _log(self, msg: str) -> None:
        now  = time.time()
        ts   = time.strftime("%H:%M:%S", time.localtime(now))
        ms   = int((now % 1) * 1000)
        line = f"[{ts}.{ms:03d}] {msg}\n"
        stripped = msg.lstrip()
        if stripped.startswith("TX"):
            tag = "tx"
        elif stripped.startswith("RX"):
            tag = "rx"
        elif any(w in msg for w in ("ERROR", "Error", "error",
                                    "failed", "Failed",
                                    "rejected", "Rejected")):
            tag = "err"
        elif any(w in msg for w in ("complete", "Complete", "Done!")):
            tag = "done"
        elif any(w in msg for w in ("Abort", "abort", "ABORT", "Aborted")):
            tag = "warn"
        else:
            tag = "info"
        self._log_box.configure(state="normal")
        self._log_box.insert("end", line, tag)
        self._log_box.see("end")
        self._log_box.configure(state="disabled")

    def _clear_log(self) -> None:
        self._log_box.configure(state="normal")
        self._log_box.delete("1.0", "end")
        self._log_box.configure(state="disabled")

    _PHASE_COLORS = {
        "IDLE":    "#888888",
        "HELLO":   "#1a6fb5",
        "DATA":    "#0077cc",
        "RESUMED": "#8b5cf6",
        "FINISH":  "#b05a00",
        "DONE":    "#217a3c",
        "ERROR":   "#cc2222",
        "STOPPED": "#888888",
    }

    def _set_phase(self, phase: str) -> None:
        color = self._PHASE_COLORS.get(phase, "#888888")
        self._phase_var.set(f"● {phase}")
        self._phase_lbl.configure(foreground=color)

    def _reset_stats(self) -> None:
        self._pkt_var.set("Packets: 0")
        self._nack_var.set("NACKs: 0")
        self._elapsed_var.set("Elapsed: 0:00")

    def _set_progress(self, sent: int, total: int) -> None:
        pct = (sent * 100.0 / total) if total > 0 else 0.0
        self._pbar_var.set(pct)
        self._stat_var.set(f"Sent: {sent:,} / {total:,} bytes  ({pct:.1f}%)")
        self.title(f"RFWU — {pct:.0f}%")

        elapsed = time.monotonic() - self._start_time
        e_mins, e_secs = divmod(int(elapsed), 60)
        self._elapsed_var.set(f"Elapsed: {e_mins}:{e_secs:02d}")

        session_bytes = sent - self._start_offset
        if elapsed > 0.5 and session_bytes > 0:
            speed = session_bytes / elapsed       # bytes/s
            remaining_bytes = total - sent
            if speed > 0:
                eta_s = remaining_bytes / speed
                mins, secs = divmod(int(eta_s), 60)
                self._eta_var.set(
                    f"{speed / 1024:.1f} KB/s  ETA: {mins}:{secs:02d}")
            else:
                self._eta_var.set("")
        else:
            self._eta_var.set("")

    def _get_params(self) -> tuple:
        host = self._ip_var.get().strip()
        try:
            port = int(self._port_var.get().strip())
        except ValueError:
            port = 80
        try:
            key = parse_key(self._key_var.get())
        except Exception:
            key = 0x534D4152
        return host, port, key

    def _set_transferring(self, active: bool) -> None:
        on  = "normal"   if active else "disabled"
        off = "disabled" if active else "normal"
        self._btn_stop.configure(state=on)
        self._btn_status.configure(state=off)
        self._btn_start.configure(state=off)
        self._btn_connect.configure(state=off)
        # Abort stays enabled whenever connected (both during and before transfer)
        if not active and self._is_connected:
            self._btn_abort.configure(state="normal")
        else:
            self._btn_abort.configure(state=on)

    def _set_connected(self, is_connected: bool,
                       sock: socket.socket | None = None) -> None:
        self._is_connected = is_connected
        self._sock         = sock
        # Stop any existing watcher before starting a new one
        if self._watcher:
            self._watcher.stop()
            self._watcher = None
        entry_state = "disabled" if is_connected else "normal"
        self._ip_entry.configure(state=entry_state)
        self._port_entry.configure(state=entry_state)
        self._key_entry.configure(state=entry_state)
        if is_connected and sock is not None:
            host = self._ip_var.get().strip()
            port = self._port_var.get().strip()
            self._btn_connect.configure(
                state="normal", text="Disconnect", command=self._disconnect_tcp)
            self._conn_status_var.set(f"Connected: {host}:{port}")
            self._btn_status.configure(state="normal")
            if self._fw_data:
                self._btn_start.configure(state="normal")
            self._btn_abort.configure(state="normal")
            # Start background watcher to detect remote closure while idle
            self._watcher = SocketWatcher(sock, self._q)
            self._watcher.start()
        else:
            self._btn_connect.configure(
                state="normal", text="Connect", command=self._connect_tcp)
            self._conn_status_var.set("")
            self._btn_status.configure(state="disabled")
            self._btn_start.configure(state="disabled")
            self._btn_stop.configure(state="disabled")
            self._btn_reboot.configure(state="disabled")
            self._btn_apply.configure(state="disabled")
            self._btn_abort.configure(state="disabled")
            self._set_phase("IDLE")
            self.title("Smart Breaker — RFWU Firmware Update")

    def _connect_tcp(self) -> None:
        self._btn_connect.configure(state="disabled")
        host, port, _ = self._get_params()
        self._log(f"Connecting to {host}:{port}\u2026")
        w = ConnectWorker(host, port, self._q)
        threading.Thread(target=w.run, daemon=True).start()

    def _disconnect_tcp(self) -> None:
        if self._watcher:
            self._watcher.stop()
            self._watcher = None
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
        self._set_connected(False)
        self._log("Disconnected.")

    # ── Actions ─────────────────────────────────────────────────────

    def _browse(self) -> None:
        path = filedialog.askopenfilename(
            title="Select firmware file",
            filetypes=[("Firmware / EFW", "*.bin *.efile"), ("All files", "*.*")])
        if not path:
            return
        self._file_var.set(path)
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as fh:
                self._fw_data = fh.read()
            self._file_info_var.set(
                f"{os.path.basename(path)}  —  {size:,} bytes  "
                f"({size / 1024:.1f} KB)")
            if self._is_connected:
                self._btn_start.configure(state="normal")
            self._log(f"Loaded: {path}  ({size:,} bytes)")
        except OSError as exc:
            self._log(f"File error: {exc}")
            self._fw_data = None
            self._btn_start.configure(state="disabled")

    def _start_watcher(self) -> None:
        """Start the idle watcher if not already running."""
        if self._sock and self._is_connected and not self._watcher:
            self._watcher = SocketWatcher(self._sock, self._q)
            self._watcher.start()

    def _stop_watcher(self) -> None:
        """Stop the idle watcher and wait for it to exit."""
        if self._watcher:
            self._watcher.stop()
            self._watcher = None

    @staticmethod
    def _drain_socket(sock: socket.socket) -> None:
        """Discard any leftover bytes sitting in the socket receive buffer.
        Called after one-shot commands to prevent the watcher from seeing
        delayed MCU responses as unexpected data."""
        import select as _select
        try:
            sock.settimeout(0)
            while True:
                chunk = sock.recv(256)
                if not chunk:
                    break
        except (BlockingIOError, OSError):
            pass
        finally:
            sock.settimeout(SOCKET_TIMEOUT)

    def _join_worker(self) -> None:
        """Signal the worker to stop and wait for its thread to exit.
        Sets a short socket timeout first so any blocking recv() unblocks
        quickly without closing the connection.  Restores the original
        timeout afterward.
        """
        if self._worker:
            self._worker.stop()
        if self._worker_thread and self._worker_thread.is_alive():
            # Temporarily shorten socket timeout to interrupt blocked recv
            if self._sock:
                try:
                    self._sock.settimeout(0.5)
                except OSError:
                    pass
            self._worker_thread.join(timeout=3.0)
            if self._sock:
                try:
                    self._sock.settimeout(SOCKET_TIMEOUT)
                except OSError:
                    pass
        self._worker        = None
        self._worker_thread = None

    def _check_status(self) -> None:
        if self._sock is None:
            return
        # Stop the watcher first and wait for it to fully exit
        # so it cannot consume the first byte of the STATUS response
        self._stop_watcher()
        self._log("Querying status…")
        w = StatusWorker(self._sock, self._q)
        threading.Thread(target=w.run, daemon=True).start()

    def _start_transfer(self) -> None:
        if not self._fw_data or self._sock is None:
            self._log("No firmware file loaded or not connected.")
            return
        # Stop idle watcher — socket will be used exclusively by TransferWorker
        self._stop_watcher()
        _, _, key = self._get_params()
        self._total_size  = len(self._fw_data)
        self._last_sent   = 0
        self._start_time   = time.monotonic()
        self._start_offset = 0
        self._btn_reboot.configure(state="disabled")
        self._reset_stats()
        self._set_transferring(True)
        self._worker = TransferWorker(self._sock, self._fw_data, key, self._q,
                                      self._force_restart_var.get())
        self._worker_thread = threading.Thread(target=self._worker.run, daemon=True)
        self._worker_thread.start()

    def _stop(self) -> None:
        """Stop the running transfer without clearing the MCU session or disconnecting."""
        self._join_worker()   # wait for current recv() to complete before releasing socket
        # Restore button states directly
        self._btn_stop.configure(state="disabled")
        self._btn_abort.configure(state="normal" if self._is_connected else "disabled")
        self._btn_status.configure(state="normal" if self._is_connected else "disabled")
        self._btn_connect.configure(state="normal")
        if self._fw_data and self._is_connected:
            self._btn_start.configure(state="normal")
        self._set_phase("STOPPED")
        self.title("Smart Breaker \u2014 RFWU Firmware Update")
        self._start_watcher()

    def _abort(self) -> None:
        """Send CMD_ABORT: clears MCU NVRAM session. Connection stays open."""
        from tkinter import messagebox
        if not messagebox.askyesno(
                "Abort Transfer",
                "This will clear the active session on the device.\n"
                "Progress will be lost and the next transfer will start from the beginning.\n\n"
                "Are you sure?",
                icon="warning"):
            return
        # Stop watcher (join) and worker (join) before touching the socket
        self._stop_watcher()
        self._join_worker()   # wait for worker's current recv() to finish
        self._set_transferring(False)
        # Send CMD_ABORT — stay connected
        if self._sock:
            try:
                self._sock.sendall(build_packet(CMD_ABORT))
                recv_packet(self._sock)   # consume ACK/NACK cleanly
            except Exception:
                pass
        self._log("Abort sent \u2014 MCU session cleared.")
        # Drain any leftover bytes (e.g. delayed ACK) before restarting watcher
        if self._sock:
            self._drain_socket(self._sock)
        # Restore idle-connected UI state and restart watcher
        if self._is_connected:
            self._btn_status.configure(state="normal")
            self._btn_abort.configure(state="normal")
            if self._fw_data:
                self._btn_start.configure(state="normal")
            self._start_watcher()

    def _reboot(self) -> None:
        if self._sock is None:
            return
        self._stop_watcher()
        self._log("Sending reboot command…")
        w = RebootWorker(self._sock, self._q)
        threading.Thread(target=w.run, daemon=True).start()

    def _apply(self) -> None:
        if self._sock is None:
            return
        from tkinter import messagebox
        if not messagebox.askyesno(
                "Apply Firmware",
                "Device will reboot into bootloader update mode and apply the downloaded firmware.\n\n"
                "Are you sure?",
                icon="warning"):
            return
        self._stop_watcher()
        self._btn_apply.configure(state="disabled")
        self._log("TX  APPLY")
        w = ApplyWorker(self._sock, self._q)
        threading.Thread(target=w.run, daemon=True).start()

    # ── Queue polling ────────────────────────────────────────────────

    def _poll_queue(self) -> None:
        try:
            while True:
                item = self._q.get_nowait()
                kind = item[0]

                if kind == "log":
                    self._log(item[1])

                elif kind == "phase":
                    self._set_phase(item[1])

                elif kind == "pkt_stats":
                    _, tx, nack = item
                    self._pkt_var.set(f"Packets: {tx:,}")
                    self._nack_var.set(f"NACKs: {nack}")

                elif kind == "progress":
                    self._last_sent = item[1]
                    self._set_progress(item[1], item[2])

                elif kind == "start_offset":
                    self._start_offset = item[1]
                    # Reset timer so ETA is relative to this session only
                    self._start_time = time.monotonic()

                elif kind == "done":
                    self._set_transferring(False)
                    self._btn_reboot.configure(state="normal")
                    self._btn_apply.configure(state="normal")
                    self._eta_var.set("Done!")
                    self._worker = None
                    self.title("Smart Breaker — RFWU Firmware Update")

                elif kind == "error":
                    self._log(f"ERROR: {item[1]}")
                    self._set_transferring(False)
                    self.title("Smart Breaker — RFWU Firmware Update")
                    if self._fw_data and self._is_connected:
                        self._btn_start.configure(state="normal")
                    self._worker = None

                elif kind == "status_result":
                    _, active, whead, nv_total, nv_rx, nv_hash = item
                    has_nv = nv_total > 0
                    sep    = "─" * 54
                    lines  = [sep, "  Device Status"]
                    lines.append(
                        f"  Transfer active : {'Yes  (offset {whead:,} bytes)' if active else 'No'}")
                    if has_nv:
                        pct = (nv_rx * 100 // nv_total) if nv_total > 0 else 0
                        lines.append( "  NVRAM session   : Yes")
                        lines.append(f"  File size       : {nv_total:,} bytes  "
                                     f"({nv_total / 1024:.1f} KB)")
                        lines.append(f"  Saved progress  : {nv_rx:,} bytes  ({pct}%)")
                        lines.append(f"  File hash       : 0x{nv_hash:08X}")
                        if self._fw_data:
                            loaded_hash = compute_file_hash(self._fw_data)
                            loaded_size = len(self._fw_data)
                            size_ok = loaded_size == nv_total
                            hash_ok = loaded_hash == nv_hash
                            if size_ok and hash_ok:
                                lines.append("  Loaded file     : MATCH — safe to resume")
                            else:
                                lines.append("  Loaded file     : MISMATCH — wrong file!")
                                if not size_ok:
                                    lines.append(
                                        f"    Size : loaded={loaded_size:,}  "
                                        f"device={nv_total:,}")
                                if not hash_ok:
                                    lines.append(
                                        f"    Hash : loaded=0x{loaded_hash:08X}  "
                                        f"device=0x{nv_hash:08X}")
                        else:
                            lines.append("  Loaded file     : (no file loaded)")
                    else:
                        lines.append("  NVRAM session   : No saved session")
                    lines.append(sep)
                    for line in lines:
                        self._log(line)
                    # Drain any leftover bytes before restarting watcher
                    if self._sock:
                        self._drain_socket(self._sock)
                    # Restart the idle watcher now that the socket is free again
                    self._start_watcher()

                elif kind == "connected":
                    sock = item[1]
                    self._log("Connected.")
                    self._set_connected(True, sock)

                elif kind == "connect_error":
                    self._log(f"Connection failed: {item[1]}")
                    self._btn_connect.configure(state="normal")

                elif kind == "remote_disconnect":
                    reason = item[1] if len(item) > 1 else "unknown"
                    was_transferring = self._worker is not None
                    if self._worker:
                        self._worker.stop()
                        self._worker = None
                    self._worker_thread = None
                    if self._watcher:
                        self._watcher.stop()
                        self._watcher = None
                    self._set_transferring(False)
                    self._set_phase("IDLE")
                    self.title("Smart Breaker — RFWU Firmware Update")
                    if self._sock:
                        try:
                            self._sock.close()
                        except OSError:
                            pass
                    self._set_connected(False)
                    self._log(f"────────────────────────────────────────────")
                    self._log(f"  Connection lost: {reason}")
                    if was_transferring and self._total_size > 0:
                        sent      = self._last_sent
                        remaining = self._total_size - sent
                        pct       = sent * 100.0 / self._total_size
                        elapsed   = time.monotonic() - self._start_time
                        e_m, e_s  = divmod(int(elapsed), 60)
                        self._log(f"  Total size      : {self._total_size:,} bytes")
                        self._log(f"  Sent            : {sent:,} bytes  ({pct:.1f}%)")
                        self._log(f"  Remaining       : {remaining:,} bytes")
                        self._log(f"  Elapsed         : {e_m}:{e_s:02d}")
                        self._log(f"  Note            : MCU session preserved — "
                                  f"reconnect to resume")
                    self._log(f"────────────────────────────────────────────")

                elif kind == "disconnected":
                    was_transferring = self._worker is not None
                    if self._worker:
                        self._worker.stop()
                        self._worker = None
                    self._worker_thread = None
                    if self._sock:
                        try:
                            self._sock.close()
                        except OSError:
                            pass
                    self._set_connected(False)
                    self._log("────────────────────────────────────────────")
                    self._log("  Connection closed by remote.")
                    if was_transferring and self._total_size > 0:
                        sent      = self._last_sent
                        remaining = self._total_size - sent
                        pct       = sent * 100.0 / self._total_size
                        elapsed   = time.monotonic() - self._start_time
                        e_m, e_s  = divmod(int(elapsed), 60)
                        self._log(f"  Total size      : {self._total_size:,} bytes")
                        self._log(f"  Sent            : {sent:,} bytes  ({pct:.1f}%)")
                        self._log(f"  Remaining       : {remaining:,} bytes")
                        self._log(f"  Elapsed         : {e_m}:{e_s:02d}")
                        self._log(f"  Note            : MCU session preserved — "
                                  f"reconnect to resume")
                    self._log("────────────────────────────────────────────")
                    self._worker = None

        except queue.Empty:
            pass

        self.after(100, self._poll_queue)


# ── Entry point ──────────────────────────────────────────────────────

if __name__ == "__main__":
    app = App()
    app.mainloop()
