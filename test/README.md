# Test Altyapısı

**Sürüm:** 1.33
**Tarih:** 2026-10-06

**Amaç:** Firmware modüllerinin host üzerinde hızlı ve tekrarlanabilir biçimde
doğrulanmasını sağlayan ortak test giriş noktasını açıklar.

**Kapsam:** Ceedling birim testleri, host integration testleri (birlikte çalışma
testleri) ve yalnızca hedef cihazda çalışan testlerin dizin düzenini kapsar.

## Ön koşul

- Ruby ve Ceedling 1.0.1 kurulu olmalıdır.
- Host C derleyicisi ve GNU Make komut satırında bulunmalıdır.
- Web arayüzü integration testi için Node.js bulunmalıdır.
- Web oturum güvenliği integration testi için Python 3 bulunmalıdır.
- Coverage (kapsam) raporu için `gcov`, Python ve `gcovr` Python modülü
  bulunmalıdır.

## Dizin düzeni

| Dizin | İçerik |
|---|---|
| `<modül>/` | Birim ve senaryo Ceedling testleri; tür dosya adından anlaşılır |
| `integration/` | Gerçek üretim modüllerini birlikte derleyen host senaryoları |
| `support/` | Birden fazla Ceedling testinin kullandığı ortak yardımcılar |
| `support_sources/` | Yalnız `TEST_SOURCE_FILE` ile seçilen ortak fake kaynakları |
| `target/` | STM32 üzerinde çalıştırılan ve host koşusuna katılmayan testler |
| `system/` | Ağ veya çalışan cihaz gerektiren uçtan uca test araçları |
| `fixtures/` | Testlerde kullanılan sabit firmware ve veri örnekleri |
| `scripts/` | Platformdan bağımsız test yardımcıları |

## Adımlar

Tüm host testleri repo kökünden şu komutla çalıştırılır:

```text
ruby test/run_all.rb
```

Yalnız Ceedling testleri için:

```text
ruby test/run_all.rb unit
```

Yalnız integration testleri için:

```text
ruby test/run_all.rb integration
```

Coverage raporu için:

```text
ruby test/run_all.rb coverage
```

Bu komut önce eski Ceedling ve GCOV çıktılarını temizler, ardından raporu
yeniden üretir.

Üretilen test çıktıları şu komutla temizlenir:

```text
ruby test/run_all.rb clean
```

## Yeni test ekleme

Saf hesaplama, codec veya modül senaryoları `test/<modül>/` altında Unity
testi olarak eklenmelidir. Donanım ve komşu modül çağrıları CMock ya da küçük
bir fake (davranışlı test çifti) ile ayrılmalıdır.

Ceedling ile çalışabilen çok modüllü senaryolar da aynı modül dizininde
tutulmalıdır. Dosya adı davranışı açıklamalıdır; örneğin
`test_iec104_link_scenario.c`. Gerçek süreç, timeout veya farklı bir çalışma
zamanı gerektiren paketler geçiş süresince `integration/` altında merkezi
çalıştırıcıya bağlı kalır.

Hedef MCU, interrupt veya fiziksel çevre birimi gerektiren testler `target/`
altında tutulmalıdır. Bu testler host test sonucu olarak gösterilmemelidir.

Ağdaki gerçek cihaza istek gönderen testler `system/` altında tutulmalıdır.
Bu testler hedef adresi açıkça verilmeden merkezi host koşusuna
eklenmemelidir.

## Ceedling modül kapsamı

| Modül | Test dosyası | Doğrulanan ana davranışlar |
|---|---|---|
| `libscp/cobs` | `scp/test_cobs.c` | Tüm 0-253 bayt uzunlukları, sıfır dizileri, tam kapasite, yerinde çözme ve bozuk blok reddi |
| `libs/crc32` | `libs/test_crc32.c` | Standart ve ikili kontrol değerleri, bayt bayt/parçalı hesaplama ve bit yansıtma sınırları |
| `libefw/efw_crc` | `efw/test_efw_crc.c` | Firmware CRC tel değeri, boş girdi, ikili veri, parçalı hesaplama ve bit yansıtma sınırları |
| `libiec104/iec104_util` | `iec104/test_iec104_util.c` | 24 bit IOA sınırları, üst bitlerin atılması, bayt sırası ve eşitlik |
| `libiec104/iec104` | `iec104/test_iec104_protocol_scenario.c` | STARTDT/STOPDT, gönderim penceresi, TESTFR, taşıma reddi, sorgulama, reset ve tüm TCP bölünme noktaları |
| `gsm/ring_buff` | `gsm/test_ring_buff.c` | FIFO, tek yuva, tam kapasite, sarma, kısmi okuma, peek, clear, blok ve sıfır kopya erişim |
| `gsm/gsm_wtd` | `gsm/test_gsm_wtd_liveness_scenario.c` | Sessizlik eşiği, iki aşamalı yeniden başlatma, hard reset, ping ile toparlanma ve busy durumu |
| `web-server/http_request_parser` | `web_server/test_http_request_parser.c` | Eksik/bozuk istekler, uzunluk sınırları, sayısal taşma, GET/POST, sorgu, gövde ve kötü yol reddi |
| `power_board/power_board_decode` | `power_board/test_power_board_decode.c` | XSUM, protokol sürümü, bozuk çerçeveler, büyük endian, işaretli sayı sınırları ve last-gasp |
| `libiec104/cp56time2a` | `iec104/test_cp56time2a.c` | RTC dönüşümü, takvim ve artık yıl, karşılaştırma, zaman farkı, ham alan sınırları ve geçersiz zaman damgası |
| `libscp/scp` | `scp/test_scp.c` | Gönderme-alma çevrimi, adres ve yayın süzme, CRC, zaman aşımı, taşma, ayrıştırıcı toparlanması ve ping |
| `libs/debouncer` | `libs/test_debouncer.c` | Alçak/yüksek geçiş eşiği, gürültü, doygunluk ve `UINT8_MAX` sayaç sınırı |
| `libs/ring_buf` | `libs/test_ring_buf.c` | Atomik FIFO, kapasite, dolu/boş durum, sarma ve toplu işleme |
| `libs/datetime` | `libs/test_datetime.c` | Biçimlendirme, takvim sınırları, artık yıl, karşılaştırma, geçen süre ve Unix epoch dönüşümü |
| `modem_config` | `application/test_modem_config.c` | NVRAM erişimi, sayısal alanlar, metin sınırları, telefon numarası, NTP, reset üst sınırı ve sayaçlar |
| `system_status` | `application/test_system_status.c` | Dijital giriş, röle, ADC, GSM ve güç kartı verilerinin birleştirilmesi ile sıcaklık min/max takibi |
| `periodic_reset` | `application/test_periodic_reset.c` | Devre dışı durum, zamanlayıcı kurma, süre değişimi, eski ayar üst sınırı, süre dolması ve yeniden etkinleştirme |
| `digital_input` | `application/test_digital_input.c` | Active-low başlangıç durumları, geçersiz kanallar ve kullanıcı reset düğmesinin 3/10/15 saniye eşikleri |
| `relay` | `application/test_relay.c` | Komut kuyruğu, iki kanallı peak/hold geçişi, ortak zamanlayıcı, saat taşması, kapatma ve shell doğrulaması |
| `time_service` | `application/test_time_service.c` | Atomik sayaç, normal geçen süre, `UINT32_MAX` taşması ve tam süre sınırı |
| `power_panic` | `application/test_power_panic.c` | Açılış seviyesi, geçici darbe, yükselen/düşen kenarlar ve bölüm başına tek ELOG kaydı |
| `libs/xprintf` | `libs/test_xsnprintf_format_scenario.c` | Tam sığma, kesilme, koruma baytları, biriktirme, float ve biçim genişliği senaryoları |
| `libs/xscanf` | `libs/test_xscanf_format_scenario.c` | Sayısal türler, taşma, metin sınırları, hatalı format ve AT yanıtı senaryoları |
| `rf/rf_scp` | `rf/test_rf_scp_codec_scenario.c` | İstek üretme, zaman eşitleme, ping yanıtı ve durum paketi çözme senaryoları |
| `rf/rf_config` | `rf/test_rf_config_scenario.c` | Varsayılan ayarlar, kalıcı kayıt, çalışma modu ve kanal sınırı senaryoları |
| `web-server/rf_json` | `web_server/test_rf_json_golden_scenario.c` | RF ayarlarının tam golden JSON çıktısı ve tampon uzunluğu |

IEC104 protokol senaryoları gerçek `iec104.c`, `iec104_config.c`,
`iec104_util.c` ve `cp56time2a.c` kaynaklarını birlikte derler. Taşıma
işlevi ve platform çağrıları sabit boyutlu test yardımcılarıyla sağlanır.
Mevcut protokol kaynağındaki daraltma ve bit alanı atamaları nedeniyle
yalnız `test_iec104_protocol_scenario` derlemesinde `-Wconversion` ve
`-Wsign-conversion` kapatılır. Diğer uyarı denetimleri açık kalır.

### IEC104 protokol senaryoları

**Amaç:** SCADA tarafından gelen baytların doğru işlenmesini ve cihazın
ürettiği APDU (uygulama protokol veri birimi) yanıtlarını doğrular.

**Kullanım yeri:** `iec104/test_iec104_protocol_scenario.c` dosyasında 54
senaryo bulunur. Her senaryo bağımsız başlangıç durumuyla çalışır.

| Alan | Kontrol edilen durumlar |
|---|---|
| Bağlantı | STARTDT, STOPDT, bağlantı açılmadan gelen veri, TESTFR yanıtı ve yeniden açılış |
| TCP veri akışı | Her bölünme noktası, tek tek gelen baytlar, birleşik çerçeveler, araya giren ilgisiz baytlar ve eksik kuyruk |
| Boyut sınırları | Geçersiz APDU uzunluğu, 253 bayt APDU sınırı, fazla büyük TCP verisi ve dolan tamponun toparlanması |
| ACK (alındı onayı) | Tekrarlanan, eski, kısmi ve henüz gönderilmemiş çerçeveyi onaylayan ACK; komut içindeki hatalı ACK |
| Sıra numarası | Beklenen sıra dışındaki giriş, alma sayacının taşması ve gönderme/ACK sayaçlarının 32.768 adımlık tam döngüsü |
| Pencere ve süre | k gönderme sınırı, w alma sınırı, t1/t2/t3 eşikleri, kısmi ACK sonrası süre ve taşıma hatasından sonra yeniden deneme |
| Interrogation (genel sorgu) | Onay-veri-bitiş sırası, olumlu/olumsuz bitiş, adres ve COT (gönderim nedeni) kontrolü |
| Reset komutu | Genel reset, bekleyen olayları temizleme, bilinmeyen QRP (reset türü), yanlış IOA (bilgi nesnesi adresi) ve eksik komut |
| Saat eşitleme | Yanıt baytları, originator address (isteği gönderen adres), milisaniye dahil RTC (gerçek zaman saati) alanları, invalid (geçersiz) biti ve eksik zaman damgası |
| Arıza verisi | Taşıma kesilince kaldığı yerden devam etme; atlanan veya yinelenen kayıt olmaması |

Başarılı gönderimlerin tamamında başlangıç baytı, en küçük çerçeve boyutu ve
uzunluk alanının gerçek bayt sayısıyla eşleşmesi kontrol edilir. İlgili
senaryolarda yanıtın sıra numarası, COT, P/N (olumlu/olumsuz onay), adres,
veri alanları ve sayaçların son değeri de kontrol edilir.

Yalnız protokol senaryoları `test/` dizininden şu komutla çalıştırılmalıdır:

```text
ceedling test:test_iec104_protocol_scenario
```

**Kapsam sınırı:** Bu testler host üzerinde çalışır. Gerçek SCADA ile ağ
üzerinden birlikte çalışma, cihaz zamanlaması ve standart uygunluk testi
ayrıca yapılmalıdır. Saat komutu işleyicisi şu anda yalnız invalid bitini
kontrol eder; takvim alanlarını tam doğrulamaz. CP56Time2a birim testleri
doğrulama işlevini sınar, ancak komut işleyicisi bu işlevi çağırmaz.
Bu açık nokta protokol paketinin tamamlanmış olduğu şeklinde
yorumlanmamalıdır.

2026-10-01 tarihli temiz `gcov:all` koşusunda 268 Ceedling testi geçmiştir;
başarısız veya atlanan test yoktur. Aynı değişikliklerle 7 integration paketi
de geçmiştir. Coverage değerleri dosya bazındadır; protokolün tüm
işlevlerinin kapsandığı anlamına gelmez.

| Kaynak | Satır kapsamı | Dal kapsamı |
|---|---|---|
| `iec104.c` | %70,00 | %71,95 |
| `cp56time2a.c` | %98,27 | %92,31 |
| `iec104_util.c` | %100,00 | %100,00 |
| `iec104_config.c` | %13,82 | %9,38 |

Yapılandırma erişimlerinin çoğu bu protokol senaryolarında kullanılmaz.
`iec104_config.c` için ayrıca ayar sınırları ve kalıcı kayıt davranışını
kapsayan birim testleri eklenmelidir.

## Integration olarak kalan testler

| Paket | Neden Ceedling dışında çalışıyor |
|---|---|
| `contiki_process` | Gerçek Contiki sürecinin belirlenen süre boyunca çalışmasını doğrular |
| `fault_log` | Çift flash kopyası, kesilen yazma ve yeniden açılış senaryolarını birlikte yürütür |
| `gsm` | Kasıtlı timeout ve gerçek Contiki süreç yeniden başlatma akışını kullanır |
| `libs` | SPI flash halka kaydını yüzlerce yazma ve yeniden açılış adımıyla doğrular |
| `nvram` | Çift NVRAM görüntüsünü, yazma arızalarını, ömür sayacının kayıt sınırını ve yeniden açılışı birlikte doğrular |
| `rfwu_auth` | Gerçek RFWU v2 parser/SHA/HMAC ve PC yardımcılarıyla yetki kapıları, replay ret, resume ve transferi doğrular |
| `rf_hub_sim` | Ayrı RF hub simülatörünü kendi çalıştırılabilir dosyasıyla sınar |
| `web_auth` | Üretim fonksiyonlarını donanım ve taşıma test doubles ile izole ederek sınar |
| `web_navigation` | Kaynak ve gömülü web içeriğini Node.js üzerinde karşılaştırır |

## Web oturum güvenliği kontrolleri

`web_server/test_http_session_token.c` dosyası 12 Ceedling testi içerir.
Token uzunluğu, sorgu eşleşmesi, hatalı girişler ve entropy (rastgelelik
kaynağı) başarısızlığı denetlenir. RNG ve fallback (yedek yol) kontrolleri
`bsp/test_bsp_random.c` dosyasındaki 9 Ceedling testiyle ayrı yürütülür.
Bu testler gerçek `Application/bsp/bsp_random.c` modülünü HAL CMock
ile derler; kabul edilen okumaların birikimi, hata bayrakları, unsigned
sarma, fallback token biçimi ve durum ilerlemesi doğrulanır.

`integration/web_auth/run_tests.py` dosyası 19 host kontrolü içerir.
Mevcut üretim dosyalarından giriş, oturum, AT log ve BSP random modülü fonksiyonları
alınarak host üzerinde derlenir. Eski token reddi, giriş kilidi, oturum
süresi, RNG hatasında doğru parola ile fallback girişi, yanlış parola
reddi, aynı tick değerinde ardışık token değişimi, hassas logların
gizlenmesi, RNG birikiminin unsigned sarması ve fallback geçiş logunun
yalnız durum değişiminde yazılması doğrulanır. RNG
başlatması main içindeki mevcut CubeMX/HAL akışında kalır. Fonksiyonların kaynak şekli değişip çıkarılamazsa test
başarısız olur; test düzeneği yeni kaynakla birlikte değerlendirilmelidir.

Donanım, log ve taşıma çağrıları test doubles (taklit bileşenler) kullanır.
Gerçek RNG kurtarması, tam HTTP/AT süreçleri ve cihaz davranışı bu testin
kapsamı dışındadır. Üretilen C dosyaları, çalıştırılabilir dosyalar ve loglar
Git tarafından yok sayılan `test/build/web_auth/` dizininde tutulur.
Test kaynağı ise repoda saklanmalıdır.

Bu paket `ruby test/run_all.rb all` ve `integration` koşularına dahildir.
Tek başına repo kökünden şu komutla çalıştırılır:

```text
make -C test/integration/web_auth run
```

Host derleyicisi `CC`, Python komutu `PYTHON` ile değiştirilebilir.

Fallback, erişim sürekliliği amacıyla kullanıcı kararıyla eklenmiştir.
32 hex karakter üretmesi 128 bit güvenlik sağladığı anlamına gelmez;
xorshift32 kriptografik bir üretici değildir. RAM durumu reset ile sıfırlanır;
ayrı açılışlarda tokenlerin farklı olması garanti edilmez. Bu testler
fallback'in güvenlik gücünü kanıtlamaz.

`http_session_token_generate()` doğrudan `bsp_random_word()` çağırır;
random callback (işlev sağlayıcısı) parametresi yoktur. RNG/fallback seçimi
BSP içinde yapılır. Web girişinde tick ve genel GSM sayaçları modüle
aktarılır; normal RNG okuması başarısız olursa fallback kendiliğinden
kullanılır. Fallback'e geçiş loglanır; aynı hata sürdükçe her kelime için
log üretilmez. Sağlıklı bir RNG okuması geçiş durumunu sıfırlar.

## Doğrulama

Başarılı koşuda merkezi komut sıfır durum koduyla biter. Ceedling JUnit XML
çıktısı `test/build/ceedling/artifacts/test/` altında oluşturulur. Coverage
çıktıları `test/build/ceedling/artifacts/gcov/` altında oluşturulur.

GitHub Actions iş akışı normal JUnit raporunu coverage temizliğinden önce
`test/build/reports/test/` altında saklar. `gcovr`, Ceedling'in kullandığı
Python ortamına kurulur. Host testleri başarısız olsa da coverage adımı,
araç kurulumu başarılıysa ve koşu iptal edilmediyse çalışır.

Linux CI (sürekli entegrasyon) koşusunda bütün `gcc` çağrıları `-m32` ile
çalışır. Bu seçim Ceedling birim testlerini, coverage derlemesini ve
Make ile çalışan C integration paketlerini kapsar. Derleme ve link
(bağlama) aynı 32 bit hedefi kullanır. Node.js web testi C derlemesi içermez.

Runner ve GCC programı 64 bit olabilir; test programları 32 bit üretilir.
`gcc-multilib` gerekli 32 bit geliştirme kütüphanelerini sağlar. Testlerden
önce `Verify 32-bit host ABI` adımı pointer, `long` ve atomik indeks
boyutlarının 4 bayt olduğunu kontrol eder. Üretilen programın ELF32 türünde
olduğu doğrulanır ve program çalıştırılır. Bu kontrol geçmezse host test
koşusu başlatılmaz. Host ABI (veri türleri ve çağrı düzeni) seçimi MCU
zamanlamasının veya donanım davranışının doğrulandığı anlamına gelmez.

`host-test-reports` artifact (indirilebilir çıktı paketi), saklanan JUnit
raporunu, üretilen coverage dosyalarını ve `test/build/ci-logs/` altındaki
`host-tests.log` ile `coverage.log` dosyalarını içerir. Derleme rapor
oluşturamadan durursa paket mevcut logları içerir. Başarılı test ve coverage
adımlarında beklenen raporların boş olmadığı ayrıca kontrol edilir.

`No files were found` mesajı görülürse ilk hata, `Run all host tests` veya
`Generate unit-test coverage` adımında aranmalıdır. Bu mesaj tek başına
testin neden başarısız olduğunu açıklamaz.

## Sık hatalar

- `ceedling` bulunamazsa Ruby gem kurulum dizini `PATH` değişkenine
  eklenmelidir.
- Eski object dosyaları kuşkulu sonuç üretiyorsa ilgili integration paketinin
  `build/` dizini temizlenmelidir.
- `gcovr` bulunamazsa normal test koşusu kullanılabilir; yalnız coverage
  komutu etkilenir.

## RFWU ve ömür sayacı bulgu testleri

`make -C test/integration/rfwu_auth run` gerçek RFWU v2 parser, CRC,
SHA-256 ve HMAC modüllerini host üzerinde derler. 25 kontrol nonce tek
kullanım/süre dolumunu, yanlış MAC ve v1 reddini, reconnect/kilit,
RNG hatasını, resume ve PC transfer/force restart/QUERY yardımcılarını sınar.
Kripto kontrolleri RFC 4231 ve Python hashlib/hmac ile karşılaştırılır.
Host testleri public fixture anahtarı kullanır; ürün anahtarını okumaz.

Python 3 ve shared library destekleyen host derleyicisi gerekir. Test,
GUI başlatmadan PC aracının gerçek protokol fonksiyonlarını ve worker
sınıflarını AST üzerinden yükler. ctypes library'sinin ABI'si Python ile
aynı seçilir; x86 üzerinde bu paket CI gcc -m32 wrapper'ını Python 64 bit
ise -m64 ile geçersiz kılar. Bu paket NVRAM/hedef pointer layout'u sınamaz;
NVRAM ve diğer 32 bit host paketleri ayrı çalışmaya devam eder. Linux CI
çalıştırması bu yerel Windows oturumunda yapılmamıştır.

`test/libs/test_hmac_sha256.c` Ceedling altında standart ve incremental
HMAC/SHA vektörlerini doğrular. `test/bsp/test_bsp_random.c` hardware-only
API'nin HAL hatasında fallback vermediğini ve mevcut web fallback API'sinin
korunduğunu doğrular. Cihaz RNG/flash/bootloader testi ayrıca yapılmalıdır.

`test/application/test_modem_config.c` ömür güncellemesinin otomatik sync
yapmadığını doğrular. `integration/nvram/test_nvram_sync.c` gerçek NVRAM
çekirdeğiyle kaydedilmemiş artışın kaybını, kaydedilmiş değerin korunmasını
ve yazma hatasında önceki sağlam değerin yüklenmesini doğrular.

Lifetime mevcut NVRAM alanında kalır. Modem config testleri 90.000 saniyelik
adımda tek kayıt çağrısını ve başarısız kayıtta sonraki 25 saate kadar tekrar
olmamasını doğrular. Periodic reset testleri sync çağrısının resetten önce
olduğunu ve sync hatasının reseti engellemediğini doğrular.

## RF-SCP R1 örnek çerçeve testleri

**Amaç:** BOLATeX R1 teslimindeki 14 senaryo dosyasının 175 ham
çerçevesini gerçek SCP parser (çözümleyici) ve encoder (kodlayıcı)
üzerinden doğrular. Test dosyası `scp/test_scp_capture_scenario.c`'dir.

**Kullanım yeri:** `test/` dizininden şu komut çalıştırılmalıdır:

```text
ceedling test:test_scp_capture_scenario
```

05.10.2026 koşusunda 6/6 Unity testi geçmiştir. Örneklerin çözülmesi,
yakalanan baytlarla birebir yeniden kodlanması, her parçalama noktası,
konsol metniyle ve arka arkaya alım, bozuk CRC sonrası toparlanma ve
100 ms yarım çerçeve sınırı denetlenir. Döngüde kullanılan 175 vektör
ayrı Unity testleri olarak sayılmaz.

Fixture, teslim edilen `mantiksal_hex` ve `hat_hex` sütunlarından
üretilir; beklenen çıktı üretim codec'inden oluşturulmaz. Repo kökünde:

```text
python test/scripts/generate_rf_scp_vectors.py
python test/scripts/generate_rf_scp_vectors.py --check
```

İlk komut takip edilen `test/fixtures/rf_scp_vectors.h` dosyasını
üretir; ikinci komut yazmadan güncelliği kontrol eder. Araç CSV alanlarını,
CRC'leri ve olay kayıtlarının iç CRC'lerini ayrıca denetler. Ceedling
koşusu üretilmiş header'ı kullandığından Python/CSV erişimi gerektirmez.
Fixture BOLATeX verisidir; teslimdeki proje kullanım ve üçüncü kişilerle
paylaşmama koşulu korunmalıdır.

**Kapsam sınırı:** Bu testler R1 uygulama işleyicileri veya cihazlar
arası akış kabulü değildir. AY_05b'deki bildirim ACK'i ve ölçüm zamanından
kaynaklanan istek/yanıt sırası firmware davranışına kopyalanmaz.
R1 bildirimi ACK gerektirmez. PWRB_04 geçersiz PARAM ile alınmış bir ret
örneğidir; geçerli akü değişti komutunun uygulandığını kanıtlamaz.

## RF-SCP R1 payload codec testleri

**Amaç:** `Application/rf/rf_scp_codec.c` içindeki durumsuz istek
doğrulaması ve yanıt/bildirim decoder (çözümleyici) işlevlerini sınar.
`rf/test_rf_scp_codec.c` içinde 18 Unity testi bulunur.

**Kullanım yeri:** `test/` dizininden:

```text
ceedling test:test_rf_scp_codec
ceedling "test:pattern[(test_rf_|test_scp|test_cobs)]"
```

05.10.2026 koşusunda yeni testler 18/18, ilgili RF/SCP/COBS/JSON
paketlerinin toplamı 71/71 geçmiştir. Testler yakalanan bütün gelen
paketleri, geçerli giden örnekleri, tüm isteklerin TYPE/uzunluk haritasını,
hatalı uzunluk/adres/sürüm/null girişlerini ve önceki çıktının korunmasını
denetler. Ayırıcı ayarlarının 22 alanı, float uç değerleri/NaN/Inf,
bağımlı eşikler ve integer sınırları sınanır. Olayların 32 bit süresi,
bilinmeyen olay/geçersiz saat/NaN tanısı, dört kayıt batch (toplu yanıt)
ve yuva bazında CRC hatası test edilir.

PWRB signed ölçümleri ve birimleri, bayat veri bayrakları, ham big-endian
baytlar, müşteri yazma maskesi, ayarsız değerler, komut PARAM kontrolü,
yankı alanları ve ERROR ek baytları korunur. Bilerek geçersiz PWRB_04
isteği başarılı komut örneği olarak kullanılmaz.

**Kapsam sınırı:** İşlem zamanlayıcıları, GEN/önceki GET karşılaştırması,
ilk CFG2 yazımı, grup uygulama takibi, kalıcı kayıt/tüketme ve gerçek
Powerboard tüketicilerine yayın bu saf codec testinin kapsamında değildir.
Testlerde production (üretim) codec kullanılır; ilgili mantık taklit edilmez.

## RF-SCP istek ve RX dispatch testleri

**Amaç:** `rf/test_rf_error_retry_scenario.c` içindeki 25 Unity testi,
gerçek rf_comm/envanter kodunun paket kabulü ve sınırlı retry
(yeniden deneme) davranışını doğrular. RX (alım) senaryoları gerçek
ring buffer, SCP parser (çözümleyici) ve dispatch (mesaj yönlendirme)
yolunu çalıştırır. UART gönderimi, bridge (saydam köprü) ve tick
(zaman sayacı) taklit edilir; protokol algoritması taklit edilmez.

**Kullanım yeri:** `test/` dizininden:

```text
ceedling test:test_rf_error_retry_scenario
ceedling "test:pattern[(test_rf_|test_scp|test_cobs)]"
```

05.10.2026 son koşusunda 25/25 senaryo ve ilgili sekiz dosyada 87/87
test geçmiştir. Yanıt adresi/CMD/SEQ/TYPE/payload kontrolü, geciken ACK,
aktif istek sırasında bütün örnek SET bildirimleri, ACK üretilmemesi,
bozuk keşif mesajında durum korunması, komuta göre timeout/retry,
SEQ/tick sarma, bütçe tükenmesi ve callback'ten yeni istek sınanır.
Timeout aynı SEQ kullanır; ERROR sonrası izinli girişim yeni SEQ ile
gönderilir. GEN uyuşmazlığı ve desteklenmeyen ayar okuması otomatik
tekrarlanmaz. Hata yanıtının ek baytları sonuç işlevine aynen ulaşır.

**Kapsam sınırı:** BOOT zincirinin restart/RTC/major politikası, envanter
boş/kısmi loaded durumu, diğer bildirimlerin veri tüketicileri ve gerçek
UART/RF zamanlaması sonraki adımlardadır. Sürüm eki kaynak ve test
adlarından çıkarılmıştır; örnekler halen kaynak R1 protokol paketindendir.
Üretilmiş fixture güncellik kontrolü yeni adla
`python test/scripts/generate_rf_scp_vectors.py --check` yapılmalıdır.

## RF açılış, saat ve envanter akışları

**Amaç:** Haberleşme senaryo dosyası açılış/envanter akışlarıyla birlikte
46 Unity testi içerir. Gerçek timer.c ve cp56time2a.c çalışır; RTC ve
tick (zaman sayacı) donanım sınırları taklit edilir. Aynı dosyada gerçek
rf_shell time/epoch girişleri de test edilir.

**Kullanım yeri:** `test/` dizininden:

```text
ceedling test:test_rf_error_retry_scenario
ceedling "test:pattern[(test_rf_|test_scp|test_cobs|test_cp56time2a)]"
```

05.10.2026 son koşusunda 46/46 senaryo, ilgili dokuz dosyada 122/122 test
geçmiştir. Log `test/build/scp-startup-tests.log` içindedir.

Geçerli saatle BOOT → TIME_SYNC → INVENTORY_SET → END; geçersiz saatle
doğrudan envanter, sonra saat geçerli olunca eşitleme; saatlik yenilemede
envanterin korunması sınanır. Yeniden BOOT'ta pending (bekleyen) isteğin
iptali ve eski ACK reddi, major uyuşmazlığı, boş/kısmi/hatalı envanter,
10 s sınırı, channel, update/delete ACK sonrası discovery kaldırma ve
epoch koşulları denetlenir. Discovery RSSI yenilemesi, dolu kuyruk ve
SIZE_MAX dahil geçersiz indekslerde mevcut durumun korunması test edilir.

**Kapsam sınırı:** TIME_SYNC geçersizken envantere devam etme, 05.10.2026
kullanıcı kararıdır; R1 bu RTU tercihini belirlemez. Web Save bağlantısı,
kalıcı envanter değişikliği ve yapılandırma öncesi epoch bekleme servisi
sonraki adımlardadır. Host tick sınırı fiziksel UART/RF sürelerini kanıtlamaz.

## RF canlı veri, alarm ve kanonik monitor

**Amaç:** R1 snapshot (son veri), ACK verilmiş EUI eşlemesi, canlı veri
tazeliği ve açma/anomali durumları gerçek RF/RX koduyla doğrulanır.
Haberleşme senaryo dosyası bu kapsamla birlikte 64 Unity testi içerir.

**Kullanım yeri:**

```text
ceedling "test:pattern[(test_rf_|test_scp|test_cobs|test_cp56time2a)]"
node test/integration/web_navigation/test_navigation.js
```

05.10.2026 koşusunda 64/64 RF senaryosu, dokuz ilgili dosyada toplam
140/140 test geçmiştir. Log `test/build/scp-live-tests.log` içindedir.
Web paketi kaynak ve gzip gömülü sayfada Türkçe/İngilizce yeni RF
monitor tablosunu doğrular.

Testler ACK öncesi veri reddi, store değişiminde eski verinin yeni EUI'ye
etiketlenmemesi, EUI taşıma/silme ve başarısız update, fider 4 maskesi,
30 s sınırı/unsigned tick sarma, SEQ boşluğu, signed RSSI/int16 sıcaklık,
NaN kalite bilgisi ve TRIP anlık akım ayrımını kapsar. AY restart'ında
latched (operatör onaylı) alarm, MH restart'ında alarmın korunması ve
SHELL onayı denetlenir. Anomali reset sırası ve yolları bağımsızdır.

JSON builder'da küçük tampon taşması/yarım yanıt engeli, 12 fazlı tam
yanıtın mevcut 8192 B tamponuna sığması ve son R1 alanları test edilir.
Simülasyon örnekleri gerçek envanter veya RF snapshot'ını değiştirmez.
Eski rf_monitor_t/DEVICEID32/unsigned RSSI modeli kullanılmaz.

**Kapsam sınırı:** 101/105 olayından alarm üretme, kalıcı geçmiş ve
IEC104/Modbus producer (veri sağlayıcı) bağlantısı sonraki adımlardadır.
Host testleri gerçek RF/UART veya açma süresini kanıtlamaz.

## RF tam olay kaydı ve örnek iletişim

**Amaç:** 60 B ham RF olayları spi_flash_log üzerinden ayrı 8 KB alanda
saklanır. Örnek HEAD/RANGE/CONSUME paketleri UART/SCP yolunda sınanır.

**Kullanım yeri:**

```text
ceedling "test:pattern[(test_rf_|test_scp|test_cobs|test_cp56time2a|test_spi_flash_log_sequence_wrap)]"
python test/scripts/generate_rf_scp_vectors.py --check
```

05.10.2026 koşusunda on bir dosyada 157/157 test geçmiştir. Yeni
`test/rf/test_rf_event_log.c` paketindeki 11 test gerçek günlük kodunu
çalıştırır; yalnız W25QXX/BSP sınırları taklit edilir. NOR modeli 1→0
yazım ve 4096 B sektör silmesini uygular, bölge dışı erişimi reddeder.
Yarım payload/CRC yazımı, sessiz CRC kaybı, geri okuma kaybı, yeniden
başlatma, halka sarma, silme hatasından toparlanma ve eski arıza alanı
adresleri sınanır. Geçersiz girdiler eski geçerli kayıtları değiştirmez.
Bilinmeyen olay, NaN, clock_quality ve ayrılmış baytlar ham kayıtta
korunur. Kanıt `build/scp-event-tests.log` içindedir.

Örnek kaydın bütün alanları sabit beklenen değerlerle doğrulanır. 0x47
bildirimi bekleyen HEAD'i tamamlamaz, R1 gereğince ACK üretmez. CSV'deki
bildirim ACK'i uygulanmaz; ham yanıtlar değiştirilmeden istek/yanıt
bağımlılıkları mantıksal sırayla yürütülür. Kısa batch ve eksik kayıt
sınırları da sınanır.

**Kapsam sınırı:** Otomatik olay çekme/tüketme, 101/105 alarm bağlantısı
ve merkez aktarımı sonraki parçadadır. NOR modeli fiziksel enerji
kesintisi veya sürücü zamanlamasını kanıtlamaz.

## Otomatik RF olay tüketimi ve 32 bit arıza süresi

**Amaç:** HEAD → RANGE → ham kalıcı kayıt → gereken arıza listesi/sync →
CONSUME akışı gerçek servis koduyla sınanır. 1/7 kalıcı, 3 geçici listesine
aktarılır; kaynak zamanı ve uint32_t süre korunur.

**Kullanım yeri:**

```text
ceedling "test:pattern[(test_rf_|test_fault_log|test_scp|test_cobs|test_cp56time2a|test_spi_flash_log_sequence_wrap|test_iec104)]"
```

05.10.2026 koşusunda 15 dosyada 246/246 test geçti. Olay servisi 23 testi
kapsar. Depolama beklerken MH halkası sarıp aynı indekse döndüğünde
HEAD total farkının eski CONSUME isteğini engellediği de sınanır. Kanıt
`build/scp-event-service-tests.log`.
Servis taşıma/kayıt API sınırlarını taklit eder; gerçek kayıt algoritmaları
ayrı NOR testlerinde çalışır. `application/test_fault_log.c` eski çift
kopya paketindeki 53 kontrolü merkezi Ceedling'de yürütür; yeni test
kaynak zamanı ve UINT32_MAX sürenin Flash yeniden açılışını doğrular.
Bu eski fixture için conversion/unused uyarı istisnaları yalnız ilgili
test paketindedir; yeni RF servisinin sıkı uyarıları korunur.

Örnek AY_05b yanıtları servis kararlarında da kullanılır; önceki gerçek
UART/SCP testleri yeni servise bildirim aktarımını denetler. Tüm 14 CSV'nin
175 frame fixture kontrolü geçmiştir. Tek/four-record batch, kısa yanıt,
99→0 sarma, bozuk ikinci kayıtta yalnız geçerli önek, yazma/append/sync
hatası, kalıcı ERROR 0x06, consume reddi, BUSY/degraded, notify kaybı ve
MH restart sonrası HEAD'den başlama sınanır. Liste eşlemesi store
indeksine göre yapılır; Fider_ID−1 varsayılmaz. head=tail boş kabul edilir;
pending=100 bildirimi varsa dolu halka okunur.

**Kapsam sınırı:** Tekilleştirme, 101/105 alarmı ve IEC104 replay/spontane
üretici bağlantısı bu testlerle tamamlanmış sayılmaz. Host NOR modeli
fiziksel enerji kesintisini veya UART/Flash sürelerini kanıtlamaz.

## RF olaylarının IEC104 spontane ve replay aktarımı

**Amaç:** Hazır kaynak kaydı yeniden zamanlanmadan IEC104 günlüğüne
aktarılır. Online gönderim kabulünde mark_sent, başarısız gönderimde
replay kaydı ve state sync hatasında tekrar eklememe sınanır.

**Kullanım yeri:** Önceki otomatik olay servisinin aynı Ceedling komutu
kullanılır. 15 ilgili dosyada 250/250, olay servisinde 27 test geçti.
Kanıt `build/scp-replay-tests.log`. Dört yeni senaryo online gönderim,
gönderim reddi, günlük yazma ve NVRAM sync hatasıdır. Kayıt/transport
API sınırları taklit edilir; frame üretimi mevcut IEC104 paketindedir.

**Kapsam sınırı:** 101/105'in geç kayıt politikası kullanıcı cevabı bekler.
Remote ACK ve fiziksel SCADA kabulü bu host sonuçlarıyla kanıtlanmaz.

## Grup ayarı bloğu hazırlığı

**Amaç:** R1 §4.10 yazma bloğu hedef Fider_ID, writable alanlar ve sıfır
reserved/CRC ile hazırlanır; geçersiz girdiler çıktıyı bozmaz.

**Kullanım yeri:** `ceedling test:test_rf_config_scenario`. Ayar paketi
10/10; önceki ortak RF/SCP/IEC104 komutunda 15 dosyada 253/253 test geçti.
AY_06/AY_06b'deki altı gerçek WRITE bloğu üretim hazırlık/request codec'inden
geçer; 54 writable bayt kaynak örnekle aynı kalır. Fider 0/5/255, NaN,
Inf, null ve alias korunması sınanır. Kanıt
`build/scp-group-block-regression.log` içindedir. Codec bağımlılığı RF JSON
ve NVRAM integration paketine eklendi; NVRAM'de 433/433 kontrol geçti.

**Kapsam sınırı:** WRITE×3/COMMIT/STATUS sıralayıcısı, APPLIED ve NVRAM
ürün politikası henüz bu testlerin kapsamında değildir. Protokol soruları
[BOLATeX belgesinde](../doc/BOLATEXE_SORULACAKLAR.md) takip edilir.

## Grup durumunun shell üzerinden sorgulanması

**Amaç:** `rf cfg-status <group_id>` mevcut 0x28 sorgusunu gönderir; MH
durumunu gösterir. Yerel ayarın uygulanmış veya kalıcı olduğunu iddia etmez.

**Kullanım yeri:** Önceki ortak RF/SCP/IEC104 test komutu kullanılır.
Haberleşme paketindeki 69 test ve 15 ilgili dosyada 256/256 test geçti.
Üç yeni senaryo, gerçek UART/SCP yolunda örnek APPLIED gövdesi, geçersiz
argüman/busy koruması ve 0/255 kimlik sınırlarıdır. Yanıt gövdesi kaynak
bildirimden aynen alınır; CMD/TYPE/SEQ sorgu yanıtına göre çerçevelenir.
Kanıt `build/scp-group-status-regression.log` içindedir.

**Kapsam sınırı:** WRITE/COMMIT/ABORT sıralayıcısı ve BOLATeX cevabı
bekleyen BQ-10/BQ-11 tamamlanmış sayılmaz. Arıza enumuna teyit bekleyen
RTU eşlemesi notu eklenmiştir; sınıflama değiştirilmemiştir.

## Normal grup uygulama servisi

**Amaç:** Grup kimliği kontrolü → WRITE×3 → COMMIT → bildirim/STATUS_GET
zinciri gerçek rf_group, config/store ve codec ile doğrulanır. APPLIED
için bitmap ve beklenen cfg_crc gerekir; NVRAM Save değiştirilmez.

**Kullanım yeri:** Önceki ortak RF/SCP/IEC104 komutu test_rf_group paketini
de alır. 06.10.2026 koşusunda 16 dosyada 283/283 test; grup 23/23,
haberleşme 73/73 geçti. Kanıt `build/scp-group-service-regression.log`.
NVRAM ve komut/ACK envanter sınırları taklit edilir; ayar hazırlığı ve
writable CRC gerçek kodda çalışır. AY_06'nın üç EUI'si, writable bloğu,
cfg_crc=0x096D ve üç durum bildirimi ham örneklerden alınır. Orijinal
wire APPLIED bildirimi RX/ring/parser/dispatch yolunda ayrıca sınanır.

Kapsam: Üç aynı blok, eski group_id reddi, envanter değişimi, fider 4,
yarım/yanlış CRC/yanlış group/type, erken ACK/notify sırası, timeout,
PARTIAL bitmap kabul sırası, USER_ABORT bilgisi, read-only probe iptali,
transport reddi, 5 s tick wrap ve 30 s epoch beklemesi. Pending WRITE
sonucunun belirsizliğinde bilinmeyen ABORT veya yeni grup gönderilmez.

**Kapsam sınırı:** BQ-06/BQ-07/BQ-11, desired/APPLIED kalıcılık, fiziksel
MH/üç AY/RF süresi ve Powerboard geçişi bu testlerle tamamlanmış sayılmaz.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-25 | 1.0 | İlk merkezi Ceedling ve integration test yapısı |
| 2026-09-28 | 1.1 | CP56Time2a, SCP, debouncer ve atomik ring buffer testleri |
| 2026-09-28 | 1.2 | Datetime, modem config ve system status testleri |
| 2026-09-28 | 1.3 | Periodic reset, digital input ve relay testleri |
| 2026-09-28 | 1.4 | Time service ve power panic testleri |
| 2026-09-28 | 1.5 | Modül bazlı dizin yapısı ve xprintf/xscanf/RF SCP Ceedling senaryoları |
| 2026-09-29 | 1.6 | RF config, RF JSON ve GSM watchdog host senaryolarının Ceedling altyapısına taşınması |
| 2026-10-01 | 1.7 | IEC104 protokol senaryolarının Ceedling'e taşınması ve TCP parçalanma sınır testleri |
| 2026-10-01 | 1.8 | IEC104 ACK, sıra numarası döngüsü, zamanlayıcı, alıcı tamponu ve saat komutu senaryolarının genişletilmesi |
| 2026-10-01 | 1.9 | Actions Python ortamının eşitlenmesi, JUnit raporunun coverage temizliğinden korunması ve CI loglarının saklanması |
| 2026-10-02 | 1.10 | Linux host testlerinin 32 bit derlenmesi ve çalıştırma öncesi ABI kontrolü |
| 2026-10-03 | 1.11 | Token testleri, 16 kalıcı web oturum güvenliği kontrolü ve merkezi web_auth paketi |
| 2026-10-03 | 1.12 | RNG birikimi, genel GSM sayaçlarıyla fallback, main tarafından başlatılmış RNG kullanımı ve kalıcı hata yolu testleri |
| 2026-10-03 | 1.13 | Ayrı bsp_random modülü, 9 CMock testi ve web token sorumluluğunun ayrılması |

| 2026-10-03 | 1.14 | RFWU kimlik doğrulama karakterizasyonu ve ömür sayacı kayıt/reset sınırı testleri |
| 2026-10-03 | 1.15 | 25 saatlik lifetime kaydı ve periyodik reset öncesi sync hata/sıra kontrolleri |
| 2026-10-03 | 1.16 | RFWU v2 ret/resume/PC ve kripto vektör kontrolleri, secure RNG ve Python ABI uyumu |
| 2026-10-05 | 1.19 | 8 KB RF tam olay günlüğü ve örnek iletişim; 11 ilgili dosyada 157/157 test |
| 2026-10-05 | 1.21 | Otomatik RF olay tüketimi, 1/7 kalıcı ve 3 geçici listesi, 32 bit süre; 15 ilgili dosyada 246/246 test |
| 2026-10-05 | 1.22 | Commit sonrası IEC104 spontane/replay bağlantısı ve 250/250 ilgili regresyon |
| 2026-10-05 | 1.23 | Grup bloğu capture/target/CRC/invalid/alias testleri; 253/253 ilgili test, 433 NVRAM kontrolü |
| 2026-10-05 | 1.24 | Shell CFG_STATUS_GET ve 256/256 ilgili regresyon; BQ-10/BQ-11 teyit notları |
| 2026-10-06 | 1.25 | Grup sıralayıcısı/CRC/kimlik/epoch/shell; 16 ilgili dosyada 283/283 test |
| 2026-10-06 | 1.26 | SCP Powerboard modeli/JSON/FC03/RX, 20 seçili dosyada 319/319 test |
| 2026-10-06 | 1.27 | Powerboard E5–E8 servis/shell ve örnek akışlar, 21 seçili dosyada 343/343 test |
| 2026-10-06 | 1.28 | RF web Kaydet/Uygula/durum/iptal, gerçek HTTP yetki yolu ve 24 seçili dosyada 378/378 test |
| 2026-10-06 | 1.29 | Modbus sabit/yapılandırılabilir akım ve RF kaynağı; 30 seçili dosyada 411/411 test |
| 2026-10-06 | 1.30 | IEC104 RTU alım zamanı ve kalite; gerçek RX ile 31 seçili dosyada 418/418 test |
| 2026-10-06 | 1.31 | SCP enerji/yük, yeni alan adları ve RF kalite bloğu; 428/428 seçili Ceedling, web ve NVRAM doğrulaması |
| 2026-10-06 | 1.32 | Anlık arıza alanlarının kaldırılması; doğrudan olay yük biti, liste IOA hazırlığı ve 432/432 ilgili regresyon |
| 2026-10-06 | 1.33 | Gerçek arıza/IEC104 günlük shell çıktısı testleri; 172/172 ilgili regresyon ve 175 çerçeve fixture kontrolü |

## 06.10.2026 SCP Powerboard tüketici doğrulaması

`power_board/test_power_board_scp.c` gerçek modeli ve codec'i çalıştırır;
`application/test_system_status.c` gerçek JSON çıktısını da doğrular.
`application/test_modbus_power_snapshot_scenario.c` gerçek SCP modeliyle
FC03 üzerinden 38 register yanıtını, signed/32 bit alanları, eskime,
yok veri ve adres/NULL sınırlarını sınar. RF haberleşme paketinde örnek E1/E3
wire verileri gerçek ring/COBS/parser/dispatch yolundan geçirilir.

```text
cd test
C:\Ruby34-x64\bin\ruby.exe -S ceedling "test:pattern[(test_rf_|test_fault_log|test_scp|test_cobs|test_cp56time2a|test_spi_flash_log_sequence_wrap|test_iec104|test_power_board|test_system_status|test_modbus_power_snapshot)]"
```

20 seçili dosyada 319/319 test geçti; kanıt `build/scp-power-regression.log`.
Bu seçili sonuç tam suite sonucu değildir. Navigation kaynak/gömülü sayfa ve web_auth entegrasyonu
ayrıca geçti. Hedefte sıkı 16 modül derlemesi ve Release link doğrulandı;
fiziksel enerji kesintisi/Powerboard kabulü yapılmadı.

## 06.10.2026 Powerboard ayar/komut doğrulaması

`power_board/test_power_board_control.c` üretim servisi ve codec ile 22
senaryoyu çalıştırır. Transport ve E1 snapshot sınırı taklit edilir;
CFG2/sonuç algoritması taklit edilmez. PWRB_03/04 örnekleri, E7 RX dispatch,
E8 için sentetik 96 bayt ve kısa blok ret testi eklenmiştir. Ayarsız ayar,
GEN çatışması, ilk yazım, 20/32 s bekleme, yankı/E1 doğrulaması, erken sonuç,
iptal akıbeti, timeout, BOOT ve tick wrap sınanır.

Önceki bölümün seçili Ceedling komutu yeni paketi de içerir: 21 dosyada
343/343 geçti. Kanıt `build/scp-power-control-regression.log`.
18 modül sıkı Cortex-M33/C11 ile geçti; Release link başarılıdır.
Bu sonuç tam suite veya fiziksel Powerboard uygulama kabulü değildir.

## COPS LED yolunun ortak CESQ sınıflandırmasına bağlanması — 06.10.2026

Gerçek gsm_COPS_state_cb artık LED sürücüsüne doğrudan yazmaz; mevcut
gsm_signal_led_update çağrısıyla ortak valid range, 4G/3G/2G önceliği ve
strength eşiklerini kullanır. COPS'un bildirdiği RAT metadata'sı korunur.
Geçersiz/sentinel tüm güçler NONE olur; geçerli alt teknolojiye fallback
çalışır. RAT bulunmayan yanıt, ERROR ve timeout LED'i değiştirmez.

Mevcut GSM response paketine beş Unity testi eklendi. Gerçek callback,
gerçek sınıflandırıcı ve yalnız output hook/test clock/state sınırları
kullanılır. Önceki 33 testten üçü başarısızdı; düzeltme sonrası paket 33/33
geçti. Son testler doğrudan led_driver_set_gsm_mode çağrısını da CMock ile
reddeder. 2G/3G/4G reserved/sentinel, geçerli LTE endpoint ve eşik çiftleri,
fallback, metadata, eksik RAT, ERROR/timeout sınanır.

Tam Ceedling 875/875 ve integration 9/9 geçti. Saklanan JUnit envanteri
tamdır. Son stricter callback koşusu da 33/33 geçti. ARM Release gsm_engine.c
derleme/link uyarı ve hata vermedi; clean/Debug veya fiziksel LED testi değildir.
Kanıt kökü build/production-audit-2026-10-03/: cops-led-before-2026-10-06.log,
cops-led-all-host-2026-10-06.log/.xml, cops-led-final-2026-10-06.log ve
cops-led-release-2026-10-06.log. Yeni API/state/queue eklenmedi. Commit yoktur.

## 06.10.2026 RF web Kaydet/Uygula doğrulaması

Yeni `web_server/test_rf_group_web.c` web adaptörü giriş/JSON sınırlarını,
`web_server/test_http_rf_group_routes.c` gerçek HTTP router auth/admin ve
GET davranışını sınar. RF grup paketinde gerçek APPLIED → JSON → değişen
istenen ayar akışı çalışır. Web navigation kaynak/gömülü sayfada Kaydet ile
Uygula'nın ayrı olduğunu, kaydedilmemiş/boş kimlik girişini ve geç durum
yanıtının yeni önbelleği bozmadığını doğrular. web_auth gerçek handler
fonksiyonlarını ve gerçek JSON adaptörünü çalıştırır.

```text
cd test
ruby -S ceedling "test:pattern[(test_rf_|test_fault_log|test_scp|test_cobs|test_cp56time2a|test_spi_flash_log_sequence_wrap|test_iec104|test_power_board|test_system_status|test_modbus_power_snapshot|test_http_rf_group_routes|test_json_config_bounds)]"
```

24 seçili dosyada 378/378 geçti; kanıt `build/rf-group-web-regression.log`.
RF grup/adaptör/yetki alt kümesi 32/32'dir. Host sonuç fiziksel AY kabulü
sayılmaz. 19 modül sıkı ARM/C11 ile ve Release link başarıyla doğrulandı.

## 06.10.2026 Modbus RF kaynak doğrulaması

`application/test_modbus_rf_snapshot.c` sabit haritayı,
`application/test_modbus_rf_configured.c` NVRAM adres haritasını çalıştırır.
`support/modbus_rf_snapshot_fixture.h` gerçek RF cache ve FC03 üretim yolu
ile ortak fixture'dır. Envanter/store ve hardware sınırları test çiftidir;
RF tazelik/akım algoritması taklit edilmez. İki paket 5'er testtir; mevcut
Powerboard paketiyle 16/16 geçti. Son genişletilmiş seçimde 30 dosya
411/411 geçti (`build/modbus-rf-regression.log`); tam suite sayısı değildir.

Önceki RF web komutundaki `test_modbus_power_snapshot` alternatifi yerine
`test_modbus_` kullanılması reset/config ve her iki RF paketini de kapsar.
LTO/O2 mevcut monolitik Modbus senaryolarındaki gibi yalnız ilgisiz
process/hardware yollarını kaldırır; test edilen callback ve core gerçektir.
20 modül sıkı ARM/C11 ile ve Release link başarıyla doğrulandı.

## 06.10.2026 IEC104 RTU alım zamanı doğrulaması

`application/test_iec104_rf_live.c` gerçek RF modeli ve üretimde kayıtlı
IEC104 okuyucularıyla 5 test çalıştırır. `rf/test_rf_error_retry_scenario.c`
gerçek wire RX üzerinden geçerli/geçersiz RTC ve eski zamanın korunmasını
iki ek testle sınar. IEC104 fixture LTO/O2 ile ilgisiz socket/process
bağlantılarını ayırır; model/okuyucu algoritmasını taklit etmez.

Son Modbus seçiminde `test_iec104` yeni paketi zaten içerir: 31 seçili
dosyada 418/418 geçti (`build/iec104-rf-live-regression.log`). 21 modül sıkı
ARM/C11 ve Release link geçti. Bu, fiziksel RF/ölçüm gecikmesi kabulü değildir.

## 06.10.2026 SCP enerji/yük ve Modbus kalite doğrulaması

`application/test_iec104_rf_live.c` 8 test, sabit ve yapılandırılabilir
Modbus RF paketleri 8'er test çalıştırır. Gerçek RF cache ve üretim
okuyucuları LIVE bit 0/1'i, bağımsız RMS geçerliliğini, eksik/eski
örneği ve salt okunur kalite bloğunu sınar. JSON bounds paketi
49500–49520 ile alan/global adres çakışmasını, FLOAT32 ikinci
register'ı dahil, kaydetmeden reddetmeyi doğrular. Web navigation
kaynak ve gömülü sayfanın aynı doğrulamasını çalıştırır.
RF monitoründe enerji/yük satırlarının bit 0/1'i doğrudan gösterdiği,
eski/eksik/geçersiz Flags değerinin — olarak sunulduğu iki dilde sınanır.

Önceki Modbus RF komutuyla 428/428 seçili Ceedling testi geçti:
`build/rf-energy-load-regression.log`. Son IEC104 fonksiyon adı
düzenlemesinden sonra `ceedling "test:pattern[test_iec104]"` 72/72 geçti.
NVRAM integration (`integration/nvram` içinde `make run`) 433/433
kontrol geçti; alan adları değişirken kayıt yerleşiminin korunması
doğrulandı. `integration/web_auth` içindeki `make run` ve web navigation
geçti. Bu sayıların hiçbiri bütün depo test sonucu olarak sunulmamalıdır.
22 modül sıkı ARM/C11 ve Release link geçti; fiziksel kabul yapılmadı.

## 06.10.2026 Anlık arıza alanlarının kaldırılması

Kullanıcı yalnız geçici/kalıcı listelerin kalmasını istedi. Mevcut RF,
IEC104, Modbus ve JSON seçimiyle 177/177 ilgili test geçti. Geniş seçim
`build/rf-fault-lists-regression.log` içinde 432/432'dir. Yeni testler
gerçek GI group 1 isteğinde yalnız üç faz akımını, kaldırılan Modbus
offsetlerinde reserved sıfırı ve ilk liste IOA'sından hazırlanan gerçek
IEC104 gönderimini sınar. Gönderim biçimi hazırlanan paketin
06.10.2026 kullanıcı commit talebiyle onaylanmıştır; testler bu
kararın teknik davranışını doğrular.

Olay fixture'ı yakalanmış örnekteki ofset 13 bitini fault/replay/online
gönderim sınırlarında doğrudan karşılaştırır. Önceki ters dönüşüm 27
olay testinin 17'sinde gösterildi. Kayıt sürümü 3 testi geçerli CRC'li
eski sürüm 2 görüntüsünün yeni yük anlamıyla kullanılmadığını doğrular.

Web navigation kaynak/gömülü sayfada iki dilde 14 IEC104 ve 12 Modbus
canlı ayar adresini doğrular; eski arıza alanları yoktur. HTTP handler,
NVRAM 307 kontrol ve NOR arıza saklama 53 kontrol geçti. 22 modül sıkı
ARM/C11 ve Release 0 hata/0 uyarıyla geçti. Fiziksel kabul yapılmadı.

## 06.10.2026 Mevcut RF operatör onayı servisi

`rf/test_rf_error_retry_scenario.c` paketine olumlu operatör onayı
regresyonu eklendi. Gerçek yakalanmış LIVE paketi SCP RX üzerinden
restart latch'i oluşturur. Eksik/geçersiz/atanmamış kaynak ve aktif
Trip_Failed için ret; 0 flag ile latch onayı; tekrar onayın reddi;
EUI ve örnek zaman sayacının korunması sınanır. Paket 79/79 geçti:
`build/rf-alarm-ack-service.log`.

Bu test mevcut servisi doğrular; yeni web düğmesi/yazma API'si eklenmedi.
Kullanıcı onay kanalını BOLATeX'e sormak istedi; BQ-16 cevabı beklenir.

## 06.10.2026 Arıza günlüklerinde yük biti çıktısı

`application/test_fault_log.c` mevcut NOR fixture üzerinden gerçek
`fault_log_dump()` girişini sınar. Yeni
`iec104/test_iec104_event_log_shell.c` gerçek event log, spi_flash_log,
CRC ve xprintf ile public dump girişini çalıştırır. Yalnız Flash,
NVRAM, servis bağımlılıkları ve terminal çıkışı test çiftidir.
`support/shell_log_enabled.h`, bu iki paket için gerçek shell çıktısını
açar; üretim kaynaklarına test dalı eklenmedi.

Yük var/yok ile enerji var/yok birbirinden bağımsız değerlerle sınanır.
İki eski gösterim düzeltme öncesi başarısız oldu. Son koşu:

```text
cd test
ruby -S ceedling "test:pattern[(test_fault_log|test_iec104_event_log_shell|test_rf_events|test_rf_error_retry_scenario|test_iec104_protocol_scenario)]"
```

`build/rf-load-shell-regression.log`: 172/172 geçti; bütün depo suite
sonucu değildir. Fixture güncellik kontrolü 14 CSV/175 çerçeve için
geçti. Release 0 hata/0 uyarıyla tamamlandı; fiziksel kabul yapılmadı.
