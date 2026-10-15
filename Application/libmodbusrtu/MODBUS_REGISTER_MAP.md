# Modbus RTU Register Haritasi ve Haberlesme Kilavuzu

> Smart Breaker Modem - Modbus RTU Slave Arabirimi
> Bu dokuman hem musteri tarafindan SCADA/RTU entegrasyonu icin, hem de
> firmware gelistirme tarafinda kaynak olarak kullanilmak uzere hazirlanmistir.

| | |
|---|---|
| Dokuman surumu | 2.0 |
| Tarih | 2026-10-15 |
| Protokol | Modbus RTU (seri) |
| Cihaz rolu | Slave (sunucu) |

---

## 1. Genel Bakis

Cihaz, RF ayirici (breaker) sahasindan topladigi olcum ve durum bilgilerini
Modbus RTU **slave** olarak yayinlar. Bir ust seviye cihaz (SCADA, RTU, PLC veya
baska bir master) bu verileri **Read Holding Registers (FC03)** ile okur.

- Veri okuma: `0x03` (Read Holding Registers)
- Komut yazma: `0x06` (Write Single Register) - yalnizca modem-reset register'i
- Adresleme: **tum register adresleri sabittir** (derleme zamani haritasi);
  web'den veya NVRAM'den adres degistirilemez. Yapilandirilabilir olanlar
  yalnizca CihazID ve baud rate'dir (bkz. bolum 10).

Harita **4 fider** (Fider 1..4) yayinlar; Modbus Fider N blogu RF tarafinda
`Fider_ID == N` atamis olan hatti yansitir. Her fider icin uc faz bilgisi
tutulur: **L1, L2, L3** (L1 = R, L2 = S, L3 = T). Her fider blogu, IEC104
tarafinda yayinlanan bilgilerin aynasini (anlik akim, enerji/yuk/RF
durumlari, trip basarisizligi, gecici ve kalici ariza listeleri) ve RF canli
olcumlerinin SCADA'ya anlamlı olanlarini tek blokta tasir.

**Gecerlilik (kalite) kurali:** her fider ve faz kendi kalite register'iyla
yayinlanir. Bir deger yalnizca ilgili kalite biti 1 iken gerceldir; kalite 0
ise yayinlanan 0 degerleri gercek olcum degildir (bkz. 5.2).

## 2. Seri Port Ayarlari

| Parametre | Varsayilan | Aciklama |
|---|---|---|
| Baud rate | 115200 | Yapilandirilabilir |
| Veri biti | 8 | Sabit |
| Parite | None | Sabit |
| Stop biti | 1 | Sabit |
| Slave adresi (Unit ID) | 23 | Yapilandirilabilir (1..247) |

Cihaz adresi 1..247 arasında olmalıdır. 0, broadcast (yayın) hedefidir;
cihazın kendi adresi olarak kaydedilmemelidir. Web arayüzü ve sunucu bu
aralık dışındaki ayarları reddeder.

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

## 5. Fider Bloklari (Sabit Harita)

### 5.1 Fider Taban Adresleri

```
Fider taban adresi (mantiksal) = 40000 + (fider_no - 1) * 1000
Fider taban adresi (base-0)    = (fider_no - 1) * 1000
```

| Fider | Taban (mantiksal) | Taban (base-0) | Blogun kapsami (mantiksal) |
|---|---|---|---|
| Fider 1 | 40000 | 0 | 40000 .. 40999 |
| Fider 2 | 41000 | 1000 | 41000 .. 41999 |
| Fider 3 | 42000 | 2000 | 42000 .. 42999 |
| Fider 4 | 43000 | 3000 | 43000 .. 43999 |

Modbus Fider N blogu, RF tarafinda Fider_ID == N atamis olan hatti yansitir.
44000 ve uzeri **desteklenmez**; master bu araligi okursa exception 02
(ILLEGAL DATA ADDRESS) alir. Bloklarin icindeki tum adresler FC03 ile normal
yanit verir; tanimli olmayan alanlar (rezerve bolgeler, bos kayitlar) 0 doner.

### 5.2 Gecerlilik (Kalite) Kurallari

Fider ve faz gecerliligi kalite register'lariyla tasinir. Bir degerin gercek
olcum sayilabilmesi icin ilgili kalite biti isaretli olmalidir.

**fider_kalite (blogun +0 register'i):**

| Bit | 1 anlami |
|---:|---|
| 0 | Fider tanimli (RF tarafinda Fider_ID atamasi yapilmis) |
| 1 | En az bir faz canli veri ulastirmis |
| 2-15 | Rezerve, 0 okunur |

**faz_kalite (her faz alt blogunun +0 register'i):**

| Bit | 1 anlami |
|---:|---|
| 0 | Fider tanimli |
| 1 | Bu faz icin canli (LIVE) ornek alindi |
| 2 | Baglanti taze: son ornek 30 s icinde |
| 3 | Anlik akim gecerli (sayisal RMS kullanilabilir) |
| 4 | Trip gerilimi gecerli |
| 5 | Harvest gerilimi gecerli |
| 6 | Zaman damgasi gecerli (ornek saglam bir saatle alinmis) |
| 7-15 | Rezerve, 0 okunur |

Kurallar:

- Fider **tanimli degilse** fider_kalite ve uc fazin faz_kalite degerleri 0'dir
  ve blogun tum veri register'lari 0 yayinlanir. Bu sifirlar olcum olarak
  yorumlanmamalidir.
- Fider **tanimli ama hic canli veri gelmemisse** kalitede yalnizca bit 0
  isaretlidir; veri register'lari 0'dir. Master degerleri yalnizca bit 1
  isaretliyken kullanmalidir ("tanimli ama veri yok" ile "gercek sifir olcum"
  boyle ayristirilir).
- Ornek **bayatsa** (bit 2 = 0): son degerler korunur, sifirlanmaz; tazelik
  yalnizca bit 2 ile anlatilir.
- Akim ornegi **gecersizse** (negatif/NaN/Inf): anlik_akim FLOAT32 NaN yayinlar
  ve bit 3 sifirdir. NaN sifir akim diye yorumlanmamalidir.
- **trip_failed** olcumden bagimsizdir: alarm olayi (BQ-01/BQ-16) canli
  ornek gelmeden de acilabilir; deger dogrudan faz onbelleginden yayinlanir
  ve kalite bitleri onu gecersiz kilmaz.
- Zaman damgasi alanlari ilgili fazin **son canli orneginin** kabul zamanini
  Unix epoch (UINT32, ABCD) olarak tasir. Ornek hic gelmediyse, ya da ornek
  saat kurulmamis/gecersiz (CP56 IV biti ya da imkansiz takvim degeri)
  iken alindiysa alan 0'dir ve kalite bit 6 sifirdir: gecersiz saat asla
  gercek bir tarih gibi yayinlanmaz.

### 5.3 Blok Yerlesimi (fider tabanindan offset)

| Offset | Alan | Boyut | Aciklama |
|---:|---|---:|---|
| 0 | fider_kalite | 1 | Bolum 5.2 |
| 1-3 | reserved | 3 | 0 doner |
| 4-27 | faz[L1] | 24 | Faz alt blogu (5.4) |
| 28-51 | faz[L2] | 24 | Faz alt blogu (5.4) |
| 52-75 | faz[L3] | 24 | Faz alt blogu (5.4) |
| 76-78 | temp_fault_count[L1..L3] | 3 | Gecerli gecici ariza kayit sayilari |
| 79-81 | perm_fault_count[L1..L3] | 3 | Gecerli kalici ariza kayit sayilari |
| 82-99 | reserved | 18 | 0 doner |
| 100-459 | temp_fault[3][15] | 360 | Gecici ariza kayitlari (5.5) |
| 460-499 | reserved | 40 | 0 doner |
| 500-859 | perm_fault[3][15] | 360 | Kalici ariza kayitlari (5.5) |
| 860-999 | reserved | 140 | 0 doner |

### 5.4 Faz Alt Blogu (24 register)

| Faz offset | Alan | Tip | Anlam |
|---:|---|---|---|
| 0 | kalite | UINT16 | faz_kalite (5.2) |
| 1-2 | anlik_akim | FLOAT32 | Anlik akim (A), SCP Live_Irms |
| 3-4 | anlik_zaman | UINT32 | Son canli ornek zamani (unix) |
| 5 | enerji_varyok | UINT16 | 0/1, LIVE Status_Flags bit 0 |
| 6-7 | enerji_zaman | UINT32 | unix |
| 8 | yuk_akimi_varyok | UINT16 | 0/1, LIVE Status_Flags bit 1 |
| 9-10 | yuk_zaman | UINT32 | unix |
| 11 | rf_haberlesme_varyok | UINT16 | 0/1, baglanti taze |
| 12-13 | rf_zaman | UINT32 | unix |
| 14 | trip_failed | UINT16 | 0/1, SCP trip basarisizlik alarmi |
| 15-16 | trip_zaman | UINT32 | unix |
| 17-18 | trip_gerilimi | FLOAT32 | Trip gerilimi (V) |
| 19-20 | harvest_gerilimi | FLOAT32 | Harvest gerilimi (V) |
| 21 | sicaklik | INT16 | Ayirici sicakligi (degC) |
| 22 | rssi | INT16 | RF sinyal gucu (ham) |
| 23 | fault_count | UINT16 | Ayirici ariza sayaci |

32-bit alanlar ust word once (ABCD, bolum 4.1). IEC104 aynasi alanlar
(anlik_akim, enerji/yuk/rf durumleri, trip_failed) IEC104 tarafindaki
bilgilerle ayni kaynaklardan beslenir; her alanin zaman damgasi ayri
register'lardan okunabilir (IEC104 nesnelerindeki CP56 zamaninin Unix epoch
karsiligi).

### 5.5 Ariza Kaydi Bolgeleri

Gecici bolge +100'den, kalici bolge +500'den baslar. Her bolgede fazlar ust
uste yatar (L1, L2, L3 sirasiyla; her faz 15 kayit) ve her kayit 8
register'dir. `temp_fault_count[faz]` / `perm_fault_count[faz]` gecerli kayit
sayisini verir; sayi disindaki kayitlar 0 doner (zaman = 0 bos kayit anlami).

| Kayit offset | Alan | Tip | Anlam |
|---:|---|---|---|
| 0-1 | ariza_akimi | FLOAT32 | Ariza anindaki akim (A) |
| 2-3 | ariza_suresi_ms | UINT32 | Ariza suresi (ms) |
| 4 | enerji_varyok | UINT16 | Ariza aninda enerji durumu (0/1) |
| 5 | yuk_akimi_varyok | UINT16 | Ariza aninda yuk akimi durumu (0/1) |
| 6-7 | zaman | UINT32 | Kayit zamani (unix); 0 = bos kayit |

Ariza kayitlari IEC104 tarafindaki gecici/kalici ariza listeleriyle ayni
kaynaktan (fault log) okunur; icerik ve kayit sayilari birebir uyusur.

### 5.6 Ornek Okuma Pencereleri

| Amac | Address (base-0) | Quantity |
|---|---:|---:|
| Fider 1 kalite + 3 faz canli blogu | 0 | 76 |
| Fider 1 L1 faz alt blogu | 4 | 24 |
| Fider 1 L1 anlik akim | 5 | 2 |
| Fider 2 fider_kalite | 1000 | 1 |
| Fider 1 ariza kayit sayilari | 76 | 6 |
| Fider 1 gecici L1 kayit 0 | 100 | 8 |
| Fider 3 kalici L2 kayit 7 | 7560 | 8 |

## 6. Ozel Kontrol Register'lari

Bu register'lar fider bloklarindan bagimsizdir ve adresleri **sabittir**.

| Mantiksal | Base-0 | Islev | Erisim | Aciklama |
|---:|---:|---|---|---|
| 50000 | 10000 | Aku uyarisi | Okuma (FC03) | Canli aku uyarisi (bkz. asagida) |
| 50001 | 10001 | Modem reset | Yazma (FC06) | Deger = 1 yazilirsa cihaz yeniden baslar |

**Aku uyarisi (50000):** guc kartindan RF-SCP ile gelen guncel alarm
maskesinin **aku ile ilgili** bolumudur. Register 16 bit genisligindedir ve
kod numaralari 15'i asan alarmlar (18 SOC_DUSUK, 20 AKU_YOK) bit indeksi
olarak kod numarasini koruyamaz; bu yuzden her aku alarmi tablo siraasiyla
**kendi adanmis register bitini** alir (bit eslemesi asagidaki tabloda):

| Bit | Alarm kodu | Kod no |
|---:|---|---:|
| 0 | AKU_COK_DUSUK | 0 |
| 1 | AKU_SOGUK | 2 |
| 2 | AKU_SICAK | 3 |
| 3 | AKU_DUSUK | 4 |
| 4 | AKU_YUKSEK | 5 |
| 5 | AKU_OVP_SARJ_YASAGI | 7 |
| 6 | AKU_OVP_KALICI | 8 |
| 7 | SOC_DUSUK | 18 |
| 8 | AKU_YOK | 20 |
| 9-15 | Rezerve, 0 okunur | - |

Deger 0 = aku uyarisinin olmamasi (veya gecerli guc karti ozeti yoklugu /
alarm maskesinin bayatlamiş olmasi). Master uyarı varlığini tek değerin
sifirdan farkiyla, nedenini bitlerle okuyabilir; aku disi kart alarmlari
(besleme, sicaklik, telemetri vb.) bu register'a girmez.

> Modem reset: `FC06`, adres 50001 (base-0 10001), deger `0x0001`. Bu komut
> cihazi guvenli sekilde yeniden baslatir. Adresler degistirilemez.

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
| 49009 | 9009 | reset_reason | UINT16 | Reset nedeni bitmask (reset_source_flag_t) |
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
- `reset_reason` (49009) mevcut `reset_source_flag_t` bitmask değerini
  taşır. Ham CSR değeri değildir. Register adresi ve genişliği aynıdır.
  SCADA/RTU bu register’ı aşağıdaki maskelere göre yorumlamalıdır.
  Birden fazla neden varsa maskeler bitwise OR ile birleştirilir.
  `0x0000`, bilinen bir reset nedeni bildirilmediğini gösterir.

| Maske | Değer | Reset nedeni |
|---|---|---|
| `0x0001` | 1 | Power-on (güç açılması) |
| `0x0002` | 2 | Brownout (besleme düşmesi) |
| `0x0004` | 4 | Harici NRST pini |
| `0x0008` | 8 | Software reset (yazılımdan reset) |
| `0x0010` | 16 | IWDG timeout |
| `0x0020` | 32 | WWDG timeout |
| `0x0040` | 64 | Low-power reset |
| `0x0080` | 128 | Option-byte reset |
| `0x0100` | 256 | Firewall reset |

Örnek: `0x0018` değeri software reset ve IWDG nedenlerinin birlikte
bildirildiğini gösterir. Hangi nedenlerin bildirilebildiği MCU’nun
reset bilgisine ve mevcut `reset_source` decoder’ına bağlıdır.


### 7.2 Güç kartı SCP özeti (49200 / base-0 9200)

[KARAR: 2026-10-06] Kullanıcı 49200 tabanında yeni SCP haritasını onayladı.
38 register, 49200..49237. Eski I2C/BQ/XSUM/latch alanları bu blokta yoktur.
Kaynak R1 0xE1/0xE3; E8 düzeni doğrulanmadan eski decoder kullanılmaz.

| Mantıksal | Base-0 | Alan | Tip | Birim / anlam |
|---:|---:|---|---|---|
| 49200 | 9200 | has_summary | UINT16 | SCP özeti alınmış: 0/1 |
| 49201 | 9201 | valid_fields | UINT16 | Alan geçerlilik bit maskesi; aşağıdaki tablo |
| 49202 | 9202 | seq | UINT16 | E1 sayaç, 0..255 |
| 49203 | 9203 | flags | UINT16 | E1 durum baytının ham değeri |
| 49204 | 9204 | flags2 | UINT16 | E1 durum2 ham değeri |
| 49205 | 9205 | telemetry_age_sec | UINT16 | Bildirilmiş yaş + RTU alım yaşı; 255 doygun |
| 49206 | 9206 | source | UINT16 | 0 akü, 1 PV, 2 DC, 3 belirsiz, 4 akımsız giriş, 255 bilinmiyor |
| 49207 | 9207 | charge_phase | UINT16 | 0 yok, 1 ön şarj, 2 CC, 3 CV, 4 tamamlama, 5 tamam |
| 49208 | 9208 | session | UINT16 | PWRB oturum, 1..255; 0 yok |
| 49209 | 9209 | pv_mv | UINT16 | mV |
| 49210 | 9210 | dc_mv | UINT16 | mV |
| 49211 | 9211 | input_ma | INT16 | mA |
| 49212 | 9212 | system_mv | UINT16 | mV |
| 49213 | 9213 | battery_mv | UINT16 | mV |
| 49214 | 9214 | battery_ma | INT16 | mA; + şarj, - deşarj |
| 49215 | 9215 | input_power_10mw | INT16 | 10 mW |
| 49216 | 9216 | battery_power_10mw | INT16 | 10 mW; + şarj |
| 49217 | 9217 | system_power_10mw | INT16 | 10 mW; negatif tüketim |
| 49218 | 9218 | soc_tenths | INT16 | %0,1; negatif değer korunur |
| 49219 | 9219 | soh_percent | UINT16 | % |
| 49220 | 9220 | battery_temperature | INT16 | derece C; -128 geçersiz |
| 49221 | 9221 | board_temperature | INT16 | derece C; -128 geçersiz |
| 49222 | 9222 | capacity_ah | UINT16 | Ah; 7 + ayar kaynağı yoksa bilinmiyor |
| 49223 | 9223 | charge_rate_permille | UINT16 | Binde C |
| 49224 | 9224 | last_gasp_count | UINT16 | MH bildirim sayacı; gerçek kesinti sayısı değildir |
| 49225 | 9225 | active_alarms_hi | UINT16 | Güncel E1/E3 maskesi, üst word |
| 49226 | 9226 | active_alarms_lo | UINT16 | Güncel maske, alt word |
| 49227 | 9227 | soc_flags | UINT16 | E1 soc_capa ham değeri |
| 49228 | 9228 | alarm_seq | UINT16 | Son E3 sayaç |
| 49229 | 9229 | suppressed | UINT16 | Son E3 bastırılmış kenar sayısı |
| 49230 | 9230 | has_last_gasp | UINT16 | Son nefes görüldü: 0/1 |
| 49231 | 9231 | last_gasp_cancelled | UINT16 | Aynı oturum son nefesi iptal edildi: 0/1 |
| 49232 | 9232 | last_gasp_session | UINT16 | Son nefes PWRB oturumu |
| 49233 | 9233 | last_gasp_cause | UINT16 | 1 ani düşüm, 2 öngörülen kesinti |
| 49234 | 9234 | summary_age_ms_hi | UINT16 | RTU alım yaşı, üst word |
| 49235 | 9235 | summary_age_ms_lo | UINT16 | RTU alım yaşı, alt word; özet yoksa UINT32_MAX |
| 49236 | 9236 | supported_summary_version | UINT16 | Desteklenen E1 yerleşimi: 1 |
| 49237 | 9237 | map_version | UINT16 | Bu register düzeni: 1 |

İşaretli değerler two's complement (ikiye tümleyen) olarak taşınmalıdır;
negatif SOC sıfıra kırpılmamalıdır. 32 bit alanlar high word first (üst word
önce) birleştirilmelidir. Ham sayısal değer geçerlilik maskesi olmadan karar
verisi sayılmamalıdır. Bayat son değerler tanı için korunur; sıfırlanmaz.

| Bit | Geçerlilik / anlam |
|---:|---|
| 0 | Güncel geçerli E1 özeti |
| 1 | PV/DC/akü gerilimleri |
| 2 | Denetleyici: ibus/vsys/ibat temel geçerliliği |
| 3 | Eşleşmiş güç ölçümü: pin/pbat |
| 4 | psys ölçülmüş |
| 5 | SOC geçerli; ayrıca bit 14 gerekir |
| 6 | SOC mutlak referanslı; 0 göreli |
| 7 | SoH geçerli |
| 8 | Akü sıcaklığı geçerli |
| 9 | Kart sıcaklığı geçerli |
| 10 | Şarj fazı geçerli |
| 11 | Kapasite biliniyor |
| 12 | Kaynak bilgisi geçerli |
| 13 | Alarm maskesi güncel |
| 14 | Deşarj ölçümü eksik değil; ibat/pbat/SOC için de gerekir |
| 15 | Şarj yorumu geçerli; flags b7:6: 1 yok, 2 var, 3 çelişkili |

Yeni E1 30 s gelmezse ölçüm maskesi kaldırılır; E3 ölçüm yaşını yenilemez.
Bu süre RTU tercihidir. MH BOOT ölçümleri geçersiz kılar; RESET alarmları
sıfırlar. Ölçüm alanları ayrı değerlendirilir: denetleyici bayatken ADC
gerilimleri geçerli kalabilir. POWER/SYSTEM_POWER bitleri ilgili ölçüm
bloğunun eşleşmesini ve psys durumunu ayrıca gerektirir.

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
| 49345 | 9345 | cell_voltage_mv[1] | UINT16 | 1. hucre gerilimi (mV) |
| 49346 | 9346 | cell_voltage_mv[2] | UINT16 | 2. hucre gerilimi (mV) |
| 49347 | 9347 | cell_voltage_mv[3] | UINT16 | 3. hucre gerilimi (mV) |
| 49348 | 9348 | cell_voltage_mv[4] | UINT16 | 4. hucre gerilimi (mV) |
| 49349 | 9349 | cell_voltage_mv[5] | UINT16 | 5. hucre gerilimi (mV) |
| 49350 | 9350 | cell_voltage_mv[6] | UINT16 | 6. hucre gerilimi (mV) |
| 49351 | 9351 | cell_voltage_mv[7] | UINT16 | 7. hucre gerilimi (mV) |
| 49352 | 9352 | cell_voltage_mv[8] | UINT16 | 8. hucre gerilimi (mV) |
| 49353 | 9353 | cell_voltage_mv[9] | UINT16 | 9. hucre gerilimi (mV) |
| 49354 | 9354 | cell_voltage_mv[10] | UINT16 | 10. hucre gerilimi (mV) |
| 49355 | 9355 | cell_voltage_mv[11] | UINT16 | 11. hucre gerilimi (mV) |
| 49356 | 9356 | cell_voltage_mv[12] | UINT16 | 12. hucre gerilimi (mV) |
| 49357 | 9357 | cell_voltage_mv[13] | UINT16 | 13. hucre gerilimi (mV) |
| 49358 | 9358 | cell_voltage_mv[14] | UINT16 | 14. hucre gerilimi (mV) |
| 49359 | 9359 | cell_voltage_mv[15] | UINT16 | 15. hucre gerilimi (mV) |
| 49360 | 9360 | cell_voltage_mv[16] | UINT16 | 16. hucre gerilimi (mV) |
| 49361 | 9361 | temperatures_c[1] | INT16 | 1. sensor sicakligi (degC) |
| 49362 | 9362 | temperatures_c[2] | INT16 | 2. sensor sicakligi (degC) |
| 49363 | 9363 | temperatures_c[3] | INT16 | 3. sensor sicakligi (degC) |
| 49364 | 9364 | temperatures_c[4] | INT16 | 4. sensor sicakligi (degC) |
| 49365 | 9365 | temperatures_c[5] | INT16 | 5. sensor sicakligi (degC) |
| 49366 | 9366 | temperatures_c[6] | INT16 | 6. sensor sicakligi (degC) |
| 49367 | 9367 | temperatures_c[7] | INT16 | 7. sensor sicakligi (degC) |
| 49368 | 9368 | temperatures_c[8] | INT16 | 8. sensor sicakligi (degC) |
| 49369 | 9369 | balance_position[1] | UINT16 | Hucre bazli dengeleme bitmask (kelime 1) |
| 49370 | 9370 | balance_position[2] | UINT16 | Hucre bazli dengeleme bitmask (kelime 2) |
| 49371 | 9371 | balance_position[3] | UINT16 | Hucre bazli dengeleme bitmask (kelime 3) |
| 49372 | 9372 | fault_codes[1] | UINT16 | BMS hata/alarm kod sozcugu 1 |
| 49373 | 9373 | fault_codes[2] | UINT16 | BMS hata/alarm kod sozcugu 2 |
| 49374 | 9374 | fault_codes[3] | UINT16 | BMS hata/alarm kod sozcugu 3 |
| 49375 | 9375 | fault_codes[4] | UINT16 | BMS hata/alarm kod sozcugu 4 |
| 49376 | 9376 | fault_codes[5] | UINT16 | BMS hata/alarm kod sozcugu 5 |
| 49377 | 9377 | fault_codes[6] | UINT16 | BMS hata/alarm kod sozcugu 6 |
| 49378 | 9378 | fault_codes[7] | UINT16 | BMS hata/alarm kod sozcugu 7 |

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
| Guc karti telemetri | 9200 | 38 |
| BMS telemetri | 9300 | 79 |
| GSM durumu | 9400 | 9 |

> UINT32 (ABCD) alanlari icin master tarafinda "32-bit Unsigned" ve big-endian
> (ABCD) word order secilmelidir (bolum 4.1).

---

## 8. Okuma Ornekleri

### 8.1 Bir Fiderin Canli Blogunu Okuma (FC03)

Fider 1'in kalite ve uc faz canli verisini tek pencerede okumak icin:

| Alan | Deger |
|---|---|
| Slave ID | 23 |
| Function | 03 (Read Holding Registers) |
| Address (base-0) | 0 |
| Quantity | 76 |

Fider 2 icin Address = 1000; Fider 3 icin 2000; Fider 4 icin 3000.

### 8.2 Sadece Anlik Akimlari Okuma

Fider 1 fazlarinin anlik akimlarini ayri pencerelerle okumak icin L1 ornegi:

| Alan | Deger |
|---|---|
| Function | 03 |
| Address (base-0) | 5 |
| Quantity | 2 |

Donen 2 register FLOAT32 (L1) olarak yorumlanir; L2 icin Address = 29,
L3 icin Address = 53. Degerin gecerliligi ayni faz alt blogunun kalite
register'indan (Address 4 / 28 / 52) kontrol edilmelidir.

### 8.3 ModbusPoll Ayar Ozeti

- Connection: Serial, 115200 8N1
- Slave ID: 23
- Function: 03
- Address: ilgili base-0 adres (orn. fider blogu icin 0)
- Quantity: 76 (fider canli blogu) veya 8 (tek ariza kaydi)
- Display format: FLOAT32 alanlar icin "32-bit Float", word order **ABCD
  (big-endian)**; durum/sure alanlari icin "Unsigned 16-bit".

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

Register adresleri sabittir; yapilandirma yalnizca seri hat ayarlarini kapsar:

| Ayar | Varsayilan | Aralik | Notu |
|---|---|---|---|
| Slave adresi (CihazID) | 23 | 1..247 | Multidrop bus icin benzersiz olmali |
| Baud rate | 115200 | standart hizlar | Master ile eslesmeli |

Web arayuzu (Modbus Yaplandirmasi sayfasi) yalnizca bu iki ayari sunar;
fider kartlarindaki adres girileri kaldirilmis ve yerini sabit harita bilgi
satiri almistir.

## 11. Revizyon Gecmisi

| Surum | Tarih | Aciklama |
|---|---|---|
| 1.0 | 2026-06-13 | Ilk surum: bitisik float tabanli register haritasi |
| 1.1 | 2026-06-15 | Son hata kodu (SonHataKodu) takibi ve web arabirimi eklendi (bkz. 9.1) |
| 1.2 | 2026-06-15 | Son hata zamani (SonHataZamani, Unix epoch) eklendi; sistem geneli zaman tabani 1970 Unix epoch'a tasindi |
| 1.3 | 2026-07-14 | Sistem istatistik (49000) ve guc karti telemetri (49200) salt-okunur bloklari eklendi (bkz. 7) |
| 1.4 | 2026-09-04 | Hat sayisi 7'ye kesinlesti; 40700 ve uzeri desteklenmez (bkz. 5.1.1) |
| 1.5 | 2026-09-16 | BMS telemetri blogu (49300, dokumante edildi) ve GSM durum blogu (49400: GSM durum/CSQ/RAT, soket durumlari, SonHataKodu/SonHataZamani) eklendi |
| 1.6 | 2026-09-25 | Isimlendirme IEC104 tarafi ile uyumlu hale getirildi (hat/Line -> fider, R/S/T -> L1/L2/L3); 5.3'te tum fiderlerin tum alt adresleri kayit bazinda tek tek listelendi; BMS dizi alanlari (cell_voltage_mv, temperatures_c, balance_position, fault_codes) tek tek acildi; ibus_ma UINT16 -> INT16 duzeltildi; batt_temp_x10 sentinel degeri -9990 olarak duzeltildi; bolum 1'deki eski "8 hat" ifadesi 7 fider olarak duzeltildi; PowerBoard dokuman referansi guncellendi (I2C_SLAVE_ENTEGRASYON_16K.md -> PowerBoard_I2C_Protocol.md) |

| 1.7 | 2026-10-03 | 49009 reset_reason ham CSR yerine mevcut reset_source_flag_t bitmask anlamına geçirildi; adresler ve register genişlikleri korundu. |

| 1.8 | 2026-10-04 | Cihaz adresi 0 sunucuda reddedildi; pasif fider ve ayrılmış offset 27..99 için sıfır yanıtının anlamı açıklandı. |
| 1.9 | 2026-10-06 | SCP enerji/yük göstergeleri; yük alanı adları; 49500–49520 salt okunur kalite bloğu ve adres çakışması koruması. |

| 2.0 | 2026-10-15 | Register haritasi tamamen sabitlesti (modbus_map.h): adresler NVRAM/web uzerinden degistirilemez. Fider bloklari yeniden tasarlandi: 4 fider x 1000 register (40000-43999); IEC104 bilgi aynasi (anlik akim, enerji/yuk/rf/trip_failed + alan bazli zaman damgalari), SCADA'ya yonelik RF canli olcumleri ve fider/faz kalite register'lari tek blokta. Gecici/kalici ariza kayit listeleri (faz basina 15 kayit) Modbus tarafina eklendi. 49500 RF kalite blogu kaldirildi (kalite fider bloklarina gomuldu). Aku uyarisi/modem reset adresleri sabitlendi (50000/50001). Yapilandirilabilir ayarlar CihazID + baud rate e indirildi. |
| 1.10 | 2026-10-06 | Anlık arıza alanları kaldırıldı; offset 0–5/12–17 ayrıldı. Durum polariteleri SCP ile eşitlendi. |
