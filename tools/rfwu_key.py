#!/usr/bin/env python3
"""Prepare the private common RFWU key header. Never prints key material.
Author: Fatih Ozcan
        fatihozcan@gmail.com
"""
import argparse
from pathlib import Path
import secrets

ROOT = Path(__file__).resolve().parents[1]


def prepare_key(key_path, header_path, create=False):
    if not key_path.exists():
        if not create:
            raise ValueError("RFWU key missing; restore the common key or use --create once")
        key_path.parent.mkdir(parents=True, exist_ok=True)
        with key_path.open("xb") as output:
            output.write(secrets.token_bytes(16))
    key = key_path.read_bytes()
    if len(key) != 16 or not any(key):
        raise ValueError("RFWU key must contain 16 nonzero-total bytes")
    values = ", ".join(f"0x{value:02X}U" for value in key)
    header = "/*\n * rfwu_product_key.h\n *\n"
    header += " *      Author: Fatih Ozcan\n"
    header += " *              fatihozcan@gmail.com\n *\n"
    header += " * Private generated RFWU product key. Do not commit.\n */\n"
    header += "#ifndef RFWU_PRODUCT_KEY_H\n#define RFWU_PRODUCT_KEY_H\n"
    header += "#define RFWU_PRODUCT_KEY_BYTES { " + values + " }\n#endif\n"
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(header, encoding="ascii", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--create", action="store_true")
    parser.add_argument("--key", type=Path, default=ROOT / "keys/rfwu_key.bin")
    parser.add_argument("--header", type=Path,
                        default=ROOT / "Application/rfwu_product_key.h")
    args = parser.parse_args()
    try:
        prepare_key(args.key, args.header, args.create)
    except ValueError as error:
        parser.error(str(error))
    print("Private RFWU header prepared; key retained unchanged.")
