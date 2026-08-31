# PowerBoard I2C Protokol Referansı — Lokal Sentez

> ### 🔒 BOLATeX — GİZLİ / TESCİLLİ BİLGİ
>
> Bu belge ve içerdiği protokol, arayüz ve algoritma tasarımları
> BOLATEX MÜHENDİSLİK DANIŞMANLIK SANAYİ VE TİCARET LİMİTED ŞİRKETİ
> ("BOLATeX") tarafından geliştirilmiştir; her türlü fikrî ve sınai mülkiyet hakkı
> BOLATeX'e aittir.
>
> KULLANIM KOŞULLARI
>   1. Yalnızca **Ayırıcı (Sectionalizer) Modem projesi** kapsamında kullanılabilir.
>   2. Başka hiçbir amaçla, başka bir üründe, projede veya türev çalışmada kullanılamaz.
>   3. BOLATeX'in **ÖNCEDEN YAZILI İZNİ OLMADAN** hiçbir kurum, kuruluş, firma veya
>      üçüncü şahıs ile paylaşılamaz; çoğaltılamaz, yayımlanamaz, devredilemez.
>   4. Bu koşullar belgenin tamamı ve her parçası için — örnek kod parçacıkları ve
>      türetilmiş dokümanlar dâhil — geçerlidir.
>
> BOLATEX MÜHENDİSLİK DANIŞMANLIK SANAYİ VE TİCARET LİMİTED ŞİRKETİ
>   Adres    : Yahyakaptan Mahallesi Elzem Sokak No:54 İç Kapı No:102
>              41050 İzmit / KOCAELİ
>   E-posta  : info@bolatex.com.tr
>   Web      : www.bolatex.com.tr
>   Telefon  : 0532 743 4969


> ## 📋 DOKÜMAN SÜRÜMÜ **Rev1.0** · TEL FORMATI **PROT_VER = 0x09** ⚠️ **DEĞİŞTİ (2026-08-30)**
>
> **Rev0.9 → Rev1.0 (PROT_VER 0x08 → 0x09), kullanıcı kararı 2026-08-30:** `0x4D PANIC_CAUSE` (PWR_PANIC nedeni), `0x4E PSYS_ST`, `0x4F CAL_VER` tahsis edildi (önceden rezerve/0x00); `0xA8 PSYS_MW` anlamı değişti (akü modunda **ölçülen** sistem yükü, tohum 690/1020 mW); `0x49 Q` `DERIVED` üretilmez; **PWR_PANIC** VBAT kolu **11,0/11,5 V** ve yalnız akü modunda (K19). Yerleşim, blok boyu ve XSUM kuralı **aynı**; yalnız 0x4D–0x4F artık dolu olduğu için 0x5F değeri değişir. Üst kart `0x1E = 0x09` bekler.
>
> *(tarihsel)* ## DOKÜMAN SÜRÜMÜ Rev0.9 · TEL FORMATI PROT_VER = 0x08
>
> **İki ayrı sürüm numarası vardır — karıştırmayın:**
>
> | | Ne | Şu an | Ne zaman artar |
> |---|---|---|---|
> | **`PROT_VER` (0x1E)** | **Tel-üstü format sözleşmesi** — register offsetleri, alan tipleri, blok uzunlukları, XSUM kuralı, bayt sırası | **`0x08`** | **YALNIZ** tel formatı değişince (yeni/kaymış alan, blok boyu, bütünlük kuralı). Karşı tarafın ayrıştırma kodu değişmek zorunda kalırsa artar |
> | **Doküman revizyonu** | Bu MD'nin sürümü — açıklama, sınır, kural, tablo zenginleşmesi | **Rev0.9** | Anlatım/kapsam genişleyince. **Tel formatını ETKİLEMEZ** |
>
>
> ⛔ **PROT_VER 0x08 KİLİTLİDİR (kullanıcı kararı 2026-08-29).** Üst kart (Fatih)
> entegrasyonu bu sürümle **sınanana kadar** tel formatı **DEĞİŞMEZ**: yeni alan tahsisi,
> alan kaydırma, blok boyu, bütünlük kuralı — hiçbiri. Yeni gereksinim doğarsa
> **birikir** (`docs/DEGISIM_RAPORU_UST_KART.md`) ve entegrasyon sınavından **sonra**
> tek bump ile (0x09) çıkar. Gerekçe: 27.08'de 0x06 için uyarılmıştı, iki günde 0x08
> oldu; sözleşme kararsızlığı karşı tarafın emeğini riske atıyor.
>
> **Sürüm eşlemesi (tek şema — 2026-08-29 tutarlılık düzeltmesi):** belge revizyonu ile
> `PROT_VER` 0x06'ya kadar bire bir gidiyordu; **Rev0.7 (2026-08-01) yalnız belge**
> revizyonuydu, o günden beri kayma var: **Rev0.8 ↔ 0x07** (`0x4A CHG_REAL`) ·
> **Rev0.9 ↔ 0x08** (`0x4B/0x4C` + `0x1A/0x63` işaretli). Tabloda daha önce
> "Rev0.7 YENİ"/"Rev0.8 YENİ" diye geçen 0x4A/0x4B/0x4C etiketleri bu şemaya çekildi.
> ⚠️ **Rev0.8 → Rev0.9 TEL FORMATI DEĞİŞTİ (PROT_VER 0x07 → 0x08).** Rezerve olan
> **`0x4B` ve `0x4C` baytları BQ TAZELİK bilgisine tahsis edildi** (K17,
> kullanıcı kararı 2026-08-28: *"BQ25'ten veri okunmadan PC'ye veri gitmesin;
> BQ25 down ise DOWN bilgisi gitsin protokollerde"*).
>
> | reg | ad | anlam |
> |---|---|---|
> | **`0x4B`** | **BQ_YAS_DS** | `0` = **TAZE** · `1..255` = **BAYAT/DOWN**, değer = son başarılı poll'dan geçen süre (**0,1 s**, doygun 255 = ≥25,5 s) |
> | **`0x4C`** | **BQ_ERR_N** | Kümülatif **başarısız poll** sayısı (**mod-256 sarar**, doygun DEĞİL — kod `err_tot & 0xFF`; üst kart ardışık okumaların farkını alır) |
>
> ⭐ **ÜST KART ŞARTI:** `0x4B != 0` iken **BQ kaynaklı alanlar BAYATTIR** ve
> karara esas alınmaz: `0x03` CHG_STAT_RAW · `0x10` VBAT · `0x23` BATT_TS ·
> `0x26/0x27` BQ_FAULT0/1 · `0x28` CHG_STAT · `0x29` ICO_STAT ·
> `0x40-0x43` BQ_REG1B/1D/1E/1F · `0x4A` CHG_REAL · `0x50` BQ_VSYS_MV.
> Alanlar **silinmez** (zaman sürekliliği korunsun, "veri yok" ile "kart yok"
> karışmasın) — eleme kararı üst kartındır.
>
> ⚠️ **`0x4C` NEDEN AYRI:** yaş her **başarılı** poll'da sıfırlanır; sahada en
> olası arıza kipi olan **aralıklı NACK/timeout** ("4 başarısız + 1 başarılı")
> yaşta **hiç görünmez**. Kaynak: `docs/DENETIM_FW_2026-08.md` (216-218).
>
> *(Önceki değişim — Rev0.7 → Rev0.8, PROT_VER 0x06 → 0x07: rezerve olan
> **`0x4A` baytı `CHG_REAL` olarak tahsis edildi**, K15.)* `0x00-0x49` ve `0x50-0x5F`
> **bayt-bayt AYNI** kaldı; yalnız bir rezerve bayt doldu ve bu nedenle **`BLK_XSUM`
> (0x5F) değeri değişti**. Üst kart tarafında gereken: ① beklenen `PROT_VER` sabiti
> `0x06 → 0x07` ② `0x4A`'nın okunması (isteğe bağlı ama önerilir).
> ⛔ **TEK SÖZLEŞME — geçmişe-uyum katmanı YOK** (2026-08-27 kullanıcı kararı; aynı
> kural `0x04` ve `0x06` geçişlerinde de uygulandı). `0x1E != 0x08` görülürse telemetri
> **REDDEDİLİR**; "eski sürümle de çalışır" yolu **YOKTUR**.
>
> *(Rev0.6 → Rev0.7 tel formatında hiçbir şey değiştirmemişti; o revizyon yalnız daha
> önce yazılı olmayan elektriksel/zamansal sınırları ve arayüzün I2C-dışı kısmını
> belgelemişti.)*
> Farkların listesi: dosya sonundaki **Revizyon Notu — Rev0.8**.

> **Kapsam:** Haberleşme Modülü Power Board (STM32C011) I2C protokolünün eksiksiz referansı — HEM 16KB
> (F4P6, `dev-16k`) HEM 32KB (F6P6, `dev-32k`) versiyonu. **İki mantıksal I2C arayüzü** kapsanır:
> (a) STM32↔BQ25798 (şarj kontrolcüsü), (b) STM32↔üst kart (telemetri/kayıt protokolü).
>
> **Otorite sırası:** KOD birincil gerçek (register/komut/bit koddan çıkar) · PDF/EDIF/MD ikincil
> (donanım bağlamı). Çelişki KODDAN çözülür AMA açıkça raporlanır (bkz. §Çelişkiler). Lokal kaynakta
> bulunamayan bilgi `[AÇIK]`; tek kaynaklı iddia `[TEK-KAYNAK]`. **Web kullanılmadı** (lokal sentez).
>
> **Üretim:** çok-ajanlı lokal tarama (16K kod / 32K kod / donanım-dokümanı) + sentez. Working-tree (commit yok).

---

## Taranan kaynaklar

**Kod (birincil):** `Src/i2c.c`, `Inc/i2c.h` · `Src/bq25798.c`, `Inc/bq25798.h` · `Src/bq25798_poll.c`,
`Inc/bq25798_poll.h` · `Src/i2c_protocol.c`, `Inc/i2c_protocol.h` · `Src/stm32c0xx_it.c` · `Src/main.c`
(orkestrasyon) · `Inc/feature_config.h` — her iki dal (`dev-16k` ve `dev-32k`).

**Donanım/doküman (ikincil):** `Haberlesme-Modulu_Power-Board_2025-11-14_R0-00.PDF` + `_EDF.txt` (EDIF
netlist, devre gerçeği) · `DOC/` BQ25798 datasheet **SLUSDV2C Rev C** §7.3.14 · `BQ25798_Register_Reference.md` ·
`PWR_I2C_Protocol.md` · `PowerBoard_FW/CLAUDE.md`.

> ⚠️ **Rev B → Rev C atıf kaydı (2026-07-21):** Eski `SLUSDV2B` PDF **silindi**; güncel datasheet
> **SLUSDV2C Rev C**'dir. Rev C'de bölüm/figür/tablo numaraları kaydı: `§9.3.x → §7.3.x`, `Fig 9-xx → 7-xx`,
> `Tablo 9-xx → 7-xx`. Bu dosyada **Rev C numaralandırması** kullanılır (eski `§9.3.14` atıfları güncellendi).
> Otorite: `BQ25798_Register_Reference.md` (SSOT) + `PowerBoard_FW/CLAUDE.md`. [PROJE]

---

## 1. Genel — rol, adres, saat, fiziksel katman

**Rol:** STM32C011 **daima I2C MASTER** (tek `I2C1` çevre birimi, `hi2c1`). İki slave aynı fiziksel bus'ta,
TCA9416 seviye-çevirici köprüsü üzerinden. STM32 tarafında IT/DMA **yok** — tüm erişim **blocking/polling**.
*(Kaynak: `i2c.c:40-51`; `i2c.h`; ISR yokluğu `stm32c0xx_it.c` — §6.)*

**Slave adresleri:**

| Cihaz | 7-bit | HAL 8-bit (W / R) | Sabit mi | Kaynak |
|---|---|---|---|---|
| BQ25798 (şarj) | `0x6B` | `0xD6` / `0xD7` | donanımsal sabit | KOD `bq25798.h:63-64` ↔ DS SLUSDV2C §7.3.14 (Fig 7-19) |
| Üst kart (STM32U375) | `0x48` | `0x90` / `0x91` | firmware sabiti | KOD `i2c_protocol.h:41-45` ↔ `PWR_I2C_Protocol.md §1` |

**Fiziksel katman (A-tarafı: STM32 + BQ):** SCL=**PB6** (`CHARGER_SCLA`), SDA=**PB7** (`CHARGER_SDAA`);
AF6, open-drain, dahili pull yok. Harici pull-up **R8(SCL)=R10(SDA)=2.2 kΩ → 3V3OVP**. A-tarafı kısa
yerel bus (ferrit/seri direnç yok). *(KOD `i2c.c:98-105` ↔ EDIF `CHARGER_SCLA/SDAA`, R8/R10; `PowerBoard_FW/CLAUDE.md` pin haritası — çapraz-doğrulandı.)*

**TCA9416DDFR köprü:** A-tarafı VCCA=**3V3OVP** ↔ B-tarafı VCCB=**3V3** seviye çevirici. B-tarafı: TCA →
**L22/L23 600R ferrit** → {**R107/R108=2.2 kΩ pull-up → 3V3**, **R109/R110=47 Ω seri**} → **J21 pin9=SDA,
pin10=SCL** → üst kart. *(EDIF `CHARGER_SDAB/SCLB`, U1; `PWR_I2C_Protocol.md §2` — çapraz-doğrulandı.)*

**Saat hızı:** **400 kHz Fast Mode** — timing register `hi2c1.Init.Timing = 0xB0110305` (PRESC=0xB,
SCLL≈1.5µs, SCLH≈1.0µs, SDADEL=250ns, SCLDEL=500ns @48MHz PCLK1). *(KOD `i2c.c:41-44` — OTORİTER.
BQ datasheet ≤400 kbit/s destekler; `BQ25798_Register_Reference.md` çelişik "100 kHz" der → bkz. §Çelişkiler.)*

**Clock stretching:** İZİN VERİLİR (`NoStretch=DISABLE`, `i2c.c:51`). BQ25798 SCL'i LOW tutabilir
(DS §7.3.14.3). Üst kart sözleşmesi: tek işlem ≤10 ms sürmeli (master timeout, `I2CPROT_TIMEOUT_MS`).

**Timeout'lar (KOD, 2026-08-01 yeniden doğrulandı):**

| Yol | Sabit | Değer | Kaynak |
|---|---|---|---|
| BQ25798 (0x6B) | `BQ25798_I2C_TIMEOUT_MS` | **20 ms** | `Inc/bq25798.h:70` |
| Üst kart (0x48) | `I2CPROT_TIMEOUT_MS` | **10 ms** | `Inc/i2c_protocol.h:48` |

> ⚠️ **DÜZELTME (Rev0.7):** bu satır önceden *"BQ = **100 ms** (`bq25798.h:67`)"* diyordu — **bayat**.
> Kodda değer `20U`'dur (commit `e4962fc` sağlık denetiminde düşürülmüş). 100 ms rakamına dayanarak
> yapılmış bir bloklanma hesabı varsa **5× hatalıdır**. Bkz. §6c bloklanma bütçesi.

**⏱️ HAL timeout semantiği (Fatih için kritik):** `Timeout` parametresi, HAL fonksiyonuna
**girişte bir kez** alınan `tickstart`'a göre ölçülür (`stm32c0xx_hal_i2c.c:1137,1192` —
`tickstart = HAL_GetTick()` → `I2C_WaitOnTXISFlagUntilTimeout(hi2c, Timeout, tickstart)`).
Yani **bayt-başına değil, İŞLEMİN TAMAMI için tek bir son-tarihtir.** Slave'in clock-stretching
bütçesi buradan çıkar → **§6b**.

> **Kaynak §1:** `i2c.c:40-105` · `bq25798.h:63-70` · `i2c_protocol.h:44-48` · EDIF `_EDF.txt`
> (CHARGER_SCLA/SDAA/SDAB/SCLB, R8/R10/R107/R108/R109/R110/L22/L23, U1/U19/U3) · DS SLUSDV2C §7.3.14 ·
> `PowerBoard_FW/CLAUDE.md` pin haritası.

---

## 1b. ⭐ ARAYÜZÜN TAMAMI — J21 (I2C **DEĞİL** yan-bant hatlar dâhil) `[Rev0.7 YENİ]`

> **Fatih için:** Power Board ↔ üst kart arayüzü **yalnız I2C değildir**. Aşağıdaki hatlar
> J21 üzerinden gider ve bir kısmı I2C'den **bağımsız, daha hızlı** yollardır. Slave tasarımı
> bunları da kapsamalıdır.

### 1b.1 🔴 `PWR_PANIC` — J21 pin 38 (I2C-DIŞI yan-bant, en kritik)

| | |
|---|---|
| **Fiziksel** | STM32 **PA12** → R6 = 47 Ω seri → net `OVP_PV` → **J21.38** |
| **Yön** | Power Board → **üst kart** (çıkış / push-pull) |
| **Polarite** | **AKTİF-LOW.** `LOW` = PANİK · `HIGH` = normal |
| **Boot değeri** | `HIGH` (gpio.c:62 `HAL_GPIO_WritePin(..., OVP_PV_Pin, GPIO_PIN_SET)`) |
| **Net adı uyarısı** | EDIF'teki net adı hâlâ **`OVP_PV`** — **YANILTICI/TARİHSEL**. 2026-07-22'de pinin işlevi OVP alarmından enerji-paniğe **yeniden atandı**. OVP artık yalnız I2C ALARM telemetrisindedir (`0x30/0x31` bit6) |

**LOW'a çekilme koşulları** (`main.c` `System_EnergyPanicTask` + termal panik, histerezisli):

| # | Koşul | Eşik (kod) |
|---|---|---|
| 1 | **Akü modu** (STM VPV<2,5 V ∧ VDC<2,5 V; çıkış >3,5 V histerezis) **ve** VBAT düşük | **K19 (2026-08-30):** STM raw `< 1231` = **11,0 V** terminal (ARM) / `> 1287` = **11,5 V** (temizle); BQ yedeği 11,06/11,56 V |
| 2 | BQ taze **ve** VSYS backstop (akü moduna KAPILI DEĞİL — "giriş var ama yetersiz" köşesi) | `VSYS < 11.0 V` (ARM) / `> 11.5 V` (temizle) |
| 3 | Etkin akü sıcaklığı soğuk-kritik | `< −20.0 °C` (ARM) / `≥ −17.0 °C` |
| 4 | Etkin akü sıcaklığı sıcak-kritik | `> +60.0 °C` (ARM) / `≤ +57.0 °C` |

*(Sabitler `main.c` `PANIC_VBAT_ON/OFF_MV`, `PANIC_VSYS_ON/OFF_MV`, `PANIC_TEMP_*_D10`.)*

**Üst kartın beklenen tepkisi:** `LOW` görünce **derhal** (I2C beklemeden) GSM/modem susturulur
ve kalıcı veri (SoC/enerji/ömür) Flash'a yazılır. **Bu sinyal I2C'den bağımsızdır ve I2C
bloklansa/ölse bile çalışır.** Nedeni ayırt etmek için `0x30 ALARM_LIVE` bit4 `NTC_COLD`
okunur (soğuk-kritik paniği bu bitle birlikte gelir).

> ⚠️ Panik nedeni I2C üzerinden **ayrıca kodlanmış bir alan olarak gelmez** — üst kart nedeni
> alarm bitlerinden ve son telemetriden çıkarır. Bkz. §Bilinen Boşluklar.

### 1b.2 `CHARGER_STAT` — J21 pin 6

| | |
|---|---|
| **Kaynak** | **BQ25798 STAT pini (U3.1)** — STM32'den **GEÇMEZ**, firmware sürmez |
| **Yön** | BQ25798 → üst kart (**open-drain**) |
| **Devre** | D9 (kırmızı LED) + R29 = 2.2 kΩ → **REGN** · **TP1** test pad'i aynı nette |
| **Anlam** | BQ'nun kendi şarj-durum göstergesi (DS SLUSDV2C STAT davranışı) |

> Üst kart bunu **bilgi/teşhis** olarak kullanabilir; **otoritatif şarj fazı `0x28 CHG_STAT`
> (debounce'lu) veya `0x03 CHG_STAT_RAW`'dır**. STAT pini firmware sözleşmesinin parçası değildir.

### 1b.3 J21 tam pin listesi (EDIF `_EDF.txt`, `docs/hardware-netlist-reference.md` §J21)

| Pin | Net | Yön (Power Board bakışı) | Not |
|---|---|---|---|
| 1, 2 | `5V` | **çıkış** (güç) | üst karta besleme |
| 3, 4, 11, 21, 22, 31, 32 | `GND` | — | |
| **6** | `CHARGER_STAT` | çıkış (BQ open-drain) | §1b.2 |
| **9** | I2C **SDA** (B tarafı) | çift yön | §1 fiziksel katman |
| **10** | I2C **SCL** (B tarafı) | **çıkış** (master) | §1 |
| 12 | `3V3` | **GİRİŞ** | ⚠️ **üst karttan gelir** (U1 TLV1117LV33 → J12.12 → J21.12); alt kartta üretilmez. TCA9416 VCCB bu raydadır |
| 13 / 15 / 17 / 18 | `BMS_TXD` / `BMS_RXD` / `BMS_OE` / `BMS_RE` | RS485 (BMS) | ST3485 (U7) — 3V3 üst karttan |
| 23 / 25 / 27 | `MODBUS_TXD` / `MODBUS_RXD` / `MODBUS_OE` | RS485 (Modbus) | ISOW1412 (U6) |
| 29 / 30 | `RELAY1_PWM` / `RELAY2_PWM` | **giriş** | üst kart röleleri sürer |
| 33-36 | `DIGITAL_IN1..4` | **giriş** | 74LVC1G17 buffer + LTV-356 opto |
| 37 | `MODBUS_EN/FLT` | — | |
| **38** | `OVP_PV` → **`PWR_PANIC`** | **çıkış (aktif-LOW)** | §1b.1 ⭐ |

> Listelenmeyen J21 pinleri EDIF'te **hiçbir nete bağlı değildir**.
>
> 🚨 **Besleme topolojisi tuzağı:** `3V3` rayı **üst karttan gelir** (J21.12). Üst kart 3V3'ü
> düşerse **TCA9416'nın VCCB'si ölür** → I2C B-tarafı kaybolur. `VCCB=0` iken TCA9416'nın
> A-tarafını yükleyip yüklemediği **lokal kaynaktan doğrulanamadı** → §Bilinen Boşluklar.

---

## 2. Frame yapısı — bayt-bayt işlem dizileri

Tüm erişimler HAL **Mem** API'si (`HAL_I2C_Mem_Read/Write`) — donanım repeated-start'ı bunlara gömülü.
Register adresi daima **8-bit** (`I2C_MEMADD_SIZE_8BIT`). Çok-baytlı değerler **MSB-first** (big-endian).

> ⚠️ **BYTE-ORDER SÖZLEŞMESİ (açık kural — 2026-07-22 netleştirmesi):** Protokoldeki TÜM
> çok-baytlı alanlar (u16/u32/int16/int32, PUSH + PULL + LASTGASP + CFG_TCAL dahil)
> **MSB-first (big-endian)** — düşük adresli bayt = MSB. Karar gerekçesi: (a) BQ25798'in
> kendi 16-bit register'ları da MSB-first → tüm bus'ta tek düzen (BQ ham aynaları 0x40-43
> yorumsuz iletilir); (b) logic-analyzer/skop dökümünde insan-okunur; (c) I2C register-map
> cihaz geleneği (SMBus). **Her iki MCU da little-endian Cortex-M** olduğundan iki tarafta da
> serileştirme shift/mask ile yapılmalı (`val>>8`, `val&0xFF` — kod: `Prot_SetU16/U32`);
> **packed-struct'ı buffer'a CAST/memcpy ETMEYİN** — LE çekirdekte baytlar ters gider
> (karşı tarafta en sık yapılan hata). 16K ilk sürümden beri değişmemiştir (parite).

**BQ25798 tek-bayt yazma** (`bq25798.c:110-116`):
```
[S][0xD6 (0x6B|W)][ACK][RegAddr][ACK][Data][ACK][P]
```
**BQ25798 tek-bayt okuma** (`bq25798.c:94-100`):
```
[S][0xD6][ACK][RegAddr][ACK][Sr][0xD7 (0x6B|R)][ACK][Data][NACK][P]
```
**BQ25798 çok-baytlı okuma (auto-increment)** — DS §7.3.14.7 register sınırını aşabilir; adres otomatik artar.
Örn. poll status bloğu (7 bayt, REG1B→REG21) tek işlem (`bq25798_poll.c:63`):
```
[S][0xD6][RegAddr=0x1B][Sr][0xD7][D(1B)][D(1C)]...[D(21)][NACK][P]
```
ADC bloğu 18 bayt (REG31→REG42) tek okuma (`bq25798_poll.c:69`). 16-bit değerler reg=MSB, reg+1=LSB.

**Üst kart telemetri PUSH** (96 bayt, offset 0x00'dan) (`i2c_protocol.c` — 16K `:377-379`, 32K `:414-416`):
```
[S][0x90 (0x48|W)][RegAddr=0x00][D(0x00)][D(0x01)]...[D(0x5F)][P]   ; 96 veri baytı
```
**Üst kart PULL (okuma; ör. REC_ACK 0x72)** (`i2c_protocol.c` 32K `:433-435`):
```
[S][0x90][RegAddr][Sr][0x91 (0x48|R)][Data...][NACK][P]
```
**Üst kart ACK yoklaması** (mod probe): `HAL_I2C_IsDeviceReady(0x90, 1 deneme, 10ms)` — yalnız adres+ACK,
veri yok (`i2c_protocol.c` 32K `:544`).

**Bütünlük (Rev0.4, 32K — 2026-07-22 kullanıcı kararı): TEK ORTAK KURAL**

> ⭐ **HER I2C bloğunun SON baytı bütünlük baytıdır: `XOR(kendinden önceki TÜM baytlar) ^ 0x5A`.**
> Veri ortasında bütünlük baytı OLMAZ. Tuz `0x5A` (SALT): ölü-hat 0x00/0xFF desenleri
> (boş slave, çekili SDA, NACK-0xFF) sahte-geçemez; restore marker'ı 0xA5'ten farklı seçildi.
> Tek uygulama noktası: `Prot_Xsum()` (32K `i2c_protocol.c`). **Geçmişe-uyum YOK — tek sözleşme.**

| # | Blok | Yön | Bütünlük baytı (SON bayt) | Kapsam |
|---|---|---|---|---|
| 1 | Telemetri 96 B | PUSH | **0x5F** = `XOR(0x00..0x5E)^0x5A` — ⚠️ eski 0x1F konumu KALKTI, 0x1F artık REZERVE (0x00) | tüm blok |
| 2 | LASTGASP 27 B | PUSH | bayt 26 = `XOR(0..25)^0x5A` | tüm blok (marker 0xB5 dahil) |
| 3 | RESTORE 18 B | PULL | **0x71** = `XOR(0x60..0x70)^0x5A` + marker 0xA5 kontrolü | tüm blok |
| 4 | STATBLK 7 B (0x72-0x78) | PULL | **0x78** = `XOR(0x72..0x77)^0x5A` | REC_ACK+CFG_GEN+CAP+TCAL+**CRATE** (Rev0.5) |
| 5 | **GUC 21 B (0xA0-0xB4)** | PUSH | **0xB4** = `XOR(0xA0..0xB3)^0x5A` | 5 x int32 mW (Rev0.6 YENI) |

- XSUM tutmazsa blok **REDDEDİLİR**: telemetri/LASTGASP'ta alıcı (üst kart) atar; RESTORE'da
  default'larla devam; STATBLK'ta o okuma yok sayılır (REC→retry, CFG→sonraki periyot).
- STATBLK tek işlemde okunur → bit hatası VE **yırtık-okuma** aynı XSUM'la yakalanır
  (üst kart bloğu gölge-tamponla atomik günceller — bkz. Fatih dokümanı §6.10/S5).
- *(Tarihsel: Rev0.3'te üç bağımsız checksum vardı; BLK_XSUM 0x1F'teydi (veri ortası),
  tuz yoktu, REC_ACK/CFG registerleri korumasızdı. 16K FROZEN bu eski davranıştadır.)*

**BQ çerçevesinde CRC/PEC YOK** — bütünlük yalnız ACK/NACK (DS §7.3.14; çapraz: `BQ25798_Register_Reference.md`
çerçeveleri CRC içermez).

> **Kaynak §2:** `bq25798.c:94-180` · `bq25798_poll.c:63-76` · `i2c_protocol.c` (PUSH/PULL/XSUM satırları
> yukarıda) · DS SLUSDV2C §7.3.14.3/§7.3.14.7 (Fig 7-21..24).

---

## 3. Register haritaları

### 3a. BQ25798'de OKUNAN/YAZILAN register'lar (her iki dal ÖZDEŞ)

| Reg | Adr | R/W | Boyut | Firmware kullanımı / yazılan değer |
|---|---|---|---|---|
| VSYSMIN | 0x00 | W | 8b | (mV-2500)/250; 12V→0x26 (`bq25798.c:236-250`) |
| VREG | 0x01 | W | 16b | mV/10; 14.4V→0x05A0 (`:252-266`) |
| ICHG | 0x03 | W | 16b | mA/10; 1200mA→0x0078 (`:268-282`) |
| IINDPM | 0x06 | R/W | 16b | Init 3300mA→330; poll'da doğrula+geri-yaz (`:397`, `bq25798_poll.c:218-240`) |
| RECHG/CELL | 0x0A | RMW | 8b | CELL=4S `UpdateReg(0x0A,0xC0,0xC0)` (`:363-364`) |
| CHG_CTRL0 | 0x0F | RMW | 8b | EN_CHG(b5) set/clear; EN_HIZ(b2) poll'da temizlenir (`:291,306`, `poll:175-179`) |
| CHG_CTRL1 | 0x10 | RMW | 8b | WD_RST(b3)=watchdog reset, her poll (`poll:75-76`, `main.c:247-248`) |
| CHG_CTRL5 | 0x14 | W | 8b | EN_IBAT `0x36` (POR 0x16 | EN_IBAT 0x20) (`:408`) |
| MPPT_CTRL | 0x15 | R/W | 8b | 32K: `0xAD` (VOC_RATE=10dk, define'lı — bench B-2) · 16K FROZEN: `0xAB`; poll'da EN_MPPT doğrula+geri-yaz (`:323`, `poll:189-208`) |
| NTC_CTRL0 | 0x17 | W | 8b | JEITA `0x6A` (`:425`) |
| NTC_CTRL1 | 0x18 | W | 8b | JEITA `0x24` (`:430`) |
| CHG_STATUS0..4 | 0x1B-1F | R | 5×8b | poll status bloğu (`poll:63`) |
| FAULT0/1 | 0x20-21 | R | 2×8b | poll fault (`:196,201`) |
| ADC_CTRL | 0x2E | W | 8b | `0x80` (ADC_EN, sürekli, 15-bit) (`:438`) |
| IBUS..TDIE ADC | 0x31-42 | R | 18B | poll ADC bloğu, 16b MSB-first (`poll:69`) |
| PART_INFO | 0x48 | R | 8b | IsPresent (PN mask 0x38, BQ25798=0x18) (`:81`, `bq25798.h:189-192`) ✅ |

> ✅ **ÇÖZÜLDÜ — Çelişki DEĞİL (2026-07-21):** İki değer **aynı register'ın iki farklı görünümü**, ikisi de
> doğru. **`0x18`** = maskeli PN alanı (kod bunu kullanır: `pn & 0x38 == 0x18` → IsPresent); **`0x19`** = tam
> POR baytı (RESERVED[7:6]=00 + PN[5:3]=011 + DEV_REV[2:0]=001 = `0b0001_1001` = 0x19, DEV_REV dahil). Kod
> yalnız PN alanını maskelediği için tam bayttaki DEV_REV=001 IsPresent kararını etkilemez. İkisi de
> **SLUSDV2C Rev C §7.5.1.57 REG48_Part_Information, s.128** ile birebir doğru. Tablo satırındaki `0x18`
> (maskeli) ve CLAUDE.md'deki `0x19` (tam bayt) **çelişmez.**

### 3b. Üst-kart TELEMETRİ bloğu (PUSH, 0x00..0x5F = 96 bayt; `I2CPROT_TLM_SIZE=0x60`)

Üst kart açısından tümü **R** (PowerBoard yazar, üst kart okur). "16K" sütunu: alan 16K'da anlamlı doluyor mu.

| Offset | İsim | Tip | Açıklama | 16K |
|---|---|---|---|---|
| 0x00 | SYS_STATE | u8 | faz[2:0] | FAULT(b6) | ANY(b7) | ✓ |
| 0x01 | SYS_FAULT | u8 | BQ FAULT0 aynası | ✓ |
| 0x02 | SYS_FLAGS | u8 | canlı alarm bitleri | ✓ |
| 0x03 | CHG_STAT_RAW | u8 | faz << 5 (REG1C konumunda) | ✓ |
| 0x04 | SOH_X10 | u16 | SoH x10 (1000=%100) | ✓¹ |
| 0x06 | EFC | u16 | eşdeğer tam çevrim | **✗ (32K)** |
| 0x08 | EQUIV_HOURS | u32 | 25°C-eşdeğer yaşlanma saati | **✗ (32K)** |
| 0x0C | GROSS_MAH | u32 | brut throughput mAh | **✗ (32K)** |
| 0x10 | VBAT_MV | u16 | BQ VBAT mV | ✓ |
| 0x12 | VPV_MV | u16 | STM PA4 mV | ✓ |
| 0x14 | VDC_MV | u16 | STM PA5 mV | ✓ |
| 0x16 | REM_EFC | u16 | kalan eşdeğer çevrim | **✗ (32K)** |
| 0x18 | REM_YEARS_X10 | u16 | kalan takvim yılı x10 | **✗ (32K)** |
| 0x1A | SOC_X10 | **i16 ⚠️ İŞARETLİ** (Rev0.9/G-1) — `-1000..+1000` = `-%100,0..+%100,0`. **NEGATİF OLAĞANDIR**: kart mutlak SoC iddia etmez (2026-08-27 kararı), bildirilen değer = ofset + açılıştan beri coulomb. ⛔ `u16` okunursa `-%1` → **65526** görünür ve *">1000 → 1000"* kırpması **SAHTE %100** üretir; aynı sözleşme `0x63 RST_SOC_X10` için de geçerlidir | **16K: VBAT→OCV LUT / 32K: coulomb** | ✓ (farklı yöntem) |
| 0x1C | REC_FLAG | u8 | b0=ALARM b1=CHECKPOINT | ✓ |
| 0x1D | SEQ | u8 | artan örnek sayacı 0-255 | ✓ |
| 0x1E | PROT_VER | u8 | **32K Rev1.0: 0x09** (2026-08-30; Rev0.9=0x08 / Rev0.8=0x07 / Rev0.6=0x06 / Rev0.5=0x05 / Rev0.4=0x04 tarihsel; 16K FROZEN: 0x03) | ✓ |
| 0x1F | *(REZERVE — Rev0.4)* | u8 | 0x00 gönderilir; eski BLK_XSUM konumu → **0x5F'e taşındı** (son-bayt kuralı) | ✓ |
| 0x20 | ICHG_MA | u16 | uygulanan şarj akımı mA | ✓ |
| 0x22 | BOARD_TEMP | i8 | kart NTC °C (-128=hata) | ✓ |
| 0x23 | BATT_TS | u8 | JEITA 0-4 | ✓ |
| 0x24 | BATT_TEMP | i16 | aku NTC x10 °C (-9990=geçersiz) | ✓ |
| 0x26 | BQ_FAULT0 | u8 | REG20 ham | ✓ |
| 0x27 | BQ_FAULT1 | u8 | REG21 ham | ✓ |
| 0x28 | CHG_STAT | u8 | faz 0-7 | ✓ |
| 0x29 | ICO_STAT | u8 | 0-3 (REG1D[7:6]) | **✗ (32K)** |
| 0x2A | HEATER_STATE | u8 | 0=OFF 1=ON 2=FAULT_NTC (0xFF=kapalı) | **✗ (32K)** |
| 0x2B | BATT_STATE | u8 | 0=PRESENT 1=SUSPECT 2=ABSENT 3=PROBING (0xFF=kapalı). **2026-08-01: `battdet` (RIPPLE FAZ 3+4) sürüyor** — VAR→0, YOK→2, hüküm-yok→3; SUSPECT(1) üretilmez. **RAPOR-ONLY**, aksiyon tetiklemez | **✗ (32K)** |
| 0x2C | BATT_CAP_AH | u8 | aktif kapasite Ah echo | ✓ |
| 0x2D | BATT_CRATE | u8 | aktif C-rate % **echo (OKUMA yolu)** — üst kart mevcut crate'i buradan okur; YAZMA yolu STATBLK CFG_CRATE 0x77 (Rev0.5) | **✗ (32K)** |
| 0x2E | ICHG_TARGET | u16 | hesaplanan ICHG hedefi mA | **✗ (32K)** |
| 0x30 | ALARM_LIVE | u8 | anlık alarm bitleri | ✓ |
| 0x31 | ALARM_LATCH | u8 | yapışkan alarm bitleri | ✓ |
| 0x32 | CHG_PHASE | u8 | 0-7 faz | ✓ |
| 0x33 | DELTA_UWH | i32 | son 1s enerji delta µWh | **✗ (32K)** |
| 0x37 | TOTAL_MWH | i32 | koşan toplam mWh | **✗ (32K)** |
| 0x3B | TOTAL_MAH | i32 | koşan toplam mAh | **✗ (32K)** |
| 0x3F | BMS_PRESENT | u8 | aktif BMS bayrağı echo | **✗ (32K)** |
| 0x40 | BQ_REG1B | u8 | ham Charger Status 0 | ✓ |
| 0x41 | BQ_REG1D | u8 | ham Charger Status 2 | ✓ |
| 0x42 | BQ_REG1E | u8 | ham Charger Status 3 | ✓ |
| 0x43 | BQ_REG1F | u8 | ham Charger Status 4 (TS_STAT) | ✓ |
| 0x44 | PWR_IO | u8 | STM GPIO bitmask (bkz. §4) | ✓ |
| 0x45 | HIZ_TRIG | u8 | EN_HIZ bekçi tetik sayısı (doygun 255) | ✓ |
| 0x46 | MPPT_TRIG | u8 | MPPT bekçi tetik sayısı | ✓ |
| 0x47 | IINDPM_TRIG | u8 | IINDPM bekçi tetik sayısı | ✓ |
| **0x49** | **PWR_SRC** | u8 | **AKTİF GİRİŞ KAYNAĞI + Psys güven seviyesi (Rev0.6 YENİ).** `bit[3:0]=SRC` (0=NONE 1=**PV**(VAC2) 2=**DC**(VAC1) 3=AMB 4=UNK) · `bit[7:4]=Q` (0=EXACT 1=DERIVED 2=NO_ATTR 3=STALE). Üst kart aktif kaynağı **yorum yapmadan** buradan okur. `0xFF` = modül kapalı (n/a). Önceden bu bayt rezerve/`0x00` idi | **✗ (32K)** |
| **0x48** | **ACDRV_TRIG** | u8 | **DIS_ACDRV bekçi tetik sayısı (Rev0.6 YENİ; doygun 255).** `>0` ise sahada **IBUS_OCP** yaşanmış demektir: koruma `EN_HIZ` ile birlikte `DIS_ACDRV=1` yapar, temizlenmezse **her iki giriş yolu kapalı kalır** ve kart yalnız aküden beslenir. Önceden bu bayt rezerve/`0x00` idi | **✗ (32K)** |
| **0x4A** | **CHG_REAL** | u8 | ⭐ **TÜRETİLMİŞ "GERÇEK ŞARJ" DURUMU (Rev0.8 YENİ — PROT_VER 0x07).** `0`=HÜKÜMSÜZ (kanıt yetersiz) · `1`=ŞARJ YOK (çip öyle diyor) · `2`=GERÇEK ŞARJ (iddia **VE** akım) · `3`=**ÇELİŞKİ** (çip şarj diyor ama akım YOK). PowerBoard, çipin **faz iddiasını** (`0x28 CHG_STAT`) kendi **akım ölçümüyle** (`0x56 IBAT_MA`) karşılaştırır; ÇELİŞKİ hükmü **ısrar şartına** bağlıdır (~15 s) — anlık dalgalanma ve taper/terminasyon geçişleri çelişki sayılmaz. ⚠️ **Ham `0x28` DEĞİŞMEDİ ve otoriter kalır**; bu alan onu değiştirmez, **yanında** durur ki üst kart çelişkiyi görüp kararı kendisi versin. Önceden bu bayt rezerve/`0x00` idi | **✗ (32K)** |
| **0x4B** | **BQ_YAS_DS** | u8 | ⭐ **BQ VERİSİNİN YAŞI (Rev0.9 YENİ — PROT_VER 0x08, K17).** `0` = **TAZE** (BQ kaynaklı alanlara güvenilebilir) · `1..255` = **BAYAT/DOWN**, değer = son **başarılı** poll'dan geçen süre (**0,1 s** birimi, doygun 255 = ≥25,5 s). Tek bayt hem **DOWN**'ı hem **yaşı** taşır — ayrı bir bayrak tutulsaydı ikisi ayrışabilirdi. ⚠️ `!= 0` iken şu alanlar **karara esas alınmaz**: `0x03` · `0x10` · `0x23` · `0x26/0x27` · `0x28` · `0x29` · `0x40-0x43` · `0x4A` · `0x50`. Kaynak: `BQ25798_PollYasDs()`, eşik `BQ25798_POLL_STALE_MS = 750 ms`. Önceden bu bayt rezerve/`0x00` idi | **✗ (32K)** |
| **0x4D** | **PANIC_CAUSE** | u8 | ⭐ **2026-08-30 — **PROT_VER 0x09 (Rev1.0, 2026-08-30).**** `PWR_PANIC` (J21.38) LOW'un **nedeni** (G-8 kapanışı): bit0 VBAT < 11,0 V (yalnız akü modunda: STM VPV<2,5 V ∧ VDC<2,5 V; K19) · bit1 VSYS < 11,0 V (backstop) · bit2 akü SOĞUK (< −20 °C) · bit3 akü SICAK (> +60 °C). `0` = panik yok. Histerezis: bit, ilgili panik temizlenince düşer | **✗ (32K)** |
| **0x4E** | **PSYS_ST** | u8 | ⭐ **PROT_VER 0x09 (Rev1.0).** `0xA8 PSYS_MW` geçerliliği: `0` TOHUM (reset sonrası 690/1020 mW varsayım) · `1` ÖLÇÜM (akü modunda 64 örnek) · `2` BAYAT (en eski örnek > 30 dk) · `3` GEÇERSİZ (BQ bayat) · `0xFF` modül kapalı. `0` /`2`/`3` iken PSYS **karara esas alınmaz**; ayrıca `0x49 Q = NO_ATTR` | **✗ (32K)** |
| **0x4F** | **CAL_VER** | u8 | ⭐ **PROT_VER 0x09 (Rev1.0).** Karttaki `[SAHA_AYAR]` kalibrasyon seti: `1` = 2026-08-30 (STM VBAT/VPV/VDC kazanç+ofset; BQ IBUS/IBAT/VBUS). `0` = kalibresiz eski firmware. **AYIRT EDİCİ:** üst kart önce bu bayta bakar — `0` ise `0x4D/0x4E` anlamsızdır (eski firmware), `≠0` ise anlamlıdır | **✗ (32K)** |
| **0x4C** | **BQ_ERR_N** | u8 | **KÜMÜLATİF BAŞARISIZ POLL (Rev0.9 YENİ — PROT_VER 0x08, K17).** **Mod-256 sarar** (doygun DEĞİL; kod `g_bq_poll.err_tot & 0xFF`). ⚠️ **Yaştan AYRI olmasının sebebi:** yaş her başarılı poll'da sıfırlanır, bu yüzden sahada en olası arıza kipi olan **aralıklı NACK/timeout** ("4 başarısız + 1 başarılı" = %80 hata) yaşta **hiç görünmez**. Bu sayaç artıyorsa I²C hattı marjinaldir (nem, termal çevrim, zayıf pull-up). Önceden rezerve/`0x00` idi | **✗ (32K)** |
| 0x50 | BQ_VSYS_MV | u16 | mV | ✓ |
| 0x52 | BQ_VBUS_MV | u16 | mV | ✓ |
| 0x54 | STM_VBAT_MV | u16 | STM PA2 ADC VBAT mV | ✓ |
| 0x56 | IBAT_MA | i16 | şarj+/deşarj− mA | ✓ |
| 0x58 | IBUS_MA | u16 | mA | ✓ |
| 0x5A | BQ_VAC1_MV | u16 | DC girişi mV | ✓ |
| 0x5C | BQ_VAC2_MV | u16 | PV girişi mV | ✓ |
| 0x5E | BQ_TDIE_C | i8 | BQ çip °C | ✓ |
| 0x5F | **BLK_XSUM** (Rev0.4) | u8 | **bütünlük = SON bayt:** `XOR(0x00..0x5E)^0x5A` — 32K; 16K FROZEN'da XSUM 0x1F'te (eski kural) | **✗ (32K)** |

¹ 0x04 SOH_X10: 16K'da hesaplanmaz, yalnız restore'dan tutulur (default %100); 32K'da LifeEst hesaplar.
16K'da **✗ (32K)** işaretli offsetler 0 gelir (üst işlemci "0 = veri yok" işlemeli — savunmacı kodlama).
*(Haritalar: 16K `i2c_protocol.h:48-90`; 32K `i2c_protocol.h:51-112`.)*

### 3c. Üst-kart OKUMA blokları (PULL, master read)

| Blok | Offset | Uzunluk | İçerik | Kaynak |
|---|---|---|---|---|
| RESTORE | 0x60-0x71 | 18 B | 0x60=marker `0xA5`; **16K:** 0x61-62=RST_SOH_X10; **32K:** 0x63-64=SoC, 0x65-68=EQUIV_HOURS, 0x69-6C=GROSS_MAH, 0x6D-70=TOTAL_MWH; 0x71=XSUM (bkz. §2) | 16K `i2c_protocol.c:534-548`; 32K `:554-601` |
| **STATBLK** (Rev0.5) | **0x72-0x78** | **7 B** | **32K'da REC_ACK/CFG_GEN/CAP/TCAL/CRATE'in TEK okuma yolu — tekil register okuması KALKTI.** Daima 7 B tek işlemde okunur, son bayt bütünlük: `[0]=REC_ACK [1]=CFG_GEN [2]=CAP_AH [3-4]=TCAL_Q15(BE) [5]=CRATE [6]=XOR(0x72..0x77)^0x5A`. XSUM tutmazsa okuma yok sayılır (REC→retry, CFG→sonraki periyot). Üst kart bloğu **gölge-tamponla atomik** günceller (herhangi bir bayt değişince 0x78 de aynı anda güncel olmalı) | `i2c_protocol.h I2CPROT_REG_STATBLK` + `i2c_protocol.c Prot_ReadStatBlock` |
| — REC_ACK | 0x72 | (statblk[0]) | üst kartın echo ettiği son kaydedilen SEQ (ek koruma: SEQ karşılaştırması) | `i2c_protocol.h` |
| — CFG_GEN | 0x73 | (statblk[1]) | config generation nabzı (yavaş poll ~8 s) | `i2c_protocol.h` |
| — CFG_CAP_AH | 0x74 | (statblk[2]) | aku kapasitesi 7-50 Ah (aralık dışı → 12) | `i2c_protocol.h` |
| — CFG_TCAL | 0x75-0x76 | (statblk[3-4]) | fabrika zaman-kalibrasyonu `t_cal` Q15 BE (0x75=H; 32768=1.000; Commit 5/I-7). `0x0000`/`0xFFFF` = kalibre edilmemiş → nominal. PowerBoard clamp ±%3.5 (31621-33915; dışı reddedilir). Üretimde 1 sn kare-dalga ölçümünden yazılır | `i2c_protocol.h` + `I2cProt_ReadConfigFull` |
| — CFG_CRATE | 0x77 | (statblk[5]) | **Şarj C-rate % (Rev0.5 YENİ):** 5..20 (0.05-0.20C); `0`/`0xFF` = ayarlanmamış → firmware default %10 korunur. PowerBoard `BattCfg_Set` içinde 5-20'ye clamp'ler; ICHG = cap×crate×10 mA (5000 tavan). Uygulama SoH/SoC reset ETMEZ (crate ≠ yeni akü). **[DIŞ] Fatih: bu baytı yaz + XSUM'u 7B üzerinden hesapla** | `i2c_protocol.h I2CPROT_REG_CFG_CRATE` + `I2cProt_ReadConfigFull` |
| — CFG_XSUM | 0x78 | (statblk[6]) | STATBLK bütünlük = SON bayt `XOR(0x72..0x77)^0x5A` | `i2c_protocol.h` |
| LASTGASP | 0x80 | 27 B | **YALNIZ 32K** — üst karta son-nefes bloğu PUSH; bayt 26 = `XOR(0..25)^0x5A` (Rev0.4) | 32K `i2c_protocol.h:119-121`, `:491-493` |
| **GÜÇ BLOĞU** | **0xA0-0xB4** | **21 B** | **YALNIZ 32K — Rev0.6 YENİ (2026-07-23).** Anlık güç telemetrisi PUSH; telemetri push'unun **hemen ardından**, aynı kadansta (~1 s), **ayrı burst**. `[0xA0]=PPV_MW [0xA4]=PDC_MW [0xA8]=PSYS_MW [0xAC]=PBAT_MW [0xB0]=PIN_MW` (5 × **int32 MSB-first, mW**) + `[0xB4]=XOR(0xA0..0xB3)^0x5A`. **İşaret:** `PBAT` + şarj / − deşarj · `PSYS` **NEGATİF = tüketim**. ⭐ **K26 (2026-08-30) `PSYS` TANIMI DEĞİŞTİ (yerleşim aynı):** artık η-türetimi değil, **akü modunda ölçülen** sistem yükü (STM VPV<2,5 V ∧ VDC<2,5 V; son 64 örnek ort. |V_bat·I_bat|); reset sonrası **tohum 700 mW**; geçerlilik DIAG `psys_st` (0 tohum/1 ölçüm/2 bayat). ⚠️ 0x00-0x5F bloğu **bayt-bayt DEĞİŞMEDİ** — üst kart yalnız bu yeni bölgeyi ekler | `i2c_protocol.h I2CPROT_REG_PWRBLK` + `i2c_protocol.c I2cProt_SendPowerBlock` · `power_calc.c` |

> **Kaynak §3:** BQ register'ları `bq25798.h:70-101` + `bq25798.c` (yazım satırları) + `bq25798_poll.c:22-25,63-111`.
> Telemetri haritası 16K `i2c_protocol.h:48-96` / 32K `i2c_protocol.h:51-121`. Okuma blokları yukarıda.
> Auto-increment/multi-byte DS §7.3.14.7 ile çapraz-doğrulandı.

---

## 4. Register bit-map'leri + enum tanımları

**PWR_SRC (0x49, Rev0.6)** — `SRC = b[3:0]` : `0`=NONE (giriş yok/akmıyor) · `1`=**PV** (VAC2 yolu,
Q1/ACFET2 iletiyor) · `2`=**DC** (VAC1 yolu, Q3/ACFET1) · `3`=AMB (iki VAC birbirine çok yakın,
ayırt edilemedi) · `4`=UNK (hiçbir VAC, VBUS'a yakın değil).
`Q = b[7:4]` : `0`=EXACT (kaynak atfedildi, STM geçerli **ve PSYS ölçüm, n=64**) — **Y-2 (2026-08-30): PSYS tohum/bayat/geçersiz iken `Q=NO_ATTR(2)`**, üst kart PSYS'e dayanmaz · `1`=DERIVED (**K26'dan beri kullanılmaz**) ·
`2`=NO_ATTR (SRC belirsiz; **`PIN_MW` yine geçerli**) · `3`=STALE (BQ verisi bayat → **güç alanlarının
hiçbirini kullanma**). `0xFF` = modül kapalı.

> 🚨 **ÜST KART İÇİN KRİTİK UYARI:** `REG1B` (0x40) içindeki `AC1_PRESENT`(b1) /
> `AC2_PRESENT`(b2) bitleri **"VAR"** demektir, **"AKTİF" DEMEZ**. PV ve DC aynı anda takılıysa
> **ikisi de 1** okunur, ama donanım aynı anda **yalnız BİR** yolu iletir (ACFET-RBFET kuralı,
> datasheet §7.3.5.4 s.32). Aktif kaynağı belirlemek için **`0x49 PWR_SRC`** kullanılmalıdır.
> *(İkinci/bağımsız iz: `0x42`=REG1E ham → `b7`=ACRB2_STAT(PV) / `b6`=ACRB1_STAT(DC);
> Tablo 7-7'ye göre aktif yol=1, pasif yol=0 — tezgâh S1/B-19 ile teyit edilecek.)*

**SYS_STATE (0x00)** — b7=ANY(0x80), b6=FAULT(0x40, `g_bq_poll.fault0≠0`), b2:0=faz maskesi(0x07).
*(16K `i2c_protocol.c:23-25,297-300`; 32K `:29-31,307-310`.)* NOT: iç durum-makinesi enum'u (INIT/CHARGING…)
değil — düşük 3 bit **BQ şarj fazı**; durum makinesi dolaylı yansır.

**ALARM bitleri (SYS_FLAGS 0x02 / ALARM_LIVE 0x30 / ALARM_LATCH 0x31)** — *(16K `i2c_protocol.h:138-145`; 32K `:149-156`)*:

| Bit | İsim | Eşik | ANY'ye girer |
|---|---|---|---|
| 7 | ANY | özet | — |
| 6 | OVP_PV | OVP latch — **trip 27.0 V / recover 25.0 V** (PV\|DC) | ✓ |
| 5 | NTC_HOT | ≥70°C (kart NTC) | ✓ |
| 4 | NTC_COLD | <0°C (kart NTC) | ✓ |
| 3 | VBAT_LOW | <10000 mV | ✓ |
| 2 | VBAT_HIGH | >14600 mV | ✓ |
| 1 | VDC_LOW | DC-IN <10000 mV | **✗ (bilgi)** |
| 0 | VPV_LOW | PV <10000 mV | **✗ (bilgi)** |

REC tetikleyen "gerçek alarm" maskesi = OVP_PV|NTC_HOT|NTC_COLD|VBAT_LOW|VBAT_HIGH (bilgi bitleri hariç).

**REC_FLAG (0x1C):** b0=ALARM (yeni gerçek-alarm latch 0→1), b1=CHECKPOINT (reset sonrası).

**BATT_TS (0x23)** `BQ25798_TsStatus_t`: 0=NORMAL(4-45°C), 1=COLD(<0°C, şarj durur), 2=COOL(0-4°C, düşük
akım), 3=WARM(45-55°C, düşük gerilim), 4=HOT(>55°C, şarj durur). Öncelik HOT>COLD>WARM>COOL.
*(`bq25798_poll.h:25-32`, decode `bq25798_poll.c:117-138`.)*

**CHG faz (0x28/0x32; REG1C[7:5])** `BQ25798_ChargeState_t`: 0=NOT_CHARGING, 1=TRICKLE, 2=PRECHARGE,
3=FAST_CC, 4=TAPER_CV, 5=RESERVED, 6=TOPOFF, 7=DONE. *(`bq25798.h:41-51,186-187`.)*
> **32K davranış notu (2026-07-19, bayt düzeni DEĞİŞMEDİ):** 32K'da 0x00[2:0]/0x28/0x32
> **3sn debounce'lu** fazdır (MPPT VOC örneklemesinin 1sn'lik chg=0 blipleri filtrelenir —
> üst kart bunları "şarj durdu" sanmasın); **0x03 CHG_STAT_RAW ham/anlık kalır** (bliplerin
> görülebildiği tek alan). 16K (FROZEN) her alanda ham/anlık gönderir — davranışsal fark,
> sözleşme/bayt uyumluluğunu bozmaz. Ayrıca 32K'da DONE sonrası **terminasyon latch**:
> şarj CE ile durdurulur, faz 0'a döner ve VBAT<13.4V olana dek 7 tekrar görülmeyebilir.

**PWR_IO bitmask (0x44)** — b0=CE(PA8, aktif-LOW: 0=şarj), b1=OVP2HIZ(PA11: 1=Hi-Z normal),
**b2=PA12 ENERJİ-PANİK** (2026-07-22 yeniden atandı; aktif-LOW: **1=normal, 0=PANİK/enerji-yok**),
b3=VBAT_DIS(PA6), b4=HEAT/OVP2PDIS(PA7), b5=QON(PC15). 1=fiziksel HIGH.
⚠️ **PA12 artık OVP DEĞİL enerji-panik sinyalidir** (donanım hattı, üst kart EXTI ile GSM sustur + kendi **kalıcı belleğine** yaz;
detay Fatih dokümanı §6.11 + `power-budget-holdup.md §3b`). **OVP durumu yalnız ALARM byte b6'dan**
(mantıksal, `OVP_IsLatched`). *(`i2c_protocol.c` `Prot_ReadPwrIo` + `main.c System_EnergyPanicTask`.)*

**32K-özel enum'lar:** ICO_STAT(0x29) 0-3 · HEATER_STATE(0x2A) 0=OFF/1=ON/2=FAULT_NTC · BATT_STATE(0x2B)
0=PRESENT/1=SUSPECT/2=ABSENT/3=PROBING **— şu an hep 0xFF (n/a): batt_detect KALDIRILDI 2026-07-22;
enum, RIPPLE FAZ 4 kararı bağlanınca yeniden canlanır (T-11)** · BMS_PRESENT(0x3F) 0/1.
*(32K `i2c_protocol.h:76-78`, `:329-338`.)*

**Ham BQ status bitleri** (her iki dal, aynı çip): REG1B — IINDPM(b7) VINDPM(b6) WD(b5) PG(b3) AC2(b2)
AC1(b1) VBUS(b0); REG1C — CHG_STAT(b7:5) VBUS_STAT(b4:1) BC1.2(b0); REG1D — ICO(b7:6) TREG(b2) DPDM(b1)
VBAT_PRESENT(b0); REG1E — ACRB2(b7) ACRB1(b6) ADC_DONE(b5) VSYS(b4) CHG_TMR(b3) TRICHG_TMR(b2) PRECHG_TMR(b1);
REG1F — TS_HOT(b3) TS_WARM(b2) TS_COOL(b1) TS_COLD(b0); REG20/21 — bkz. `BQ25798_Register_Reference.md`
§FAULT (VBUS/VBAT/IBUS/IBAT/CONV/VAC OVP-OCP; VSYS_SHORT/OVP, OTG, TSHUT). *(`bq25798.h:144-192`, register-ref.)*

**PROT_VER değeri:** 32K = **0x09 (Rev1.0, 2026-08-30 — `0x4D PANIC_CAUSE` + `0x4E PSYS_ST` + `0x4F CAL_VER`, `0xA8 PSYS` anlamı, K19 eşiği)** · Rev0.9=0x08 (`0x4B`/`0x4C`) (`i2c_protocol.h`) · Rev0.8=0x07 (`0x4A CHG_REAL`) / Rev0.6=0x06 / Rev0.5=0x05 / Rev0.4=0x04 tarihsel · 16K FROZEN = 0x03 (tarihsel).

> **Kaynak §4:** bit/enum satırları yukarıda inline. BQ ham-status bit tanımları `bq25798.h:144-192` +
> `BQ25798_Register_Reference.md` (REG1B-21) ile çapraz-doğrulandı.

---

## 5. Komut seti + örnek sekanslar

**Üst-karttan RW komut register'ı YOK** — protokol **monitor-only**. Eski Rev0.1 komut bloğu (0x40-0x4F
START/STOP/CLEAR_LATCH/BQ_RESET) **KALDIRILDI**; 0x40-0x44 artık ham BQ status + PWR_IO'dur.
`I2CPROT_CMD_*` sabitleri header'da hâlâ tanımlı (`i2c_protocol.h:127-131`) **ama hiçbir dispatch/tüketici
yok** (her iki dalda; `i2c_protocol.c`'de komut-işleme fonksiyonu bulunmaz). *(16K/32K çapraz-doğrulandı.)*

Kontrol iki yoldan:
- **(a) Yerel BQ şarj kontrolü** — `StartCharge`: PA8=LOW (CE aktif-LOW) + REG0F EN_CHG=1; `StopCharge`:
  EN_CHG=0 + PA8=HIGH. *(`bq25798.c:285-318`.)*
- **(b) Üst-kart config PULL** — `CFG_CAP_AH (0x74)` değişince kapasite→ICHG uygulanır (7-50 Ah, C/10,
  `BQ25798_ICHG_MAX_MA=5000` saturasyon); farklı akü ise SoH/SoC reset + checkpoint iste. `CFG_GEN (0x73)`
  nabzı değişince tam config okunur. *(16K `Prot_ApplyCapacity:426-457`; 32K `Prot_ApplyCapAh:198-234`.)*

**Örnek sekanslar:**
- *Tipik BQ okuma (VBAT ADC, 16b):* `S,0xD6,0x3B,Sr,0xD7,VBAT_MSB,VBAT_LSB,NACK,P` → mV.
- *Tipik telemetri turu:* PowerBoard 96B'yi `S,0x90,0x00,D0..D95,P` ile yazar (≈1000ms periyot).
- *REC (kayıt-onay) akışı:* PUSH sonrası `REC_FLAG≠0` ise üst kart bloğu işleyip `REC_ACK(0x72)`'ye
  gönderilen SEQ'i yazar → PowerBoard `S,0x90,0x72,Sr,0x91,ACK,NACK,P` ile okur; `ACK==SEQ` ise onaylı.
- *Hata durumu:* herhangi HAL çağrısı NACK/timeout → `HAL_ERROR`; BQ poll `valid=0` + FAULT'a doğru;
  üst kart telemetrisi fire-and-forget (yeniden dövmez).

> **Kaynak §5:** `i2c_protocol.h:127-135` (kullanılmayan CMD sabitleri) · `bq25798.c:285-318` ·
> `i2c_protocol.c` (ApplyCapacity 16K `:426-457` / 32K `:198-234`; REC `:388-418` / `:424-452`).

---

## 6. Hata / zamanlama / ISR

**I2C ISR YOK.** `stm32c0xx_it.c`'de yalnız NMI/HardFault/SVC/PendSV/SysTick var — **`I2C1_IRQHandler`
tanımlı değil** (her iki dal). Tüm I2C **blocking master (polling)**; interrupt/DMA durum makinesi yok.
*(`stm32c0xx_it.c`.)*

**NACK/timeout/busy:** her HAL çağrısı `!=HAL_OK` kontrollü → ikili olay (`0x34 EV_TLM_FAIL`, `0x33 EV_REC_FAIL`, `0x35 EV_RESTORE`, `0x72 EV_CFG_FAIL`; K22 2026-08-30 — UART'ta metin log yok) + durum kodu döner. BQ poll
herhangi adım hatasında `g_bq_poll.valid=0` + `HAL_ERROR` (`bq25798_poll.c:61,67,73,80`). I2C 2.8.x
spurious-BERR ele alınmaz (weak default handler yok bile) — etkisiz.

**BQ_MISS → bus-recovery → FAULT:** 200ms poll; `BQ25798_Poll()` hata dönerse `s_bq_miss_cnt++`;
`BQ_MISS_LIMIT=5` (≈1sn) → `I2C_BusRecover()` + `sys_state=FAULT`. Başarılı poll sayacı sıfırlar.
*(`main.c:63,240-281`.)*

**Bus-recovery** (`I2C_BusRecover`, register-seviyesi 9×SCL): PE=0 (durum-makinesi reset, TIMINGR korunur)
→ PB6/PB7 AF→GP-OD → ikisi HIGH → SDA serbest olana dek ≤9× SCL darbesi → STOP → AF6'ya dön → PE=1 +
`hi2c1.State=READY`. Yarı-periyot ~15µs busy-wait (`I2C_RECOVER_HALF_DELAY=200`). **Tetik:** (1) CHARGING
5-miss, (2) INIT IsPresent-fail (boot köprüsü). *(16K `i2c.c:144,163-207`; 32K `i2c.c:142-149,160-204`; tetik `main.c`.)*

**REC retry:** `REC_MAX_RETRY=10`, arası `REC_RETRY_MS=5`; her denemede 5ms bekle → REC_ACK oku →
`ACK==SEQ` başarı; değilse aynı blok yeniden yaz. 10 hata → sticky `s_rec_error=1` (LED). *(`i2c_protocol.h:101-102`.)*

**BQ watchdog:** POR=40s (REG10 `WATCHDOG[2:0]=101`); her 200ms poll `WD_RST` yazar (200× marj). WD
dolduysa (REG1B[5]) re-init. FAULT'ta bile WD_RST yazılır (VREG 16.8V'a dönmesin).
*(`bq25798.c:451`, `main.c:243-266`; 40s kaynağı: `BQ25798_Register_Reference.md` REG10 WATCHDOG →
DS §7.5.1.13 Tablo 7-26, s.72.)*

**Bekçiler (poll'da periyodik doğrula+düzelt):** EN_HIZ auto-set→temizle+`hiz_trig++`(0x45); EN_MPPT
düştü→REG15 geri-yaz+`mppt_trig++`(0x46); IINDPM saptı→geri-yaz+`iindpm_trig++`(0x47); **`DIS_ACDRV` set→temizle+`acdrv_trig++`(0x48, Rev0.6)**. *(`bq25798_poll.c`.)*

**Mod makinesi (UPLINK/BENCH):** Boot `ModeInit(upper_acked)` — restore(0x60)/config(0x73) ACK'ladıysa
UPLINK, aksi BENCH. BENCH'te sessiz re-probe `IsDeviceReady(0x48)` boot+2s sonra her 10s → ACK'ta UPLINK'e
geç. **UPLINK→BENCH otomatik düşüş YOK** (thrash önlemi). BENCH'te 0x48 tamamen susturulur.
*(16K `i2c_protocol.c:470-516`; 32K `:501-549`; `main.c`.)*

**Config poll:** `CFG_POLL_DIV=8` → her ~8s CFG_GEN 1 bayt; değişince tam config oku.

> **Kaynak §6:** `stm32c0xx_it.c` (ISR yokluğu) · `bq25798_poll.c` · `main.c` ·
> `i2c.c` (BusRecover) · `i2c_protocol.c` (REC/mode). Timeout değerleri §1.

> ### ⚠️ Rev0.7 DÜZELTMELERİ — §6'daki bayat beyanlar (koda karşı doğrulandı)
> | Eski beyan | KOD gerçeği | Kaynak |
> |---|---|---|
> | "**200 ms** poll" (3 yerde) | **250 ms** (`POLL_PERIOD_MS=250`; I-7'de DMA-senkron `ADC_DMA_POLL_N=311` → **249.629 ms**) | `main.c:64`, `Inc/adc_dma.h:58` |
> | "`BQ_MISS_LIMIT=5` (**≈1 sn**)" | 5 × 250 ms = **1.25 s** | `main.c:66` |
> | "WD_RST her 200 ms (**200× marj**)" | 40 s / 250 ms = **160× marj** | `main.c:64` + REG10 |
> | "REC retry: arası **`REC_RETRY_MS=5`**; her denemede 5 ms bekle" | **`REC_RETRY_MS` KODDA YOK.** T-15 ile retry **bloklamayan** hâle getirildi: `I2cProt_ServiceRec()` **poll kadansında** (~250 ms) çağrılır; 10 deneme → toplam pencere **~2.5 s** | `i2c_protocol.c:561-600` |
> | §1 "BQ timeout **100 ms**" | **20 ms** | `Inc/bq25798.h:70` |
> | §7 F6 "PROT_VER **0x04**" | **0x07** (Rev0.7'den beri; 0x06 Rev0.6) | `Inc/i2c_protocol.h:149` ⚠️ *(satır atfı Rev0.8'de düzeltildi — `:136` artık PROT_VER değil)* |
>
> Bu satırlar **sessizce düzeltilmedi** — eski hâlleriyle birlikte raporlanıyor (proje kuralı).

---

## 6b. ⚡ ELEKTRİKSEL + ZAMANSAL SINIRLAR `[Rev0.7 YENİ]`

### 6b.1 Hat parametreleri

| Parametre | Değer | Kaynak / durum |
|---|---|---|
| Rol | PowerBoard = **MASTER** (tek master) | `i2c.c` (STM32 hiç slave yapılandırılmaz) |
| Çevre birimi | `I2C1`, **PB6=SCL / PB7=SDA**, AF6, open-drain, dahili pull **yok** | `i2c.c:96-106` |
| Saat kaynağı | PCLK1 = **48 MHz** | `i2c.c:89-90` |
| **Bus hızı** | **400 kHz — Fast Mode** (`TIMINGR = 0xB0110305`: PRESC=0xB → t=250 ns · SCLL=5 → 1.5 µs · SCLH=3 → 1.0 µs · SDADEL=1 → 250 ns · SCLDEL=1 → 500 ns) | `i2c.c:41` **KOD OTORİTER** |
| SCL periyodu | **2.5 µs** (1.5 + 1.0, yükselme süresi hariç) | [HESAP] |
| Analog filtre | **AÇIK** | `i2c.c:59` |
| Dijital filtre | **0** (kapalı) | `i2c.c:66` |
| Slave adres | üst kart **0x48** (7-bit) → HAL 8-bit `0x90`/`0x91` | `i2c_protocol.h:44-45` · **[ONAY BEKLİYOR]** §8 |

> 🔴 **400 kHz — 100 kHz DEĞİL.** Bu, üst kart tasarımını doğrudan bağlar: slave ISR'ı
> 2.5 µs'lik SCL periyoduna yetişmelidir. `BQ25798_Register_Reference.md`'deki "100 kHz
> Standard Mode" ifadesi **bayattır** (bkz. §Çelişkiler C1) ve bu dokümanı bağlamaz.
> Kod yorumunda tezgâh notu var: *"skopla SCL~400 kHz + NACK yok doğrula"* → **[TEZGAH_DOGRULANACAK]**.

### 6b.2 Fiziksel yol ve pull-up'lar (EDIF)

```
 [STM32 PB6/PB7] ──┬── R8 / R10 = 2.2 kΩ → 3V3OVP        (A tarafı)
                   ├── BQ25798 U3.14/U3.15
                   └── TCA9416 U1.1/U1.4  (VCCA = 3V3OVP)
                            │
                       U1.8 / U1.5        (VCCB = 3V3  ← ÜST KARTTAN, J21.12)
                            │
                   L23 / L22 = 600R ferrit
                            │
                   R107 / R108 = 2.2 kΩ → 3V3      (B tarafı pull-up)
                            │
                   R110 / R109 = 47 Ω seri
                            │
                   J21.10 (SCL) / J21.9 (SDA) ──► üst kart
```

| Öğe | Değer | Ray | Kaynak |
|---|---|---|---|
| A-tarafı pull-up | R8 (SCL), R10 (SDA) = **2.2 kΩ** | **3V3OVP** (U18 AP7375 LDO) | EDIF; `hardware-netlist-reference.md` sat.30/49 |
| B-tarafı pull-up | R107 (SCL), R108 (SDA) = **2.2 kΩ** | **3V3** (⚠️ üst karttan, J21.12) | EDIF; `hardware-netlist-reference.md` sat.331 |
| B-tarafı seri | R109 (SDA), R110 (SCL) = **47 Ω** | — | EDIF sat.985/987 |
| B-tarafı ferrit | L22 (SDA), L23 (SCL) = **600R** | — | EDIF sat.985/987 |
| Köprü | **TCA9416DDFR** (SOT-23-8) VCCA=3V3OVP ↔ VCCB=3V3 | — | EDIF U1 |

- **Kablo/konnektör kapasitansı ölçülmedi** → gerçek yükselme süresi ve 400 kHz'de
  kurulum marjı **[TEZGAH_DOGRULANACAK]** (skop: SCL/SDA yükselme kenarı, `t_r`).
- **TCA9416 pass-gate mi repeater mi** lokal kaynaktan doğrulanamadı → §8 `[AÇIK]`.
  Firmware **pass-gate varsayar** (B-taraf SDA-LOW tüm bus'ı kilitler; 9×SCL kurtarır).

### 6b.3 Kadans — hangi turda ne gider (KOD)

| Katman | Periyot | İçerik | Kaynak |
|---|---|---|---|
| **Poll tik** | **250 ms** (I-7: DMA-senkron 249.629 ms, jitter ~0) | BQ status+ADC+WD_RST, bekçiler, `ServiceRec`, `PrintCompact`, `System_EnergyPanicTask` | `main.c:64` · `adc_dma.h:58` |
| **PUSH (üst kart)** | **her 4. poll ≈ 1000 ms** | ① Telemetri 96 B (`0x00`) → hemen ardından ② Güç bloğu 21 B (`0xA0`) — **iki bağımsız burst** | `main.c:69,607-618` |
| **Config PULL** | **her 8. telemetri periyodu ≈ 8 s** | STATBLK 7 B (`0x72`) | `I2CPROT_CFG_POLL_DIV=8` |
| **REC retry** | poll kadansı, ≤10 deneme → **~2.5 s** pencere | STATBLK oku + 96 B yeniden yaz | `i2c_protocol.c:561-600` |
| **BENCH re-probe** | boot+2 s, sonra **10 s** | `IsDeviceReady(0x48)` (yalnız adres+ACK) | `i2c_protocol.c:684-705` |
| **Boot (bir kez)** | — | RESTORE 18 B oku → STATBLK 7 B oku → mod kararı | `main.c:254-262` |
| **LASTGASP** | olay-tetikli | 27 B (`0x80`) tek burst | `lastgasp.c` |

### 6b.4 İşlem süreleri ve **clock-stretching bütçesi** ⭐

400 kHz'de 1 bayt = 9 SCL periyodu (8 veri + ACK) = **22.5 µs**. Aşağıdakiler **[HESAP]**
— yükselme süresi, stretching ve bus arbitrasyonu **hariç** nominal değerlerdir.

| Blok | Bayt (adres+reg+veri) | Nominal süre | Timeout | **Toplam stretch bütçesi** | Bayt başına ortalama |
|---|---|---|---|---|---|
| **Telemetri PUSH** | 1+1+96 = 98 | **2.21 ms** | 10 ms | **7.79 ms** | **≈ 81 µs** ⭐ |
| Güç bloğu PUSH | 1+1+21 = 23 | 0.52 ms | 10 ms | 9.48 ms | ≈ 451 µs |
| LASTGASP PUSH | 1+1+27 = 29 | 0.65 ms | 10 ms | 9.35 ms | ≈ 346 µs |
| RESTORE PULL | 1+1+1+18 = 21 | 0.47 ms | 10 ms | 9.53 ms | ≈ 529 µs |
| STATBLK PULL | 1+1+1+7 = 10 | 0.23 ms | 10 ms | 9.77 ms | ≈ 1396 µs |
| *(BQ status 7 B)* | 10 | 0.23 ms | 20 ms | — | — |
| *(BQ ADC 18 B)* | 21 | 0.47 ms | 20 ms | — | — |

> ### 🔴 BAĞLAYICI SINIR — 96 baytlık telemetri PUSH'u
> Slave (üst kart), **tüm 98 baytlık işlem boyunca toplam ≤ 7.79 ms** clock-stretch
> yapabilir; bu, **ortalama ≈ 81 µs/bayt** demektir. Bu bütçe aşılırsa master
> `HAL_TIMEOUT` döner ve **o turun telemetrisi kaybolur** (§6c).
>
> Bütçe **toplamdır**: tek bir bayt için 5 ms stretch etmek de geçerlidir, yeter ki
> işlemin tamamı 10 ms'i aşmasın (§1 HAL timeout semantiği).
>
> **Mühendislik önerisi (spec değil):** tek stretch < 1 ms ve toplam < 5 ms hedeflenirse
> %50 marj kalır — yükselme süresi ve ölçülmemiş kablo kapasitansı bu marjı yiyecektir.

---

## 6c. 🚧 BLOKLANMA KISITI VE KURALLARI `[Rev0.7 YENİ]`

### 6c.1 Master mimarisi — neden bloklar

**I2C ISR YOKTUR** (`stm32c0xx_it.c`'de `I2C1_IRQHandler` tanımlı değil). Tüm erişim
**blocking/polling** HAL çağrısıdır ve **ana döngüde (thread bağlamında)** koşar.
Dolayısıyla bir I2C işlemi süresince ana döngü **durur**.

### 6c.2 Bir poll tikindeki en-kötü bloklanma [HESAP]

**BQ yolu (0x6B, timeout 20 ms/işlem):**

| Adım | İşlem sayısı | Not |
|---|---|---|
| Status bloğu oku | 1 | başarısızsa **erken dönüş** (`valid=0`) |
| ADC bloğu oku | 1 | başarısızsa erken dönüş |
| WD_RST (`UpdateReg` = oku+yaz) | 2 | başarısızsa erken dönüş |
| Bekçiler: REG0F · REG12 · REG15 · IINDPM · REG14 · REG2E | ≤6 oku + ≤6 yaz | **erken dönüş YOK** — her biri ayrı ayrı timeout'a düşebilir |
| **Toplam en-kötü** | **16 × 20 ms = 320 ms** | |

**Üst kart yolu (0x48, timeout 10 ms/işlem):**

| Adım | İşlem | Süre |
|---|---|---|
| `ServiceRec` aktifse | STATBLK oku + 96 B yaz | 20 ms |
| Her 4. poll | Telemetri + Güç bloğu | 20 ms |
| Her 8. telemetri periyodu | STATBLK (+ tam config) | 20 ms |
| **Toplam en-kötü** | | **50 ms** |

> **BİR POLL TİKİNDE EN-KÖTÜ TOPLAM ≈ 370 ms > 250 ms poll periyodu.**
>
> Bu **gerçekleşirse** sonuç: poll tiki kaçırılır → `AdcDma_PollLostCount()` artar
> (kayıp-poll monitörü, `adc_dma.h:89`). Veri kaybı olur, **koruma kaybı olmaz** (§6c.3).
>
> ⚠️ 320 ms'lik BQ kolu **NACK'te oluşmaz** — NACK anında hata döndürür. Bu senaryo
> yalnızca **takılı SCL/SDA** (stretch/stuck) durumunda, üstelik zorunlu 4 işlem geçip
> bekçilerin takılmasıyla mümkündür. Yaygın tam-stall hâlinde ilk okuma 20 ms'de düşer
> ve poll erken döner. **[TEZGAH_DOGRULANACAK]** — ölçülmedi.

### 6c.3 ✅ GÜVENLİK BEYANI — I2C bloklanmasından ETKİLENMEYENLER

| Fonksiyon | Bağlam | Neden etkilenmez |
|---|---|---|
| **AWD1 donanım OVP (27.0 V)** | **DONANIM** analog watchdog | ADC çevre biriminde; CPU'dan bağımsız, tepki ~56 µs. `HT1=761` |
| **OVP değerlendirme / kaynak izleme / ısıtıcı-VPV kapısı** | **DMA yarı-transfer ISR** (~1.25 kHz, 803 µs) | `OVP_I7Process()` `adc_dma.c:284`'te **ISR bağlamında** koşar → main-loop bloklanması **önceliklendirilir** |
| ADC + DMA veri toplama | donanım + ISR | circular DMA durmaz |
| UART log akışı | TX-DMA | `uart_debug.c` bloklamaz |

> 🔒 **Sonuç: aşırı-gerilim koruması ve giriş izleme I2C'ye BAĞLI DEĞİLDİR.** I2C bus'ı
> tamamen kilitlense bile OVP donanım+ISR yolundan çalışmaya devam eder. Bu, üst kart
> tasarımı için bir **güvence**: slave tarafındaki bir hata koruma fonksiyonunu düşürmez.

**Etkilenen (gecikir):** ısıtıcı PWM görevi (20 ms) · LED durumu · ripple/battdet
güncellemesi · **bir sonraki BQ `WD_RST`** (BQ WD = 40 s → 370 ms gecikme zararsız, 100× marj) ·
ve aşağıdaki madde:

> ### ⚠️ `PWR_PANIC` (PA12) gecikmesi — Fatih'in bilmesi gereken
> `System_EnergyPanicTask()` **poll zincirinin İÇİNDE ve I2C push'larından SONRA** çağrılır
> (`main.c:621`). Dolayısıyla panik iddiasının gecikmesi:
> **(bir sonraki poll tikine kadar ≤250 ms) + (o tikteki I2C bloklanması)**.
> Nominal ≈ 250 ms, en-kötü ≈ 620 ms [HESAP].
> Kod yorumunun beyan ettiği tasarım niyeti: *"Üst kart bunu I2C'yi beklemeden **EXTI** ile
> görüp GSM sustur + Flash yazar"* → **üst kartta J21.38 kenar-kesmesi (EXTI) kurulmalıdır**,
> polling ile değil.

### 6c.4 Slave (üst kart) tarafına KURALLAR

| # | Kural | Gerekçe / kaynak |
|---|---|---|
| **S-1** | **Yalnız 0x48'e yanıt ver.** Başka adrese ACK verme; general-call **kapalıdır** (`GeneralCallMode=DISABLE`, `i2c.c:50`) | Bus'ta BQ 0x6B de var; yanlış ACK BQ trafiğini bozar |
| **S-2** | **Clock-stretch toplamı işlem başına ≤ §6b.4 bütçesi** (96 B push için **7.79 ms**, ort. **81 µs/bayt**) | Aşılırsa `HAL_TIMEOUT` → o turun verisi kaybolur |
| **S-3** | **SCL'yi tek seferde uzun süre LOW tutma.** Üst sınır işlem-toplamı bütçesidir (S-2); tek stretch < 1 ms önerilir | Master'da stretch için ayrı bir donanım üst sınırı **yoktur** — tek sınır HAL timeout'udur |
| **S-4** | **Repeated-START bekle.** Tüm okumalar `HAL_I2C_Mem_Read` ile yapılır: `[S][0x90][reg][Sr][0x91][veri…][NACK][P]`. Yazmalar tek START'lıdır | `i2c_protocol.c:198,715` |
| **S-5** | **Register adresi daima 8-bit**, auto-increment beklenir (96 B tek işlemde `0x00`'dan itibaren) | `I2C_MEMADD_SIZE_8BIT` |
| **S-6** | **Tüm çok-baytlı alanlar MSB-first.** Packed-struct'ı buffer'a cast/memcpy **ETME** | §2 byte-order sözleşmesi |
| **S-7** | **PULL bloklarını gölge-tamponla atomik güncelle.** Master tek işlemde okur; yırtık okuma XSUM'a takılır ve o tur **kaybedilir** | §2 madde 3/4 |
| **S-8** | **SDA'yı serbest bırakabilir ol.** Master, bus kilidinde 9×SCL darbesi + STOP üretir (§6c.5); slave bu darbelerle transaction sınırına dönmelidir | `i2c.c:160-203` |
| **S-9** | `PWR_PANIC` (J21.38) için **EXTI kenar-kesmesi** kur — I2C'yi bekleme | §6c.3 kutusu |

### 6c.5 Bus kilitlenme kurtarma — **KODDA VARDIR**

`I2C_BusRecover()` (`i2c.c:160-203`, register seviyesi, 16K ile birebir):

1. `I2C1->CR1 &= ~PE` — durum makinesi reset (**TIMINGR korunur**)
2. PB6/PB7 → AF'den genel-çıkış-OD'ye; ikisi de HIGH
3. **SDA serbest kalana dek ≤ 9× SCL darbesi** (~15 µs yarı-periyot ≈ **33 kHz**)
4. **STOP** üret (SCL HIGH iken SDA LOW→HIGH)
5. Pinleri AF6'ya döndür
6. `PE=1` + `hi2c1.State = READY`, `ErrorCode = NONE`

**Tetikleyiciler (KOD):**

| # | Koşul | Yer |
|---|---|---|
| 1 | CHARGING'de **5 ardışık BQ poll hatası** (5 × 250 ms = **1.25 s**) → recovery + `StopCharge()` + `FAULT` | `main.c:466-476` |
| 2 | Boot'ta `BQ25798_IsPresent()` başarısız (INIT↔FAULT yarışı köprüsü) | `main.c:944-951` |

> ### ⚠️ ÖNEMLİ SINIR — kurtarma **BQ hatasına** bağlıdır, üst kart hatasına DEĞİL
> `0x48` işlemlerinin başarısızlığı **kendi başına** bus-recovery tetiklemez; üst kart
> hataları yalnız telemetri kaybı / REC-retry / mod makinesi yollarına gider.
>
> **Pratikte kısmi koruma vardır:** iki slave **aynı fiziksel bus'tadır**; üst kart SDA'yı
> kilitlerse BQ poll'ları da başarısız olur ve **1.25 s içinde** recovery tetiklenir — yani
> BQ poll'u bir **bus sağlık kanaryası** işlevi görür. Ancak bu, kurtarmanın 9×SCL
> darbesinin **TCA9416 üzerinden B-tarafına geçtiği** varsayımına dayanır ve bu
> **lokal kaynaktan doğrulanmamıştır** → §8 `[AÇIK]` + **[TEZGAH_DOGRULANACAK]**.

---

## 6d. 🥾 BOOT SIRASI ve ÜST KART YOKSA ZARİF DÜŞÜŞ `[Rev0.7 YENİ]`

> **Fatih boot sırasını buna göre tasarlamalıdır.** PowerBoard **üst kartı beklemez** —
> hazır değilse default'larla çalışmaya devam eder ve sonradan yakalar.

### 6d.1 Boot dizisi (KOD, `main.c:252-262`)

```
1. I2cProt_Init()               → s_tlm[96] = 0, SoC=%100, cap=12Ah, BMS=var (default)
2. I2cProt_RestoreFromUpper()   → 0x60'tan 18 B PULL   (timeout 10 ms)
3. I2cProt_ReadConfigFull()     → 0x72'den 7 B PULL    (timeout 10 ms)
4. I2cProt_ModeInit(upper_acked)→ 2 VEYA 3 ACK'ladıysa UPLINK, aksi BENCH
```

`upper_acked`, adım 2 **veya** 3'ten **herhangi biri** `I2CPROT_OK` dönerse 1 olur.

### 6d.2 Zarif düşüş matrisi — üst kart hazır değilse ne olur

| Durum | Log | Sonuç |
|---|---|---|
| RESTORE **I2C hatası** (NACK/timeout) | `W/PROT: Restore okunamadi -> default` | SoC **%100**, enerji/ömür sıfır tabanla başlar |
| RESTORE **marker≠0xA5 veya XSUM tutmaz** | `W/PROT: Restore gecersiz (marker/xsum) -> default` | aynı — **veri kabul edilmez** |
| RESTORE OK | `W/PROT: Restore OK SoC=<n>` | SoC/enerji/ömür tohumlanır |
| CONFIG **I2C hatası** | `W/PROT: Config okunamadi (I2C)` | kapasite **12 Ah**, C-rate **%10** → ICHG **1.2 A** (firmware default) |
| CONFIG **XSUM hatası** | `W/PROT: Config okunamadi (XSUM)` | aynı; `s_cfg_valid=0` → **~8 s'de bir yeniden denenir** |
| İkisi de başarısız | + `W/PROT: ust kart yok (0x48 NACK) -> mod: BENCH` | **BENCH modu**: 0x48'e **hiç** yazılmaz/okunmaz (NACK spam yok) |
| Sonradan üst kart uyanırsa | `W/PROT: mod: BENCH -> UPLINK (0x48 uyandi)` | boot+2 s, sonra **10 s**'de bir sessiz `IsDeviceReady` probe'u |

**Default'lar (üst kart olmadan çalışan değerler):**

| Alan | Default | Kaynak |
|---|---|---|
| SoC | **%100** (`s_soc_x10 = 1000`) | `i2c_protocol.c` Init |
| BMS var mı | **1 (var)** | Init — Haicen paketinde RS485 BMS varsayımı |
| Kapasite | **12 Ah** (`I2CPROT_CAP_AH_DEFAULT`) | `i2c_protocol.h:223` |
| C-rate | **%10** → ICHG 1.2 A | `battery_cfg` |
| t_cal (Q15) | nominal (`0x0000`/`0xFFFF` = kalibre edilmemiş) | `i2c_protocol.c:776-781` |

> ⚠️ **UPLINK → BENCH otomatik düşüş YOKTUR** (thrash önlemi). Bir kez UPLINK'e geçildiyse,
> üst kart sonradan sussa bile mod UPLINK kalır; hatalar normal hata yollarından (NACK →
> telemetri kaybı, REC retry) görünür. `i2c_protocol.c:682-684`.

### 6d.3 CFG yazma yolu — `0x77 CFG_CRATE` ve kapasite

Üst kart **config'i PUSH etmez**; PowerBoard **PULL eder**. Akış:

1. Üst kart STATBLK'ı (`0x72..0x78`) kendi tarafında günceller — **gölge tamponla atomik**,
   son bayt `0x78 = XOR(0x72..0x77) ^ 0x5A`.
2. Değişikliği duyurmak için **`0x73 CFG_GEN` nabzını değiştirir** (herhangi bir farklı değer).
3. PowerBoard ~8 s'de bir STATBLK okur; `CFG_GEN` değiştiyse **tam config uygular**.

| Alan | Off | Geçerli aralık | Aralık dışı davranış |
|---|---|---|---|
| `CFG_CAP_AH` | 0x74 | **7..50 Ah** | modül en yakın tablo index'ine eşler; kapasite index'i **değişirse SoH/SoC/enerji RESET** |
| `CFG_TCAL` | 0x75-76 | Q15, `0x0000`/`0xFFFF` = kalibre edilmemiş | nominal korunur; modül **±%3.5** clamp'ler |
| `CFG_CRATE` | 0x77 | **5..20** (%0.05-0.20C) | `0x00`/`0xFF` = ayarlanmamış → firmware default **%10** korunur; modül 5..20'ye clamp'ler. **Yalnız ICHG yeniden hesaplanır — SoH/SoC/enerji RESET OLMAZ** |

*(Kaynak: `i2c_protocol.c:759-795`, `Prot_ApplyCapAh`.)*

### 6d.4 Rezerve / yazılmayan alanların davranışı

| Bölge | Durum | PowerBoard ne gönderir | Üst kart ne yapmalı |
|---|---|---|---|
| `0x1F` | **REZERVE** (eski BLK_XSUM konumu, Rev0.4'te 0x5F'e taşındı) | **0x00** | yok say |
| ~~`0x4A`~~ | ⚠️ **Rev0.7'de TAHSİS EDİLDİ → `CHG_REAL`** | artık **yazılıyor** (bkz. §3b, 0x4A satırı) | **anlam atfet** — rezerve DEĞİL |
| `0x4D..0x4F` | **2026-08-30'dan itibaren TAHSİSLİ** (PANIC_CAUSE / PSYS_ST / CAL_VER — §3b, PROT_VER 0x09 / Rev1.0) | önceki firmware'lerde **0x00** | eski üst kart yok sayar; yeni üst kart §3b'ye göre okur |
| `0x4B` / `0x4C` | **K17 (Rev0.9 / PROT_VER 0x08): TAHSİS EDİLDİ** — `BQ_YAS_DS` / `BQ_ERR_N` | `i2c_protocol.c` her turda yazar | **oku** — `0x4B != 0` ⇒ BQ alanları bayat |
| Modülü kapalı alanlar (ör. `0x2A`, `0x2B`, `0x49`) | modül `ENABLE_*=0` ise | **`0xFF` = n/a** | `0xFF`'i "**veri yok**" olarak işle, geçerli bir enum değeri sanma |
| 16K'da olmayan alanlar | — | **0x00** | "veri yok" (§7 F8/F9) |

- **Tüm 0x00..0x5E aralığı XSUM'a dâhildir** (`Prot_Xsum(s_tlm, 0x5F)`) — rezerve baytlar da
  bütünlüğe girer, yani sabit 0x00 olarak doğrulanır.
- **Rezerve alana yazma:** PowerBoard bu alanları **okumaz**; üst kart oraya bir şey yazsa
  bile PUSH bir sonraki turda **0x00 ile üzerine yazar**. Kalıcı değildir.
- **Tanımsız offset'ten okuma (>0x5F, PULL dışı):** PowerBoard hiç denemez; slave'in ne
  döndüreceği sözleşmede **tanımsızdır** → **[AÇIK]**, §8.

---

## 7. 🔴 16K ↔ 32K FARK TABLOSU

| # | Konu | 16K (F4P6) | 32K (F6P6) | Fark tipi |
|---|---|---|---|---|
| F1 | I2C rolü / adresler / timing | MASTER, 0x6B+0x48, `0xB0110305` | AYNI | **ÖZDEŞ** |
| F2 | Frame/XSUM/auto-increment | telemetri+restore XSUM | AYNI + LASTGASP XSUM | telemetri/restore özdeş; 32K ek LASTGASP checksum |
| F3 | ISR | I2C ISR yok (blocking) | AYNI | **ÖZDEŞ** |
| F4 | Bus-recovery / REC retry / mod makinesi | var | AYNI | **ÖZDEŞ** |
| F5 | Komut seti | monitor-only, dispatch yok | AYNI | **ÖZDEŞ** |
| F6 | PROT_VER | 0x03 | **0x07 (Rev0.7)** *(tarihsel zincir: 0x04 Rev0.4 → 0x05 Rev0.5 → 0x06 Rev0.6 → 0x07 Rev0.7)* | ⚠️ **PARİTE BOZULDU (bilinçli, 2026-07-22'den itibaren):** 32K uçtan-uca bütünlük (BLK_XSUM 0x1F→0x5F + tuz), STATBLK 7B + CFG_CRATE, GÜÇ bloğu (0xA0) ve `0x4A CHG_REAL` sırasıyla 0x04/0x05/0x06/0x07'yi getirdi; 16K FROZEN eski kuralda. Üst kart sürümü PROT_VER'den ayırt eder. **Kod otoritesi:** `i2c_protocol.h` §PROT_VER |
| F7 | Bekçi sayaçları 0x45-47 | var | var | **ÖZDEŞ** (parite) |
| F8 | Enerji/ömür alanları (0x06,08,0C,16,18,33,37,3B) | **YOK (0 gelir)** | DOLU (EFC/EQUIV_HOURS/GROSS_MAH/REM_*/DELTA/TOTAL) | **32K ekler** |
| F9 | Durum alanları (0x29,2A,2B,2D,2E,3F) | **YOK (0 gelir)** | DOLU (ICO/HEATER/BATT_STATE/CRATE/ICHG_TARGET/BMS) | **32K ekler** |
| F10 | SOC_X10 (0x1A) | VBAT→OCV LUT (yaklaşık) | coulomb sayımı (gerçek) | **yöntem farklı** (offset/tip aynı) |
| F11 | SOH_X10 (0x04) | hesaplanmaz (restore-held, %100) | LifeEst hesaplar | **davranış farklı** |
| F12 | RESTORE payload (0x60-71) | marker + RST_SOH_X10 | marker + SoC + EQUIV_HOURS + GROSS_MAH + TOTAL_MWH | **payload farklı** (uzunluk/marker/XSUM aynı) |
| F13 | LASTGASP bloğu (0x80) | **YOK** | 27B PUSH | **32K ekler** |
| F14 | feature_config | `ENABLE_UPLINK` sabit=1 (standalone deprecated) | çok modül (ENERGY/LIFE/HEATER/BATT_DETECT/LASTGASP) | build-konfig farkı |
| F15 | NTC alarm debounce | 2-örnek (ADC oversampling yok) | [AÇIK — 32K debounce ayrı doğrulanmadı] | muhtemel fark |

**Sözleşme-yüzeyi özeti:** register haritası offset/tip/XSUM/polarite/adres/timing **birebir aynı** →
üst kart tek-kod iki kartta çalışır. Farklar **zarif** (fazladan dolu alan / 0-alan / yöntem-kalitesi) —
sert kırılma yok. Tek dikkat: üst işlemci `0-alanları "veri yok"` işlemeli ve RESTORE payload'unu dala
göre yorumlamalı (F12). *(Karşılaştırma: agent 16K vs 32K kod çıkarımları, dosya:satır yukarıda.)*

---

## 7b. 🚦 `0x2B BATT_STATE` — RAPOR-ONLY SÖZLEŞMESİ `[Rev0.7 YENİ]`

**Üreten:** `battdet` modülü (RIPPLE FAZ 3+4, 2026-08-01'den beri) — `Src/battdet.c`.
Öncesinde alan `0xFF` gönderiliyordu (PA6/R5 prob modülü 2026-07-22'de kaldırılmıştı).

| Değer | Anlam | battdet karşılığı |
|---|---|---|
| `0` | **PRESENT** — akü VAR | `BATTDET_VAR` |
| `1` | SUSPECT | **bu sürümde ÜRETİLMEZ** (rezerve) |
| `2` | **ABSENT** — akü YOK | `BATTDET_YOK` |
| `3` | **PROBING** — dedektör ölçüyor, **hüküm YOK** | `BATTDET_BILINMIYOR` |
| `0xFF` | modül kapalı (`ENABLE_BATDET=0`) | — |

> ### 🔴 ÜST KART BU ALANA AKSİYON BAĞLAMAMALIDIR
> Dedektör **RAPOR-ONLY**'dir: PowerBoard tarafında da hiçbir kontrol aksiyonu (şarj kesme,
> davranış değiştirme) tetiklemez. Nedenleri:
>
> 1. **Eşikler henüz kalibre edilmemiştir** — beş sabitin tamamı `[TEZGAH_KALIBRE]`
>    (`Inc/battdet.h`). Kalibrasyon oturumu: `docs/BATDET_KALIBRASYON_TEST_PLANI.md`.
> 2. **Bilinen kör nokta:** ripple imzası **yalnız şarj regülasyonunda** (CV/DONE) oluşur.
>    Rampa/CC fazında ve regülasyon dışında hüküm `3 PROBING` döner — bu **"akü yok" demek
>    değildir**. `3`'ü "yok" sayan bir üst-kart mantığı **yanlış olur**.
> 3. Giriş gerilimine bağlı genlik sapması **açık sorudur** (12 V'ta ölçülen ~861 mV RMS,
>    20 V'ta doğrulanmadı). Kaynak: `docs/battery-absence-detection-study.md` §10.2.
>
> **İzin verilen kullanım:** görüntüleme, loglama, saha teşhisi.
> **Yasak:** alarm üretme, şarj/röle/modem davranışını değiştirme, kullanıcıya "akü yok"
> ihbarı gönderme. Aksiyon bağlama **ayrı bir karar + ayrı bir PROT_VER değerlendirmesi**
> gerektirir.

---

## 8. Açık kalemler `[AÇIK]` / **BİLİNEN BOŞLUKLAR**

- **[AÇIK] TCA9416 pass-gate mi repeater mi:** proje alanında TCA9416 datasheet yok (yalnız URL referansı).
  EDIF "I2C Bidirectional Voltage Level Translator" der; `PWR_I2C_Protocol.md` "FM+ köprü" der ama
  pass-gate/repeater mimari ayrımı lokal kaynaktan doğrulanamadı. *(Not: firmware davranışı pass-gate
  varsayar — B-taraf SDA-LOW tüm bus'ı kilitler, 9×SCL kurtarır; `i2c_protocol.c` yorumu `5b/§5`.)*
- **[AÇIK] Üst kart (STM32U375) kendi MCU I2C pinleri:** üst kart şeması proje alanında yok; yalnız J21
  arayüz pinleri (9=SDA, 10=SCL, 38=OVP_PV) belgeli. `[TEK-KAYNAK: EDIF/J21]`.
  > **Not (2026-07-21):** Proje bilgisine göre üst kart tarafı **`I2C3 = PD12(SCL) / PD13(SDA)`**
  > `[PROJE]` (kullanıcı beyanı). J21 fiziksel pin eşleşmesinin **nihai donanım/Fatih teyidi** hâlâ
  > beklendiğinden madde **[AÇIK]** kalır; pin bilgisi yalnız yönlendirme içindir.
- **[AÇIK] 32K NTC alarm debounce davranışı** (16K 2-örnek; 32K ayrı doğrulanmadı) — F15.
- **[AÇIK] Üst kart 0x48 adresinin nihai onayı** — `PowerBoard_FW/CLAUDE.md` "önerildi, onay bekliyor"
  der; kod + protokol dokümanı kesin 0x48 uygular (bkz. §Çelişkiler C2). Donanım onayı belgede net değil.

### 8b. ⭐ BİLİNEN BOŞLUKLAR — dürüst envanter `[Rev0.7 YENİ]`

> Bu bölüm **kasıtlıdır**: karşı taraf mühendisinin "bu belgede yazmıyor" diyeceği hiçbir
> nokta kalmasın diye, **bilmediğimizi de yazıyoruz.** Hiçbiri uydurulmuş değerle kapatılmadı.

| # | Boşluk | Neden açık | Kapatma yolu |
|---|---|---|---|
| **G-1** | **Üst kart 0x48 adres onayı** | Kod kesin 0x48 uygular; **donanım/Fatih nihai teyidi yok** | **S0** tezgâh adımı (ilk ortak koşu) |
| **G-2** | **Clock-stretching gerçek toleransı** | §6b.4 bütçesi **[HESAP]**'tır; yükselme süresi ve kablo kapasitansı hesaba katılmadı. Slave'in gerçekte ne kadar stretch edebildiği **ölçülmedi** | **[TEZGAH_DOGRULANACAK]** — skopla SCL LOW süresi + `HAL_TIMEOUT` sayacı |
| **G-3** | **400 kHz'de sinyal bütünlüğü** | 2.2 kΩ pull-up + 600R ferrit + 47 Ω seri + **ölçülmemiş kablo/konnektör kapasitansı**. Kod yorumu zaten doğrulama istiyor | **[TEZGAH_DOGRULANACAK]** — skopla `t_r`, SCL ~400 kHz, NACK yok |
| **G-4** | **TCA9416 pass-gate mi repeater mi** | Datasheet proje alanında yok; firmware **pass-gate varsayar** | Datasheet temini **veya** tezgâhta B-taraf SDA'yı kilitleyip A-tarafın etkilenmesini gözleme |
| **G-5** | **Bus-recovery'nin B-tarafına geçişi** | 9×SCL darbesinin TCA9416 üzerinden üst karta ulaştığı **varsayımdır** (G-4'e bağlı). Ayrıca recovery **yalnız BQ hatasına** bağlıdır, 0x48 hatasına değil (§6c.5) | **[TEZGAH_DOGRULANACAK]** — üst kartı SDA-LOW'da tutup 1.25 s içinde kurtarma gözlenmeli |
| **G-6** | **Üst kart 3V3 düşerse TCA9416 `VCCB=0` davranışı** | A-tarafını yükleyip yüklemediği lokal kaynaktan çıkarılamadı (`hardware-netlist-reference.md` sat.1094 de açık bırakıyor) | **[TEZGAH_DOGRULANACAK]** — üst kart 3V3'ü kesip BQ poll'unun sürüp sürmediğine bakılır |
| **G-7** | **Komut seti YOK** — protokol **monitor-only** | Üst karttan PowerBoard'a **komut/dispatch yolu yoktur**; tek etki yüzeyi STATBLK config'idir (kapasite/C-rate/t_cal). Şarj aç-kapa, röle, reset gibi komutlar **tanımlı değildir** | Genişletme **PROT_VER artışı** gerektirir — ayrı karar |
| **G-8** | **Panik NEDENİ I2C'de kodlanmıyor** | `PWR_PANIC` (J21.38) tek bit; neden (VBAT / VSYS / soğuk / sıcak) ayrı bir alanda **gelmez**. Üst kart alarm bitlerinden ve son telemetriden çıkarır | Yeni alan = PROT_VER artışı — ayrı karar |
| **G-9** | **Tanımsız offset'ten okuma davranışı** | Slave'in `>0x5F` (PULL blokları dışı) offset'lere ne döndüreceği **sözleşmede tanımsız**. PowerBoard hiç denemez | Fatih'in slave tasarımıyla netleşir; belgelenmeli |
| **G-10** | **Üst kart yokken uzun-süre davranışının saha kanıtı yok** | BENCH modu tezgâhta çalışıyor; **günler süren** çalışmada re-probe/mod makinesi davranışı gözlenmedi | Saha/uzun-koşu testi |
| **G-11** | **En-kötü bloklanma (≈370 ms) ölçülmedi** | §6c.2 tamamen **[HESAP]**; `AdcDma_PollLostCount()` sahada okunmadı | **[TEZGAH_DOGRULANACAK]** — kayıp-poll sayacı izlenmeli |
| **G-12** | **32K NTC alarm debounce** | 16K 2-örnek; 32K ayrı doğrulanmadı (§7 F15) | Kod incelemesi / tezgâh |
| **G-13** | **`0x2B BATT_STATE` eşikleri kalibre değil** | 5 sabit `[TEZGAH_KALIBRE]`; alan **RAPOR-ONLY** (§7b) | `docs/BATDET_KALIBRASYON_TEST_PLANI.md` |

**`[TEZGAH_DOGRULANACAK]` etiketli madde sayısı: 6** → G-2, G-3, G-5, G-6, G-11 + §6b.1
bus hızı skop teyidi.

---

## 🔺 Çelişkiler (kod-otoriter çözüm + açık rapor)

**C1 — Bus hızı (400 kHz vs 100 kHz):** `BQ25798_Register_Reference.md` (L819-820) "Bizim fSCL 100 kHz
Standard Mode" + "fSCL max 1000 kHz FM+" der. **KOD OTORİTER: 400 kHz** (`i2c.c:41` Timing `0xB0110305`,
Fast Mode) — CLAUDE.md + `PWR_I2C_Protocol.md` + BQ datasheet (≤400 kbit/s) ile tutarlı. Register-ref'teki
100 kHz/1000 kHz ifadeleri **bayat/yanlış** → düzeltilmeli. *(Çözüm: 400 kHz Fast Mode.)*

**C2 — Üst kart 0x48 durumu:** `PowerBoard_FW/CLAUDE.md` L193 "0x48 önerildi, onay bekliyor"; kod + protokol
dokümanı 0x48'i **kesin uygular**. **KOD OTORİTER: 0x48 uygulanıyor** (`i2c_protocol.h:41`). CLAUDE.md notu
tarihsel/güncellenmemiş. *(Donanım tarafı nihai teyidi §8 [AÇIK] olarak kalır.)*

**C3 — Bekçi sayaçları "16K-ONLY" yorumu:** 16K `i2c_protocol.h` yorumu 0x45-47'yi "16K-ONLY" der; oysa
32K de PROT_VER=0x03 + sayaçları uygular (parite serisi sonrası). **KOD DEĞERİ tutarlı** (iki dal 0x03 +
sayaçlar); yalnız 16K header **YORUMU bayat**. Fonksiyonel çelişki değil, doküman-yorumu çelişkisi →
16K yorumu güncellenebilir. *(Çözüm: iki dalda da var; parite tam.)*

---

## 9b. 📦 PAKET ENVANTERİ + HAZIR REFERANS KOD + CONFIG DEFAULT'LARI `[Rev0.7 YENİ]`

### 9b.1 Hazır referans kod — `docs/upper_board_reference/`

| Dosya | İçerik |
|---|---|
| **`pwr_i2c_packets.h`** | Tüm offsetler, uzunluklar, enum'lar, bit maskeleri, **config default'ları**, paket struct'ları, API |
| **`pwr_i2c_packets.c`** | MSB-first serileştirme + XSUM + **her paket için parse/build** fonksiyonları |

> **Bağımsızdır** (yalnız `<stdint.h>`), PowerBoard firmware'ine **derlenmez**, üst kart
> projesine doğrudan alınabilir. `arm-none-eabi-gcc -Wall -Wextra -O2` ile
> **0 error / 0 warning** doğrulandı (cortex-m0plus hedefiyle).
>
> Paket yapılarını **buradan alın** — MD tabloları insan okuması içindir, kod otoritedir.

### 9b.2 Paket envanteri — tek bakışta

| Paket | Yön | Offset | Uzunluk | Kadans | Bütünlük | API |
|---|---|---|---|---|---|---|
| **TELEMETRİ** | PowerBoard → üst kart (PUSH) | `0x00` | **96 B** | ~1000 ms | `[0x5F]` | `PwrI2c_ParseTelemetry()` |
| **GÜÇ BLOĞU** | PowerBoard → üst kart (PUSH) | `0xA0` | **21 B** | ~1000 ms, telemetriden hemen sonra, **ayrı burst** | `[0xB4]` | `PwrI2c_ParsePowerBlock()` |
| **LASTGASP** | PowerBoard → üst kart (PUSH) | `0x80` | **27 B** | olay-tetikli (güç kesilmesi) | `[0x9A]` + marker `0xB5` | `PwrI2c_ParseLastGasp()` |
| **RESTORE** | üst kart → PowerBoard (PULL) | `0x60` | **18 B** | **yalnız boot'ta** | `[0x71]` + marker `0xA5` | `PwrI2c_BuildRestore()` |
| **STATBLK** | üst kart → PowerBoard (PULL) | `0x72` | **7 B** | ~8 s | `[0x78]` | `PwrI2c_BuildStatBlk()` |

**Bütünlük kuralı — hepsinde aynı:** son bayt = `XOR(kendinden önceki tüm baytlar) ^ 0x5A`.

### 9b.3 ⭐ CONFIG DEFAULT'LARI — üst kart bunları sunmalı

> **Üst kartta henüz konfigürasyon yoksa STATBLK'ı aşağıdaki değerlerle sunun.**
> Bunlar PowerBoard'ın kendi default'larıyla **aynıdır** → "ayarsız" ile "ayarlı ama aynı"
> arasında davranış farkı oluşmaz. Hazır fonksiyon: **`PwrI2c_BuildStatBlkDefaults()`**.

| Off | Alan | **DEFAULT** | Geçerli aralık | "Ayarsız" işareti | PowerBoard ayarsızken ne yapar |
|---|---|---|---|---|---|
| `0x72` | `REC_ACK` | **`0x00`** | 0-255 | — | kayıt eşleşmez → REC retry (~2.5 s) sonra `REC FAIL` |
| `0x73` | `CFG_GEN` | **`0x01`** | 0-255 (nabız) | — | değer **değişince** tam config okur |
| `0x74` | `CFG_CAP_AH` | **`12`** | **7..50** | — | aralık dışıysa en yakın tablo index'i |
| `0x75-76` | `CFG_TCAL` (Q15) | **`0x0000`** | Q15, ±%3.5 clamp | **`0x0000` / `0xFFFF`** | nominal zaman kalibrasyonu |
| `0x77` | `CFG_CRATE` | **`10`** (%10) | **5..20** | **`0x00` / `0xFF`** | firmware default **%10** → ICHG **1.2 A** |
| `0x78` | `CFG_XSUM` | hesaplanır | — | — | tutmazsa okuma yok sayılır, ~8 s sonra tekrar |

**RESTORE için default:**

> 🔴 **Saklanmış veri YOKSA marker'ı `0xA5` YAPMAYIN.** `0x00` (veya `0xA5` dışı herhangi
> bir değer) gönderin → PowerBoard "geçersiz" deyip **temiz-akü default'larıyla** başlar
> (SoC %100). `0xA5` + çöp veri göndermek **yanlış SoC/enerji tohumlar** ve SoH/ömür
> tahminini kalıcı bozar. Hazır fonksiyon: **`PwrI2c_BuildRestoreEmpty()`**.

**PowerBoard'ın üst kart hiç yokken kullandığı değerler** (karşılaştırma için):

| Alan | Değer | Kaynak |
|---|---|---|
| SoC | **%100** (`s_soc_x10 = 1000`) | `i2c_protocol.c` Init |
| Kapasite | **12 Ah** | `i2c_protocol.h:223` |
| C-rate | **%10** → ICHG **1.2 A** | `battery_cfg` |
| BMS var mı | **1 (var)** | Init |
| t_cal | nominal | `i2c_protocol.c:776-781` |

### 9b.4 Kullanım iskeleti (üst kart tarafı)

```c
#include "pwr_i2c_packets.h"

/* --- PowerBoard'dan gelen yazmalar (slave receive) --- */
void OnMasterWrite(uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (reg == PWR_TLM_BASE && len == PWR_TLM_LEN) {
        PwrTelemetry_t t;
        if (PwrI2c_ParseTelemetry(data, &t)) { UseTelemetry(&t); }
        /* 0 dönerse: XSUM tutmadı -> BLOĞU AT, bir sonraki turu bekle */
    }
    else if (reg == PWR_PWRBLK_BASE && len == PWR_PWRBLK_LEN) {
        PwrPowerBlock_t p;
        if (PwrI2c_ParsePowerBlock(data, &p)) { UsePower(&p); }
    }
    else if (reg == PWR_LASTGASP_BASE && len == PWR_LASTGASP_LEN) {
        PwrLastGasp_t g;
        if (PwrI2c_ParseLastGasp(data, &g)) { PersistNow(&g); }  /* güç kesiliyor! */
    }
}

/* --- PowerBoard'ın okuyacağı bloklar: GÖLGE TAMPON + ATOMİK YAYIN --- */
static uint8_t s_restore[PWR_RESTORE_LEN];
static uint8_t s_statblk[PWR_STATBLK_LEN];

void ConfigInit(void)
{
    if (HaveStoredState()) {
        PwrRestore_t r = { .soc_x10 = LoadSoc(), .equiv_hours = LoadEquiv(),
                           .gross_mah = LoadGross(), .total_mwh = LoadMwh() };
        PwrI2c_BuildRestore(s_restore, &r);
    } else {
        PwrI2c_BuildRestoreEmpty(s_restore);      /* marker 0x00 — DOĞRU davranış */
    }
    PwrI2c_BuildStatBlkDefaults(s_statblk);       /* 12 Ah / %10 / t_cal ayarsız */
}

/* Config değişince: gölge tamponda kur, sonra TEK SEFERDE yayınla */
void ConfigApply(uint8_t cap_ah, uint8_t crate_pct)
{
    uint8_t tmp[PWR_STATBLK_LEN];
    PwrStatBlk_t sb = { .rec_ack = LastAckedSeq(),
                        .cfg_gen = NextGen(),      /* ⭐ DEĞİŞMELİ ki PowerBoard okusun */
                        .cap_ah = cap_ah, .tcal_q15 = TcalOrZero(),
                        .crate_pct = crate_pct };
    PwrI2c_BuildStatBlk(tmp, &sb);
    AtomicPublish(s_statblk, tmp, PWR_STATBLK_LEN);   /* kesme kapalı / çift tampon */
}
```

---

## 10. Sözleşme & tasarım niyeti (üst-kart protokolü)

> Kaynak: `PWR_I2C_Protocol.md` (emekliye ayrıldı — içeriği buraya taşındı). Kod-teyitli.

**Rol felsefesi:** Üst işlemci (`0x48`) **YALNIZ MONİTÖR + KAYITÇI** — şarj kontrolü YOK. PowerBoard
MASTER'dır: telemetriyi PUSH eder, SOH/SOC'yi her periyotta gönderir. **Kalıcı kayıt üst kartta** tutulur
(PowerBoard Flash'a yazmaz); PowerBoard alarm anında `REC_FLAG` ile "bu örneği kaydet" der. Açılışta
PowerBoard üst karttan son SOH/SOC + enerji sayaçlarını **okuyup tohumlar**. Tek yapılandırma girişi:
üst kart akü kapasitesini (7-50 Ah) gönderebilir; gelmezse 12 Ah varsayılan.

**REC (kayıt-onay) tam akış (`0x1C`/`0x72`):** Yeni alarm (latch 0→1) → `REC_FLAG` bit0 + güncel `SEQ`
ile blok yazılır → PowerBoard `REC_ACK (0x72)` okur → `REC_ACK == SEQ` ise üst kart kalıcı kaydetti (ACK).
ACK yoksa ≤10 kez tekrar (5ms arayla, ~250ms tavan). Başarıda UART `LOG_I("REC OK seq=..")` **(WARN
seviyesinde GİZLİ — sessiz başarı)**; 10 fail'de `LOG_E("REC FAIL seq=..")` + **LED 6× blink** (CLAUDE.md
LED tablosu). `REC_FLAG` bit1 = CHECKPOINT (periyodik/olay kalıcı yazma; aynı ACK mekanizması).
> **Üst-kart tasarım rehberi:** ACK döngüsü kötü durumda ana döngüyü ~250ms bloklar.
> ⚠️ **NETLEŞTİRME (2026-07-21) — 🔴 GÜNCELLENDİ 2026-08-21:** Bu pencerede **İKİ bağımsız donanım
> koruması** aktiftir: **(1) STM32 `AWD1` analog watchdog — CANLI** (`HT1=761`, trip **27.0 V**,
> tepki **~56 µs**; §6c.3 tablosuyla aynı), **(2) BQ25798 çip içi `VAC_OVP`** (POR ~26 V;
> `BQ25798_Register_Reference.md` REG10[5:4], §7.5.1.13).
> ~~*"STM32 AWD1 donanım-OVP yolu BUGÜN ÖLÜDÜR · `HT1=1523` onarımı henüz uygulanmadı"*~~
> **— BU HÜKÜM GERİ ÇEKİLDİ.** AWD1 onarımı **22.07.2026'da (I-7) UYGULANDI**; uygulanan değer
> `HT1=1523` değil **`HT1=761`** (ratio 4 / shift 0) olmuştur. `HT1=1523` önerisi hiç kodlanmadı.
> Kaynak izi: `Inc/ovp_control.h:45-47` · `Inc/feature_config.h:24` (`ENABLE_I7_ADC_DMA=1`).
> *(Bu satır §6c.3 ile **çelişiyordu**; §6c.3 doğruydu.)* Üst kart `REC_ACK`'i **<5ms** güncellemeli;
> `SEQ` atlaması bu pencereyi zaten görünür kılar.

**CFG_GEN nabız mekanizması (`0x73`):** Slave (üst kart) master'ı asenkron uyaramaz → PowerBoard config'i
**nabızla** öğrenir: açılışta config bloğunu tam okur; runtime yalnız `CFG_GEN`'i (1 bayt) **yavaş poll**
eder (~8s / her 8. periyot); `CFG_GEN` değişince bloğu tam okur + uygular. **Her periyot tam-okuma YOK.**
`CFG_CAP_AH (0x74)` 7-50 Ah → `ICHG = cap × C/10`; aralık dışı → 12 Ah. Kapasite **değişimi** SoH/SoC/enerji
sayaçlarını reset tetikler → detay + reset tablosu: **`Battery_State_Management.md`**.

**PROT_VER (Rev0.8):** 32K `0x1E` = **0x08** — **TEK SÖZLEŞME, geçmişe-uyum katmanı YOK**
(kullanıcı kararı 2026-07-22, **2026-08-27'de 0x07 için YİNELENDİ**): üst kart 32K karta karşı
Rev0.7 kurallarını uygular (XSUM 0x5F, STATBLK **0x72-0x78 = 7B** [CFG_CRATE 0x77 dahil],
tuz 0x5A, **`0x4A` = CHG_REAL — rezerve DEĞİL**).
⛔ Üst kart `0x1E != 0x08` görürse telemetriyi **REDDEDER**; XSUM tek başına yetmez —
XSUM yalnız *bozulmayı* yakalar, alan kayması üreten bir *sürüm farkını* geçerli XSUM ile
birlikte geçirir. *(Rev0.6=0x06 / Rev0.5=0x05 / Rev0.4=0x04 tarihsel; 16K arşiv kartı
bağlanırsa PROT_VER=0x03 görülür — o eski kural setidir; 16K üretimi durdu.)*

**Telemetri alanı → üreten firmware modülü (mimari):**
| Alan (off) | Üreten modül |
|---|---|
| SYS_STATE (0x00) | `main` durum makinesi |
| SYS_FLAGS/ALARM_LIVE/LATCH (0x02/0x30/0x31) | `i2c_protocol` (STM gözlem/alarm mantığı) |
| BATT_STATE (0x2B) | ~~`batt_detect` (PA6/R5 aktif prob)~~ **modül KALDIRILDI (2026-07-22)** → ✅ **2026-08-01'den itibaren `battdet` (RIPPLE FAZ 3+4) sürüyor** (T-11 düzeltmesi KAPANDI). Eşleşme: **0=PRESENT** (akü VAR) · **2=ABSENT** (akü YOK) · **3=PROBING** (hüküm yok / kapılar kapalı). `1=SUSPECT` bu sürümde ÜRETİLMEZ (rezerve). `0xFF` yalnız `ENABLE_BATDET=0` iken. ⚠️ Dedektör **RAPOR-ONLY**: bu alan hiçbir kontrol aksiyonunu tetiklemez, üst kart da tetiklememelidir (eşikler `[TEZGAH_KALIBRE]`). **PROT_VER 0x06 DEĞİŞMEDİ** — alan zaten sözleşmedeydi, yalnız gerçek değer gelmeye başladı. SSOT: `docs/battery-absence-detection-study.md` §10 |
| HEATER_STATE (0x2A) | `heater_control` (PA7) |
| BOARD_TEMP/VPV/VDC/STM_VBAT (0x22/12/14/54) | STM ADC (PA3/PA4/PA5/PA2) |
| SOH/SOC/enerji (0x04/1A/06...) | `life_estimator`/`energy_meter` (yalnız 32K) |
> STM-kaynaklı alarm bitleri (§4) BQ donanım fault'larından (REG20/21) AYRIDIR: BQ fault = yonga koruması,
> ALARM bitleri = STM firmware'inin kendi izlemesi.

**Kapanmış tasarım kararları (2026-07-01):** SoC kaynağı (16K OCV-LUT / 32K coulomb, 32K'da OCV ASLA) ·
REC ACK'li ≤10-deneme · restore `0x60` blok-read (komutsuz) · checksum 8-bit XOR (16K-dostu) · adres
çakışması yok · `BATT_TEMP_X10` açık-NTC sentinel = -9990.

## 11. İşlem (transaction) haritası

| # | İşlem | Tip | Yön | Tetik | Offset | Boyut |
|---|---|---|---|---|---|---|
| 1 | Telemetri | write | PWR → Üst | periyodik ~1s | 0x00-0x5F | 96 B |
| 2 | Restore | read | PWR ← Üst | açılış (1 kez) | 0x60-0x71 | 18 B |
| 3 | Kayıt ACK | read | PWR ← Üst | kayıt isteği sonrası (≤10×) | 0x72 | 1 B |
| 4 | Config nabız | read | PWR ← Üst | yavaş ~8s | 0x73 | 1 B |
| 5 | Config tam (STATBLK) | read | PWR ← Üst | açılış + CFG_GEN Δ | 0x72-0x78 | 7 B |
| 6 | LASTGASP (32K) | write | PWR → Üst | güç-kesilme öngörü | 0x80 | 27 B |
| 7 | **Güç bloğu (32K)** | write | PWR → Üst | periyodik ~1s (telemetriden hemen sonra) | 0xA0-0xB4 | 21 B |

REG1C[4:1] **VBUS_STAT** giriş-kaynağı tipi: 0000=Yok · 0001=USB SDP · 0010=CDP · 0011=DCP · 0101=Unknown ·
0111=HVDCP · 1000=BACKUP (BQ datasheet Tablo 7-37).

## 12. Bölüm-bazlı kaynak özeti

Her bölüm sonunda "Kaynak §N" bloğu inline verilmiştir. Genel kaynak seti: **Kod** (`i2c.c`, `bq25798.c/.h`,
`bq25798_poll.c/.h`, `i2c_protocol.c/.h`, `stm32c0xx_it.c`, `main.c` — her iki dal, `git show dev-16k:` /
çalışma ağacı dev-32k) · **Donanım** (`_EDF.txt` EDIF netlist, `Haberlesme-Modulu_..._R0-00.PDF`, DOC/
BQ25798 SLUSDV2C §7.3.14) · **MD** (`BQ25798_Register_Reference.md`, `PWR_I2C_Protocol.md`, CLAUDE.md'ler).

**Otorite kuralı uygulandı:** her fonksiyonel değer koddan (dosya:satır); donanım bağlamı EDIF/PDF/datasheet
ile çapraz-doğrulandı; üç çelişki koddan çözülüp açıkça raporlandı; bulunamayan dört kalem `[AÇIK]` işaretlendi.

---

## 📝 Revizyon Notu — 2026-07-21 (bayat/mesnetsiz nokta düzeltmeleri; kod değişikliği yok)

**Kaynak:** `BAYAT_MESNETSIZ_DENETIM_2026-07-21.md` §A/§A2 · `BQ25798_Register_Reference.md` (REG10 WATCHDOG,
VAC_OVP) · `PowerBoard_FW/CLAUDE.md` (OVP kutusu, PART_INFO). **Repo kuralı:** eski içerik korundu; atıf
numaraları Rev C'ye güncellendi, teknik değer uydurulmadı.

| # | Nerede | Ne | Neden |
|---|---|---|---|
| 1 | §Taranan kaynaklar + tüm gövde | `SLUSDV2B §9.3.14 / Fig 9-19 / §9.3.14.3/.7 / Tablo 9-37` → **SLUSDV2C §7.3.14 / Fig 7-19 / §7.3.14.3/.7 / Tablo 7-37** + Rev B→Rev C atıf kutusu | Rev B PDF silindi; Rev C bölüm renumber (`§9.3.x→§7.3.x`) |
| 2 | §3a REG48 satırı | PART_INFO 0x18 ↔ CLAUDE.md 0x19 → **🔴 çelişki kutusu** ("datasheet Rev C ile çözülecek") | İki değer farklı ölçüm (maskeli PN vs DEV_REV dahil); UYDURULMADI |
| 2b | §3a REG48 satırı + kutu | **🔴→✅ KAPATILDI** (2026-07-21 araştırma): çelişki DEĞİL — `0x18`=maskeli PN (`pn&0x38==0x18`), `0x19`=tam POR bayt (DEV_REV=001 dahil). İkisi de doğru | Datasheet Rev C §7.5.1.57 s.128 ile teyitli; register'ın iki görünümü |
| 3 | §6 BQ watchdog | POR=40s → **kaynak izi eklendi** (REG10 WATCHDOG, §7.5.1.13 T7-26 s.72) | İzsiz sayısal değer |
| 4 | §8 [AÇIK] üst kart pinleri | **`I2C3=PD12(SCL)/PD13(SDA)` [PROJE] notu** eklendi; [AÇIK] korundu | Proje gerçeği; nihai Fatih/donanım teyidi bekliyor |
| 5 | §10 REC penceresi | "OVP donanım yedeği aktif" → **netleştirme**: aktif olan **BQ çip VAC_OVP (~26V)**; ~~**STM AWD1 yolu BUGÜN ÖLÜ**~~ | "STM donanım OVP boşluğu kapatıyor" yanılgısını önlemek için |

> 🔴 **Yukarıdaki 5 no'lu satırın "AWD1 ÖLÜ" kısmı SUPERSEDED'dir** (22.07.2026 I-7 ile
> AWD1 CANLI oldu, `HT1=761`, 27,0 V, ~56 µs). Bir alttaki 2026-07-22 notu ve dosya
> sonundaki **2026-08-21** notu geçerlidir. *(Bu satır tarihsel iz olarak bırakıldı;
> 2026-08-21'de işaretlendi — işaretsiz hâli okuyucuyu yanlış yönlendiriyordu.)*

**Silinen doğru içerik yok.**

---

### 📝 Revizyon Notu — 2026-07-22 (Commit 5 / I-7)

| # | Nerede | Ne | Neden |
|---|---|---|---|
| 1 | §3c PULL tablosu | **YENİ register `CFG_TCAL` 0x75-0x76** (t_cal Q15 BE; 0/0xFFFF=yok→nominal; NACK=geri-uyumlu sessiz atlama; clamp ±%3.5) | I-7 madde 13: fabrika zaman-kalibrasyonu üst kartta saklanır, boot'ta yüklenir (kullanıcı kararı D1, 2026-07-22). **[DIŞ] Fatih tarafına bildirilecek** — eski üst kart kodu KIRILMAZ (ayrı işlem, NACK toleranslı) |
| 2 | (bilgi) | §10'daki "STM AWD1 yolu BUGÜN ÖLÜ" notu: `ENABLE_I7_ADC_DMA=1` binaride artık **CANLI** (27.0V, ~56µs — Commit 5); default (I7=0) binaride ölü kalır | I-7 kodlandı; hangi binarinin sahada olduğuna dikkat |

**Silinen doğru içerik yok.**

---

### 📝 Revizyon Notu — 2026-07-22 (3. tur) — PROTOKOL Rev 0.5: STATBLK'e CFG_CRATE

**Kullanıcı kararı:** Şarj C-rate'i üst karttan I2C ile ayarlanabilsin (default C/10, aralık 0.05-0.20C).

| # | Değişiklik | Nerede | Gerekçe |
|---|-----------|--------|---------|
| 1 | **STATBLK 6B → 7B (0x72-0x78):** `[5]=CFG_CRATE (0x77)` eklendi, XSUM `0x77→0x78 [6]`. `[6]=XOR(0x72..0x77)^0x5A` | §3c PULL / STATBLK | C-rate yazma yolu eksikti (yalnız OKUMA echo 0x2D vardı) |
| 2 | **PROT_VER 0x04 → 0x05** | 0x1E + §PROT_VER | Sürüm sinyali; **GEÇMİŞE-UYUM YOK** |
| 3 | **CFG_CRATE semantiği:** 5..20 (0.05-0.20C); `0`/`0xFF`=ayarlanmamış→default %10. PowerBoard 5-20 clamp; ICHG=cap×crate×10mA (5000 tavan); SoH/SoC reset YOK | 0x77 satırı + `battery_cfg.h` | Eski 1-100% guven-sınırı fazla açıktı (crate=100%→yanlış kapasitede 0.42C+); LiFePO4 için 0.20C muhafazakâr-güvenli |
| — | Kod: `i2c_protocol.h` (STATBLK_LEN 7, CFG_CRATE/XSUM/PROT_VER), `i2c_protocol.c I2cProt_ReadConfigFull` (crate parse+apply), `battery_cfg.h` (CRATE_MIN 5/MAX 20) | — | Build 0/0 |

> ⚠️ **[DIŞ] FATİH KOORDİNASYONU ZORUNLU (hard cutover):** Üst kart STATBLK'ı **7 bayt** yazmalı ve
> XSUM'u **7B üzerinden** (`XOR(0x72..0x77)^0x5A`, 0x78'de) hesaplamalı. Aksi halde 6-baytlık eski
> XSUM PowerBoard'ın 7-bayt okumasında **tutmaz → TÜM config (cap dahil) reddedilir → default**.
> PROT_VER=0x05 ile sürüm ayırt edilir. Deploy koordineli olmalı.

**Silinen doğru içerik yok.**

---

### 📝 Revizyon Notu — 2026-07-22 (2. tur) — PROTOKOL Rev 0.4: uçtan-uca bütünlük

| # | Nerede | Ne | Neden |
|---|---|---|---|
| 1 | §2 bütünlük bölümü | **TEK ORTAK KURAL:** her bloğun SON baytı = `XOR(öncekiler)^0x5A`; veri ortasında bütünlük baytı YASAK. 4 blok tablosu (Telemetri/LASTGASP/RESTORE/STATBLK) | Kullanıcı kararı 2026-07-22: "her pakette bütünlük, son bayt bütünlük için, ortada olmasın" |
| 2 | §3b 0x1F/0x1E/0x5F satırları | **BLK_XSUM 0x1F → 0x5F** (son bayt); 0x1F REZERVE(0x00); **PROT_VER 0x03 → 0x04** | Son-bayt kuralı; sürüm sinyali |
| 3 | §3c PULL tablosu | **STATBLK 0x72-0x77 (6B)** tek okuma yolu — REC_ACK/CFG_GEN/CAP/TCAL tekil okunMAZ; son bayt 0x77 XSUM; gölge-tampon atomiklik şartı | REC_ACK/CFG korumasızdı (bit hatası + yırtık-okuma açığı); önceki turdaki "NACK geri-uyumlu ayrı TCAL okuması" hükmü **KALDIRILDI** — geçmişe-uyum YOK (kullanıcı kararı) |
| 4 | PROT_VER bölümleri (F6, §geri-uyum) | "0x02 VE 0x03 kabul et" hükmü kaldırıldı → **tek sözleşme 0x04**; 16K FROZEN 0x03 tarihsel, parite bilinçli bozuldu | Geçmişe-uyum katmanı istenmiyor; 16K üretimi durdu |
| 5 | (kod) | `Prot_Xsum()` tek uygulama noktası; `Prot_ReadStatBlock()`; LASTGASP/RESTORE tuzlandı | `i2c_protocol.c/.h` — DEVLOG 2026-07-22 |

**Silinen doğru içerik yok** — süperseded hükümler tarihsel not olarak işaretlendi.


### 📝 Revizyon Notu — 2026-07-23 — PROTOKOL Rev 0.6: anlık GÜÇ bloğu (0xA0-0xB4)

**Kaynak:** `docs/power-telemetry-design.md` (tasarım SSOT, kullanıcı onayı 2026-07-23) ·
kod `power_calc.c/.h` + `i2c_protocol.c I2cProt_SendPowerBlock`.

| # | Ne | Nerede | Neden |
|---|---|---|---|
| 1 | **YENİ blok: GÜÇ 0xA0-0xB4 (21 B) PUSH** | §Blok tabloları + §Bütünlük + §İşlem listesi | Anlık güç telemetrisi (Ppv/Pdc/Psys/Pbat/Pin). Üst kartın güç bütçesi + saha teşhisi için |
| 2 | **PROT_VER 0x05 → 0x06** | 0x1E + §PROT_VER (3 yer) | Sürüm sinyali; **GEÇMİŞE-UYUM YOK** (tek sözleşme kuralı) |
| 3 | Bütünlük tablosuna 5. satır | §Uçtan-uca bütünlük | Rev0.4 "SON bayt" kuralı yeni bloğa da aynen uygulanır: `0xB4 = XOR(0xA0..0xB3)^0x5A` |

⭐ **Tasarım kararı — mevcut sözleşme KORUNDU (Seçenek B):** telemetri bloğu `0x00-0x5F`
**bayt-bayt değişmedi**; güç verisi mevcut bloğa sıkıştırılmak yerine yeni bir bölgeye
**ayrı burst** ile yazılır. Böylece Fatih tarafında mevcut ayrıştırma kodu **hiç
değişmez**, yalnız yeni bölge eklenir. Bedeli: saniyede 1 ek yazma (~21 B @400 kHz
≈ 0.5 ms, bus ≈ %0.05). Alternatif (mevcut 8 boş bayta 10 mW/LSB int16 sığdırmak)
çözünürlük ve ölçek belirsizliği nedeniyle **reddedildi**.

**Alan semantiği (üst kart için kritik):**
- Tümü **int32, MSB-first, birim mW** (2's complement — negatifler işaretlidir).
- `PBAT_MW`: **+ = şarj** (aküye giren) / **− = deşarj**.
- `PSYS_MW`: **negatif = tüketim**. **TÜRETİLMİŞ değerdir, ölçüm değil** —
  BQ25798'de ISYS ADC kanalı yoktur; `Psys = Pbat − η·Pin` denge denkleminden gelir.
  Giriş yokken (`PIN_MW = 0`) `Psys ≡ Pbat`, yani **tam doğru**; şarj sürerken η
  belirsizliği doğrudan Psys'e biner. η bugün **1.000 (düzeltme yok)**, tezgâh
  kampanyasından kalibre edilecek. **Clamp yoktur:** pozitif bir `PSYS_MW`
  ölçüm/η hatasının göstergesidir, veri bozulması değil.
- `PPV_MW`/`PDC_MW`: aktif olmayan yol **0**'dır (donanım aynı anda tek yol iletir).
  Kaynak atfı belirsizse (AMB/UNK) **ikisi de 0** olur ama `PIN_MW` yine geçerlidir —
  yani `PPV+PDC ≠ PIN` görülebilir; **`PIN_MW` toplam giriş için otoritedir**.
- Kaynak atfı kodu (`src`) ve güven seviyesi (`q`) I2C'ye **konmadı** (UART D/DIAG
  satırında var). Gerekirse 0xB5/0xB6'ya eklenir — yeni PROT_VER gerektirir.
- **Isıtıcı bu bloğun HİÇBİR alanında görünmez:** `VPV`'den beslenir (Q10 P-kanal
  high-side, ACFET2'nin öncesi), akımı BQ'nun hiçbir pininden geçmez. Isıtıcı ON iken
  panel yüklenip VPV çöker → `PPV_MW` düşer; **"PPV≈0" tek başına "PV yok" demek
  değildir** (bkz. `docs/hardware-netlist-reference.md` §4.2).

**[DIŞ] Fatih aksiyonu:** 0xA0-0xB4 bölgesini yazılabilir kıl, 21 baytı gölge-tamponla
al, `0xB4` XSUM'u doğrula (tutmazsa bloğu at). Mevcut 0x00-0x5F ayrıştırması
**değiştirilmeyecek**; yalnız `PROT_VER` beklentisi 0x05 → **0x06** olacak.

---

### 📝 Revizyon Notu — **Rev0.7** (2026-08-01) — sistem gerçekliği senkronizasyonu
### ⚠️ **TEL FORMATI DEĞİŞMEDİ — `PROT_VER` = `0x06` AYNEN KALIR**

**Amaç:** bu dokümanı üst kart (STM32U375VET6) implementasyonu için **sözleşme** hâline
getirmek. Fatih tarafında **hiçbir ayrıştırma kodu değişmez**; eklenen her şey daha önce
yazılı olmayan sınır/kural/arayüz bilgisidir.

**Yöntem:** her beyan **KODDAN** türetildi (`Otorite: KOD > şema/EDIF > SLUSDV2C > MD`).
Koddan türetilemeyen hiçbir sayı yazılmadı; ölçüm gerektirenler **[TEZGAH_DOGRULANACAK]**,
bilinmeyenler **§8b Bilinen Boşluklar** (G-1…G-13).

#### Rev0.6 → Rev0.7 farkları

| # | Ne eklendi/değişti | Bölüm |
|---|---|---|
| 1 | **Sürüm ayrımı kutusu** — `PROT_VER` (tel formatı) ↔ doküman revizyonu | başlık |
| 2 | **§1b ARAYÜZÜN TAMAMI** — `PWR_PANIC` (J21.38, aktif-LOW, 4 tetik koşulu + eşikleri, üst kartın beklenen tepkisi), `CHARGER_STAT` (J21.6, BQ open-drain), **J21 tam pin listesi** + besleme topolojisi tuzağı (3V3 üst karttan) | YENİ |
| 3 | **§6b ELEKTRİKSEL + ZAMANSAL SINIRLAR** — hat parametreleri, pull-up/ferrit/seri değerleri (EDIF), kadans tablosu, **işlem süreleri ve clock-stretching bütçesi** (96 B push → **7.79 ms toplam / ≈81 µs per bayt**) | YENİ |
| 4 | **§6c BLOKLANMA KISITI** — en-kötü poll bloklanması (**≈370 ms > 250 ms**), **güvenlik beyanı** (AWD1 donanım + `OVP_I7Process` DMA-ISR yolu I2C'den **etkilenmez**), `PWR_PANIC` gecikmesi (nominal ~250 ms, en-kötü ~620 ms), **slave kuralları S-1…S-9**, bus-recovery gerçeği | YENİ |
| 5 | **§6d BOOT + ZARİF DÜŞÜŞ** — boot dizisi, üst kart yokken davranış matrisi + `W/PROT` logları, default'lar, **CFG yazma yolu (0x77 CFG_CRATE dâhil)**, **rezerve alan davranışı (0x1F, 0x4A-0x4F)** | YENİ |
| 6 | **§7b `0x2B` RAPOR-ONLY SÖZLEŞMESİ** — battdet bağlantısı, 4 değer + `0xFF`, **"üst kart aksiyon bağlamamalı"** kuralı ve 3 gerekçesi | YENİ |
| 7 | **§8b BİLİNEN BOŞLUKLAR** — G-1…G-13 dürüst envanter (6 adet [TEZGAH_DOGRULANACAK]) | YENİ |
| 7b | **§9b PAKET ENVANTERİ + REFERANS KOD + CONFIG DEFAULT'LARI** — 5 paketin tek-bakış tablosu · **`docs/upper_board_reference/pwr_i2c_packets.{h,c}`** (bağımsız, `-Wall -Wextra` **0/0**) · **config default tablosu** (`CAP=12 Ah`, `CRATE=%10`, `TCAL=0x0000`, `GEN=0x01`, `REC_ACK=0x00`) + **RESTORE boş-marker kuralı** + üst kart kullanım iskeleti | YENİ |
| 8 | **§1 timeout tablosu** — BQ **20 ms** / üst kart **10 ms** + **HAL timeout semantiği** (işlem-toplamı son-tarih) | düzeltme |
| 9 | **§6 bayat beyan düzeltmeleri** (6 kalem, eskisiyle birlikte listelendi) | düzeltme |

#### 🔴 KODA AYKIRI BULUNAN ESKİ BEYANLAR (sessizce düzeltilmedi — açık rapor)

| # | MD'deki eski beyan | KOD gerçeği | Kaynak |
|---|---|---|---|
| E-1 | §1 "BQ `I2C_TIMEOUT_MS = **100 ms**` (`bq25798.h:67`)" | **20 ms** (`bq25798.h:70`) — `e4962fc` sağlık denetiminde düşürülmüş | `Inc/bq25798.h:70` |
| E-2 | §6 "**200 ms** poll" (3 yerde) | **250 ms** (`POLL_PERIOD_MS`); I-7'de DMA-senkron **249.629 ms** | `main.c:64` · `adc_dma.h:58` |
| E-3 | §6 "`BQ_MISS_LIMIT=5` (**≈1 sn**)" | **1.25 s** (5 × 250 ms) | `main.c:66` |
| E-4 | §6 "WD_RST **200× marj**" | **160×** (40 s / 250 ms) | [HESAP] |
| E-5 | §6 "REC retry arası **`REC_RETRY_MS=5`**, her denemede 5 ms bekle" | **`REC_RETRY_MS` KODDA YOK.** T-15 ile bloklamayan hâle getirildi; poll kadansında (~250 ms), 10 deneme → **~2.5 s** pencere | `i2c_protocol.c:561-600` |
| E-6 | §7 F6 "PROT_VER **0x04**" | **0x06** | `i2c_protocol.h:136` |

> **E-1 ve E-5 ciddidir:** ikisi de bloklanma/zamanlama hesabını yanlış yöne çeker
> (E-1 5× abartır, E-5 var olmayan bir gecikme uydurur). Bu doküman **kod değerlerini**
> kullanır; eski satırlar yukarıda gerekçesiyle bırakıldı.

#### Kapsam dışı (bu turda ÜRETİLMEDİ)
- **Fatih-teslim paketi (PDF + ZIP)** — kullanıcı revizyonu tahkim edecek, **S0** sonucu
  (0x48 onayı) eklendikten sonra üretilecek.
- Kod değişikliği **YOK** — bu tur yalnız dokümandır.

---

### 📝 Revizyon Notu — 2026-08-17 (yalnız düzeltme; tel formatı ve kod DEĞİŞMEDİ)

| # | Ne | Nerede | Neden |
|---|---|---|---|
| 1 | §7 **F6 satırı**: 32K sütunu `0x04 (Rev0.4)` → **`0x06 (Rev0.6)`** (tarihsel zincir satır içinde korundu) | §7 16K→32K fark tablosu | Satır **bayattı**: Rev0.5 (0x05) ve Rev0.6 (0x06) bump'ları tabloya işlenmemişti. §3b (`0x1E` satırı), Rev0.6 revizyon notu ve `i2c_protocol.h:136` **0x06** diyor; ayrıca Rev0.7 turunda **E-6** olarak zaten *bulunmuş ama düzeltilmemişti*. Cihazdan okunan ikili kayıtta da `prot_ver=0x06` teyitli |

> **Etki:** yok — doküman içi tutarsızlık giderildi; **`PROT_VER` = `0x06` değişmedi**,
> üst kart tarafında hiçbir ayrıştırma kodu etkilenmez. **Silinen doğru içerik yok.**

---

### 📝 Revizyon Notu — 2026-08-21 (OVP eşiği + AWD1 durumu; tel formatı ve kod DEĞİŞMEDİ)

**Tetikleyen:** Fatih Özcan'ın 2026-08-20 tarihli `J21.38 / OVP_PV` sorusu. Soruyu
araştırırken **bu dokümanın kendi içinde çeliştiği** görüldü — ve çelişkinin **yanlış
yarısı müşteriye teslim edilmişti**.

| # | Ne | Nerede | Neden |
|---|---|---|---|
| 1 | `ALARM_LIVE` bit6 eşiği: `~26.5V` → **`trip 27.0 V / recover 25.0 V`** | §ALARM bit tablosu (bit 6 `OVP_PV`) | **Bayattı.** 26,5 V eşiği I-7 **öncesi** (legacy `ENABLE_I7_ADC_DMA=0`) yoluna aittir; aktif derleme I-7 yolundadır. Kaynak izi: `Inc/ovp_control.h:45-47` (`OVP_TRIP_CODE=3046`=27,0 V · `OVP_RECOVER_CODE=2820`=25,0 V) · `Inc/feature_config.h:24` |
| 2 | 🔴 *"STM32 AWD1 donanım-OVP yolu BUGÜN ÖLÜDÜR · `HT1=1523` onarımı henüz uygulanmadı"* → **GERİ ÇEKİLDİ**; AWD1 **CANLI**, `HT1=`**`761`** | §6c.3 altındaki REC/`VAC_OVP` netleştirme kutusu | **Doküman kendi içinde çelişiyordu:** §6c.3 tablosu *"AWD1 donanım OVP (27.0 V), ~56 µs, `HT1=761`"* derken aynı dokümanın ~390 satır aşağısı *"AWD1 ÖLÜ"* diyordu. §6c.3 **doğruydu**. AWD1 onarımı 22.07.2026'da (I-7, Commit 5) uygulandı; uygulanan değer `1523` değil **`761`** (ratio 4 / shift 0). `HT1=1523` önerisi **hiç kodlanmadı** |

> **Güvenlik açısından anlamı:** Eski metin, OVP donanım yedeğinin **yalnız BQ25798
> `VAC_OVP`** olduğunu söylüyordu. Doğrusu **iki bağımsız donanım katmanı** vardır:
> STM32 `AWD1` (27,0 V, ~56 µs) **ve** BQ `VAC_OVP` (~26 V POR). Yani koruma eski
> metnin ima ettiğinden **daha güçlüdür** — hüküm eksik yöndeydi, tehlikeli yönde değil.
>
> **Etki:** **tel formatı DEĞİŞMEDİ**, `PROT_VER` = `0x06` duruyor, üst kartta hiçbir
> ayrıştırma kodu etkilenmez. Firmware **derlenmedi/değişmedi** — bu tur yalnız belge
> düzeltmesidir. **Silinen doğru içerik yok** (geri çekilen hükümler üstü çizili bırakıldı).
>
> ⚠️ **Dağıtım notu:** Bu iki hata, **01.08.2026 ve 17.08.2026 gönderimlerindeki
> kopyalarda da vardır.** Paylaşılan sürücüdeki kopyalar bu revizyonla güncellendi ve
> durum Fatih Bey'e 2026-08-21 yanıtında bildirildi.

---

### 📝 Revizyon Notu — **Rev0.9** (2026-08-28) — PROTOKOL **PROT_VER 0x07 → 0x08**: `0x4B BQ_YAS_DS` + `0x4C BQ_ERR_N`

**Kullanıcı kararı:** *"BQ25'ten veri okunmadan PC'ye veri gitmesin — tabii bu I²C
içinde de geçerli. Ham veriler bizi yanıltır."* + *"Son BQ25 verisi defalarca
yenilenmeden gönderilmesin, I²C ve UART üzerinden. BQ25 down veya bilerek yapıldı
ise BQ25'in DOWN bilgisi gitsin protokollerde."* Üç seçenek sunuldu; **(b) İŞARETLE**
seçildi — alanlar gitmeye devam eder, **bayat damgasıyla**.

**Ne değişti:** rezerve olan `0x4B` ve `0x4C` tahsis edildi.

| reg | ad | anlam |
|---|---|---|
| `0x4B` | **BQ_YAS_DS** | **Tek bayt** hem DOWN'ı hem yaşı taşır: `0` = taze · `1..255` = bayat (son **başarılı** poll'dan geçen süre, 0,1 s, doygun 255 = ≥25,5 s) |
| `0x4C` | **BQ_ERR_N** | Başarısız BQ okuma sayacı — **mod-256 sarar**; üst kart **ardışık okumaların farkını** alır, mutlak değer anlamlı değildir |

**Neden iki ayrı alan:** yaş her **başarılı** poll'da sıfırlanır; sahada en olası
arıza kipi olan **aralıklı NACK** (*"4 başarısız + 1 başarılı"* = %80 hata oranı)
yaşta **hiç görünmez**. Kaynak: `docs/DENETIM_FW_2026-08.md` K-6 (216-218).

**Aynı gün denetimden gelen düzeltmeler** (sunulmadan önce kapatıldı):

| bulgu | neydi | ne yapıldı |
|---|---|---|
| Soğuk açılış | `last_tick` BSS'te 0 → yaş **boot'tan** ölçülüyordu; t=740 ms'te *"0,7 s bayat"* (ölçüldü). Projenin kendi **F-20** kuralının ihlali: `t == 0` sentinel yasak | Ayrı `ilk_ok` bayrağı; hiç başarılı poll yoksa **255** |
| ADC_EN düşmesi | BQ ADC durunca alanlar **donuyor** ama I²C sağlıklı → yaş `0` = *"taze"* diyordu (tezgahta yaşandı, F9) | `adc_donuk` bayrağı → **255** |
| Yutulan okumalar | `vindpm_r` ve `reg12/13` başarısızlığı **sayılmıyordu** — tam da hedeflenen aralıklı NACK rejiminde sayaç kör | Hata sayacına işleniyor (yaş düşürülmüyor: blok okumaları taze) |
| Sayaç biçimi | Kümülatif + doygun: 4 Hz poll'da tam kopma **63,75 s**'te 255'e ulaşıp sonsuza dek donuyordu; ayrıca DIAG'daki üç komşusu (`d_poll_lost`/`d_rpl_stale`/`d_tx_drop`) **saniyelik delta** — aynı satırda dördüncü sayacın kümülatif olması yanlış yorumlanırdı | UART'ta **saniyelik delta** (`d_bq_err`), I²C'de **mod-256** |
| Sınav boşluğu | Yaş hesabını **hiçbir sınav çalıştırmıyordu**; üç mutasyon sağ kalıyordu | Saf çekirdek `Inc/bq_yas.h`'e alındı, host testinde **11 ölçüt**; üç mutasyonun üçü de artık yakalanıyor |

⚠️ **"BQ down" ile "poll bilerek durduruldu" ayrımı:** kart FAULT fazında tam poll'u
**durdurur** (yalnız WD_RST) → yaş büyür. Ayrım `0x00 SYS_STATE` faz maskesindedir:
`yaş>0 ∧ faz=FAULT` ⇒ **kasıtlı duruş** · `yaş>0 ∧ faz≠FAULT` ⇒ **BQ down**.

**Blok boyu DEĞİŞMEDİ** (`0x60`); `BLK_XSUM` (`0x5F` = `XOR(0x00..0x5E)^0x5A`) yeni
baytları **zaten kapsar** — üst kartın XSUM algoritması bozulmaz, yalnız değeri değişir.

⚠️ **Üst kart tarafı EKSİK (ROADMAP K17-d):** `docs/upper_board_reference/`
içindeki `PWR_I2C_PROT_VER` `0x08`'e çekildi (aksi halde referans ayrıştırıcı **her
çerçeveyi reddederdi**), ama `PwrTelemetry_t` iki yeni alanı **taşımıyor** ve
`PwrI2c_ParseTelemetry()` onları **okumuyor**. DOWN bilgisi tel üzerinde var,
referans tüketici henüz kullanmıyor.

### Aynı sürümde ikinci değişiklik — **G-1: `0x1A` ve `0x63` İŞARETLİ**

SoC modeli 2026-08-27'de **işaretli** yapıldı (kart mutlak SoC iddia etmez;
açılışta 0, negatif olabilir). Ama `0x1A` sözleşmede `u16` idi ve **kapalı
döngü sahte %100 üretiyordu**:

```
kart -%1,0 bildirir      -> tel: 0xFFF6
üst kart u16 okur        -> 65526
restore bloğu kırpar     -> 1000
kart reset olur, restore -> +%100,0   ← SAHTE
```

⚠️ **Denetim uyarısı (2026-08-28):** ilk düzeltme **yalnız telemetri ayağına**
dokundu ve tel üzerinde hiçbir şeyi değiştirmedi (bayt akışı zaten ikiye
tümleyendi, eklenen kelepçe erişilemezdi). Turun asıl halkası **restore**
ayağıydı ve açıktı. Şimdi dört halkanın **dördü** de işaretli:
`Src/i2c_protocol.c` telemetri + restore · `docs/upper_board_reference/`
okuma + restore kurulumu.

**Bu, `PROT_VER 0x08` bump'ına bindirildi** — ayrı bir `0x09` dağıtımı
gerekmedi (bump henüz sahaya çıkmamıştı).

**Kod:** `Inc/i2c_protocol.h` · `Src/i2c_protocol.c` · `Inc/bq_yas.h` (saf yaş
hesabı) · `Inc/bq25798_poll.h` · `Src/bq25798_poll.c` · `Src/diag_telem.c` ·
`tests/binlog_host_test.c`. **UART karşılığı:** DIAG `114 bq_yas_ds` / `115 d_bq_err`,
`BINLOG_PKT_VER 6→7`, `schema_sum 0x7A55→0x7D33`.

---

### 📝 Revizyon Notu — **Rev0.8** (2026-08-27) — PROTOKOL **PROT_VER 0x06 → 0x07**: `0x4A CHG_REAL`

> ⚠️ **BU REVİZYONDA TEL FORMATI DEĞİŞTİ.** Rev0.7 ve öncesindeki üç revizyon notu
> (2026-08-01 / 08-17 / 08-21) yalnız belge düzeltmesiydi; **bu değil.**

**Kod:** `Inc/i2c_protocol.h:139` (`I2CPROT_REG_CHG_REAL`), `:149` (`PROT_VER_VAL 0x07`) ·
`Src/i2c_protocol.c:390` (`s_tlm[0x4A] = g_bq_poll.chg_real`) ·
`Src/bq25798_poll.c:255-284` (türetim) · `Inc/bq25798_poll.h:38-48` (eşikler).
Commit: `bfb43b8` (2026-08-26) + belge/sözleşme senkronu (2026-08-27).

| # | Ne | Nerede | Neden |
|---|---|---|---|
| 1 | **`0x4A` rezerve → `CHG_REAL` (u8)** | §3b register tablosu · §6d rezerve tablosu | Çip kendi **faz iddiasıyla** kendi **akım ölçümünü** çelişkiye düşürebiliyor; üst kart ham bayrağa bakıp **yanlış bilgileniyordu** |
| 2 | **PROT_VER 0x06 → 0x07** | 0x1E + §PROT_VER (4 yer) + başlık kutusu | Rezerve bir bayt doldu → **`BLK_XSUM` (0x5F) değeri değişti** → karşı tarafın sürüm denetimi tetiklenmeli |
| 3 | Doküman revizyonu Rev0.7 → **Rev0.8** | başlık kutusu | Tel formatı değiştiği için ikisi birlikte arttı |
| 4 | Referans kod: **`PROT_VER` denetimi EKLENDİ** | `docs/upper_board_reference/pwr_i2c_packets.c:88-92` | 🔴 **Rev0.6 referansında `PWR_I2C_PROT_VER` tanımlıydı ama `PwrI2c_ParseTelemetry()` onu HİÇ KULLANMIYORDU** — yalnız XSUM denetleniyordu. Tek sözleşme kuralı kodda karşılıksızdı |
| 5 | Bayat satır atıfları düzeltildi | §6 Rev0.7 düzeltme tablosu · §7 F6 | `Inc/i2c_protocol.h:136` artık PROT_VER değil; doğrusu **`:149`** |

**Ölçülen gerekçe (uydurma değil):** 2026-08-26, 11:16:50–14:34:53 (**3,30 saat**),
`bench/2026-08-25/kayit_gece/run_20260825_211256.bin` segment 1 —
`chg_stat = FAST_CC(3)` sabitken `ibat` ortalaması **0,0 mA**; **11.924 örneğin
yalnız 50'sinde** `ibat ≥ 10 mA` (**%99,58**, payda tüm pencere), tepe **62 mA**.

⭐ **Tasarım kararı — ham bayrak KORUNDU (Seçenek A, kullanıcı kararı 2026-08-26):**
`0x28 CHG_STAT` **değiştirilmedi** ve otoriter kalır; türetilmiş bayrak **yanına**
kondu. Üst kart **ikisini de görür ve kararı kendisi verir**. Reddedilen seçenekler:
**B** = ham bayrağı düzelt (çipin gerçeğini gizlerdi) · **C** = hiç değiştirme
(üst kart yanlış bilgilenmeye devam ederdi).

⛔ **TEK SÖZLEŞME — GEÇMİŞE-UYUM KATMANI YOK** (kullanıcı kararı 2026-08-27).
Aynı kural `0x04` (Rev0.4) ve `0x06` (Rev0.6) geçişlerinde de uygulandı.
`i2c_protocol.h`'te bir ara *"üst kart eski sürümle de çalışır"* yorumu vardı —
politikayla çeliştiği için **kaldırıldı**.

**Üst kart tarafında gereken (tam liste):**
1. `PWR_I2C_PROT_VER` sabiti `0x06` → **`0x07`**
2. `PwrI2c_ParseTelemetry()` içinde **sürüm denetimi** (referans `.c`'ye eklendi)
3. `0x4A CHG_REAL` okunması — *isteğe bağlı ama önerilir*; okunmazsa yalnız çelişki görülemez
4. `0x00-0x49` ve `0x50-0x5F` **bayt-bayt AYNI** — başka hiçbir ayrıştırma değişikliği yok

**Silinen doğru içerik yok** (tarihsel sürüm zinciri korundu).


### 📝 Revizyon Notu — 2026-08-29 (yalnız tutarlılık + KİLİT; tel formatı ve kod DEĞİŞMEDİ)

- **PROT_VER 0x08 KİLİTLENDİ** (kullanıcı kararı) — üst kart entegrasyonu sınanana kadar bump yok; üstteki kutu.
- Belge içi **iki sürüm şeması** bir aradaydı: tablo satırları `0x4A` "Rev0.7 YENİ", `0x4B/0x4C` "Rev0.8 YENİ",
  `0x1E` "32K Rev0.8: 0x08" derken revizyon notları **Rev0.8 = 0x07, Rev0.9 = 0x08** diyordu (DEVLOG 2026-08-27 de öyle).
  Notlar otoriter alındı; satırlar 33 · 345 · 377 · 378 · 379 · 484 · 838 tek şemaya çekildi.
- `0x4C BQ_ERR_N` üst kutuda ve tabloda **"doygun 255"** yazıyordu; kod `err_tot & 0xFF` → **mod-256 sarar**
  (Rev0.9 notu zaten doğruydu). İki yer düzeltildi.
- Firmware başlık yorumları (`Inc/i2c_protocol.h` `:72`, `:176`) aynı şemaya çekildi.