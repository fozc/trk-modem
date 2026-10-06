"""CP56Time2a helpers (local-time, Turkey UTC+3 semantics per spec 4.1/4.4)."""

import time


def pack_cp56_ms(ms_of_minute):
    return (ms_of_minute % 60000).to_bytes(2, "little")


def pack_cp56(local_time_struct, invalid=False, summer=False):
    """time.struct_time -> 7-byte CP56Time2a body of 0x07 TIME_SYNC."""
    ms = 0
    minute = local_time_struct.tm_min & 0x3F
    if invalid:
        minute |= 0x80
    hour = local_time_struct.tm_hour & 0x1F
    if summer:
        hour |= 0x80
    day = local_time_struct.tm_mday & 0x1F
    dow = (local_time_struct.tm_wday + 1) & 0x07  # Monday=1, Sunday=7
    day |= dow << 5
    month = local_time_struct.tm_mon & 0x0F
    year = (local_time_struct.tm_year - 2000) & 0x7F
    return bytes([ms & 0xFF, (ms >> 8) & 0xFF, minute, hour, day, month,
                  year])


def unpack_cp56(body):
    """7-byte CP56 body -> dict with field values and validity flags."""
    ms = body[0] | (body[1] << 8)
    minute = body[2] & 0x3F
    invalid = bool(body[2] & 0x80)
    hour = body[3] & 0x1F
    summer = bool(body[3] & 0x80)
    day = body[4] & 0x1F
    dow = (body[4] >> 5) & 0x07
    month = body[5]
    year = 2000 + body[6]
    return {
        "ms": ms, "min": minute, "invalid": invalid, "hour": hour,
        "summer": summer, "day": day, "dow": dow, "month": month,
        "year": year,
    }


def cp56_out_of_range(body):
    """Field-range check per spec 4.4 (used by the MH to reject 0x07)."""
    f = unpack_cp56(body)
    if f["ms"] > 59999 or f["min"] > 59 or f["hour"] > 23:
        return True
    if not 1 <= f["day"] <= 31 or not 1 <= f["month"] <= 12:
        return True
    if f["year"] < 2020 or f["year"] > 2099:
        return True
    return False


def local_now_struct():
    return time.localtime()
