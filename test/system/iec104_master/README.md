# IEC-104 Master Kabul Süiti (c104) — test/system/iec104_master

Cihazın IEC-104 dinleyicisini (GSM üzerinden, varsayılan port 2404)
bağımsız bir master stack'i ile uçtan uca doğrular. Master tarafı
[c104](https://pypi.org/project/c104/) 2.2.1 (lib60870-C v2 çekirdeği,
GPLv3) — kütüphane **repoya girmez**, bench-lokal venv'e kurulur;
kurulu değilse süit SKIP döner. Negatif/fault kapsaması bu süitte
değil, Ceedling'dedir (`test/iec104/test_iec104_protocol_scenario.c`).

## Kurulum (bir kez)

```bash
# Python 3.13 (wheel'ler 3.13'te bitiyor; sistem 3.14 olabilir)
py -3.13 -m venv test/system/iec104_master/.venv
test/system/iec104_master/.venv/Scripts/python.exe -m pip install \
    --only-binary=:all: c104==2.2.1 "pyserial>=3.5,<4"
```

`pyserial` yalnız `--console` kullanılıyorsa gerekir (konsol yardımcısı
`tools/hwtest/serial_io.py` üzerinden import eder).

`.venv` gitignored'dır; silinmesi süiti bozmaz (SKIP döner).

**Ortam notları:**
- venv bağımsız bir kurulum DEĞİLDİR; `pyvenv.cfg` üzerinden base
  Python 3.13'e bağlıdır — 3.13 kaldırılırsa venv çalışmaz
  ([venv belgesi](https://docs.python.org/3/library/venv.html)).
- 3.13 per-user kurulumu kullanıcı PATH'ine üç giriş ekler; bunlar
  süit için gereksizdir (venv mutlak yol, `py -3.13` kayıt defteri
  etiketiyle çözülür) ve istenirse kullanıcı PATH'inden silinebilir:
  `...\Python313\Scripts\`, `...\Python313\`, `...\Python\Launcher\`.
- Donanımsız doğrulama: `python test_apdu_selftest.py` (22 vektör;
  GI20 zaman etiketsiz 13/1, çoklu/kesilmiş nesneler ve negatif onay
  P/N biti dahil), `python test_cases_selftest.py` (19 vaka kontrolü).
  Integration sarmalayıcısı bunları her koşuda önce çalıştırır.

## Kullanım

```bash
# vaka listesi (venv gerekmez)
python test/system/iec104_master/run_suite.py --list

# tam süit (hedef açıkça verilir; varsayılan IP YOK)
python test/system/iec104_master/run_suite.py --host 188.59.76.59

# konsollu + kabul modu + opt-in mutasyonlar
python test/system/iec104_master/run_suite.py --host 188.59.76.59 \
    --console COM16 --require-device --setup-feeders \
    --clock-sync --mutate-eventlog

# merkez koşudan (wrapper): hedef yoksa SKIP
IEC104_DEVICE_HOST=188.59.76.59 ruby test/run_all.rb integration
```

Cihaz IP'si konsoldan `gsm status` ile bulunur. Süit tek istemcili
dinleyiciye sahip olduğu için çalışırken başka 104 istemcisi
bağlanmamalıdır.

## Vaka kataloğu

| Vaka | Doğrulanan | Koşul |
|---|---|---|
| i01_startdt_mei | STARTDT_ACT→CON→M_EI_NA_1(70) COT=4 CA IOA=0 | cihaz |
| i02_gi_station | QOI=20: ACT_CON→tam IOA kümesi→ACT_TERM; kalite beklentisi bench RF durumuna göre | aktif hat |
| i03_gi_groups | QOI=21/22 kesin küme; QOI=23/24 ACT_CON/TERM + arıza IOA bölgesi | aktif hat |
| i04_clock_sync | C_CS_NA_1 ACT_CON + konsol zaman damgasıyla apply | `--clock-sync` + `--console` |
| i05_testfr_idle | cihaz t3 TESTFR_ACT → master CON → GI canlılığı (master t3'ü cihazdan büyük seçilir) | cihaz |
| i06_replay_reconnect | sentetik kayıt replay: içerik + en-yeni-önce sıra + unsent tükenmesi | `--mutate-eventlog` + `--console` |
| i07_replay_retention | replay ortasında link kesintisi: kayıt kaybı yok (de5604c), pencere 2 en-yeni-önce | `--mutate-eventlog` + `--console` |

## Sonuç ve çıkış kodları

- Özet: `PASS=x FAIL=y SKIP=z DEFERRED=w` + verdict satırı
  (`ACCEPTANCE PASS` / `PARTIAL (SKIP/DEFERRED var)` / `FAIL`).
- FAIL → exit 1. `--require-device` ile SKIP/DEFERRED de exit 1.
- Hedef verilmediyse SKIP + exit 0 (merkezi koşu bozulmaz).

## Kanıt

`test/build/iec104-master/<zaman>/`: `apdu.jsonl` (her APDU: yön, ham
hex, monotonic zaman, ASDU çözümü; U/S çerçeveler dahil),
`manifest.json` (git commit, c104/py sürümü, cihaz oracle anlık
görüntüsü: port/CA/T0-T3/K/W/satır IOA haritası, vaka sonuçları),
`console.log` (konsol verildiyse).

## Oracle ve sınırlar

- i06/i07, tek kayıt için master ACK'sini bekleyen RTU replay akışını
  destekler. Kayıtlar arası sessizlik sınırı gerçek master t2
  (`confirm_interval`) + 6 s gözlem payıdır. Toplama bütçesi ilk gelen
  kayıttan sonra başlar ve kayıt sayısına göre genişler. Bu bir test
  bekleme payıdır; protokol zamanlayıcıları değiştirilmez.
- Beklenen IOA kümesi cihazın **canlı yapılandırmasından** üretilir
  (web `/config/iec104` Hatlar bölümü + `/config/rf`); doc tabloları
  tek başına oracle değildir (bir kısmı eski).
- Kalite semantiği `iec104_process.c` read_anlik_akim'a göre: hub
  offline iken akım değerleri IV zorunludur (NT seçimlik); online
  iken kalite dağılımı kanıt olarak kaydedilir.
- `--setup-feeders` RF store'u fider 1,2/bölge 1 ile doldurur;
  `--mutate-eventlog` `iec104evtlog test N` ile Flash'a kayıt yazar;
  `--clock-sync` cihaz RTC'sini host saatine çeker. Üçü de bilinçli
  bench mutasyonudur, varsayılan kapalıdır.
- 2404'e inbound erişim yoksa otomatik port değişikliği yapılmaz:
  teşhisle başarısız kabul edilir (operatör filtresi raporlanır).
- Oracle bir yazılım stack'idir (lib60870); gerçek SCADA donanımı ve
  standart uygunluk (conformance) testi kapsam dışıdır.

## 19.10.2026 gerçek cihaz kabul kapsamı

**Amaç:** Desteklenen işlev, host testi, simülatör ve gerçek donanım
kanıtını ayırmak. Bu tablo tam üretim kabulü veya coverage yüzdesi değildir.

**Kullanım yeri:** SCADA kabul planı ve test eksiği takibi.

**Kaynak:** `Application/libiec104/iec104.c`, `iec104_process.c`,
`iec104_replay.c`, `rf/rf_alarm.c`; mevcut Ceedling senaryoları ve
`.temp_files/bolatex-20261019/` ham koşum kayıtları.

| İşlev / senaryo | Bu turdaki gerçek cihaz kanıtı | Açık kapsam |
|---|---|---|
| STARTDT ve başlangıç bildirimi | i01 PASS | Gerçek saha SCADA ürünüyle uyumluluk ayrıca |
| GI20, anlık akım/enerji/yük/RF/trip durumu | i02 PASS: iki etkin fiderde 30 IOA, type=13/1, CA/COT ve onay sırası | Her fiziksel fazın gerçek değeri, kalite değişimi ve bütün fider kapasiteleri ayrı doğrulanmalı |
| GI21/22 grup sorguları | i03 PASS: adres kümeleri ve type=36/30 | Gerçek akım/enerji değişimiyle sayısal karşılaştırma yok |
| GI23/24 arıza geçmişi | i03 PASS: onay/sonlandırma, veri tipi ve adres bölgesi | Beklenen her kaydın akım/süre/zaman/sınıfı ve 15+15 sınırı bağımsız veri kümesiyle birebir karşılaştırılmadı |
| Bağlantı yokken kayıt, bağlanınca otomatik replay | i06 PASS: konsoldan beş sentetik Flash kaydı; yeni GI gönderilmeden fallback replay, içerik/sıra, unsent=0 ve kapanış snapshot | Gerçek AY arızasının bağlantı kapalıyken baştan sona bu yoldan geçmesi ayrı kabul |
| Replay ortasında bağlantı kesilmesi | i07 PASS: ilk pencerede 1, ikinci pencerede 4 kayıt; 5/5, doğru sıra, unsent=0 | Seçici master ACK kaybı, gerçek güç kesintisi ve RTU restart fiziksel olarak aynı şey değildir |
| Bağlı SCADA'ya yeni arıza/alarmın anlık spontane iletimi | Host ACK/durable replay ve RF/HIL alarm testleri mevcut | Bağlantı açıkken yeni olay üretip GI/reconnect olmadan COT=3, içerik ve ACK sonrası kalıcılığı birlikte doğrulayan cihaz koşusu yok |
| Gerçek çoklu 101/105, ACTIVE→CLEARED ve üç faz | Simülatör ve host karşılıkları var | Gerçek akım düzeneği, doğru EUI eşlemeleri ve üçüncü AY gerekli; TEST137 gerçek alarm değildir |
| Saat set etme: C_CS_NA_1 (103) | i04 var, bu tur koşulmadı; host geçerli/geçersiz saat ve RTC reddi testleri var | Ham gönderilen zaman → ACT_CON → RTC uygulaması; ret halinde saatin korunması cihazda doğrulanmalı |
| t3 TESTFR ve cevapsız t1/t2 davranışı | i05 var, bu tur koşulmadı; host zamanlayıcı/pencere/ACK testleri var | Cihazda idle probe, cevapsız master ve toparlanma koşuları eksik |
| Reset: C_RP_NA_1, QRP=1 | Üretim handler'ı ve host onay/ret testleri var | SCADA onayı hatta çıktıktan sonra gerçek reset ve pending kayıtların dönüşü bu turda doğrulanmadı |
| Desteklenmeyen tip/CA/IOA/COT ve bozuk/parçalı APDU | Host protokol testleri var | Gerçek bağlantıda olumsuz yanıt ve durumun korunması için ayrı negatif paket gerekli |
| Kuyruk/pencere doluluğu, ACK sınırı ve restart | Host ACK-delivery, durable replay ve protokol testleri var | Fiziksel K/W, yavaş master, uzun süre yük, ACK anında güç kesilmesi açık |

### İşlev sınırları: test eklemek tek başına tamamlamaz

- Single/double command 45/46 aktif destek listesinde değildir. SCADA
  üzerinden kesici aç/kapa veya SBO çalışıyor kabul edilmemelidir.
  Host testleri bu komutların olumsuz yanıtını doğrular.
- Genel setpoint 48/49/50 için aktif handler bulunmaz. Web'de ayar
  bulunması, aynı ayarın IEC104 üzerinden set edilebildiğini göstermez.
- QRP=2 mevcut handler'da olumlu onay döndürür, bekleyen olayları
  temizlemez; kodda TODO vardır. Kayıt silme davranışı ürün/protokol
  kararıyla netleştirilmeden tamamlanmış işlev sayılmamalıdır.
- `iec104_periodic_send()` veri göndermeyen TODO gövdesidir. Akım,
  enerji ve yük durumunun her değişimde/periyotta kendiliğinden
  yayımlanması, arıza/alarm için çalışan COT=3 yoluyla karıştırılmamalıdır.
- Akü uyarısı IOA'sı yapılandırılır; aktif yayın çağrı yolu bulunmamıştır.
- `rf_real_mh` m7 kalite testi bu turda koşulmadı. Mevcut hali GI20
  isteyip zaman etiketli type=36 bekler; güncel GI20=13/1 sözleşmesiyle
  uyumlu değildir. Zaman etiketi kabulü için GI21 yoluna uyarlanmalı ve
  geçerli/geçersiz kalite ile eksik/IV zaman damgası regresyonları eklenmelidir.

### Kanıt ve sonraki sıra

İmaj: CRC `0xD86D1FED`, profile=0, 354788 B; üretim kaynak hash'leri
paketle eşleşir. Kaynak ağacı o sırada kirli olduğundan yalnız Git
kimliği imajın tam tarifi değildir.

`iec104-repeat/manifest.json` i01/i03/i06/i07 PASS, eski console oracle
nedeniyle i02 FAIL içerir. Etkilenen i02, okuyucu düzeltildikten sonra
`iec104-gi-final/manifest.json` içinde PASS oldu. Beş vaka tek koşuda
geçmiş gibi sunulmamalıdır. `iec104-acceptance-index.json` bu iki
kanıtı birleştirir. Ayarlar `restore-verification.json` ile geri
doğrulandı; test kayıtları silinmedi.

Kapanış sırası:

1. Gerçek cihaz test aracındaki m7 uyumsuzluğu giderilmeli; açık
   bağlantıda yeni olay ve ACK kaybı senaryoları mevcut süite eklenmelidir.
2. i04/i05 ve desteklenen reset/olumsuz komutlar cihazda doğrulanmalıdır.
3. Bağlantı yokken gerçek AY olayı, çoklu alarm ve gerçek güç kesintisi
   aynı son imajda doğrulanmalıdır.
4. Genel setpoint/çıkış kontrolü, QRP=2, periyodik yayın ve akü uyarısının
   ürün kapsamı netleştirilmelidir. Destek eklenecekse mevcut kapalı
   alt sistem kararları korunarak ayrıca uygulanıp kabul edilmelidir.
