"""rf_real_mh parser regression against real MH (022555bf) frames.

Fixtures are RF RX console lines captured on the bench 2026-10-11
(epoch4_rf_rx.log: an `rf epoch 4` window; mh_151712_rf_rx.log: the
m1-m7 run console). They pin the wire format the parser must handle:
libscp 7-byte header DST+SRC+TYPE+CMD+SEQ+LEN+~LEN, CRC-16-LE over
header+body, optional console trailing byte.

Run: python test/system/rf_real_mh/test_parser_selftest.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_smoke import (frame_bytes, live_bodies, log_records,  # noqa: E402
                       parse_scp_frame)

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as handle:
        frames = [f for f in (frame_bytes(line) for line in handle)
                  if f is not None]
    assert frames, "fixture bos: %s" % name
    return frames


def test_every_real_frame_validates():
    frames = load("epoch4_rf_rx.log")
    assert len(frames) == 69
    invalid = [f for f in frames if parse_scp_frame(f) is None]
    assert not invalid, "%d cerceve paket CRC/LEN (~LEN) Dogrulamasini gecemedi" % len(invalid)


def test_live_bodies_read_boot_counters():
    bodies = live_bodies(load("epoch4_rf_rx.log"))
    assert len(bodies) == 42
    per_src = {}
    for body in bodies:
        per_src.setdefault(body[0], set()).add(body[31] | (body[32] << 8))
    # web /monitor/rf gozlemiyle birebir: iki AY, BC 7 ve 20
    assert per_src == {5: {7}, 6: {20}}, per_src


def test_epoch_records_decode_with_own_crc():
    records = log_records(load("epoch4_rf_rx.log"))
    assert [(rec[7], rec[8], rec[9]) for _, rec in records] == [(201, 1, 4)]


def test_renewal_120_per_ay_present():
    records = log_records(load("mh_151712_rf_rx.log"))
    marks = sorted((rec[8], rec[9], rec[10]) for _, rec in records
                   if 120 == rec[7])
    assert marks == [(1, 1, 1), (1, 1, 2)], marks   # fider 1, faz 1 ve 2
    assert any(201 == rec[7] for _, rec in records)


def test_corrupted_frame_rejected():
    frame = load("epoch4_rf_rx.log")[0]
    span_end = 7 + frame[5] + 2          # header + body + packet CRC
    for pos in (0, 3, 5, 6, span_end - 1):
        broken = bytearray(frame)
        broken[pos] ^= 0xFF
        assert parse_scp_frame(bytes(broken)) is None, "pos=%d" % pos
    # CRC'nin otesindeki konsol kuyruk bayti dogrulanmaz, tolere edilir
    trailing = bytearray(frame)
    trailing[-1] ^= 0xFF
    assert parse_scp_frame(bytes(trailing)) is not None


def test_epoch_refresh_ack_body_is_empty():
    frames = load("epoch4_rf_rx.log")
    acks = [parsed for parsed in (parse_scp_frame(f) for f in frames)
            if parsed is not None and 0x2A == parsed[0]]
    assert len(acks) == 1
    assert acks[0][2] == b"", acks[0][2]


def test_every_truncated_packet_is_rejected():
    frame = load("epoch4_rf_rx.log")[0]
    end = 7 + frame[5] + 2
    for length in range(end):
        assert parse_scp_frame(frame[:length]) is None, length


def test_prefix_or_multiple_trailing_bytes_cannot_hide_a_bad_frame():
    frame = load("epoch4_rf_rx.log")[0]
    exact = frame[:7 + frame[5] + 2]
    assert parse_scp_frame(b"\x00" + exact) is None
    assert parse_scp_frame(exact + b"\x00\x00") is None


def test_wrong_message_type_is_not_live_or_event_data():
    frame = bytearray(next(f for f in load("epoch4_rf_rx.log")
                           if f[3] == 0x11))
    frame[2] = 4  # ERROR must never become a LIVE_DATA sample.
    end = 7 + frame[5]
    crc = run_crc(frame[:end])
    frame[end:end + 2] = bytes((crc & 255, crc >> 8))
    assert not live_bodies([bytes(frame)])


def run_crc(data):
    # Independently specified golden polynomial; not the decoder under test.
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def test_every_packet_bit_corruption_is_rejected():
    frames = load("epoch4_rf_rx.log")
    for cmd in (0x2A, 0x11, 0x44):
        frame = next(f for f in frames if f[3] == cmd)
        exact = frame[:7 + frame[5] + 2]
        for byte in range(len(exact)):
            for bit in range(8):
                broken = bytearray(exact)
                broken[byte] ^= 1 << bit
                assert parse_scp_frame(bytes(broken)) is None, (cmd, byte, bit)


def test_valid_packet_crc_does_not_hide_corrupt_record_crc():
    frame = bytearray(next(f for f in load("epoch4_rf_rx.log")
                           if f[3] == 0x44))
    frame[7 + 58] ^= 1
    end = 7 + frame[5]
    crc = run_crc(frame[:end])
    frame[end:end + 2] = bytes((crc & 255, crc >> 8))
    assert parse_scp_frame(frame) is not None
    assert log_records([frame]) == []


def make_frame(cmd, body, kind=3, dst=2, src=1):
    header = bytes((dst, src, kind, cmd, 7, len(body), len(body) ^ 255))
    raw = header + body
    crc = run_crc(raw)
    return raw + bytes((crc & 255, crc >> 8))


def test_parser_obeys_libscp_244_byte_payload_capacity():
    for length in range(256):
        raw = make_frame(0x44, bytes(length))
        parsed = parse_scp_frame(raw)
        if length <= 244:
            assert parsed is not None and len(parsed[2]) == length
        else:
            assert parsed is None, length


def test_partial_record_batch_and_wrong_type_cannot_be_event_records():
    record = log_records(load("epoch4_rf_rx.log"))[0][1]
    for frame in (make_frame(0x44, record + b"\0"),
                  make_frame(0x42, record + record),
                  make_frame(0x44, record, kind=2)):
        assert parse_scp_frame(frame) is not None
        assert log_records([frame]) == []


def test_foreign_address_or_unknown_type_is_rejected_even_with_valid_crc():
    for frame in (make_frame(0x2A, b"", dst=0),
                  make_frame(0x2A, b"", src=0),
                  make_frame(0x2A, b"", src=3),
                  make_frame(0x2A, b"", kind=0),
                  make_frame(0x2A, b"", kind=6)):
        assert parse_scp_frame(frame) is None


def main():
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_")]
    for test in tests:
        test()
        print("PASS: %s" % test.__name__)
    print("%d/%d PASS" % (len(tests), len(tests)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
