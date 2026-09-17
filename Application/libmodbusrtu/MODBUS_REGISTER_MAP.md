# Modbus RTU Register Haritasi ve Haberlesme Kilavuzu

> Smart Breaker Modem - Modbus RTU Slave Arabirimi
> Bu dokuman hem musteri tarafindan SCADA/RTU entegrasyonu icin, hem de
> firmware gelistirme tarafinda kaynak olarak kullanilmak uzere hazirlanmistir.

| | |
|---|---|
| Dokuman surumu | 1.5 |
| Tarih | 2026-09-16 |
| Protokol | Modbus RTU (seri) |
| Cihaz rolu | Slave (sunucu) |

---

## 1. Genel Bakis

Cihaz, RF ayirici (breaker) sahasindan topladigi olcum ve durum bilgilerini
Modbus RTU **slave** olarak yayinlar. Bir ust seviye cihaz (SCADA, RTU, PLC veya
baska bir master) bu verileri **Read Holding Registers (FC03)** ile okur.

- Veri okuma: `0x03` (Read Holding Registers)
- Komut yazma: `0x06` (Write Single Register) - sadece ozel kontrol register'lari
- Adresleme: her enerji hatti icin **bitisik** ve **sabit** bir register blogu

Cihaz en fazla **8 enerji hatti** (Line 1..8) destekler. Her hat icin uc faz
bilgisi tutulur: **R (L1), S (L2), T (L3)**.

---

## 2. Seri Port Ayarlari

| Parametre | Varsayilan | Aciklama |
|---|---|---|
| Baud rate | 115200 | Yapilandirilabilir |
| Veri biti | 8 | Sabit |
| Parite | None | Sabit |
| Stop biti | 1 | Sabit |
| Slave adresi (Unit ID) | 23 | Yapilandirilabilir (1..247) |

Hat seviyesi (frame) sonu, Modbus standardina uygun olarak **3.5 karakter
sessizlik (T3.5)** ile belirlenir.

---

## 3. Adresleme Yontemi

Bu dokumanda iki adres gosterimi kullanilir:

| Gosterim | Aciklama | Ornek |
|---|---|---|
| **Mantiksal register (4xxxx)** | Insan-okunur holding register numarasi | 40000 |
| **Protokol adresi (base-0)** | Telde giden ham PDU adresi | 0 |

Donusum kurali:

```
Protokol adresi (base-0) = Mantiksal register - 40000
```

> ModbusPoll, pymodbus gibi araclarda "base-0 / PDU address" alanina
> **protokol adresi** girilir. Ornek: 40030 mantiksal register -> Address 30.

---

## 4. Veri Tipleri ve Kodlama

| Tip | Register | Kodlama | Aciklama |
|---|---|---|---|
| **FLOAT32** | 2 | IEEE-754 single, big-endian (ABCD) | Analog olcumler (akim) |
| **UINT16** | 1 | Isaretsiz 16-bit | Sure ve durum bilgileri |
| **UINT32** | 2 | Isaretsiz 32-bit, big-endian (ABCD) | Zaman damgasi (Unix epoch) |

### 4.1 Word/Byte Order (ABCD)

Tum 32-bit degerler (FLOAT32 ve UINT32) **big-endian, high-word-first (ABCD)**
sirasiyla yayinlanir:

```
32-bit deger = 0xAABBCCDD
  Register[N]   = 0xAABB   (high word, once gelir)
  Register[N+1] = 0xCCDD   (low word)
```

Ornek - FLOAT32 12.5 A degeri (IEEE-754 = 0x41480000):

```
  Register[N]   = 0x4148
  Register[N+1] = 0x0000
```

> Okuma araclarinda 32-bit deger icin "32-bit Float / 32-bit Unsigned" tipi ve
> **big-endian (ABCD)** word order secilmelidir.

### 4.2 Zaman Damgasi (Unix Epoch)

Zaman bilgisi **standart Unix epoch** (1 Ocak 1970 00:00:00 UTC referansli),
**UINT32 saniye** olarak yayinlanir. 2106 yilina kadar gecerlidir.

> Cihaz dahili saat referansini sinir katmaninda standart 1970-epoch'a cevirir;
> master tarafinda ek bir donusum gerekmez.

---

## 5. Register Haritasi (Hat Basina Blok)

Her enerji hatti icin register blogu, hat taban adresinden itibaren
**bitisik** yerlesir. Boylece bir master, bir hattin tum verisini **tek
okuma penceresinde** alabilir.

### 5.1 Hat Taban Adresleri

```
Hat taban adresi (mantiksal) = 40000 + (hat_index * 100)
Hat taban adresi (base-0)    = hat_index * 100
```

| Hat | hat_index | Taban (mantiksal) | Taban (base-0) |
|---|---|---|---|
| Line 1 | 0 | 40000 | 0 |
| Line 2 | 1 | 40100 | 100 |
| Line 3 | 2 | 40200 | 200 |
| Line 4 | 3 | 40300 | 300 |
| Line 5 | 4 | 40400 | 400 |
| Line 6 | 5 | 40500 | 500 |
| Line 7 | 6 | 40600 | 600 |

### 5.1.1 Hat Adres Araliklari (Canli Veri Blogu)

Her hattin canli veri blogu 27 register'dir (offset 0..26). Asagidaki tablo, her
hat icin tek pencerede okunacak adres araligini ve okuma parametrelerini verir.

| Hat | Canli blok (mantiksal) | Canli blok (base-0) | FC03 okuma (base-0) |
|---|---|---|---|
| Line 1 | 40000 .. 40026 | 0 .. 26 | Address 0,   Quantity 27 |
| Line 2 | 40100 .. 40126 | 100 .. 126 | Address 100, Quantity 27 |
| Line 3 | 40200 .. 40226 | 200 .. 226 | Address 200, Quantity 27 |
| Line 4 | 40300 .. 40326 | 300 .. 326 | Address 300, Quantity 27 |
| Line 5 | 40400 .. 40426 | 400 .. 426 | Address 400, Quantity 27 |
| Line 6 | 40500 .. 40526 | 500 .. 526 | Address 500, Quantity 27 |
| Line 7 | 40600 .. 40626 | 600 .. 626 | Address 600, Quantity 27 |

> **Aktif olmayan hatlar:** Cihaz 7 hat destekler (v1.4'te kesinlesti)
> ancak sahada hepsi aktif olmayabilir. Aktif olmayan bir hattin register
> araligi icin cihazin davranisi: tum register'lar 0 doner (boylece master
> sabit pencereyle hatasiz okur ve "veri yok" durumunu degerden anlar).
> 40700 ve uzeri (8. hat) **desteklenmez** - master bu araligi okursa
> exception 02 (ILLEGAL DATA ADDRESS) alir.

> **Offset 27..99 araligi:** Her hat blogunda canli veriden sonra gelen bu aralik
> ileride ariza kayit (fault-log) bloklari icin **rezerve** edilmistir; bu surumde
> okunmasi tavsiye edilmez.

### 5.2 Blok Icerigi (taban adresinden offset)

Asagidaki tablo Line 1 (taban 40000 / base-0 0) icin gosterilmistir. Diger
hatlar icin tablodaki adreslere hat taban adresini ekleyin.

| Offset | Mantiksal | Base-0 | Alan | Faz | Tip | Birim/Anlam |
|---:|---:|---:|---|:---:|---|---|
| 0  | 40000 | 0  | ariza_akimi | R | FLOAT32 | Ariza akimi (A) |
| 2  | 40002 | 2  | ariza_akimi | S | FLOAT32 | Ariza akimi (A) |
| 4  | 40004 | 4  | ariza_akimi | T | FLOAT32 | Ariza akimi (A) |
| 6  | 40006 | 6  | anlik_akim | R | FLOAT32 | Anlik akim (A) |
| 8  | 40008 | 8  | anlik_akim | S | FLOAT32 | Anlik akim (A) |
| 10 | 40010 | 10 | anlik_akim | T | FLOAT32 | Anlik akim (A) |
| 12 | 40012 | 12 | ariza_suresi | R | UINT16 | Ariza suresi (ms) |
| 13 | 40013 | 13 | ariza_suresi | S | UINT16 | Ariza suresi (ms) |
| 14 | 40014 | 14 | ariza_suresi | T | UINT16 | Ariza suresi (ms) |
| 15 | 40015 | 15 | ariza_kalicimi | R | UINT16 | 0/1 (bkz. 5.3) |
| 16 | 40016 | 16 | ariza_kalicimi | S | UINT16 | 0/1 |
| 17 | 40017 | 17 | ariza_kalicimi | T | UINT16 | 0/1 |
| 18 | 40018 | 18 | enerji_varyok | R | UINT16 | 0/1 |
| 19 | 40019 | 19 | enerji_varyok | S | UINT16 | 0/1 |
| 20 | 40020 | 20 | enerji_varyok | T | UINT16 | 0/1 |
| 21 | 40021 | 21 | nominal_akim_varyok | R | UINT16 | 0/1 |
| 22 | 40022 | 22 | nominal_akim_varyok | S | UINT16 | 0/1 |
| 23 | 40023 | 23 | nominal_akim_varyok | T | UINT16 | 0/1 |
| 24 | 40024 | 24 | rf_haberlesme_varyok | R | UINT16 | 0/1 |
| 25 | 40025 | 25 | rf_haberlesme_varyok | S | UINT16 | 0/1 |
| 26 | 40026 | 26 | rf_haberlesme_varyok | T | UINT16 | 0/1 |

- **Canli veri blogu**: offset 0..26 (toplam **27 register**), bitisik.
- FLOAT32 degerler cift adrese hizalanmistir (offset 0,2,4,6,8,10).
- Offset 27..99 araligi ileride kullanim icin **rezerve** (arz/ariza kayit
  bloklari) edilmistir; bu surumde tanimsizdir.

### 5.3 Durum Register Anlamlari

Durum register'lari 0/1 mantiksal degerdir:

| Alan | 0 | 1 |
|---|---|---|
| ariza_kalicimi | Gecici / ariza yok | Kalici ariza |
| enerji_varyok | (uygulama tanimli) | (uygulama tanimli) |
| nominal_akim_varyok | (uygulama tanimli) | (uygulama tanimli) |
| rf_haberlesme_varyok | RF haberlesme var | RF haberlesme yok |

> Not: enerji_varyok / nominal_akim_varyok alanlarinin 0/1 polaritesi cihaz
> uygulamasina gore netlestirilmeli ve bu tabloya islenmelidir.

---

## 6. Ozel Kontrol Register'lari

Bu register'lar hat bloklarindan bagimsizdir.

| Mantiksal | Base-0 | Islev | Erisim | Aciklama |
|---:|---:|---|---|---|
| 50000 | 10000 | Aku uyarisi | Okuma (FC03) | Aku/besleme uyari durumu |
| 50001 | 10001 | Modem reset | Yazma (FC06) | Deger = 1 yazilirsa cihaz yeniden baslar |

> Modem reset: `FC06`, adres 50001 (base-0 10001), deger `0x0001`. Bu komut
> cihazi guvenli sekilde yeniden baslatir.

---

## 7. Sistem ve Guc Telemetri Bloklari (Salt Okunur)

Hat bazli olcum blogundan (bolum 5) ve kontrol register'larindan (bolum 6)
bagimsiz, genel amacli **salt-okunur** iki blok. Her ikisi de bitisik ve tek
FC03 penceresinde toplu okunabilir. Yazma (FC06) desteklenmez.

### 7.1 Sistem Istatistik Blogu (49000 / base-0 9000)

Cihazin dahili durumu: RTC, calisma suresi, reset nedeni, besleme gerilimleri
ve dijital inputlar. 17 register (49000..49016).

| Mantiksal | Base-0 | Alan | Tip | Birim/Anlam |
|---:|---:|---|---|---|
| 49000 | 9000 | rtc_sec | UINT16 | RTC saniye |
| 49001 | 9001 | rtc_min | UINT16 | RTC dakika |
| 49002 | 9002 | rtc_hour | UINT16 | RTC saat |
| 49003 | 9003 | rtc_day | UINT16 | RTC gun |
| 49004 | 9004 | rtc_month | UINT16 | RTC ay |
| 49005 | 9005 | rtc_year | UINT16 | RTC yil (tam, orn. 2026) |
| 49006 | 9006 | rtc_unix | UINT32 (ABCD) | Unix epoch, high word |
| 49007 | 9007 | rtc_unix | UINT32 (ABCD) | Unix epoch, low word |
| 49008 | 9008 | uptime | UINT16 | Calisma suresi (dakika) |
| 49009 | 9009 | reset_reason | UINT16 | Reset kaynak CSR bitleri |
| 49010 | 9010 | mcu_temp | UINT16 | MCU die sicakligi (°C) |
| 49011 | 9011 | v5v | UINT16 | 5V ray (mV) |
| 49012 | 9012 | v3v3 | UINT16 | 3V3 ray (mV) |
| 49013 | 9013 | v3v8 | UINT16 | 3V8 ray (mV) |
| 49014 | 9014 | uptime_raw | UINT32 (ABCD) | Calisma suresi (ms), high word |
| 49015 | 9015 | uptime_raw | UINT32 (ABCD) | Calisma suresi (ms), low word |
| 49016 | 9016 | dinput_states | UINT16 | Dijital input bitmask (bit 0..7) |

- `rtc_unix` (49006-49007) standart Unix epoch'tur (bolum 4.2).
- `uptime` (49008) **dakika** cinsindendir; tam deger icin `uptime_raw`
  (49014-49015, **ms**, UINT32 ABCD) kullanilir.
- `reset_reason` MCU reset kaynak CSR bitleridir (ham); bit anlamlari MCU'ya
  ozgudur.

### 7.2 Guc Karti (PowerBoard) Telemetri Blogu (49200 / base-0 9200)

PowerBoard'dan I2C yardimci (slave) uzerinden ~1 sn'de bir gelen telemetri:
batarya/PV/DC gerilimleri, sarj/bus akimlari, sicakliklar, SoC/SoH ve BQ
fault/alarm bitleri. 32 register (49200..49231).

| Mantiksal | Base-0 | Alan | Tip | Birim/Anlam |
|---:|---:|---|---|---|
| 49200 | 9200 | valid | UINT16 | 1 = checksum gecerli, 0 = hatali |
| 49201 | 9201 | prot_ver | UINT16 | Protokol surumu |
| 49202 | 9202 | seq | UINT16 | Ornek sayaci (wrap) |
| 49203 | 9203 | rec_flag | UINT16 | REC_FLAG bitfield |
| 49204 | 9204 | blk_xsum | UINT16 | Alinan checksum byte |
| 49205 | 9205 | sys_state | UINT16 | SYS_STATE |
| 49206 | 9206 | sys_fault | UINT16 | SYS_FAULT (BQ FAULT0 aynasi) |
| 49207 | 9207 | sys_flags | UINT16 | SYS_FLAGS (canli alarmlar) |
| 49208 | 9208 | soh_x10 | UINT16 | Batarya sagligi %, x10 (1000 = 100.0%) |
| 49209 | 9209 | soc_x10 | UINT16 | Sarj durumu %, x10 (1000 = 100.0%) |
| 49210 | 9210 | vbat_mv | UINT16 | Batarya gerilimi (mV) |
| 49211 | 9211 | vpv_mv | UINT16 | PV gerilimi (mV) |
| 49212 | 9212 | vdc_mv | UINT16 | DC gerilimi (mV) |
| 49213 | 9213 | ichg_ma | UINT16 | Sarj akimi (mA) |
| 49214 | 9214 | ibat_ma | INT16 | Batarya akimi (mA, +sarj / -desarj) |
| 49215 | 9215 | ibus_ma | UINT16 | Bus akimi (mA) |
| 49216 | 9216 | board_temp_c | INT16 | Kart NTC sicakligi (°C) |
| 49217 | 9217 | batt_temp_x10 | INT16 | Batarya NTC x10 (°C, 255 = 25.5°C) |
| 49218 | 9218 | batt_ts | UINT16 | JEITA bolge (0..4) |
| 49219 | 9219 | chg_stat | UINT16 | Sarj fazi (0..7) |
| 49220 | 9220 | chg_phase | UINT16 | Sarj fazi (ayna) |
| 49221 | 9221 | batt_cap_ah | UINT16 | Aktif kapasite (Ah) |
| 49222 | 9222 | bq_fault0 | UINT16 | BQ REG20 raw |
| 49223 | 9223 | bq_fault1 | UINT16 | BQ REG21 raw |
| 49224 | 9224 | alarm_live | UINT16 | Canli alarm bitleri |
| 49225 | 9225 | alarm_latch | UINT16 | Latch'li alarm bitleri |
| 49226 | 9226 | pwr_io | UINT16 | GPIO aynasi |
| 49227 | 9227 | bq_vsys_mv | UINT16 | BQ VSYS (mV) |
| 49228 | 9228 | bq_vbus_mv | UINT16 | BQ VBUS (mV) |
| 49229 | 9229 | bq_vac1_mv | UINT16 | BQ VAC1 = DC giris (mV) |
| 49230 | 9230 | bq_vac2_mv | UINT16 | BQ VAC2 = PV giris (mV) |
| 49231 | 9231 | bq_tdie_c | INT16 | BQ die sicakligi (°C) |

- **Isaretli (INT16) alanlar:** `ibat_ma`, `board_temp_c`, `batt_temp_x10`,
  `bq_tdie_c` - iki-tumleyen (two's complement) olarak yayinlanir; master taraf
  signed 16-bit olarak okumalidir.
- `valid` (49200) = 0 ise checksum eslesmedi; alanlar yine de decode edilir ama
  guvenilir kabul edilmez.
- `*_x10` alanlari 10 ile olceklidir (100.0% -> 1000).
- Sicaklik/sentinel degerleri icin bkz. PowerBoard sozlesmesi
  (`doc/I2C_SLAVE_ENTEGRASYON_16K.md`).

### 7.3 BMS Telemetri Blogu (49300 / base-0 9300)

Harici BMS modulunden okunan pak olcumleri: pak gerilimi/akimi, SoC/SoH,
hucresel gerilimler ve sicakliklar, calisma durumu, MOS ve hata kodlari.
79 register (49300..49378). Kaynak modul: `Application/modbus_bms_stats.c`.

| Mantiksal | Base-0 | Alan | Tip | Birim/Anlam |
|---:|---:|---|---|---|
| 49300 | 9300 | valid | UINT16 | 1 = veri gecerli, 0 = hatali |
| 49301 | 9301 | soh_valid | UINT16 | 1 = SoH gecerli |
| 49302 | 9302 | total_voltage_x10 | UINT16 | Pak gerilimi x10 (0.1 V) |
| 49303 | 9303 | current_x10 | INT16 | Pak akimi x10 (0.1 A, +sarj / -desarj) |
| 49304 | 9304 | soc_x10 | UINT16 | Sarj durumu x10 (%) |
| 49305 | 9305 | soh_x10 | UINT16 | Pil sagligi x10 (%) |
| 49306 | 9306 | life_heartbeat | UINT16 | LIFE heartbeat sayaci |
| 49307 | 9307 | active_cell_count | UINT16 | Hucre adedi |
| 49308 | 9308 | temp_sensor_count | UINT16 | Sicaklik sensoru adedi |
| 49309 | 9309 | max_cell_mv | UINT16 | En yuksek hucre gerilimi (mV) |
| 49310 | 9310 | max_cell_index | UINT16 | En yuksek hucre sirasi |
| 49311 | 9311 | min_cell_mv | UINT16 | En dusuk hucre gerilimi (mV) |
| 49312 | 9312 | min_cell_index | UINT16 | En dusuk hucre sirasi |
| 49313 | 9313 | cell_diff_mv | UINT16 | Hucre gerilim farki (mV) |
| 49314 | 9314 | max_temp_c | INT16 | En yuksek hucre sicakligi (degC) |
| 49315 | 9315 | max_temp_index | UINT16 | En sicak sensor sirasi |
| 49316 | 9316 | min_temp_c | INT16 | En dusuk hucre sicakligi (degC) |
| 49317 | 9317 | min_temp_index | UINT16 | En soguk sensor sirasi |
| 49318 | 9318 | temp_diff_c | INT16 | Sicaklik farki (degC) |
| 49319 | 9319 | work_state | UINT16 | 0=bosta / 1=sarj / 2=desarj |
| 49320 | 9320 | charger_status | UINT16 | 0=yok / 1=sarj cihazi var |
| 49321 | 9321 | load_status | UINT16 | 0=yok / 1=yuk var |
| 49322 | 9322 | remaining_cap_x10 | UINT16 | Kalan kapasite x10 (0.1 Ah) |
| 49323 | 9323 | cycle_count | UINT16 | Cevrim sayisi |
| 49324 | 9324 | balance_state | UINT16 | 0=kapali / 1=pasif / 2=aktif |
| 49325 | 9325 | mos_status_flags | UINT16 | MOS durum bitmask |
| 49326 | 9326 | average_voltage_mv | UINT16 | Ortalama hucre gerilimi (mV) |
| 49327 | 9327 | power_w | UINT16 | Guç (W) |
| 49328 | 9328 | energy_wh | UINT16 | Enerji (Wh) |
| 49329 | 9329 | mos_temp_c | INT16 | Guç MOS sicakligi (degC) |
| 49330 | 9330 | ambient_temp_c | INT16 | Ortam sicakligi (degC) |
| 49331 | 9331 | heating_temp_c | INT16 | Isitma sicakligi (degC) |
| 49332 | 9332 | heating_current_a | UINT16 | Isitma akimi (A) |
| 49333 | 9333 | current_limit_state | UINT16 | 1=akim sinirlama aktif / 0=kapali |
| 49334 | 9334 | current_limit_x10 | INT16 | Akim siniri x10 (0.1 A) |
| 49335 | 9335 | rtc_year | UINT16 | BMS RTC yili (tam, orn. 2026) |
| 49336 | 9336 | rtc_month | UINT16 | BMS RTC ay |
| 49337 | 9337 | rtc_day | UINT16 | BMS RTC gun |
| 49338 | 9338 | rtc_hour | UINT16 | BMS RTC saat |
| 49339 | 9339 | rtc_minute | UINT16 | BMS RTC dakika |
| 49340 | 9340 | rtc_second | UINT16 | BMS RTC saniye |
| 49341 | 9341 | remaining_charge_min | UINT16 | Kalan sarj suresi (dk) |
| 49342 | 9342 | dido_status | UINT16 | DI1..8 low byte / DO1..8 high byte |
| 49343 | 9343 | wake_source_flags | UINT16 | Uyanma kaynagi bitmask |
| 49344 | 9344 | comm_interface_type | UINT16 | 1 = RS485, 2 = UART |
| 49345..49360 | 9345..9360 | cell_voltage_mv[1..16] | UINT16 x16 | Hucresel gerilimler (mV) |
| 49361..49368 | 9361..9368 | temperatures_c[1..8] | INT16 x8 | Sensor sicakliklari (degC) |
| 49369..49371 | 9369..9371 | balance_position[1..3] | UINT16 x3 | Hucre bazli dengeleme bitmask |
| 49372..49378 | 9372..9378 | fault_codes[1..7] | UINT16 x7 | BMS hata/alarm kod sozcukleri |

- `valid` (49300) = 0 ise diger alanlar guvenilir kabul edilmez.
- Isaretli (INT16) alanlar iki-tumleyen olarak yayinlanir (bolum 7.1 ile ayni).

### 7.4 GSM Durumu Blogu (49400 / base-0 9400)

Modem sagligi: GSM durum, sinyal kalitesi (ham AT+CSQ), kusak (RAT), soket
durumlari ve son Modbus exception kaydi. 9 register (49400..49408). Kaynak
modul: `Application/modbus_gsm_stats.c`.

| Mantiksal | Base-0 | Alan | Tip | Birim/Anlam |
|---:|---:|---|---|---|
| 49400 | 9400 | gsm_state | UINT16 | 0=ortak init 1=modul init 2=SIM hata 3=normal 4=guc kesinti 5=guc tasarrufu |
| 49401 | 9401 | gsm_signal_csq | UINT16 | Ham AT+CSQ: 0..31, 99 = bilinmiyor |
| 49402 | 9402 | gsm_rat | UINT16 | 0=bilinmiyor 2=2G 3=3G 4=4G |
| 49403 | 9403 | mb_last_error_code | UINT16 | Son Modbus exception kodu (bkz. 9.1) |
| 49404 | 9404 | mb_last_error_time_hi | UINT32 (ABCD) | Son hata zamani (Unix epoch), high word |
| 49405 | 9405 | mb_last_error_time_lo | UINT32 (ABCD) | Son hata zamani, low word |
| 49406 | 9406 | socket_state_web | UINT16 | Web listener soket durumu (bkz. asagida) |
| 49407 | 9407 | socket_state_iec104 | UINT16 | IEC104 listener soket durumu |
| 49408 | 9408 | socket_state_dialer | UINT16 | HES dialer (musteri) soket durumu |

- `gsm_signal_csq`: CSQ = 0 cok zayif, 31 cok iyi; 99 olculmedi/bilinmiyor.
- Soket durumlari (`socket_state_t`): 0=kapali, 1=acik, 2=okunmayi bekleyen
  data var, 3=bagli cihaz var, 4=data gonderim modunda, 5=dinlemede (aktif
  cihaz yok), 6=acilamadi (hata).
- `modbus-tools/gsm_stats.mbp` durum/kusak/hata/soket alanlarini "Normal",
  "2G", "Hata Yok", "Dinliyor" gibi metinlerle gosterir (value-name eslemesi).

### 7.5 Toplu Okuma (FC03)

| Blok | Address (base-0) | Quantity |
|---|---:|---:|
| Sistem istatistik | 9000 | 17 |
| Guc karti telemetri | 9200 | 32 |
| BMS telemetri | 9300 | 79 |
| GSM durumu | 9400 | 9 |

> UINT32 (ABCD) alanlari icin master tarafinda "32-bit Unsigned" ve big-endian
> (ABCD) word order secilmelidir (bolum 4.1).

---

## 8. Okuma Ornekleri

### 8.1 Bir Hattin Tum Verisini Okuma (FC03)

Line 1'in tum canli verisini tek pencerede okumak icin:

| Alan | Deger |
|---|---|
| Slave ID | 23 |
| Function | 03 (Read Holding Registers) |
| Address (base-0) | 0 |
| Quantity | 27 |

Line 2 icin Address = 100, Quantity = 27; Line 3 icin Address = 200; ...

### 8.2 Sadece Anlik Akimlari Okuma

Line 1 anlik akim (R/S/T) icin:

| Alan | Deger |
|---|---|
| Function | 03 |
| Address (base-0) | 6 |
| Quantity | 6 |

Donen 6 register, 3 adet FLOAT32 (R, S, T) olarak yorumlanir.

### 8.3 ModbusPoll Ayar Ozeti

- Connection: Serial, 115200 8N1
- Slave ID: 23
- Function: 03
- Address: ilgili base-0 adres (orn. 0)
- Quantity: 27
- Display format: FLOAT32 alanlar icin "32-bit Float", word order **ABCD
  (big-endian)**; durum/sure alanlari icin "Unsigned 16-bit".

---

## 9. Exception (Hata) Yanitlari

Cihaz, gecersiz isteklere Modbus standart exception kodlari ile yanit verir:

| Kod | Ad | Anlam |
|---|---|---|
| 0x01 | ILLEGAL FUNCTION | Desteklenmeyen fonksiyon kodu |
| 0x02 | ILLEGAL DATA ADDRESS | Istenen adres haritada tanimli degil |
| 0x03 | ILLEGAL DATA VALUE | Gecersiz yazma degeri veya istek sayisi |

> Onemli: Tek bir FC03 okuma penceresinde **tanimsiz** bir adres bulunursa,
> cihaz tum istek icin `0x02` doner. Bu nedenle okuma pencereleri yukaridaki
> bitisik blok sinirlari icinde tutulmalidir (orn. Address 0, Quantity 27).

### 9.1 Son Hata Kodu (SonHataKodu)

Cihaz, gonderdigi **son exception kodunu** dahili olarak saklar. Bu deger her
yeni exception yanitinda guncellenir; basarili istekler degeri **degistirmez**,
yani alan acilistan bu yana olusan en son hatayi yansitir (otomatik silinmez).

Deger su sekilde gozlenir:

| Arabirim | Alan | Aciklama |
|---|---|---|
| Web (HTTP) | `SonHataKodu` (`GET /r?modbusConfigs` JSON) | Son exception kodunu UINT8 olarak doner |
| Web (HTTP) | `SonHataZamani` (`GET /r?modbusConfigs` JSON) | Son hatanin olustugu an, **Unix epoch saniye** (UINT32, 1970 tabanli) |

Deger anlamlari, yukaridaki exception kodlari ile aynidir:

| SonHataKodu | Anlam |
|---|---|
| 0x00 | Acilistan bu yana hata olusmadi |
| 0x01 | Son hata: ILLEGAL FUNCTION |
| 0x02 | Son hata: ILLEGAL DATA ADDRESS |
| 0x03 | Son hata: ILLEGAL DATA VALUE |

> Not: `SonHataZamani` yalnizca RAM'de tutulur (NVRAM'de kalici degildir); cihaz
> yeniden baslayinca 0'a doner. Zaman damgasi standart Unix epoch'tur (1 Ocak
> 1970 referansli) ve `uint32_t` oldugu icin 2106 yilina kadar gecerlidir
> (signed 2038 sorunu yasanmaz). Deger 0 ise henuz hata olusmamis demektir.

> Not: `SonHataKodu` / `SonHataZamani` ayrica v1.5'ten beri Modbus tarafinda da
> okunabilir: iletisim durumu blogu 49403..49405 (bkz. 7.4). Deger anlamlari
> asagidaki exception kodlari ile aynidir:

---

## 10. Yapilandirilabilir Ayarlar Ozeti

| Ayar | Varsayilan | Aralik | Notu |
|---|---|---|---|
| Slave adresi | 23 | 1..247 | Multidrop bus icin benzersiz olmali |
| Baud rate | 115200 | standart hizlar | Master ile eslesmeli |
| Aku uyarisi register | 50000 | - | Ozel register adresi |
| Modem reset register | 50001 | - | Ozel register adresi |

---

## 11. Revizyon Gecmisi

| Surum | Tarih | Aciklama |
|---|---|---|
| 1.0 | 2026-06-13 | Ilk surum: bitisik float tabanli register haritasi |
| 1.1 | 2026-06-15 | Son hata kodu (SonHataKodu) takibi ve web arabirimi eklendi (bkz. 9.1) |
| 1.2 | 2026-06-15 | Son hata zamani (SonHataZamani, Unix epoch) eklendi; sistem geneli zaman tabani 1970 Unix epoch'a tasindi |
| 1.3 | 2026-07-14 | Sistem istatistik (49000) ve guc karti telemetri (49200) salt-okunur bloklari eklendi (bkz. 7) |
| 1.4 | 2026-09-04 | Hat sayisi 7'ye kesinlesti; 40700 ve uzeri desteklenmez (bkz. 5.1.1) |
| 1.5 | 2026-09-16 | BMS telemetri blogu (49300, dokumante edildi) ve GSM durum blogu (49400: GSM durum/CSQ/RAT, soket durumlari, SonHataKodu/SonHataZamani) eklendi |
