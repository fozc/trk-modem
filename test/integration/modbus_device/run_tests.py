#!/usr/bin/env python3
"""Modbus RTU device-in-the-loop acceptance checks (bench only; SKIP
without hardware).

Exercises the production RTU core over a real RS-485 line: map regression,
garbage-stream tail rejection, foreign-address traffic, paced transmission
(host-side pacing only), broadcast, exception contracts and response-time
measurement (first byte and complete response recorded separately - no
trailing-wait artefact in the numbers).

Environment:
  MODBUS_RTU_PORT     RS-485 adapter port (e.g. COM10); unset -> SKIP
  MODBUS_RTU_BAUD     default 115200
  MODBUS_RTU_SLAVE    default 23
"""
import os
import serial
import time

PORT = os.environ.get("MODBUS_RTU_PORT")
BAUD = int(os.environ.get("MODBUS_RTU_BAUD", "115200"))
STOPBITS = int(os.environ.get("MODBUS_RTU_STOPBITS", "1"))
SLAVE = int(os.environ.get("MODBUS_RTU_SLAVE", "23"))
RESPONSE_TIMEOUT_S = float(os.environ.get("MODBUS_RTU_RESPONSE_TIMEOUT_S", "1"))
RELAY1 = 10002      # PDU (logical 50002)
RELAY2 = 10003      # PDU (logical 50003)
AKU = 10000         # PDU (logical 50000)

passed = 0
failed = 0
notes = []


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def send_paced(ser, data, interval_s):
    """Record host write-call gaps; these are not wire timestamps."""
    starts = []
    for value in data:
        starts.append(time.perf_counter())
        ser.write(bytes([value]))
        time.sleep(interval_s)
    gaps = [(later-earlier)*1000 for earlier,later in zip(starts,starts[1:])]
    return gaps


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("PASS: %s %s" % (name, detail))
    else:
        failed += 1
        print("FAIL: %s %s" % (name, detail))


def note(text):
    notes.append(text)
    print("NOT: %s" % text)


def frame(slave, func, *payload):
    body = bytes([slave, func]) + bytes(payload)
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def fc06(slave, reg, value):
    return frame(slave, 6, (reg >> 8) & 0xFF, reg & 0xFF,
                 (value >> 8) & 0xFF, value & 0xFF)


def fc03(slave, start, count):
    return frame(slave, 3, (start >> 8) & 0xFF, start & 0xFF,
                 (count >> 8) & 0xFF, count & 0xFF)


def valid_frame(buf):
    """Strict Modbus RTU response validation: length, address, CRC."""
    if len(buf) < 4:
        return False
    if buf[0] != SLAVE:
        return False
    return crc16(buf[:-2]) == ((buf[-1] << 8) | buf[-2])


def expect_no_extra(ser, window_s):
    """Any byte inside the window is late/duplicate evidence."""
    deadline = time.time() + window_s
    extra = b""
    while time.time() < deadline:
        if ser.in_waiting:
            extra += ser.read(ser.in_waiting)
    return extra


def request(ser, req, resp_len, quiet_window_s=0.03):
    """Send one request; return (response, first_byte_ms, complete_ms).

    first_byte_ms and complete_ms measure only real line time - no
    trailing-wait artefact is included in the reported numbers.
    """
    ser.reset_input_buffer()
    t0 = time.perf_counter()
    ser.write(req)
    return receive_response(ser, resp_len, quiet_window_s, t0)


def receive_response(ser, resp_len, quiet_window_s=0.03, started=None):
    """Finish the outstanding transaction before sending another request."""
    t0 = time.perf_counter() if started is None else started
    first_ms = None
    resp = b""
    # Persistent relay commands verify two Flash copies before responding.
    deadline = t0 + RESPONSE_TIMEOUT_S
    while time.perf_counter() < deadline:
        if ser.in_waiting:
            if first_ms is None:
                first_ms = (time.perf_counter() - t0) * 1000.0
            resp += ser.read(ser.in_waiting)
            if len(resp) >= resp_len:
                break
    complete_ms = (time.perf_counter() - t0) * 1000.0
    extra = expect_no_extra(ser, quiet_window_s)
    if extra:
        resp += b"[EXTRA:%s]" % extra.hex().encode()
    return resp, first_ms, complete_ms


def read_regs(ser, start, count):
    resp, _, _ = request(ser, fc03(SLAVE, start, count), 5 + 2 * count)
    if (len(resp) == 5 + 2 * count) and valid_frame(resp) and (resp[1] == 3) \
            and (resp[2] == 2 * count):
        return [(resp[3 + 2 * i] << 8) | resp[4 + 2 * i]
                for i in range(count)]
    return None


def expect_silence(ser, window_s, after_write=b""):
    """Assert the line stays completely quiet (not even one byte)."""
    if after_write:
        ser.reset_input_buffer()
        ser.write(after_write)
    extra = expect_no_extra(ser, window_s)
    return b"" == extra, extra


def main():
    if not PORT:
        print("SKIP: MODBUS_RTU_PORT ayarli degil (gercek RS-485 hedefi "
              "bekleniyor)")
        return 0

    ser = serial.Serial(PORT, BAUD, bytesize=8, parity="N",
                        stopbits=STOPBITS, timeout=0.005)

    # ---- T1: map regression (strict FC03 validation) ---------------
    check("T1 fider1 kalite PDU0 = 0x0003",
          read_regs(ser, 0, 1) == [0x0003])
    check("T1 faz1 kalite PDU4 = 0x007F",
          read_regs(ser, 4, 1) == [0x007F])
    check("T1 aku PDU10000 okunur", read_regs(ser, AKU, 1) is not None)

    # ---- T2: FC06 echo + readback, no duplicate response -----------
    req = fc06(SLAVE, RELAY1, 1)
    resp, _, _ = request(ser, req, 8)
    check("T2 FC06 R1=1 echo (adres+islev+CRC)", resp == req,
          "%s" % resp.hex())
    check("T2 FC03 R1=1", read_regs(ser, RELAY1, 1) == [1])
    req = fc06(SLAVE, RELAY1, 0)
    resp, _, _ = request(ser, req, 8)
    check("T2 FC06 R1=0 echo", resp == req, "%s" % resp.hex())
    check("T2 FC03 R1=0", read_regs(ser, RELAY1, 1) == [0])

    # ---- T3: garbage stream + valid tail in the SAME burst ----------
    garbage = bytes([0xFF]) * 300 + fc06(SLAVE, RELAY1, 1)
    quiet, extra = expect_silence(ser, 0.4, after_write=garbage)
    check("T3 cop akisi + kuyruk: hatta tam sessizlik", quiet,
          "extra=%s" % extra.hex())
    check("T3 kuyruk komut OLMADI (R1 hala 0)",
          read_regs(ser, RELAY1, 1) == [0])
    req = fc06(SLAVE, RELAY1, 1)
    resp, _, _ = request(ser, req, 8)
    check("T3 sessizlik sonrasi komut: tek echo", resp == req)
    check("T3 R1=1 (durum dogrulamasi; callback sayisi kaniti degil)", read_regs(ser, RELAY1, 1) == [1])
    request(ser, fc06(SLAVE, RELAY1, 0), 8)
    check("T3 temizlik R1=0", read_regs(ser, RELAY1, 1) == [0])

    # ---- T4: foreign-address traffic then our request --------------
    quiet, extra = expect_silence(ser, 0.15,
                                  after_write=fc03(5, 0, 1))
    check("T4 yabanci istege hatta sessizlik", quiet,
          "extra=%s" % extra.hex())
    check("T4 ardindan bizim istek yanitlanir",
          read_regs(ser, AKU, 1) is not None)

    # ---- T5: paced transmission (host-side pacing, wire unproven) --
    note("T5 aralik degerleri bilgisayar tarafi gonderim araligidir; "
         "teldeki bayt araligi USB/OS nedeniyle kanitlanmaz (D1 fiziksel "
         "dogrulamasi analizor ile acik).")
    req = fc06(SLAVE, RELAY1, 1)
    ser.reset_input_buffer()
    gaps = send_paced(ser, req, 0.0015)
    note("T5 host write-call gaps (1.5 ms target): min=%.3f max=%.3f ms"
         % (min(gaps), max(gaps)))
    response, _, _ = receive_response(ser, 8)
    check("T5 paced request echo completes before next FC03", response == req)
    vals = read_regs(ser, RELAY1, 1)
    check("T5 ~1,5 ms gonderim araligi: kabul", vals == [1])
    request(ser, fc06(SLAVE, RELAY1, 0), 8)

    req = fc06(SLAVE, RELAY1, 1)
    ser.reset_input_buffer()
    gaps = send_paced(ser, req, 0.005)
    note("T5 host write-call gaps (5 ms target): min=%.3f max=%.3f ms"
         % (min(gaps), max(gaps)))
    quiet, extra = expect_silence(ser, 0.15)
    check("T5 ~5 ms gonderim araligi: dusurulur (belgeli taviz)",
          quiet and read_regs(ser, RELAY1, 1) == [0],
          "extra=%s" % extra.hex())
    time.sleep(0.05)
    request(ser, fc06(SLAVE, RELAY1, 1), 8)
    check("T5 retry kabul", read_regs(ser, RELAY1, 1) == [1])
    request(ser, fc06(SLAVE, RELAY1, 0), 8)

    # ---- T6: broadcast write ----------------------------------------
    quiet, extra = expect_silence(ser, 0.3,
                                  after_write=fc06(0, RELAY1, 1))
    check("T6 broadcast: hatta sessizlik", quiet,
          "extra=%s" % extra.hex())
    check("T6 broadcast uygulandi (R1=1)",
          read_regs(ser, RELAY1, 1) == [1])
    request(ser, fc06(SLAVE, RELAY1, 0), 8)

    # ---- T7: exception contracts ------------------------------------
    resp, _, _ = request(ser, fc06(SLAVE, RELAY1, 2), 5)
    check("T7 R1=2 VALUE exception (tam cerceve)",
          len(resp) == 5 and valid_frame(resp) and resp[1] == 0x86
          and resp[2] == 3, "%s" % resp.hex())
    resp, _, _ = request(ser, fc06(SLAVE, AKU, 1), 5)
    check("T7 salt-okunur aku yazma reddedilir",
          len(resp) == 5 and valid_frame(resp) and resp[1] == 0x86,
          "%s" % resp.hex())

    # ---- T8: response time (first byte / complete, ayrı ayrı) -------
    firsts = []
    completes = []
    for _ in range(5):
        _, f, c = request(ser, fc03(SLAVE, AKU, 1), 7, quiet_window_s=0.01)
        firsts.append(f)
        completes.append(c)
    med_first = sorted(x for x in firsts if x is not None)[2]
    med_complete = sorted(completes)[2]
    note("T8 medyan ilk-bayt %.1f ms, tam-yanit %.1f ms "
         "(cevirici katkisi dahil; cihaz katkisi analizor ile ayrilir)"
         % (med_first, med_complete))
    # Olcum kaydi: urun gereksinimi kaynagi olmayan bir ust sinir konmaz.

    # ---- T9: first clean ADC DMA sample reaches the real FC03 path ---
    # No ADC fault is injected in this bench test. Invalid sentinel words
    # must not be returned after normal device startup.
    adc = read_regs(ser, 9010, 4)
    check("T9 ADC sicaklik/gerilim blogu okunur", adc is not None)
    check("T9 ADC DMA ornegi gecerli",
          adc is not None and adc[0] != 0x8000
          and all(value != 0xFFFF for value in adc[1:]),
          "registers=%s" % adc)

    # ---- T10: second relay and restoration of its original state ----
    relay2_before = read_regs(ser, RELAY2, 1)
    check("T10 R2 initial state available",
          relay2_before in ([0], [1]))
    if relay2_before in ([0], [1]):
        try:
            for value in (1, 0):
                command = fc06(SLAVE, RELAY2, value)
                response, _, _ = request(ser, command, 8)
                check("T10 R2=%d echo" % value, response == command)
                check("T10 R2=%d readback" % value,
                      read_regs(ser, RELAY2, 1) == [value])
        finally:
            request(ser, fc06(SLAVE, RELAY2, relay2_before[0]), 8)
            check("T10 R2 original state restored",
                  read_regs(ser, RELAY2, 1) == relay2_before)

    ser.close()
    print("SONUC: %d PASS / %d FAIL / %d NOT" % (passed, failed,
                                                 len(notes)))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
