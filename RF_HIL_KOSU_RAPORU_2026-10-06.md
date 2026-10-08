# RF HIL Koşu Raporu — 06.10.2026

| | |
|---|---|
| Rapor tarihi | 06.10.2026 (gün sonu kapanışı) |
| Kapsam | RF hub (MH) simülatörü ve HIL test süütünün 06.10.2026 tarihindeki tüm koşuları, düzeltmeleri ve sonuçları |
| Dayanak plan | `RF_HUB_SIM_HIL_TEST_PLANI_2026-10-06.md` v1.2 |
| Sonuç | Seçili uygulanmış bench vakaları **26/26 PASS (exit=0)**; donanımsız self-test **57/57**. Ertelenmiş senaryolar ve üretim UART3 kabulü bu sonuçta yoktur |
| Cihaz durumu | HIL bench imajı yüklü (`RF_SCP_OVER_MODBUS_PORT=1` derlemesi); üretime dönüş = 0 ile derle + xmodem |

## 1. Amaç ve kullanım yeri

Bu rapor, gün içinde `test/system/rf_hil/` altında yapılan **tüm** HIL
koşularının tek değerlendirmesidir: koşu envanteri, her dönemin hata
nedenleri (cihaz mı, harness mi), DUT (test edilen modem) davranış
bulguları ve kalan işler. Tekil koşu raporları ve izler
`test/build/hil-rf/<koşu-kimliği>/` altında korunmaktadır; bu belge
onların yerine geçmez, üst özetidir.

## 2. Ortam ve kurulum

- **DUT**: troika-smart-breaker-modem, imzalı `.efw` xmodem yoluyla
  yüklendi (bootlog `FW_INSTALL_OK` #923, 17:12; sonraki yenilemeler de
  aynı akışla). Gün içinde inceleme tabanı `f352bed` iken paralel
  çalışmayla HEAD `1692957` → `4578ada` ilerledi; her koşunun manifest
  dosyası kendi commit/dirty durumunu kaydeder.
- **Taşıma profili**: `Application/rf/rf_hil_transport.h` ->
  `RF_SCP_OVER_MODBUS_PORT=1` (bench): SCP, UART4/RS-485 üzerinden
  PC'deki simülatöre konuşur; USART3 RF dağıtımı derlemeden kapalı,
  Modbus kölesi başlatılmaz, TX `uart_send_buffer_rs485` ile DE
  (PA15) kontrollü. Repo varsayılanı 0 (üretim); iki varyant da
  uyarısız derlenir, 39/39 Modbus host testi her ikisinde de geçer.
- **Kablo**: COM10 (USB-RS485) modem Modbus konnektöründe; konsol
  COM16 230400 8N1. Yarı çift yönlü hattın kendi-echo'su zararsızdır
  (libscp DST adres denetimi CRC'den önce çalışır); bu, gün içinde
  telde doğrulandı.
- **DUT RF store'u**: web API üzerinden fider 1-2 x 3 faz dolduruldu
  (EUI `00124B0038C9F1x/2x/3A`, zone 1); `configure_dut.py` bu akışı
  kalıcılaştırır. Parola IP'den türetilir (`admin<son oktet+1>`).
- **Peer yanıtı kanıtı**: `observed_hub_fw=SIM-T4R1`, MH simülatörünün
  yanıt verdiğini gösterir. DUT build/CRC/profil kimliğinin kanıtı değildir.
  Tarihsel manifestteki profil 0, kaynak varsayılanından alınmıştır;
  rapordaki bench profil 1 ile aynı kanıt sayılmamalıdır. Ayrıntı ve
  07.10.2026 yazılım düzeltmesi [bağımsız değerlendirmededir](RF_HIL_DEGERLENDIRME_2026-10-06.md).

## 3. Donanımsız doğrulama (port açılmadan)

| Aşama | İçerik | Sonuç |
|---|---|---|
| Codec golden | 14 BOLATeX CSV'sinin 175 çerçevesi bayt-bayt round-trip; CRC spec vektörü `05 01 02 10 01 02 FD AA BB -> 0x5035` | 17/17 (ilk), 53/53 (model ile), **57/57 (v1.2 ekleriyle)** |
| cfg_crc | Bağımsız Python hesabı `crc16(blok[3:57])`, AY_06 APPLIED `0x096D` golden'ı | Geçti |
| v1.2 ekleri | u16/u32 sayaç kesmesi (sarma), CFG2'nin MH reboot'ta kalıcılığı, iç-CRC bozuk kayıt servis kolu, ayrıştırıcı boşluk sınırı 99/100/101 ms | 4/4 |
| Merkezi kapı | `test/integration/rf_hil` paketi `test/run_all.rb` listesine eklendi | Geçti |

## 4. Koşu envanteri (kronolojik)

Tüm koşular `test/build/hil-rf/` altında; sayılar `report.md`'den
alınmıştır. "Dönem" sütunu, o koşunun hangi kod/süreç durumunda
yapıldığını gösterir.

| Koşu | Vaka | Sonuç | Dönem / not |
|---|---:|---|---|
| 20261006-185815 | 1 | FAIL=1 | İlk kalibrasyon (a1); `send_and_wait` yanlış kullanımı |
| 20261006-185852 | 19 | PASS=4 FAIL=15 | İlk tam süit; `wait_upload_complete` içindeki erken `break` tüm vakaları 2-3 s'de düşürüyor (harness hatası, cihaz değil) |
| 191501–194952 (7 koşu) | 1-5 | karışık | Tekil vaka hata ayıklaması (c1 üretimi, a2/d3/e4/h1/h3 düzeltme doğrulamaları) |
| 20261006-193621 | 19 | PASS=14 FAIL=5 | wait_upload düzeltmesi sonrası tam süit; kalan 5 hata: zamanlama pencereleri, ERROR kod filtresi (0x03/0x04 karışması), komut hedefsiz drop |
| 20261006-195131 | 19 | **PASS=19** | Hızlı süit ilk tam geçiş (plan v1.1 kapsamı) |
| 20261006-201011 | 26 | PASS=23 FAIL=3 | v1.2 süiti ilk koşu; g3/g4 (konsol desen + kapasite-doğrulama zinciri), d6 (kurulum yarışı) |
| 20261006-202929–211538 (9 koşu) | 1-3 | karışık | g3/g4/d6/h7 tekil düzeltme ve doğrulama koşuları |
| 20261006-205735 | 26 | PASS=25 FAIL=1 | a1: çalışır-imaj sondası vaka başlangıcını blokladı (harness); sonda engellemesiz yapıldı |
| **20261006-211601** | **26** | **PASS=26 (exit=0)** | **Kapanış — seçili uygulanmış bench vakalarının geçişi** |

Not: `203959` koşusu da 25/26'dır (h7'nin zamanlama kırılganlığı
dönemi); ayrıntılar koşu raporundadır. §6, izlenen harness/timing
düzeltmelerini açıklar. Kapanışın seçili vakalarında FAIL yoktur;
günün tüm önceki FAIL izleri bağımsız olarak yeniden sınıflandırılmadı.

## 5. DUT davranış doğrulamaları (telde kanıtlı)

1. Getirme zinciri: BOOT -> TIME_SYNC -> envanter (<10 s aralıklar) ->
   END; geç hub; hub reboot'unun üçlü bildirimi (0x12 reset, 0xE3
   0xFF, 0x13) ve yeniden yükleme.
2. Zaman aşımında **aynı SEQ** ile tekrar; ERROR 0x03'te **yeni SEQ**;
   busy sürerken spec'in önerdiği `0x02 GET_FRAM_STATS` tırmanışı
   (rf_events §4.6 uygulaması).
3. Olay çekme döngüsü: 0x47 -> 0x40 -> 0x44 (start=tail, <=4) ->
   kayıt CRC -> 0x46 (son+1) -> left=0; bozuk yuvada (ERROR 0x06)
   atlama.
4. **İç-CRC'si bozuk kayıtta prefix koruması**: `consumes=[1]` —
   bozuk kaydın ötesine tüketim yok (`rf_events.c` "tail held" yolu
   uçtan uca doğrulandı). Gün içinde bu konudaki şüphe, vakadaki
   kurulum yarışı çözülünce geri alındı.
5. Konfig grubu: 3x0x22 (ayrı EUI, her biri yeni SEQ) + 0x24 +
   STAGED->DELIVERED->APPLIED; bildirilen `cfg_crc` firmware'in
   yazılabilir-dilim hesabıyla eşleşti; düşen 0x22'de aynı SEQ tekrarı;
   abort'ta 0x26 + FAILED sebep 10; kasıtlı crc bozulmasında MISMATCH.
6. PWRB: 0xE1 özetleri ve alarm kenarları/aktif maske; **gerçek
   20-32 s E5 yankı akışı** — `cfg-set capacity` sonrası telde iki
   GET, maskede yalnız b13, yeni GEN, `echo=1/1 m1=0x00 m2=0x00`
   (konsol kanıt satırı); `battery-replaced` -> 0xE6 [05 A5] ->
   ACK [05 SIRA] -> 0xE7 sonuc 0x00 + DURUM b3.
7. Hat sağlamlığı: bozuk CRC sessiz atma + tekrar; >100 ms boşlukla
   bölünen çerçeve atılır, <100 ms'deki parse edilir (tekrar yok);
   çerçeveler arası metin gürültüsü; bilinmeyen CMD'ye tepkisizlik;
   üretici bandı (0xF5) çerçevesi yok sayılır, bağlantı sağlıklı kalır.
8. Kesinti: hub sessizliğinde 10 s'lik GET_STATUS yoklamaları sürer,
   BOOT ile toparlanma.

## 6. Harness/simülatör kaynaklı düzeltmeler (özet)

- `wait_upload_complete` erken `break` (günün baskın hatası; "0x05
  gelmedi" görüntüsü yaratıyordu), ERROR kod filtresi 0x04->0x03,
  vaka zamanlama pencereleri, komut hedefli `drop_next(cmd=...)`,
  d6 bozulma-kurulum yarışı (bozulmayı kayıtlardan önce kur),
  çalışır-imaj sondasının bloklaması, h7 canlılık kanıtının konsol
  tarafına alınması.
- **Kapasite doğrulama zinciri** (üç koşu düştürdü): 0x05, ayar durumu
  belirsizken reddedilir; doğrulama GET'i `cfg-read` ile tetiklenir;
  echo + m1/m2 yetmez — E1 `cap_ah` yazılan değerle eşit olmalı.
  Senaryolar `set_telemetry cap_ah=N` ile eşledi.
- Kabuk eşzamanlı yük altında saniyeler gecikebilir: PWR komut
  deadline'ları 25 s + idempotent GET'lerde tek retry.
- Konsol okuyucu ölümü artık görünür işaret taşır (`§11` ERROR
  sınıflamasını mümkün kılar).

## 7. Üretim bulgu adayları (cihaz tarafı, iş bekliyor)

1. Web giriş ayrıştırıcısı iki nokta sonrası boşluklu standart
   JSON'da kullanıcı adını sessizce boş okuyup "Invalid credentials"
   döndürüyor (konsol kanıtı mevcut; compact JSON gerekiyor).
2. Konsol RF dökümünde 0x40/0x42/0x44/0x46 ve 0xE5-0xE8 ACK/bildirim
   isimleri "UNKNOWN" görünüyor (rf log isim tablosu eksiki; kozmetik).

## 8. Kalış ve sonraki adımlar

07.10.2026: login whitespace ve komut adı bulguları yazılımda düzeltildi.
İmaj/profil ve DEFERRED başarı kapıları da düzeltildi. Yeni firmware ile
fiziksel koşu yapılmadı; tarihsel PASS kayıtları yeni yazılımın testi
olarak kullanılmamalıdır. Güncel durum bağımsız değerlendirme v1.1'dedir.

- Kayıtlı ertelenmiş vakalar: soak profili (gerçek 60/120 s grup
  bütçeleri, 60 s olay yoklaması, saatlik TIME_SYNC statik kontrol),
  h5 (700 ms geç yanıt), c3/c4 (durmuş uptime, Trip_Failed), b1
  (keşif atama akışı), e5 (konfig ortasında hub reset).
- Cihazda HIL bench imajı yüklü; üretim profiline dönüş: başlıktaki
  bayrak 0 ile derle + imzalı `.efw`'yi xmodem ile yükle. Gerçek MH
  güncellendiğinde üretim imajıyla UART3'te aynı süit tekrar
  koşulmalıdır (profil sonuçları birleştirilmez — plan §4.1).
- Faz 2: HTTP/Modbus master/IEC104 SCADA istemci kabulleri.

## 9. Kanıt dizini

- Kapanış koşusu: `test/build/hil-rf/20261006-211601/`
  (report.md, manifest.json, vaka başına sim_trace.jsonl + console.log
  + snapshots.json).
- Günün tüm koşuları: `test/build/hil-rf/20261006-*/`.
- Donanımsız: `python test/system/rf_hil/test_sim_selftest.py` (57),
  `make -C test/integration/rf_hil run`.
- Süit girişi: `python test/system/rf_hil/run_hil.py --rf-port COM10
  --console COM16` (vaka listesi: `--list`).


## 10. Gerçek MH gözlem koşusu (gece, plan dışı ek)

Süitten sonra bench değişti: gerçek MH'nin yazılımı BOLATeX Teslim4 R1
künyesindeki `6dc02267` sürümüne güncellendi ve cihaza 23:39:01'de
üretim profilli bir imaj kuruldu (bootlog `FW_INSTALL_OK` #926; HIL
bench imajı böylece fiilen üretimle değiştirildi). COM16'nın
boşalmasıyla 90 s'lik salt-okuma gözlem yapıldı
(`Release/realhub_observation.log`):

- **Getirme akışı gerçek hub'la da birebir yaşandı**: BOOT_NOTIFY ->
  TIME_SYNC -> 6 girdi envanter -> END ("envanter yuklendi: 6 cihaz") ->
  0x40 (10 B, tail'li). Hub sağlıklı: `sched=1`, çevrim sayacı ~2/s
  artıyor, uptime süreklidir. 23:42:56'da ikinci bir getirme zinciri
  görüldü (DUT tarafı yeniden başlatma; nedeni bu gözlemde
  belirlenmedi — Modbus FC06 sıfırlama veya harici tetik olabilir).
- **0xE1 özetleri 10 s kadansla 9 çerçeve** toplandı ve dokümandaki
  §5.2 yerleşimiyle çözüldü: `ver=1`, `sira` kesintisiz artıyor
  (33-41), `oturum=34` sabit, `kart_sic=25 C` makul. Diğer değerler
  bench koşusunu yansıtıyor: `vbat~52 mV`, `vpv~106 mV`, `cap_ah=7`
  (bilinmiyor sentinel), `kaynak=0`, `alarm_aktif` dolu — gerçek akü/PV
  kaynağı bağlı olmayan test masası için fiziksel olarak tutarlı.
- **Bulgu #2 canlı doğrulandı**: 0xE1 ve 0x40 konsolda `SET/ACK
  UNKNOWN` olarak loglanıyor (RF log isim tablosu eksikliği gerçek
  hub'la da görünür).

**RTU-tek-restart boşluğu (yeni bulgu):** 23:49:37'de modem sıfırlandı
(elog: `RESET_CAUSE PIN`) ve MH ayakta + envanteri yüklü kaldığı için
BOOT bir daha gelmedi; DUT `MH SCP major: 0, Envanter: BEKLIYOR`
süresiz kaldı (hat, GET_STATUS ve 0xE1 çalışmaya devam etti). Spec
§1.10 yalnız MH-restart'ı kapsıyor; RTU'nun tek başına resetlenmesi
tanımsız. BOLATeX'e sorulmalı: DUT, yanıt veren hub'a envanteri BOOT
beklemeden yeniden yükleyebilmeli mi? Kurtarma: MH'yi resetlemek —
23:55:35'te doğrulandı: BOOT -> TIME_SYNC (ACK 6 ms) -> 6 girdi (18 ms
aralıklarla) -> END -> "envanter yuklendi" -> 0x40, toplam 153 ms;
`MH SCP major: 1, Envanter: YUKLU`, `hub up=3s fw=6dc02267`.

Bu gözlem salt okumadır; simülatör süitinin gerçek hub'la eşdeğer
kabulü değildir (plan §1). Konsol komut çıktıları (`rf status`,
`pwrboard show`) bu pencerede GSM yoğunluğu nedeniyle alınamadı;
E1 çözümü ham çerçevelerden yapıldı.

## Değişiklik geçmişi

| Tarih | Açıklama |
|---|---|
| 06.10.2026 | İlk sürüm: günün 28 koşusunun konsolide değerlendirmesi (kapanış 26/26) |
| 06.10.2026 (gece) | §10: gerçek MH (6dc02267) gözlem koşusu eklendi; commit'ler 38c3901/9a2f0ec/bu commit |
