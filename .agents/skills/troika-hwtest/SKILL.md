---
name: troika-hwtest
description: >-
  Troika smart-breaker-modem cihazında donanım testi, firmware derleme/yükleme
  ve debug işlemleri. Kullanıcının ST-Link, COM16, seri konsol, flash, firmware
  güncelleme, xmodem, kabul (acceptance) testi, bootlog/elog testi, hardfault
  testi veya cihaz üzerinden doğrulama dediği HER durumda bu skill kullanılmalıdır
  — kullanıcı "cihaza yükle", "donanımda test et", "flash'la" gibi ifadeler
  kullandığında da. Ayrıntılı adımlar doc/HW_TEST_VE_DEBUG_REHBERI.md içindedir.
---

# Troika Donanım Test / Debug

Cihaz üzerinde derleme, yükleme ve test yaparken **önce
`doc/HW_TEST_VE_DEBUG_REHBERI.md` dosyasını oku** — tüm adımlar, doğrulama
ölçütleri ve sık hatalar tablosu oradadır. Bu dosya yalnızca hızlı özeti ve
kritik kuralları taşır.

## Ortam (bu makine)

- Konsol: **COM16, 230400 8N1** (LPUART1). ST-Link VCP (COM17) konsol değildir.
- Toolchain ve Programmer yolları rehberin "Ön koşullar" tablosundadır;
  derlemeden önce o PATH satırını export et.
- Betikler: `tools/hwtest/serial_io.py` (konsol komut + kayıt) ve
  `tools/hwtest/xmodem_send.py` (XMODEM-CRC gönderici). Yenilerini yazma;
  bunları kullan.

## Kritik kurallar (atlanırsa iş bozulur)

1. **Firmware değişikliği asla doğrudan ST-Link yazmasıyla yapma.** BSL,
   CRC'si superblock ile uyuşmayan imajı sessizce geri yükler. Güncelleme
   daima konsoldan `su admin` → `xmodem start` → `xmodem_send.py` akışıyla
   (imzalı `.efw`) yapılır.
2. **Programmer bağlantısı daima `mode=UnderReset reset=SWRST`** olmalıdır;
   HotPlug, harici watchdog (EWDT) yüzünden "failed to erase memory" verir.
3. **XMODEM başlama timeout 1 dakikadır** — `xmodem start` verdikten sonra
   gönderimi hemen başlat.
4. Boot logunu yakalamak için seri kaydı arka planda başlat, 1 sn sonra
   cihazı resetle.
5. Test için koda geçici kanca (ör. `udf #0`) eklendiğinde, test bitince
   kaldır ve temiz imajı yeniden kur; cihazı test kancalı firmware'de
   bırakma.

## Tipik akış

1. `make -C Release -j8 all` (Release, BSL bölgesine dokunmaz; Debug yazma).
2. Güncelleme: rehber A.5 (xmodem + `.efw`).
3. Konsol testleri: rehber A.2 + A.6 (`help`, `su admin`, `ps`, `bootlog
   dump`, `elog dump` ...).
4. Doğrulama: açılışta `SPI flash: AT25SF321B 4 MB` satırı ve `bootlog
   dump` içinde `FW_INSTALL_OK`/`FW_APPROVED` olmalıdır.
