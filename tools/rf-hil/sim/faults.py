"""Wire-level fault injection for the RF hub simulator TX path.

The engine sits between the hub model (ScpPacket) and the serial writer:
it can delay, drop, corrupt, split and prefix/suffix frames, mirroring
the fault classes the DUT must tolerate (spec 2.2 / 6.1). Behaviour-level
faults (busy, degraded rings, FAILED groups...) live on HubModel knobs.
"""

import time

from . import scp_codec as sc


class FaultEngine:
    def __init__(self, write_bytes, now_ms=None, on_note=None):
        self.write_bytes = write_bytes    # callable(bytes) -> None
        self.now_ms = now_ms or (lambda: int(time.monotonic() * 1000))
        self.on_note = on_note or (lambda text: None)

        self.delayed = []                 # [(due_ms, bytes)]
        self.drop_next_replies = 0
        self.drop_cmd = None              # restrict drops to this cmd
        self.corrupt_next = None          # "crc" | "len"
        self.corrupt_cmd = None
        self.split_next_gap_ms = None
        self.noise_before = None          # bytes or None
        self.noise_after = None
        self.delay_next_ms = 0
        self.delay_all_ms = 0

    # -- injection knobs (control channel / scenario) --

    def arm(self, action, **args):
        if action == "drop_next":
            self.drop_next_replies = int(args.get("count", 1))
            self.drop_cmd = args.get("cmd")
        elif action == "delay_next":
            self.delay_next_ms = int(args.get("ms", 0))
        elif action == "delay_all":
            self.delay_all_ms = int(args.get("ms", 0))
        elif action == "corrupt_next":
            self.corrupt_next = args.get("kind", "crc")
        elif action == "split_next":
            self.split_next_gap_ms = int(args.get("gap_ms", 150))
        elif action == "noise_before":
            text = args.get("text", "MH servis konsolu: test satiri\r\n")
            self.noise_before = text.encode("ascii", "replace")
        elif action == "noise_after":
            text = args.get("text", "MH log: bilgi\r\n")
            self.noise_after = text.encode("ascii", "replace")
        else:
            raise ValueError("unknown fault action %r" % action)
        self.on_note("fault armed: %s %r" % (action, args))

    def clear(self):
        self.drop_next_replies = 0
        self.corrupt_next = None
        self.split_next_gap_ms = None
        self.noise_before = None
        self.noise_after = None
        self.delay_next_ms = 0
        self.delay_all_ms = 0
        self.delayed = []
        self.on_note("faults cleared")

    # -- TX path --

    def send_packet(self, pkt, kind):
        if kind != "notify" and self.drop_next_replies > 0 and \
                (self.drop_cmd is None or pkt.cmd == self.drop_cmd):
            self.drop_next_replies -= 1
            self.on_note("fault: dropped reply cmd=0x%02X seq=%d" %
                         (pkt.cmd, pkt.seq))
            return

        logical = bytearray(sc.encode_packet(pkt))

        if self.corrupt_next is not None and kind != "notify":
            kind_fault = self.corrupt_next
            self.corrupt_next = None
            if kind_fault == "crc":
                logical[-1] ^= 0x5A
            elif kind_fault == "len":
                logical[6] ^= 0x03  # break ~LEN, CRC recomputed below
            self.on_note("fault: corrupted %s of cmd=0x%02X" %
                         (kind_fault, pkt.cmd))

        frame = b"\x00" + sc.cobs_encode(bytes(logical)) + b"\x00"

        delay = self.delay_all_ms
        if self.delay_next_ms and kind != "notify":
            delay += self.delay_next_ms
            self.delay_next_ms = 0
            self.on_note("fault: delayed reply by %d ms" % delay)

        now = self.now_ms()

        if self.noise_before is not None:
            self.delayed.append((now, self.noise_before))
            self.noise_before = None

        if self.split_next_gap_ms is not None and kind != "notify":
            gap = self.split_next_gap_ms
            self.split_next_gap_ms = None
            cut = max(2, len(frame) // 2)
            self.delayed.append((now, frame[:cut]))
            self.delayed.append((now + gap, frame[cut:]))
            self.on_note("fault: split frame, %d ms gap" % gap)
        else:
            self.delayed.append((now + delay, frame))

        if self.noise_after is not None:
            self.delayed.append((now + delay + 5, self.noise_after))
            self.noise_after = None

        self.delayed.sort(key=lambda item: item[0])

    def pump(self):
        """Flush due frames; call from the main loop each iteration."""
        if not self.delayed:
            return
        now = self.now_ms()
        due = [item for item in self.delayed if item[0] <= now]
        if due:
            self.delayed = [item for item in self.delayed
                            if item[0] > now]
            for _, chunk in due:
                self.write_bytes(chunk)

    def pending(self):
        return len(self.delayed)
