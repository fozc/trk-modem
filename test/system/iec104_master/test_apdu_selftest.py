#!/usr/bin/env python3
"""Hardware-less golden tests for the suite's APDU parser.

Every vector below was captured live from the device + c104 (runs
20261009-155624/161222) or hand-built from the production wire format.
Run standalone or through the integration wrapper; exit 1 on failure.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import apdu  # noqa: E402

FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print("PASS: %s" % name)
    else:
        FAILURES.append(name)
        print("FAIL: %s %s" % (name, detail))


def parse(hexstr):
    return apdu.parse_apdu(bytes.fromhex(hexstr))


# --- U/S/I classification (live captured) ---------------------------
check("u_startdt_act", parse("68 04 07 00 00 00")["u"] == "STARTDT_ACT")
check("u_startdt_con", parse("68 04 0b 00 00 00")["u"] == "STARTDT_CON")
check("u_testfr_act", parse("68 04 43 00 00 00")["u"] == "TESTFR_ACT")
check("u_testfr_con", parse("68 04 83 00 00 00")["u"] == "TESTFR_CON")
s_frame = parse("68 04 01 00 02 00")
check("s_frame", s_frame["kind"] == "S" and s_frame["vr"] == 1)
# I-frame with VS=1 (first octet 0x02) must NOT be misread as S-frame
gi_con = parse("68 0e 02 00 02 00 64 01 07 00 01 00 00 00 00 14")
check("i_vs1_not_s", gi_con["kind"] == "I" and gi_con["vs"] == 1)

# --- header layout: TYPE VSQ COT OA CA (live captured) --------------
mei = parse("68 0e 00 00 00 00 46 01 04 01 01 00 00 00 00 00")
check("mei_header", mei["asdu"]["type"] == 70 and
      mei["asdu"]["cot"] == 4 and mei["asdu"]["oa"] == 1 and
      mei["asdu"]["ca"] == 1 and mei["asdu"]["pn"] == 0 and
      mei["asdu"]["test"] == 0)
check("mei_object", mei["asdu"]["objects"][0] ==
      {"ioa": 0, "coi": 0})
gi_act = parse("68 0e 00 00 02 00 64 01 06 00 01 00 00 00 00 14")
check("gi_qoi", gi_act["asdu"]["objects"][0]["qoi"] == 20)

# --- P/N and T bits: P/N=bit6, T=bit7 (production cot_t) -------------
# Negative ACT_CON: cause 7 + P/N -> 0x47; must decode pn=1, test=0.
neg = parse("68 0e 02 00 02 00 64 01 47 00 01 00 00 00 00 14")
check("pn_bit_negative_confirm", neg["asdu"]["pn"] == 1 and
      neg["asdu"]["test"] == 0 and neg["asdu"]["cot"] == 7,
      "got pn=%s test=%s" % (neg["asdu"]["pn"], neg["asdu"]["test"]))
# Test-mode frame: cause 6 + T bit -> 0x86; pn=0, test=1.
tst = parse("68 0e 00 00 02 00 64 01 86 00 01 00 00 00 00 14")
check("t_bit_test_mode", tst["asdu"]["pn"] == 0 and
      tst["asdu"]["test"] == 1 and tst["asdu"]["cot"] == 6)

# --- object decoders (hand-built, lengths from the wire) ------------
value = struct.pack("<f", 137.5)
frame = parse("68 19 04 00 02 00 24 01 03 01 01 00 06 04 00 " +
              value.hex(" ") + " 10 " + "11 22 33 44 55 66 05")
obj = frame["asdu"]["objects"][0]
check("m_me_tf_1", obj["ioa"] == 1030 and obj["value"] == 137.5 and
      obj["qds"]["bl"] == 1)
frame = parse("68 15 04 00 02 00 1e 01 03 01 01 00 12 04 00 50 "
              "11 22 33 44 55 66 05")
obj = frame["asdu"]["objects"][0]
check("m_sp_tb_1", obj["ioa"] == 1042 and obj["siq"]["value"] == 0 and
      obj["siq"]["bl"] == 1 and obj["siq"]["nt"] == 1)

# --- CP56 round trip (SU = bit 7 of the hour byte) -------------------
# 0x44 = 0100_0100: hour=4, su=0;  0xC4 = 1100_0100: hour=4, su=1
cp56 = apdu.parse_cp56(bytes.fromhex("11 22 33 44 55 66 05"), 0)
check("cp56_decode", cp56["iv"] == 0 and cp56["su"] == 0 and
      cp56["hour"] == 4 and cp56["day"] == 21 and cp56["year"] == 2005)
cp56_su = apdu.parse_cp56(bytes.fromhex("11 22 33 c4 55 66 05"), 0)
check("cp56_su_bit7", cp56_su["su"] == 1 and cp56_su["hour"] == 4)

# --- malformed / degenerate frames must degrade, never crash ---------
# SQ=1 ASDU: no objects key, body kept as hex evidence
sq1 = parse("68 0b 04 00 02 00 24 81 03 01 01 00 06 03 00")
check("sq1_no_objects", sq1["asdu"]["sq"] == 1 and
      "objects" not in sq1["asdu"] and "body_hex" in sq1["asdu"])
# truncated ASDU (< 6 bytes): asdu None, raw hex preserved
short = parse("68 06 04 00 02 00 24 01")
check("short_asdu_none", short["kind"] == "I" and
      short["asdu"] is None and "asdu_hex" in short)
# object body shorter than declared count: fall back to body_hex
trunc = parse("68 0d 04 00 02 00 24 02 03 01 01 00 06 03 00 00")
check("truncated_objects_hex", "objects" not in trunc["asdu"] and
      "body_hex" in trunc["asdu"])
# unknown type id: IOA extracted once, rest kept raw
unk = parse("68 0f 04 00 02 00 2a 01 03 01 01 00 06 04 00 aa bb")
check("unknown_type_raw", unk["asdu"]["type"] == 42 and
      unk["asdu"]["objects"][0]["ioa"] == 1030 and
      unk["asdu"]["objects"][0]["raw_rest_hex"] == "aabb")

print("\n%d/%d OK" % (
    18 - len(FAILURES), 18))
raise SystemExit(1 if FAILURES else 0)
