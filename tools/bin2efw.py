#!/usr/bin/env python3
"""
bin2efw.py - Convert a binary file to EFW firmware format (v2).

EFW Header (128 bytes, packed):
  magic            : uint32  (big-endian, 0x2A454657 = "*EFW")
  efw_file_version : uint8   (0x02)
  file_type        : uint8
  device_type      : uint8
  device_model     : uint8
  app_version      : 4 x uint8 (major, minor, patch, extra)  [efw_version_t]
  app_size         : uint32  (little-endian, RAW/expanded firmware size)
  app_crc          : uint32  (little-endian, CRC-32 of RAW app data)
  signature_r      : 32 bytes (ECDSA-P256 signature R component)
  short_commit_hash: 8 bytes  (git short hash, ASCII, zero-padded)
  day              : uint8
  month            : uint8
  year             : uint16  (little-endian)
  hour             : uint8
  minute           : uint8
  second           : uint8
  auth_type        : uint8   (0x02 = ECDSA-P256)
  signature_s      : 32 bytes (ECDSA-P256 signature S component)
  encryption_type  : uint8   (0x00=none, 0x01=AES-128-CTR)
  iv               : 16 bytes (AES initialization vector)
  compression_type : uint8   (offset 117; 0x00=NONE, 0x01=LZMA1)
  stored_size      : uint32  (offset 118, LE; payload bytes stored after header)
  lzma_props       : 5 bytes (offset 122; props byte + dict_size LE32)
  reserved         : 1 byte  (offset 127; zero in v2)

v2 payload semantics:
  - NONE      : stored payload == raw app data (stored_size == app_size).
  - LZMA1     : stored payload == LZMA1(raw) or AES-128-CTR(key, iv, LZMA1(raw)).
  - Signature covers header(r/s=0) + STORED payload (as written to SPI).
  - app_size/app_crc always describe the RAW (expanded) firmware; the
    expanded CRC is checked on device during preflight/install, not at RX.

-H / --version-header ile kullanım:
  version.h dosyasındaki aşağıdaki #define'lar otomatik parse edilir:
    DEVICE_TYPE      -> --device-type
    DEVICE_MODEL     -> --device-model
    APP_TYPE         -> --file-type (-t)
    VERSION_MAJOR    -> version major
    VERSION_MINOR    -> version minor
    VERSION_PATCH    -> version patch
    VERSION_EXTRA    -> version extra

  Örnek version.h içeriği:
    #define DEVICE_TYPE      (100)
    #define DEVICE_MODEL     (1)
    #define APP_TYPE         (1)
    #define VERSION_MAJOR    (1)
    #define VERSION_MINOR    (2)
    #define VERSION_PATCH    (0)
    #define VERSION_EXTRA    (0)

  Komut satırından açıkça verilen değerler (-v, -t, --device-type, --device-model)
  her zaman version.h'den okunan değerlerin önüne geçer.

Usage:
  # ECDSA-P256 signing (required)
  python bin2efw.py firmware.bin --sign-key keys/private_key.pem

  # version.h + signing + LZMA compression
  python bin2efw.py firmware.bin -H ../Application/version.h --sign-key keys/private_key.pem --compress

  # Bootloader, v2.1.0.0, device-type=3
  python bin2efw.py firmware.bin --sign-key keys/private_key.pem -t 1 -v 2.1.0.0 --device-type 0x03


"""

import argparse
import hashlib
import lzma
import os
import re
import struct
import subprocess
import sys
from datetime import datetime

EFW_MAGIC = b'*EFW'             # 0x2A454657 big-endian on wire
EFW_FILE_VERSION = 0x02

# v2: signature covers header (with r/s zeroed) + STORED payload.
# These offsets must match EFW_SIG_R_OFFSET / EFW_SIG_S_OFFSET in efw.h.
SIG_R_OFFSET = 20
SIG_S_OFFSET = 68

# v2 payload fields (must match efw.h offsets; reserve area of v1)
COMPRESSION_TYPE_OFFSET = 117
STORED_SIZE_OFFSET      = 118
LZMA_PROPS_OFFSET       = 122

# Authentication type code (must match efw.h)
EFW_AUTH_TYPE_ECDSA_P256  = 0x02

# Encryption type codes (must match efw.h)
EFW_ENCRYPTION_NONE       = 0x00
EFW_ENCRYPTION_AES128_CTR = 0x01

# Compression type codes (must match efw.h)
EFW_COMPRESSION_NONE = 0x00
EFW_COMPRESSION_LZMA1 = 0x01

# LZMA profile - MUST match the device decoder compile settings
# (LZMA_EMB_DICT_SIZE=4096, LZMA_EMB_MAX_LC=1, LZMA_EMB_MAX_LP=1).
LZMA_DICT_SIZE = 4096
LZMA_LC = 1
LZMA_LP = 1
LZMA_PB = 1

FILE_TYPE_MAP = {
    1: 0x40,  # Bootloader  (2 << 5)
    2: 0x60,  # Application (3 << 5)
    3: 0x20,  # Config      (1 << 5)
}

FILE_TYPE_NAMES = {
    1: "Bootloader",
    2: "Application",
    3: "Config",
}

_HMAC_SIZE     = 32
_SIG_R_SIZE    = 32
_SIG_S_SIZE    = 32
_COMMIT_HASH_SIZE = 8

# Packed header format — mirrors C struct __packed efw_raw_fields_t
#   4s    : magic                (4B, raw bytes "*EFW")
#   B     : efw_file_version     (1B)
#   B     : file_type            (1B)
#   B     : device_type          (1B)
#   B     : device_model         (1B)
#   BBBB  : major, minor, patch, extra  (efw_version_t, 4B)
#   I     : app_size             (4B, little-endian, RAW size)
#   I     : app_crc              (4B, little-endian, RAW CRC)
#   32s   : signature_r          (32B)
#   8s    : short_commit_hash    (8B)
#   B     : day                  (1B)
#   B     : month                (1B)
#   H     : year                 (2B, little-endian)
#   B     : hour                 (1B)
#   B     : minute               (1B)
#   B     : second               (1B)
#   B     : auth_type            (1B)
#   32s   : signature_s          (32B)
#   B     : encryption_type      (1B, 0x00=none, 0x01=AES-128-CTR)
#   16s   : iv                   (16B, AES initialization vector)
#   B     : compression_type     (1B, offset 117; 0x00=none, 0x01=LZMA1)
#   I     : stored_size          (4B, LE, offset 118)
#   5s    : lzma_props           (5B, offset 122)
#   B     : reserved             (1B, offset 127; zero)

EFW_HEADER_SIZE = 128
_V1_FIELDS_FMT = struct.Struct('<4sBBBBBBBBII32s8sBBHBBBB32sB16s')
assert _V1_FIELDS_FMT.size == COMPRESSION_TYPE_OFFSET
_HDR_FMT = struct.Struct('<4sBBBBBBBBII32s8sBBHBBBB32sB16sBI5sB')

HEADER_SIZE = _HDR_FMT.size
assert HEADER_SIZE == EFW_HEADER_SIZE, \
    f"Header size mismatch: expected {EFW_HEADER_SIZE}, got {HEADER_SIZE}"


# ---------------------------------------------------------------------------
# CRC-32 (bit-by-bit-fast, matches efw_crc.c exactly)
#   Width=32, Poly=0x04C11DB7, XorIn=0xFFFFFFFF,
#   ReflectIn=True, XorOut=0xFFFFFFFF, ReflectOut=True
# ---------------------------------------------------------------------------

def _crc_reflect(data, data_len):
    ret = data & 0x01
    for i in range(1, data_len):
        data >>= 1
        ret = (ret << 1) | (data & 0x01)
    return ret


def efw_crc32(data):
    crc = 0xFFFFFFFF
    for byte_val in data:
        i = 0x01
        while i & 0xFF:
            bit = 1 if (crc & 0x80000000) else 0
            if byte_val & i:
                bit = 1 - bit
            crc = (crc << 1) & 0xFFFFFFFF
            if bit:
                crc ^= 0x04C11DB7
            i <<= 1
        crc &= 0xFFFFFFFF
    return _crc_reflect(crc, 32)


# ---------------------------------------------------------------------------
# Firmware ECDSA-P256 signing
# ---------------------------------------------------------------------------

def fw_ecdsa_sign(private_key_path: str, data: bytes):
    """Sign data with ECDSA-P256 (SHA-256 digest). Returns (r, s) as 32-byte each."""
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    from cryptography.hazmat.primitives.serialization import load_pem_private_key

    with open(private_key_path, 'rb') as f:
        private_key = load_pem_private_key(f.read(), password=None)

    # Sign produces DER-encoded signature
    der_sig = private_key.sign(data, ec.ECDSA(hashes.SHA256()))

    # Decode DER to (r, s) integers
    r_int, s_int = utils.decode_dss_signature(der_sig)

    # Convert to fixed 32-byte big-endian
    r_bytes = r_int.to_bytes(32, byteorder='big')
    s_bytes = s_int.to_bytes(32, byteorder='big')

    return r_bytes, s_bytes


# ---------------------------------------------------------------------------
# LZMA1 profile helpers (compress-then-encrypt pipeline)
# ---------------------------------------------------------------------------

def lzma1_compress(data: bytes):
    """Compress with the fixed device profile. Returns (props5, stream).

    The streaming compressor writes the .lzma (ALONE) header with unknown
    uncompressed size -> the stream ALWAYS ends with an end marker, which the
    device decoder requires for its finish() size check.
    """
    comp = lzma.LZMACompressor(format=lzma.FORMAT_ALONE, filters=[{
        'id': lzma.FILTER_LZMA1,
        'preset': 9,
        'dict_size': LZMA_DICT_SIZE,
        'lc': LZMA_LC,
        'lp': LZMA_LP,
        'pb': LZMA_PB,
    }])
    blob = comp.compress(data) + comp.flush()
    if len(blob) < 13:
        raise RuntimeError("LZMA encoder produced no output")
    props = blob[:5]
    stream = blob[13:]

    # Profile contract proof: props byte is (pb*5+lp)*9+lc, dict_size LE32.
    pb = props[0] // 45
    lp = (props[0] % 45) // 9
    lc = props[0] % 9
    dict_size = struct.unpack('<I', props[1:5])[0]
    if (lc, lp, pb) != (LZMA_LC, LZMA_LP, LZMA_PB) or dict_size != LZMA_DICT_SIZE:
        raise RuntimeError(
            f"LZMA props outside device profile: lc={lc} lp={lp} pb={pb} "
            f"dict={dict_size} (need {LZMA_LC}/{LZMA_LP}/{LZMA_PB}/{LZMA_DICT_SIZE})")
    # Unknown-size encoding (0xFFFF...) guarantees the end marker is present.
    if blob[5:13] != b'\xff' * 8:
        raise RuntimeError("LZMA encoder wrote a known size - end marker not guaranteed")
    return props, stream


def lzma1_expand(props: bytes, raw_size: int, stream: bytes) -> bytes:
    """Expand a v2 LZMA1 stream back to raw bytes (self-verification).

    Decodes with the encoder's own framing (unknown size + end marker),
    which is exactly the byte layout produced by lzma1_compress().
    """
    alone = props + b'\xff' * 8 + stream
    out = lzma.decompress(alone, format=lzma.FORMAT_ALONE)
    if len(out) != raw_size:
        raise RuntimeError(f"LZMA expand size mismatch: {len(out)} != {raw_size}")
    return out


# ---------------------------------------------------------------------------
# Header builder
# ---------------------------------------------------------------------------

def get_git_short_hash():
    """Return the first 8 chars of the current git HEAD commit hash, or empty."""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--short=8', 'HEAD'],
            capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            return result.stdout.strip()[:8]
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return ''


def build_header(device_type, device_model, file_type_byte,
                 major, minor, patch, extra,
                 app_size, app_crc, sig_r: bytes,
                 commit_hash: str, build_time: datetime,
                 auth_type: int, sig_s: bytes,
                 encryption_type: int = EFW_ENCRYPTION_NONE,
                 iv: bytes = b'\x00' * 16,
                 compression_type: int = EFW_COMPRESSION_NONE,
                 stored_size: int = 0,
                 lzma_props: bytes = b'\x00' * 5):
    assert len(sig_r) == _SIG_R_SIZE
    assert len(sig_s) == _SIG_S_SIZE
    assert len(iv) == 16
    assert len(lzma_props) == 5
    hash_bytes = commit_hash.encode('ascii')[:_COMMIT_HASH_SIZE]
    hash_bytes = hash_bytes.ljust(_COMMIT_HASH_SIZE, b'\x00')
    return _HDR_FMT.pack(
        EFW_MAGIC,
        EFW_FILE_VERSION, file_type_byte, device_type, device_model,
        major, minor, patch, extra,
        app_size,
        app_crc,
        sig_r,
        hash_bytes,
        build_time.day, build_time.month, build_time.year,
        build_time.hour, build_time.minute, build_time.second,
        auth_type,
        sig_s,
        encryption_type,
        iv,
        compression_type,
        stored_size,
        lzma_props,
        0,
    )


# ---------------------------------------------------------------------------
# Argument helpers
# ---------------------------------------------------------------------------

def parse_version_header(header_path):
    """Parse a C version.h file and extract #define values."""
    defines = {}
    pattern = re.compile(r'#define\s+(\w+)\s+\(?(\d+)\)?')
    with open(header_path, 'r') as f:
        for line in f:
            m = pattern.match(line.strip())
            if m:
                defines[m.group(1)] = int(m.group(2))
    return defines


def parse_version(version_str):
    parts = version_str.split('.')
    if len(parts) != 4:
        raise argparse.ArgumentTypeError(
            f"Version format: major.minor.patch.extra  (got: {version_str})")
    try:
        values = [int(p) for p in parts]
    except ValueError:
        raise argparse.ArgumentTypeError(
            f"Version parts must be integers: {version_str}")
    for v in values:
        if not 0 <= v <= 255:
            raise argparse.ArgumentTypeError(
                f"Each version part must be 0-255: {version_str}")
    return tuple(values)


def parse_uint8(value_str):
    v = int(value_str, 0)
    if not 0 <= v <= 255:
        raise argparse.ArgumentTypeError(f"Value must be 0-255: {value_str}")
    return v


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Convert a binary file to EFW firmware format.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  %(prog)s firmware.bin --sign-key keys/private_key.pem
  %(prog)s firmware.bin -H ../Application/version.h --sign-key keys/private_key.pem
  %(prog)s firmware.bin --sign-key keys/private_key.pem -t 1 -v 2.1.0.0 --device-type 0x03
""")

    parser.add_argument('input',
                        help='Input binary file')
    parser.add_argument('-o', '--output',
                        help='Output EFW file (default: <input>.efw)')
    parser.add_argument('-t', '--file-type',
                        type=int, choices=[1, 2, 3], default=None,
                        help='1=bootloader, 2=application (default), 3=config')
    parser.add_argument('-v', '--version',
                        type=parse_version, default=None,
                        help='Firmware version major.minor.patch.extra (default: 1.0.0.0)')
    parser.add_argument('--device-type',
                        type=parse_uint8, default=None,
                        help='Device type 0-255 (default: 0x00)')
    parser.add_argument('--device-model',
                        type=parse_uint8, default=None,
                        help='Device model 0-255 (default: 0x00)')
    parser.add_argument('-H', '--version-header',
                        default=None,
                        help='Path to version.h to auto-extract version/device info')
    parser.add_argument('--sign-key', required=True,
                        help='Path to ECDSA-P256 private key PEM file for signing')
    parser.add_argument('--compress', action='store_true',
                        help='Compress payload with LZMA1 (device profile: '
                             'dict=4096, lc=1, lp=1, pb=1). Falls back to '
                             'uncompressed storage if compression is not beneficial.')
    parser.add_argument('--encrypt-key', default=None,
                        help='Path to AES-128 key file (16 bytes raw) for encryption')

    args = parser.parse_args()

    # --- parse version.h if provided --------------------------------------
    if args.version_header:
        if not os.path.isfile(args.version_header):
            print(f"Error: version header not found: {args.version_header}",
                  file=sys.stderr)
            sys.exit(1)
        defines = parse_version_header(args.version_header)
        if args.version is None:
            args.version = (
                defines.get('VERSION_MAJOR', 1),
                defines.get('VERSION_MINOR', 0),
                defines.get('VERSION_PATCH', 0),
                defines.get('VERSION_EXTRA', 0),
            )
        if args.device_type is None:
            args.device_type = defines.get('DEVICE_TYPE', 0x00)
        if args.device_model is None:
            args.device_model = defines.get('DEVICE_MODEL', 0x00)
        if args.file_type is None:
            args.file_type = defines.get('APP_TYPE', 2)

    # --- apply hardcoded defaults for anything still unset ----------------
    if args.version is None:
        args.version = (1, 0, 0, 0)
    if args.device_type is None:
        args.device_type = 0x00
    if args.device_model is None:
        args.device_model = 0x00
    if args.file_type is None:
        args.file_type = 2

    # --- input file --------------------------------------------------------
    if not os.path.isfile(args.input):
        print(f"Error: file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    with open(args.input, 'rb') as f:
        app_data = f.read()

    if len(app_data) == 0:
        print("Error: input file is empty", file=sys.stderr)
        sys.exit(1)

    # --- output path -------------------------------------------------------
    # Default name: <input_base>_vMAJOR.MINOR.PATCH_YYYYMMDD_HHMMSS_hash.efw
    if args.output is None:
        major, minor, patch, extra = args.version
        commit_hash = get_git_short_hash()
        build_time = datetime.now()
        out_dir = os.path.dirname(args.input) or '.'
        base, _ = os.path.splitext(os.path.basename(args.input))
        date_str = build_time.strftime('%Y%m%d')
        time_str = build_time.strftime('%H%M%S')
        hash_str = commit_hash if commit_hash else 'nohash'
        args.output = os.path.join(
            out_dir,
            f"{base}_v{major}.{minor}.{patch}_{date_str}_{time_str}_{hash_str}.efw")
    else:
        major, minor, patch, extra = args.version
        commit_hash = get_git_short_hash()
        build_time = datetime.now()

    # --- compute CRC & compress & encrypt & sign -------------------------
    app_size = len(app_data)
    app_crc = efw_crc32(app_data)

    # Pipeline order is fixed: compress FIRST, then encrypt. Encrypted data
    # does not compress, so the reverse order would waste the transfer gain.
    compression_type = EFW_COMPRESSION_NONE
    lzma_props = b'\x00' * 5
    stage_data = app_data          # after compression (plain compressed)

    if args.compress:
        lzma_props, stream = lzma1_compress(app_data)
        if len(stream) < app_size:
            compression_type = EFW_COMPRESSION_LZMA1
            stage_data = stream
        else:
            print(f"Warning: compression not beneficial "
                  f"(stored would be {len(stream)} >= raw {app_size}) - "
                  f"storing uncompressed (NONE)")
            # Canonical NONE carries no props: reset what the encoder
            # produced, otherwise the header violates the format contract
            # (device-side verify rejects props on a NONE payload).
            lzma_props = b'\x00' * 5

    # Encryption (optional) - applied to the (possibly compressed) payload
    encryption_type = EFW_ENCRYPTION_NONE
    iv = b'\x00' * 16
    payload_data = stage_data      # data written after header (as stored)

    if args.encrypt_key:
        if not os.path.isfile(args.encrypt_key):
            print(f"Error: encrypt key not found: {args.encrypt_key}",
                  file=sys.stderr)
            sys.exit(1)
        with open(args.encrypt_key, 'rb') as f:
            aes_key = f.read()
        if len(aes_key) != 16:
            print(f"Error: AES key must be exactly 16 bytes, "
                  f"got {len(aes_key)}", file=sys.stderr)
            sys.exit(1)

        from cryptography.hazmat.primitives.ciphers import (
            Cipher, algorithms, modes,
        )
        iv = os.urandom(16)
        cipher = Cipher(algorithms.AES(aes_key), modes.CTR(iv))
        encryptor = cipher.encryptor()
        payload_data = encryptor.update(stage_data) + encryptor.finalize()
        encryption_type = EFW_ENCRYPTION_AES128_CTR

    stored_size = len(payload_data)

    # ECDSA signs: header(with r/s=0) + STORED payload - the exact bytes the
    # device re-reads from SPI flash before accepting the package.
    if not os.path.isfile(args.sign_key):
        print(f"Error: sign key not found: {args.sign_key}", file=sys.stderr)
        sys.exit(1)

    file_type_byte = FILE_TYPE_MAP[args.file_type]

    # Build a provisional header with zeroed r/s for signing
    zero_sig = b'\x00' * 32
    signing_header = build_header(
        device_type=args.device_type,
        device_model=args.device_model,
        file_type_byte=file_type_byte,
        major=major, minor=minor, patch=patch, extra=extra,
        app_size=app_size,
        app_crc=app_crc,
        sig_r=zero_sig,
        sig_s=zero_sig,
        commit_hash=commit_hash,
        build_time=build_time,
        auth_type=EFW_AUTH_TYPE_ECDSA_P256,
        encryption_type=encryption_type,
        iv=iv,
        compression_type=compression_type,
        stored_size=stored_size,
        lzma_props=lzma_props,
    )

    # Sign: header(r/s=0) + stored payload
    to_sign = signing_header + payload_data
    sig_r, sig_s = fw_ecdsa_sign(args.sign_key, to_sign)

    # --- build final header with real signature & write -----------------
    header = build_header(
        device_type=args.device_type,
        device_model=args.device_model,
        file_type_byte=file_type_byte,
        major=major, minor=minor, patch=patch, extra=extra,
        app_size=app_size,
        app_crc=app_crc,
        sig_r=sig_r,
        commit_hash=commit_hash,
        build_time=build_time,
        auth_type=EFW_AUTH_TYPE_ECDSA_P256,
        sig_s=sig_s,
        encryption_type=encryption_type,
        iv=iv,
        compression_type=compression_type,
        stored_size=stored_size,
        lzma_props=lzma_props,
    )

    # Atomic publish (review): write to a temp name and replace only after
    # the self-checks pass, so an interrupted/failed run can never leave an
    # invalid package under the FINAL name.
    tmp_output = args.output + '.tmp'
    with open(tmp_output, 'wb') as f:
        f.write(header)
        f.write(payload_data)

    # --- self-verification: raw byte equality + signature ----------------
    check_data = payload_data
    if encryption_type == EFW_ENCRYPTION_AES128_CTR:
        from cryptography.hazmat.primitives.ciphers import (
            Cipher, algorithms, modes,
        )
        with open(args.encrypt_key, 'rb') as f:
            aes_key = f.read()
        dec = Cipher(algorithms.AES(aes_key), modes.CTR(iv)).decryptor()
        check_data = dec.update(payload_data) + dec.finalize()
    if compression_type == EFW_COMPRESSION_LZMA1:
        check_data = lzma1_expand(lzma_props, app_size, check_data)
    if check_data != app_data:
        print("Error: self-verification FAILED - expanded payload does not "
              "match raw input; output file is invalid", file=sys.stderr)
        os.remove(tmp_output)
        sys.exit(1)

    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
    with open(args.sign_key, 'rb') as f:
        private_key = load_pem_private_key(f.read(), password=None)
    try:
        # Device stores raw r||s (uECC format); cryptography verify() needs DER.
        der_sig = utils.encode_dss_signature(
            int.from_bytes(sig_r, 'big'), int.from_bytes(sig_s, 'big'))
        private_key.public_key().verify(
            der_sig, to_sign, ec.ECDSA(hashes.SHA256()))
    except Exception:
        print("Error: self-verification FAILED - signature does not verify; "
              "output file is invalid", file=sys.stderr)
        os.remove(tmp_output)
        sys.exit(1)

    os.replace(tmp_output, args.output)

    # --- summary ----------------------------------------------------------
    print(f"EFW created : {args.output}")
    print(f"  Format    : v{EFW_FILE_VERSION}")
    print(f"  File type : {FILE_TYPE_NAMES[args.file_type]} (0x{file_type_byte:02X})")
    print(f"  Version   : {major}.{minor}.{patch}.{extra}")
    print(f"  Device    : type=0x{args.device_type:02X}  model=0x{args.device_model:02X}")
    print(f"  Raw size  : {app_size} bytes")
    print(f"  App CRC   : 0x{app_crc:08X} (raw)")
    print(f"  Auth      : ECDSA-P256")
    if compression_type == EFW_COMPRESSION_LZMA1:
        print(f"  Codec     : LZMA1 (dict={LZMA_DICT_SIZE}, lc={LZMA_LC}, "
              f"lp={LZMA_LP}, pb={LZMA_PB})")
        print(f"  Stored    : {stored_size} bytes "
              f"(ratio {stored_size / app_size:.3f}, "
              f"{100.0 * stored_size / app_size:.1f}% of raw)")
    else:
        print(f"  Codec     : None")
        print(f"  Stored    : {stored_size} bytes")
    if encryption_type == EFW_ENCRYPTION_AES128_CTR:
        print(f"  Encryption: AES-128-CTR (applied after compression)")
        print(f"  IV        : {iv.hex()}")
    else:
        print(f"  Encryption: None")
    print(f"  Sig R     : {sig_r.hex()}")
    print(f"  Sig S     : {sig_s.hex()}")
    print(f"  Git hash  : {commit_hash if commit_hash else '(none)'}")
    print(f"  Build time: {build_time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  Total     : {HEADER_SIZE + stored_size} bytes")
    print(f"  Self-check: PASSED (raw byte equality + ECDSA verify)")


if __name__ == '__main__':
    main()
