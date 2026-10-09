#!/usr/bin/env python3
"""IEC104 master suite cases (i01..i06).

Each case returns (verdict, evidence_lines) where verdict is one of
PASS / FAIL / SKIP / DEFERRED. Assertions rely on the raw frame list in
Master104 (U-frames included); the JSONL evidence file gets every APDU
regardless. Mutating cases (i04 apply-check, i06 eventlog fixture) are
gated by explicit opt-in flags, see run_suite.py.
"""

import time

import apdu
from apdu import frame_is_i_asdu as is_i_asdu  # noqa: F401
from apdu import frame_is_u as is_u  # noqa: F401


class Ctx:
    """Shared suite context handed to every case."""

    def __init__(self, master, oracle, console, console_state, args,
                 expected, outdir):
        self.master = master
        self.oracle = oracle
        self.console = console
        self.console_state = console_state
        self.args = args
        self.expected = expected
        self.outdir = outdir


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def _rx_frames(master, t0):
    """Rx I-frames at/after t0 WITH a decoded ASDU.

    Malformed I-frames (undecodable ASDU) are skipped here; they stay in
    the JSONL evidence but must never crash downstream indexing.
    """
    result = []
    for frame in master.since(t0):
        if (frame[1] == "rx" and frame[3]
                and frame[3].get("kind") == "I"
                and frame[3].get("asdu")):
            result.append(frame)
    return result


def _ic_confirm(frame, cot, qoi):
    """C_IC_NA_1 confirm predicate safe against object-less ASDUs
    (count=0, SQ=1 or truncated bodies carry no objects key)."""
    if not is_i_asdu(frame, "rx", apdu.TYPE_C_IC_NA_1, cot=cot, pn=0):
        return False
    objects = frame[3]["asdu"].get("objects") or []
    return bool(objects) and objects[0].get("qoi") == qoi


def _collect_interrogation(master, t0, qoi, data_deadline_s=30.0):
    """Wait ACT_CON -> data* -> ACT_TERM for one interrogation.

    Returns con/term frames, the GI data frames (type 30/36 with an
    interrogated COT 20..24 between con and term) and any concurrent
    spontaneous frames (COT outside 20..24) in the window. Structural
    validation of CA/COT/type happens in _check_gi_frames.
    """
    ok, con = master.wait(lambda f: _ic_confirm(f, 7, qoi), 12.0, t0=t0)
    if not ok:
        return False, {"stage": "ACT_CON", "qoi": qoi}
    ok, term = master.wait(lambda f: _ic_confirm(f, 10, qoi),
                           data_deadline_s, t0=con[0])
    if not ok:
        return False, {"stage": "ACT_TERM", "qoi": qoi}
    window = [f for f in _rx_frames(master, t0)
              if con[0] <= f[0] <= term[0]
              and f[3]["asdu"]["type"] in (apdu.TYPE_M_ME_TF_1,
                                           apdu.TYPE_M_SP_TB_1)]
    data = [f for f in window if 20 <= f[3]["asdu"]["cot"] <= 24]
    concurrent = [f for f in window if not 20 <= f[3]["asdu"]["cot"] <= 24]
    return True, {"con": con, "term": term, "data": data,
                  "concurrent": concurrent}


def _check_gi_frames(detail, qoi, ca, currents, states):
    """Strict GI response validation.

    Every data frame must carry the expected common address, the
    query-specific interrogated COT (20 for station, the group number
    itself for groups) and the type matching each IOA family: current
    points are M_ME_TF_1 measurements, state points are M_SP_TB_1.
    Returns None when valid, else a failure string.
    """
    expected_cot = 20 if qoi == 20 else qoi
    for frame in detail["data"]:
        asdu = frame[3]["asdu"]
        if asdu["ca"] != ca:
            return "CA=%s (beklenen %d, tip=%s cot=%s)" % (
                asdu["ca"], ca, asdu["type"], asdu["cot"])
        if asdu["cot"] != expected_cot:
            return "COT=%s (beklenen %d, sorgu=%d)" % (
                asdu["cot"], expected_cot, qoi)
        if qoi in (20, 21, 22):
            for obj in asdu.get("objects", []):
                ioa = obj["ioa"]
                if ioa in currents and asdu["type"] != \
                        apdu.TYPE_M_ME_TF_1:
                    return "akim IOA %d tip %s (beklenen olcum 36)" % (
                        ioa, asdu["type"])
                if ioa in states and asdu["type"] != \
                        apdu.TYPE_M_SP_TB_1:
                    return "durum IOA %d tip %s (beklenen 30)" % (
                        ioa, asdu["type"])
    return None


def _ioas(frames):
    result = set()
    for frame in frames:
        for obj in frame[3]["asdu"].get("objects", []):
            result.add(obj["ioa"])
    return result


def _qualities(frames):
    """{(ioa): quality-dict} for M_ME_TF_1 objects (measured values)."""
    result = {}
    for frame in frames:
        if frame[3]["asdu"]["type"] == apdu.TYPE_M_ME_TF_1:
            for obj in frame[3]["asdu"].get("objects", []):
                result[obj["ioa"]] = {"qds": obj.get("qds"),
                                      "value": obj.get("value")}
    return result


# ---------------------------------------------------------------------------
# i01 - STARTDT handshake + M_EI
# ---------------------------------------------------------------------------


def i01_startdt_mei(ctx):
    """STARTDT_ACT -> STARTDT_CON -> M_EI_NA_1(70) COT=4 CA IOA=0."""
    ca = ctx.oracle["ca"]
    t0 = time.monotonic()
    if not ctx.master.connect(timeout_s=20.0):
        return "FAIL", ["STARTDT_CON gelmedi (20 s)"]
    ok_act, act = ctx.master.wait(
        lambda f: is_u(f, "tx", "STARTDT_ACT"), 1.0, t0=t0)
    ok_con, con = ctx.master.wait(
        lambda f: is_u(f, "rx", "STARTDT_CON"), 1.0, t0=t0)
    ok_mei, mei = ctx.master.wait(
        lambda f: is_i_asdu(f, "rx", apdu.TYPE_M_EI_NA_1, cot=4),
        12.0, t0=t0)
    if not (ok_act and ok_con and ok_mei):
        return "FAIL", ["act=%s con=%s mei=%s" % (ok_act, ok_con, ok_mei)]
    asdu = mei[3]["asdu"]
    order_ok = act[0] <= con[0] <= mei[0]
    if not order_ok:
        return "FAIL", ["frame sirasi bozuk: act-con-mei degil"]
    if asdu["ca"] != ca or asdu["pn"] != 0:
        return "FAIL", ["M_EI ca=%s (beklenen %d) pn=%s" %
                        (asdu["ca"], ca, asdu["pn"])]
    ioa = asdu["objects"][0]["ioa"] if asdu.get("objects") else None
    if ioa != 0:
        return "FAIL", ["M_EI IOA=%s (beklenen 0)" % ioa]
    return "PASS", [
        "STARTDT_ACT -> STARTDT_CON -> M_EI_NA_1 COT=4 CA=%d IOA=0" % ca,
        "ilk veri 45 s watchdog penceresinde (%.1f s)" % (mei[0] - t0)]


# ---------------------------------------------------------------------------
# i02 - station interrogation (QOI 20)
# ---------------------------------------------------------------------------


def i02_gi_station(ctx):
    """ACT_CON -> full expected IOA set -> ACT_TERM; quality sane."""
    station_set = ctx.expected["station"]
    if not station_set:
        return "SKIP", ["aktif hat yok: --setup-feeders ile RF store "
                        "yapilandirin (beklenen kume bos, veri yolu "
                        "dogrulanamaz)"]
    t0 = time.monotonic()
    if not ctx.master.interrogate(20):
        return "FAIL", ["interrogation(20) gonderilemedi"]
    ok, detail = _collect_interrogation(ctx.master, t0, 20)
    if not ok:
        return "FAIL", ["QOI=20 %s gelmedi" % detail["stage"]]
    ca = ctx.oracle["ca"]
    failure = _check_gi_frames(detail, 20, ca, ctx.expected["group1"],
                               ctx.expected["group2"])
    if failure:
        return "FAIL", ["GI cevabi yapisi gecersiz: %s" % failure]
    got = _ioas(detail["data"])
    if got != station_set:
        missing = sorted(station_set - got)
        extra = sorted(got - station_set)
        return "FAIL", [
            "IOA kumesi eslesmedi: eksik=%s fazla=%s" % (missing, extra)]
    lines = ["ACT_CON -> %d veri cercevesi -> ACT_TERM sirasi tamam" %
             len(detail["data"]),
             "IOA kumesu tam (%d nokta), CA=%d, COT=20" %
             (len(station_set), ca)]
    if detail["concurrent"]:
        lines.append("pencerede es zamanli spontane=%d (set disi "
                     "tutuldu)" % len(detail["concurrent"]))
    # Quality per plan item 5: expectation follows the bench RF state.
    quals = _qualities(detail["data"])
    currents = ctx.expected["group1"]
    measured_currents = [ioa for ioa in quals if ioa in currents]
    if not measured_currents:
        return "FAIL", ["akim noktalari olcum (M_ME_TF_1) olarak "
                        "gelmedi - kalite kontrolu bos gecemaz"]
    rf_online = ctx.console_state.get("rf_online")
    if rf_online is False:
        bad = {ioa: q for ioa, q in quals.items()
               if ioa in currents and not q["qds"]["iv"]}
        if bad:
            return "FAIL", ["hub offline iken IV bekleniyordu: %s" % bad]
        lines.append("hub offline: akim degerleri IV isaretli (dogru)")
    else:
        summary = {}
        for ioa, q in quals.items():
            if ioa in currents:
                key = ("iv" if q["qds"]["iv"] else "") + \
                      ("nt" if q["qds"]["nt"] else "") or "clean"
                summary[key] = summary.get(key, 0) + 1
        lines.append("akim kalite dagilimi=%s (rf_online=%s; yalniz "
                     "rapor, kabul maddesi degil)" % (summary, rf_online))
    return "PASS", lines


# ---------------------------------------------------------------------------
# i03 - group interrogations (QOI 21..24)
# ---------------------------------------------------------------------------


def _fault_space_ranges(ctx):
    ranges = []
    for line in ctx.oracle["lines"]:
        if line["in_use"]:
            ranges.append((line["temp_fault_base"],
                           line["temp_fault_base"] + 2000))
            ranges.append((line["perm_fault_base"],
                           line["perm_fault_base"] + 2000))
    return ranges


def _in_ranges(ioa, ranges):
    return any(low <= ioa < high for low, high in ranges)


def i03_gi_groups(ctx):
    """QOI 21/22 exact sets; QOI 23/24 ACT_CON+ACT_TERM with fault IOAs."""
    if not ctx.expected["station"]:
        return "SKIP", ["aktif hat yok (i02 ile ayni kosul)"]
    lines = []
    ca = ctx.oracle["ca"]
    for qoi, expected in ((21, ctx.expected["group1"]),
                          (22, ctx.expected["group2"])):
        t0 = time.monotonic()
        if not ctx.master.interrogate(qoi):
            return "FAIL", ["interrogation(%d) gonderilemedi" % qoi]
        ok, detail = _collect_interrogation(ctx.master, t0, qoi)
        if not ok:
            return "FAIL", ["QOI=%d %s gelmedi" % (qoi, detail["stage"])]
        failure = _check_gi_frames(detail, qoi, ca,
                                   ctx.expected["group1"],
                                   ctx.expected["group2"])
        if failure:
            return "FAIL", ["QOI=%d cevap yapisi gecersiz: %s" %
                            (qoi, failure)]
        got = _ioas(detail["data"])
        if got != expected:
            return "FAIL", ["QOI=%d kume eslesmedi: eksik=%s fazla=%s" %
                            (qoi, sorted(expected - got),
                             sorted(got - expected))]
        lines.append("QOI=%d: ACT_CON -> %d cerceve -> ACT_TERM, kume "
                     "tam (CA=%d, COT=%d)" % (qoi, len(detail["data"]),
                                              ca, qoi))
    ranges = _fault_space_ranges(ctx)
    for qoi in (23, 24):
        t0 = time.monotonic()
        if not ctx.master.interrogate(qoi):
            return "FAIL", ["interrogation(%d) gonderilemedi" % qoi]
        ok, detail = _collect_interrogation(ctx.master, t0, qoi,
                                            data_deadline_s=45.0)
        if not ok:
            return "FAIL", ["QOI=%d %s gelmedi (45 s)" %
                            (qoi, detail["stage"])]
        failure = _check_gi_frames(detail, qoi, ca,
                                   ctx.expected["group1"],
                                   ctx.expected["group2"])
        if failure:
            return "FAIL", ["QOI=%d cevap yapisi gecersiz: %s" %
                            (qoi, failure)]
        data_types = {f[3]["asdu"]["type"] for f in detail["data"]}
        if not data_types <= {apdu.TYPE_M_ME_TF_1, apdu.TYPE_M_SP_TB_1}:
            return "FAIL", ["QOI=%d beklenmedik tip: %s" %
                            (qoi, sorted(data_types))]
        if detail["data"]:
            stray = {ioa for ioa in _ioas(detail["data"])
                     if not _in_ranges(ioa, ranges)}
            if stray:
                return "FAIL", ["QOI=%d ariza IOA bolgesi disinda: %s" %
                                (qoi, sorted(stray))]
        lines.append("QOI=%d: ACT_CON -> %d ariza cercevesi -> ACT_TERM"
                     % (qoi, len(detail["data"])))
    return "PASS", lines


# ---------------------------------------------------------------------------
# i04 - clock sync (opt-in: changes the device clock)
# ---------------------------------------------------------------------------


def i04_clock_sync(ctx):
    """C_CS_NA_1 ACT_CON echo; apply check via console timestamps.

    c104 transmits the timestamp in UTC (verified live: sent 12:57 while
    host local was 15:57, UTC+3). The device applies the received
    timestamp verbatim, so the honest assertion compares the device
    clock against the CP56 value WE SENT, not against host local time.
    """
    if not ctx.args.clock_sync:
        return "SKIP", ["cihaz saatini degistirir: --clock-sync ile "
                        "calisir"]
    import console_io
    if ctx.console is None:
        return "SKIP", ["--clock-sync icin --console gerekli"]
    t0 = time.monotonic()
    pre_stamp = console_io.latest_console_time(ctx.console)
    if not ctx.master.clock_sync():
        return "FAIL", ["clock_sync() gonderilemedi"]
    ok, con = ctx.master.wait(
        lambda f: is_i_asdu(f, "rx", apdu.TYPE_C_CS_NA_1, cot=7, pn=0),
        10.0, t0=t0)
    if not ok:
        return "FAIL", ["C_CS_NA_1 ACT_CON gelmedi"]
    ok_tx, tx = ctx.master.wait(
        lambda f: is_i_asdu(f, "tx", apdu.TYPE_C_CS_NA_1, cot=6),
        1.0, t0=t0)
    if not ok_tx:
        return "FAIL", ["gonderilen C_CS_NA_1 karesi kayitta yok"]
    sent = tx[3]["asdu"]["objects"][0]["time"]
    sent_sec = sent["ms"] // 1000
    sent_ms = sent["ms"] % 1000
    sent_tuple = (sent["year"], sent["month"], sent["day"],
                  sent["hour"], sent["min"], sent_sec)
    lines = ["C_CS_NA_1 ACT_CON P/N=0; gonderilen CP56 %04d-%02d-%02d "
             "%02d:%02d:%02d.%03d" % (sent_tuple + (sent_ms,))]
    # Apply check: newest console wall-clock timestamp vs sent value.
    time.sleep(6.0)     # let a timestamped background line arrive
    stamp = console_io.latest_console_time(ctx.console)
    if stamp is None:
        return "FAIL", ["apply dogrulamasi icin zaman damgali konsol "
                        "satiri gorulmedi"]
    if stamp == pre_stamp:
        return "FAIL", ["senkron sonrasi YENI zaman damgali konsol satiri "
                        "gelmedi (eski damga %s ile karsilastirma "
                        "anlamsiz)" % (stamp,)]
    import calendar
    import datetime
    sent_epoch = calendar.timegm(
        datetime.datetime(*sent_tuple).timetuple())
    device_epoch = calendar.timegm(datetime.datetime(*stamp).timetuple())
    drift = abs(device_epoch - sent_epoch)
    lines.append("cihaz saati %02d:%02d:%02d; gonderilene gore kayma "
                 "<=%d s (OLCUM SINIRI: konsol satiri senkron+~6 s sonra "
                 "okundu; gercek sapma bu pencerenin altindadir; c104 "
                 "UTC gonderir, host yereli %s)" %
                 (stamp[3], stamp[4], stamp[5], drift,
                  time.strftime("%H:%M")))
    # Console lines carry second resolution and arrive seconds late;
    # a genuine non-apply shows up as minutes/hours of drift.
    if drift > 30.0:
        return "FAIL", ["alinan timestamp uygulanmamis gorunuyor "
                        "(kayma %.0f s)" % drift]
    return "PASS", lines


# ---------------------------------------------------------------------------
# i05 - idle TESTFR from the device
# ---------------------------------------------------------------------------


def i05_testfr_idle(ctx):
    """Device t3 expires first: rx TESTFR_ACT -> tx TESTFR_CON -> GI ok.

    t3 only fires on a SILENT link. Bench reality observed 2026-10-09:
    in quiet windows the TCP link DROPS first (~40-75 s; device #SS sees
    NO_CARRIER, c104 silently reconnects with a new STARTDT) and no
    TESTFR is ever observed. A drop inside the window is a FAIL with
    evidence (real interop finding), genuine rx traffic is an honest
    SKIP, and a master TESTFR before the device's means wrong t3 setup.
    """
    device_t3 = ctx.oracle["t3"]
    idle_budget = device_t3 + 45.0
    t0 = time.monotonic()
    deadline = t0 + idle_budget
    rx_act = None
    tx_testfr = None
    reconnect = None            # unsolicited STARTDT_ACT in window
    closed_state = None         # first CLOSED* state event in window
    new_rx = 0
    scan_from = t0
    while time.monotonic() < deadline:
        ok, frame = ctx.master.wait(
            lambda f: True,
            max(0.5, deadline - time.monotonic()), t0=scan_from)
        if not ok:
            break
        scan_from = frame[0] + 0.001
        direction = frame[1]
        parsed = frame[3] or {}
        if direction == "state":
            if closed_state is None and \
                    "CLOSED" in str(parsed.get("state", "")):
                closed_state = frame
        elif direction == "rx":
            if parsed.get("kind") == "U" and \
                    parsed.get("u") == "TESTFR_ACT":
                rx_act = frame
                break
            new_rx += 1
        elif parsed.get("kind") == "U" and \
                parsed.get("u") == "TESTFR_ACT":
            if tx_testfr is None:
                tx_testfr = frame
        elif parsed.get("kind") == "U" and \
                parsed.get("u") == "STARTDT_ACT" and reconnect is None:
            reconnect = frame
    if tx_testfr is not None and (rx_act is None or
                                  tx_testfr[0] < rx_act[0]):
        return "FAIL", ["master TESTFR_ACT'i cihazdan once gonderdi "
                        "(t3 ayari yanlis)"]
    if rx_act is not None:
        ok_con, _ = ctx.master.wait(
            lambda f: is_u(f, "tx", "TESTFR_CON"), 5.0, t0=rx_act[0])
        if not ok_con:
            return "FAIL", ["TESTFR_CON yaniti uretilmedi"]
        t1 = time.monotonic()
        if not ctx.master.interrogate(20):
            return "FAIL", ["TESTFR sonrasi interrogation gonderilemedi"]
        ok, detail = _collect_interrogation(ctx.master, t1, 20)
        if not ok:
            return "FAIL", ["TESTFR sonrasi GI %s gelmedi" %
                            detail["stage"]]
        return "PASS", [
            "cihaz TESTFR_ACT uretti (bekleme %.0f s, t3=%d s)" %
            (rx_act[0] - t0, device_t3),
            "master TESTFR_CON yanitladi, ardindan GI tamamlandi",
            "pencerede diger rx trafik=%d" % new_rx]
    drop = closed_state or reconnect
    if drop is not None:
        tx_s = len([f for f in ctx.master.since(t0)
                    if f[1] == "tx" and
                    (f[3] or {}).get("kind") == "S"])
        signal = "durum=CLOSED" if closed_state is not None \
            else "istemsiz STARTDT (yeniden baglanti)"
        lines = [
            "sessiz pencerede link dustu (T+%.0f s: %s); cihazdan "
            "TESTFR_ACT gelmedi" % (drop[0] - t0, signal),
            "pencerede yeni rx=%d, tx S-ACK=%d" % (new_rx, tx_s)]
        if ctx.console is not None:
            import console_io
            dump = console_io.run_cmd(ctx.console, "iec104elog dump 3",
                                      settle_s=4.0)
            disc = [line.strip() for line in dump.splitlines()
                    if "DISC" in line.upper() or "CONN" in line.upper()]
            for line in disc[-4:]:
                lines.append("elog: %s" % line)
            lines.append("kok neden ACIK: master/modem/ag/firmware "
                         "zamanlamasi ayristirilmali (iec104elog DISC + "
                         "master kanitlari). Not: master'a periyodik "
                         "trafik linki korur ama bosta TESTFR testini "
                         "karsilamaz")
        return "FAIL", lines
    if new_rx:
        return "SKIP", [
            "link sessiz degil: %d rx karesi t3'yu besliyor; TESTFR "
            "gozlenemez (sessiz bench'te tekrar)" % new_rx]
    return "FAIL", ["%.0f s sessiz bekleyise ragmen cihazdan TESTFR_ACT "
                    "gelmedi ve link de dusmedi (t3=%d s)" %
                    (idle_budget, device_t3)]


# ---------------------------------------------------------------------------
# i06 - replay on reconnect (opt-in fixture via console)
# ---------------------------------------------------------------------------


def i06_replay_reconnect(ctx):
    """Inject N synthetic records, reconnect, verify newest-first replay.

    Content formulas come from iec104_event_log_test(): current =
    100 + (seq%900)/10, duration = 100 + (seq%50)*20. The injected seqs
    are known from iec104evtlog status, so both content and order are
    asserted exactly - not just unsent==0.
    """
    import console_io
    if not ctx.args.mutate_eventlog:
        return "SKIP", ["Flash/NVRAM'e yazar: --mutate-eventlog ile "
                        "calisir"]
    if ctx.console is None:
        return "SKIP", ["--mutate-eventlog icin --console gerekli"]
    if not ctx.expected["station"]:
        return "SKIP", ["aktif hat yok: replay kayitlari in-use fider "
                        "IOA'larina yazilir (once --setup-feeders)"]
    count = getattr(ctx.args, "evtlog_count", 5)
    if not ctx.master.is_connected:
        # Standalone run: establish the link the case is about to break.
        if not ctx.master.connect(timeout_s=20.0):
            return "FAIL", ["ilk baglanti kurulamadi (STARTDT_CON yok)"]
    ctx.master.disconnect()
    time.sleep(2.0)
    st0 = console_io.parse_evtlog_status(console_io.run_cmd(
        ctx.console, "iec104evtlog status", settle_s=4.0))
    if st0 is None or st0["next_seq"] is None:
        return "FAIL", ["iec104evtlog status okunamadi"]
    console_io.run_cmd(ctx.console, "iec104evtlog test %d" % count,
                       settle_s=4.0)
    st1 = console_io.parse_evtlog_status(console_io.run_cmd(
        ctx.console, "iec104evtlog status", settle_s=4.0))
    if st1 is None or st1["next_seq"] != st0["next_seq"] + count:
        return "FAIL", ["evtlog test yazmadi: next_seq %s -> %s (beklenen "
                        "+%d)" % (st0["next_seq"],
                                  (st1 or {}).get("next_seq"), count)]
    old_unsent = st0["unsent"] or 0
    lines = ["%d sentetik kayit eklendi (seq %d..%d); onceki unsent=%d" %
             (count, st0["next_seq"], st1["next_seq"] - 1, old_unsent)]

    t0 = time.monotonic()
    if not ctx.master.connect(timeout_s=20.0):
        return "FAIL", ["yeniden baglanilamadi (STARTDT_CON yok)"]
    ok, first = ctx.master.wait(
        lambda f: f[1] == "rx" and f[3] and f[3].get("kind") == "I"
        and (f[3].get("asdu") or {}).get("cot") == 3, 30.0, t0=t0)
    if not ok:
        return "FAIL", ["replay baslamadi (30 s, 15 s fallback timer "
                        "asildi olmali)"]
    # Collect until 6 s of silence after the last COT=3 frame (cap 60 s).
    last = first[0]
    deadline = t0 + 60.0
    while time.monotonic() < deadline:
        ok, frame = ctx.master.wait(
            lambda f: f[1] == "rx" and f[3] and
            f[3].get("kind") == "I" and
            (f[3].get("asdu") or {}).get("cot") == 3, 6.0,
            t0=last + 0.001)
        if not ok:
            break
        last = frame[0]
    replay = [f for f in _rx_frames(ctx.master, t0)
              if f[3]["asdu"]["cot"] == 3]

    measured = [f for f in replay
                if f[3]["asdu"]["type"] == apdu.TYPE_M_ME_TF_1
                and len(f[3]["asdu"].get("objects", [])) == 2]
    # The bench may deliver ambient alarm records (real MH fault lists)
    # interleaved with our synthetic replay. Identify OUR records by the
    # exact (current, duration) formula of the known injected seqs and
    # require them to arrive newest-first; everything else is recorded
    # as ambient noise.
    seq_first = st1["next_seq"] - 1     # newest unsent = newest injected
    expected_pairs = []
    for offset in range(count):
        seq = seq_first - offset
        expected_pairs.append((seq,
                               100.0 + (seq % 900) / 10.0,
                               float(100 + (seq % 50) * 20)))
    matched = []
    ambient = 0
    cursor = 0
    for frame in measured:
        objects = frame[3]["asdu"]["objects"]
        if cursor >= len(expected_pairs):
            ambient += 1
            continue
        seq, want_current, want_duration = expected_pairs[cursor]
        if (abs(objects[0]["value"] - want_current) <= 0.051 and
                abs(objects[1]["value"] - want_duration) <= 0.5):
            matched.append((seq, frame))
            cursor += 1
        else:
            ambient += 1
    if len(matched) != count:
        return "FAIL", ["sentetik replay kaydi %d/%d (ortam gurultusu=%d; "
                        "en son eslesen seq=%s)" %
                        (len(matched), count, ambient,
                         matched[-1][0] if matched else None)]
    lines.append("replay icerik + en-yeni-once sirasi dogrulandi "
                 "(seq %d..%d; ortam kaydi=%d)" %
                 (seq_first, seq_first - count + 1, ambient))

    st2 = console_io.parse_evtlog_status(console_io.run_cmd(
        ctx.console, "iec104evtlog status", settle_s=4.0))
    if st2 is None:
        return "FAIL", ["iec104evtlog status (son) okunamadi"]
    if st2["unsent"] and st2["next_seq"] <= st1["next_seq"]:
        # No new records appeared after our injection, yet unsent
        # remains: our injected records were not drained.
        return "FAIL", ["unsent=%s hala dolu (yeni kayit yok, replay "
                        "bizim seq'leri birakmis)" % st2["unsent"]]
    lines.append("unsent durum=%s (yeni ortam kaydi=%s)" %
                 (st2["unsent"],
                  max(0, (st2["next_seq"] or 0) - st1["next_seq"])))

    # Closing snapshot: after the backlog drains the device re-sends the
    # current RF/trip state snapshot; the FULL expected set must arrive
    # strictly AFTER the last synthetic replay record. Older ambient
    # records may replay after ours (newest-first order) - their frames
    # are recorded as extras, not failures.
    last_match_t = matched[-1][1][0]
    closing = [f for f in replay
               if f[0] > last_match_t
               and f[3]["asdu"]["type"] == apdu.TYPE_M_SP_TB_1]
    closing_ioas = _ioas(closing)
    snapshot_set = ctx.expected["snapshot"]
    missing = snapshot_set - closing_ioas
    if missing:
        return "FAIL", ["kapanis anlik goruntusu eksik: eksik=%s "
                        "(gelen=%s)" % (sorted(missing),
                                        sorted(closing_ioas))]
    extras = sorted(closing_ioas - snapshot_set)
    lines.append("kapanis anlik goruntusu tam (%d IOA, son kayit "
                 "sonrasi; ortam eki=%s)" % (len(snapshot_set),
                                             extras or "yok"))
    return "PASS", lines


# ---------------------------------------------------------------------------
# registry
# ---------------------------------------------------------------------------

CASES = {
    "i01_startdt_mei": (i01_startdt_mei,
                        "STARTDT_CON + M_EI_NA_1(70) COT=4"),
    "i02_gi_station": (i02_gi_station,
                       "QOI=20 tam IOA kumesi + kalite"),
    "i03_gi_groups": (i03_gi_groups,
                      "QOI 21/22 kume + 23/24 ariza sorgusu"),
    "i04_clock_sync": (i04_clock_sync,
                       "C_CS_NA_1 ACT_CON (+apply, --clock-sync)"),
    "i05_testfr_idle": (i05_testfr_idle,
                        "cihaz t3 TESTFR_ACT + GI canliligi"),
    "i06_replay_reconnect": (i06_replay_reconnect,
                             "evtlog replay icerik+sira (--mutate-eventlog)"),
}
