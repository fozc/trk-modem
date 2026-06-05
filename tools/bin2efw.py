#!/usr/bin/env python3
"""
bin2efw.py - Convert a binary file to EFW firmware format.

EFW Header (128 bytes, packed):
  magic            : uint32  (big-endian, 0x2A454657 = "*EFW")
  efw_file_version : uint8   (0x02)
  file_type        : uint8
  device_type      : uint8
  device_model     : uint8
  app_version      : 4 x uint8 (major, minor, patch, extra)  [efw_version_t]
  app_size         : uint32  (little-endian)
  app_crc          : uint32  (little-endian, CRC-32 of app data)
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

  # ECDSA-P256 signing + AES-128-CTR encryption (optional)
  python bin2efw.py firmware.bin --sign-key keys/private_key.pem --encrypt-key keys/aes_key.bin

  # version.h + signing
  python bin2efw.py firmware.bin -H ../Application/version.h --sign-key keys/private_key.pem

  # Bootloader, v2.1.0.0, device-type=3
  python bin2efw.py firmware.bin --sign-key keys/private_key.pem -t 1 -v 2.1.0.0 --device-type 0x03


"""

import argparse
import hashlib
import re
import struct
import subprocess
import sys
import os
from datetime import datetime

EFW_MAGIC = b'*EFW'             # 0x2A454657 big-endian on wire
EFW_FILE_VERSION = 0x02

# Authentication type code (must match efw.h)
EFW_AUTH_TYPE_ECDSA_P256  = 0x02

# Encryption type codes (must match efw.h)
EFW_ENCRYPTION_NONE       = 0x00
EFW_ENCRYPTION_AES128_CTR = 0x01
EFW_AES_IV_SIZE           = 16

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
#   I     : app_size             (4B, little-endian)
#   I     : app_crc              (4B, little-endian)
#   32s   : signature_r          (32B, v1: hmac, v2: ecdsa.r)
#   8s    : short_commit_hash    (8B)
#   B     : day                  (1B)
#   B     : month                (1B)
#   H     : year                 (2B, little-endian)
#   B     : hour                 (1B)
#   B     : minute               (1B)
#   B     : second               (1B)
#   B     : auth_type            (1B, v2 field)
#   32s   : signature_s          (32B, v2: ecdsa.s)
#   Xs    : reserve              (padding to EFW_HEADER_SIZE)
#   H     : year                 (2B, little-endian)
#   B     : hour                 (1B)
#   B     : minute               (1B)
#   B     : second               (1B)
#   Xs    : reserve              (padding to EFW_HEADER_SIZE)

EFW_HEADER_SIZE = 128
_RAW_FIELDS_FMT = struct.Struct('<4sBBBBBBBBII32s8sBBHBBBB32sB16s')
_RESERVE_SIZE   = EFW_HEADER_SIZE - _RAW_FIELDS_FMT.size
_HDR_FMT = struct.Struct(f'<4sBBBBBBBBII32s8sBBHBBBB32sB16s{_RESERVE_SIZE}s')

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
# AES-128-CTR encryption
# ---------------------------------------------------------------------------

def fw_aes128_ctr_encrypt(key_path: str, data: bytes):
    """Encrypt data with AES-128-CTR. Returns (ciphertext, iv)."""
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

    with open(key_path, 'rb') as f:
        aes_key = f.read()
    if len(aes_key) != 16:
        print(f"ERROR: AES key must be 16 bytes, got {len(aes_key)}", file=sys.stderr)
        sys.exit(1)

    iv = os.urandom(EFW_AES_IV_SIZE)
    cipher = Cipher(algorithms.AES(aes_key), modes.CTR(iv))
    encryptor = cipher.encryptor()
    ciphertext = encryptor.update(data) + encryptor.finalize()
    return ciphertext, iv


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
                 iv: bytes = None):
    assert len(sig_r) == _SIG_R_SIZE
    assert len(sig_s) == _SIG_S_SIZE
    if iv is None:
        iv = b'\x00' * EFW_AES_IV_SIZE
    assert len(iv) == EFW_AES_IV_SIZE
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
        b'\x00' * _RESERVE_SIZE,
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

    parser.add_argument('input', nargs='?', default=None,
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
    parser.add_argument('--sign-key', default=None,
                        help='Path to ECDSA-P256 private key PEM file for signing')
    parser.add_argument('--encrypt-key', default=None,
                        help='Path to AES-128 raw key file (16 bytes) for CTR encryption')

    args = parser.parse_args()

    # --- validate required args for EFW conversion ------------------------
    if args.input is None:
        parser.error("the following arguments are required: input")
    if args.sign_key is None:
        parser.error("the following arguments are required: --sign-key")

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

    # --- compute CRC, optionally encrypt, then sign ------------------------
    app_size = len(app_data)
    app_crc = efw_crc32(app_data)  # CRC is always over plaintext

    # Encrypt firmware data if --encrypt-key is provided
    encryption_type = EFW_ENCRYPTION_NONE
    iv = b'\x00' * EFW_AES_IV_SIZE
    output_data = app_data

    if args.encrypt_key:
        if not os.path.isfile(args.encrypt_key):
            print(f"Error: encrypt key not found: {args.encrypt_key}", file=sys.stderr)
            sys.exit(1)
        output_data, iv = fw_aes128_ctr_encrypt(args.encrypt_key, app_data)
        encryption_type = EFW_ENCRYPTION_AES128_CTR

    # Sign the data that will be stored on SPI flash (ciphertext if encrypted)
    if not os.path.isfile(args.sign_key):
        print(f"Error: sign key not found: {args.sign_key}", file=sys.stderr)
        sys.exit(1)
    sig_r, sig_s = fw_ecdsa_sign(args.sign_key, output_data)

    # --- build & write ----------------------------------------------------
    file_type_byte = FILE_TYPE_MAP[args.file_type]

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
    )

    with open(args.output, 'wb') as f:
        f.write(header)
        f.write(output_data)

    # --- summary ----------------------------------------------------------
    enc_label = "AES-128-CTR" if encryption_type == EFW_ENCRYPTION_AES128_CTR else "None"
    print(f"EFW created : {args.output}")
    print(f"  File type : {FILE_TYPE_NAMES[args.file_type]} (0x{file_type_byte:02X})")
    print(f"  Version   : {major}.{minor}.{patch}.{extra}")
    print(f"  Device    : type=0x{args.device_type:02X}  model=0x{args.device_model:02X}")
    print(f"  App size  : {app_size} bytes")
    print(f"  App CRC   : 0x{app_crc:08X} (plaintext)")
    print(f"  Auth      : ECDSA-P256")
    print(f"  Sig R     : {sig_r.hex()}")
    print(f"  Sig S     : {sig_s.hex()}")
    print(f"  Encryption: {enc_label}")
    if encryption_type == EFW_ENCRYPTION_AES128_CTR:
        print(f"  IV        : {iv.hex()}")
    print(f"  Git hash  : {commit_hash if commit_hash else '(none)'}")
    print(f"  Build time: {build_time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  Total     : {HEADER_SIZE + app_size} bytes")


if __name__ == '__main__':
    main()
