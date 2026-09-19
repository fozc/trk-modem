"""Seri konsol yardimcisi - donanim testleri icin (COM16 / LPUART1 230400 8N1).

Kullanim:
  python serial_io.py capture <port> <baud> <saniye> <cikti_dosyasi>

stdin'e yazilan satirlar (bos ve '#' ile baslayanlar haric) acilista
0.4 sn arayla cihaza gonderilir; ardindan <saniye> boyunca gelen tum
baytlar cikti dosyasina yazilir. '# ...' satirlari yorum sayilir.

Ornek:
  printf 'help\nbootlog dump\n' | python serial_io.py capture COM16 230400 15 out.log
"""
import sys
import time
import serial

def main():
    if len(sys.argv) < 6 or sys.argv[1] != "capture":
        print(__doc__)
        sys.exit(1)

    port, baud = sys.argv[2], int(sys.argv[3])
    seconds = float(sys.argv[4])
    outfile = sys.argv[5]
    lines = [l.rstrip("\n\r") for l in sys.stdin if l.strip() and not l.startswith("#")]

    ser = serial.Serial(port, baud, timeout=0.2)
    ser.reset_input_buffer()
    for line in lines:
        ser.write((line + "\r").encode())
        time.sleep(0.4)
    with open(outfile, "wb") as f:
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                f.write(data)
                f.flush()
    ser.close()

if __name__ == "__main__":
    main()
