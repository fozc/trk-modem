#!/usr/bin/env python3
"""c104 master wrapper: Init.NONE, raw-APDU evidence, controlled commands.

Design constraints (suite plan, approved 2026-10-09):
- connection opens with Init.NONE so c104 sends no automatic GI or clock
  sync; every application command is issued by a test case;
- raw RX/TX callbacks are registered BEFORE connect and record every
  APDU (U and S frames included) with monotonic time into the frame
  list, which is the single source for all assertions and JSONL;
- master keep-alive (t3) is raised above the device t3 for the idle
  case so the device always produces the first TESTFR.
"""

import threading
import time

import c104

import apdu
from apdu import frame_is_i_asdu, frame_is_u  # noqa: F401 (re-export)


class Master104:
    """One c104 Connection bound to the evidence recorder."""

    def __init__(self, host, port, common_address, evidence,
                 keep_alive_s=None):
        self.host = host
        self.port = port
        self.ca = common_address
        self.evidence = evidence
        self.frames = []            # (t_mono, dir, raw, parsed)
        self._lock = threading.Condition()
        self.client = c104.Client()
        self.conn = self.client.add_connection(
            host, port, init=c104.Init.NONE)
        if keep_alive_s is not None:
            # Must be set before connect; c104 defaults t3=20 s which is
            # below the device default t3=60 s.
            self.conn.protocol_parameters.keep_alive_interval = \
                int(keep_alive_s)
        self._station = self.conn.add_station(common_address)
        self.conn.on_receive_raw(self._on_receive_raw)
        self.conn.on_send_raw(self._on_send_raw)
        self.conn.on_state_change(self._on_state_change)
        self.conn.on_unexpected_message(self._on_unexpected)

    # -- callbacks (c104 enforces exact annotated signatures) ----------

    def _on_receive_raw(self, connection: c104.Connection,
                        data: bytes) -> None:
        self._record("rx", data)

    def _on_send_raw(self, connection: c104.Connection,
                     data: bytes) -> None:
        self._record("tx", data)

    def _on_state_change(self, connection: c104.Connection,
                         state: c104.ConnectionState) -> None:
        with self._lock:
            self.frames.append((time.monotonic(), "state", bytes(),
                                {"state": str(state).rsplit(".", 1)[-1]}))
            self._lock.notify_all()
        if self.evidence is not None:
            self.evidence.write({"dir": "state", "apdu_hex": "",
                                 "asdu": None, "frame":
                                 {"state": str(state).rsplit(".", 1)[-1]}})

    def _on_unexpected(self, connection: c104.Connection,
                       message: c104.IncomingMessage,
                       cause: c104.Umc) -> None:
        with self._lock:
            self.frames.append((time.monotonic(), "unexpected",
                                bytes(), {"unexpected": str(cause)}))
            self._lock.notify_all()
        if self.evidence is not None:
            self.evidence.write({"dir": "unexpected",
                                 "apdu_hex": "", "asdu": None,
                                 "frame": {"unexpected": str(cause)}})

    def _record(self, direction, data):
        parsed = apdu.parse_apdu(data)
        with self._lock:
            self.frames.append((time.monotonic(), direction, data,
                                parsed))
            self._lock.notify_all()
        if self.evidence is not None:
            self.evidence.write({
                "dir": direction, "apdu_hex": data.hex(),
                "asdu": (parsed or {}).get("asdu"), "frame": parsed})

    # -- lifecycle -------------------------------------------------------

    def start(self):
        self.client.start()

    def connect(self, timeout_s=20.0):
        """Connect and wait for STARTDT_CON (c104 sends STARTDT_ACT)."""
        self.conn.connect()
        ok, _ = self.wait(
            lambda f: f[1] == "rx" and f[3] and f[3].get("u") ==
            "STARTDT_CON", timeout_s)
        return ok

    def disconnect(self):
        try:
            self.conn.disconnect()
        except Exception:            # noqa: BLE001 (best-effort teardown)
            pass

    def stop(self):
        self.disconnect()
        try:
            self.client.stop()
        except Exception:            # noqa: BLE001
            pass

    @property
    def is_connected(self):
        return bool(self.conn.is_connected)

    @property
    def confirm_interval_s(self):
        """Actual master t2; replay may wait this long for each ACK."""
        return float(self.conn.protocol_parameters.confirm_interval)

    # -- commands --------------------------------------------------------

    def interrogate(self, qoi_value):
        """Send C_IC_NA_1 with the given QOI; own waits drive assertions."""
        return self.conn.interrogation(
            self.ca, qualifier=c104.Qoi(qoi_value),
            wait_for_response=False)

    def clock_sync(self):
        return self.conn.clock_sync(self.ca, wait_for_response=False)

    # -- frame queries ----------------------------------------------------

    def since(self, t0):
        """Frames (and state events) recorded at or after t0."""
        with self._lock:
            return [frame for frame in self.frames if frame[0] >= t0]

    def wait(self, predicate, timeout_s, t0=None):
        """Wait until a frame satisfies predicate; returns (found, frame).

        predicate receives the (t, dir, raw, parsed) tuple. t0 limits the
        scan to frames recorded at/after t0 (default: call time).
        """
        deadline = time.monotonic() + timeout_s
        start = t0 if t0 is not None else time.monotonic()
        while True:
            with self._lock:
                for frame in self.frames:
                    if frame[0] >= start and predicate(frame):
                        return True, frame
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False, None
                self._lock.wait(remaining)
