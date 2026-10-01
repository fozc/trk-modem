# Test Altyapısı

**Sürüm:** 1.6
**Tarih:** 2026-09-29

**Amaç:** Firmware modüllerinin host üzerinde hızlı ve tekrarlanabilir biçimde
doğrulanmasını sağlayan ortak test giriş noktasını açıklar.

**Kapsam:** Ceedling birim testleri, host integration testleri (birlikte çalışma
testleri) ve yalnızca hedef cihazda çalışan testlerin dizin düzenini kapsar.

## Ön koşul

- Ruby ve Ceedling 1.0.1 kurulu olmalıdır.
- Host C derleyicisi ve GNU Make komut satırında bulunmalıdır.
- Web arayüzü integration testi için Node.js bulunmalıdır.
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

## Integration olarak kalan testler

| Paket | Neden Ceedling dışında çalışıyor |
|---|---|
| `contiki_process` | Gerçek Contiki sürecinin belirlenen süre boyunca çalışmasını doğrular |
| `fault_log` | Çift flash kopyası, kesilen yazma ve yeniden açılış senaryolarını birlikte yürütür |
| `gsm` | Kasıtlı timeout ve gerçek Contiki süreç yeniden başlatma akışını kullanır |
| `libiec104` | Protokol çekirdeğinin çok adımlı bağlantı ve taşıma akışını birlikte yürütür |
| `libs` | SPI flash halka kaydını yüzlerce yazma ve yeniden açılış adımıyla doğrular |
| `nvram` | Çift NVRAM görüntüsünü, yazma arızalarını ve yeniden açılışı birlikte doğrular |
| `rf_hub_sim` | Ayrı RF hub simülatörünü kendi çalıştırılabilir dosyasıyla sınar |
| `web_navigation` | Kaynak ve gömülü web içeriğini Node.js üzerinde karşılaştırır |

## Doğrulama

Başarılı koşuda merkezi komut sıfır durum koduyla biter. Ceedling JUnit XML
çıktısı `test/build/ceedling/artifacts/test/` altında oluşturulur. Coverage
çıktıları `test/build/ceedling/artifacts/gcov/` altında oluşturulur.

## Sık hatalar

- `ceedling` bulunamazsa Ruby gem kurulum dizini `PATH` değişkenine
  eklenmelidir.
- Eski object dosyaları kuşkulu sonuç üretiyorsa ilgili integration paketinin
  `build/` dizini temizlenmelidir.
- `gcovr` bulunamazsa normal test koşusu kullanılabilir; yalnız coverage
  komutu etkilenir.

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
