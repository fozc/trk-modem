# Gerçek MH Duman Süiti (`rf_real_mh`)

**Sürüm:** 1.1
**Tarih:** 2026-10-10

HIL süiti (`test/system/rf_hil`) PC'deki simülatörün oynadığı MH'ye karşı
koşar; bu süit gerçek MH ve ayırıcı kartlarla doğrulama yapar. Üretim
imajında MH trafiği RTU konsolundan gözlenir. m1–m4 ve m7 gözlem yapar;
m5/m6 açık opt-in ile epoch komutu gönderir veya RTU'yu resetler.

Bu süit, simülatörün yakalayamayacağı tek sınıfı hedefler: gerçek hub
davranışı. Örnek: VINCI'de geçersiz ölçüm/zaman damgası görülürken
LIVE_DATA akışı da yoktu. 10.10.2026 kullanıcı bilgisine göre AY–MH
bağlantısı henüz kurulmamıştı; bu gözlem MH firmware arızasını kanıtlamaz.
`m3_live_boot` akışı, m7 ise gerçek IEC104 kalite ve zaman damgasını sınar.

## Ön koşullar

1. Cihazda **üretim imajı** (`RTU image profile=0`). HIL bench imajı
   (profile=1) USART3 RF dağıtımını derlemeden çıkarır; süit bu durumda
   BLOCKED döner.
2. Gerçek MH, SCP hattına (USART3) bağlı ve ayakta.
3. Konsol portu erişilebilir (varsayılan COM16, 230400).

Aktif RF-SCP yolu **yüklenen imajın profili** belirler: RS-485 adaptörü
(COM10) ve gerçek MH aynı anda takılı kalabilir. Profile-0'da SCP
USART3'ten gerçek MH'ye gider (COM10 normal Modbus portu olur);
profile-1'de firmware gerçek hubu yok sayar ve SCP'yi COM10'daki
simülatöre yöneltir. Süitler arasında geçiş yeniden kablolama değil,
ilgili profilin imajını yüklemekle yapılır.

## Kullanım

```bash
python test/system/rf_real_mh/run_smoke.py --console COM16 \
    [--fw 022555bf] [--duration 60] [--epoch] [--reset-dut] \
    [--case m3_live_boot] [--list]
```

- `--fw`: beklenen MH firmware kimliği (varsayılan `022555bf`).
- `--epoch`: `rf epoch 1` gönderir — MH kartı değişimi senaryosu; olay
  201 + 120 kayıtlarının gerçek halkadan okunmasını doğrular (m5).
- `--reset-dut`: cihazı ST-Link ile resetler ve BOOT gelmeden kurulumu
  (BQ-17: GET_STATUS fw kimliği → TIME_SYNC → 0x40/0x48 → envanter)
  gerçek huba karşı doğrular (m6).
- `--case`: tek vaka seçimi (tekrarlı seçim kanıt üzerine yazdığı için
  reddedilir). Vaka verilmezse hepsi koşar.

Çıktı: `test/build/rf-real-mh/<zaman>/` altında `report.md`,
`results.json` ve `console.log`. Çıkış kodu: FAIL/ERROR → 1;
`--require-device` ile SKIP/BLOCKED da başarısız sayılır.

## Vaka kataloğu

| Vaka | Ne doğrular | Sonuç mantığı |
|---|---|---|
| m1_hub_fw | GET_STATUS yanıtında beklenen fw kimliği | farklı kimlik = FAIL; yanıt yok = SKIP (MH bağlı değil olabilir) |
| m2_inventory | RTU'da `Envanter: YUKLU` durumu | YUKLU değil = FAIL; fiziksel AY/EUI eşleşmesi ve giriş sayısını doğrulamaz |
| m3_live_boot | LIVE_DATA akışı + telde Boot_Counter (gövde bayt 31–32, BQ-19) | akış yok = FAIL; sayaç 0 taşıyan kaynak = FAIL |
| m4_ring_consume | 0x47 zilinin 0x48 tüketmesiyle kapanması | zil var tüketme yok = FAIL; ikisi de yok = SKIP (bekleyen kayıt zorlanamaz) |
| m5_epoch_201 | `--epoch`: olay 201 (+120) kayıtları gerçek halkadan okunur | 100 s içinde 201 yok = FAIL |
| m6_bootless | `--reset-dut`: BOOT_NOTIFY gelmeden kurulum | BOOT geldi = FAIL (MH de yeniden başlamış; pencereyi kaydır) |
| m7_asdu_quality | `--104-host`: canlı akan fiderlerin GI ölçüm noktalarında kalite VE CP56 damga geçerli | canlı akış + tüm noktalar IV = FAIL (VINCI belirtisi); geçerli ölçüm + iv=1 damga = FAIL |

## Sınırlar

- `rf inv` bir listeleme komutu değildir; MH'ye envanteri yeniden yükler.
  m2 yalnız `rf status` okur. Envanter yükleme onayı, ayırıcıyla RF
  bağlantısının kurulduğunu kanıtlamaz. Gerçek EUI-64, fider/faz ataması
  ve AY–MH bağlantısı ayrı doğrulanmalıdır. R2 `channel` alanı yalnız
  bilgi amaçlıdır.
- AY–MH bağlantısı kurulmamış bir bench'te LIVE_DATA yokluğu firmware
  arızası olarak bildirilmemelidir. m3/m5/m7 için bu önkoşul sağlanmalıdır;
  eski rapordaki m3 FAIL böyle bir kurulumda kabul engeli olarak ayrı
  değerlendirilmelidir. m4'te zil yoksa tüketme yolu sınanmamıştır.
- Merkezi paket, cihaz hedefi verilmeden önce dört host selftest'i
  çalıştırır. Doğrudan komut: `python test_smoke_selftest.py`.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-10 | 1.1 | AY bağlantı önkoşulu, m2 salt gözlem kapsamı ve merkezi host selftest'leri |
- Gerçek ayırıcı arızası/olayı üretilemediği için olay zinciri
  (101/142/201 sınıflaması) cihaz tarafında yalnız gerçek trafik olduğunda
  gözlemlenir; kapsamlı sınama HIL süitindedir.
- m5/m6 cihaz durumunu değiştirir (0x2A gönderir / cihazı resetler) —
  opt-in bayraklarla açılırlar.
