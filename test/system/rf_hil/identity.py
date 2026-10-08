"""Firmware artifact and installed-DUT identity checks; no serial access."""
import hashlib
import re
import struct
from pathlib import Path

IMAGE_PATTERN = (r"RTU image: crc=0x([0-9A-Fa-f]{8}) size=(\d+) "
                 r"git=(\S*) profile=([01])")


class IdentityError(RuntimeError):
    """The selected firmware/profile cannot be proven for this case."""


def read_image_identity(filename):
    path = Path(filename).resolve()
    data = path.read_bytes()
    if len(data) < 256 or data[:4] != b"*EFW" or data[4] != 1 or data[5] != 0x60:
        raise IdentityError("Expected an application EFW v1 artifact")
    size, crc, stored_size = struct.unpack_from("<III", data, 12)
    if not size or not stored_size or len(data) != 256 + stored_size:
        raise IdentityError("Invalid EFW image size/CRC metadata")
    try:
        git_commit = data[24:31].rstrip(b"\0").decode("ascii")
    except UnicodeDecodeError as error:
        raise IdentityError("Invalid EFW build commit") from error
    if not git_commit:
        raise IdentityError("Missing EFW build commit")
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(),
                app_size=size, app_crc=crc, git_commit=git_commit)


def parse_dut_identity(text):
    match = re.search(IMAGE_PATTERN, text)
    if not match:
        raise IdentityError("Missing installed DUT image/profile response")
    return dict(app_crc=int(match[1], 16), app_size=int(match[2]),
                git_commit=match[3], transport_profile=int(match[4]))


def verify_dut_identity(observed, expected, profile):
    if expected is None or profile is None:
        raise IdentityError("Firmware artifact and expected profile are required")
    for name in ("app_crc", "app_size", "git_commit"):
        if observed[name] != expected[name]:
            raise IdentityError("Installed DUT %s differs from selected EFW" % name)
    if observed["transport_profile"] != profile:
        raise IdentityError("Installed DUT transport profile differs from selection")


def result_exit_code(results):
    verdicts = [value[0] for value in results.values()]
    if not verdicts or any(value not in ("PASS", "FAIL") for value in verdicts):
        return 2
    return 1 if "FAIL" in verdicts else 0
