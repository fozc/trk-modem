"""Minik XMODEM-CRC gonderici (128 bayt blok) - BSL/uygulama guncelleme yolu.

Kullanim:
  python xmodem_send.py <port> <baud> <efw_dosyasi> [log_dosyasi]

Alicinin 'C' davetini bekler, dosyayi SOH/128B/CRC16-CCITT paketleriyle
gonderir, EOT ile bitirir. Aktarim sirasinda alicidan gelen her sey
log dosyasina yazilir.

Once cihazda alıcı moduna gecilmelidir:  su admin  ->  xmodem start
Baslama timeout 1 dk, hareketsizlik timeout 10 dk'dir (bkz. doc rehberi).
"""
import sys
import time
import serial

SOH = 0x01
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
PAD = 0x1A

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc

def read_resp(ser, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if b:
            return b[0]
    return None

def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)

    port, baud, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    logfile = sys.argv[4] if len(sys.argv) > 4 else None
    data = open(path, "rb").read()

    ser = serial.Serial(port, baud, timeout=0.2)
    ser.reset_input_buffer()
    logf = open(logfile, "wb") if logfile else None

    print("alici daveti 'C' bekleniyor ...")
    deadline = time.time() + 60
    got_c = False
    while time.time() < deadline:
        b = ser.read(64)
        if b and logf:
            logf.write(b); logf.flush()
        if b and 0x43 in b:
            got_c = True
            break
    if not got_c:
        print("HATA: alicidan 'C' gelmedi")
        sys.exit(1)
    print("alici hazir")

    seq = 1
    i = 0
    retries = 0
    while i < len(data):
        chunk = data[i:i + 128]
        chunk = chunk + bytes([PAD] * (128 - len(chunk)))
        pkt = bytes([SOH, seq & 0xFF, (~seq) & 0xFF]) + chunk
        c = crc16(chunk)
        pkt += bytes([(c >> 8) & 0xFF, c & 0xFF])
        ser.write(pkt)
        resp = read_resp(ser)
        if resp == ACK:
            i += 128
            seq = (seq + 1) & 0xFF
            retries = 0
            if (i // 128) % 64 == 0:
                print("gonderildi %d/%d" % (i, len(data)))
        elif resp == NAK:
            retries += 1
            print("NAK @ %d (tekrar %d)" % (i, retries))
            if retries > 16:
                print("HATA: cok fazla NAK")
                sys.exit(1)
        elif resp == CAN:
            print("HATA: alici iptal etti (CAN)")
            sys.exit(1)
        else:
            b = ser.read(256)
            if b and logf:
                logf.write(b); logf.flush()
            retries += 1
            if retries > 16:
                print("HATA: %d ofsetinde yanit yok" % i)
                sys.exit(1)

    ser.write(bytes([EOT]))
    resp = read_resp(ser)
    if resp == NAK:
        ser.write(bytes([EOT]))
        resp = read_resp(ser)
    tail = ser.read(256)
    if (resp == ACK) or (tail and ACK in tail):
        print("EOT onaylandi - aktarim tamam")
    else:
        print("EOT yaniti: %r kuyruk=%r" % (resp, tail[:32] if tail else None))
    if logf and tail:
        logf.write(tail); logf.flush()
    time.sleep(1)
    b = ser.read(4096)
    if b and logf:
        logf.write(b); logf.flush()
    ser.close()

if __name__ == "__main__":
    main()
