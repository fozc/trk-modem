# PowerBoard — I²C **Slave (0x48)** Entegrasyon Rehberi

### Üst kart slave yazılımı için I²C haberleşme sözleşmesi

| Alan | Değer |
|---|---|
| **Doküman** | PowerBoard I²C Slave Entegrasyon Rehberi |
| **Ürün** | Smart_Sectionalizer — Power Board (16K firmware) |
| **Doküman No** | BLTX-PWRB-I2C-SLV-16K |
| **Revizyon** | **R0.00** |
| **Yayın Tarihi** | 2026-07-05 |
| **Hazırlayan** | **Yaşar BOLAT** |
| **Firma** | **BOLATeX** · bolatex.com.tr |
| **Protokol** | PROT_VER 0x03 · Slave adres 0x48 (7-bit) · 400 kHz Fast Mode · MSB-first |
| **Durum** | Yayınlandı |

> **Amaç ve kapsam:** Bu doküman, **üst kart tarafında I²C slave (0x48) yazılımını** geliştirecek ekip
> içindir. PowerBoard'un iç firmware'ini bilmeye gerek yoktur; buradaki haberleşme sözleşmesi + davranış
> modeli, slave'i **baştan sona kurmaya yeterlidir.** Doküman, uygulanacak I²C hattının **slave tarafını**
> (adres, hız, byte-order, register-pointer modeli, checksum, veri blokları ve zamanlama) bağlayıcı biçimde
> tanımlar.

**İşlemci bağımsızlığı:** Bu doküman **belirli bir slave işlemcisine bağlı değildir.** Slave tarafında şu an
bir STM32 kullanılıyor olsa da ileride farklı bir MCU'ya geçilebilir. Bu nedenle burada **hiçbir üreticiye
özgü kütüphane/HAL/araç** varsayılmaz; yalnızca **I²C sözleşmesi ve slave davranış gereksinimleri** (adres,
hız, byte-order, register-pointer modeli, checksum, bloklar) tanımlanır. Bunları hangi işlemci/kütüphane ile
gerçeklediğiniz size aittir; verilen kod parçaları **dilden bağımsız mantığın C ile örneğidir.**

---

## Rol ve mimari

Power Board bu hatta **I²C MASTER**'dır: üst karta (**slave 0x48**) telemetri **push** eder ve boot/periyodik
olarak üst karttan **restore / ACK / config** okur. Üst kart **her zaman slave**'dir — bus'ı sürmez, master
olmaz. "Power Board'dan veri çekmek", pratikte **slave tarafında iki işlemi doğru karşılamaktır.**

### Sistem topolojisi

```
         güç girişi  (PV 25W  /  DC)
                 |
                 v
         BQ25798RQMR  --  şarj IC, I2C slave 0x6B
                 |
                 |  (dahili I2C hattı, Power Board içi)
                 v
         STM32C011F4P6  --  [ I2C MASTER ]
                 |
                 |  I2C   400 kHz  Fast Mode
                 v
         TCA9416  --  köprü (pass-gate, izole DEĞİL)
                 |
                 |  B-yan:  J21 pin9 = SDA  /  pin10 = SCL   (3V3)
                 v
         ÜST KART  --  [ I2C SLAVE  0x48 ]   <== bu doküman
                       (işlemci bağımsız; şu an STM32, değişebilir)
```

> **TCA9416 = pass-gate köprü** (izole repeater değil): B-tarafında SDA uzun süre LOW tutulursa **tüm bus**
> (BQ tarafı dâhil) kilitlenir. Power Board bunu 9×SCL bus-recovery ile ~1 s'de kurtarır. **Slave SDA'yı
> uzun süre LOW tutmamalıdır.**

### Veri akışı

```
   POWER BOARD (MASTER)                          ÜST KART (SLAVE 0x48)
   --------------------                          ---------------------

   A) TELEMETRİ PUSH   (~1 s periyot)
      S 0x90 0x00 D0..D95 P      ===========>    reg[0x00..0x5F]  (96 bayt)
                                                   `- BLK_XSUM doğrula

   B) PULL / OKUMA     (boot + periyodik)
      S 0x90 ptr  Sr 0x91 <N B> P  =========>    reg[0x60..0x74]  (slave üretir)
                                                   |- 0x60  RESTORE  (boot'ta 1 kez)
                                                   |- 0x72  REC_ACK  (SEQ echo)
                                                   `- 0x73 / 0x74  CONFIG
```

Yalnızca bu **iki işlem tipi** vardır; slave yazılımının tamamı bu ikisini karşılamaktan ibarettir.

---

## 0. Slave'in karşılaması gereken I²C kriterleri (işlemci-bağımsız)

Aşağıdakiler **bağlayıcı sözleşmedir**; slave hangi MCU ile yapılırsa yapılsın bunları sağlamalıdır:

| # | Kriter | Değer / kural |
|---|--------|---------------|
| K1 | **Slave adres** | 7-bit **`0x48`**; bu adrese **ACK** verilir (8-bit: write `0x90`, read `0x91`) |
| K2 | **Hız** | **400 kHz** Fast Mode'a dayanmalı (SCL'i master sürer; gerçek ~370–390 kHz) |
| K3 | **Byte order** | Tüm çoklu-bayt alanlar **MSB-first (big-endian)** |
| K4 | **Clock stretching** | İzinli, ama **tek işlem ≤ 10 ms** sürmeli (aşarsa master iptal eder) |
| K5 | **Register-pointer** | Her işlemin **ilk baytı = register adresi (pointer)**, veri değil |
| K6 | **Otomatik artış** | Pointer sonrası her bayt için adres **otomatik +1** (hem write hem read) |
| K7 | **Write (master→slave)** | pointer `0x00` + 96 bayt = telemetri; reg-map'e yazılır |
| K8 | **Read (slave→master)** | pointer (0x60/0x72/0x73/0x74) + repeated-start; slave o adresten baytları sunar |
| K9 | **Bus nezaketi** | SDA uzun süre LOW tutulmaz (pass-gate bus kilidi riski) |
| K10 | **Bilinmeyen adres** | Tanımsız pointer'a erişimde bus askıda bırakılmaz (0 dön ya da NACK) |

> **K5 kritik:** Register-pointer'ı ayrı ele almazsanız **tüm veri 1 bayt kayar.** `S`/`Sr`/`P` ve `0x90/0x91`
> I²C standardıdır (donanım üretir); sizin ilgilendiğiniz `ptr` ve veri baytlarıdır.

---

## 1. İki işlem tipi

| # | İşlem | Bus dizisi | Slave görevi |
|---|-------|-----------|--------------|
| **A** | **Telemetri PUSH** (master write) | `S · 0x90 · 0x00 · D0..D95 · P` | 96 baytı `0x00` tabanlı reg-map'e yaz, XSUM doğrula |
| **B** | **PULL / okuma** (master read) | `S · 0x90 · ptr · Sr · 0x91 · <N bayt> · P` | `ptr`'den başlayan baytları sun (restore / ACK / config) |

---

## 2. Slave veri modeli: register-file

Her iki işlem de tek bir **256 baytlık register dosyası** üzerinden temiz biçimde ele alınır. Master'ın
"pointer + otomatik artış" davranışı slave'de bir dizi + indeks ile taklit edilir:

```
   reg[256]  --  slave RAM (tek dizi, iki bölge)

   0x00 .. 0x5F   telemetri       <== MASTER WRITE   (siz saklarsınız)
   0x1F           BLK_XSUM         <== (siz doğrularsınız)
   0x60 .. 0x71   RESTORE (18 B)   ==> MASTER READ    (SİZ üretirsiniz, boot'ta 1 kez)
   0x72           REC_ACK          ==> MASTER READ    (SEQ echo)
   0x73           CFG_GEN          ==> MASTER READ
   0x74           CFG_CAP_AH       ==> MASTER READ    (7..50 Ah)
```

- **0x00–0x5F**: master yazar → slave saklar (telemetri).
- **0x60–0x74**: master okur → slave **önceden üretip** koyar.
- İki bölge farklı pointer'la geldiği için çakışmaz; tek `reg[256]` ikisini de taşır.

---

## 3. Slave davranış modeli (olay-güdümlü, işlemci-bağımsız)

Aşağıda slave'in karşılaması gereken davranış, **soyut olaylar** cinsinden verilmiştir. Bu olayları hangi
çevrebirim/kesme/DMA mekanizmasıyla yakaladığınız işlemcinize bağlıdır; mantık aynıdır.

```
   durum: reg_ptr, ilk_bayt_bekleniyor

   OLAY  adres_eşleşti(yön):
       eğer yön == YAZMA (master yazıyor):        # telemetri / pointer
           ilk_bayt_bekleniyor = doğru
       eğer yön == OKUMA (master okuyor):          # pull
           # reg_ptr, önceki yazma-pointer aşamasında set edildi
           çıkışa hazırla: reg[reg_ptr], reg[reg_ptr+1], ...   (otomatik artış)

   OLAY  bayt_alındı(b):                            # master yazdı
       eğer ilk_bayt_bekleniyor:
           reg_ptr = b                              # K5: ilk bayt = pointer
           ilk_bayt_bekleniyor = yanlış
       değilse:
           reg[reg_ptr] = b;  reg_ptr = reg_ptr + 1 # K6: otomatik artış

   OLAY  bayt_isteniyor() -> b:                     # master okuyor (SCL sürüyor)
       b = reg[reg_ptr];  reg_ptr = reg_ptr + 1
       döndür b

   OLAY  dur (STOP):
       eğer son işlem YAZMA ve başladığı pointer == 0x00:
           telemetri_servis()                       # XSUM doğrula + REC bak (Bölüm 5,9)
       tekrar dinlemeye dön
```

**Uygulama notları (işlemciden bağımsız ilkeler):**
1. Adres eşleşince **yönü** öğrenin: master yazıyor mu (telemetri/pointer) yoksa okuyor mu (pull).
2. Yazmada **ilk baytı pointer** olarak ayırın; kalan baytları pointer'dan itibaren reg-file'a koyun.
3. Okumada `reg_ptr`'den başlayıp **otomatik artışla** bayt sunun.
4. Checksum doğrulama / restore üretimi gibi **ağır işi olay/kesme bağlamında yapmayın**; bir bayrak set edip
   ana döngüde işleyin (K4: işlem ≤10 ms).
5. STOP sonrası **tekrar dinler duruma** dönün (bir sonraki işlemi kaçırmayın).

---

## 4. Telemetri register haritası (0x00–0x5F, master → slave)

Master `0x00`'dan **96 bayt** yazar (⚠ çoklu-bayt = MSB-first, K3):

| Ofset | Alan | Tip | Açıklama |
|---|---|---|---|
| 0x00 | SYS_STATE | u8 | faz[2:0] · FAULT(b6) · ANY(b7) — §12.1 |
| 0x01 | SYS_FAULT | u8 | BQ FAULT0 (REG20) aynası |
| 0x02 | SYS_FLAGS | u8 | Canlı alarm bitleri — §5.3 |
| 0x03 | CHG_STAT_RAW | u8 | YALNIZ şarj fazı bit[7:5]; VBUS_STAT/BC1.2 iletilmez (0) |
| 0x04 | SOH_X10 | u16 | Akü SoH ×10 (restore'dan seed; 16K hesaplamaz) |
| 0x10 | VBAT_MV | u16 | BQ VBAT (mV) |
| 0x12 | VPV_MV | u16 | PV gerilimi — host ADC (mV) |
| 0x14 | VDC_MV | u16 | DC gerilimi — host ADC (mV) |
| 0x1A | SOC_X10 | u16 | SoC ×10 (16K: VBAT→OCV; şarjda ~%100'e yapışır) |
| 0x1C | REC_FLAG | u8 | b0=ALARM · b1=CHECKPOINT — §9 |
| 0x1D | SEQ | u8 | Örnek sayacı 0–255, sarar |
| 0x1E | PROT_VER | u8 | **0x03** (0x02 VE 0x03'ü kabul edin) |
| 0x1F | BLK_XSUM | u8 | Telemetri XOR sağlaması — §5.1 |
| 0x20 | ICHG_MA | u16 | Uygulanan şarj akımı (mA) |
| 0x22 | BOARD_TEMP | i8 | Kart NTC °C (-128 = hata) |
| 0x23 | BATT_TS | u8 | Akü JEITA 0–4 — §12.4 |
| 0x24 | BATT_TEMP | i16 | Akü NTC ×10 °C; **-9990 = geçersiz/NTC yok** |
| 0x26 | BQ_FAULT0 | u8 | BQ REG20 ham — §12.11 |
| 0x27 | BQ_FAULT1 | u8 | BQ REG21 ham — §12.12 |
| 0x28 | CHG_STAT | u8 | Şarj fazı 0–7 — §12.5 |
| 0x2C | BATT_CAP_AH | u8 | Aktif kapasite (Ah) echo (CFG_CAP_AH yansıması) |
| 0x30 | ALARM_LIVE | u8 | Anlık alarm bitleri |
| 0x31 | ALARM_LATCH | u8 | Yapışkan (latched) alarm bitleri |
| 0x32 | CHG_PHASE | u8 | 0–7 şarj fazı (= 0x28) |
| 0x40–0x43 | BQ_REG1B/1D/1E/1F | u8×4 | BQ ham status — §12.6–12.9 |
| 0x44 | PWR_IO | u8 | GPIO bitmask + **polarite** — §5.2, §12.10 |
| 0x45 | HIZ_TRIG | u8 | EN_HIZ bekçi-tetik sayacı (doygun 255) |
| 0x46 | MPPT_TRIG | u8 | MPPT bekçi-tetik sayacı |
| 0x47 | IINDPM_TRIG | u8 | IINDPM bekçi-tetik sayacı |
| 0x50 | BQ_VSYS_MV | u16 | BQ VSYS |
| 0x52 | BQ_VBUS_MV | u16 | BQ VBUS |
| 0x54 | STM_VBAT_MV | u16 | VBAT — host ADC (mV) |
| 0x56 | IBAT_MA | i16 | BQ IBAT (şarj +/deşarj −) |
| 0x58 | IBUS_MA | u16 | BQ IBUS |
| 0x5A | BQ_VAC1_MV | u16 | BQ VAC1 = **DC girişi** |
| 0x5C | BQ_VAC2_MV | u16 | BQ VAC2 = **PV girişi** |
| 0x5E | BQ_TDIE_C | i8 | BQ çip sıcaklığı °C |

**16K'dan DAİMA 0 gelen alanlar** (32K-planlı; "0 = veri yok" işleyin):
`0x06, 0x08, 0x0C, 0x16, 0x18, 0x29, 0x2A, 0x2B, 0x2D, 0x2E, 0x33, 0x37, 0x3B, 0x3F` + `0x48–0x4F`/`0x5F`.

> **0x03:** firmware buraya YALNIZ şarj fazını (bit[7:5]) yazar; VBUS_STAT[4:1]+BC1.2[0] daima 0 —
> giriş-kaynak tipi 0x03'ten çıkarılamaz.

### Çoklu-bayt okuma (MSB-first) — dilden bağımsız mantık, C örneği
```c
uint16_t rd_u16(const uint8_t *r, uint8_t off) {
    return (uint16_t)((r[off] << 8) | r[off + 1]);          /* MSB-first (K3) */
}
int16_t  rd_i16(const uint8_t *r, uint8_t off) {
    return (int16_t)rd_u16(r, off);
}
/* Örnek: SoH% = rd_u16(reg, 0x04)/10.0 ; VBAT_mV = rd_u16(reg, 0x10) ; IBAT = rd_i16(reg, 0x56) */
```

---

## 5. Doğrulama, alarm ve polarite

### 5.1 Telemetri checksum — BLK_XSUM (0x1F)
`BLK_XSUM` = `0x00`–`0x5F` arasındaki **95 baytın XOR'u, YALNIZ 0x1F hariç.** Tutmuyorsa örneği işaretleyin/atın.

```c
uint8_t tlm_xsum_ok(const uint8_t *r) {
    uint8_t x = 0;
    for (uint8_t i = 0x00; i <= 0x5F; i++)
        if (i != 0x1F) x ^= r[i];
    return (x == r[0x1F]);
}
```

### 5.2 PWR_IO (0x44) — POLARİTE (ters kurmayın)
PWR_IO **ham fiziksel pin aynasıdır** (bit=1 → pin HIGH):
`b0=CE(aktif-LOW: 0=şarj) · b1=OVP2HIZ(1=Hi-Z normal, 0=OVP) · b2=OVP_PV(aktif-LOW) ·
b3=VBAT_DIS(1=akü kesildi) · b4=HEAT · b5=QON`

| Sinyal | PWR_IO b2 (ham pin) | ALARM byte b6 (mantıksal) |
|---|---|---|
| **OVP_PV** | `b2=1 → NORMAL` · `b2=0 → ALARM` (aktif-LOW) | `b6=1 → ALARM` (**OTORİTER**) |

**➜ OVP kararı HER ZAMAN ALARM byte b6'dan verilir (1=alarm), PWR_IO b2'den değil.**

### 5.3 Alarm bitleri (SYS_FLAGS 0x02 = ALARM_LIVE 0x30; ALARM_LATCH 0x31 yapışkan)
`b7=ANY · b6=OVP_PV · b5=NTC_HOT(≥70°C) · b4=NTC_COLD(<0°C) · b3=VBAT_LOW(<10V) ·
b2=VBAT_HIGH(>14.6V) · b1=VDC_LOW(info) · b0=VPV_LOW(info)`.
`ANY` yalnız gerçek alarmlardan beslenir; **VDC_LOW/VPV_LOW ANY'ye girmez.**

---

## 6. Slave'in ÜRETECEĞİ okuma blokları (0x60–0x74, master → slave'den okur)

| Ofset | Alan | Uzunluk | Slave ne sunar |
|---|---|---|---|
| **0x60–0x71** | Restore bloğu | 18 B | Boot'ta 1 kez okunur. marker `0xA5` + SoH + **kendi XSUM'u** — §7 |
| **0x72** | REC_ACK | 1 B | En son kalıcı-kaydedilen SEQ echo'su — §9 |
| **0x73** | CFG_GEN | 1 B | Config generation nabzı (değiştikçe artırılan sayaç) |
| **0x74** | CFG_CAP_AH | 1 B | Akü kapasitesi **7–50 Ah** (aralık dışı → 12 varsayılır) — §8 |

Bu bölge boot'tan **önce** ve değiştikçe hazır tutulur; master okuduğunda anında hazır olmalı.

---

## 7. RESTORE bloğu (0x60–0x71, 18 B) — üretim + checksum

Power Board **yalnızca boot'ta bir kez** bu bloğu okur ve SoH'yi buradan **seed** eder (16K SoH'yi kendisi
hesaplamaz; slave'in sakladığı son değeri geri verir).

**Byte-map:**

| Ofset | Alan | Uzunluk | 16K kullanır mı? |
|---|---|---|---|
| 0x60 | MARKER = `0xA5` | 1 B | ✅ geçerlilik |
| 0x61 | SoH ×10 (MSB-first) | 2 B | ✅ **okunur** |
| 0x63 | SoC | 2 B | ⬜ 16K yok sayar (32K kullanır) |
| 0x65 | EQUIV_H (equivalent full cycles) | 4 B | ⬜ |
| 0x69 | GROSS_mAh | 4 B | ⬜ |
| 0x6D | TOTAL_mWh | 4 B | ⬜ |
| 0x71 | XSUM | 1 B | ✅ geçerlilik |

**Geçerlilik (Power Board tarafı):** `marker == 0xA5` **VE** XSUM tutmalı; aksi halde restore reddedilir →
SoH **default %100**.

> ⚠ **Restore XSUM span'i telemetriden FARKLIDIR:** XSUM = **0x60..0x70 (17 bayt, MARKER DAHİL)** XOR'u,
> sonuç 0x71'e yazılır. 16K, 0x63–0x70'i okumasa da bu baytlar checksum'a **dahildir** — 0 bıraksanız bile
> XSUM'u 17 baytın tamamı üzerinden hesaplayın, yoksa restore reddedilir.

```c
/* Restore bloğunu reg[0x60..0x71] içine üret (boot'tan ÖNCE hazır olsun) */
void build_restore(uint8_t *reg, uint16_t soh_x10 /*, 32K alanları */) {
    reg[0x60] = 0xA5;                          /* MARKER */
    reg[0x61] = (uint8_t)(soh_x10 >> 8);       /* SoH MSB */
    reg[0x62] = (uint8_t)(soh_x10 & 0xFF);     /* SoH LSB */
    for (uint8_t a = 0x63; a <= 0x70; a++) reg[a] = 0x00;   /* 16K: 0 · 32K: gerçek değerler */

    uint8_t x = 0;
    for (uint8_t a = 0x60; a <= 0x70; a++) x ^= reg[a];     /* 17 bayt, MARKER dâhil */
    reg[0x71] = x;                                          /* XSUM */
}
```

> **KRİTİK — restore tekrar edilmez:** Power Board restore'u **yalnız boot'ta** okur. O okuma kaçırılır
> (veya o an NACK verilir) ise, SoH bir sonraki Power Board **reset'ine kadar** default %100 kalır —
> BENCH→UPLINK geçişi bile restore'u yeniden tetiklemez. **Restore bloğu boot'tan önce hazır tutulmalı ve
> 0x60 okumasına anında doğru yanıt verilmelidir.**
>
> **Boot penceresi (entegrasyon riski):** İki kart **aynı güç hattından birlikte** açılıyorsa, Power Board
> boot'unu bitirip 0x48'i ilk yokladığında üst kartın **restore bloğunu çoktan hazır** etmiş olması gerekir.
> Üst kart daha yavaş açılıyorsa bu ilk okumayı kaçırır. **Öneri:** SoH'yi kalıcı bellekte (NVM/flash)
> saklayın ve slave I²C'yi, restore bloğu (marker+SoH+XSUM) **RAM'de hazır olduktan sonra** aktifleştirin;
> böylece 0x48 ACK verdiği anda blok da doğru olur. (Power Board yeniden yoklama yapar: boot, boot+2 s,
> sonra her 10 s — ancak **restore** yalnız ilk başarılı boot okumasında alınır.)

---

## 8. CFG_CAP_AH (0x74) ve echo (0x2C)

- `0x74` = akü kapasitesi (Ah); master okur. **7–50 Ah** aralığında sunun (aralık dışı → 12 Ah varsayılır).
  Kapasite → şarj akımı (ICHG = cap×1000/10 mA, C/10).
- Kapasite telemetride `0x2C` (BATT_CAP_AH) olarak **echo** döner: 16K'da aynen; 32K'da preset'e
  yuvarlanmış dönebilir → hata saymayın.
- `0x73` (CFG_GEN): config değiştikçe artırın; Power Board ~8 s'de bir poll eder, değişince tam config'i okur.

---

## 9. REC — kayıt onayı (REC_ACK handshake)

`REC_FLAG (0x1C) ≠ 0` gelen telemetri, Power Board'un **kalıcı-kayıt** (persist) istediğini gösterir
(b0=ALARM yeni alarm, b1=CHECKPOINT periyodik). Aynı blok **aynı SEQ** ile, `REC_ACK (0x72) == gönderilen
SEQ` verilene kadar **≤10 kez** (~5 ms arayla) yeniden gönderilir — pratik tavan **~50 ms** (I²C işlem
süreleriyle ~75–100 ms'e kadar).

```
   POWER BOARD (MASTER)                       ÜST KART (SLAVE 0x48)
   --------------------                       ---------------------
   push:  SEQ=N, REC_FLAG(0x1C)!=0  =======>  bloğu işle + persist et
   read:  0x72 (REC_ACK)            <=======  reg[0x72] = N   (o SEQ'i echo et)
   ACK==N ?  DONE (sessiz)
   değilse retry ≤10 -> başarısız:  E/PROT REC FAIL + LED 6x   (VERİ KAYBI DEĞİL)
```

**Slave görevi:** REC_FLAG≠0 görünce bloğu kalıcılaştırın, **sonra** `reg[0x72]`'ye o SEQ'i yazın. Başarılı
REC **sessizdir** (UART'ta satır yok; yalnız REC FAIL loglanır).

```c
void service_telemetry(uint8_t *reg) {          /* ana döngüde, olay/kesme dışında (K4) */
    if (!tlm_xsum_ok(reg)) return;              /* bozuk örnek: atla */
    uint8_t seq = reg[0x1D];
    if (reg[0x1C] != 0) {                        /* REC isteniyor */
        persist_save(reg);                       /* olay/SoH kalıcı kaydı (uygulama storage) */
        reg[0x72] = seq;                         /* REC_ACK = SEQ */
    }
    persist_update_soh(rd_u16(reg, 0x04));       /* boot restore'unda geri verebilmek için sakla */
}
```

---

## 10. Master davranış zarfı (slave'in bekleyeceği)

| Konu | Power Board davranışı |
|---|---|
| Telemetri periyodu | reg 0x00'dan 96B write, nominal ~1000 ms (BQ poll + REC jitter'i olabilir) |
| NACK'a tepki (telemetri) | fire-and-forget: NACK'te loglar, sonraki 1 s turunu bekler (üst üste dövmez) |
| Boot okuması | Restore(0x60)+Config(0x73) **bir kez** okunur |
| Mod | `0x48 ACK → UPLINK` · `NACK → BENCH`. Re-probe: boot+2 s, sonra her 10 s |
| İlk paket (BENCH→UPLINK) | **Tam-formlu**: SEQ artmış, XSUM geçerli, tüm alanlar dolu |
| UPLINK→BENCH | **Otomatik düşüş YOK** (thrash önlemi); yalnız Power Board reset'inde BENCH'e döner |

---

## 11. Savunmacı kodlama — 16K ↔ 32K tek-kod

Blok yapısı/offset/XSUM/polarite **iki kartta birebir aynıdır** → tek kod yeter. Yalnız:

| Konu | 16K | 32K | ➜ Nasıl kodlayın |
|---|---|---|---|
| **PROT_VER (0x1E)** | `0x03` | `0x02` veya `0x03` | **0x02 VE 0x03'ü KABUL EDİN** — sert-red yok |
| **0-gelen alanlar** | HEP 0 | DOLU | "0 olmalı" doğrulaması yazmayın; `0` = "veri yok" |
| **SoC (0x1A)** | OCV-yaklaşık (~%100'e yapışır) | coulomb (gerçek) | 16K'da şarjda %100 tuhaf değil |
| **CAP echo (0x2C)** | yazılan aynen | preset'e yuvarlanabilir | echo farklı dönebilir → hata saymayın |
| **0x45–47 sayaçlar** | dolu | 0 (port gelene dek) | "0 = anormal" varsaymayın |
| **Restore 0x63–0x70** | 16K okumaz | 32K okur | bloğu TAM doldurun (checksum dâhil) — tek-kod |

---

## 12. Register bit / enum eki

**12.1 SYS_STATE (0x00):** `b7=ANY · b6=FAULT · b5:3=rezerve · b2:0=PHASE(0–7)`
**12.2 ALARM (0x02=0x30=0x31):** §5.3.
**12.3 REC_FLAG (0x1C):** `b1=CHECKPOINT · b0=ALARM`.
**12.4 BATT_TS (0x23):** `0=NORM(4–45°C) · 1=COLD(<0°C, şarj durur) · 2=COOL(0–4°C) · 3=WARM(45–55°C) · 4=HOT(>55°C, şarj durur)`.
**12.5 CHG faz (0x28=0x32=SYS_STATE[2:0]):** `0=yok · 1=Trickle(<9V) · 2=Pre-charge · 3=Fast CC · 4=Taper CV · 5=rezerv · 6=Top-off · 7=Tamamlandı`.
**12.6 REG1B — Charger Status 0 (0x40):** `b7=IINDPM · b6=VINDPM · b5=WD_STAT · b4=- · b3=PG_STAT · b2=AC2_PRESENT(PV) · b1=AC1_PRESENT(DC) · b0=VBUS_PRESENT`.
**12.7 REG1C (0x03'te yalnız bit[7:5]):** CHG_STAT fazı; VBUS_STAT[4:1]+BC1.2[0] iletilmez (0).
**12.8 REG1D — Charger Status 2 (0x41):** `b7:6=ICO_STAT · b2=TREG_STAT · b1=DPDM_STAT · b0=VBAT_PRESENT`.
**12.9 REG1E — Charger Status 3 (0x42):** `b7=ACRB2 · b6=ACRB1 · b5=ADC_DONE · b4=VSYS_STAT · b3=CHG_TMR · b2=TRICHG_TMR · b1=PRECHG_TMR`.
**12.9b REG1F — TS_STAT (0x43):** `b3=TS_HOT · b2=TS_WARM · b1=TS_COOL · b0=TS_COLD`.
**12.10 PWR_IO (0x44):** §5.2 (polarite).
**12.11 REG20 — FAULT 0 (0x01=0x26):** `b7=IBAT_REG · b6=VBUS_OVP · b5=VBAT_OVP · b4=IBUS_OCP · b3=IBAT_OCP · b2=CONV_OCP · b1=VAC2_OVP(PV) · b0=VAC1_OVP(DC)`.
**12.12 REG21 — FAULT 1 (0x27):** `b7=VSYS_SHORT · b6=VSYS_OVP · b5=OTG_OVP · b4=OTG_UVP · b3=- · b2=TSHUT · b1:0=-`.

---

## 13. Slave hazır-olma listesi

- [ ] I²C çevrebirimi **slave**, own addr **0x48** (7-bit), 400 kHz, clock-stretch açık (K1–K4).
- [ ] Adres eşleşince **yön** ayrılıyor (master yazıyor / okuyor).
- [ ] Write'ta **ilk bayt = pointer** ayrıştırılıyor (K5, 1-bayt kayma yok).
- [ ] Pointer sonrası **otomatik artış** (K6) hem write hem read'de çalışıyor.
- [ ] `0x00` pointer'lı 96B write reg-file'a yazılıyor; **BLK_XSUM doğrulanıyor**.
- [ ] `0x60–0x71` restore bloğu boot'tan **önce** hazır: marker `0xA5` + SoH + **17-bayt XSUM**.
- [ ] `0x72` REC_ACK: REC_FLAG≠0 gelince persist + SEQ echo.
- [ ] `0x73` CFG_GEN + `0x74` CFG_CAP_AH (7–50) sunuluyor.
- [ ] Okuma/işlem **≤10 ms** tamamlanıyor (K4).
- [ ] `PROT_VER` = 0x02 **veya** 0x03 kabul; **0-gelen alanlar** "yok" işleniyor.
- [ ] SDA uzun süre LOW tutulmuyor (K9).

---

## 14. Entegrasyon (joint) test akışı

> ⚠ **ÖN-KOŞUL:** masa (bench) testi onayı gelmeden joint teste başlanmaz.

1. Kartları bağla, Power Board boot → 0x48 ACK verince UART'ta `W/PROT: ... mod: UPLINK`.
2. Restore: boot'tan önce 0x60 bloğunu hazır tut → `Restore OK SoH=..`.
3. Telemetri doğrula: 96B, `PROT_VER=0x03`, SEQ artıyor, XSUM tutuyor.
4. REC: bir alarm tetikle → `REC_FLAG≠0` → `REC_ACK`'e SEQ yaz → başarılı REC **sessiz**.
5. Config: `0x74`'e kapasite (7–50) → `0x2C` echo doğrula.
6. Sayaçlar: `0x45/0x46/0x47` bekçi-tetik sayaçlarını periyodik oku.
7. Kenar: üst kartı çıkar → Power Board BENCH'e düşmez (thrash yok), NACK'ları normal işler.

### Power Board UART satırları (test sırasında, 115200 8N1, J25)

| UART satırı (birebir) | Ne zaman | Anlamı |
|---|---|---|
| `W/PROT: ... mod: UPLINK` | Boot; 0x48 ACK | Telemetri akar (restore artık yeniden okunmaz) |
| `W/PROT: Restore OK SoH=N` | Boot; 0x60 geçerli | SoH seed edildi |
| `W/PROT: Restore gecersiz (marker/xsum) -> default` | marker≠0xA5 / xsum tutmaz | SoH %100 default |
| `E/PROT: REC FAIL seq=N` | REC_ACK zamanında yok | 10 deneme doldu (başarılı REC sessiz) |

> **NOT:** WARN seviyesi log — yalnız `E`/`W` çıkar; `I` (info) satırı yok. `W/MON:` blokları **yalnız
> BENCH** modunda; UPLINK'te MON tamamen susar.

---

## 15. Revizyon Tarihçesi

| Rev | Tarih | Değişiklik | Hazırlayan |
|-----|-------|------------|------------|
| **R0.00** | 2026-07-05 | İlk yayın. Slave (0x48) entegrasyon sözleşmesi: I²C kriterleri (K1–K10), işlemci-bağımsız davranış modeli, telemetri register haritası (0x00–0x5F), okuma blokları (restore/REC_ACK/config), checksum tanımları, boot penceresi ve REC handshake, savunmacı 16K↔32K kodlama, register bit/enum eki. | Yaşar BOLAT |

> **Revizyon kuralı:** Ana sürüm/geriye-uyumsuz değişiklikte birinci hane (R1.00), geriye-uyumlu
> ekleme/düzeltmede ikinci hane (R0.01…) artırılır. Her değişiklik bu tabloya bir satır olarak eklenir.

**Soru / çelişki iletimi:** ilgili **bölüm numarasıyla** iletiniz (örn. "§7 restore XSUM") — hızlı dönüş yapılır.

---

<sub>© 2026 **BOLATeX** · bolatex.com.tr — Bu doküman *Smart_Sectionalizer — Power Board* projesine aittir ve
üst kart I²C slave entegrasyonu amacıyla paylaşılmıştır. Hazırlayan: **Yaşar BOLAT** · Doküman No:
BLTX-PWRB-I2C-SLV-16K · Revizyon: **R0.00** · 2026-07-05.</sub>
