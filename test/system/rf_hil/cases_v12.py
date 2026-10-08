#!/usr/bin/env python3
"""Plan v1.2 additions: console-triggered PWRB flows, group negatives,
inner-CRC corruption, short-gap split and vendor-band ignore.

Imported and registered by cases.py (same registry decorator).
"""

import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cases import (case, wait_upload_complete, refresh, _left_zero,   # noqa: E402
                   any_retry, ConsoleChecks)


@case("e2_cfg_crc_mismatch",
      "APPLIED crc bilincli bozulur: DUT MISMATCH durumuna dusmeli",
      scenario={"setup": [{"do": "set_knob",
                           "name": "cfg_report_crc_offset", "value": 1}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def e2(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send("admin")
    time.sleep(0.3)
    ctx.console.send("rf cfg-apply 1 11")
    applied = _wait_group_state(ctx, 3, timeout_s=45)
    tc = refresh(ctx)
    body = bytes.fromhex(applied["data_hex"])
    reported = int.from_bytes(body[4:6], "little")
    writes = [bytes.fromhex(r["data_hex"])[8:]
              for r in tc.rx_requests(0x22)]
    from sim import scp_codec as sc
    computed = sc.crc16_ccitt_false(writes[-1][3:57])
    tc.expect(reported != computed,
              "bildirilen crc hala dogru: 0x%04X" % reported)
    state = ctx.console.send_and_wait(
        "rf cfg-state", r"state=REPORT_MISMATCH", timeout_s=10)
    tc.expect(state, "DUT did not report MISMATCH")
    ctx.evidence.append(state[1])
    body2 = ctx.console.drain(2.5)
    ctx.evidence += body2[:15]
    return "reported crc 0x%04X != computed 0x%04X (MISMATCH)" % (
        reported, computed)


@case("e3_cfg_abort",
      "STAGED sonrasi rf cfg-abort: 0x26 + FAILED sebep 10",
      scenario={"setup": [{"do": "set_knob", "name": "cfg_delivered_ms",
                           "value": 8000},
                          {"do": "set_knob", "name": "cfg_applied_ms",
                           "value": 30000}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def e3(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send("admin")
    time.sleep(0.3)
    ctx.console.send("rf cfg-apply 1 12")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        tc = refresh(ctx)
        if tc.rx_requests(0x24):
            break
        time.sleep(1.5)
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x24), "COMMIT gelmedi")
    ctx.console.send("rf cfg-abort")
    time.sleep(4.0)
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x26), "0x26 CFG_ABORT telde gorulmedi")
    failed = _wait_group_state(ctx, 4, timeout_s=10)
    body = bytes.fromhex(failed["data_hex"])
    tc.expect(body[3] == 10, "sebep USER_ABORT(10) degil: %d" % body[3])
    ctx.console.send("rf cfg-state")
    body2 = ctx.console.drain(2.5)
    ctx.evidence += body2[:12]
    return "abort verified (reason=10)"


@case("g3_pwr_cfg2_flow",
      "pwrboard cfg-read/cfg-set: 0xE5 GET -> SET (yalniz b13) -> yeni "
      "GEN -> ~20 s sonra yanki dogrulamasi",
      scenario={"setup": [{"do": "set_telemetry", "value": {"cap_ah": 30}}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def g3(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    got = None
    for _ in range(2):
        got = ctx.console.send_and_wait("pwrboard cfg-read",
                                        r"Request started|Not started",
                                        timeout_s=25)
        if got:
            break
    assert got, "pwrboard cfg-read ciktisi alinamadi"
    time.sleep(1.0)
    ctx.console.send("pwrboard cfg-set capacity 30")
    time.sleep(30.0)  # echo + E1 cap_ah match (spec 5.4 dogrulamasi)
    got2 = None
    for _ in range(2):
        got2 = ctx.console.send_and_wait("pwrboard cfg-read",
                                         r"Request started|Not started",
                                         timeout_s=25)
        if got2:
            break
    assert got2, "ikinci cfg-read ciktisi alinamadi"
    ctx.console.send("pwrboard control")
    body2 = ctx.console.drain(2.5)
    ctx.evidence += body2[:20]
    tc = refresh(ctx)
    gets = tc.rx_requests(0xE5)
    sets = [r for r in gets if bytes.fromhex(r["data_hex"])]
    tc.expect(len(gets) >= 2,
              "en az iki 0xE5 GET beklenir (yanki dogrulama)")
    tc.expect(len(sets) == 1, "tam bir 0xE5 SET beklenir: %d" % len(sets))
    data = bytes.fromhex(sets[0]["data_hex"])
    mask = int.from_bytes(data[1:3], "little")
    tc.expect(mask == (1 << 13),
              "SET maskesi yalniz b13 olmali: 0x%04X" % mask)
    acks = [r for r in tc.tx_frames(0xE5)
            if r.get("kind") == "reply" and r["data_hex"]]
    tc.expect(acks, "SET ACK yok")
    new_gen = bytes.fromhex(acks[-1]["data_hex"])[0]
    tc.expect(new_gen >= 1, "GEN ilerlemeli: %d" % new_gen)
    replies = [r for r in tc.tx_frames(0xE5)
               if r.get("kind") == "reply" and len(r["data_hex"]) == 46]
    tc.expect(replies, "23 B GET ACK yok")
    last = bytes.fromhex(replies[-1]["data_hex"])
    tc.expect(last[17] == 1, "yanki_gecerli=1 degil: %d" % last[17])
    tc.expect(last[18] == new_gen, "yanki_gen=%d != GEN=%d" %
              (last[18], new_gen))
    tc.expect(last[19] & 0xDF == 0, "m1 ret biti var: 0x%02X" % last[19])
    tc.expect(last[20] & 0x7F == 0, "m2 ret biti var: 0x%02X" % last[20])
    return "E5 flow: gen=%d echo verified" % new_gen


@case("g4_pwr_command",
      "kapasite yaz+yanki dogrula -> battery-replaced: 0xE6 [05 A5] -> "
      "ACK [05 SIRA] -> 0xE7 sonuc 0x00 + DURUM b3",
      scenario={"setup": [{"do": "set_telemetry", "value": {"cap_ah": 40}}],
                "steps": [{"at_s": 0.5, "do": "boot"}]})
def g4(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    # 0x05 is refused while settings are uncertain (spec 5.4): write a
    # valid capacity first and let the echo verify, then command.
    ctx.console.send("pwrboard cfg-read")
    time.sleep(1.0)
    ctx.console.send("pwrboard cfg-set capacity 40")
    time.sleep(30.0)
    # verification GET is issued by cfg-read; without it the settings
    # machine stays in "waiting for verification" and refuses 0x05
    ctx.console.send_and_wait("pwrboard cfg-read",
                              r"Request started|cfg|kapasite",
                              timeout_s=10)
    time.sleep(3.0)
    got = ctx.console.send_and_wait("pwrboard battery-replaced",
                                    r"Request started|Not started",
                                    timeout_s=25)
    assert got, "battery-replaced ciktisi alinamadi"
    assert "Request started" in got[1], "komut reddedildi: %s" % got[1]
    body = ctx.console.drain(2.0)
    ctx.evidence += body[:10]
    time.sleep(4.0)  # sim result delay 2 s + margin
    got2 = ctx.console.send_and_wait("pwrboard result-get",
                                     r"Request started|result|sonuc|durum",
                                     timeout_s=10)
    assert got2, "result-get ciktisi alinamadi"
    ctx.console.send("pwrboard control")
    body2 = ctx.console.drain(2.5)
    ctx.evidence += body2[:15]
    tc = refresh(ctx)
    cmds = tc.rx_requests(0xE6)
    tc.expect(cmds, "0xE6 istegi yok")
    data = bytes.fromhex(cmds[0]["data_hex"])
    tc.expect(data[0] == 0x05 and data[1] == 0xA5,
              "KOMUT/PARAM 05/A5 degil: %s" % data.hex())
    acks = [r for r in tc.tx_frames(0xE6)
            if r.get("kind") == "reply" and r["data_hex"]]
    tc.expect(acks, "0xE6 ACK yok")
    ack_body = bytes.fromhex(acks[0]["data_hex"])
    tc.expect(ack_body[0] == 0x05, "ACK komutu 05 degil")
    results = [r for r in tc.tx_frames(0xE7)
               if r.get("kind") == "notify" and r["data_hex"]]
    tc.expect(results, "0xE7 bildirimi yok")
    res = bytes.fromhex(results[-1]["data_hex"])
    tc.expect(res[2] == 0x00, "SONUC 0x00 degil: 0x%02X" % res[2])
    tc.expect(res[4] & 0x08,
              "DURUM b3 (dogrulandi) kurulmadi: 0x%02X" % res[4])
    return "battery-replaced verified (sira=%d)" % ack_body[1]


@case("d6_bad_record_crc",
      "0x44 govdesinde ic-CRC'si bozuk kayit: DUT basarili prefix "
      "otesine consume gondermemeli",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 11.0, "do": "corrupt_record_slot",
                           "index": 1},
                          {"at_s": 12.0, "do": "add_events", "count": 2,
                           "code": 1, "line": 1}]})
def d6(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    time.sleep(30.0)
    tc = refresh(ctx)
    ranges = tc.rx_requests(0x44)
    tc.expect(ranges, "0x44 cekme yok")
    consumes = [struct.unpack("<H", bytes.fromhex(r["data_hex"]))[0]
                for r in tc.rx_requests(0x46)]
    # slot 0 saglam, slot 1 bozuk: basarili prefix yalniz slot 0
    tc.expect(all(c <= 1 for c in consumes),
              "bozuk kayit otesine consume gonderildi: %s" % consumes)
    bell = tc.tx_frames(0x47)
    return "consumes=%s (prefix korunuyor), bells=%d" % (consumes,
                                                         len(bell))


@case("h2_split_short_gap",
      "Yarim cerceve + <100 ms bosluk: DUT parse etmeli, tekrar olmamali",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 12.0, "do": "add_events", "count": 1,
                           "code": 1, "line": 1}]})
def h2(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    ctx.sim.call(do="fault", action="split_next", args={"gap_ms": 40})
    time.sleep(25.0)
    tc = refresh(ctx)
    retries = any_retry(tc)
    tc.expect(not retries,
              "kisa bosluklu bolunme tekrar dogurdu: %s" % retries)
    tc.expect(tc.rx_requests(0x46), "olay cekme tamamlanmadi")
    tc.expect(_left_zero(tc), "left=0'a ulasilamadi")
    return "short-gap split parsed without retry"


@case("h7_vendor_band_notify",
      "Uretici bandi (0xF5) istek-disi SET: DUT sessizce yok saymali",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def h7(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    ctx.console.mark()
    ctx.sim.call(do="raw_notify", cmd=0xF5, data_hex="0102")
    time.sleep(6.0)
    out = ConsoleChecks(ctx.console.new_lines())
    out.assert_not_contains(r"ERROR|Error|HARDFAULT|HardFault")
    # link health: live frames (or any RF RX) keep flowing after the
    # ignored vendor-band frame; the periodic GET_STATUS may fall
    # outside this short window when the 60 s event poll intervenes
    frames = out.rf_rx_frames()
    if not frames:
        raise AssertionError("yok sayma sonrasi RF aktivitesi gorulmedi")
    return "vendor-band frame ignored; %d RF frames after" % len(frames)


def _wait_group_state(ctx, state, timeout_s=30):
    """Wait for a 0x21 notify carrying the requested group state."""
    deadline = time.monotonic() + timeout_s
    found = None
    while time.monotonic() < deadline:
        tc = refresh(ctx)
        for r in tc.tx_frames(0x21):
            if r.get("kind") == "notify" and r["data_hex"]:
                if bytes.fromhex(r["data_hex"])[1] == state:
                    found = r
        if found:
            return found
        time.sleep(1.5)
    raise AssertionError("group state %d not reported" % state)
