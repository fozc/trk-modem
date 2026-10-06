#!/usr/bin/env python3
"""Donanimsiz (no-hardware) self-tests for the tools/rf-hil simulator.

Coverage:
  1. Codec: CRC spec vector, COBS round-trip, parser state machine
     (noise tolerance, 100 ms gap discard, misaddress/CRC/Len faults).
  2. Golden frames: every hat_hex wire frame of the 14 BOLATeX Teslim4
     R1 capture CSVs decodes and re-encodes byte-exactly.
  3. cfg_crc: the firmware slice (block[3:57]) reproduces the APPLIED
     cfg_crc reported in AY_06.
  4. Hub model: bring-up/inventory semantics, SEQ/idempotency rules,
     event-ring math (wrap/overflow/bad slot/busy), config-group
     timeline incl. FAILED reasons, PWRB CFG2 GEN machine, 0xE6/0xE7
     lifecycle, proactive generators, fault engine wire transforms.

Run:  python test/system/rf_hil/test_sim_selftest.py
Exit: 0 pass, 1 failure count reported.
"""

import csv
import struct
import sys
import time as _time
import unittest
from pathlib import Path

TEST_ROOT = Path(__file__).resolve().parents[2]        # test/
REPO_ROOT = TEST_ROOT.parent                            # repo root
SIM_ROOT = REPO_ROOT / "tools" / "rf-hil"
CAPTURE_ROOT = (REPO_ROOT / "doc" / "BOLATeX_Teslim4_R1_RTU_Arayuzu_MD"
                / "Ornek_SCP_Akislari")

sys.path.insert(0, str(SIM_ROOT))

import sim.scp_codec as sc              # noqa: E402
import sim.cp56 as cp56                 # noqa: E402
import sim.hub_model as hm              # noqa: E402
import sim.faults as faults_mod         # noqa: E402


def load_capture_rows():
    """(filename, row_number, csv row dict) for all 14 captures."""
    files = sorted(CAPTURE_ROOT.glob("AY_*.csv"))
    files += sorted(CAPTURE_ROOT.glob("PWRB_*.csv"))
    if len(files) != 14:
        raise unittest.SkipTest("capture CSVs not found at %s" % CAPTURE_ROOT)
    rows = []
    for path in files:
        with path.open(encoding="utf-8-sig", newline="") as source:
            for row_number, row in enumerate(
                    csv.DictReader(source, delimiter=";"), 2):
                rows.append((path.name, row_number, row))
    return rows


def row_to_packet(row):
    logical = bytes.fromhex(row["mantiksal_hex"])
    pkt = sc.decode_logical(logical, sc.ADDR_HUB)
    if pkt is None:
        pkt = sc.decode_logical(logical, sc.ADDR_RTU)
    assert pkt is not None, "logical frame decodes for hub or rtu"
    return pkt


def make_eui(line, phase):
    return bytes([0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9,
                  0xF0 | (line & 0x0F), (phase & 0x0F) * 16 + 0x0A])


class CodecBasics(unittest.TestCase):

    def test_crc_spec_vector(self):
        # Spec 2.1: 05 01 02 10 01 02 FD AA BB -> 0x5035 (wire 35 50)
        crc = sc.crc16_ccitt_false(bytes.fromhex("050102100102FDAABB"))
        self.assertEqual(crc, 0x5035)

    def test_cobs_roundtrip_random(self):
        import random
        rng = random.Random(20261006)
        for _ in range(200):
            data = bytes(rng.randrange(256)
                         for _ in range(rng.randrange(0, 300)))
            self.assertEqual(sc.cobs_decode(sc.cobs_encode(data)), data)
            self.assertNotIn(0, sc.cobs_encode(data))


class ParserBehaviour(unittest.TestCase):

    def make_parser(self):
        self.now = 1000
        received = []
        parser = sc.ScpParser(
            sc.ADDR_HUB, lambda fr: received.append(fr),
            clock=lambda: self.now)
        return parser, received

    def feed_frame(self, parser, pkt):
        parser.feed_bytes(sc.encode_frame(pkt))
        parser.packet_done()

    def test_valid_frame_delivered(self):
        parser, received = self.make_parser()
        self.feed_frame(parser, sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 7))
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0].pkt.cmd, 0x40)
        self.assertEqual(received[0].pkt.seq, 7)

    def test_text_noise_between_frames_ignored(self):
        parser, received = self.make_parser()
        parser.feed_bytes(b"MH servis konsolu acilis satiri\r\nlog: ok\n")
        self.feed_frame(parser, sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_PING, 0x00, 1))
        parser.feed_bytes(b"daha fazla metin gurultusu ...")
        self.feed_frame(parser, sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x01, 2))
        self.assertEqual([f.pkt.seq for f in received], [1, 2])

    def test_gap_discards_partial_frame(self):
        parser, received = self.make_parser()
        frame = sc.encode_frame(sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 9))
        parser.feed_bytes(frame[:8])
        self.now += 150  # > 100 ms gap
        parser.feed_bytes(frame[8:])
        self.assertEqual(received, [])
        self.assertEqual(len(parser.discarded_chunks), 1)

    def test_short_gap_keeps_frame(self):
        parser, received = self.make_parser()
        frame = sc.encode_frame(sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 9))
        parser.feed_bytes(frame[:8])
        self.now += 40  # < 100 ms gap
        parser.feed_bytes(frame[8:])
        self.assertEqual(len(received), 1)

    def test_bad_crc_dropped(self):
        parser, received = self.make_parser()
        logical = bytearray(sc.encode_packet(sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 5)))
        logical[-1] ^= 0xFF
        parser.feed_bytes(b"\x00" + sc.cobs_encode(bytes(logical)) +
                          b"\x00")
        self.assertEqual(received, [])

    def test_len_mismatch_dropped(self):
        parser, received = self.make_parser()
        logical = bytearray(sc.encode_packet(sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 5)))
        logical[6] ^= 0x04  # break ~LEN
        parser.feed_bytes(b"\x00" + sc.cobs_encode(bytes(logical)) +
                          b"\x00")
        self.assertEqual(received, [])

    def test_wrong_dst_dropped(self):
        parser, received = self.make_parser()
        self.feed_frame(parser, sc.ScpPacket(
            0x03, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 5))
        self.assertEqual(received, [])

    def test_broadcast_dst_accepted(self):
        parser, received = self.make_parser()
        self.feed_frame(parser, sc.ScpPacket(
            sc.ADDR_BROADCAST, sc.ADDR_RTU, sc.TYPE_GET, 0x01, 5))
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0].pkt.dst, sc.ADDR_BROADCAST)

    def test_broadcast_src_rejected(self):
        parser, received = self.make_parser()
        self.feed_frame(parser, sc.ScpPacket(
            sc.ADDR_HUB, sc.ADDR_BROADCAST, sc.TYPE_SET, 0x04, 5, b"\x00"))
        self.assertEqual(received, [])


class GoldenCaptures(unittest.TestCase):
    """Every captured frame: decode CSV logical/wire and re-encode exact."""

    @classmethod
    def setUpClass(cls):
        cls.rows = load_capture_rows()

    def test_decode_reencode_byte_exact(self):
        count = 0
        for filename, row_number, row in self.rows:
            logical = bytes.fromhex(row["mantiksal_hex"])
            wire = bytes.fromhex(row["hat_hex"])
            pkt = row_to_packet(row)
            self.assertEqual(sc.encode_packet(pkt), logical,
                             "%s:%d logical round-trip" % (filename,
                                                           row_number))
            self.assertEqual(sc.encode_frame(pkt), wire,
                             "%s:%d wire round-trip" % (filename, row_number))
            count += 1
        self.assertGreater(count, 100)

    def test_parser_accepts_all_wire_frames(self):
        for filename, row_number, row in self.rows:
            received = []
            listener = (sc.ADDR_RTU if row["yon"].startswith("MH")
                        else sc.ADDR_HUB)
            parser = sc.ScpParser(listener, received.append)
            parser.feed_bytes(bytes.fromhex(row["hat_hex"]))
            self.assertEqual(len(received), 1,
                             "%s:%d" % (filename, row_number))

    def test_cfg_crc_matches_applied_report(self):
        """block[3:57] CRC must equal the APPLIED 0x21 cfg_crc (AY_06)."""
        rows = [(f, n, r) for f, n, r in self.rows
                if f.startswith("AY_06_y")]
        blocks = set()
        applied_crcs = set()
        for filename, row_number, row in rows:
            pkt = row_to_packet(row)
            if pkt.cmd == 0x22 and pkt.type == sc.TYPE_SET:
                self.assertEqual(len(pkt.data), 104)
                blocks.add(bytes(pkt.data[8:104]))
            if pkt.cmd == 0x21 and pkt.type == sc.TYPE_SET:
                if pkt.data[1] == 3:  # APPLIED
                    applied_crcs.add(int.from_bytes(pkt.data[4:6], "little"))
        self.assertEqual(len(blocks), 1, "AY_06 uses one identical block")
        self.assertTrue(applied_crcs, "AY_06 must contain an APPLIED report")
        block = blocks.pop()
        computed = sc.crc16_ccitt_false(block[3:57])
        self.assertIn(computed, applied_crcs,
                      "firmware writable-slice CRC == reported cfg_crc")


class Cp56Helpers(unittest.TestCase):

    def test_roundtrip(self):
        st = _time.localtime(1762306800)
        body = cp56.pack_cp56(st)
        f = cp56.unpack_cp56(body)
        self.assertEqual((f["min"], f["hour"], f["day"], f["month"]),
                         (st.tm_min, st.tm_hour, st.tm_mday, st.tm_mon))
        self.assertFalse(f["invalid"])

    def test_range_rejects(self):
        body = bytearray(cp56.pack_cp56(_time.localtime()))
        body2 = bytearray(body)
        body2[2] &= 0x3F
        body2[5] = 13  # month out of range
        self.assertTrue(cp56.cp56_out_of_range(bytes(body2)))


# ----------------------------------------------------------------------
# Hub model tests (transport-free; packets captured via a send spy)
# ----------------------------------------------------------------------

class HubHarness:
    """Deterministic hub + fake RTU request sender + tick clock."""

    def __init__(self):
        self.now = 100000
        self.sent = []

        def send_fn(pkt, kind):
            self.sent.append((self.now, pkt, kind))

        self.hub = hm.HubModel(send_fn, now_ms=lambda: self.now)

    def advance(self, ms):
        end = self.now + ms
        while self.now < end:
            self.now += 10
            self.hub.tick()

    def request(self, cmd, typ=sc.TYPE_GET, data=b"", seq=1,
                dst=sc.ADDR_HUB, src=sc.ADDR_RTU):
        pkt = sc.ScpPacket(dst, src, typ, cmd, seq, data)
        self.hub.handle_packet(pkt)
        return pkt

    def replies(self):
        return [p for (_, p, k) in self.sent
                if k in ("reply", "reply-replay")]

    def notifies(self, cmd=None):
        return [p for (_, p, k) in self.sent
                if k == "notify" and (cmd is None or p.cmd == cmd)]


def upload_inventory(h, zone=1, lines=(1, 2)):
    """Standard bring-up: TIME_SYNC + 0x04 x N + 0x05."""
    body = cp56.pack_cp56(_time.localtime())
    assert body[2] & 0x80 == 0
    h.request(0x07, sc.TYPE_SET, body, seq=10)
    seq = 11
    for line in lines:
        for phase in (1, 2, 3):
            eui = make_eui(line, phase)
            data = bytes([zone, line, phase]) + eui + bytes([0])
            h.request(0x04, sc.TYPE_SET, data, seq=seq)
            seq += 1
    h.request(0x05, sc.TYPE_SET, b"", seq=seq)


class BringUpTests(unittest.TestCase):

    def test_time_sync_accepts_valid(self):
        h = HubHarness()
        upload_inventory(h)
        self.assertTrue(h.hub.time_valid)
        self.assertTrue(h.hub.loaded)
        self.assertEqual(len(h.hub.inventory), 6)

    def test_time_sync_rejects_iv1(self):
        h = HubHarness()
        body = bytearray(cp56.pack_cp56(_time.localtime()))
        body[2] |= 0x80  # IV = 1 -> ERROR 0x02 (spec 4.4)
        h.request(0x07, sc.TYPE_SET, bytes(body), seq=10)
        reply = h.replies()[-1]
        self.assertEqual(reply.type, sc.TYPE_ERROR)
        self.assertEqual(reply.data[0], 0x02)
        self.assertFalse(h.hub.time_valid)

    def test_time_sync_rejects_out_of_range(self):
        h = HubHarness()
        body = bytearray(cp56.pack_cp56(_time.localtime()))
        body[5] = 13
        h.request(0x07, sc.TYPE_SET, bytes(body), seq=10)
        self.assertEqual(h.replies()[-1].data[0], 0x02)

    def test_boot_backoff_and_stop(self):
        h = HubHarness()
        h.hub.start_boot()
        h.advance(1000)
        self.assertEqual(len(h.notifies(0x13)), 1)
        h.advance(1000)          # repeat at ~2 s
        h.advance(2000)
        self.assertEqual(len(h.notifies(0x13)), 2)
        upload_inventory(h)
        h.advance(70000)
        self.assertEqual(len(h.notifies(0x13)), 2)  # stopped for good

    def test_empty_inventory_keeps_boot(self):
        h = HubHarness()
        h.hub.start_boot()
        h.advance(1000)
        h.request(0x05, sc.TYPE_SET, b"", seq=3)
        ack = h.replies()[-1]
        self.assertEqual(ack.type, sc.TYPE_ACK)
        self.assertFalse(h.hub.loaded)
        h.advance(2000)
        self.assertGreaterEqual(len(h.notifies(0x13)), 2)

    def test_fider0_rejected_on_set(self):
        h = HubHarness()
        upload_inventory(h)
        eui = make_eui(9, 1)
        h.request(0x04, sc.TYPE_SET,
                  bytes([1, 0, 1]) + eui + bytes([0]), seq=50)
        self.assertEqual(h.replies()[-1].data[0], 0x02)

    def test_fider0_deletes_on_update(self):
        h = HubHarness()
        upload_inventory(h)
        eui = make_eui(1, 1)
        self.assertIn(eui.hex(), h.hub.inventory)
        h.request(0x06, sc.TYPE_SET,
                  bytes([1, 0, 1]) + eui + bytes([0]), seq=51)
        self.assertEqual(h.replies()[-1].type, sc.TYPE_ACK)
        self.assertNotIn(eui.hex(), h.hub.inventory)


class IdempotencyTests(unittest.TestCase):

    def test_get_repeat_reprocesses(self):
        h = HubHarness()
        upload_inventory(h)
        h.request(0x01, sc.TYPE_GET, b"", seq=90)
        r1 = [p for p in h.replies() if p.seq == 90]
        self.assertEqual(len(r1), 1)
        h.request(0x01, sc.TYPE_GET, b"", seq=90)  # same SEQ repeat
        r1 = [p for p in h.replies() if p.seq == 90]
        self.assertEqual(len(r1), 2)  # GET repeat: fresh processing

    def test_set_repeat_replays_reply(self):
        h = HubHarness()
        upload_inventory(h)
        eui = make_eui(1, 1)
        data = bytes([1, 1, 1]) + eui + bytes([0])
        h.request(0x06, sc.TYPE_SET, data, seq=91)
        h.request(0x06, sc.TYPE_SET, data, seq=91)  # repeat, no interrupt
        acks = [p for p in h.replies() if p.seq == 91]
        self.assertEqual(len(acks), 2)
        h.request(0x01, sc.TYPE_GET, b"", seq=92)   # intervening request
        h.request(0x06, sc.TYPE_SET, data, seq=91)
        acks = [p for p in h.replies() if p.seq == 91]
        self.assertEqual(len(acks), 3)  # reprocessed after interruption


class EventRingTests(unittest.TestCase):

    def test_pull_loop_math(self):
        h = HubHarness()
        upload_inventory(h)
        h.hub.add_events(7, code=1, line=1)
        self.assertEqual(h.hub.pending_count(), 7)
        h.request(0x40, sc.TYPE_GET, b"", seq=60)
        head = h.replies()[-1]
        self.assertEqual(len(head.data), 10)
        head_i, wrap, total, tail = struct.unpack("<HHIH", head.data)
        self.assertEqual((head_i, wrap, total, tail), (7, 0, 7, 0))
        h.request(0x44, sc.TYPE_GET, struct.pack("<HH", 0, 4), seq=61)
        batch = h.replies()[-1]
        self.assertEqual(len(batch.data), 240)
        h.request(0x46, sc.TYPE_SET, struct.pack("<H", 4), seq=62)
        ack = h.replies()[-1]
        tail, left = struct.unpack("<HH", ack.data)
        self.assertEqual((tail, left), (4, 3))
        h.request(0x46, sc.TYPE_SET, struct.pack("<H", 2), seq=63)
        err = h.replies()[-1]
        self.assertEqual((err.type, err.data[0]), (sc.TYPE_ERROR, 0x02))
        h.request(0x46, sc.TYPE_SET, struct.pack("<H", 8), seq=64)
        err = h.replies()[-1]
        self.assertEqual(err.data[0], 0x02)

    def test_wrap_and_overflow(self):
        h = HubHarness()
        upload_inventory(h)
        h.hub.add_events(105, code=1, line=1)
        st = h.hub.status()["ring"]
        self.assertEqual(st["total"], 105)
        self.assertEqual(st["wrap"], 1)
        self.assertEqual(st["head"], 5)
        self.assertEqual(st["pending"], 100)  # 5 oldest lost on overflow
        seq = 70
        consumed = 0
        while h.hub.pending_count() > 0:
            tail = h.hub.tail()
            count = min(4, h.hub.pending_count())
            h.request(0x44, sc.TYPE_GET,
                      struct.pack("<HH", tail, count), seq=seq)
            batch = h.replies()[-1]
            self.assertEqual(len(batch.data) // 60, count)
            consumed += count
            h.request(0x46, sc.TYPE_SET,
                      struct.pack("<H", (tail + count) % 100), seq=seq)
            seq += 1
        self.assertEqual(consumed, 100)
        self.assertEqual(h.hub.pending_count(), 0)

    def test_bad_slot_error606(self):
        h = HubHarness()
        upload_inventory(h)
        h.hub.add_events(2, code=1, line=1)
        h.hub.bad_slots.add(0)
        h.request(0x44, sc.TYPE_GET, struct.pack("<HH", 0, 2), seq=80)
        err = h.replies()[-1]
        self.assertEqual((err.type, err.data[0]), (sc.TYPE_ERROR, 0x06))
        h.request(0x42, sc.TYPE_GET, struct.pack("<H", 1), seq=81)
        self.assertEqual(len(h.replies()[-1].data), 60)

    def test_busy_error603_not_cached(self):
        h = HubHarness()
        upload_inventory(h)
        h.hub.add_events(2, code=1, line=1)
        h.hub.busy_until_ms = h.now + 5000
        h.request(0x44, sc.TYPE_GET, struct.pack("<HH", 0, 2), seq=82)
        self.assertEqual(h.replies()[-1].data[0], 0x03)
        h.request(0x44, sc.TYPE_GET, struct.pack("<HH", 0, 2), seq=82)
        self.assertEqual(
            len([p for p in h.replies() if p.seq == 82]), 2)

    def test_bell_edge_and_repeat(self):
        h = HubHarness()
        upload_inventory(h)
        self.assertEqual(len(h.notifies(0x47)), 0)
        h.hub.add_events(1, code=1, line=1)
        self.assertEqual(len(h.notifies(0x47)), 1)
        h.advance(61000)
        self.assertEqual(len(h.notifies(0x47)), 2)  # 60 s repeat


class ConfigGroupTests(unittest.TestCase):

    def make_block(self):
        block = bytearray(96)
        block[0:3] = bytes([0, 1, 0])
        block[3:7] = struct.pack("<f", 6.0)
        block[7:11] = struct.pack("<f", 13.0)
        block[11:15] = struct.pack("<f", 0.3)
        block[15:19] = struct.pack("<f", 1000.0)
        block[19:23] = struct.pack("<f", 2.0)
        block[23] = 50
        block[24:26] = struct.pack("<H", 60)
        block[26:28] = struct.pack("<H", 30)
        block[28:30] = struct.pack("<H", 180)
        block[30:32] = struct.pack("<H", 60)
        block[32:36] = struct.pack("<f", 5.0)
        block[36:38] = struct.pack("<H", 200)
        block[38:40] = struct.pack("<H", 100)
        block[40:42] = struct.pack("<H", 40)
        block[42] = 3
        block[43] = 0
        block[44] = 1
        block[45] = 39
        block[46] = 0
        block[47:51] = struct.pack("<f", 2.0)
        block[51:53] = struct.pack("<H", 5000)
        block[53:57] = struct.pack("<f", 32.0)
        return bytes(block)

    def write_group(self, h, group_id=7, block=None):
        block = block or self.make_block()
        seq = 100
        for phase in (1, 2, 3):
            eui = make_eui(1, phase)
            h.request(0x22, sc.TYPE_SET, eui + block, seq=seq)
            self.assertEqual(h.replies()[-1].type, sc.TYPE_ACK)
            seq += 1
        h.request(0x24, sc.TYPE_SET, bytes([group_id]), seq=seq)
        return block

    def test_happy_path_applied_with_crc(self):
        h = HubHarness()
        upload_inventory(h)
        block = self.write_group(h)
        notes = h.notifies(0x21)
        self.assertEqual(notes[0].data[1], hm.GROUP_STAGED)
        h.advance(5000)
        h.advance(10000)
        notes = h.notifies(0x21)
        states = [n.data[1] for n in notes]
        self.assertEqual(states, [hm.GROUP_STAGED, hm.GROUP_DELIVERED,
                                  hm.GROUP_APPLIED])
        applied = notes[-1]
        self.assertEqual(applied.data[2], 0x07)  # member bitmap
        crc = int.from_bytes(applied.data[4:6], "little")
        self.assertEqual(crc, sc.crc16_ccitt_false(block[3:57]))

    def test_status_get_reflects_history(self):
        h = HubHarness()
        upload_inventory(h)
        self.write_group(h)
        h.advance(20000)
        h.request(0x28, sc.TYPE_GET, bytes([7]), seq=200)
        reply = h.replies()[-1]
        self.assertEqual(len(reply.data), 8)
        self.assertEqual(reply.data[1], hm.GROUP_APPLIED)

    def test_missing_group_commit_rejected(self):
        h = HubHarness()
        upload_inventory(h)
        block = self.make_block()
        eui = make_eui(1, 1)
        h.request(0x22, sc.TYPE_SET, eui + block, seq=100)
        h.request(0x24, sc.TYPE_SET, bytes([9]), seq=101)
        self.assertEqual(h.replies()[-1].data[0], 0x02)

    def test_fourth_eui_rejected(self):
        h = HubHarness()
        upload_inventory(h)
        block = self.make_block()
        for phase in (1, 2, 3, 4):
            eui = make_eui(1, phase)
            h.request(0x22, sc.TYPE_SET, eui + block, seq=110 + phase)
        self.assertEqual(h.replies()[-1].data[0], 0x02)

    def test_abort_reason10(self):
        h = HubHarness()
        upload_inventory(h)
        self.write_group(h)
        h.request(0x26, sc.TYPE_SET, bytes([7]), seq=150)
        notes = h.notifies(0x21)
        self.assertEqual(notes[-1].data[1], hm.GROUP_FAILED)
        self.assertEqual(notes[-1].data[3], 10)

    def test_reboot_fails_group_reason8(self):
        h = HubHarness()
        upload_inventory(h)
        self.write_group(h)
        h.hub.reboot()
        h.request(0x28, sc.TYPE_GET, bytes([7]), seq=210)
        reply = h.replies()[-1]
        self.assertEqual(reply.data[1], hm.GROUP_FAILED)
        self.assertEqual(reply.data[3], 8)
        notifies = [p for (_, p, k) in h.sent if k == "notify"]
        self.assertTrue(any(p.cmd == 0x12 and p.data[1] == 2
                            for p in notifies))
        self.assertTrue(any(p.cmd == 0xE3 and p.data[3] == 0xFF
                            for p in notifies))

    def test_busy_writes_while_staged(self):
        h = HubHarness()
        upload_inventory(h)
        block = self.write_group(h)   # 3 members + commit -> STAGED
        # a new 0x22 (new SEQ) while STAGED -> ERROR 0x03
        h.request(0x22, sc.TYPE_SET, make_eui(1, 1) + block, seq=130)
        err = h.replies()[-1]
        self.assertEqual((err.type, err.data[0]), (sc.TYPE_ERROR, 0x03))


class PwrbTests(unittest.TestCase):

    def test_get_returns_23b(self):
        h = HubHarness()
        h.request(0xE5, sc.TYPE_GET, b"", seq=1)
        reply = h.replies()[-1]
        self.assertEqual(len(reply.data), 23)
        self.assertEqual(reply.data[0], 1)

    def test_set_gen_mismatch(self):
        h = HubHarness()
        data = bytes([5]) + (0x0200).to_bytes(2, "little") + b"\x00" * 13
        h.request(0xE5, sc.TYPE_SET, data, seq=2)
        err = h.replies()[-1]
        self.assertEqual(err.type, sc.TYPE_ERROR)
        self.assertEqual(err.data, bytes([0x03, 0x00]))  # busy + gen 0

    def test_set_write_and_echo(self):
        h = HubHarness()
        h.hub.pwr_echo_delay_s = 0.5
        alanlar = bytearray(13)
        alanlar[7] = 40          # byte9 C-ORANI
        alanlar[11] = 30         # byte13 KAPASITE
        alanlar[12] = 0x06       # byte14 period 6 s
        data = bytes([0]) + ((1 << 9) | (1 << 13) | (1 << 14)) \
            .to_bytes(2, "little") + bytes(alanlar)
        h.request(0xE5, sc.TYPE_SET, data, seq=3)
        ack = h.replies()[-1]
        self.assertEqual(ack.type, sc.TYPE_ACK)
        self.assertEqual(ack.data[0], 1)  # GEN advanced to 1
        h.advance(600)
        h.request(0xE5, sc.TYPE_GET, b"", seq=4)
        reply = h.replies()[-1]
        self.assertEqual(reply.data[17], 1)   # echo valid
        self.assertEqual(reply.data[18], 1)   # echo gen == GEN
        self.assertEqual(reply.data[1:17][9], 40)
        self.assertEqual(reply.data[1:17][13], 30)

    def test_set_out_of_range_c2red(self):
        h = HubHarness()
        alanlar = bytearray(13)
        alanlar[7] = 250         # byte9 out of 20..200
        alanlar[11] = 99         # byte13 out of 7..54
        alanlar[12] = 0x03
        data = bytes([0]) + ((1 << 9) | (1 << 13) | (1 << 14)) \
            .to_bytes(2, "little") + bytes(alanlar)
        h.request(0xE5, sc.TYPE_SET, data, seq=5)
        err = h.replies()[-1]
        self.assertEqual(err.data[0], 0x02)
        red = struct.unpack("<H", err.data[1:3])[0]
        self.assertEqual(red, (1 << 9) | (1 << 13))

    def test_command_result_lifecycle(self):
        h = HubHarness()
        h.hub.pwr_result_delay_s = 0.3
        h.request(0xE6, sc.TYPE_SET, bytes([0x05, 0xA5]), seq=6)
        ack = h.replies()[-1]
        self.assertEqual(ack.data, bytes([0x05, 0x01]))
        h.request(0xE7, sc.TYPE_GET, b"", seq=7)
        cur = h.replies()[-1]
        self.assertEqual(cur.data[2], 0xFF)   # no result yet
        h.advance(400)
        results = h.notifies(0xE7)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].data[2], 0x00)
        self.assertEqual(results[0].data[4], 0x08)  # verified bit

    def test_command_bad_param(self):
        h = HubHarness()
        h.hub.pwr_result_sonuc = 0x00
        h.hub.pwr_result_delay_s = 0.1
        h.request(0xE6, sc.TYPE_SET, bytes([0x05, 0x00]), seq=8)
        h.advance(200)
        results = h.notifies(0xE7)
        self.assertEqual(results[-1].data[2], 0x02)  # PARAM invalid

    def test_command_over_5_rejected(self):
        h = HubHarness()
        h.request(0xE6, sc.TYPE_SET, bytes([0x06, 0x00]), seq=9)
        self.assertEqual(h.replies()[-1].data[0], 0x02)

    def test_e1_e3_as_request_error601(self):
        h = HubHarness()
        h.request(0xE1, sc.TYPE_GET, b"", seq=10)
        self.assertEqual(h.replies()[-1].data[0], 0x01)
        h.request(0xE3, sc.TYPE_SET, b"\x00" * 11, seq=11)
        self.assertEqual(h.replies()[-1].data[0], 0x01)

    def test_e8_without_telemetry(self):
        h = HubHarness()
        h.request(0xE8, sc.TYPE_GET, b"", seq=12)
        self.assertEqual(h.replies()[-1].data[0], 0x05)


class ProactiveAndFaultsTests(unittest.TestCase):

    def test_live_stream_and_trip(self):
        h = HubHarness()
        upload_inventory(h, lines=(1,))
        h.hub.live_period_s = 1
        h.advance(3500)
        lives = h.notifies(0x11)
        self.assertGreaterEqual(len(lives), 3)
        first, last = lives[0], lives[-1]
        self.assertEqual(first.data[0], (1 << 2) | 1)
        up_first = int.from_bytes(first.data[5:9], "little")
        up_last = int.from_bytes(last.data[5:9], "little")
        self.assertGreater(up_last, up_first)
        h.hub.inject_trip(1, 2)
        trip = h.notifies(0x10)[-1]
        self.assertEqual(trip.data[1], 1)
        self.assertEqual(trip.data[3], (1 << 2) | 2)

    def test_reserved_band_and_unknown(self):
        h = HubHarness()
        h.request(0x60, sc.TYPE_GET, b"", seq=1)
        self.assertEqual(h.replies()[-1].data[0], 0x01)
        h.request(0xE2, sc.TYPE_GET, b"", seq=2)
        self.assertEqual(h.replies()[-1].data[0], 0x01)

    def test_cfg_read_all_505(self):
        h = HubHarness()
        h.request(0x20, sc.TYPE_GET, b"\x00" * 8, seq=3)
        self.assertEqual(h.replies()[-1].data[0], 0x05)

    def test_set_config_504(self):
        h = HubHarness()
        h.request(0x03, sc.TYPE_SET, b"\x00", seq=4)
        self.assertEqual(h.replies()[-1].data[0], 0x04)

    def test_broadcast_processed_not_answered(self):
        h = HubHarness()
        upload_inventory(h)
        h.request(0x01, sc.TYPE_GET, b"", seq=5, dst=sc.ADDR_BROADCAST)
        self.assertEqual(len([p for p in h.replies() if p.seq == 5]), 0)

    def test_fault_engine_drop_and_corrupt(self):
        h = HubHarness()
        out = []
        engine = faults_mod.FaultEngine(out.append,
                                        now_ms=lambda: h.now,
                                        on_note=lambda t: None)
        pkt = sc.ScpPacket(sc.ADDR_RTU, sc.ADDR_HUB, sc.TYPE_ACK, 0x01,
                           9, b"")
        engine.arm("drop_next", count=1)
        engine.send_packet(pkt, "reply")
        self.assertEqual(out, [])
        engine.send_packet(pkt, "reply")
        engine.pump()
        self.assertEqual(len(out), 1)

        engine.arm("corrupt_next", kind="crc")
        engine.send_packet(pkt, "reply")
        engine.pump()
        frame = out[-1]
        logical = sc.cobs_decode(frame[1:-1])
        with self.assertRaises(sc.ScpFrameError):
            sc.decode_logical(logical, sc.ADDR_RTU)

    def test_fault_engine_split_and_delay(self):
        h = HubHarness()
        out = []
        engine = faults_mod.FaultEngine(out.append,
                                        now_ms=lambda: h.now,
                                        on_note=lambda t: None)
        pkt = sc.ScpPacket(sc.ADDR_RTU, sc.ADDR_HUB, sc.TYPE_ACK, 0x40,
                           9, b"")
        engine.arm("split_next", gap_ms=150)
        engine.send_packet(pkt, "reply")
        engine.pump()
        self.assertEqual(len(out), 1)          # first half only
        h.now += 200
        engine.pump()
        self.assertEqual(len(out), 2)          # remainder after gap

        engine.arm("delay_next", ms=700)
        engine.send_packet(pkt, "reply")
        engine.pump()
        self.assertEqual(len(out), 2)
        h.now += 750
        engine.pump()
        self.assertEqual(len(out), 3)


class V12PlanAdditions(unittest.TestCase):
    """Plan v1.2 additions: counter wrap, CFG2 persistence, corrupt
    inner record CRC, parser gap boundary at 99/100/101 ms."""

    def test_ring_counter_wrap_truncation(self):
        h = HubHarness()
        upload_inventory(h)
        # push total past the u16 wrap field boundary (65535 * 100)
        h.hub.total = 65535 * 100 + 40
        h.hub.consumed = 65535 * 100
        for i in range(40):
            h.hub.slots[i] = h.hub.build_event_record(1, 1, 1, (i % 3) + 1)
        h.request(0x40, sc.TYPE_GET, b"", seq=300)
        reply = h.replies()[-1]
        self.assertEqual(reply.type, sc.TYPE_ACK)
        head_i, wrap_i, total_i, tail_i = struct.unpack("<HHIH",
                                                       reply.data)
        self.assertEqual(wrap_i, 65535)          # truncated, no exception
        self.assertEqual(head_i, 40)
        self.assertEqual(total_i, (65535 * 100 + 40) & 0xFFFFFFFF)

    def test_cfg2_survives_hub_reboot(self):
        h = HubHarness()
        h.hub.pwr_echo_delay_s = 0.1
        alanlar = bytearray(13)
        alanlar[11] = 30
        data = bytes([0]) + ((1 << 13)).to_bytes(2, "little") +             bytes(alanlar)
        h.request(0xE5, sc.TYPE_SET, data, seq=3)
        gen_before = h.replies()[-1].data[0]
        h.advance(200)
        h.hub.reboot()
        h.request(0xE5, sc.TYPE_GET, b"", seq=4)
        reply = h.replies()[-1]
        self.assertEqual(reply.data[1:17][13], 30)   # capacity kept
        self.assertEqual(reply.data[18], gen_before)  # echo gen kept
        self.assertEqual(reply.data[1:17][1], gen_before)  # GEN kept

    def test_corrupt_record_slot_served_broken(self):
        h = HubHarness()
        upload_inventory(h)
        h.hub.add_events(2, code=1, line=1)
        h.hub.corrupt_record_slots.add(1)
        h.request(0x42, sc.TYPE_GET, struct.pack("<H", 1), seq=5)
        reply = h.replies()[-1]
        self.assertEqual(reply.type, sc.TYPE_ACK)
        record = bytes(reply.data)
        crc = sc.crc16_ccitt_false(record[0:58])
        self.assertNotEqual(crc, int.from_bytes(record[58:60], "little"))

    def test_parser_gap_boundary_99_100_101(self):
        for gap_ms, should_parse in ((99, True), (100, False),
                                     (101, False)):
            received = []
            self.now = 5000
            parser = sc.ScpParser(
                sc.ADDR_HUB, lambda fr: received.append(fr),
                clock=lambda: self.now)
            frame = sc.encode_frame(sc.ScpPacket(
                sc.ADDR_HUB, sc.ADDR_RTU, sc.TYPE_GET, 0x40, 9))
            for byte in frame[:6]:
                parser.feed(byte, now_ms=5000)
            for byte in frame[6:]:
                parser.feed(byte, now_ms=5000 + gap_ms)
            if should_parse:
                self.assertEqual(len(received), 1,
                                 "gap %d ms should keep the frame" % gap_ms)
            else:
                self.assertEqual(received, [],
                                 "gap %d ms should discard" % gap_ms)


def build_suite():
    suite = unittest.TestSuite()
    for cls in (CodecBasics, ParserBehaviour, GoldenCaptures, Cp56Helpers,
                BringUpTests, IdempotencyTests, EventRingTests,
                ConfigGroupTests, PwrbTests, ProactiveAndFaultsTests,
                V12PlanAdditions):
        suite.addTest(unittest.TestLoader().loadTestsFromTestCase(cls))
    return suite


def main():
    result = unittest.TextTestRunner(verbosity=2).run(build_suite())
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
