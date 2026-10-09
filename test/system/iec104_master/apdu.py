#!/usr/bin/env python3
"""Minimal IEC 60870-5-104 APDU/APCI/ASDU parser (stdlib only).

Raw bytes captured from the c104 on_receive_raw/on_send_raw callbacks are
turned into structured frames for suite assertions and JSONL evidence.
U/S frames carry no ASDU (asdu=None). Monitored-direction types that the
device actually emits get object-level decoding; unknown types keep the
raw body hex so evidence never loses information.

Wire format reference: IEC 60870-5-2/5-4 (APCI) and 60870-5-101 ASDU
layout as implemented by Application/libiec104 (see iec104_types.h).
"""

import struct

# ---------------------------------------------------------------------------
# Constants (mirror Application/libiec104/iec104_data_types.h)
# ---------------------------------------------------------------------------

TYPE_M_SP_TB_1 = 30           # single point with CP56Time2a
TYPE_M_ME_TF_1 = 36           # short float measured value with CP56Time2a
TYPE_M_EI_NA_1 = 70           # end of initialisation
TYPE_C_IC_NA_1 = 100          # interrogation command
TYPE_C_CS_NA_1 = 103          # clock sync command
TYPE_C_RP_NA_1 = 105          # reset process command

# Canonical U-frame control octets (IEC 60870-5-104 figure 8)
U_STARTDT_ACT = 0x07
U_STARTDT_CON = 0x0B
U_STOPDT_ACT = 0x13
U_STOPDT_CON = 0x23
U_TESTFR_ACT = 0x43
U_TESTFR_CON = 0x83

U_NAMES = {
    U_STARTDT_ACT: "STARTDT_ACT",
    U_STARTDT_CON: "STARTDT_CON",
    U_STOPDT_ACT: "STOPDT_ACT",
    U_STOPDT_CON: "STOPDT_CON",
    U_TESTFR_ACT: "TESTFR_ACT",
    U_TESTFR_CON: "TESTFR_CON",
}

COT_NAMES = {
    3: "SPONT", 4: "INIT", 6: "ACT", 7: "ACT_CON", 8: "DEACT",
    9: "DEACT_CON", 10: "ACT_TERM", 20: "INROG_STATION",
    21: "INROG_G1", 22: "INROG_G2", 23: "INROG_G3", 24: "INROG_G4",
}

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------


def parse_cp56(data, offset):
    """Decode a 7-byte CP56Time2a at offset -> dict (or None on overflow)."""
    if offset + 7 > len(data):
        return None
    msec = data[offset] | (data[offset + 1] << 8)
    minute = data[offset + 2] & 0x3F
    iv = (data[offset + 2] >> 7) & 0x01
    hour = data[offset + 3] & 0x1F
    su = (data[offset + 3] >> 6) & 0x01  # noqa: F841 (reported for evidence)
    day = data[offset + 4] & 0x1F
    month = data[offset + 5] & 0x0F
    year = 2000 + (data[offset + 6] & 0x7F)
    return {"ms": msec, "min": minute, "iv": iv, "hour": hour, "su": su,
            "day": day, "month": month, "year": year}


def qds_flags(qds):
    return {"ov": (qds >> 0) & 1, "bl": (qds >> 4) & 1,
            "sb": (qds >> 5) & 1, "nt": (qds >> 6) & 1,
            "iv": (qds >> 7) & 1}


def siq_flags(siq):
    return {"value": siq & 0x01, "bl": (siq >> 4) & 1,
            "sb": (siq >> 5) & 1, "nt": (siq >> 6) & 1,
            "iv": (siq >> 7) & 1}


def _read_ioa(body, pos):
    return (body[pos] | (body[pos + 1] << 8) |
            (body[pos + 2] << 16)), pos + 3


# ---------------------------------------------------------------------------
# ASDU body decoders (SQ=0 sequential layout only; the device never sets SQ)
# ---------------------------------------------------------------------------


def _decode_objects(type_id, count, body, pos):
    """Decode `count` objects of a known type; None when body is short."""
    objects = []
    for _ in range(count):
        if pos + 3 > len(body):
            return None
        ioa, pos = _read_ioa(body, pos)
        if type_id == TYPE_M_SP_TB_1:
            if pos + 8 > len(body):
                return None
            obj = {"ioa": ioa, "siq": siq_flags(body[pos])}
            pos += 1
            obj["time"] = parse_cp56(body, pos)
            pos += 7
        elif type_id == TYPE_M_ME_TF_1:
            if pos + 12 > len(body):
                return None
            value = int.from_bytes(body[pos:pos + 4], "little",
                                   signed=False)
            obj = {"ioa": ioa,
                   "value": struct.unpack("<f", body[pos:pos + 4])[0],
                   "raw": value}
            pos += 4
            obj["qds"] = qds_flags(body[pos])
            pos += 1
            obj["time"] = parse_cp56(body, pos)
            pos += 7
        elif type_id == TYPE_M_EI_NA_1:
            # IOA + COI (cause of initialisation) byte
            if pos + 1 > len(body):
                return None
            obj = {"ioa": ioa, "coi": body[pos]}
            pos += 1
        elif type_id == TYPE_C_IC_NA_1:
            if pos + 1 > len(body):
                return None
            obj = {"ioa": ioa, "qoi": body[pos]}
            pos += 1
        elif type_id == TYPE_C_CS_NA_1:
            if pos + 7 > len(body):
                return None
            obj = {"ioa": ioa, "time": parse_cp56(body, pos)}
            pos += 7
        elif type_id == TYPE_C_RP_NA_1:
            if pos + 1 > len(body):
                return None
            obj = {"ioa": ioa, "qrp": body[pos]}
            pos += 1
        else:
            obj = {"ioa": ioa, "raw_rest_hex":
                   body[pos:].hex()}
            pos = len(body)
        objects.append(obj)
    return objects


def parse_asdu(body):
    """Decode an ASDU body (without APCI) -> dict, or None if malformed.

    Header layout on this link (verified live against both peers and
    Application/libiec104 make_asdu_header): TYPE, VSQ, COT, OA(1 byte),
    CA(2 LE). The device echoes the requesting master's OA in confirms
    and uses its own OA in spontaneous sends.
    """
    if len(body) < 6:
        return None
    type_id = body[0]
    vsq = body[1]
    sq = (vsq >> 7) & 0x01
    count = vsq & 0x7F
    cot_raw = body[2]
    # Bit layout per IEC 60870-5-101 7.2.2 and the production cot_t
    # (iec104_types.h:101-105): bits 0-5 cause, bit 6 P/N, bit 7 T.
    pn = (cot_raw >> 6) & 0x01
    t_test = (cot_raw >> 7) & 0x01
    cot = cot_raw & 0x3F
    oa = body[3]
    ca = body[4] | (body[5] << 8)
    pos = 6
    asdu = {"type": type_id, "sq": sq, "count": count, "cot": cot,
            "cot_name": COT_NAMES.get(cot, "COT_%d" % cot), "pn": pn,
            "test": t_test, "oa": oa, "ca": ca}
    if sq == 1:
        # Device never sends SQ=1; keep evidence without object decode.
        asdu["body_hex"] = body[pos:].hex()
        return asdu
    objects = _decode_objects(type_id, count, body, pos)
    if objects is None:
        asdu["body_hex"] = body[pos:].hex()
    else:
        asdu["objects"] = objects
    return asdu


# ---------------------------------------------------------------------------
# APDU entry point
# ---------------------------------------------------------------------------


def parse_apdu(data):
    """Parse one complete APDU (starting 0x68) -> dict or None."""
    if len(data) < 6 or data[0] != 0x68:
        return None
    length = data[1]
    if len(data) < 2 + length or length < 4:
        return None
    ctrl = data[2:2 + length]
    frame = {"len": length, "hex": data[:2 + length].hex()}
    if (ctrl[0] & 0x01) == 0:
        # I-frame: VS occupies bits 1-15 of the first control word
        frame["kind"] = "I"
        frame["vs"] = (ctrl[0] | (ctrl[1] << 8)) >> 1
        frame["vr"] = (ctrl[2] | (ctrl[3] << 8)) >> 1
        asdu = parse_asdu(ctrl[4:length])
        if asdu is None:
            frame["asdu"] = None
            frame["asdu_hex"] = ctrl[4:length].hex()
        else:
            frame["asdu"] = asdu
    elif (ctrl[0] & 0x03) == 0x01:
        # S-frame (first control octet is always 0x01)
        frame["kind"] = "S"
        frame["vr"] = (ctrl[2] | (ctrl[3] << 8)) >> 1
    else:
        # U-frame (bit0=1 bit1=1; e.g. 0x07/0x0B/0x43/0x83)
        u = ctrl[0]
        frame["kind"] = "U"
        frame["u"] = U_NAMES.get(u, "U_0x%02X" % u)
    return frame


# ---------------------------------------------------------------------------
# Frame predicates (used by the suite; no c104 dependency so --list and
# the integration wrapper work without the venv)
# ---------------------------------------------------------------------------


def frame_is_u(frame, direction, name):
    """(t, dir, raw, parsed) tuple is direction + named U-frame."""
    return (frame[1] == direction and frame[3] is not None and
            frame[3].get("kind") == "U" and frame[3].get("u") == name)


def frame_is_i_asdu(frame, direction, type_id, cot=None, pn=None):
    """(t, dir, raw, parsed) tuple is an I-frame with the ASDU shape."""
    if frame[1] != direction or frame[3] is None:
        return False
    if frame[3].get("kind") != "I":
        return False
    asdu = frame[3].get("asdu")
    if asdu is None or asdu.get("type") != type_id:
        return False
    if cot is not None and asdu.get("cot") != cot:
        return False
    if pn is not None and asdu.get("pn") != pn:
        return False
    return True
