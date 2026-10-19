"""Socket-level master for negative commands and deliberately withheld ACKs.

The positive interoperability suite keeps c104. This small synchronous
driver permits the malformed/unsupported commands c104 will not generate.
All wire evidence uses the existing APDU parser and recorder.
"""
import socket
import time

import apdu


class WireMaster:
    def __init__(self, host, port, ca, evidence, auto_ack=True):
        self.host, self.port, self.ca = host, port, ca
        self.evidence = evidence
        self.auto_ack = auto_ack
        self.frames = []
        self.sock = None
        self.buffer = bytearray()
        self.send_sn = 0
        self.receive_sn = 0

    @property
    def is_connected(self):
        return self.sock is not None

    def _record(self, direction, raw):
        parsed = apdu.parse_apdu(raw)
        if parsed is None:
            raise ValueError("Malformed APDU: " + raw.hex())
        self.frames.append((time.monotonic(), direction, raw, parsed))
        if self.evidence is not None:
            self.evidence.write({"dir": direction, "apdu_hex": raw.hex(),
                                 "asdu": parsed.get("asdu"), "frame": parsed})
        return parsed

    def _send(self, raw):
        self.sock.sendall(raw)
        self._record("tx", raw)

    def connect(self, timeout_s=45):
        self.sock = socket.create_connection((self.host, self.port), timeout_s)
        self.buffer.clear()
        self.send_sn = self.receive_sn = 0
        self._send(bytes.fromhex("680407000000"))
        return self.wait(lambda f: f[1] == "rx" and
                         f[3].get("u") == "STARTDT_CON", timeout_s)[0]

    def acknowledge(self):
        control = self.receive_sn << 1
        self._send(bytes((0x68, 4, 1, 0, control & 255, control >> 8)))

    def command(self, type_id, payload, cot=6, ca=None, acknowledge_sn=None):
        common = self.ca if ca is None else ca
        asdu = bytes((type_id, 1, cot, 0, common & 255, common >> 8)) + payload
        nr = self.receive_sn if acknowledge_sn is None else acknowledge_sn
        tx, rx = self.send_sn << 1, nr << 1
        self._send(bytes((0x68, len(asdu) + 4, tx & 255, tx >> 8,
                          rx & 255, rx >> 8)) + asdu)
        self.send_sn = (self.send_sn + 1) & 0x7fff

    def interrogate(self, qoi):
        self.command(100, bytes((0, 0, 0, qoi)))
        return True

    def _receive(self, timeout):
        self.sock.settimeout(timeout)
        try:
            chunk = self.sock.recv(4096)
        except socket.timeout:
            return
        if not chunk:
            self.disconnect()
            return
        self.buffer.extend(chunk)
        while len(self.buffer) >= 2:
            if self.buffer[0] != 0x68 or not 4 <= self.buffer[1] <= 253:
                raise ValueError("Invalid APCI prefix")
            length = self.buffer[1] + 2
            if len(self.buffer) < length:
                break
            raw = bytes(self.buffer[:length])
            del self.buffer[:length]
            frame = self._record("rx", raw)
            if frame["kind"] == "I":
                if frame["vs"] != self.receive_sn:
                    raise ValueError("Unexpected DUT send sequence")
                self.receive_sn = (self.receive_sn + 1) & 0x7fff
                if self.auto_ack:
                    self.acknowledge()
            elif frame.get("u") == "TESTFR_ACT":
                self._send(bytes.fromhex("680483000000"))

    def wait(self, predicate, timeout_s, t0=None):
        start = time.monotonic() if t0 is None else t0
        deadline = time.monotonic() + timeout_s
        while True:
            for frame in self.frames:
                if frame[0] >= start and predicate(frame):
                    return True, frame
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.is_connected:
                return False, None
            self._receive(min(remaining, 1.0))

    def since(self, t0):
        return [frame for frame in self.frames if frame[0] >= t0]

    def disconnect(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def stop(self):
        self.disconnect()
