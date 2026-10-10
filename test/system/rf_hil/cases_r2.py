#!/usr/bin/env python3
"""SCP R2 (022555bf) device cases: the BOLATeX teslim 09.10 "repeat in
R2" list, bench-observable subset.

Mapping to the teslim answer table (R2 ile tekrarlanacak sivlar):
  r2a bootless setup (BQ-17)      r2b consume via 0x48 (item 4)
  r2c event 142 classification    r2d src_eui_hash mismatch (item 1/2)
  r2e live Boot_Counter (item 3)  r2f service wipe 138/3 (item 7)
  r2g 0x2A renewal records (12)   r2h keyless MH (item 11)

free_slots, store format/state loss, wrap overflow and the reset consume
lock are covered hardware-less in test_sim_selftest.py R2SimAdditions.
"""

import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cases import case, wait_upload_complete, refresh, settle_live  # noqa: E402


def wait_drained(ctx, timeout_s=75):
    """Wait until the DUT consumed every pending record."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        state = ctx.sim.call(do="status")["state"]["ring"]
        if 0 == state["pending"]:
            return refresh(ctx)
        refresh(ctx)  # keeps the sim alive across watchdog restarts
        time.sleep(0.5)
    raise AssertionError("records were not drained in %d s" % timeout_s)


def read_fault_count(ctx, line, kind):
    """fltlog dump total for one phase; kind is TEMPORARY/PERMANENT.

    Retries once: under verbose RF logging the shell reply can be lost
    in a LIVE_DATA burst (bench race, not firmware behaviour)."""
    reply = None
    for attempt in range(2):
        reply = ctx.console.send_and_wait(
            "fltlog dump %d" % line,
            r"Phase 1 - %s FAULTS \(total: (\d+)," % kind, timeout_s=10)
        if reply:
            break
        ctx.console.drain(1.0)
    assert reply, "%s fault count missing" % kind
    ctx.evidence.append(reply[1])
    return int(reply[2].group(1))


def login(ctx):
    ctx.console.send("su admin")
    time.sleep(0.3)
    ctx.console.send("admin")
    time.sleep(0.3)


def notify_frames(tc, cmd):
    return [r for r in tc.tx_frames(cmd) if r.get("kind") == "notify"]


def frames_after(tc, cmd, t_ms):
    """Notify frames strictly newer than the given trace timestamp."""
    return [r for r in notify_frames(tc, cmd) if r.get("t_ms", 0) > t_ms]


def wait_renewal_records(ctx, timeout_s=40):
    """Wait until the pending 0x2A renewal wrote its 201 + 120 records."""
    start_total = ctx.sim.call(do="status")["state"]["ring"]["total"]
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        state = ctx.sim.call(do="status")["state"]["ring"]
        if state["total"] >= start_total + 4:
            return refresh(ctx)
        refresh(ctx)  # keeps the sim alive
        time.sleep(0.5)
    raise AssertionError("renewal records (201 + 3 x 120) not written")


def read_record_codes(tc):
    """event codes from every 0x42/0x44 reply body in the trace."""
    codes = []
    for request_cmd in (0x42, 0x44):
        for record in tc.tx_frames(request_cmd):
            if "reply" != record.get("kind"):
                continue
            data = bytes.fromhex(record["data_hex"])
            for offset in range(0, len(data) - 59, 60):
                codes.append(data[offset + 7])
    return codes


@case("r2a_bootless_setup",
      "RTU reboot + BOOT gelmeden kurulum: fw kimliginden scp_major (BQ-17)",
      scenario={"noboot": True,
                "setup": [{"do": "set_knob", "name": "fw_label",
                           "value": "022555bf"}]})
def r2a(ctx):
    from cases_findings import dut_reset  # late import avoids a cycle
    dut_reset()
    time.sleep(6.0)                       # DUT boot
    ctx.console.send("rf log verbose")    # reset clears the log level
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        tc = refresh(ctx)
        if tc.rx_requests(0x05):
            break
        time.sleep(1.0)
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x05),
              "BOOT olmadan envanter tamamlanmadi (fw kimligi taninmadi)")
    tc.expect(not notify_frames(tc, 0x13), "beklenmedik BOOT_NOTIFY")
    tc.assert_order([0x01, 0x07, 0x40, 0x04, 0x05])
    tc.expect(tc.rx_requests(0x48), "acilis korumasi 0x48 ile tuketilmedi")
    from cases_findings import wait_inventory_loaded
    wait_inventory_loaded(ctx, timeout_s=60)
    return "bootless bring-up ok"


@case("r2b_consume_via_0x48",
      "Tuketme 0x48 LOG_CONSUME_IF ile yapilir; 0x46 kullanilmaz",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 20.0, "do": "add_events", "count": 3,
                           "code": 2, "line": 1}]})
def r2b(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    tc = wait_drained(ctx)
    tc.expect(tc.rx_requests(0x48), "0x48 tuketme istegi yok")
    tc.expect(not tc.rx_requests(0x46),
              "R2 modunda 0x46 kullanilmamali")
    acks = [r for r in tc.tx_frames(0x48) if "reply" == r.get("kind")]
    tc.expect(acks, "0x48 ACK yok")
    return "consume via 0x48 ok (acks=%d)" % len(acks)


@case("r2c_event_142_temporary",
      "142 gecici ariza sayilir; 3 artik gecici sayilmaz (BQ-20)",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def r2c(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    before_temp = read_fault_count(ctx, 1, "TEMPORARY")
    before_perm = read_fault_count(ctx, 1, "PERMANENT")
    boot_counter = int(time.time()) & 0xFFFF
    ctx.sim.call(do="add_events", count=1, code=142, line=1, phase=1,
                 fault_count=3, boot_counter=boot_counter)
    wait_drained(ctx)
    assert read_fault_count(ctx, 1, "TEMPORARY") == before_temp + 1, \
        "142 gecici ariza sayilmadi"
    assert read_fault_count(ctx, 1, "PERMANENT") == before_perm, \
        "142 kalici ariza sayildi"
    ctx.sim.call(do="add_events", count=1, code=3, line=1, phase=1,
                 fault_count=3, boot_counter=boot_counter)
    wait_drained(ctx)
    assert read_fault_count(ctx, 1, "TEMPORARY") == before_temp + 1, \
        "3 olayi gecici ariza olarak sayildi (R2 kurali degil)"
    return "142=temporary +1; 3 not counted"


@case("r2d_source_hash_mismatch",
      "Kaynak hash uyusmaz: kalici ariza sayilmaz, ham kayit tutulur",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def r2d(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    before_perm = read_fault_count(ctx, 1, "PERMANENT")
    # MH tarafinda kart degisti; DUT hala eski EUI'ye bagli (Bildirim 4)
    ctx.sim.call(do="move_card", line=1, phase=1, zone=1)
    ctx.console.mark()
    boot_counter = int(time.time()) & 0xFFFF
    ctx.sim.call(do="add_events", count=1, code=101, line=1, phase=1,
                 boot_counter=boot_counter)
    wait_drained(ctx)
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x42) or tc.rx_requests(0x44),
              "kayit okunmadi (ham kayit da yok)")
    assert read_fault_count(ctx, 1, "PERMANENT") == before_perm, \
        "uyusmayan kaynak yine de kalici ariza sayildi"
    warned = [text for _, text in ctx.console.new_lines()
              if "unverified" in text]
    assert warned, "konsolda 'source unverified' uyarisi yok"
    ctx.evidence += warned[:3]
    return "mismatch retained raw only"


@case("r2e_live_boot_counter",
      "Canli veri bayt 26-27 Boot_Counter tasir; degisince oturum tazelenir",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def r2e(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    settle_live(ctx.console, 12)
    tc = refresh(ctx)
    lives = notify_frames(tc, 0x11)
    tc.expect(lives, "canli veri akisi yok")
    counters = {struct.unpack_from("<H", bytes.fromhex(r["data_hex"]),
                                   31)[0] for r in lives}
    tc.expect(counters == {7}, "Boot_Counter 7 beklenir: %s" % counters)
    pre_t = max((r.get("t_ms", 0) for r in lives), default=0)
    ctx.sim.call(do="live_value", line=1, phase=1,
                 values={"boot_counter": 9})
    settle_live(ctx.console, 12)
    tc = refresh(ctx)
    src11 = (1 << 2) | 1
    phase1 = [r for r in frames_after(tc, 0x11, pre_t)
              if src11 == bytes.fromhex(r["data_hex"])[0]]
    tc.expect(phase1, "fider1/faz1 canli verisi kesildi")
    counters = {struct.unpack_from("<H", bytes.fromhex(r["data_hex"]),
                                   31)[0] for r in phase1}
    tc.expect(counters == {9}, "yeni Boot_Counter 9 beklenir: %s" % counters)
    from cases_bq import read_trip_state  # late import avoids a cycle
    assert 0 == read_trip_state(ctx, 1)[1], "oturum yenilenmesi latch birakti"
    return "live Boot_Counter on wire + session refreshed"


@case("r2f_service_wipe_recovery",
      "Servis silmesi: 138/3 ilk kayit, total korunur, cekme toparlanir",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"},
                          {"at_s": 20.0, "do": "add_events", "count": 2,
                           "code": 2, "line": 1}]})
def r2f(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    tc = wait_drained(ctx)
    heads = [r for r in tc.tx_frames(0x40) if "reply" == r.get("kind")]
    tc.expect(heads, "0x40 yaniti yok")
    total_before = struct.unpack_from(
        "<I", bytes.fromhex(heads[-1]["data_hex"]), 4)[0]
    ctx.sim.call(do="service_wipe")
    tc = wait_drained(ctx)
    heads = [r for r in tc.tx_frames(0x40) if "reply" == r.get("kind")]
    body = bytes.fromhex(heads[-1]["data_hex"])
    head_i, wrap_i, total_i, tail_i = struct.unpack("<HHIH", body)
    assert total_i == total_before + 1, \
        "total korunmadi: %d -> %d" % (total_before, total_i)
    assert (head_i, wrap_i, tail_i) == (1, 0, 0), \
        "servis silmesi tablosu bozuk: %s" % ((head_i, wrap_i, tail_i),)
    codes = read_record_codes(tc)
    tc.expect(138 in codes, "138 kaydi okunmadi (raw)")
    return "service wipe recovered (total %d -> %d)" % (total_before,
                                                        total_i)


@case("r2g_epoch_renewal_records",
      "0x2A yenileme: tamamlandiginda 201 + ayirici basina 120 yazilir",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def r2g(ctx):
    ctx.console.send("rf log verbose")
    wait_upload_complete(ctx)
    login(ctx)
    ctx.sim.call(do="set_knob", name="epoch_duration_ms", value=3000)
    ctx.console.send("rf epoch 1")
    time.sleep(2.0)
    tc = refresh(ctx)
    tc.expect(tc.rx_requests(0x2A), "0x2A istegi yok")
    acks = [r for r in tc.tx_frames(0x2A) if "reply" == r.get("kind")]
    tc.expect(acks, "0x2A ACK yok")
    tc = wait_renewal_records(ctx)
    tc = wait_drained(ctx, timeout_s=90)
    codes = read_record_codes(tc)
    tc.expect(201 in codes, "olay 201 kaydi yok")
    tc.expect(codes.count(120) >= 3,
              "ayirici 120 kayitlari yok: %s" % codes.count(120))
    return "epoch records: 201 + %d x 120" % codes.count(120)


@case("r2h_keyless_reject",
      "Anahtarsiz MH: 0x22 ERROR 0x02 alir; 0x24 commit yapilmaz",
      scenario={"steps": [{"at_s": 0.5, "do": "boot"}]})
def r2h(ctx):
    ctx.console.send("rf log on")
    wait_upload_complete(ctx)
    login(ctx)
    ctx.sim.call(do="set_knob", name="mh_has_key", value=False)
    ctx.console.send("rf cfg-apply 1 21")
    time.sleep(6.0)
    tc = refresh(ctx)
    writes = tc.rx_requests(0x22)
    tc.expect(writes, "0x22 gonderilmedi")
    rejected = [r for r in tc.tx_frames(0x22)
                if "reply" == r.get("kind") and
                bytes.fromhex(r["data_hex"])[0:1] == b"\x02"]
    tc.expect(rejected, "0x22 ERROR 0x02 yaniti yok")
    tc.expect(not tc.rx_requests(0x24), "anahtarsiz MH'ye 0x24 gitti")
    ctx.sim.call(do="set_knob", name="mh_has_key", value=True)
    body = ctx.console.drain(2.0)
    ctx.evidence += [text for _, text in body if text.strip()][:8]
    return "keyless rejected; writes=%d" % len(writes)
