# Gerçek MH Duman Süiti (`rf_real_mh`)

HIL süiti (`test/system/rf_hil`) PC'deki simülatörün oynadığı MH'ye karşı
koşar; bu süit onun **karşıt uç** doğrulamasıdır: gerçek Modem RF Hub ve
gerçek ayırıcı kartlar. Gerçek hub bench'ten komutlanamadığı için her
denetim **gözleme dayalıdır** — DUT konsolunda ayrıntılı RF günlüğü
açılır ve gerçek hattın gerçekten ne teslim ettiği doğrulanır.

Bu süit, simülatörün yakalayamayacağı tek sınıfı hedefler: gerçek hub
davranışı. Örnek: 2026-10-09 gecesi VINCI'de görülen timetag-IV sorununun
kökü, gerçek MH'nin hiç LIVE_DATA göndermemesiydi (`m3_live_boot` bunu
doğrudan sınar).

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
| m2_inventory | `Envanter: YUKLU` + giriş sayısı | YUKLU değil = FAIL |
| m3_live_boot | LIVE_DATA akışı + telde Boot_Counter (gövde bayt 31–32, BQ-19) | akış yok = FAIL; sayaç 0 taşıyan kaynak = FAIL |
| m4_ring_consume | 0x47 zilinin 0x48 tüketmesiyle kapanması | zil var tüketme yok = FAIL; ikisi de yok = SKIP (bekleyen kayıt zorlanamaz) |
| m5_epoch_201 | `--epoch`: olay 201 (+120) kayıtları gerçek halkadan okunur | 100 s içinde 201 yok = FAIL |
| m6_bootless | `--reset-dut`: BOOT_NOTIFY gelmeden kurulum | BOOT geldi = FAIL (MH de yeniden başlamış; pencereyi kaydır) |
| m7_asdu_quality | `--104-host`: canlı akan fiderlerin GI ölçüm noktalarında kalite VE CP56 damga geçerli | canlı akış + tüm noktalar IV = FAIL (VINCI belirtisi); geçerli ölçüm + iv=1 damga = FAIL |

## Sınırlar

- Gerçek ayırıcı arızası/olayı üretilemediği için olay zinciri
  (101/142/201 sınıflaması) cihaz tarafında yalnız gerçek trafik olduğunda
  gözlemlenir; kapsamlı sınama HIL süitindedir.
- m5/m6 cihaz durumunu değiştirir (0x2A gönderir / cihazı resetler) —
  opt-in bayraklarla açılırlar.
