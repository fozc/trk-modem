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
