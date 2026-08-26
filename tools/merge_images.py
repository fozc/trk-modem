#!/usr/bin/env python3
"""
merge_images.py - Merge two Intel HEX images into a single flashable file.

Typical use: combine the bootloader hex and the application hex of a
project into one file that can be flashed in a single pass with
STM32CubeProgrammer, STM32CubeIDE or ST-LINK Utility.

All load addresses come from the hex files themselves - the tool holds no
device-specific constants, so it works for any MCU and any memory layout.
Overlapping data between the inputs is rejected; address gaps between the
images (e.g. between the end of the bootloader and the app base) are left
out of the output, so those regions stay erased (0xFF) on the chip.

Each input is sanity-checked as a Cortex-M image: the reset entry point
read from its vector table must point inside that image itself.

Usage:
  python tools/merge_images.py \
      ../troika-smart-breaker-modem-bootlodaer/Debug/troika-smart-breaker-modem-bootloader.hex \
      Release/troika-smart-breaker-modem.hex \
      -o combined_flash.hex
"""

import argparse
import os
import struct
import sys

SEGMENT_GRANULARITY = 4  # warn if a segment is not 4-byte aligned


class Segment:
    """A contiguous run of bytes placed at a fixed flash address."""

    def __init__(self, addr, data, source=None):
        self.addr = addr
        self.data = data
        self.source = source

    @property
    def end(self):
        return self.addr + len(self.data)


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Merge two Intel HEX images (e.g. bootloader + "
                    "application) into a single flashable Intel HEX file.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('inputs', nargs=2, metavar='HEX',
                        help='Input Intel HEX files, e.g. bootloader.hex '
                             'app.hex (order does not matter)')
    parser.add_argument('-o', '--output', required=True,
                        help='Output Intel HEX file (e.g. combined_flash.hex)')
    return parser.parse_args()


# ---------------------------------------------------------------------------
# Intel HEX reader
# ---------------------------------------------------------------------------

def read_hex_segments(path):
    """Parse an Intel HEX file into address-sorted Segments."""
    chunks = []  # (address, bytes) per data record, coalesced afterwards
    upper = 0

    with open(path, 'r') as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(':'):
                fail(f"{path}:{line_no}: missing ':' start code")
            try:
                rec = bytes.fromhex(line[1:])
            except ValueError:
                fail(f"{path}:{line_no}: invalid hex characters")
            if len(rec) < 5:
                fail(f"{path}:{line_no}: record too short")
            count, addr, rec_type = rec[0], struct.unpack('>H', rec[1:3])[0], rec[3]
            payload = rec[4:4 + count]
            if len(payload) != count:
                fail(f"{path}:{line_no}: length mismatch")
            if ((sum(rec) & 0xFF) != 0):
                fail(f"{path}:{line_no}: bad checksum")

            if rec_type == 0x00:  # data
                chunks.append((upper + addr, payload))
            elif rec_type == 0x01:  # end of file
                break
            elif rec_type == 0x04:  # extended linear address
                if count != 2:
                    fail(f"{path}:{line_no}: bad ELA record")
                upper = struct.unpack('>H', payload)[0] << 16
            elif rec_type == 0x05:  # start linear address (entry point)
                pass
            else:
                fail(f"{path}:{line_no}: unsupported record type 0x{rec_type:02X}")

    if not chunks:
        fail(f"{path}: no data records found")
    chunks.sort(key=lambda c: c[0])

    segments = []
    for addr, data in chunks:
        if segments and addr == segments[-1].end:
            segments[-1].data += data
        else:
            segments.append(Segment(addr, bytearray(data), path))
    return segments


def check_vector_table(name, segments):
    """Sanity-check the image looks like a valid Cortex-M image.

    The second vector-table word is the reset entry point; it must point
    inside the image itself. This rejects truncated or corrupt files.
    """
    first = segments[0]
    if len(first.data) < 8:
        fail(f"{name}: too small to contain a vector table")
    entry = struct.unpack_from('<I', first.data, 4)[0]
    entry &= ~1  # clear Thumb bit
    image_end = max(s.end for s in segments)
    if not (first.addr <= entry < image_end):
        fail(f"{name}: reset entry 0x{entry:08X} is outside the image "
             f"0x{first.addr:08X}..0x{image_end:08X} - truncated or "
             f"corrupt file?")


# ---------------------------------------------------------------------------
# Intel HEX writer
# ---------------------------------------------------------------------------

def hex_record(addr, rec_type, payload):
    rec = bytearray()
    rec.append(len(payload))
    rec += struct.pack('>H', addr)
    rec.append(rec_type)
    rec += payload
    checksum = (-sum(rec)) & 0xFF
    rec.append(checksum)
    return ':' + rec.hex().upper()


def write_intel_hex(path, segments, line_bytes=16):
    lines = []
    upper = None
    for seg in segments:
        offset = 0
        while offset < len(seg.data):
            chunk = seg.data[offset:offset + line_bytes]
            addr = seg.addr + offset
            if upper != addr >> 16:
                upper = addr >> 16
                lines.append(hex_record(0, 0x04,
                                        struct.pack('>H', upper)))
            lines.append(hex_record(addr & 0xFFFF, 0x00, chunk))
            offset += len(chunk)
    lines.append(hex_record(0, 0x01, b''))
    lines.append('')

    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(lines))


# ---------------------------------------------------------------------------
# Merge
# ---------------------------------------------------------------------------

def coalesce(segments):
    """Sort and merge adjacent/overlapping segments."""
    segments = sorted(segments, key=lambda s: s.addr)
    merged = []
    for seg in segments:
        if seg.addr % SEGMENT_GRANULARITY != 0:
            print(f"  WARNING: segment at 0x{seg.addr:08X} is not "
                  f"{SEGMENT_GRANULARITY}-byte aligned")
        if merged and seg.addr <= merged[-1].end:
            if seg.addr < merged[-1].end:
                fail(f"overlapping data at 0x{seg.addr:08X} from "
                     f"{seg.source}, but {merged[-1].source} already "
                     f"covers up to 0x{merged[-1].end:08X}")
            merged[-1].data += seg.data
        else:
            merged.append(seg)
    return merged


def main():
    args = parse_args()

    for path in args.inputs:
        if not os.path.isfile(path):
            fail(f"file not found: {path}")

    segments = []
    for path in args.inputs:
        image = coalesce(read_hex_segments(path))
        check_vector_table(path, image)
        segments += image
    merged = coalesce(segments)

    print("Merged image layout:")
    prev_end = None
    for seg in merged:
        gap = ""
        if prev_end is not None and seg.addr > prev_end:
            gap = f"  (gap 0x{seg.addr - prev_end:X} bytes left erased)"
        print(f"  0x{seg.addr:08X} .. 0x{seg.end:08X}  "
              f"{len(seg.data):6d} bytes{gap}")
        prev_end = seg.end

    write_intel_hex(args.output, merged)
    print(f"Intel HEX  : {args.output}")


if __name__ == '__main__':
    main()
