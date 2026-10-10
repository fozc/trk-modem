"""MH (RF hub) behaviour model per BOLATeX SCP R2 (09.10.2026).

Implements the hub side of the SCP interface: every RTU request handler,
proactive notification generators, inventory/bring-up semantics, the
100-slot persistent event ring, the config-group state machine and the
power-board (PWRB) family. Wire faults are applied by faults.py on the
encoded frame; behaviour faults are knobs on this model.

The model is transport-agnostic: it emits ScpPacket objects through the
injected send callback and receives parsed packets via handle_packet().
Time is injected (monotonic ms) so the self-tests are deterministic.

Ring bookkeeping is exact: head = total % 100, wrap = total // 100,
tail = consumed % 100, pending = total - consumed.
"""

import struct
import time

from . import cp56
from . import scp_codec as sc

HUB_SLOTS = 100
EVENT_LEN = 60
BLOCK_LEN = 96
INVENTORY_MAX = 21
RF_FEEDERS = (1, 2, 3, 4)

# Reserved CMD bands -> ERROR 0x01 (spec section 3).
RESERVED_BANDS = (
    list(range(0x08, 0x10)) + list(range(0x15, 0x20)) +
    list(range(0x2C, 0x40)) + list(range(0x49, 0x60)) +
    list(range(0x60, 0xC0)) + list(range(0xC0, 0xE0)) +
    [0xE0, 0xE2, 0xE4] + list(range(0xE9, 0xF0))
)

GROUP_IDLE = 0
GROUP_STAGED = 1
GROUP_DELIVERED = 2
GROUP_APPLIED = 3
GROUP_FAILED = 4

PWR_PERIOD_S = 10
LIVE_PERIOD_S = 5
BOOT_FIRST_MS = 1000
BOOT_MAX_MS = 30000
INVENTORY_GAP_MS = 10000
LOG_BELL_REPEAT_MS = 60000


def in_reserved_band(cmd):
    return cmd in RESERVED_BANDS


class InventoryEntry:
    def __init__(self, zone, fider, phase, eui, channel):
        self.zone = zone
        self.fider = fider
        self.phase = phase
        self.eui = bytes(eui)
        self.channel = channel
        self.live_seq = 0
        self.uptime_s = 0
        # live-value knobs (scenario-controllable)
        self.irms_a = 12.5
        self.v_trip = 32.0
        self.v_harvest = 8.4
        self.mcu_temp_c = 31
        self.rssi_dbm = -45
        self.status_flags = 0x03
        self.fsm_error = 0
        self.trip_failed = 0
        self.log_pending = 0
        self.current_state = 1
        self.fault_count = 0
        self.boot_counter = 7
        self.last_live_ms = None             # set by _send_live / preload

    def src(self):
        return ((self.fider << 2) | self.phase) & 0xFF


class HubModel:
    """State + handlers for the simulated MH. Transport-free."""

    def __init__(self, send_fn, now_ms=None, on_event=None):
        self.send = send_fn                # send_fn(ScpPacket, kind)
        self.now_ms = now_ms or (lambda: int(time.monotonic() * 1000))
        self.on_event = on_event or (lambda ev: None)

        # --- knobs (defaults; scenario/control overrides) ---
        self.fw_label = "SIM-T4R2"
        self.sched_active = 1
        self.live_period_s = LIVE_PERIOD_S
        self.live_enabled = True          # master switch for the 0x11 stream
        self.live_muted = set()           # (fider, phase) tuples: no 0x11
        self.log_bell_enabled = True      # allow poll-without-notify tests
        self.pwr_enabled = True           # master switch for the 0xE1 stream
        self.mh_has_key = True            # R2: keyless -> 0x22/0x24/0x2A 0x02
        self.rf_queue_busy = False        # R2: 0x2A ERROR 0x03 condition 4
        self.epoch_duration_ms = 19800    # R2 masa: 19.6..19.9 s renewal
        self.cfg_delivered_ms = 5000
        self.cfg_applied_ms = 10000
        self.cfg_force_fail_reason = None  # e.g. 1/2 forced at commit
        self.cfg_mute = False             # drop all 0x21 notifies
        self.cfg_no_deliver = False        # STAGED -> FAILED 5 after window
        self.cfg_no_apply = False          # DELIVERED -> FAILED 6 after win.
        self.cfg_delivered_fail_ms = 8000  # failure window when no_deliver
        self.cfg_applied_fail_ms = 8000    # failure window when no_apply
        self.cfg_report_crc_offset = 0     # corrupt the reported cfg_crc
        self.pwr_echo_delay_s = 20.0
        self.pwr_result_delay_s = 2.0
        self.pwr_result_sonuc = 0x00
        self.pwr_akibet_delay_s = 2.0     # 0x05 cancel -> akıbet notify
        self.pwr_cancel_verified = False  # akıbet b3 instead of b2
        self.aku_kaynak_yok = False       # 0xE1 soh 0xFF + 39..42 0xFFFF
        self.telemetry_stale = False      # 0xE1 durum b0=0, 39..44 0xFFFF

        # --- identity / time ---
        self.started_ms = self.now_ms()
        self.time_valid = False
        self.synced_wall_s = 0.0
        self.synced_mono_ms = 0

        # --- inventory ---
        self.inventory = {}                # eui_hex -> InventoryEntry
        self.learned_zone = None
        self.loaded = False
        self.last_inventory_activity_ms = None

        # --- request/idempotency tracking (spec 2.3) ---
        self.last_request = None           # (src, seq)
        self.last_set_reply = None         # ScpPacket replayed on repeat

        # --- event ring ---
        self.slots = [None] * HUB_SLOTS
        self.total = 0                     # writes ever (monotonic)
        self.consume_baseline = None
        self.consume_locked = False
        self.consumed = 0                  # consume cursor (monotonic)
        # R2 4.6: head/wrap/tail slots are explicit cursors so a service
        # wipe can report wrap=0/tail=0 while `total` is preserved.
        self.ring_head_slot = 0
        self.ring_wrap = 0
        self.ring_tail_slot = 0
        self.bad_slots = set()             # indices -> ERROR 0x06
        # slots served with a corrupted INNER record CRC (plan v1.2
        # section 6.1: distinct from ERROR 0x06; the RTU must not
        # consume past the valid prefix of the ACK body)
        self.corrupt_record_slots = set()
        self.degraded = False
        self.busy_until_ms = 0             # log cmds -> ERROR 0x03 window
        self.bell_last_ms = None

        # --- config group ---
        self.group = None
        self.group_history = {}            # group_id -> last 8 B body
        self.epoch_busy_until_ms = 0
        self.epoch_due_ms = None           # R2: renewal -> 201 + 120 events
        self.epoch_feeder = 0

        # --- proactive state ---
        self.boot_started = False
        self.boot_next_ms = None
        self.boot_interval_ms = 1000
        self.proactive_seq = 0
        self.live_next_ms = {}
        self.pwr_next_ms = None
        self.pwr_session = 1
        self.pwr_sira = 0
        self.pwr_alarm_sayac = 0

        # --- PWRB model ---
        self.cfg2 = bytearray(16)          # SURUM 0, GEN 0 (unset)
        self.echo_valid = 0
        self.echo_gen = 0
        self.m1 = 0
        self.m2 = 0
        self.c2_red_last = 0
        self.pwr_busy_until_ms = 0
        self.telemetry = None              # dict -> 0xE1 values / 0xE8 raw
        self.pwr_alarm_mask = 0
        self.cmd_state = {"komut": 0, "sira": 0, "sonuc": 0xFF, "yayin": 0,
                          "durum": 0}
        self.cmd_pending = False
        self.cmd_param = 0
        self.cmd_due_ms = 0
        self.echo_due_ms = None
        self.akibet_due_ms = None          # R2 5.6: 0x05 cancel akıbet notify

    # ------------------------------------------------------------------
    # helpers
    # ------------------------------------------------------------------

    def note(self, text, **extra):
        rec = {"event": "note", "text": text}
        rec.update(extra)
        self.on_event(rec)

    def uptime_s(self):
        return int((self.now_ms() - self.started_ms) / 1000)

    def wall_struct(self):
        """Local-time struct for stamps; None before TIME_SYNC."""
        if not self.time_valid:
            return None
        return time.localtime(self.synced_wall_s +
                              (self.now_ms() - self.synced_mono_ms) / 1000.0)

    def next_proactive_seq(self):
        self.proactive_seq = (self.proactive_seq + 1) & 0xFF
        return self.proactive_seq

    def _cfg_notify(self, body, note=None):
        """Send a 0x21 status body; cfg_mute drops it on the floor
        (lost-terminal-notification cases)."""
        if self.cfg_mute:
            self.note("cfg notify muted: %s" % (note or ""))
            return
        self.notify(0x21, body, note=note)

    def notify(self, cmd, data, note=None):
        pkt = sc.ScpPacket(sc.ADDR_RTU, sc.ADDR_HUB, sc.TYPE_SET, cmd,
                           self.next_proactive_seq(), bytes(data))
        self.send(pkt, "notify")
        if note:
            self.note(note, cmd=cmd)

    def reply(self, req, data=b""):
        pkt = sc.ScpPacket(req.src, sc.ADDR_HUB, sc.TYPE_ACK, req.cmd,
                           req.seq, bytes(data))
        self.send(pkt, "reply")
        if req.type == sc.TYPE_SET:
            self.last_set_reply = pkt

    def reply_error(self, req, code, extra=b""):
        pkt = sc.ScpPacket(req.src, sc.ADDR_HUB, sc.TYPE_ERROR, req.cmd,
                           req.seq, bytes([code]) + bytes(extra))
        self.send(pkt, "reply")

    # ring accessors
    def head(self):
        return self.ring_head_slot

    def wrap_count(self):
        return self.ring_wrap

    def tail(self):
        return self.ring_tail_slot

    def pending_count(self):
        # BOLATeX BQ-02: the ring holds at most HUB_SLOTS - 1 unconsumed
        # records; the writer advances tail when it would reach the oldest
        # unconsumed slot, so head == tail is always empty.
        pending = self.total - self.consumed
        return min(pending, HUB_SLOTS - 1)

    # ------------------------------------------------------------------
    # request dispatch
    # ------------------------------------------------------------------

    def handle_packet(self, pkt):
        """Entry point for a parsed RTU frame (addressed to hub/BCAST)."""
        self.rx_count = getattr(self, "rx_count", 0) + 1
        self.on_event({"event": "rx", "cmd": pkt.cmd, "name": pkt.name(),
                       "type": pkt.type, "type_name": pkt.type_name(),
                       "seq": pkt.seq, "len": len(pkt.data),
                       "data_hex": pkt.data.hex()})

        repeat = (self.last_request == (pkt.src, pkt.seq))
        self.last_request = (pkt.src, pkt.seq)

        # spec 2.2: broadcast is processed but never answered
        answered = pkt.dst != sc.ADDR_BROADCAST

        if pkt.type not in (sc.TYPE_GET, sc.TYPE_SET, sc.TYPE_PING):
            if answered:
                self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
            return

        # SET repeat rule (2.3): same SEQ with no intervening request
        if (pkt.type == sc.TYPE_SET and repeat and
                self.last_set_reply is not None and
                self.last_set_reply.cmd == pkt.cmd and
                self.last_set_reply.seq == pkt.seq):
            self.send(self.last_set_reply, "reply-replay")
            return

        self._dispatch(pkt, answered)

    def _dispatch(self, pkt, answered):
        typ = pkt.type
        cmd = pkt.cmd

        if typ == sc.TYPE_PING:
            if cmd != 0x00:
                if answered:
                    self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
                return
            if answered:
                self.reply(pkt)
            return

        if in_reserved_band(cmd):
            if answered:
                self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
            return

        # PWR family source check (spec 5.1)
        if 0xE5 <= cmd <= 0xE8 and pkt.src != sc.ADDR_RTU:
            if answered:
                self.reply_error(pkt, sc.ERR_NOT_SUPPORTED)
            return

        handlers = {
            0x01: self._h_get_status,
            0x02: self._h_get_fram_stats,
            0x03: self._h_set_config,
            0x04: self._h_inventory_set,
            0x05: self._h_inventory_end,
            0x06: self._h_inventory_update,
            0x07: self._h_time_sync,
            0x20: self._h_cfg_read_all,
            0x22: self._h_cfg_write,
            0x24: self._h_cfg_commit,
            0x26: self._h_cfg_abort,
            0x28: self._h_cfg_status_get,
            0x2A: self._h_epoch_refresh,
            0x40: self._h_log_read_head,
            0x42: self._h_log_read_record,
            0x44: self._h_log_read_range,
            0x46: self._h_log_consume_to,
            0x48: self._h_log_consume_if,
            0xE1: self._h_notify_only,
            0xE3: self._h_notify_only,
            0xE5: self._h_pwr_cfg2,
            0xE6: self._h_pwr_command,
            0xE7: self._h_pwr_result_get,
            0xE8: self._h_pwr_telemetry,
        }
        handler = handlers.get(cmd)
        if handler is None:
            if answered:
                self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
            return
        handler(pkt, answered)

    # ------------------------------------------------------------------
    # system commands (4.2)
    # ------------------------------------------------------------------

    def _h_get_status(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        body = bytearray()
        body += struct.pack("<I", self.uptime_s())
        label = self.fw_label.encode("ascii")[:15]
        body += label + b"\x00" * (16 - len(label))
        body += bytes([self.sched_active])
        body += struct.pack("<I", self.uptime_s() // 10)
        if answered:
            self.reply(pkt, body)

    def _h_get_fram_stats(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy():
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        free = 0 if self.degraded else 99 - self.pending_count()
        body = struct.pack("<HHHB", self.head(), self.wrap_count() & 0xFFFF, free,
                           1 if self.degraded else 0) + b"\x00"
        if answered:
            self.reply(pkt, body)

    def _h_set_config(self, pkt, answered):
        if answered:
            self.reply_error(pkt, sc.ERR_NOT_SUPPORTED)

    def _h_notify_only(self, pkt, answered):
        # 0xE1 / 0xE3 as GET/SET from RTU -> ERROR 0x01 (spec 5.1)
        if answered:
            self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)

    # ------------------------------------------------------------------
    # inventory (4.3)
    # ------------------------------------------------------------------

    def _parse_inventory_body(self, pkt):
        if len(pkt.data) != 12:
            return None
        zone, fider, phase = pkt.data[0], pkt.data[1], pkt.data[2]
        eui = pkt.data[3:11]
        channel = pkt.data[11]
        if zone > 7 or fider > 7 or not 1 <= phase <= 3:
            return None
        return zone, fider, phase, eui, channel

    def _ack_set(self, pkt):
        self.reply(pkt)

    def _store_inventory_entry(self, zone, fider, phase, eui, channel):
        """R2 4.3: last write wins per position. A different EUI-64 at the
        same (fider, phase) drops the old card's RF session without an MH
        reboot; rewriting the same EUI-64 preserves the card's session
        state (uptime/live_seq/boot_counter, BQ-17.2)."""
        key = eui.hex()
        for other_key, other in list(self.inventory.items()):
            if other_key != key and other.fider == fider and \
                    other.phase == phase:
                del self.inventory[other_key]
                self.live_next_ms.pop(other_key, None)
                self.note("card_replaced", old=other_key, new=key)
        existing = self.inventory.get(key)
        if existing is not None:
            existing.zone = zone
            existing.fider = fider
            existing.phase = phase
            existing.channel = channel
        else:
            self.inventory[key] = InventoryEntry(zone, fider, phase, eui,
                                                 channel)

    def _h_inventory_set(self, pkt, answered):
        if pkt.type != sc.TYPE_SET:
            if answered:
                self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
            return
        parsed = self._parse_inventory_body(pkt)
        if parsed is None or parsed[1] == 0:  # fider=0 rejected on 0x04
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        zone, fider, phase, eui, channel = parsed
        if self.learned_zone is not None and zone != self.learned_zone:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if eui.hex() not in self.inventory and \
                len(self.inventory) >= INVENTORY_MAX:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        self._store_inventory_entry(zone, fider, phase, eui, channel)
        if self.learned_zone is None:
            self.learned_zone = zone
        self.last_inventory_activity_ms = self.now_ms()
        self.boot_next_ms = None  # valid entry pauses BOOT repeats (4.3)
        self.note("inventory_set", eui=eui.hex(), fider=fider, phase=phase)
        if answered:
            self._ack_set(pkt)

    def _h_inventory_end(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self.inventory:
            self.loaded = True
            self.boot_next_ms = None
            self.note("inventory_loaded", count=len(self.inventory))
        else:
            self.note("inventory_end_empty")
            if self.boot_next_ms is None:
                self.boot_next_ms = self.now_ms() + self.boot_interval_ms
        if answered:
            self._ack_set(pkt)

    def _h_inventory_update(self, pkt, answered):
        if pkt.type != sc.TYPE_SET:
            if answered:
                self.reply_error(pkt, sc.ERR_UNKNOWN_CMD)
            return
        parsed = self._parse_inventory_body(pkt)
        if parsed is None:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        zone, fider, phase, eui, channel = parsed
        if fider == 0:
            self.inventory.pop(eui.hex(), None)
            self.note("inventory_delete", eui=eui.hex())
        else:
            if self.learned_zone is not None and zone != self.learned_zone:
                if answered:
                    self.reply_error(pkt, sc.ERR_INVALID_PARAM)
                return
            self._store_inventory_entry(zone, fider, phase, eui, channel)
            if self.learned_zone is None:
                self.learned_zone = zone
            self.note("inventory_update", eui=eui.hex(), fider=fider,
                      phase=phase)
        if answered:
            self._ack_set(pkt)

    def _h_time_sync(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 7:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        body = pkt.data
        fields = cp56.unpack_cp56(body)
        if fields["invalid"] or cp56.cp56_out_of_range(body):
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        try:
            wall = time.mktime((fields["year"], fields["month"],
                                fields["day"], fields["hour"],
                                fields["min"], 0, 0, 0, -1)) + \
                fields["ms"] / 1000.0
        except (OverflowError, ValueError):
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        self.time_valid = True
        self.synced_wall_s = wall
        self.synced_mono_ms = self.now_ms()
        self.note("time_synced", wall=wall)
        if answered:
            self._ack_set(pkt)

    def _h_epoch_refresh(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 1:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        fider = pkt.data[0]
        if fider not in RF_FEEDERS or not self.mh_has_key:
            # R2 4.3: ERROR 0x02 for a bad feeder or a keyless MH.
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        now = self.now_ms()
        group = self.group
        config_running = group is not None and group["state"] in (
            GROUP_STAGED, GROUP_DELIVERED)
        if not self.loaded or config_running or \
                now < self.epoch_busy_until_ms or self.rf_queue_busy:
            # R2 4.3: the four ERROR 0x03 conditions.
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        self.epoch_busy_until_ms = now + self.epoch_duration_ms
        self.epoch_due_ms = now + self.epoch_duration_ms
        self.epoch_feeder = fider
        self.note("epoch_refresh", fider=fider)
        if answered:
            self._ack_set(pkt)

    # ------------------------------------------------------------------
    # event ring (4.6)
    # ------------------------------------------------------------------

    def _log_busy(self):
        return self.now_ms() < self.busy_until_ms

    def build_event_record(self, code, zone, line, phase, fault_count=0,
                           max_current=1250.0, di_dt=800.0, dur_ms=120,
                           v_trip=32.0, v_harv=8.4, mcu_temp=31,
                           clock_quality=1, boot_counter=7,
                           perm_count=None, temp_count=None,
                           mh_record=False):
        """Build one 60 B record (R2 4.7).

        mh_record=True models an MH-produced record (135/136/138/201 and
        MH's own 143): timestamp, boot_counter, uptime_sec, clock_quality
        and src_eui_hash are zero. perm_count/temp_count fill the
        event-specific u16 fields at offsets 41/43 (122 cfg_crc, 201
        phase mask).
        """
        if not 0 <= boot_counter <= 0xFFFF:
            raise ValueError("event boot counter must be uint16")
        rec = bytearray(EVENT_LEN)
        if mh_record:
            clock_quality = 0
            boot_counter = 0
        else:
            stamp = self.wall_struct()
            if stamp is None:
                rec[0:7] = b"\x00" * 7  # invalid clock: ms=0 fields zero
                clock_quality = 0
            else:
                rec[0:7] = cp56.pack_cp56(stamp)
        rec[7] = code
        rec[8] = zone
        rec[9] = line
        rec[10] = phase
        rec[11] = fault_count
        rec[13] = 1 if max_current > 20 else 0   # nominal_current_status
        rec[14] = 1 if max_current > 0.3 else 0  # energy_status
        rec[15:19] = struct.pack("<f", max_current)
        rec[19:23] = struct.pack("<f", di_dt)
        rec[23:27] = struct.pack("<I", dur_ms)
        rec[27:31] = struct.pack("<f", v_trip)
        rec[31:35] = struct.pack("<f", v_harv)
        rec[39:41] = struct.pack("<h", mcu_temp)
        if perm_count is not None:
            rec[41:43] = struct.pack("<H", perm_count & 0xFFFF)
        if temp_count is not None:
            rec[43:45] = struct.pack("<H", temp_count & 0xFFFF)
        rec[49:51] = struct.pack("<H", boot_counter)
        rec[51:55] = struct.pack("<I", 0 if mh_record else self.uptime_s())
        rec[55] = clock_quality
        entry = next((item for item in self.inventory.values()
                      if item.fider == line and item.phase == phase), None)
        if entry is not None and not mh_record and \
                code not in (135, 136, 138, 201):
            source_hash = sc.crc16_ccitt_false(entry.eui) or 0xFFFF
            rec[56:58] = struct.pack("<H", source_hash)
        crc = sc.crc16_ccitt_false(rec[0:58])
        rec[58:60] = struct.pack("<H", crc)
        return bytes(rec)

    def add_events(self, count, code=1, line=1, zone=None, phase=None,
                   fault_count=None, boot_counter=7, dur_ms=None,
                   perm_count=None, temp_count=None, mh_record=False):
        """Append records; a full ring overwrites the oldest unconsumed."""
        if not 0 <= boot_counter <= 0xFFFF:
            raise ValueError("event boot counter must be uint16")
        if zone is None:
            zone = self.learned_zone if self.learned_zone is not None else 0
        was_pending = self.pending_count()
        for i in range(count):
            ph = phase if phase is not None else (i % 3) + 1
            fc = fault_count if fault_count is not None else 2
            if code == 7:
                fc = 0
            kwargs = {}
            if dur_ms is not None:
                kwargs["dur_ms"] = dur_ms
            rec = self.build_event_record(code, zone, line, ph, fc,
                                          boot_counter=boot_counter,
                                          perm_count=perm_count,
                                          temp_count=temp_count,
                                          mh_record=mh_record, **kwargs)
            if self.pending_count() >= HUB_SLOTS - 1:
                self.consumed += 1  # oldest unconsumed slot is lost
                self.ring_tail_slot = (self.ring_tail_slot + 1) % HUB_SLOTS
            self.slots[self.ring_head_slot] = rec
            self.ring_head_slot = (self.ring_head_slot + 1) % HUB_SLOTS
            if 0 == self.ring_head_slot:
                self.ring_wrap = (self.ring_wrap + 1) & 0xFFFF
            self.total += 1
        self.note("events_added", count=count, code=code, line=line,
                  pending=self.pending_count())
        if was_pending == 0 and self.pending_count() > 0:
            self.send_log_bell()

    def _reset_store(self, subcode, keep_total):
        """Apply the R2 4.6 store-reset table and arm the consume lock."""
        self.slots = [None] * HUB_SLOTS
        if not keep_total:
            self.total = 0
            self.consumed = 0
        self.slots[0] = self.build_event_record(138, 0, 0, 0,
                                                fault_count=subcode,
                                                mh_record=True)
        self.total += 1
        self.consumed = self.total - 1
        self.ring_head_slot = 1
        self.ring_wrap = 0
        self.ring_tail_slot = 0
        self.consume_locked = True
        self.consume_baseline = None
        self.bell_last_ms = None
        self.note("store_reset", subcode=subcode, total=self.total)
        self.send_log_bell()

    def service_wipe(self):
        """R2: service console wipe -> first record 138 subcode 3."""
        self._reset_store(3, keep_total=True)

    def store_format(self):
        """Boot-time formatting -> 138 subcode 1, total restarts at 1."""
        self._reset_store(1, keep_total=False)

    def store_state_loss(self):
        """Boot-time state loss -> 138 subcode 2, total restarts at 1."""
        self._reset_store(2, keep_total=False)

    def advance_ring(self, records):
        """Fast-forward the write cursor for BQ-18 wrap-overflow tests."""
        records = int(records)
        if 0 > records:
            raise ValueError("records must be >= 0")
        ahead = self.ring_head_slot + records
        self.ring_wrap = (self.ring_wrap + ahead // HUB_SLOTS) & 0xFFFF
        self.ring_head_slot = ahead % HUB_SLOTS
        self.total += records
        self.note("ring_advanced", records=records, total=self.total,
                  wrap=self.ring_wrap)

    def send_log_bell(self):
        self.bell_last_ms = self.now_ms()
        if not self.log_bell_enabled:
            return
        self.notify(0x47, struct.pack("<HH", min(self.pending_count(),
                                                 0xFFFF), self.head()),
                    note="log_bell pending=%d" % self.pending_count())

    def _h_log_read_head(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy() or self.degraded:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        self.consume_locked = False
        if self.consume_baseline is None:
            self.consume_baseline = self.consumed
        body = struct.pack("<HHIH", self.head(),
                           self.wrap_count() & 0xFFFF,
                           self.total & 0xFFFFFFFF, self.tail())
        if answered:
            self.reply(pkt, body)

    def _slot_readable(self, index):
        return index not in self.bad_slots and self.slots[index] is not None

    def _slot_record(self, index):
        """Record bytes; corrupt the inner CRC when the knob asks for it."""
        record = bytearray(self.slots[index])
        if index in self.corrupt_record_slots:
            record[58] ^= 0xFF
        return bytes(record)

    def _h_log_read_record(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or len(pkt.data) != 2:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy() or self.degraded:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        index = struct.unpack("<H", pkt.data)[0]
        if index >= HUB_SLOTS:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if not self._slot_readable(index):
            if answered:
                self.reply_error(pkt, sc.ERR_RECORD_INVALID)
            return
        if answered:
            self.reply(pkt, self._slot_record(index))

    def _h_log_read_range(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or len(pkt.data) != 4:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy() or self.degraded:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        start, count = struct.unpack("<HH", pkt.data)
        if start >= HUB_SLOTS or count < 1:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        count = min(count, 4)
        body = bytearray()
        index = start
        for _ in range(count):
            if index == self.head() or not self._slot_readable(index):
                break
            body += self._slot_record(index)
            index = (index + 1) % HUB_SLOTS
        if not body:
            if answered:
                self.reply_error(pkt, sc.ERR_RECORD_INVALID)
            return
        if answered:
            self.reply(pkt, body)

    def _h_log_consume_to(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 2:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy() or self.degraded:
            # spec 2.3: 0x46 ERROR 0x03 replies are never cached
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        index = struct.unpack("<H", pkt.data)[0]
        if index >= HUB_SLOTS:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self.consume_baseline is None:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        target = self.consume_baseline + ((index -
                    (self.consume_baseline % HUB_SLOTS)) % HUB_SLOTS)
        if target < self.consumed or target > self.total:
            self.consume_baseline = None
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        self.consumed = target
        self.ring_tail_slot = target % HUB_SLOTS
        self.consume_baseline = target
        self.note("log_consume", index=index, tail=self.tail(),
                  left=self.pending_count())
        if answered:
            self.reply(pkt, struct.pack("<HH", self.tail(),
                                        min(self.pending_count(),
                                            0xFFFF)))

    def _h_log_consume_if(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 4:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self._log_busy() or self.degraded:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        target = struct.unpack("<I", pkt.data)[0]
        if self.consume_locked or target < self.consumed or target > self.total:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        self.consumed = target
        self.ring_tail_slot = target % HUB_SLOTS
        self.note("log_consume_if", target=target, tail=self.tail(),
                  left=self.pending_count())
        if answered:
            self.reply(pkt, struct.pack("<HHI", self.tail(),
                                        self.pending_count(), self.consumed))

    # ------------------------------------------------------------------
    # config group (4.9 / 4.10)
    # ------------------------------------------------------------------

    def cfg_writable_crc(self, block):
        return sc.crc16_ccitt_false(block[3:57])

    def _new_group(self):
        return {
            "id": 0,
            "state": GROUP_IDLE,
            "members": [],           # eui bytes in 0x22 accept order
            "block": None,
            "reason": 0,
            "cfg_crc": 0,
            "attempts": 0,
            "staged_ms": None,
            "delivered_ms": None,
        }

    def _h_cfg_read_all(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or len(pkt.data) != 8:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if answered:
            self.reply_error(pkt, sc.ERR_NOT_AVAILABLE)

    def _h_cfg_write(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 104:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        eui = pkt.data[0:8]
        block = bytes(pkt.data[8:104])
        if not self.mh_has_key:
            # R2 4.9: a keyless MH rejects 0x22 with ERROR 0x02.
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group = self.group
        if group is not None and group["state"] in (GROUP_STAGED,
                                                    GROUP_DELIVERED):
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        if group is None or group["state"] != GROUP_IDLE:
            group = self._new_group()
            self.group = group
            self.group_history.clear()  # BQ-11: first WRITE drops old job.
        if eui not in group["members"] and len(group["members"]) >= 3:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)  # 4th EUI
            return
        if eui in group["members"]:
            group["members"].remove(eui)
        group["members"].append(eui)
        group["block"] = block
        self.note("cfg_write", eui=eui.hex(), members=len(group["members"]))
        if answered:
            self._ack_set(pkt)

    def _group_status_body(self, group):
        bitmap = group.get("bitmap")
        if bitmap is None:
            bitmap = (1 << len(group["members"])) - 1
        return (bytes([group["id"], group["state"], bitmap & 0xFF,
                       group["reason"]]) +
                struct.pack("<H", group["cfg_crc"]) +
                bytes([group["attempts"], 0x00]))

    def _member_live(self, eui):
        """R2 4.9: liveness is checked per fider x phase."""
        entry = self.inventory.get(eui.hex())
        if entry is None or not self.live_enabled:
            return False
        if (entry.fider, entry.phase) in self.live_muted:
            return False
        if entry.last_live_ms is None:
            # A preloaded entry models an already-live feeder.
            return True
        window_ms = 3 * int(self.live_period_s * 1000)
        return (self.now_ms() - entry.last_live_ms) <= window_ms

    def _h_cfg_commit(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 1:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group_id = pkt.data[0]
        if not self.mh_has_key:
            # R2 4.9: a keyless MH rejects 0x24 with ERROR 0x02.
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group = self.group
        if group is None or len(group["members"]) < 3:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if group["state"] in (GROUP_STAGED, GROUP_DELIVERED):
            if group_id != group["id"]:
                if answered:
                    self.reply_error(pkt, sc.ERR_BUSY)
                return
            if answered:  # same id while running: ACK, no new job
                self._ack_set(pkt)
            return
        group["id"] = group_id
        now = self.now_ms()

        # prerequisites (R2 4.9): block Fider_ID vs member feeders,
        # distinct phases, per fider x phase liveness. A failed check
        # stays below RF and reports FAILED with the offending bitmap.
        reason = None
        bitmap = (1 << len(group["members"])) - 1
        block_fider = group["block"][1]
        if not 1 <= block_fider <= 4:
            reason = 2  # whole group flagged
        else:
            phases = {}
            for index, eui in enumerate(group["members"]):
                entry = self.inventory.get(eui.hex())
                if entry is None or entry.fider != block_fider:
                    reason = 2
                    bitmap = 1 << index
                    break
                if entry.phase in phases:
                    reason = 2
                    bitmap = (1 << phases[entry.phase]) | (1 << index)
                    break
                phases[entry.phase] = index
            if reason is None:
                for index, eui in enumerate(group["members"]):
                    if not self._member_live(eui):
                        reason = 1
                        bitmap = 1 << index
                        break
        if self.cfg_force_fail_reason is not None:
            reason = self.cfg_force_fail_reason
            bitmap = (1 << len(group["members"])) - 1
        if reason is not None:
            group["state"] = GROUP_FAILED
            group["reason"] = reason
            group["bitmap"] = bitmap
            self.group_history[group_id] = self._group_status_body(group)
            if answered:
                self._ack_set(pkt)
            self._cfg_notify(self._group_status_body(group),
                        note="cfg FAILED reason=%d bitmap=0x%X" %
                             (reason, bitmap))
            return

        group["bitmap"] = None
        group["state"] = GROUP_STAGED
        group["staged_ms"] = now
        group["attempts"] += 1
        self.group_history[group_id] = self._group_status_body(group)
        self.note("cfg_commit", group_id=group_id)
        if answered:
            self._ack_set(pkt)
        self._cfg_notify(self._group_status_body(group), note="cfg STAGED")

    def _h_cfg_abort(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 1:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group_id = pkt.data[0]
        group = self.group
        if group is None or \
                (group["state"] != GROUP_IDLE and group["id"] != group_id) or \
                group["state"] in (GROUP_FAILED, GROUP_APPLIED):
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group["id"] = group_id
        group["state"] = GROUP_FAILED
        group["reason"] = 10  # USER_ABORT
        group["bitmap"] = None
        self.group_history[group_id] = self._group_status_body(group)
        if answered:
            self._ack_set(pkt)
        self._cfg_notify(self._group_status_body(group),
                    note="cfg FAILED USER_ABORT")

    def _h_cfg_status_get(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or len(pkt.data) != 1:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        group_id = pkt.data[0]
        group = self.group
        body = None
        if group_id == 0 and (group is None or group["state"] == GROUP_IDLE):
            body = bytes(8)  # Pre-COMMIT members have no queryable identity.
        elif group is not None and group["id"] == group_id and \
                group["state"] != GROUP_IDLE:
            body = self._group_status_body(group)
        elif group_id in self.group_history:
            body = self.group_history[group_id]
        if body is None:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if answered:
            self.reply(pkt, body)

    # ------------------------------------------------------------------
    # PWRB family (5.x)
    # ------------------------------------------------------------------

    def build_pwr_summary(self):
        t = dict(self.telemetry) if self.telemetry is not None else {}
        telem_valid = self.telemetry is not None and not self.telemetry_stale
        sira = (self.pwr_sira + 1) & 0xFF
        self.pwr_sira = sira
        body = bytearray(45)
        body[0] = 1                                   # ver
        body[1] = sira
        durum = t.get("durum", 0x03)
        durum2 = t.get("durum2", 0x00)
        if self.telemetry is None:
            durum &= ~0x01                            # no telemetry at all
            t["tlm_yas"] = 255
        elif self.telemetry_stale:
            durum &= ~0x01
        if self.aku_kaynak_yok:
            durum2 &= ~0x0C                           # b3:2 = 0 (kaynak yok)
        body[2] = durum
        body[3] = durum2
        body[4] = t.get("tlm_yas", 0)
        body[5] = t.get("kaynak", 1)
        body[6] = t.get("sarj_fazi", 5)
        body[7] = self.pwr_session
        body[8:10] = struct.pack("<H", t.get("vpv_mv", 12500))
        body[10:12] = struct.pack("<H", t.get("vdc_mv", 0))
        body[12:14] = struct.pack("<h", t.get("ibus_ma", 850))
        body[14:16] = struct.pack("<H", t.get("vsys_mv", 3300))
        body[16:18] = struct.pack("<H", t.get("vbat_mv", 13600))
        body[18:20] = struct.pack("<h", t.get("ibat_ma", 120))
        body[20:22] = struct.pack("<h", t.get("pin_10mw", 106))
        body[22:24] = struct.pack("<h", t.get("pbat_10mw", 16))
        body[24:26] = struct.pack("<h", t.get("psys_10mw", -45))
        body[26:28] = struct.pack("<h", t.get("soc_pm", 870))
        body[28] = 0xFF if self.aku_kaynak_yok else t.get("soh_pct", 96)
        body[29] = t.get("aku_sic", 24)
        body[30] = t.get("kart_sic", 31)
        body[31] = t.get("cap_ah", 7)
        body[32] = t.get("crate_pm", 22)
        body[33] = t.get("lg_adet", 0)
        body[34:38] = struct.pack("<I", self.pwr_alarm_mask)
        body[38] = t.get("soc_capa", 0x03)
        # R2 5.2 battery-life tail: 0xFFFF means unknown.
        if not telem_valid:
            body[39:45] = b"\xFF" * 6
        else:
            if self.aku_kaynak_yok:
                body[39:43] = b"\xFF" * 4
                body[43:45] = struct.pack("<H", t.get("kalan_yil_x10", 50))
            else:
                body[39:41] = struct.pack("<H", t.get("efc", 0))
                body[41:43] = struct.pack("<H", t.get("kalan_efc", 2500))
                body[43:45] = struct.pack("<H", t.get("kalan_yil_x10", 50))
        return bytes(body)

    def send_pwr_summary(self, note=None):
        self.notify(0xE1, self.build_pwr_summary(), note=note)

    def send_pwr_alarm(self, kod, durum, deger0=0, deger1=0, bastirilan=0):
        self.pwr_alarm_sayac = (self.pwr_alarm_sayac + 1) & 0xFF
        body = (bytes([1, self.pwr_alarm_sayac, bastirilan, kod, durum]) +
                struct.pack("<I", self.pwr_alarm_mask) +
                bytes([deger0, deger1]))
        self.notify(0xE3, body, note="pwr_alarm kod=0x%02X durum=%d" %
                    (kod, durum))

    def set_pwr_alarm(self, kod, active):
        """Level alarm edge -> 0xE3 and alarm_aktif mask update."""
        if active:
            self.pwr_alarm_mask |= (1 << kod)
        else:
            self.pwr_alarm_mask &= ~(1 << kod)
        self.send_pwr_alarm(kod, 1 if active else 0)

    def send_son_nefes(self, oturum=None, neden=1):
        oturum = self.pwr_session if oturum is None else oturum
        self.send_pwr_alarm(0x20, 2, deger0=neden, deger1=oturum)

    def _h_pwr_cfg2(self, pkt, answered):
        if pkt.type == sc.TYPE_GET:
            if pkt.data:
                if answered:
                    self.reply_error(pkt, sc.ERR_INVALID_PARAM)
                return
            body = bytearray(23)
            body[0] = 1
            body[1:17] = self.cfg2
            body[17] = self.echo_valid
            body[18] = self.echo_gen
            body[19] = self.m1
            body[20] = self.m2
            body[21:23] = struct.pack("<H", self.c2_red_last)
            if answered:
                self.reply(pkt, body)
            return
        if len(pkt.data) != 16:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self.now_ms() < self.pwr_busy_until_ms:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        gen_okunan = pkt.data[0]
        verilen = struct.unpack("<H", pkt.data[1:3])[0]
        alanlar = pkt.data[3:16]
        if verilen & 0x8003:  # bits 0, 1, 15 forbidden
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if gen_okunan != self.cfg2[1]:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY, bytes([self.cfg2[1]]))
            return
        red = 0
        if verilen & (1 << 9):
            value = alanlar[7]
            if not (value in (0x00, 0xFF) or 20 <= value <= 200):
                red |= 1 << 9
        if verilen & (1 << 13):
            value = alanlar[11]
            if not (value in (0x00, 0xFF) or 7 <= value <= 54):
                red |= 1 << 13
        if verilen & (1 << 14):
            value = alanlar[12]
            if not (1 <= (value & 0x3F) <= 10) or (value & 0x40):
                red |= 1 << 14
        if red:
            self.c2_red_last = red
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM,
                                 struct.pack("<H", red))
            return
        changed = False
        for bit in range(2, 15):
            if verilen & (1 << bit):
                idx = bit - 2
                if self.cfg2[bit] != alanlar[idx]:
                    self.cfg2[bit] = alanlar[idx]
                    changed = True
        if changed:
            self.cfg2[0] = 1  # SURUM becomes 1 on first accepted write
            self.cfg2[1] = (self.cfg2[1] + 1) & 0xFF
            self.echo_valid = 0
            self.echo_due_ms = self.now_ms() + int(
                self.pwr_echo_delay_s * 1000)
        self.note("pwr_cfg2_set", gen=self.cfg2[1], changed=changed)
        if answered:
            self.reply(pkt, bytes([self.cfg2[1]]))

    def _h_pwr_command(self, pkt, answered):
        if pkt.type != sc.TYPE_SET or len(pkt.data) != 2:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        komut, param = pkt.data[0], pkt.data[1]
        if komut > 0x05:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if komut == 0x00:
            was_pending = self.cmd_pending
            pending_komut = self.cmd_state["komut"] if was_pending else 0
            self.cmd_pending = False
            if answered:
                self.reply(pkt, bytes([0x00, 0x00]))
            if was_pending:
                if 0x05 == pending_komut:
                    # R2 5.6: cancel keeps the transmission count, reports
                    # b1 (izleme sürüyor); the separate akıbet notification
                    # (b2 unverifiable / b3 verified) follows after the
                    # akıbet delay.
                    self.cmd_state.update(sonuc=0xFF, durum=0x02)
                    self.akibet_due_ms = self.now_ms() + int(
                        self.pwr_akibet_delay_s * 1000)
                else:
                    self.cmd_state.update(sonuc=0xFF, yayin=0, durum=0x00)
                self._notify_pwr_result("cancelled before applying")
            return
        if self.cmd_pending:
            if answered:
                self.reply_error(pkt, sc.ERR_BUSY)
            return
        sira = (self.cmd_state["sira"] + 1) & 0xFF
        if sira == 0:
            sira = 1
        self.cmd_state = {"komut": komut, "sira": sira, "sonuc": 0xFF,
                          "yayin": 1, "durum": 0x10}
        self.cmd_pending = True
        self.cmd_param = param
        self.cmd_due_ms = self.now_ms() + int(self.pwr_result_delay_s * 1000)
        self.note("pwr_command", komut=komut, sira=sira)
        if answered:
            self.reply(pkt, bytes([komut, sira]))
        self._notify_pwr_result("pwr command started")

    def _notify_pwr_result(self, note):
        state = self.cmd_state
        body = bytes([state["komut"], state["sira"], state["sonuc"],
                      state["yayin"], state["durum"]])
        self.notify(0xE7, body, note=note)

    def _finish_pwr_command(self):
        komut = self.cmd_state["komut"]
        sonuc = self.pwr_result_sonuc
        if komut == 0x05 and self.cmd_param != 0xA5:
            sonuc = 0x02  # PARAM invalid
        self.cmd_state["sonuc"] = sonuc
        self.cmd_pending = False
        self.cmd_state["durum"] &= ~0x10
        if sonuc == 0xFF:
            self.cmd_state["yayin"] = 2  # BQ-13 terminal no-response report.
        if sonuc == 0x00 and komut == 0x05:
            self.cmd_state["durum"] |= 0x08  # b3: applied + verified
        self._notify_pwr_result("pwr_result sonuc=0x%02X" % sonuc)

    def _h_pwr_result_get(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        s = self.cmd_state
        if answered:
            self.reply(pkt, bytes([s["komut"], s["sira"], s["sonuc"],
                                   s["yayin"], s["durum"]]))

    def _h_pwr_telemetry(self, pkt, answered):
        if pkt.type != sc.TYPE_GET or pkt.data:
            if answered:
                self.reply_error(pkt, sc.ERR_INVALID_PARAM)
            return
        if self.telemetry is None:
            if answered:
                self.reply_error(pkt, sc.ERR_NOT_AVAILABLE)
            return
        if answered:
            self.reply(pkt, self.telemetry.get("raw96", b"\x00" * 96))

    # ------------------------------------------------------------------
    # proactive generators and actions
    # ------------------------------------------------------------------

    def start_boot(self):
        self.boot_started = True
        self.boot_next_ms = self.now_ms() + BOOT_FIRST_MS
        self.boot_interval_ms = 1000

    def reboot(self):
        """Simulate MH restart (1.10): triple notify + state reset."""
        self.inventory.clear()
        self.loaded = False
        self.learned_zone = None
        self.time_valid = False
        self.pwr_alarm_mask = 0
        self.proactive_seq = 0xFF  # next notify uses seq 0
        self.live_next_ms = {}
        self.started_ms = self.now_ms()
        self.consume_baseline = None
        self.epoch_due_ms = None       # a restart interrupts the renewal
        self.akibet_due_ms = None      # PWRB chain breaks on restart
        group = self.group
        if group is not None and group["state"] in (GROUP_STAGED,
                                                    GROUP_DELIVERED):
            self.group_history.clear()
            group["state"] = GROUP_FAILED
            group["reason"] = 8  # MODEM_REBOOT
            self.group_history[group["id"]] = self._group_status_body(group)
        elif group is not None and group["state"] == GROUP_IDLE:
            self.group = None  # Pre-COMMIT staging is volatile.
            self.group_history.clear()
        self.notify(0x12, bytes([0xFF, 2]) + struct.pack("<HI", 0, 0) +
                    b"\x00\x00", note="anomaly reset")
        self.notify(0xE3, bytes([1, (self.pwr_alarm_sayac + 1) & 0xFF, 0,
                                 0xFF, 3]) + struct.pack("<I", 0) +
                    b"\x00\x00", note="pwr alarm RESET")
        self.start_boot()

    def _send_boot_notify(self):
        self.notify(0x13, bytes([1]), note="boot_notify")
        self.boot_interval_ms = min(self.boot_interval_ms * 2, BOOT_MAX_MS)
        self.boot_next_ms = self.now_ms() + self.boot_interval_ms

    def inject_trip(self, line, phase, fault_count=2, max_current=0.0):
        entry = self._entry_by_line_phase(line, phase)
        zone = entry.zone if entry else (self.learned_zone or 0)
        src_fp = ((line << 2) | phase) & 0xFF
        body = (bytes([0x04, 1, zone, src_fp, fault_count, 0]) +
                struct.pack("<f", max_current) + struct.pack("<H", 0))
        self.notify(0x10, body, note="trip_notify line=%d phase=%d" %
                    (line, phase))

    def inject_discovery(self, eui, rssi=-40):
        eui = bytes(eui)
        self.notify(0x14, eui + bytes([rssi & 0xFF]),
                    note="discovery eui=%s" % eui.hex())

    def inject_anomaly(self, src=0x01, state=1, win=5, total=42, path=0):
        body = (bytes([src, state]) + struct.pack("<HI", win, total) +
                bytes([path, 0]))
        self.notify(0x12, body, note="anomaly src=0x%02X state=%d" %
                    (src, state))

    def _entry_by_line_phase(self, line, phase):
        for entry in self.inventory.values():
            if entry.fider == line and entry.phase == phase:
                return entry
        return None

    def _build_live_body(self, entry):
        body = bytearray(28)
        body[0:4] = struct.pack("<I", entry.uptime_s)
        body[4] = entry.current_state
        body[5] = entry.fault_count
        body[6:10] = struct.pack("<f", entry.irms_a)
        body[10:14] = struct.pack("<f", entry.v_trip)
        body[14:18] = struct.pack("<f", entry.v_harvest)
        body[18:20] = struct.pack("<h", entry.mcu_temp_c)
        body[20] = entry.rssi_dbm & 0xFF
        body[21] = entry.status_flags
        body[22] = entry.fsm_error
        body[23] = entry.trip_failed
        body[24] = entry.log_pending
        body[26:28] = struct.pack("<H", entry.boot_counter)
        return bytes(body)

    def _send_live(self, entry):
        entry.last_live_ms = self.now_ms()
        entry.live_seq = (entry.live_seq + 1) & 0xFFFFFFFF
        if entry.live_seq == 0:
            entry.live_seq = 1
        entry.uptime_s += int(self.live_period_s)
        body = (bytes([entry.src()]) +
                struct.pack("<I", entry.live_seq) +
                self._build_live_body(entry))
        self.notify(0x11, body, note="live line=%d phase=%d" %
                    (entry.fider, entry.phase))

    # ------------------------------------------------------------------
    # periodic tick
    # ------------------------------------------------------------------

    def tick(self):
        now = self.now_ms()

        if self.boot_started and not self.loaded and \
                self.boot_next_ms is not None and now >= self.boot_next_ms:
            self._send_boot_notify()

        # upload-gap rule: >10 s without a new entry/END restarts BOOT
        if (self.boot_started and not self.loaded and
                self.boot_next_ms is None and
                self.last_inventory_activity_ms is not None and
                now - self.last_inventory_activity_ms > INVENTORY_GAP_MS):
            self.boot_interval_ms = 1000
            self.boot_next_ms = now
            self.note("boot_restarted_gap")

        # live stream
        if self.live_enabled and self.loaded:
            for key, entry in list(self.inventory.items()):
                if (entry.fider, entry.phase) in self.live_muted:
                    continue
                due = self.live_next_ms.get(key)
                if due is None:
                    self.live_next_ms[key] = now + int(
                        self.live_period_s * 1000)
                elif now >= due:
                    self._send_live(entry)
                    self.live_next_ms[key] = now + int(
                        self.live_period_s * 1000)

        # 0x47 repeat while records stay pending
        if self.pending_count() > 0 and self.bell_last_ms is not None and \
                now - self.bell_last_ms >= LOG_BELL_REPEAT_MS:
            self.send_log_bell()

        # config-group timeline
        group = self.group
        if group is not None:
            if group["state"] == GROUP_STAGED:
                if self.cfg_no_deliver:
                    if now - group["staged_ms"] >= self.cfg_delivered_fail_ms:
                        group["state"] = GROUP_FAILED
                        group["reason"] = 5
                        self.group_history[group["id"]] = \
                            self._group_status_body(group)
                        self._cfg_notify(self._group_status_body(group),
                                    note="cfg FAILED TIMEOUT(deliver)")
                elif now - group["staged_ms"] >= self.cfg_delivered_ms:
                    group["state"] = GROUP_DELIVERED
                    group["delivered_ms"] = now
                    self.group_history[group["id"]] = \
                        self._group_status_body(group)
                    self._cfg_notify(self._group_status_body(group),
                                note="cfg DELIVERED")
            elif group["state"] == GROUP_DELIVERED:
                if self.cfg_no_apply:
                    if now - group["delivered_ms"] >= self.cfg_applied_fail_ms:
                        group["state"] = GROUP_FAILED
                        group["reason"] = 6
                        self.group_history[group["id"]] = \
                            self._group_status_body(group)
                        self._cfg_notify(self._group_status_body(group),
                                    note="cfg FAILED PARTIAL_COMMIT")
                elif now - group["delivered_ms"] >= self.cfg_applied_ms:
                    group["state"] = GROUP_APPLIED
                    crc = self.cfg_writable_crc(group["block"])
                    group["cfg_crc"] = (crc + self.cfg_report_crc_offset) \
                        & 0xFFFF
                    self.group_history[group["id"]] = \
                        self._group_status_body(group)
                    self._cfg_notify(self._group_status_body(group),
                                note="cfg APPLIED crc=0x%04X" %
                                group["cfg_crc"])

        # PWR periodic summary
        if self.pwr_enabled:
            if self.pwr_next_ms is None:
                self.pwr_next_ms = now + int(PWR_PERIOD_S * 1000)
            elif now >= self.pwr_next_ms:
                self.send_pwr_summary()
                self.pwr_next_ms = now + int(PWR_PERIOD_S * 1000)

        # PWR echo + pending command completion
        if self.echo_due_ms is not None and now >= self.echo_due_ms:
            self.echo_valid = 1
            self.echo_gen = self.cfg2[1]
            self.m1 = 0
            self.m2 = 0
            self.echo_due_ms = None
            self.note("pwr_echo", gen=self.echo_gen)
        if self.cmd_pending and now >= self.cmd_due_ms:
            self._finish_pwr_command()

        # R2 5.6: separate akıbet notification for a cancelled 0x05
        if self.akibet_due_ms is not None and now >= self.akibet_due_ms:
            self.akibet_due_ms = None
            self.cmd_state["durum"] = 0x08 if self.pwr_cancel_verified \
                else 0x04
            self._notify_pwr_result("0x05 akibet b%d" %
                                    (3 if self.pwr_cancel_verified else 2))

        # R2 4.3: renewal completion -> event 201 + one 120 per ayırıcı
        if self.epoch_due_ms is not None and now >= self.epoch_due_ms:
            feeder = self.epoch_feeder
            duration_ms = int(self.epoch_duration_ms)
            self.epoch_due_ms = None
            phases = [entry.phase for entry in self.inventory.values()
                      if entry.fider == feeder]
            mask = 0
            for phase in phases:
                mask |= 1 << (phase - 1)
            self.add_events(1, code=201, line=feeder, phase=0,
                            fault_count=0, dur_ms=duration_ms,
                            perm_count=mask, mh_record=True)
            for phase in sorted(phases):
                self.add_events(1, code=120, line=feeder, phase=phase,
                                fault_count=0)
            self.note("epoch_done", feeder=feeder, phases=mask)

    # ------------------------------------------------------------------
    # introspection for control/status
    # ------------------------------------------------------------------

    def status(self):
        return {
            "uptime_s": self.uptime_s(),
            "fw": self.fw_label,
            "time_valid": self.time_valid,
            "inventory_loaded": self.loaded,
            "inventory_count": len(self.inventory),
            "inventory": [
                {"eui": eui, "zone": e.zone, "fider": e.fider,
                 "phase": e.phase}
                for eui, e in sorted(self.inventory.items())],
            "ring": {"head": self.head(), "wrap": self.wrap_count(),
                     "tail": self.tail(), "total": self.total,
                     "pending": self.pending_count(),
                     "degraded": self.degraded,
                     "bad_slots": sorted(self.bad_slots)},
            "group": (None if self.group is None else {
                "id": self.group["id"], "state": self.group["state"],
                "members": len(self.group["members"]),
                "reason": self.group["reason"],
                "cfg_crc": self.group["cfg_crc"]}),
            "pwr": {"gen": self.cfg2[1], "echo_valid": self.echo_valid,
                    "echo_gen": self.echo_gen, "m1": self.m1, "m2": self.m2,
                    "alarm_mask": self.pwr_alarm_mask,
                    "cmd": dict(self.cmd_state)},
        }
