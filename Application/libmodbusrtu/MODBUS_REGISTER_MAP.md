# Modbus RTU Register Haritasi ve Haberlesme Kilavuzu

> Smart Breaker Modem - Modbus RTU Slave Arabirimi
> Bu dokuman hem musteri tarafindan SCADA/RTU entegrasyonu icin, hem de
> firmware gelistirme tarafinda kaynak olarak kullanilmak uzere hazirlanmistir.

| | |
|---|---|
| Dokuman surumu | 1.1 |
| Tarih | 2026-06-15 |
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
| Line 8 | 7 | 40700 | 700 |

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
| Line 8 | 40700 .. 40726 | 700 .. 726 | Address 700, Quantity 27 |

> **Aktif olmayan hatlar:** Cihaz en fazla 8 hat destekler ancak sahada hepsi
> aktif olmayabilir. Aktif olmayan bir hattin register araligi icin cihazin
> davranisi su sekildedir: (bu davranis firmware tarafinda netlestirilecektir -
> bkz. dokuman notu). Onerilen davranis: aktif olmayan hat icin tum register'lar
> 0 doner (boylece master sabit pencereyle hatasiz okur ve "veri yok" durumunu
> degerden anlar).

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

## 7. Okuma Ornekleri

### 7.1 Bir Hattin Tum Verisini Okuma (FC03)

Line 1'in tum canli verisini tek pencerede okumak icin:

| Alan | Deger |
|---|---|
| Slave ID | 23 |
| Function | 03 (Read Holding Registers) |
| Address (base-0) | 0 |
| Quantity | 27 |

Line 2 icin Address = 100, Quantity = 27; Line 3 icin Address = 200; ...

### 7.2 Sadece Anlik Akimlari Okuma

Line 1 anlik akim (R/S/T) icin:

| Alan | Deger |
|---|---|
| Function | 03 |
| Address (base-0) | 6 |
| Quantity | 6 |

Donen 6 register, 3 adet FLOAT32 (R, S, T) olarak yorumlanir.

### 7.3 ModbusPoll Ayar Ozeti

- Connection: Serial, 115200 8N1
- Slave ID: 23
- Function: 03
- Address: ilgili base-0 adres (orn. 0)
- Quantity: 27
- Display format: FLOAT32 alanlar icin "32-bit Float", word order **ABCD
  (big-endian)**; durum/sure alanlari icin "Unsigned 16-bit".

---

## 8. Exception (Hata) Yanitlari

Cihaz, gecersiz isteklere Modbus standart exception kodlari ile yanit verir:

| Kod | Ad | Anlam |
|---|---|---|
| 0x01 | ILLEGAL FUNCTION | Desteklenmeyen fonksiyon kodu |
| 0x02 | ILLEGAL DATA ADDRESS | Istenen adres haritada tanimli degil |
| 0x03 | ILLEGAL DATA VALUE | Gecersiz yazma degeri veya istek sayisi |

> Onemli: Tek bir FC03 okuma penceresinde **tanimsiz** bir adres bulunursa,
> cihaz tum istek icin `0x02` doner. Bu nedenle okuma pencereleri yukaridaki
> bitisik blok sinirlari icinde tutulmalidir (orn. Address 0, Quantity 27).

### 8.1 Son Hata Kodu (SonHataKodu)

Cihaz, gonderdigi **son exception kodunu** dahili olarak saklar. Bu deger her
yeni exception yanitinda guncellenir; basarili istekler degeri **degistirmez**,
yani alan acilistan bu yana olusan en son hatayi yansitir (otomatik silinmez).

Deger su sekilde gozlenir:

| Arabirim | Alan | Aciklama |
|---|---|---|
| Web (HTTP) | `SonHataKodu` (`GET /r?modbusConfigs` JSON) | Son exception kodunu UINT8 olarak doner |

Deger anlamlari, yukaridaki exception kodlari ile aynidir:

| SonHataKodu | Anlam |
|---|---|
| 0x00 | Acilistan bu yana hata olusmadi |
| 0x01 | Son hata: ILLEGAL FUNCTION |
| 0x02 | Son hata: ILLEGAL DATA ADDRESS |
| 0x03 | Son hata: ILLEGAL DATA VALUE |

> Not: Bu alan tani/izleme amaclidir; Modbus register haritasinda yer almaz,
> yalnizca web arabiriminden okunur.

---

## 9. Yapilandirilabilir Ayarlar Ozeti

| Ayar | Varsayilan | Aralik | Notu |
|---|---|---|---|
| Slave adresi | 23 | 1..247 | Multidrop bus icin benzersiz olmali |
| Baud rate | 115200 | standart hizlar | Master ile eslesmeli |
| Aku uyarisi register | 50000 | - | Ozel register adresi |
| Modem reset register | 50001 | - | Ozel register adresi |

---

## 10. Revizyon Gecmisi

| Surum | Tarih | Aciklama |
|---|---|---|
| 1.0 | 2026-06-13 | Ilk surum: bitisik float tabanli register haritasi |
| 1.1 | 2026-06-15 | Son hata kodu (SonHataKodu) takibi ve web arabirimi eklendi (bkz. 8.1) |
