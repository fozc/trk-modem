"""SCP v1.0 wire codec for the RF hub (MH) simulator.

Mirrors Application/libscp (scp.c + cobs.c) byte for byte:
  - CRC-16/CCITT-FALSE over DST..DATA, little-endian on the wire.
  - COBS stuffing with 0x00 SOF/EOF delimiters.
  - Header DST,SRC,TYPE,CMD,SEQ,LEN,~LEN + DATA + CRC (max data 244 B).
  - Parser state machine with 100 ms inter-byte gap discard and the
    same address/length/CRC validation order as libscp.

Self-tested against the BOLATeX Teslim4 R1 capture CSVs (hat_hex bytes).
"""

import binascii
import time
from dataclasses import dataclass, field

ADDR_BROADCAST = 0x00
ADDR_HUB = 0x01
ADDR_RTU = 0x02

TYPE_GET = 0x01
TYPE_SET = 0x02
TYPE_ACK = 0x03
TYPE_ERROR = 0x04
TYPE_PING = 0x05

HEADER_SIZE = 7
CRC_SIZE = 2
OVERHEAD_SIZE = HEADER_SIZE + CRC_SIZE
MAX_DATA_SIZE = 244
PACKET_MAX_SIZE = OVERHEAD_SIZE + MAX_DATA_SIZE
FRAME_MAX_SIZE = PACKET_MAX_SIZE + 2
DELIMITER = 0x00

ERR_UNKNOWN_CMD = 0x01
ERR_INVALID_PARAM = 0x02
ERR_BUSY = 0x03
ERR_NOT_SUPPORTED = 0x04
ERR_NOT_AVAILABLE = 0x05
ERR_RECORD_INVALID = 0x06

CMD_NAMES = {
    0x00: "PING",
    0x01: "GET_STATUS",
    0x02: "GET_FRAM_STATS",
    0x03: "SET_CONFIG",
    0x04: "INVENTORY_SET",
    0x05: "INVENTORY_END",
    0x06: "INVENTORY_UPDATE",
    0x07: "TIME_SYNC",
    0x10: "TRIP_NOTIFY",
    0x11: "LIVE_DATA",
    0x12: "ANOMALY_REPORT",
    0x13: "BOOT_NOTIFY",
    0x14: "DISCOVERY_REPORT",
    0x20: "CFG_READ_ALL",
    0x21: "CFG_STATUS_NOTIFY",
    0x22: "CFG_WRITE",
    0x24: "CFG_COMMIT",
    0x26: "CFG_ABORT",
    0x28: "CFG_STATUS_GET",
    0x2A: "EPOCH_REFRESH",
    0x40: "LOG_READ_HEAD",
    0x42: "LOG_READ_RECORD",
    0x44: "LOG_READ_RANGE",
    0x46: "LOG_CONSUME_TO",
    0x47: "LOG_AVAILABLE",
    0xE1: "PWR_SUMMARY",
    0xE3: "PWR_ALARM",
    0xE5: "PWR_CFG2",
    0xE6: "PWR_COMMAND",
    0xE7: "PWR_RESULT",
    0xE8: "PWR_TELEMETRY",
}

TYPE_NAMES = {
    TYPE_GET: "GET",
    TYPE_SET: "SET",
    TYPE_ACK: "ACK",
    TYPE_ERROR: "ERROR",
    TYPE_PING: "PING",
}


class ScpFrameError(ValueError):
    """Raised when a logical packet fails structural validation."""


@dataclass
class ScpPacket:
    dst: int
    src: int
    type: int
    cmd: int
    seq: int
    data: bytes = b""

    def name(self):
        return CMD_NAMES.get(self.cmd, "0x%02X" % self.cmd)

    def type_name(self):
        return TYPE_NAMES.get(self.type, "0x%02X" % self.type)


def crc16_ccitt_false(data):
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection)."""
    return binascii.crc_hqx(bytes(data), 0xFFFF)


def cobs_encode(data):
    """Standard COBS stuffing; output never contains 0x00."""
    out = bytearray(1)  # placeholder for the leading code byte
    code_idx = 0
    code = 1
    for byte in bytes(data):
        if byte == 0x00:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)  # placeholder for the next code
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code_idx = len(out)
                out.append(0)  # placeholder for the next code
                code = 1
    out[code_idx] = code
    return bytes(out)


def cobs_decode(data):
    """Inverse of cobs_encode; raises ScpFrameError on malformed input."""
    data = bytes(data)
    out = bytearray()
    read_idx = 0
    while read_idx < len(data):
        code = data[read_idx]
        read_idx += 1
        if code == 0x00:
            raise ScpFrameError("zero code byte in COBS block")
        for _ in range(1, code):
            if read_idx >= len(data):
                raise ScpFrameError("truncated COBS block")
            value = data[read_idx]
            read_idx += 1
            if value == 0x00:
                raise ScpFrameError("zero data byte in COBS block")
            out.append(value)
        if code != 0xFF and read_idx < len(data):
            out.append(0x00)
    return bytes(out)


def encode_packet(pkt):
    """ScpPacket -> logical bytes (header + data + CRC-LE)."""
    if len(pkt.data) > MAX_DATA_SIZE:
        raise ScpFrameError("data exceeds %d bytes" % MAX_DATA_SIZE)
    logical = bytearray()
    logical += bytes([pkt.dst, pkt.src, pkt.type, pkt.cmd, pkt.seq,
                      len(pkt.data), (~len(pkt.data)) & 0xFF])
    logical += pkt.data
    crc = crc16_ccitt_false(logical)
    logical += crc.to_bytes(2, "little")
    return bytes(logical)


def encode_frame(pkt):
    """ScpPacket -> wire bytes (0x00 + COBS(logical) + 0x00)."""
    logical = encode_packet(pkt)
    frame = b"\x00" + cobs_encode(logical) + b"\x00"
    if len(frame) > FRAME_MAX_SIZE:
        raise ScpFrameError("frame exceeds %d bytes" % FRAME_MAX_SIZE)
    return frame


def decode_logical(logical, my_address):
    """Logical bytes -> ScpPacket; validates like libscp decode_packet.

    Returns None when the frame is validly formed but not addressed to us
    (wrong DST) so the caller can distinguish noise from misaddressing.
    Raises ScpFrameError for structural/CRC faults.
    """
    logical = bytes(logical)
    if len(logical) < OVERHEAD_SIZE:
        raise ScpFrameError("shorter than header+CRC")
    dst = logical[0]
    if dst not in (my_address, ADDR_BROADCAST):
        return None
    src = logical[1]
    typ = logical[2]
    cmd = logical[3]
    seq = logical[4]
    length = logical[5]
    nlen = logical[6]
    if (length + nlen) != 0xFF:
        raise ScpFrameError("LEN/~LEN mismatch")
    if len(logical) != length + OVERHEAD_SIZE:
        raise ScpFrameError("length mismatch")
    if length > MAX_DATA_SIZE:
        raise ScpFrameError("LEN too large")
    computed = crc16_ccitt_false(logical[:HEADER_SIZE + length])
    if computed != int.from_bytes(logical[-2:], "little"):
        raise ScpFrameError("CRC mismatch")
    if src == ADDR_BROADCAST:
        raise ScpFrameError("broadcast used as source")
    return ScpPacket(dst, src, typ, cmd, seq, logical[7:-2])


def frame_from_wire_chunk(chunk):
    """One wire chunk between delimiters -> logical bytes (COBS decode)."""
    return cobs_decode(chunk)


@dataclass
class ParsedFrame:
    pkt: ScpPacket
    raw_chunk: bytes
    t_ms: int


class ScpParser:
    """Byte-stream parser with libscp semantics (SOF/COLLECT/READY).

    feed() must receive wall-clock timestamps; a >=gap_timeout_ms pause
    inside a partial frame discards it, exactly like the DUT's parser.
    """

    WAIT_SOF = 0
    COLLECT = 1
    READY = 2

    def __init__(self, my_address, on_frame, gap_timeout_ms=100,
                 clock=None):
        self.my_address = my_address
        self.on_frame = on_frame
        self.gap_timeout_ms = gap_timeout_ms
        self.clock = clock or (lambda: int(time.monotonic() * 1000))
        self.state = self.WAIT_SOF
        self.buf = bytearray()
        self.last_rx_ms = self.clock()
        self.discarded_chunks = []  # noise/garbage observation log

    def feed(self, byte, now_ms=None):
        now = self.clock() if now_ms is None else now_ms
        if self.state == self.COLLECT and self.buf:
            if now - self.last_rx_ms >= self.gap_timeout_ms:
                self.discarded_chunks.append((now, bytes(self.buf)))
                self.buf = bytearray()
                self.state = self.WAIT_SOF
        self.last_rx_ms = now

        if self.state == self.WAIT_SOF:
            if byte == DELIMITER:
                self.state = self.COLLECT
                self.buf = bytearray()
        elif self.state == self.COLLECT:
            if byte == DELIMITER:
                if self.buf:
                    self._finish_frame(now)
                # an empty or invalid chunk: this 0x00 is the next SOF
                if self.state == self.READY:
                    return
                self.buf = bytearray()
            else:
                if len(self.buf) < FRAME_MAX_SIZE:
                    self.buf.append(byte)
                else:
                    self.buf = bytearray()  # overflow; resync on next 0x00
        # READY: caller must call packet_done() before further feeding

    def feed_bytes(self, data, now_ms=None):
        base = self.clock() if now_ms is None else now_ms
        for i, byte in enumerate(bytes(data)):
            self.feed(byte, now_ms=base + i)

    def _finish_frame(self, now):
        raw = bytes(self.buf)
        try:
            logical = cobs_decode(raw)
            pkt = decode_logical(logical, self.my_address)
        except ScpFrameError:
            self.discarded_chunks.append((now, raw))
            return
        if pkt is None:
            self.discarded_chunks.append((now, raw))
            return
        self.state = self.READY
        self.on_frame(ParsedFrame(pkt, raw, now))

    def packet_done(self):
        if self.state == self.READY:
            self.state = self.WAIT_SOF
            self.buf = bytearray()
