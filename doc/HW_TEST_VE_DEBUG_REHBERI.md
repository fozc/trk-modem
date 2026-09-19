# Donanım Test ve Debug Rehberi (ST-Link + Seri Konsol)

**Amaç:** Bu rehber, cihazın (troika-smart-breaker-modem) ST-Link ve seri
konsol üzerinden nasıl derlenip yazılacağını, güncelleneceğini ve test
edileceğini adım adım anlatır. 2026-09-19 tarihli donanım kabul test
oturumunda doğrulanmış yöntemleri ve tuzakları biriktirir.

**Kullanım yeri:** Donanım üzerinde firmware güncellemesi, kabullenme
(acceptance) testi, arıza doğrulaması ya da konsol üzerinden tanılama
yapılacağı her durumda kullanılır.

---

## Ön koşullar

| Araç | Konum / değer |
|---|---|
| ARM toolchain | `C:\ST\STM32CubeIDE_2.1.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin` |
| STM32CubeProgrammer CLI | `C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe` |
| Seri konsol | **COM16** (USB Serial Port), **LPUART1 230400 8N1** (PC0/PC1) |
| ST-Link VCP | COM17 (konsol değildir; sadece tanıma) |
| Python | pyserial kurulu olmalıdır (`pip install pyserial`) |
| Yardımcı betikler | `tools/hwtest/serial_io.py`, `tools/hwtest/xmodem_send.py` |

Derleme PATH'i (Git Bash):

```bash
export PATH="/c/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:$PATH"
```

---

## Adımlar

### A.1 Firmware derleme

```bash
make -C Release -j8 all
```

- Release yapılandırması `STM32U375VETX_BOOT.ld` ile bağlanır: uygulama
  **0x08014000**'den başlar, 0x08000000'deki 80 KB'lık bootloader (BSL)
  bölgesine dokunmaz. Debug yapılandırması 0x08000000 kullanır; bootloader
  kurulu cihazda Debug imajı yazılmamalıdır.
- Post-build adımı imzalı paketi otomatik üretir:
  `Release/troika-smart-breaker-modem_v1.0.0_<zaman>_<commit>.efw`
  (bin2efw.py, ECDSA imza + AES; anahtarlar `keys/`).
- Açılış bandındaki "Git Commit" kimliği çalışan binary'den değil boot
  superblock'tan okunur (`Application/version.h` açıklaması). En son BSL
  ile kurulmuş paketin kimliği görünür.

### A.2 Konsoldan komut gönderme ve kayıt alma

```bash
printf 'help\nbootlog dump\n' | python tools/hwtest/serial_io.py \
    capture COM16 230400 15 /tmp/out.log
```

- stdin'deki satırlar açılışta 0.4 sn arayla gönderilir; ardından verilen
  süre boyunca konsol kaydedilir.
- Boot logunu baştan yakalamak için: kaydı arka planda başlat, 1 sn sonra
  cihazı sıfırla (bkz. A.4), kayıt bitince dosyayı incele.
- Konsolda AT trafiği ve `[Stack]` satırları arka plan günlüğüdür;
  filtrelemek için `grep -avE "AT TX|AT RX|\[Stack\]|SI\["` kullanılır.

### A.3 Cihazı sıfırlama (yazılım reseti)

```bash
/c/ST/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe \
    -c port=SWD mode=UnderReset reset=SWRST -rst
```

**ÖNEMLİ:** Bağlantı modu daima `mode=UnderReset reset=SWRST` olmalıdır.
Nedeni: kartta harici watchdog (EWDT) vardır; çekirdek durdurulunca
beslenmez ve hedefi resetleyip bağlantıyı keser. `mode=HotPlug` ile yazma
"failed to erase memory" hatasıyla sonuçlanır.

### A.4 ST-Link ile doğrudan yazma (yalnız binary birebir aynıysa)

```bash
cd Release && /c/ST/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe \
    -c port=SWD mode=UnderReset reset=SWRST -w troika-smart-breaker-modem.elf -v -rst
```

**ÖNEMLİ — BSL geri alma davranışı:** BSL, her açılışta iç flash'taki
imajın CRC'sini boot superblock'taki beklenen değerle karşılaştırır.
Uyuşmazrsa SPI flash'taki onaylı yedek bölümü geri yükler; doğrudan
yazılan farklı imaj sessizce geri alınır (2026-09-19 testinde gözlemlendi).
Bu yüzden **firmware değişikliği daima A.5'teki güncelleme yoluyla**
yapılmalıdır; doğrudan yazma yalnızca birebir aynı binary'yi geri koymak
için kullanışlıdır.

### A.5 Firmware güncelleme (BSL yolu, önerilen)

```bash
# 1) Alici moduna gec (konsol):
printf 'su admin\nxmodem start\n' | python tools/hwtest/serial_io.py \
    capture COM16 230400 6 /tmp/xm0.log
# "XMODEM mode activated" + hedef bolum yazisini ve 'C' davetini dogrula.

# 2) Imzali paketi gonder:
python tools/hwtest/xmodem_send.py COM16 230400 \
    Release/<paket>.efw /tmp/xm1.log
```

- Akış: uygulama paketi SPI **download bölümüne** yazar → BL-21 gereği
  paket kimliğini IPC'ye bağlar → kendi kendine reset → BSL doğrular
  (ECDSA + CRC) ve kurar → yeni imaj açılır.
- Zaman aşmaları: aktarım başlama **1 dk**, hareketsizlik **10 dk**.
  `xmodem start` verdikten sonra 1 dk içinde gönderim başlatılmalıdır.
- Hedef bölüm A/B dönüşümlüdür ("Target area: Firmware_A/B"); eski imaj
  yedek olarak kalır, hatalı kurulumda geri alınır.
- Web arayüzü üzerinden de aynı yollar kullanılabilir (fwupdate sayfası).

### A.6 Shell kabul testi komutları

| Komut | Doğrulanan davranış |
|---|---|
| `help` | Hizalı kullanım blokları; tüm komutlar listelenir |
| `su admin` / `whoami` / `exit` | root'a geçiş ve user'a dönüş (varsayılan parola `admin`) |
| `ps` | Süreç listesi (`shell_task` görünmeli) |
| `exec <ad>` / `kill <ad>` | "Process restarted/killing" (ölü süreç için "not found" doğru davranıştır) |
| `term test` | 127-255 arası genişletilmiş ASCII basımı |
| `bootlog status / dump [n] / dump raw [n]` | Kod çözülmüş ve ham özet |
| `elog dump [n]` | Kalıcı olay günlüğü |
| `pwrboard show` / `gsm status` | Güç kartı ve modem durumu |

### A.7 Naked handler doğrulaması (objdump)

```bash
arm-none-eabi-objdump -d Release/troika-smart-breaker-modem.elf \
    --disassemble=HardFault_Handler
```

Beklenen: tam olarak 6 komut (`tst/ite/mrseq/mrsne/mov/b`), prologue
yok. C kodu içermemelidir (GCC naked fonksiyonlarda C desteklemez).

### A.8 Kasıtlı HardFault testi (APP-1 tipi doğrulama)

1. Test kancası ekle (ör. bir shell handler'ına):
   `__asm volatile("udf #0");` — kullanım notu: geçicidir, test sonrası
   kaldırılmalıdır.
2. Derle (A.1), güncelle (A.5), konsoldan tetikle (A.2).
3. Beklenenler:
   - `HFSR: FORCED` (0x40000000 doğru çözümlenir; eski kaymış bitfield
     bunu "DEBUGEVT" basardı),
   - `Usage Fault: UNDEFINSTR`, ABFSR satırı yok,
   - `*** HardFault: halting - watchdog will reset ***` sonrası ~1-2 sn'de
     EWDT reseti,
   - açılışta elog'a `SYSTEM_HARDFAULT` kaydı (pc/lr/cfsr/hfsr konsol
     dump'ıyla aynı).
4. Kancayı kaldır, temiz imajı A.5 ile geri kur.

### A.9 Bootloader (BSL) boyut bütçesi

```bash
cd ../troika-smart-breaker-modem-bootlodaer   # dizin adındaki yazım hatası gerçektir
make -C Release -j8 all
arm-none-eabi-size Release/troika-smart-breaker-modem-bootloader.elf
```

Bütçe 80 KB (81920 bayt, `text+data`). Ölçüm (2026-09-19): 53 996 bayt
(%66) — INFO/TERM yerleşik komutları açıkken bile ~28 KB boşluk vardır.

---

## Doğrulama

- Yazma/güncelleme sonrası açılış kaydında `SPI flash: AT25SF321B 4 MB
  (JEDEC 0x1F-0x87-0x01)` satırı görülmeli, "Jedec Id Error" olmamalıdır.
- `bootlog dump` son kayıtları `FW_INSTALL_OK` + `FW_APPROVED` olarak
  göstermelidir; bu, kurulumun BSL tarafından doğrulandığının kanıtıdır.
- `ps` çıktısında `shell_task` yoksa `SHELL_USER_CONTIKI_PROCESS` tanımı
  eksik demektir (.cproject derleme tanımlarını kontrol ediniz).

---

## Sık hatalar

| Belirti | Neden | Çözüm |
|---|---|---|
| "failed to erase memory" (yazmada) | EWDT, durdurulan çekirdeği resetliyor | `mode=UnderReset reset=SWRST` kullan |
| Direkt yazılan imaj "yok oluyor" | BSL CRC uyuşmazlığında yedeği geri yüklüyor | Güncellemeyi A.5 (xmodem/web) ile yap |
| `xmodem start` sonrası 'C' gelmiyor | 1 dk başlama timeout'u geçti | Komutu yeniden ver, hemen gönder |
| Aktarım ortasında CAN | Paket CRC/format hatası ya da timeout | Betiği yeniden çalıştır; seyrekse kablo/hız kontrolü |
| Boot logu yakalanamıyor | Kayıt, resetten sonra başlatılmış | Kaydı arka planda başlat, 1 sn sonra reset ver |
| COM16 açılmıyor | Başka uygulama portu tutuyor | Modbus Poll / terminal uygulamalarını kapat |
| `-r` okuma hatası (Programmer) | Argüman eksik (dosya gerekir) | `-r 0x08000000 64 <dosya>` biçimini kullan |

---

## Değişiklik geçmişi

| Tarih | Açıklama |
|---|---|
| 2026-09-19 | İlk sürüm — 2026-09-19 kabul test oturumunda doğrulanmış akışlar (APP-1/2/3 kabulleri, BSL geri alma davranışı, EWDT/UnderReset kuralı) |
