# Modbus Poll .mbp Dosya Hazirlama Rehberi

> Bu klasordeki `.mbp` dosyalari **Modbus Poll** (Witte Software, v13.2.1.2558)
> tarafindan acilip kaydedilen XML yapilaridir. Cihaza yeni bir Modbus blok
> eklendiginde (orn. `modbus_*_stats.c`), ona karsilik gelen `.mbp`'yi bu
> rehbere gore hizlica uretebilirsin.

Firmware tarafindan yayinlanan bloklari ve adreslerini her zaman
[MODBUS_REGISTER_MAP.md](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md)
ile es tut.

---

## 1. Altin Kural: Sayimlar Uyumlu Olmali

Bir `.mbp`'yi Modbus Poll hatasiz acmak icin **bes adet** birbiriyle uyumlu
olmalidir. Hepsinin ortak degeri **N = register (satir) sayisi** = `<Quantity>`:

| Element | Gerekli adet | Aciklama |
|---|---|---|
| `<Quantity>` | **N** | Poll penceresindeki register sayisi |
| `<RowCount>` | **N** | Grid satir sayisi |
| `<RowHight>` icinde `<RH>` | **N** adet | Her satir icin bir yukseklik (200) |
| `<Formats>` icinde `<F>` | **N** adet | Her register icin bir format |
| `<Bytes>` icinde `<B>` | **2N** adet | Her 16-bit register = **2 byte** ← en sik hata |
| `<CellData>` icinde `<Cell idx="i">` | **N** adet | Her register icin bir hucre (32-bit dusuk kelime atlanabilir) |

Ayrica sabitler: `<ColCount>` = `2` ve `<ColumnWidth>` icinde **2** adet `<CW>`.

> **Yaygin hata:** `<Bytes>` adedini N yerine 2N yazmayi unutmak. Modbus Poll
> o zaman `File error: bytes count mismatch: Line ...` verir. N register icin
> daima **2 x N** adet `<B>` gereklidir (her register 16-bit = 2 byte).

Kontrol formulu:
```
RowCount == Quantity == #(<F>) == #(<RH>) == #(<Cell>)
#(<B>) == 2 * Quantity
```

---

## 2. Adresleme

Bu projede holding register referanslari **40000 tabanli** (mantiksal) tutulur.
`.mbp` ise telde giden **PDU (base-0)** adresini ister:

```
<Address>  =  mantiksal_taban  -  40000          (OneBased = 0)
```

| Blok | Mantiksal | `<Address>` (PDU) | `<Quantity>` |
|---|---|---|---|
| Sistem istatistik | 49000 | 9000 | 17 |
| Guc karti telemetri | 49200 | 9200 | 32 |

Sabit ayarlar: `<SlaveID>23</SlaveID>`, `<Function>3</Function>` (FC03),
`<OneBased>0</OneBased>`, `<ScanRate>1000</ScanRate>`.

> Cihazin SlaveID'si NVRAM'den okunur (varsayilan 23). Farkli ise
> `<SlaveID>`'yi guncelle.

---

## 3. Format Kodlari (`f=`)

Sadece **gorunumu** etkiler; poll edilen veriyi degil. Repo'da dogrulanmis
kodlar:

| `f=` | Tip | Baslangic `v=` ornegi | Kullanim |
|---|---|---|---|
| `"1"` | Unsigned 16-bit (decimal) | `v="0"` | Olcumler, sayilar, durumlar (varsayilan) |
| `"2"` | Hex 16-bit | `v="0x0"` veya `v="0x0000"` | Bitfield / fault / status kelimesi |
| `"17"` | 32-bit Unsigned (ABCD) | `v="0"` | UINT32 **yuksek kelime** |
| `"16"` | (32-bit eslerinden) | `v="--"` | UINT32 **dusuk kelime** (isimsiz birakilir) |
| `"0"` | Signed 16-bit | `v="0"` | Negatif deger tasiyabilir (sicaklik vb.) |

- Baslangic `v=` degeri onemli degil: Modbus Poll dosyayi acinca gercek
  degerlerle doldurur ve kaydederken normalize eder (orn. `0x0` -> `0x0000`).
- **Isaretli** (INT16) alanlar icin `f="0"` secilebilir veya `f="1"` birakilip
  Modbus Poll arayuzunden "Signed" yapilabilir; hucre adina `(signed)` notu
  dusmek pratiktir.
- 32-bit ABCD cifti: yuksek kelime `f="17"`, dusuk kelime `f="16"` ve dusuk
  kelimeye `<Cell>` vermezsin (isim "--" gorunur). Ornek icin `system_stats.mbp`
  idx 6/7 ve 14/15.

---

## 4. Hizli Yol: Var Olan Dosyayi Kopyala (onerilen)

En hizli ve en az hatali yontem: **`power_stats.mbp` (veya `system_stats.mbp`)
kopyasini al**, sonra sadece asagidakileri degistir:

1. Dosya adini ve `<RowCount>`/`<Quantity>` = yeni **N**.
2. `<Address>` = yeni mantiksal taban - 40000.
3. `<RowHight>`: N adet `<RH>200</RH>` (fazlaysa sil, azsa ekle).
4. `<Formats>`: N adet `<F>` (tipine gore `f="1"` / `f="2"`). sayiyi N yap.
5. `<Bytes>`: **2 x N** adet `<B>0</B>`. sayiyi 2N yap. ← burada yanlisan en sik hata.
6. `<CellData>`: N adet `<Cell idx="0..N-1">`, her birine `<Name>`.
7. Kaydet, Modbus Poll'da ac; degerleri kendisi doldursun.

> Tum sabit bloklari (`<WP>`, `<LogText>`, `<LogExcel>`, `<Scales>`, `<ValueNames>`,
> `<ChartSeries>`, `<BinNames>` vb.) **oldugu gibi** birak.

---

## 5. Sifirdan Sablon (N = 4 ornek)

Asagidaki sablon N=4 icin dogrulanmistir: 4 `<F>`, 8 `<B>`, 4 `<Cell>`, 4 `<RH>`.
Buyuk N icin ilgili bloklari cogalt.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ModbusPoll>
   <FileSchema r="0" c="1"/>
   <Version major="13" minor="2" patch="1" build="2558"/>
   <dpi>96</dpi>
   <WP left="547" right="898" top="-1" bottom="646" ShowCmd="1" MaxPosX="-1" MaxPosY="-1" MinPosX="-1" MinPosY="-1"/>
   <ScanRate>1000</ScanRate>
   <SlaveID>23</SlaveID>
   <Enable>1</Enable>
   <StopOnError>0</StopOnError>
   <OneBased>0</OneBased>
   <RowsDialog>4</RowsDialog>
   <HideNames>0</HideNames>
   <HexMode>0</HexMode>
   <DisplayAddr>0</DisplayAddr>
   <ColCount>2</ColCount>
   <RowCount>4</RowCount>
   <ColumnWidth>
      <CW>1620</CW>
      <CW>1230</CW>
   </ColumnWidth>
   <RowHight>
      <RH>200</RH>
      <RH>200</RH>
      <RH>200</RH>
      <RH>200</RH>
   </RowHight>
   <ScrollPosV>0</ScrollPosV>
   <ScrollPosH>0</ScrollPosH>
   <FocusRow>0</FocusRow>
   <FocusCol>2</FocusCol>
   <LogText>
      <Eachread>0</Eachread><Rate>1</Rate><LogChangedOnly>0</LogChangedOnly>
      <LogErrors>0</LogErrors><LogErrorsOnly>0</LogErrorsOnly><LogAddress>1</LogAddress>
      <LogDate>0</LogDate><TDelimiter>0</TDelimiter><LogMs>1</LogMs><Delimiter>0</Delimiter>
      <AutoStart>0</AutoStart><Flush>0</Flush><Append>0</Append>
      <NewLogFileAtMidnight>0</NewLogFileAtMidnight><InsertHeader>0</InsertHeader>
      <NameCellsInTopRow>0</NameCellsInTopRow><PollDefinition>0</PollDefinition>
      <LogName>Type log name here</LogName><FileName></FileName>
   </LogText>
   <LogExcel>
      <Eachread>1</Eachread><Rate>1</Rate><StopAfter>1000</StopAfter>
      <LogChangedOnly>0</LogChangedOnly><InsertHeader>1</InsertHeader>
      <NameCellsInTopRow>1</NameCellsInTopRow><PollDefinition>1</PollDefinition>
      <LogName>Type log name here</LogName>
   </LogExcel>
   <Data>
      <Function>3</Function>
      <Address>9200</Address>
      <Quantity>4</Quantity>
      <EnronMode>0</EnronMode>
      <Formats>
         <F f="1" v="0"/>
         <F f="1" v="0"/>
         <F f="2" v="0x0"/>
         <F f="1" v="0"/>
      </Formats>
      <Bytes>
         <B>0</B><B>0</B><B>0</B><B>0</B>
         <B>0</B><B>0</B><B>0</B><B>0</B>
      </Bytes>
      <CellData>
         <Cell idx="0"><Colors/><Compare compare1="0" compare2="0" conditional1="0" conditional2="0" cnc="0"/><Name>Alan 0</Name><Font used="false"/></Cell>
         <Cell idx="1"><Colors/><Compare compare1="0" compare2="0" conditional1="0" conditional2="0" cnc="0"/><Name>Alan 1</Name><Font used="false"/></Cell>
         <Cell idx="2"><Colors/><Compare compare1="0" compare2="0" conditional1="0" conditional2="0" cnc="0"/><Name>Alan 2</Name><Font used="false"/></Cell>
         <Cell idx="3"><Colors/><Compare compare1="0" compare2="0" conditional1="0" conditional2="0" cnc="0"/><Name>Alan 3</Name><Font used="false"/></Cell>
      </CellData>
      <Scales/>
      <ValueNames/>
      <ChartSeries/>
      <BinNames/>
   </Data>
</ModbusPoll>
```

> `<Bytes>` icinde `<B>`'leri ayni satira yan yana yazabilirsin; Modbus Poll
> kaydederken her birini ayri satira donusturur. Onemli olan **sayinin 2N** olmasi.

---

## 6. Adim Adim Tarif

1. **Bloku tanimla:** mantiksal taban adresi ve register sayisi N
   (firmware'deki `modbus_*_stats.c` layout struct'i ile ayni sirada).
2. **Adresi hesapla:** `<Address>` = mantiksal taban - 40000.
3. **Sablonu cogalt:** bolum 4 (kopyala) veya bolum 5 (sifirdan).
4. **N degerlerini gir:** `<Quantity>`, `<RowCount>` = N; `<RowHight>` = N adet.
5. **Formatlari yaz:** N adet `<F>` (olcum `f="1"`, bitfield `f="2"`,
   32-bit yuksek `f="17"` + dusuk `f="16"`, signed `f="0"`).
6. **Byte'lari yaz:** **2N** adet `<B>0</B>`.
7. **Hucreleri adlandir:** N adet `<Cell idx="0..N-1">` + `<Name>`.
8. **Kaydet ve ac:** Modbus Poll gercek degerleri doldurur; tekrar kaydedince
   dosyayi normalize eder.

---

## 7. Acilista Hata Alirsan

| Hata mesaji | Sebep | Cozum |
|---|---|---|
| `bytes count mismatch` | `<B>` adedi != 2 x `<Quantity>` | `<Bytes>`'i 2N yap (bolum 1) |
| Grid yanlis/eksik satir | `<RowCount>` veya `<RH>` != N | `<RowCount>` ve `<RowHight>`'i N yap |
| Hucre isimleri kaymis | `<Cell idx>` 0..N-1 degil veya atland | idx'leri 0'dan baslat, araliksiz |
| Adres bulunamadi (exception 0x02) | `<Address>` yanlis | mantiksal taban - 40000 oldugunu kontrol et |
| Slave yanit vermiyor | `<SlaveID>` yanlis | cihazin NVRAM adresiyle eslestir (vars. 23) |

---

## 8. Klasordeki Dosyalar

| Dosya | Icerik | Taban (mantiksal / PDU) |
|---|---|---|
| `system_stats.mbp` | Sistem istatistikleri (RTC, uptime, gerilim...) | 49000 / 9000 |
| `power_stats.mbp` | Guc karti (PowerBoard) telemetrisi | 49200 / 9200 |
| `Mbpoll1.mbp` | Hat olcum blogu ornegi (Line 1, base 0) | 40000 / 0 |
| `hat2.mbp` | Hat olcum blogu ornegi | - |

Yeni bir blok eklerken bu tabloyu ve
[MODBUS_REGISTER_MAP.md](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md)
bolum 7'yi guncellemeyi unutma.
