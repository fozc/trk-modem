# Test Altyapısı

**Sürüm:** 1.80
**Tarih:** 2026-10-26

## Mevcut IEC104 protokol regresyonları

**Amaç:** Application/libiec104 üretim girişinden komut yan etkilerini,
K/TX doluluğunda yanıtın korunmasını ve paket başına T1 süresini sınamak.

**Kullanım yeri:** test/ içinde ceedling test:pattern[iec104].
Test/P-N işaretli saat/reset/GI, tam-frame golden yanıtlar, kısmi ACK,
timestamp slotlarının yeniden kullanımı, STOPDT sırasında TESTFR,
başlangıç bildirimi retry ve bozuk akışın kapanması doğrulanmalıdır.
Python SCADA ve cihaz koşusu
[master süitinin kullanımına](system/iec104_master/README.md) göre yapılmalıdır.

## GSM SIM, SMS ve AT zamanlama testleri

**Amaç:** SIM durumunu, alınan SMS callback'ini ve AT beklemelerini sınamak.

**Kullanım yeri:** Merkezi GSM testleri ve Modbus register haritası.

`test/` dizininde `ruby -S ceedling "test:pattern[(gsm|at_engine|modbus_map)]"`
çalıştırılmalıdır. Gerçek Contiki döngüsü için depo kökünde
`make -C test/integration/gsm run_coldboot run_restart_loop` kullanılmalıdır.
Bu testler 25 ms döngüyü sanal saatle sınar; fiziksel süreyi ölçmez.

SMS testleri göndereni, çok satırlı metni, bozuk yanıtı ve hata sonrası
silme isteği oluşmamasını doğrular. SMS üzerinden komut çalıştırılmaz.
QSS testleri normal modda SIM çıkarılınca mevcut modem restart sürecine
geçildiğini ve SMS içindeki QSS metninin bildirim sayılmadığını doğrular.
SIM durumu 49409 register'ında okunmalıdır; değerler Modbus haritası §7.4'te
tanımlıdır. PUK mevcut SIM hata LED'ini kullanır.

Otomatik PIN girişi geliştirmede `GSM_AUTO_PIN_ENABLED=0` ile kapalıdır.
Üretimde açılmadan önce PIN açık derleme de aynı init testleriyle sınanmalıdır.
Depo kökünde `ruby test/scripts/run_gsm_pin.rb` çalıştırılmalıdır. Bu komut
aynı init testlerini ayrı build dizininde `GSM_AUTO_PIN_ENABLED=1` ile
çalıştırır; üretim varsayılanını değiştirmez.

Alım testleri gerçek AT motoru ve GSM callback'lerini birlikte kullanır.
Payload (taşınan veri) içindeki `NO CARRIER`, `OK`, `ERROR`, `#QSS`,
`SRING` ve NUL byte'ları değişmeden teslim edilmelidir. 1024 byte alım,
yanıtın farklı noktalardan bölünmesi, eksik payload, boş payload ve bozuk
uzunluk alanı sınanır. Kayıt sorgularındaki doğrudan `ERROR` yanıtı CEER
akışına geçmelidir; normal şebeke aramasında CGREG/CEREG sorgulamaları
sürmelidir. Okuma boyutu 1024 byte olarak korunur.

Diğer modüllerle regresyon için `test/` dizininde `ruby -S ceedling test:all`
çalıştırılmalıdır. ARM Release derlemesi donanım rehberi A.1'e göre
sınanmalıdır. Yeni `gsm_sms.c` kaynağı için eski yerel CubeIDE build
listeleri yeniden üretilmelidir.

Listener testleri, kapanma bildiriminden sonra yeni `SSENDEXT` isteği
oluşmamasını ve kapalı `SS=0` durumunda `SH` gönderilmemesini doğrular.
Prompt/payload bekleyen işlem tamamlanmalı; kapanış bir listener'a aitse
diğer listener'ın TX durumu korunmalıdır. `SH` hatası sonrası kapanma
bilgisi, socket durumu doğrulanana kadar korunur.

Cihazda tekrar için depo kökünde aşağıdaki komut kullanılmalıdır:

```text
python test/system/gsm_disconnect_test.py <modem_ip> --console COM18 --rounds 2
```

Bu test COM18'i kullanır; FIN/RST ile bağlantıyı farklı aşamalarda keser.
4096 byte POST gövdesi tamamlandıktan sonra da kesme yapılır. Her kesmeden
sonra HTTP gövdesi referans yanıtla karşılaştırılır ve eşzamanlı IEC104
TESTFR yanıtı doğrulanır. Konsol kaydında `SS=0` sonrası `SH` ve kapanma
bildirimi sonrası yeni bağlantı kabul edilmeden `SSENDEXT` bulunmamalıdır.
Röle komutu, ayar yazımı veya firmware chunk
aktarımı yapılmaz. Konsol kaydı `test/build/` altında tutulur. Testten
önce doğru firmware yüklenmeli ve harici web istemcileri kapatılmalıdır.

Host testleri gerçek SIM, SMS teslimi veya modem güç çevrimi kanıtı değildir.

## Çalışma sayaçları

**Amaç:** Konfigürasyon NVRAM yazımlarını periyodik sayaçlardan ayırmak.
**Kullanım yeri:** `Application/runtime_stats.c`, mevcut `spi_flash_log` ve
web/seri terminaldeki `stats` komutu.

Kayıt aralığı 15 dakika olmalıdır. İki 4 KiB sektör
`0x234000–0x235FFF` aralığındadır; önceki Flash alanlarının adresleri
korunur. NVRAM şeması 1 olarak kalır; sayaç alanları konfigürasyondan çıkarılmıştır.
Her kayıt 64 bayttır: 2 bayt sequence, 60 bayt payload ve en son yazılan 2 bayt CRC. Payload little-endian biçimindedir:
version (2 bayt, 1), flags (2 bayt, bit 0 RTC geçerli), UTC (4 bayt) ve
13 adet uint32_t sayaç. RTC geçersizse UTC sıfırdır.

Sayaç sırası: lifetime saniyesi, GSM RX/TX baytları, web/IEC server/IEC
client bağlantıları, IEC aktif saniyesi, GSM watchdog kurtarmaları, Modbus
istek/CRC/UART/TX hataları ve açılış sayısıdır. Bağlantı sayısı yalnız
kapalıdan bağlıya geçişte artmalıdır. IEC süresi mevcut link-active bilgisini
bir saniyelik ana süreç timer'ında örnekler. Modbus istek sayısı CRC'si doğru,
cihaza adreslenmiş istekleri (exception yanıtları dahil) ve işlenen FC06
broadcast yazımlarını içerir; başka cihazların isteklerini içermez. CRC hatası
tüm bus üzerindeki bozuk tam çerçeveleri sayar. UART sayacı RX line error
bildirimlerini, TX sayacı DMA hatası, gönderim başlatma hatası, meşgulken
düşen yanıt ve timeout yollarını sayar. GSM kurtarmaları watchdog'un soft
recovery/modül yeniden başlatma girişimleridir; MCU hard reseti bu sayaca
eklenmez. uint32_t sayaçlar taşmada modulo 2^32 devam eder.

Sahada cihaz yoktur; eski sayaçları taşıyan migration (veri aktarımı) kodu
bulunmaz. Eski lifetime ve web/IEC oturum alanları NVRAM'den çıkarılmıştır.
Şema 1'de NVRAM boyutu 1976 bayttır (önceki 1988 bayt); eski konfigürasyonlar
mevcut varsayılan ayar yolunu kullanır. Boş sayaç günlüğü sıfırdan başlar.
Açılışta Flash'a sayaç kaydı yazılmaz; ilk checkpoint (kalıcı kayıt) 15 dakika
sonunda yapılır. Açılış sayacı da bu kayda dahildir. Dolayısıyla kayıt öncesi
resetlerde boot artışı ve diğer son değişimler kaybolabilir. Önceki
kaydedilmiş toplam korunur. Kesin reset geçmişi için mevcut bootlog
kullanılmalıdır. Mevcut periyodik reset öncesinde son snapshot ayrıca
kaydedilir; tüm resetlere yeni bir flush eklenmez. Factory default işlemi
yalnız NVRAM'i etkiler; bu günlükteki cihaz ömür sayaçlarını temizlemez.

Yeni kayıt okunup sequence ve payload doğrulanmadan başarılı sayılmamalıdır.
Yarım kayıt/CRC hatasında önceki geçerli kayıt kullanılmalıdır. Açılışta SPI
okuma hatası veya bilinmeyen kayıt sürümü varsa alan silinmemelidir; kalıcı
kayıt kapalı kalırken RAM sayaçları işlemeye devam eder. Periyodik yazım hatası
loglanır ve sonraki deneme 15 dakika sonradır. Başarılı checkpoint'ler arasında
ani güç kaybında en çok yaklaşık 15 dakikalık veri kaybolur; devam eden Flash
hatasında kayıp süresi uzayabilir. Halka en fazla 128 kayıt tutar; sektör
geçişinde diğer sektörün 64 kaydı korunur (yaklaşık 16–32 saat geçmiş).

`stats` güncel RAM sayaçlarını gösterir. `stats history 0` en yeni sekiz
kalıcı kaydı CSV olarak, yeni kayıttan eskiye gösterir; `8`, `16`, … ile
sayfalama yapılmalıdır. `127` son olası kayıttır. Bu komutlar salt okunurdur.

`test/` dizininden `ceedling test:runtime_stats`,
`ceedling test:runtime_stats_shell`, GSM kenar senaryoları ve Modbus DMA
senaryosu çalıştırılmalıdır. NOR modeli gerçek spi_flash_log üzerinde yarım
yazma, kayıp CRC, silme/okuma hatası, bozuk kayıt, sektör dönüşü, boş
günlük başlangıcı, uint32_t taşması ve timer wrap yollarını sınar. HTTP JSON testi LifeTime'ın
ayrı sayaçtan geldiğini doğrular. Host testleri fiziksel Flash sürelerini ve elektrik kesintisi davranışını ölçmez.

## JSON testleri

JSON çekirdeği ve modem HTTP yolları merkezi Ceedling altyapısında sınanır.
`test/` dizininden aşağıdaki komutlar çalıştırılmalıdır:

```text
ceedling json:check
ceedling json:coverage
ceedling json:arm
```

`json:check`, Unity testlerini, bağımsız JSON/Unicode/float referanslarını,
üretim parser/writer karşılaştırmasını ve gerçek HTTP handler testini içerir.
`json:coverage` en az %95 satır, %90 dal ve %100 fonksiyon kapsamı ister.
`json:arm` için `ARM_CC` ortam değişkeni hedef derleyiciyi göstermelidir.
`JSON_BASELINE_SOURCE` verilirse aynı API'ye sahip eski çekirdekle aynı
süreçte karşılaştırma yapılır. Host süreleri fiziksel cihaz süresi değildir.

Cihazsız runner regresyonları
`python test/integration/web_device/test_roundtrip_helpers.py` ile çalışır.
Canlı cihaz betiğinde RF POST yalnız `--allow-rf-apply` ile açılır; aynı
ayarların yeniden yazılması bile gerçek RF uygulaması başlatabilir.
Kullanım sözleşmesi [kütüphane rehberinde](../Application/json/README.md),
cihaz test adımları [cihaz test rehberindedir](integration/json/DEVICE_TEST.md).
Üretilen test ve ölçüm çıktıları yerel build dizininde, Git dışında kalır.

## 23.10.2026 küçük HTTP kuyruğu ve statik gövde akışı

GSM ring 10 KiB oldu; HTML/gzip gövdeleri Flash'tan gönderilir. İlgili
Ceedling 54/54, web auth/transport, web navigation, RFWU 33/33 ve ARM
Release/EFW doğrulaması geçti. Net RAM kazancı map'te 40.392 bayttır.
100 KiB statik kaynak, 8 KiB JSON kopyası ve hata/sıralama yolları üretim
transport kodunda sınandı. Cihaz testi yapılmadı; [uygulama raporuna](reports/2026-10-23-http-static-stream.md) bakılmalıdır.

## 23.10.2026 web kuyruk boyut regresyonu

Minifier testi dinamik yanıtın gerçek GSM kuyruğuna sığmasını ve üç
gömülü HTML kaynağının byte uzunluklarını denetler. Dil testi üretim sözlük sıkıştırmasının devreye
girmesini de şart koşar. Tam web navigation, ARM Release ve imzalı paket
self-check geçti. Index gzip 49.062 → 47.057 bayt oldu; RAM kuyruğu
değişmedi. Ayrıntılar [boyut raporundadır](reports/2026-10-23-web-size.md).

## 22.10.2026 Configurator kullanım ve günlük akışı

Tam Configurator pytest 46 geçti; GUI smoke geçti. Web günlüklerinin
toplam/aralık/tümünü okuma akışı kaynak ve gömülü kodda sınandı; tam web
navigation geçti, audit 42 kontrol ve profile 40 kontrol verdi. Gerçek
yerel tarayıcıda 237 sentetik kayıtla sayaçlar doğrulandı. Cihaza yazma
ve yükleme yapılmadı. Ayrıntılar [kullanım raporundadır](reports/2026-10-22-configurator-usability.md).

## 21.10.2026 Configurator ve ortak ayar dosyaları

Yeni masaüstü düzeni 34 pytest ve GUI smoke ile doğrulandı. Program/web
ortak ayar dosyaları kaynak ve gömülü JavaScript üzerinde 40 kontrolden
geçti. Tam web navigation ve 36 endüstriyel audit kontrolü geçti.
Web 1.4.15 ARM Release ve imza self-check geçti; cihaza yüklenmedi.
Yeni arayüzle gerçek cihazda altı yalnız okuma kontrolü geçti.
Ayrıntılar [tasarım ve dosya raporundadır](reports/2026-10-21-configurator-ui-profiles.md).

## 21.10.2026 çıkış durumlarını koruma

Röle son ON/OFF komutları ve web `PreserveOutputStates` tercihi merkezi
relay, Modbus, HTTP/parser ve NVRAM testlerine eklenmiştir. Tam Ceedling
1581/1581, NVRAM integration 559/559, web navigation ve web auth geçti.
Kaynak/embedded web audit 34/34'tür. İmzalı web 1.4.14 imajıyla gerçek
cihazda kalıcılık senaryosunun 25 kontrolü geçti.

Durum değiştiren FC06 yaklaşık 221–241 ms sürdüğünden gerçek cihaz test
alıcı penceresi 200 ms'den varsayılan 1 s'ye çıkarılmıştır; garanti edilen
cihaz üst sınırı değildir. Tam Modbus koşusu **28 PASS / 2 FAIL / 4 NOT**
vermiştir. T5 aralıklı gönderim echo/readback bulgusu açıktır.
Ayrıntılar [kalıcılık raporunda](reports/2026-10-21-output-retention.md).

**Amaç:** Firmware modüllerinin host üzerinde hızlı ve tekrarlanabilir biçimde
doğrulanmasını sağlayan ortak test giriş noktasını açıklar.

**Kapsam:** Ceedling birim testleri, host integration testleri (birlikte çalışma
testleri) ve yalnızca hedef cihazda çalışan testlerin dizin düzenini kapsar.

## 21.10.2026 endüstriyel web regresyonları

**Amaç:** Oturumlar arası veri ayrımı, sayısal ayar doğruluğu, body okuma
timeout'u, grup üye tanısı ve Modbus seçeneklerini doğrulamak.

**Kullanım yeri:** `make -C test/integration/web_navigation run` içindeki
`test_industrial_audit.js`. Gerçek source ve embedded sayfa fonksiyonları
çalışır; 15 kontrol iki sürümde **30/30 PASS** verir. Gecikmiş eski oturum
body/401 yanıtları, logout'un anında temizliği, salt okunur kullanıcı,
float alanlarının korunması ve dirty browser çıkışı da kapsanır.
RF envanter/donanım sınırı mock'tur; gerçek AY silme kabulü değildir.

Web navigation ve web_auth paketleri, ARM Release ve imzalı EFW
self-check geçti. Ayrıntı ve önceki kırmızı kanıtlar
[web inceleme raporundadır](reports/2026-10-21-web-industrial-audit.md#9-uygulama-ve-kapanış--21102026).
Cihaza yükleme yapılmadı.

## 20.10.2026 ek kabul ve kalan fiziksel sınırlar

**Amaç:** İlk tam koşunun ardından kalan maddeleri mevcut RTU + MH/AY +
RS485 düzeniyle sınamak. Ayrıntılar [kapanış kaydındadır](reports/2026-10-20-acceptance.md).

**Kullanım yeri:** Yeni üretim paketinde NTP takvim doğrulaması, geçici
fixture ile Modbus timeout/HardFault ve HIL okuma sırasında halka taşması.

- Yeni yedi NTP C regresyonuyla tam Ceedling sonucu **1571/1571** oldu.
  Geçersiz tarih offset hesabından önce mevcut `dt_init` ile reddedilir.
  NTP, UTC + web TimeZone; IEC104, gelen CP56 değerini doğrudan kullanır.
- Gerçek Modbus timeout ve enjekte HAL dönüş hatalarında başarılı
  toparlanma/yeni okuma ile başarısız toparlanma/elog/yanıtsız okuma
  ayrıldı. Otomatik reset yapılmadı. Gerçek HardFault, watchdog açılışı ve
  kalıcı PC/LR/CFSR/HFSR eşleşmesi doğrulandı. Fixture üretime dahil değildir.
- `d5_wrap_during_read` artık uygulanmıştır: aynı HIL imajında üç taşma,
  366 üretilen kayıt ve 229,9 saniyelik koşu PASS. Önceki 47 vakaya bu vaka
  eklenmiştir; bunlar tek bir yeni 48-vaka koşusu gibi sunulmamalıdır.
- `m8_configuration` tam APPLIED/bitmap/CRC ölçütünü uygular. Gerçek MH
  grup 250 sonucu **FAILED/bitmap 0x04/reason 1/CRC 0000**, beklenen D07F
  oldu. Bu fiziksel kabul açık kaldı; ACK/201 ile kapatılmadı.
- Yeni fiziksel kanıt kararları için fault 4, halka 3, MH 2 ve NTP reset 3
  olmak üzere **12 yeni Python test yöntemi** eklendi ve merkezi girişlere
  bağlandı. Önceki aşamadaki 18 Python regresyonundan ayrıdır.

**Sınır:** Kullanıcı ilave ölçüm ekipmanı olmadığını belirtti. Güç kesintisi,
fiziksel akım enjeksiyonu, fiziksel DMA arızası ve osiloskop gerektiren
T1.5/echo ölçümleri açık kalmalıdır. 229,9 saniyelik taşma koşusu saha
süresinde soak veya sayısız çevrim güvencesi sayılmamalıdır.

## 20.10.2026 tam koşu ve kabul testi düzeltmeleri

**Amaç:** Merkezi host testlerini, coverage (kod kapsamı) ölçümünü ve gerçek
cihaz kanıtını ayrı kaydetmek.

**Kullanım yeri:** `ruby test/run_all.rb all` ve `ceedling gcov:all`.
Ceedling iki girişte de 112 C test dosyasında **1564/1564**, fail/ignore=0
sonucunu verdi. Merkezi 14 integration girişinin cihaz hedefi verilmeyen
alt koşuları SKIP olarak ayrılmalıdır; aşağıdaki fiziksel koşular ayrıca
çalıştırılmıştır. Ham kayıtlar `.temp_files/full-test-20261020/` altındadır.

| Cihaz koşusu | Sonuç | Kanıt |
|---|---|---|
| RF HIL, profil 1 | Uygulanan 47 vakanın tamamı PASS | İlk koşu 42 PASS/5 FAIL; test düzeltmelerinden sonra aynı imajda 5/5 tekrar PASS; `hil-final-matrix.json` |
| IEC104, üretim profili | 13/13 PASS | `iec104-production-full.log`; RTC sync, olay yazma, ACK/replay ve periodic akış dahil |
| Gerçek MH, üretim profili | Yedi vakanın tamamı farklı koşularda PASS | `real-mh-authorized`, `real-mh-event-quality`, `real-mh-bootless-fresh`; epoch ACK/201 sonucu kesin APPLIED kabulü değildir |
| Modbus, COM10 | 23 PASS/0 FAIL | `modbus-production-settled.log`; gerçek FC03 ADC sıcaklık/gerilim bloğu dahil |
| Web, üretim profili | PASS | `web-production-current.log`; ADCValid ve örnek yaşı dahil |

Coverage envanterindeki 124 üretim C dosyasının 75'i ölçülmüştür:
10856/14138 satır (%76,8) ve 6672/10015 dal (%66,6). Kalan 49 dosya
ölçülmemiştir; bu, testlerinin bulunmadığı anlamına gelmez. Bu oranlar tüm
firmware'e veya fiziksel zamanlama doğrulamasına genellenmemelidir.

**Düzeltmeler:** Web StopBits testi başlangıç slave ID/baud değerlerini
kullanmalı, başarısızlık dahil her çıkışta özgün ayarları geri yüklemeli ve
GET ile doğrulamalıdır. MH runner log komutlarının onayını sırayla beklemeli;
kurulum başarısızsa trafik vakalarını BLOCKED raporlamalıdır. Reset öncesi
konsol işareti alınarak önceki epoch kayıtları dışlanmalıdır. Programmer
2.23 için doğrulanan bağlantı seçeneği `mode=UR reset=SWRST` olmalıdır.

HIL LIVE testleri tam ve yeni konsol örneğini, mevcut ONLINE/OFFLINE ve
GECERSIZ alanlarını okumalıdır. IEC104 boşaltma bütçesi başlangıç kayıt
sayısı ve master ACK aralığından bir kez hesaplanmalı; kuyruk takılırsa
süre dolmalıdır. Eski sabit 90 saniye, 20 kayıt ve 10 saniyelik ACK aralığı
olan bu koşuda yetersizdi. Toplam **18 yeni host test yöntemi** eklendi:
web 5, MH 3, HIL assertions 7 ve HIL IEC104 3. İlgili selftest toplamları
sırasıyla 5, 24, 16 ve 7'dir.

**Derleme ve geri yükleme:** Debug `-O0` FLASH sınırını 49580 bayt aştı.
Kullanıcının onayladığı `-Og` ile tam Debug derlemesi geçti; linker/bellek
yerleşimi değişmedi. Release derlemesi ve imzalı EFW self-check geçti.
Test edilen üretim paketi CRC `1AFCFA00`, profil 0, 356664 bayttır. HIL
sonunda bu paket yeniden kuruldu; başlangıç RF/IEC104/Modbus ayarları GET
ile karşılaştırıldı ve log seviyeleri geri yüklendi. Kayıtlar topluca
silinmedi. Üretim C mantığı veya otomatik reset politikası değiştirilmedi.

**Sınır:** `d5_wrap_during_read` mevcut ertelenmiş soak vakasıdır; 47
uygulanan vakaya dahil değildir. Fiziksel güç kesintisi, gerçek arıza akımı,
DMA hata enjeksiyonu ve uzun süreli soak bu koşuda doğrulanmamıştır.

## 19.10.2026 UART toparlanması ve ADC geçerliliği

**Amaç:** GSM abort hatasını, Modbus TX tamamlanma timeout'unu ve ADC
geçerlilik bilgisinin tüketicilere aktarılmasını doğrulamak.

**Kullanım yeri:** Gerçek BSP UART toparlanması `bsp/test_uart_tx_scenario.c`
ile; AT timeout/reset girişleri `gsm/test_at_engine_scenario.c` ile sınanır.
Modbus senaryosu, yeni RX olmadan Contiki timer üzerinden TX timeout'unu,
tick taşmasını ve başarısız toparlanmada OE'nin bırakılmasını kapsar.
ADC testleri başlatma hatası, ilk örnek, hata callback'i ve temiz yeniden
başlatmayı; system_status, JSON ve gerçek FC03 testleri geçersiz ölçümü kapsar.

`application/test_elog_uart_recovery.c`, gerçek elog girişinde port, sonuç,
UART/DMA hata kodları ve aynı sonuç için port başına 60 saniyelik kayıt
sınırlamasını doğrular. Sonuç değişirse hemen kayıt alınmalıdır. Otomatik
sistem reseti eklenmemiştir; BSP'de ileride değerlendirmek için TODO vardır.

**Doğrulama:** Merkezi `ruby -S ceedling test:all` **1564/1564**, 0 fail/ignore
geçti (`build/transport-fixes-full-unit.log`). Web navigation source/embedded
testleri geçti (`build/transport-fixes-web.log`). GSM integration `make run_all`
21 cold-boot ve 19 restart kontrolü ile beklenen hang timeout'unu geçti.
Release ve paket self-check geçti (`build/transport-fixes-release.log`).
Yedi değişen üretim modülü strict C11/Werror derlemesini geçti; ADC modülünün
aynı kontrolü mevcut ST `stm32u3xx_ll_adc.h:2767` sign-conversion uyarısına
takılır. Bu vendor uyarısı değiştirilmemiştir. Host testleri fiziksel
DMA/IRQ zamanlamasını kanıtlamaz; cihaza yükleme yapılmamıştır.

## 19.10.2026 Modbus TX DMA yerel toparlanması (önceki doğrulama)

**Amaç:** Hat ayarı uygulanırken abort timeout sonrasında kanalın yerel
toparlanmasını ve sonraki gerçek FC06 yanıtını doğrulamak.

**Kullanım yeri:** `application/test_modbus_dma_tx_scenario.c` gerçek
byte-input, üretim callback ve DMA write yolunu kullanır. Reset/Init hata
sonuçları, eski ayara geri dönüş, slave-ID sırası, eksik DMA handle ve
IRQ'nun önceki durumu sınanır. DMA dosyasında net 7 yeni regresyon vardır.
HAL karşılıkları DMAT, ERROR/BUSY_TX ve Parent bağlantısının ilgili
ön koşul/yan etkilerini korur; fiziksel DMA hatası üretilmez.

Son merkezi `ruby -S ceedling test:all` koşusu **1547/1547**, 0 fail/ignore;
`build/modbus-recovery-full-unit.log`. NVRAM integration 521 passed/0 failed.
ARM Release, paket self-check ve değişen modülün strict C11/Werror derlemesi
geçti. Üretim davranışı ve kalan T1.5/echo/fiziksel kabul sınırları
[çözüm planı bölüm 6](../doc/MODBUS_CORE_COZUM_PLANI_2026-10-16.md#6-uart4-tx-dma-yerel-toparlanması--19102026)
içindedir. Cihaza yükleme veya fiziksel ölçüm yapılmadı.

## 19.10.2026 bağımsız Bolatex doğrulaması

Son merkezi Ceedling koşusu **1528/1528**, failure/ignored=0'dır.
18 Ekimdeki 35 yeni teste bu turda iki regresyon eklenmiştir:

- `rf/test_rf_events.c`: 129 açık grup boyunca gerçek store/HEAD/0x48
  yolunun ham, faz ve replay kayıtlarını saklayıp tüketmeye devam etmesi.
- `rf/test_rf_error_retry_scenario.c`: mevcut legacy bölge 0 içinde yeni
  EUI'nin aktif veya pasif satır üzerinden devreye alınmasının reddi;
  mevcut EUI'yi koruma/silme ve ret halinde journal'ın korunması.

HIL ortak `read_trip_state` okuyucusu güncel `rf live` shell çıktısına
uyarlanmıştır; mevcut dokuz host oracle testi hatalı alarm ve eksik kalıcı
kayıt beklentilerini koruyarak geçer. Simülatörün host testleri, fiziksel
HIL vakaları ve gerçek MH/AY testleri ayrı sonuçlardır.

IEC104 master decoder'ı GI20'nin zaman etiketsiz 13/1 tiplerini de
çözer; GI21/22'nin zaman etiketli 36/30 sözleşmesi korunur. Parser
22/22, vaka okuyucusu 19/19 host testiyle doğrulanmıştır. Replay süre
hesabı, durumdaki bütün backlog'u kapsar; eski kayıtlar var diye test
erken bitirilmez. Yanlış tip/CA/COT, eksik veri ve replay kaybı
kontrolleri korunmuştur.

`RF Link: BOSTA/MESGUL`, `scp_is_free()` komut meşguliyetidir; bağlantı
veya faz tazeliği kanıtı değildir. IEC104 console oracle bu satırdan
çevrimdışı sonucu çıkarmaz.

Başarısız ilk koşular, son imaj/source SHA-256 manifestleri, komutlar,
cihaz testlerinin PASS/SKIP/BLOCKED ayrımı ve kalan R2.3 teslim sınırları
[doğrulama kaydı §8](../doc/BOLATEX_UYGULAMA_DOGRULAMA_2026-10-18.md#8-bağımsız-doğrulama--19102026)
içindedir. Kapsama yüzdesi ölçülmemiştir. TEST 137 gerçek koruma olayı
değildir; eski MH/AY veya simülatör sonucu R2.3 fiziksel kabulü sayılmaz.

IEC104 için sorgu, replay, spontane olay, saat/reset/komut ve henüz
uygulanmamış işlev ayrımı
[gerçek cihaz kapsam tablosunda](system/iec104_master/README.md#19102026-gerçek-cihaz-kabul-kapsamı)
tutulur. Beş cihaz vakasının geçmesi tam SCADA kabulü anlamına gelmez.

## 18.10.2026 Bolatex R2 netleştirmeleri ve R2.3 RTU uyarlaması

**Amaç:** Üreticinin yeni kapanış, kayıt kaybı, kapasite, bölge ve bakım
kurallarını gerçek üretim girişlerinden doğrulamaktır.

**Kullanım yeri:** `rf/`, `power_board/`, `web_server/test_rf_config_parse.c`
ve mevcut `integration/web_navigation` paketi. Ayrıntılı kurallar,
komutlar ve kanıt sınırları
[uygulama/doğrulama kaydında](../doc/BOLATEX_UYGULAMA_DOGRULAMA_2026-10-18.md)
tek yerde tutulur.

Net 35 yeni C testi vardır. Eski kapanış fixture'ları üç AY ve 600+2 s
koşulunu sağlıyor; kapasite testi yeni belirsiz kapanış politikasını
doğruluyor. Testler yeni/legacy bölge 0 ayrımını, MH restart sonrasında
bölgeyi korumayı, 144 kaynak doğrulamasını ve mevcut 0x2A/90 s yolunu
kapsar. Gerçek SCP çözümlemesi ve store/HEAD/0x48 akışı kullanılır.

Son merkezi koşu 1526/1526, failure/ignored=0. Web'in kaynak ve gömülü
sayfa testleri ile altı ek host integration paketi geçti. Dört değişen
modül Cortex-M33 strict/Werror derlemesini, Release de paket self-check'ini
geçti. Cihaz portları açılmadı; R2.3 fiziksel kabulü ve gerçek arıza akımı
bu sonuçlarla tamamlanmış sayılmaz.

## 16.10.2026 Modbus röle kontrolü ve durum okuma

**Amaç:** Yerel röle 1/2 için 50002/50003 adreslerinde FC06 yazma ve
FC03 komut durumu okuma davranışını doğrulamaktır. Adres sözleşmesi
[Modbus haritası §6.1](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md#61-röle-kontrolü-ve-durum-okuma)
içindedir. IEC104 kontrolü bu değişikliğin kapsamı dışındadır.

`application/test_modbus_reset_scenario.c` gerçek RTU byte alımından
üretim callback'lerine ve yanıt paketine kadar çalışır; röle sürücüsü
sınırında test double (test karşılığı) kullanılır. Beş yeni test; iki
kanalın ON/OFF ve toplu durum okumasını, başka bir kaynaktan değişen
komut durumunu, geçersiz değerlerde iki kanalın korunmasını, broadcast
ve tanımsız adres davranışını sınar. Yanıt CRC ve FC06 echo doğrulanır.
Mevcut reset testleri korunur. Röle sürücüsünün kendi peak/hold testleri
de aynı koşuya dahildir.

Modbus/röle alt kümesi 86/86 geçti; failure/ignored yoktur. Kanıt:
`test/build/modbus-relay-unit-2026-10-16.log`. Web metadata JSON ve
kaynak/gömülü sayfa TR/EN adres tablosu testleri
`make -C test/integration/web_navigation run` ile geçti. Release derlemesi
ve paket self-check geçti; kanıt:
`test/build/modbus-relay-release-2026-10-16.log`.
Bu sonuçlar host ve derleme kanıtıdır; cihaza yükleme veya fiziksel kontak
ölçümü yapılmamıştır.

## 13.10.2026 v0.15 dizi eşleme ve kapanış regresyonları

`rf/test_rf_faults.c` aynı fiderde kesişen 142 dizi aralıklarını,
ayrık aralıkları, geç fazları ve aynı gruptaki 1/7 üstünlüğünü sınar.
1/7 için 999/1000/1001 ms sınır testleri korunur. Clock_quality 0/2,
eksik başlangıç, boot/kimlik değişimi ve kapasite kaybı kesin toplam
üretmez. 128 grup kapasitesinde açık kanıtların ezilmediği ve tamamlanan
grupların sayaçları korunarak yeniden kullanıldığı doğrulanır.

Kapanış; T_Mem_Dead, taze idle LIVE, sonraki pending=0 LIVE ve bu
kanıttan sonra gönderilmiş HEAD/boş halka sırasıyla sınanır. Başka bir
fazın gecikmiş kayıtları erken kesinleşmeyi engeller. Yeni kayıt veya
MH restart eski kapanış kanıtını iptal eder. Yazılım sahipliği cooperative
process (işbirlikli süreç) bağlamındadır; ISR atomikliği gerektirmez.

`rf/test_rf_events.c` gerçek kayıt/store/HEAD/tüketim girişlerinden
kesinleşmeyi ve eski HEAD'in reddini doğrular. Ham olarak görülmüş kayıt
ile kalıcı faz kaydı ayrılır; eşleme sonradan geçerli olduğunda kayıt
atlanmaz, gerçekten kalıcı saklanan kopya ikinci kez listeye eklenmez.
Web JSON `Pending` ve `IsFinal` taşır; source ve embedded sayfa kesin/
kesinleşmedi görünümünü merkezi web_navigation paketinde sınar.

Bu eklemede 26 Unity regresyonu eklendi. Son tam koşu 1327/1327,
0 failure/error/skipped; kanıt `build/v015-unit-commit-2026-10-13.log`.
Değişen beş üretim modülü Cortex-M33 C11/yedi zorunlu warning seçeneğiyle
Werror altında derlendi. Release geçti. Bunlar fiziksel kabul değildir.

v0.15 BQ-23 gereği m5 yalnız eşleşen ACK/201 kampanya gözlemidir.
120 eksik/tekrarlı/eski kampanyadan olabilir; nihai başarı sonraki
yapılandırmanın APPLIED/üye bitmap/CRC doğrulamasıdır.

## 12.10.2026 R2 sınır regresyonları

[R2 çalışma planının](../doc/RF_SCP_R2_AGENT_CALISMA_PLANI.md) uygulanması
sırasında iki turda 73 + tur 4/5/6 eklemeleriyle 85 yeni Unity testi eklendi (toplam 1212 → 1297, 0 hata).
Birinci tur güncel coverage envanterinin alınmayan satırlarını, ikinci tur
gcovr dal detayından "hiç alınmamış yönleri" (dead branch direction) kapattı.

| Paket | Eklenen kontroller (tur 1) |
|---|---|
| `rf/test_rf_scp_codec.c` (+10) | Olay `clock_quality` 3+ reddi; PWR_CFG2 rate maskesi 0/255 özel değerleri ve 20-200 sınırı; gelen cmd-0 ACK çözümü ve bilinmeyen komut reddi; TRIP/ANOMALY geçersiz kaynak-bölge-durum-yol reddi; PWR_ALARM sürüm/durum sınırı; CFG2 echo_valid >1 reddi; FRAM degraded >1 reddi; CFG_STATUS state/bitmap sınırı; ERROR çerçeve uzunluk kuralları (geniş gövde yalnız PWR_CFG2) |
| `rf/test_rf_events.c` (+15) | boot-protect meşgulken reddi ve envanter kapısı; drain_reason üç durumu; durdurulmuş döngüye geç yanıt zararsızlığı; fault satırı binding'ten store fallback'i ve eşleşme yoksa yalnız-ham; geçersiz yük bayrağı ve NaN akım yalnız-ham; `clock_quality=0` yerel saat + IV; alarm geçersiz/atansız kaynak; alarm geri çekilme penceresi, pencere sonrası yeniden deneme, slot EUI değişimi bekleyen kaydı bozmaz; süreç ilk başarısız kaynakta durur; LIVE damgası yoksa yerel saat; tekrar aynı durum yeniden kuyruğa almaz; LOG_AVAILABLE eşik dışı pending/head yoksayılır |
| `rf/test_rf_group.c` (+8) | RESTARTED durumunda yalnız terminal sorgu formu kabulü; gecikmiş ilerleme bildirimi UNCERTAIN→WAITING geçişi ve ABORTING'i düşürmemesi; state-0 raporu UNCERTAIN; `SCP_CMD_RESTARTED` komut sonucu hub restart; başka gruba ait STATUS ACK UNCERTAIN; abort transport reddi durumu korur; komut bekleme sırasında process sessiz; UNCERTAIN salt-okunur probe iptali |
| `rf/test_rf_config_scenario.c` (+2) | NULL sözleşme kontrolleri (defaults/copy/crc_ok/crc_compute/writable_crc/is_zero); `rf_config_crc_ok` geçerli CRC kabulü ve bayt değişimi/sıfır CRC reddi |

Tur 2 (dal yönleri; 38 test):

| Paket | Eklenen kontroller (tur 2) |
|---|---|
| `rf/test_rf_scp_codec.c` (+8) | TIME_SYNC her takvim alanının sınır dışı reddi; INVENTORY zone/fider/faz sınırları ve UPDATE'in atansız fideri kabulü; READ_RECORD/CONSUME_TO slot 100 reddi ve RANGE kuyruk 0/slot 100 reddi; 246/61/120 bayt gövde uzunluk kuralları; PWR_COMMAND üretici servis kodları reddi; CFG2 rezerv bit 7 reddi; LIVE log_pending 101 reddi; LOG_AVAILABLE/HEAD/FRAM slot 100 reddi |
| `rf/test_rf_events.c` (+19) | notify NULL/yabancı mesaj yoksayma; istenenden büyük batch durdurur; total bir modül ilerleyince yeniden çapa; paketsiz ACK ve çözülemeyen OK yanıtı durdurur; BUSY→FRAM sorgusu sırasında tekrar BUSY durdurur; sağlıklı FRAM yanıtı temiz durdurur; eşleşmeyen/boş hata yanıtları durdurur; CONSUME INVALID_PARAM taze HEAD ister; protect tüketimi kayan kuyrukla başarısız; drain left=0 ile devam eder; store taraması başka bölge/fideri atlar; negatif/aşırı büyük akım ve hatalı enerji durumu yalnız-ham; `clock_quality=1` + geçersiz takvim IV işaretler; process iletişim/envanter durumlarını bekler; alarm verisiz kaynağı yoksayar; operatör yeniden mandalli alarm tekrar kuyruğa girer; pending iken tekrar live yoksayılır |
| `rf/test_rf_group.c` (+11) | matches_config iş öncesi/sonrası ve fider/bölge/üye/in_use sapması; can_start/is_active matrisi (raporsuz, state-2 ilerleme, terminal gözlem); tam bitmap'li CRC uyuşmazlığı yeniden başlatılabilir, eksik bitmap kilitler ve poll beklemedeyken MISMATCH düşmez; start geçersiz satır/kullanılmayan store/meşgul transport/iletişim kapalı reddi; handle_status guard formları ve terminal durumlar; CHECKING boş gövde/BUSY kodu/paketsiz OK/çözülemeyen OK/yabancı grup UNCERTAIN, boşta grup WRITING; WAITING poll NULL/çözülemeyen yanıt UNCERTAIN; RESTARTED commit'siz poll etmez, hata yanıtı durumu korur, iletişim kapalıysa sorgulamaz; WRITING epoch/envanter kaybı UNCERTAIN; poll gönderim reddi tesisi korur; abort reddi matrisi (boşta, pending, meşgul, terminal) ve onaylı WRITE sonrası iptal |

Kalan alınmayan satır/dallar sınıflandırılmıştır ve test edilmediği anlamına
gelmez: codec statik tablo satırları, decode dispatch `default`ları ve
döngü-instance/helper-inline dal yuvaları (gcovr satır-dal modeli her yinelemeyi
ayrı yuva sayar), `rf_events` HEAD head/tail sınır çift-kontrolü (codec aynı
sınırı zaten reddediyor), `rf_events` verify imleç değişmezi (tutarlı
head/tail/total ile `cursor == next_slot(tail,pending)` hep korunur),
`rf_alarm` set_pending öncesi binding kaybı (tek iş parçacıklı akışta araya
giremez), `rf_faults` `UINT32_MAX` doygunluk savunması, `rf_config` tembel
init (üretim açılışta `rf_store_init` çağırır) ve `rf_group` can_start/abort
zincirlerinde kısa-devre ile erişilemez alt-koşullar. Ayrıntılar
[R2 kapsam raporu](../doc/RF_SCP_R2_TEST_KAPSAM_VE_KABUL.md) §7.1'dedir.

## 11.10.2026 RF-SCP R2 test genişletmesi

[R2 kapsam ve kabul matrisi](../doc/RF_SCP_R2_TEST_KAPSAM_VE_KABUL.md),
üreticinin 17 tekrar sınavını gerçek modül girişleriyle eşler. Yeni
discovery ve UART bridge paketleri bütün public girişlerini sınar;
codec/model/arıza/alarm/EUI/SCP paketleri sınır ve invalid-input davranışını
genişletir. RF log handler'ı gerçek `rf_log.c` ile merkez logger'a bağlanır.
Alarm binding test çifti artık `line_index` değerini sabit bir değerle
ezmez; geçerli satır ve fallback (alternatif eşleme) ayrı sınanır.

Checksum ile sabitlenen kaynak snapshot'ında 96 dosyada 1212/1212 Unity
testi geçti. Önceki 97 dosya/1223 toplamından eski I2C decoder'ın kullanıcı
tarafından kaldırılan 11 testi çıkarılmıştır; yeni 52 regresyon korunmuştur.
Envanter tamdır; failure/error/skipped yoktur. RF alt kümesi 10 dosyada
322 testtir; 52 yeni Unity testi bu çalışmaya aittir. Döngü adımları veya
Python selftest sayıları Unity toplamına eklenmez. Güncel coverage ve
fiziksel kabul sınırları kapsam matrisinin §7 bölümündedir.

`integration/rf_real_mh/run_tests.py`, cihaz seçmeden önce 18 smoke/config ve
14 parser host regresyonunu çalıştırır. Parser regresyon hatası cihaz
koşusunu durdurur. Cihaz hedefi ayarlı değilse fiziksel bölüm SKIP olur;
bu sonuç fiziksel kabul başarısı değildir. Güçlendirilen m4, request değil
eşleşen ACK ve ordinal/slot ilerlemesini; m5, aynı boot'a ait AY 120
kayıtlarını ve 201 faz maskesini de arar. Eski m4/m5 PASS'leri bu yeni
ölçütleri geçmiş kabul edilmez.

Bu çalışma fider toplamının ±1 saniyelik olay zamanı eşleştirmesini
değiştirmez. BQ-20 kapanış/belirsizlik kararı kapsam matrisinde açıktır.
Normal merkezi komutlar aşağıda korunmuştur; clean coverage komutu
mevcut merkezi build çıktısını temizler. Bu turda eşzamanlı kaynak
değişiklikleri nedeniyle son koşu byte düzeyinde doğrulanan
`build/r2-snapshot` kopyasında yapıldı. Asıl testler merkezi dizindedir.
Gcovr artık `GCOV_BUILD_OUTPUT_PATH` ile yalnız aktif koşunun object
dizinini tarar; eski build profilleri yeni ölçüme karışmaz. Kaynak manifest'i,
JUnit ve ölçülen/ölçülmeyen kaynak envanteri kapsam matrisinde belirtilmiştir.

## 10.10.2026 IEC104 HTTP ve GSM açılış regresyonları

`test/` dizininde `ruby -S ceedling test:test_http_iec_config_scenario` ve
`ruby -S ceedling test:test_gsm_init_scenario` çalıştırılmalıdır.
İlk dosya gerçek HTTP GET/POST, JSON parser (ayrıştırıcı) ve setter üzerinden
T3'ün kısmi kayıtta korunmasını ve geçersiz isteğin reddini sınar. İkinci
dosya gerçek GSM init vektörü ve dispatcher (yönlendirici) üzerinden sabit
ayarların genel init'te kalmasını, açılışın listener'a aktarılmasını,
yeniden init'te RAM önbelleğinin geçersiz sayılmasını ve ERROR yolunu sınar.
Eski SCFG/açılış adımları test vektöründe yeniden etkinleştirilerek gerçek
dispatcher üzerinden çalıştırılır; init'te açılmış soketin listener'da
tekrar açılmadığı doğrulanır. Üretim init fonksiyonları korunmaktadır.
`test_gsm_listener_scenario` ilk açılış/yeniden bağlantıda SCFG önbelleğini,
komut/yanıt sırasını ve açılış öncesinde değişen ayarın yeniden uygulanmasını
sınar. Config saklama, HTTP yanıt ve AT transport sınırları mock'tur.
Sonuçlar ve fiziksel kabul sınırı üretim raporu §10.58–10.59'da tutulmalıdır.

## 14.10.2026 tarama bulguları regresyonları

`application/test_power_panic_init.c`, açılışta GPIO okuması sonrasında
enjekte edilen düşen kenarın ve önceden kapalı IRQ'nun korunmasını sınar.
Mevcut `test_power_panic.c` episode (panik dönemi) testleri korunmuştur.
`bsp/test_bsp_rtc_write.c`, gerçek BSP setter'larında TIM17 korumasını,
bekleyen tick'i, takvim/milisaniye bütünlüğünü ve NULL girdiyi sınar.
`test_rtc_sync.c` gerçek RTC service (hizmet) doğrulamasını korur; birleşik
BSP yazımı tek callback olduğundan yalnız yazma sayısı fixture'ı günceldir.

`application/test_xmodem_update_scenario.c`, gerçek application callback
ve watchdog üzerinden overflow+COMPLETE kapanışını, boş EOT'yi,
geçersiz imaj başlığında IPC isteği gönderilmemesini ve 1 dakika timeout
sınırını sınar. Transport/Flash/reset sınırları mock'tur. Web IEC104
paketindeki gerçek parser/handler testi gövdesiz firmware POST'unu ve
aktarımı koruyarak reddini içerir. Shell event-log paketi gerçek utils
parser'ıyla geçersiz count reddini ve geçerli kayıt üretimini sınar.
Xscanf edge paketi INT32_MIN, komşu değerler ve underflow'u içerir.

Merkezi komut `ruby test/run_all.rb unit` olmalıdır. Bu eklemede 20 yeni
Unity testi vardır; son koşu 1374/1374 PASS'tir. Host IRQ modeli fiziksel
zamanlama kabulü değildir. Test sonuçları ve doğrulama sınırları
görüşmede paylaşılır.

Sonraki RF alarm log incelemesinde, `test_rf_alarm_log.c` içindeki CMock
`ExpectAndReturn`/`IgnoreAndReturn` dizisinin `true` döndürürken `out`
verisini doldurmadığı yürütülen bir probe (kanıt programı) ile gösterildi.
Fixture artık gerçek getter sözleşmesine uygun callback ile LIVE verisini
doldurur; ACK sayacı her testte sıfırlanır. ACTIVE/CLEARED renk, satır sayısı
ve log OFF sessizlik beklentileri korunmuştur. Odaklı
`ceedling test:test_rf_alarm_log` ve `ceedling gcov:test_rf_alarm_log`
koşuları 3/3 PASS'tir; bu sonuç yeni bir tam GCOV koşusu değildir.

## Kompakt dil tablosu doğrulaması

Web navigation paketindeki `test_i18n_pack.js`, gerçek minifier üzerinden
kompakt dil tablosunu ve kaynak/gzip tam sözlük eşitliğini doğrular.

## Ön koşul
- Web build/minifier testleri için Node.js 18 veya üzeri bulunmalı ve
  depo kökünde `npm ci --prefix web-page/tools --ignore-scripts`
  çalıştırılmalıdır. Ayrıntılar `web-page/tools/README.md` içindedir.
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

Kalıcı günlük ve replay (yeniden gönderim) testleri için:

```text
ruby test/run_all.rb logs
```

Bu komut seçili Ceedling testlerini tek koşuda, ardından mevcut
`integration/fault_log` ve `integration/libs` paketlerini çalıştırır.
Her paketin hatası çıkış koduna yansır. Ruby yürütücüsü Ceedling gem'ini
doğrudan başlatır; Windows batch yorumlaması regex (düzenli ifade)
seçimini parçalara ayırmaz.

### Tek paket seçme

Her integration paketi repo kökünden `make -C` ile tek başına çalışır:

| Paket | Komut | Ek koşul |
|---|---|---|
| `contiki_process` | `make -C test/integration/contiki_process run` | — |
| `fault_log` | `make -C test/integration/fault_log run` | — |
| `gsm` | `make -C test/integration/gsm run` | — |
| `iec104_master` | `make -C test/integration/iec104_master run` | `IEC104_DEVICE_HOST=<ip>` verilmezse self-test + SKIP |
| `modbus_device` | `make -C test/integration/modbus_device run` | `MODBUS_RTU_PORT` verilmezse SKIP |
| `libs` | `make -C test/integration/libs run` | — |
| `nvram` | `make -C test/integration/nvram run` | — |
| `rf_hil` | `make -C test/integration/rf_hil run` | simülatör veya HIL imajı gerekebilir |
| `rf_hub_sim` | `make -C test/integration/rf_hub_sim run` | — |
| `rf_real_mh` | `make -C test/integration/rf_real_mh run` | üretim imajı + gerçek MH + `--console COM16` |
| `rfwu_auth` | `make -C test/integration/rfwu_auth run` | — |
| `web_auth` | `make -C test/integration/web_auth run` | — |
| `web_device` | `make -C test/integration/web_device run` | cihaz web erişimi; `WEB_DEVICE_HOST` (varsayılan 188.59.76.59), cihaz tek web istemcisi hizmet verir — tarayıcı sekmesi test sırasında kapalı olmalıdır |
| `web_device` (seçili alanlar) | `python test/integration/web_device/run_roundtrip.py` | aynı cihaz erişimi; seçili alanlarda mutasyon/geri yükleme, şema ve ret kontrolleri; RF POST yalnız `--allow-rf-apply` ile çalışır |
| `web_device` (cihazsız runner testleri) | `python test/integration/web_device/test_roundtrip_helpers.py` | ağ/cihaz kullanmaz; hata yolunda geri yükleme ve yanlış PASS regresyonları |
| `web_navigation` | `make -C test/integration/web_navigation run` | Node.js |

Cihaz gerektiren paketlerin ortam değişkeni/parametre verilmeden
çalıştırılması self-test'i koşup cihaz vakalarını SKIP ile geçer (exit 0).
Örnek — IEC104 cihaz süiti tek komutla:

```text
IEC104_DEVICE_HOST=188.59.76.59 make -C test/integration/iec104_master run
```

### Bağımsız Python self-test'ler

```text
python test/system/iec104_master/test_apdu_selftest.py
python test/system/iec104_master/test_cases_selftest.py
python test/system/rf_real_mh/test_parser_selftest.py
python test/system/rf_real_mh/test_smoke_selftest.py
python test/system/rf_hil/test_sim_selftest.py
```

Ceedling JUnit XML çıktısı `test/build/ceedling/artifacts/test/junit_tests_report.xml`
dosyasına yazılır.

Coverage raporu için:

```text
ruby test/run_all.rb coverage
```

Bu komut eski Ceedling ve GCOV çıktılarını temizleyip raporu yeniden
üretir. JUnit envanteri güncel test dosyalarıyla karşılaştırılır; eksik,
başarısız veya atlanan test içeren koşu başarılı kabul edilmez.
`scripts/logic_coverage.py`, bütün `Application/**/*.c` dosyalarını
`test/build/logic-coverage.json` içinde listeler. Her ölçülen dosyada
çalıştırılan/toplam satır ve dal sayısı, çalışmayan satırlar ve eksik dal
satırları bulunur. Ölçülmeyen dosyalar `unmeasured` olarak kalır.
Test çiftleri, header'lar ve vendor kaynakları üretim toplamına katılmaz.

Bu envanterin kendi kontrolleri coverage hedefinde çalışır. Raporu yeniden
derlemeden okumak için:

```text
python test/scripts/logic_coverage.py
```

JSON toplamları yalnız ölçülen üretim C dosyalarına aittir; bütün firmware
için coverage yüzdesi değildir. Ayrı integration paketleri ve test dosyasına
doğrudan alınan kaynaklar bu dosya envanterinde ölçülmemiş görünebilir.
Kaynak adıyla ölçülmemek, hiç test edilmemek anlamına gelmez. `%100` satır
ve dal kapsamı da bütün durum dizilerinin doğru olduğunu kanıtlamaz.

Üretim açısından kritik host paketlerini birlikte çalıştırmak için:

```text
ruby test/run_all.rb critical
```

Bu hedef tüm Ceedling testlerini; ardından fault log, ortak SPI log,
GSM toparlanma, NVRAM, RFWU, IEC104 master, web auth ve web navigation
integration (birlikte çalışma) paketlerini çalıştırır. Hata olan paket
çıkış kodunu başarısız yapar. Bu bir host kapısıdır; gerçek donanım,
enerji kesintisi veya SCADA saha kabulü yerine kullanılmamalıdır.

NVRAM paketinde her iki kopyanın magic/schema/length/CRC bozulması ve
her sayfada kesilen açılış onarımı sınanır. Saklanan test değeri
varsayılandan farklıdır; yanlışlıkla default'a dönmek başarı sayılamaz.
RFWU fixture (test çifti), Flash init/write/finish çağrılarını ayrı ayrı
reddedebilir. Byte/TCP bölünmesi, CRC hatası, resume (kaldığı yerden
devam) sınırı ve reserved flags (ayrılmış bayraklar) doğrulaması gerçek
parser üzerinden yapılır; özel üretim anahtarı kullanılmaz.

Modbus testleri maksimum 125 register yanıtını, CRC ve callback
sayısını; hatalı sayım/adres, RX overflow (tampon taşması), parse
sırasında gelen byte ve tick wrap (sayaç taşması) sonrası toparlanmayı
sınar. Context (çalışma durumu) çevresindeki canary (koruma baytları)
bellek dışına yazmayı kontrol eder. Bu paket mevcut yazılım timeout
modunu sınar; fiziksel ISR zamanlaması kabulü değildir.

RTC testleri HAL Time/Date hata dönüşlerinde yazılım saati ve epoch'un
korunmasını, valid marker'ın temizlenmesini ve sonraki başarılı
eşitlemeyi sınar. `rtc_sync` ve `rtc_hw_write` artık bool sonuç döndürür.
IEC104 T3 regresyonları 20/60/65525 sınırlarını, tam T3+2 TESTFR eşiğini,
parçalı veri ile frame ayrımını ve bekleyen TESTFR_CON durumunu sınar.
`gsm/test_gsm_listener_scenario.c`, gerçek listener state machine (dinleyici
durum makinesi) üzerinden config/SCFG/OPEN sırasını ve hata halinde
bağlantının açılmamasını doğrular. Aktif bağlantıda Kaydet, sonraki bağlantıda
uygulama ve onaysız olayların korunması gerçek IEC104 TX/replay senaryosunda
sınanır. Web doğrulaması source ve gömülü HTML üzerinden ayrı çalıştırılır.
Zamanlama kabulü için gerçek GSM/SCADA koşumu ayrıca yapılmalıdır.

IEC104 protokol senaryosu CP56Time2a alan/takvim doğrulamasını ve RTC
servisinin ret sonucunun negatif cevap ve korunmuş son eşitleme
bilgisi olarak yansımasını kontrol eder. Gerçek RTC doğrulaması ayrı
`bsp/test_rtc_sync.c` paketinde, HAL sınırları taklit edilerek sınanır.

Üretilen test çıktıları şu komutla temizlenir:

```text
ruby test/run_all.rb clean
```

## Yeni test ekleme

### Mantık ve hata yolu testleri — 10.10.2026

Hedef, üretim girişlerinin geçerli/geçersiz değerlerini, sınırlarını,
durum geçişlerini ve hata sonrası veri korumasını sınamaktır. Test edilen
algoritma taklit edilmemeli; donanım ve taşıma sınırları taklit edilmelidir.
Döngü içindeki kontrol sayısı ayrı Unity testi olarak sayılmamalıdır.

| Paket | Eklenen kontroller |
|---|---|
| `iec104/test_iec104_config_edges.c` | Bütün genel ayarlar, NULL ve NVRAM meşgulken veri koruma; tüm fider/faz/15 kayıt/4 alan/geçici-kalıcı IOA kombinasyonları ve her boyutta geçersiz indeks. Mevcut adres sözleşmesi kullanılır. |
| `efw/test_efw_validation_edges.c` | Base ve identity başlıklarının her doğrulama alanı, bütün reserved byte'lar, hedef/size/version uyuşmazlıkları, bütün kısa base boyları, NULL, byte sırası ve sürüm sıralamasının tüm kararları. Bu yapısal doğrulamadır; imza kabulü değildir. |
| `application/test_app_ipc_scenario.c` | Gerçek CRC ve EFW parser ile IPC mesajı, image binding, approval/update ayrımı, erase/write/verify retleri, doğrulama öncesinde reset engeli ve terminal hata dönüşleri. Reset çağrısı host test çiftiyle sayılır. |
| `application/test_modbus_system_stats.c` | 17 register'ın tamamı, takvim yılı, ABCD word sırası, ADC kanal seçimi, uptime sınırı ve geçersiz adreslerde çıkışın korunması. |
| `application/test_modbus_gsm_stats.c` | 9 register'ın tamamı, CSQ unknown değeri, RAT, üç socket'in seçimi, hata zamanı word sırası ve NULL/adres retleri. |
| `application/test_modbus_bms_stats.c` | Bütün scalar ve array register'ları, işaretli değerler, iki yönde yuvarlama/doyum, bağımsız geçerlilik bayrakları ve geçersiz adresler. Reader etkinleştirilmez. |
| `power_board/test_power_board_control.c` | Rate/capacity/period için 256 byte değerinin tamamı; transport ret sonrası bekleyen SET, timeout sonrası cache korunması ve yeniden okuma, hatalı bildirimde önceki durumun korunması. |
| `libs/test_ring_buf.c` | 1–16 storage kapasitesinde 4096 adım; bağımsız FIFO modeliyle sıra, boş/dolu retleri, boş slot sayısı, wrap ve canary (koruma byte'ları). ISR yarış zamanı ölçümü değildir. |

**Doğrulama:** 10.10.2026 temiz coverage koşusunda 90 dosyada 1118/1118
Unity testi geçti; başarısız/atlanan test yoktur. Bu turda 43 yeni Unity
testi eklendi. Envanter aracının altı Python testi ayrı sayılır. Sekiz
kritik integration paketi de geçti. Bütün üretim C dosyalarının 69'u
Ceedling dosya coverage raporunda ölçüldü; 55'i ölçülmedi.

| Üretim kaynağı | Önce satır / dal | Sonra satır / dal |
|---|---|---|
| `iec104_config.c` | %29,61 / %28,12 | %100 / %100 |
| `efw.c` | %85,48 / %58,33 | %100 / %100 |
| `app_ipc.c` | Ölçülmedi | %100 / %100 |
| `ring_buf.c` | %97,83 / %93,75 | %100 / %100 |
| `modbus_system_stats.c` | %19,35 / %22,73 | %95,16 / %95,45 |
| `modbus_gsm_stats.c` | Ölçülmedi | %91,89 / %93,75 |
| `modbus_bms_stats.c` | Ölçülmedi | %96,59 / %97,50 |
| `power_board_control.c` | %93,49 / %79,66 | %95,44 / %84,14 |

Bu üç Modbus veri bloğunun geçerli adres aralığı bütün `case` etiketlerini
kaplar; kalan `default` yolları bu aralıktan erişilemez. Coverage yüzdesini
artırmak için protokol aralığı veya üretim kodu değiştirilmemiştir.

Sonraki kapsam çalışmasında JSON envanterindeki açık satır/dallar ve
ölçülmeyen dosyalar esas alınmalıdır. Boot/superblock, XMODEM ve güncelleme
süreçleri; GSM/IEC104 süreç geçişleri; web taşıma/oturum ve cihaz başlangıcı
ayrı davranış matrisleriyle tamamlanmalıdır. Mevcut ertelemeler ve kapalı
alt sistemler test kapsamı gerekçesiyle etkinleştirilmemelidir. Flash enerji
kesintisi ve gerçek ISR/DMA zamanlaması cihaz kanıtı gerektirir.

### Kalıcı günlük test kapsamı

#### IEC104 ACK sonrası tüketme — 10.10.2026

Kuyruğa kabul edilen kayıt, master'ın geçerli `N(R)` onayı bütün
I-frame'lerini kapsayana kadar unsent (gönderilmemiş) kalmalıdır.
Fault kaydı iki ASDU içerir; ilk ASDU'nun onayı kaydı tüketmemelidir.
S-frame ve I-frame içindeki cumulative ACK (toplu onay) aynı mevcut
doğrulama yolundan geçer. Tek kalıcı olay kaydı onay bekler; başka kayıtlar
depoda kalır. Socket/reset onay bekleyen callback'i iptal eder. ACK gelmezse
mevcut t1 ve bağlantı yönetimi geçerlidir; yeni timeout katmanı yoktur.
Canlı RF fault/alarm girişleri de aynı ACK yolunu kullanır.

`iec104/test_iec104_ack_delivery_scenario.c` gerçek Contiki replay, gerçek
IEC104 parser, gerçek `iec104_process.c` TX slotları ve gerçek kalıcı logu
birlikte çalıştırır. GSM, Flash ve NVRAM sınırları test çiftleridir.
RAM kuyruğunda kopuş, GSM kabulünden sonra ACK'siz kopuş, kısmi fault ACK,
bekleyen ACK sırasında yeni kayıt, ACK-state sync hatası, hatalı gelecek
ACK ve t1 timeout sınanır. Eski replay kaynağıyla ilk senaryo yeniden
açılışta `Expected 2 Was 0` üretmiştir; düzeltmeden sonra kayıtlar korunur.

Protokol paketinde tam/kısmi/eski/mükerrer/gelecek ACK, I-frame onayı,
15-bit wrap, reset, NULL ve ikinci ASDU'nun taşıma reddi sınanır.
Önceden `TEST_IGNORE` olan durable replay regresyonu artık çalışır.
ACK sonrası NVRAM sync başarısızsa yeniden açılışta kayıt tekrar
gönderilebilir. Bu at-least-once (en az bir kez) davranışıdır; master
uygulamasının kendi kalıcı deposuna yazdığını kanıtlamaz.

Son host koşusunda 1134/1134 Unity testi ve sekiz kritik integration
paketi geçti; atlanan test yoktur. Master i06/i07 toplama penceresi master'ın
gerçek t2 (`confirm_interval`) değerine göre bekler; c104 2.2.1 yerel
varsayılanı 10 s iken eski 6 s sessizlik sınırı erken sonuç üretiyordu.
Bu değişiklikten sonra 13/13 master vaka selftest'i geçti. Fiziksel
profile-0 i07 turu ve gerçek MH m3/m7 kabulü ayrıca yapılmalıdır.

| Dosya | Gerçek üretim girişleri ve doğrulanan davranış |
|---|---|
| `application/test_fault_log.c` | Mevcut 11 A/B senaryosu ayrı test adıyla raporlanır. Tüm fider/faz ve geçici/kalıcı listelerde 15 kayıt sınırı, yeniden açılış, sıra ve geçersiz girişte önceki verinin korunması sınanır. |
| `iec104/test_iec104_event_log.c` | Yarım payload/CRC yazımı, sessiz CRC kaybı, iç/dış CRC, bilinmeyen kayıt türü, dolu halkada erase hatası, sıra numarası taşması, araya kayıt eklenmesi, başarısız/eksik NVRAM sync ve `mark_sent` gelmeden tekrar okuma. |
| `iec104/test_iec104_durable_replay_scenario.c` | Gerçek Contiki replay süreci + gerçek `iec104_event_log` + gerçek `spi_flash_log`; yalnız taşıma ve donanım/NVRAM sınırları taklit edilir. Gönderim reddi, 150 ardışık başarısızlık ve bağlantı kesilip yeniden kurulmasında kayıtların korunması ve en yeniden eskiye sıra doğrulanır. |
| `rf/test_rf_event_log.c` | Onaylı 60 bayt ham RF kayıtları; yarım yazımdan sonra yeniden açılış ve retry, bozuk en yeni kayıttan sonra eski kaydın okunabilmesi, sektör dönüşümü ve sınırlar. |
| `libs/test_spi_flash_log_sequence_wrap.c` | Ortak NOR kütüphanesinin sıra numarası taşması ve açılış taraması. Ayrıntılı Flash hata yolları `integration/libs` paketindedir. |

`support/iec104_event_log_fixture.h`, sayfa sınırlarını ve NOR 1→0
yazımını denetler. Belirli program çağrısında kısmi hata veya sessiz
yazmama, erase reddi ve NVRAM sync reddi üretir. Çalışan replay durumu
ile son başarılı sync görüntüsü ayrıdır; yeniden açılış yardımcısı RAM'i
kalıcı görüntüden yükler. Sadece modülü tekrar başlatmak, NVRAM
kalıcılığını doğrulayan yeniden açılış olarak değerlendirilmemelidir.

Bu fixture (test çifti) gerçek NVRAM'in A/B algoritmasını taklit etmez;
o algoritma kendi NVRAM paketinde sınanır. Bu testler fiziksel enerji
kesintisi, Flash dayanıklılığı veya gerçek SCADA kabulü kanıtı değildir.

### Diğer merkezi senaryolar

RF-SCP kayıt/onay regresyonları merkezi Ceedling paketindedir:
`rf/test_rf_faults.c` dizi ve fider sayımını,
`rf/test_rf_events.c` kayıt/128 kimlik/otomatik onay/boşaltmayı,
`iec104/test_iec104_event_log.c` karma fault/alarm kalıcılığını,
`iec104/test_iec104_replay_scenario.c` gerçek replay (yeniden gönderim)
sürecini sınar. Son test, değiştirilmemiş Contiki scheduler'ını host
wrapper ile derler; vendor conversion uyarılarının yerel istisnası
production replay kontrollerine uygulanmaz. Fiziksel zamanlama kanıtı değildir.

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

## RF HIL süiti (`system/rf_hil/`)

RF hub (MH) donanımında-çevrim (HIL) testleri: `tools/rf-hil/`
simülatörü DUT'un USART3 RF hattına bağlı USB-TTL adaptör üzerinden
konuşur, DUT konsolu (COM16) etkileşimli oturumla sürülür ve doğrulama
simülatör izi (DUT'un gönderdiği çerçeveler) + konsol çıktısı üzerinden
yapılır.

- Donanımsız self-test (codec CSV golden'ları, cfg_crc, halka, grup,
  idempotency, PWRB GEN makinesi, fault motoru ve R2 model davranışları —
  81 test):
  `python test/system/rf_hil/test_sim_selftest.py`
- HIL koşusu (canlı cihaz; RF hattında simülatör, konsolda DUT):
  `python test/system/rf_hil/run_hil.py --rf-port COM10 --console COM16`
  (vaka listesi: `--list`; rapor `test/build/hil-rf/<ts>/report.md`)
- Kablolama ve simülatör kullanımı: `tools/rf-hil/README.md`.
- Merkezi host koşusuna bağlı değildir; fiziksel kurulum ister.
- SCP R2 (`022555bf`): simülatör R2 sözleşmesini modeller (0x48 koşullu
  tüketme, 0x46 sıra koruması, src_eui_hash, Boot_Counter, depo sıfırlma
  tablosu, anahtarsız MH, COMMIT ön koşulları, 0x2A/olay 201, 45 B 0xE1,
  0xE7 iptal koreografisi, kart değişimi). `cases_r2.py` 8 R2 cihaz
  vakası taşır (toplam 45 kayıtlı vaka; `d5_wrap_during_read` ertelenmiş);
  tüketim beklentileri 0x48'e göredir.

## Ceedling modül kapsamı

**Güncel envanter tarihi:** 06.10.2026. Saklanan tam koşuda 74 dosyada **875/875 test geçti; failed/ignored=0.** CESQ rapor/LED aralık düzeltmesi ve COPS ortak sınıflandırma bağlantısı doğrulanmıştır. Kaynak: build/production-audit-2026-10-03/cops-led-all-host-2026-10-06.log ve .xml. Tek paket XML'i tam envanter yerine kullanılmamalıdır.

| Test dosyası | Senaryo | Sonuç |
|---|---:|---|
| [application/test_application_utils_bounds.c](application/test_application_utils_bounds.c) | 4 | Geçti |
| [application/test_digital_input.c](application/test_digital_input.c) | 11 | Geçti |
| [application/test_fault_log.c](application/test_fault_log.c) | 2 | Geçti |
| [application/test_modbus_config_edges.c](application/test_modbus_config_edges.c) | 6 | Geçti |
| [application/test_modbus_power_snapshot_scenario.c](application/test_modbus_power_snapshot_scenario.c) | 6 | Geçti |
| [application/test_modbus_reset_scenario.c](application/test_modbus_reset_scenario.c) | 4 | Geçti |
| [application/test_modbus_system_stats.c](application/test_modbus_system_stats.c) | 5 | Geçti |
| [application/test_modem_config.c](application/test_modem_config.c) | 16 | Geçti |
| [application/test_periodic_reset.c](application/test_periodic_reset.c) | 8 | Geçti |
| [application/test_power_panic.c](application/test_power_panic.c) | 5 | Geçti |
| [application/test_relay.c](application/test_relay.c) | 9 | Geçti |
| [application/test_system_status.c](application/test_system_status.c) | 5 | Geçti |
| [application/test_time_service.c](application/test_time_service.c) | 6 | Geçti |
| [bsp/test_adc_dma.c](bsp/test_adc_dma.c) | 4 | Geçti |
| [bsp/test_bsp_delay_us.c](bsp/test_bsp_delay_us.c) | 8 | Geçti |
| [bsp/test_bsp_random.c](bsp/test_bsp_random.c) | 11 | Geçti |
| [bsp/test_rtc_sync.c](bsp/test_rtc_sync.c) | 24 | Geçti |
| [bsp/test_spi_timeout.c](bsp/test_spi_timeout.c) | 8 | Geçti |
| [bsp/test_uart_tx_scenario.c](bsp/test_uart_tx_scenario.c) | 8 | Geçti |
| [efw/test_efw_crc.c](efw/test_efw_crc.c) | 5 | Geçti |
| [efw/test_efw_parser.c](efw/test_efw_parser.c) | 6 | Geçti |
| [gsm/test_at_engine_scenario.c](gsm/test_at_engine_scenario.c) | 35 | Geçti |
| [gsm/test_at_socket_log.c](gsm/test_at_socket_log.c) | 8 | Geçti |
| [gsm/test_gsm_info_edges.c](gsm/test_gsm_info_edges.c) | 18 | Geçti |
| [gsm/test_gsm_response_scenario.c](gsm/test_gsm_response_scenario.c) | 33 | Geçti |
| [gsm/test_gsm_signal_led_edges.c](gsm/test_gsm_signal_led_edges.c) | 3 | Geçti |
| [gsm/test_gsm_socket_edges.c](gsm/test_gsm_socket_edges.c) | 10 | Geçti |
| [gsm/test_gsm_wtd_liveness_scenario.c](gsm/test_gsm_wtd_liveness_scenario.c) | 4 | Geçti |
| [gsm/test_ring_buff.c](gsm/test_ring_buff.c) | 16 | Geçti |
| [iec104/test_cp56time2a.c](iec104/test_cp56time2a.c) | 14 | Geçti |
| [iec104/test_iec104_protocol_scenario.c](iec104/test_iec104_protocol_scenario.c) | 59 | Geçti |
| [iec104/test_iec104_util.c](iec104/test_iec104_util.c) | 5 | Geçti |
| [libs/test_bms_full_map.c](libs/test_bms_full_map.c) | 8 | Geçti |
| [libs/test_bms_reader_scenario.c](libs/test_bms_reader_scenario.c) | 11 | Geçti |
| [libs/test_crc32.c](libs/test_crc32.c) | 6 | Geçti |
| [libs/test_crc32_edges.c](libs/test_crc32_edges.c) | 5 | Geçti |
| [libs/test_datetime.c](libs/test_datetime.c) | 9 | Geçti |
| [libs/test_debouncer.c](libs/test_debouncer.c) | 5 | Geçti |
| [libs/test_debouncer_edges.c](libs/test_debouncer_edges.c) | 6 | Geçti |
| [libs/test_hmac_sha256.c](libs/test_hmac_sha256.c) | 3 | Geçti |
| [libs/test_hmac_sha256_edges.c](libs/test_hmac_sha256_edges.c) | 4 | Geçti |
| [libs/test_modbus_request_length.c](libs/test_modbus_request_length.c) | 8 | Geçti |
| [libs/test_ring_buf.c](libs/test_ring_buf.c) | 6 | Geçti |
| [libs/test_sha256_edges.c](libs/test_sha256_edges.c) | 5 | Geçti |
| [libs/test_shell_color_output.c](libs/test_shell_color_output.c) | 5 | Geçti |
| [libs/test_shell_command_registration.c](libs/test_shell_command_registration.c) | 4 | Geçti |
| [libs/test_spi_flash_log_sequence_wrap.c](libs/test_spi_flash_log_sequence_wrap.c) | 2 | Geçti |
| [libs/test_w25qxx_address_encoding.c](libs/test_w25qxx_address_encoding.c) | 23 | Geçti |
| [libs/test_xscanf_edges.c](libs/test_xscanf_edges.c) | 7 | Geçti |
| [libs/test_xscanf_format_scenario.c](libs/test_xscanf_format_scenario.c) | 8 | Geçti |
| [libs/test_xsnprintf_edges.c](libs/test_xsnprintf_edges.c) | 4 | Geçti |
| [libs/test_xsnprintf_format_scenario.c](libs/test_xsnprintf_format_scenario.c) | 22 | Geçti |
| [power_board/test_power_board_control.c](power_board/test_power_board_control.c) | 22 | Geçti |
| [power_board/test_power_board_decode.c](power_board/test_power_board_decode.c) | 11 | Geçti |
| [power_board/test_power_board_scp.c](power_board/test_power_board_scp.c) | 13 | Geçti |
| [rf/test_rf_config_scenario.c](rf/test_rf_config_scenario.c) | 10 | Geçti |
| [rf/test_rf_error_retry_scenario.c](rf/test_rf_error_retry_scenario.c) | 76 | Geçti |
| [rf/test_rf_event_log.c](rf/test_rf_event_log.c) | 11 | Geçti |
| [rf/test_rf_events.c](rf/test_rf_events.c) | 27 | Geçti |
| [rf/test_rf_group.c](rf/test_rf_group.c) | 23 | Geçti |
| [rf/test_rf_scp_codec.c](rf/test_rf_scp_codec.c) | 20 | Geçti |
| [rf/test_rf_scp_codec_scenario.c](rf/test_rf_scp_codec_scenario.c) | 4 | Geçti |
| [scp/test_cobs.c](scp/test_cobs.c) | 14 | Geçti |
| [scp/test_scp.c](scp/test_scp.c) | 12 | Geçti |
| [scp/test_scp_capture_scenario.c](scp/test_scp_capture_scenario.c) | 6 | Geçti |
| [web_server/test_http_request_parser.c](web_server/test_http_request_parser.c) | 23 | Geçti |
| [web_server/test_http_request_parser_edges.c](web_server/test_http_request_parser_edges.c) | 12 | Geçti |
| [web_server/test_http_response_bounds.c](web_server/test_http_response_bounds.c) | 4 | Geçti |
| [web_server/test_http_response_edges.c](web_server/test_http_response_edges.c) | 11 | Geçti |
| [web_server/test_http_session_token.c](web_server/test_http_session_token.c) | 12 | Geçti |
| [web_server/test_http_session_token_edges.c](web_server/test_http_session_token_edges.c) | 6 | Geçti |
| [web_server/test_json_config_bounds.c](web_server/test_json_config_bounds.c) | 26 | Geçti |
| [web_server/test_json_parser_edges.c](web_server/test_json_parser_edges.c) | 14 | Geçti |
| [web_server/test_rf_json_golden_scenario.c](web_server/test_rf_json_golden_scenario.c) | 1 | Geçti |

Temel kapsam parser/sınır, RTC sync/boot/resync, SPI timeout, UART/RS-485
gönderimi, ADC DMA örnekleri, Flash WEL/JEDEC, NVRAM/lifetime, Modbus,
RF retry/COBS, IEC104 protokolü, BMS parser/reader ve formatter/web log
senaryolarıdır. Test geçişi kapalı bir alt sistemi etkinleştirmez.

Ortak host C seçenekleri `-std=c11 -pedantic -Wall -Wextra -Werror
-Wshadow -Wconversion -Wdouble-promotion -Wformat=2`'dir. Güncel dar
istisnalar `project.yml` dosyasındadır: legacy GSM senaryosunda sign,
conversion, pedantic, unused-function ve array-parameter; JSON config
senaryosunda array-parameter/conversion; RF JSON senaryosunda conversion
ve float-conversion seçenekleri kapatılır. Bu istisnalar genel uygunluk
belgesi olarak sunulmamalıdır. IEC104 paketinde eski conversion istisnası
artık bulunmaz. İlgisiz donanım yollarını eleyen LTO/O2 ve host platform
define'ları yalnız ilgili paketlere uygulanır; shell kayıt testi
`-funsigned-char` ile hedef plain-char davranışını eşler.

### SHA-256, HMAC ve AT sınır testleri — 05.10.2026

Başlanmış üç paket tamamlanmıştır; başka modüle geçilmemiştir.
29 yeni Unity testi toplamı 524'ten 553'e çıkarmıştır. SHA-256 paketi 5/5,
HMAC paketi 4/4 geçmiştir. İlk AT koşusundaki 19/20 sonucu tarihsel kayıttır;
SRING düzeltmesiyle 14 test daha eklenmiş, AT paketi **34/34** geçmiştir.

SHA-256 padding/block sınırlarında 10, HMAC key/data sınırlarında 56
bağımsız referans vektörü kullanılır. Vektörler `test` dizininde
`python scripts/generate_crypto_vectors.py` ile standart Python hashlib/hmac
üzerinden üretilir. Veri ve anahtar sentetiktir; ürün anahtarları okunmaz.
Her kripto paketinde 129 bayt mesajın 130 split noktası sınanır.
AT paketinde 29 bayt yanıtın 30 split noktası sınanır. Döngü kontrolleri
ayrı Unity testi olarak sayılmaz. Güncel coverage yüzdesi çıkarılmamıştır.

AT paketi gerçek `at_engine2.c`, GSM `utils.c` ve `ring_buff.c` çalıştırır.
UART DMA, tick ve callback sınırları test çiftleriyle ayrılır. Command ve
response kapasitesi, busy/retry/timeout, tick wrap, erken DMA completion,
abort, binary NUL/OK, parçalı SRECV ve bekleyen URC'nin resetten sonra
korunması test edilir. Host senaryoları fiziksel UART/IRQ zamanlamasını kanıtlamaz.

Yeni AT profili host define ve LTO/O2 kullanır. Eski GSM `utils.c`
include'u çevresinde yalnız `-Wconversion`/`-Wsign-conversion` tanıları
scoped diagnostic push/pop ile bastırılır; diğer yeni kodda strict
warning seçenekleri korunur. Host HAL enum'una BUSY/TIMEOUT eklenmiştir.

İlk `test_at_srecv_binary_error_and_urc_patterns_are_not_dispatched` hatası
kullanıcının yeni düzeltme talebiyle kapatılmıştır. URC/CME araması yalnız
payload dışındaki metinde yapılır; silme aralığı payload CRLF'sine dokunmaz.
Payload sonundaki sahte SRECV header'ı sayacı yeniden kurmaz.
Reset/clear binary alanları temizler ve ring buffer'ı korur; cancel
binary alanları ve ring buffer'ı temizler. Yeni kuyruk/durum eklenmemiştir.

14 yeni Unity testi; yedi URC türü, CME, binary NUL, bytewise (bayt bayt)
alım ve her split noktası, gerçek URC'lerin header öncesi/payload sonrası,
kısmi satır, timeout/reset/clear/cancel/retry, sonraki normal komut,
0/1024 bayt payload ve gerçek ERROR/CME terminal yanıtlarını kapsar.
Düzeltme öncesi 33 testin 11'i başarısızdır; nihai AT paketi 34/34 geçer.
[Hata ve düzeltme raporu](../AT_SRECV_URC_HATA_RAPORU_2026-10-05.md).
Kanıt: `build/production-audit-2026-10-03/sring-all-unit-final.log`.

### Güncel entegrasyon paketleri

Merkezi `ruby test/run_all.rb` komutu 05.10.2026 tarihinde
yeniden çalıştırılmıştır: **9/9 paket geçti**. Paket sayısı senaryo veya
assertion sayısı değildir. Log: `build/production-audit-2026-10-03/
sring-all-host.log`.

| Paket | Kapsam | Sonuç |
|---|---|---|
| contiki_process | Host süreç davranışı | Geçti |
| fault_log | Arıza kaydı/kalıcılık | Geçti |
| gsm | GSM/AT ve akış kontrolleri | Geçti |
| libs | Flash log/XMODEM/datetime ve ortak kütüphaneler | Geçti |
| nvram | Gerçek NVRAM başlangıç ve sync | Geçti |
| rfwu_auth | RFWU v2, nonce/MAC ve PC yardımcıları | Geçti |
| rf_hub_sim | RF hub simülasyonu | Geçti |
| iec104_master | Cihaz 104 kabul süiti (c104); `IEC104_DEVICE_HOST` yoksa SKIP | SKIP (hedef verilmedi) |
| web_auth | HTTP/oturum/RNG ve log gizleme | Geçti |
| web_navigation | Web sayfa/Save/adres davranışı | Geçti |
| web_device | Cihaz web API (cihaz yoksa SKIP) | SKIP (cihaz yok) |
| rf_real_mh | Gerçek MH gözlem dumanı (profile=0); `RF_REAL_MH_CONSOLE` yoksa SKIP | SKIP (hedef verilmedi) |

`iec104_master` ve `web_device` canlı cihaz ister; cihaz/hedef
bulunamazsa SKIP ile geçer, kabul koşusu için açık hedef gerekir
(`IEC104_DEVICE_HOST=... ruby test/run_all.rb integration`). IEC104
süiti `test/system/iec104_master/` içinde yaşar; kurulum (Python 3.13
venv + pinned c104) ve vaka kataloğu
[`system/iec104_master/README.md`](system/iec104_master/README.md)
dosyasındadır.

Gerçek MH duman süiti `test/system/rf_real_mh/` içinde yaşar: HIL'in
simülatör-MH'sine karşıt uç olarak gerçek huba karşı gözlem temelli 6
denetim (fw kimliği, envanter, LIVE_DATA + Boot_Counter, 0x47→0x48,
`--epoch` ile olay 201/120, `--reset-dut` ile BOOT'suz kurulum). Üretim
profili (0) önkoşuldur; katalog ve sınırlar
[`system/rf_real_mh/README.md`](system/rf_real_mh/README.md)
dosyasındadır. Tetikleme: `RF_REAL_MH_CONSOLE=COM16 make -C
test/integration/rf_real_mh run`.

Host testleri MMIO (donanım register erişimi), fiziksel IRQ/DMA süresi,
Flash güç kesintisi veya cihaz üzerinde OTA (uzaktan güncelleme) kabulünü
kanıtlamaz. Bu sınırlar ve ertelenen işler
[güncel üretim raporunda](../URETIM_HAZIRLIK_RAPORU_2026-10.md#10-güncel-üretim-durumu-ve-bulgu-tablosu) tutulur.

### IEC104 protokol senaryoları

**Amaç:** SCADA tarafından gelen baytların doğru işlenmesini ve cihazın
ürettiği APDU (uygulama protokol veri birimi) yanıtlarını doğrular.

**Kullanım yeri:** Senaryolar `iec104/test_iec104_protocol_scenario.c`
dosyasındadır. Her senaryo bağımsız başlangıç durumuyla çalışır.

| Alan | Kontrol edilen durumlar |
|---|---|
| Bağlantı | STARTDT ile aynı alımdaki devam frame'i, tekrar STARTDT ve STOPDT sonrası sıra/ACK bilgisinin korunması; STOPDT_CON öncesi bekleyen ACK'ler ve t1; taşımanın reddettiği onaylar |
| TCP veri akışı | Her bölünme noktası, tek tek gelen baytlar, birleşik çerçeveler, araya giren ilgisiz baytlar ve eksik kuyruk |
| Boyut sınırları | Geçersiz APDU uzunluğu, kısa I-frame başlığı, uzun S/U-frame, komut VSQ nesne sayısı, 253 bayt APDU sınırı, fazla büyük TCP verisi ve dolan tamponun toparlanması |
| ACK (alındı onayı) | Tekrarlanan, eski, kısmi ve henüz gönderilmemiş çerçeveyi onaylayan ACK; komut içindeki hatalı ACK |
| Sıra numarası | Beklenen sıra dışındaki giriş, alma sayacının taşması ve gönderme/ACK sayaçlarının 32.768 adımlık tam döngüsü |
| Pencere ve süre | k gönderme sınırı, w alma sınırı, t1/t2/t3 eşikleri, kısmi ACK sonrası süre ve taşıma hatasından sonra yeniden deneme |
| Interrogation (genel sorgu) | QOI 20/21/22 için k=1 ile onay-veri-bitiş sırası; taşıma doluyken devam, eksiksiz ve tekrarsız IOA kümesi, originator address korunması ve reset ile iptal |
| Reset komutu | Genel reset, bekleyen olayları temizleme, bilinmeyen QRP (reset türü), yanlış IOA (bilgi nesnesi adresi) ve eksik komut |
| Saat eşitleme | Yanıt baytları, originator address (isteği gönderen adres), milisaniye dahil RTC (gerçek zaman saati) alanları, invalid (geçersiz) biti ve eksik zaman damgası |
| Arıza verisi | Taşıma kesilince kaldığı yerden devam etme; atlanan veya yinelenen kayıt olmaması |

Aktif Type 1/13/30/31/36 nesnelerinin boyutları ve float, kalite, zaman
alanlarının ofsetleri `iec104_types.h` içindeki `_Static_assert`
kontrolleriyle host ve Cortex-M33 derlemelerinde sınanır.
`test_iec104_util` SQ=0 için paket başına 60/30/22/18/16/60 nesne
sınırlarını (Type 1/13/30/34/36/45) ve sonraki Type 36 nesnesinin
255 bayt sınırına sığmadığını doğrular. Type 34 için kapasite hesabı,
üretimde bu tipin desteklendiği anlamına gelmez.

`iec104/test_iec104_fault_query_scenario.c` gerçek QOI 23/24 uygulama
süreçlerini, protokolü ve Contiki scheduler'ı birlikte çalıştırır. K=1,
reddedilen onay/bitiş, araya giren komutta OA korunması, eşzamanlı sorgu
reddi, STOPDT sonrası devam ve soket kapanışında iptal sınanır.
ACK/delivery paketi K=1 altında arıza kaydını iki frame ile gönderir;
yalnız ikinci frame onaylandıktan sonra kalıcı unsent sayısının düştüğünü
ve yeniden başlangıçta bu durumun korunduğunu doğrular. Taşıma ikinci
frame'i reddettiğinde ilk frame tekrarlanmamalı; çağıranın kayıt tamponu
değişse de saklanan ikinci frame korunmalıdır.
Akü uyarısı testleri PowerBoard alarm kümesini, geçersiz kaynağın IV
bitini, değişiklik bildiriminin yeniden denenmesini ve GI20 kapsamını doğrular.
Eski CP24 ve Type 30/34 tanımları wire byte ve boyut testlerine dahildir;
bu kontroller Type 34 desteğini etkinleştirmez.

#### Tam frame golden vektörleri

**Amaç:** `iec104/test_iec104_golden_vectors.c` içindeki sabit byte
dizileriyle gerçek üretim girişlerinin tüm APDU yanıtını karşılaştırmak.
Beklenen diziler üretim serializer'ı veya packed C yapılarıyla
üretilmemelidir. Uzunluk ve her byte, `0x68` başlangıcından son nesnenin
son byte'ına kadar karşılaştırılır. Mevcut platform fake'i yalnız RTC,
veri kaynağı ve taşıma sınırında kullanılır.

| Type / yol | Tam frame kapsamı |
|---|---|
| 1 / 13 | GI20 içinde üç nesneli durum ve float frame'leri; ACT_CON/veri/ACT_TERM dizisinin tamamı |
| 30 | Tek nesneli spontane, üç nesneli GI22 ve iki nesneli arıza replay frame'i |
| 31 | Mevcut tek nesneli double-point göndericisi |
| 36 | Tek nesneli spontane, üç nesneli GI21, iki nesneli arıza replay; float işareti, kesir, kalite ve zaman |
| 70 / 100 | Başlangıç bildirimi ve GI20/21/22 onay/bitiş frame'leri |
| 103 / 105 | Saat ve reset komutlarının olumlu/olumsuz yanıtları; yanlış CA/COT; RTC ve reboot olayı yan etkileri |
| 45 / 46 | Desteklenmeyen komutların tam negatif yanıtları; handler etkinleştirilmez |
| APCI / TCP | I/S/U kontrol alanları, 15-bit sıra sarma, aynı TCP verisindeki iki komut ve saat komutunun bütün bölünme noktaları |

Type 1/13 için aktif üretim yolu üç fazı birlikte gönderir. Type 31
göndericisi tek nesnelidir. Sırf test için yeni tekli/çoklu gönderim API'si
eklenmez; mevcut girişler sınanır. Golden vektörler sıfır olmayan CA/OA ve
üç byte IOA, `1.0`, `-2.5`, `0.15625` float değerleri, IV/NT/SB/BL/OV
kaliteleri ve SU içeren CP56Time2a ile alan/byte yer değiştirmelerini yakalar.

Başarılı gönderimlerin tamamında başlangıç baytı, en küçük çerçeve boyutu ve
uzunluk alanının gerçek bayt sayısıyla eşleşmesi kontrol edilir. İlgili
senaryolarda yanıtın sıra numarası, COT, P/N (olumlu/olumsuz onay), adres,
veri alanları ve sayaçların son değeri de kontrol edilir.

Protokol ve golden paketleri `test/` dizininden şu komutlarla
çalıştırılmalıdır:

```text
ceedling test:test_iec104_protocol_scenario
ceedling test:test_iec104_fault_query_scenario
ceedling test:test_iec104_golden_vectors
```

**Kapsam sınırı:** Bu testler host üzerinde çalışır. Gerçek SCADA ile ağ
üzerinden birlikte çalışma, cihaz zamanlaması ve standart uygunluk testi
ayrıca yapılmalıdır. Saat komutunun `cp56time2a_is_valid()` ile takvim
doğrulaması ve RTC servisinin başarısız yanıtı gerçek komut girişinden
sınanır.

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
doc/BOLATEXE_SORULACAKLAR.md'de (yerel dosya, git dışı) takip edilir.

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
| 2026-10-12 | 1.49 | R2 sınır regresyonları: 85 yeni Unity testi (1212→1297; son ekleme: çoklu alarm batch/hata/restart pencereleri), ölü dal yönlerinin savunma/artefakt/gürültü sınıflandırması |
| 2026-10-10 | 1.41 | Kalıcı IEC104 kayıtlarının N(R) sonrası tüketilmesi, gerçek TX/ACK/log regresyonu ve master ACK aralığına uygun replay toplama süresi |
| 2026-10-10 | 1.40 | SCP R2 HIL seti: simülatör R2 modeli (81 self-test), cases_r2 (8 cihaz vakası, toplam 45), 0x48 tüketim taşıması; iec104 master i07 replay tutma vakası; rf_real_mh gerçek MH duman süiti; bench konsol yarışına yeniden deneme |
| 2026-10-10 | 1.39 | Bütün üretim C dosyalarının coverage envanteri, eksiksiz JUnit kontrolü ve sekiz modülde mantık/hata yolu testleri |
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

| 2026-10-05 | 1.17 | 51 Ceedling dosyası/524 senaryo, 9 entegrasyon paketi ve güncel warning/LTO kapsamı |

| 2026-10-05 | 1.18 | SHA-256/HMAC/AT için 29 yeni test; 54 dosya/553 test, 552 geçti ve raporlanan 1 parser hatası açık |
| 2026-10-05 | 1.19 | 8 KB RF tam olay günlüğü ve örnek iletişim; 11 ilgili dosyada 157/157 test |

| 2026-10-05 | 1.20 | SRING binary payload düzeltmesi; AT 34/34, merkezi 57 dosya/661 test ve 9 integration paketi geçti |
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
| 2026-10-06 | 1.34 | Logging açık/kapalı olay günlüğü, gönderim durumu ve hata koruması; 95/95 ilgili test |
| 2026-10-07 | 1.35 | HIL login/komut adı ve imaj/profil/başarı kapıları; web auth, 79 RF, 57 sim ve 9 kanıt kapısı testi |

## 06.10.2026 modül test genişletmesi

GSM socket/info ve Modbus config için ilk Ceedling paketleri eklenmiştir.
CRC/debouncer sınırları genişletilmiştir. Beş yeni dosyada 43 test ve mevcut
PowerBoard FC03 paketinde dört ek test ile **47 yeni Unity testi** oluşmuştur.
Yeni/uyarlanan altı pakette 49 test: 43 geçti, 6 CESQ regresyonu başarısızdır.
Üretim kodu değiştirilmemiştir; önemli düzeltme önerileri son değerlendirmede
[modül kapsam raporuna](../CEEDLING_MODUL_KAPSAM_RAPORU_2026-10-06.md) yazılmıştır.

PowerBoard sağlayıcı test sınırı canonical API'ye uyarlandı. Web auth
entegrasyonu yeni gerçek JSON modülüne bağlandı. Integration ilk koşuda
8/9, bu test bağlantısı düzeltildikten sonraki web_auth koşusu başarılıdır.
Diğer çalışmalardaki 10 yeni test nihai 835 toplamına dahildir;
bu modül test çalışmasının katkısına mal edilmez.

```text
python test/scripts/test_test_inventory.py
python test/scripts/test_inventory.py --junit test/build/production-audit-2026-10-03/module-expansion-final-2026-10-06.xml
```

İlk komut envanter aracının 7 Python unittest'ini çalıştırır. Bunlar Unity
sayısına eklenmez. İkinci komut repo kökünden tam envanteri raporlar;
altı açık regresyon nedeniyle sıfır olmayan sonuç beklenir. Eksik/duplicate,
stale suite ve count uyuşmazlığı da başarı sayılmaz. Tam koşunun JUnit kopyası
başka tek paket koşularından önce saklanmalıdır. Eski başarılı test sayıları
bu son başarısız koşunun yerine kullanılmamalıdır.

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
Bu seçili sonuç tam suite sonucu değildir; yukarıdaki GSM CESQ regresyonu
ayrı açık bulgudur. Navigation kaynak/gömülü sayfa ve web_auth entegrasyonu
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

## 06.10.2026 IEC104 olay günlüğü logging kapalı yapı

`iec104/test_iec104_event_log.c` varsayılan NO_SHELL_LOG yapısını,
`test_iec104_event_log_shell.c` gerçek shell çıktısını çalıştırır. İkisi
`support/iec104_event_log_fixture.h` üzerinden aynı NOR modeli ve üretim
spi_flash_log/CRC girişlerini kullanır. Algoritma taklit edilmez.

En yeni gönderilmemiş kaydın yük/süre alanları, yeniden init, yanlış sıra
onayı, aralığın tüketilmesi, NULL giriş ve yazma hatasında sıra/durumun
korunması sınanır. Logging kapalı ilk derleme kullanılmayan `sent`
değişkeninde hata verdi; hesap SHELL_LOG argümanına taşındıktan sonra
iki yapı da geçti. Seçim:

```text
cd test
ruby -S ceedling "test:pattern[(test_iec104_event_log|test_rf_events|test_fault_log|test_iec104_protocol_scenario)]"
```

`build/iec104-event-log-state-regression.log`: 95/95 geçti. Release
0 hata/0 uyarıdır. HIL raporunu inceleme sırasında ayrıca simülatör
self-test'i port açılmadan 57/57 geçti; bu sayılar fiziksel kabul veya
tek bir birleşik test sayısı olarak sunulmamalıdır.

## 07.10.2026 HIL yazılım bulguları

Web auth integration, gerçek login handler/yardımcılarını çalıştırır.
Whitespace/alan sırası/ASCII escape ve bozuk/uzun/NUL girişte oturumun
korunması sınanır; mevcut token/lockout testleri de geçer.

`integration/rf_hil/run_tests.py`, 57 simülatör testini ve yeni
`system/rf_hil/test_hil_identity.py` içindeki 9 testi çalıştırır. Gerçek
runner'da eksik/yanlış/değişen imaj, profil, boş/ertelenmiş veya tekrarlı
seçim sınanır. Üretim status callback'i gerçek boot header'ıyla 0/1 host
C derlemesinde doğrulanır. Serial/console sınırları test çiftidir.

RF gerçek SCP RX paketi 79/79 ve Release 0 hata/0 uyarıyla geçti.
Fiziksel HIL veya yükleme yapılmadı; yeni imajla yeniden koşu gerekir.

## 08.10.2026 RF HIL kabul kontrolleri

Merkezi `integration/rf_hil/run_tests.py` paketi simülatör self-test,
imaj/profil kimliği ve `test_hil_assertions.py` negatif kabul testlerini
birlikte çalıştırır. Bu üç paket port açmaz.

Alarm HIL testleri yalnız çıktıdaki alan adını değil kalıcı IEC104 alarm
kaydını, gerçek latch değerini ve kalıcı faz arıza sayacındaki değişimi
denetler. 101, aynı açma önceden bilinmiyorsa kalıcı arıza ve alarmdır;
105 yalnız alarmdır. Konsol `(zaman, metin)` satırları doğru ayrılır.
Başlangıçtaki boş CONSUME, sonraki bekleyen kayıtları başarı saydıramaz.

`cases_recovery.py` kayıp LOG_AVAILABLE sonrası periyodik okuma, sabit
uptime'da ölçüm kalitesi ve COMMIT sırasında MH restart senaryolarını
tanımlar. `d5_wrap_during_read` uzun süreli HIL testi hâlâ ertelenmiştir.
Simülatör `add_events` eyleminde isteğe bağlı `boot_counter` alır;
varsayılan 7 korunur. Ayrı AY açılışları açıkça farklı kimlikle üretilir.
`log_bell_enabled=false` kayıtları koruyup bildirimleri susturur.

Donanım koşuları ve ilk başarısız izler `build/rf-hil-2026-10-08-*`
altındadır. Sayısal sonuç ve kalan fiziksel sınırlar üretim hazırlık
raporunun ilgili bölümünde tutulur; ilk koşu raporları sonradan değiştirilmez.

## RF web doğrulama incelemesi — 14.10.2026

26 kaynak dosyası `test/build/rf-web-final-evidence/source-hashes.json`
ile birebir eşleşti. Son tam Ceedling koşusu 1354/1354; ajanın tekrar
koşusu `build/e2e2-rf-unit.log` içinde 464/464'tür. Web auth ve kaynak/gömülü
navigation sonuçları `build/e2e2-webauth.log` ve `build/e2e2-webnav.log`
içindedir. Canlı web_device 15/15 sonucu ajan tarafından bildirildi;
bu incelemede ayrı son koşu logu bulunmadığı için bağımsız doğrulanmış
bir tam kabul sonucu olarak kullanılmamalıdır.

`build/e2e2-move-watch.log` içinde silme ACK → SET ACK → END ACK sırası
vardır. Ancak S→T taşımasından sonra LIVE paketlerinin source alanı 6
olarak kaldı; `build/e2e2-mon1.json` T fazında HasData=false gösterir.
RTU source alanından fider/faz çözer (`rf_inventory_get_binding`).
ACK başarısı, taşınan cihazın LIVE akışının yeni fazda doğrulandığı
anlamına gelmez. MH/AY davranışı ve RTU eşlemesi birlikte incelenmelidir;
kök neden veya yenileme süresi bu kayıtlarla kesinleşmemiştir.

Aynı hedef POST sonrası restart, kayıtlı hedefin korunmasını gösterir.
Bekleyen silme işleminin ACK öncesinde fiziksel kesilmesi ve eski EUI
listesinden kurtarılması bu koşuda yakalanmadı. Host replay/NOR kesinti
testleri geçmiştir; fiziksel güç kesintisi kabulü ayrıca yapılmalıdır.


## Modbus register adresleri web doğrulaması — 15.10.2026

**Amaç:** Cihazdan okunan salt okunur register listesinin adres, veri türü
ve arayüz davranışını doğrulamak.

`web_server/test_modbus_map_json.c`, gerçek JSON üreticisinin alanlarını,
buffer (tampon) sınırlarını ve hata çıktısını sınar.
`test_http_iec_config_scenario.c`, Modbus GET yanıtındaki mevcut ayarlar
ile `RegisterMap` alanını ve yetersiz yanıt tamponunda HTTP 500 davranışını
üretim handler (istek işleyici) girişinden doğrular.

`make -C test/integration/web_navigation run`, aynı üreticiyi host üzerinde
çalıştırır. JSON alanları register bloklarını boşluksuz ve çakışmasız
kapsamalıdır. Kaynak ve gömülü sayfa testleri; cihazdan gelen adreslerin
kullanılmasını, fider/faz/kayıt seçimini, iki dili ve Kaydet isteğinin yalnız
`CihazID` ile `BaudRate` içermesini doğrular. Komut adresi belgelenir;
web üzerinden reset komutu verilmez.

**Sınır:** Host testleri ve Release derlemesi fiziksel cihaz kabulü değildir.
`web_device` paketindeki yeni harita kontrolleri cihaz yüklendikten sonra
ayrıca çalıştırılmalıdır.

**Sonuç:** 15.10.2026 koşusunda tam Ceedling paketi 1381/1381 geçti.
Web navigation, oturum/taşıma testleri ve imzalı Release self-check geçti.
Koşu kayıtları `.temp_files/modbus-register-ui-*.log` altındadır.
Bu değişiklik cihazda denenmedi.


## Web operatör metinleri ve durum regresyonları — 15.10.2026

`make -C test/integration/web_navigation run`, kaynak ve gömülü iki web
sayfasını Türkçe/İngilizce sınar. Form değerleri dil değişiminde korunur;
yeniden GET, kaydedilmemiş değişikliklerde onay bekler. İptal edilen okuma
istek göndermez. POST kabulü cihazdan yeni okuma gibi gösterilmez.

RF izleme testleri ham sayaç/FSM/servis kodlarının görünmediğini,
fiziksel ölçümlerin ve LIVE örneğinden bağımsız açma alarmının korunduğunu
doğrular. UTC etiketleri, gerçek arıza IOA aralıkları ve PIN/SBO sınırları
üretim JavaScript girişleriyle sınanır. Konsol bekleme süresi sonu belirsiz
sonuç olarak bildirilir.

Firmware sayfası yeniden başlatma testleri kabul, ret, HTTP hatası,
bozuk JSON, bağlantı hatası ve kullanıcı iptalini kapsar. Yalnız doğrulanmış
kabulde başarı mesajı beklenir. Bu host kontrolleri cihazın fiziksel olarak
yeniden başladığını kanıtlamaz. Web paketi, oturum/taşıma testleri ve son
imzalı Release self-check geçti; cihaz yüklemesi yapılmadı.

## Web ilk açılışında eksik HTML regresyonu — 16.10.2026

**Amaç:** Gömülü HTML büyüdüğünde HTTP yanıtının kesilmesini önlemek.

`make -C test/integration/web_auth run`, gerçek HTTP router, yanıt üreticisi
ve GSM HTTP gönderim kodunu çalıştırır. Güncel gömülü index ve firmware
sayfalarının modem sınırına kadar her baytı karşılaştırılır. Önceki 32 KiB
tamponla index için beklenen 38.151 bayt yerine 31.744 bayt gözlenmiştir.

Kullanıcı kararıyla HTTP kuyruğu ortak `ring_buff` ile 48 KiB yapılmıştır.
Kullanılabilir kapasite, bir bayt rezerv nedeniyle 49.151 bayttır. Testler
bu kullanılabilir kapasitenin tamamını,
kapasite aşımında mevcut verinin korunmasını, sarma sırasında doğru sırayı
ve bağlantı sıfırlamada eski yanıtın temizlenmesini kapsar. Gömülü sayfa
boyutları üretim sırasında header dosyalarına yazılır; sayfa ve 512 bayt
başlık payı kuyruğa sığmazsa `_Static_assert` derlemeyi durdurur.
`web_navigation` converter testi üretilen boyut sabitini de doğrular.

**Sonuç ve sınır:** İki web integration paketi ve imzalı ARM Release
self-check geçmiştir. Host testi fiziksel GSM aktarımını kanıtlamaz;
bu düzeltme cihaza henüz yüklenmemiştir.

## Ortak ring buffer ve atomik destek kontrolleri — 16.10.2026

**Amaç:** Her pozitif tampon boyutunda SPSC davranışını ve desteklenmeyen
derleme hedeflerinin açıkça reddedilmesini doğrulamak.

**Kullanım yeri:** `gsm/test_ring_buff.c` merkezi Ceedling paketindedir.
11 boyutta toplam 55.000 işlem doğrusal modelle karşılaştırılır. Küçük
boyutlarda 1.785 head/tail durumu, 48 KiB tam/dolu ve sarılmış bloklar,
zero-copy sınırları, geçersiz yeniden başlatmada verinin korunması ve
`libs/ring_buf` ile ortak byte işlevlerinin karşılaştırması kapsanır.

`make -C test/integration/libs run`, mevcut Flash log testleriyle birlikte
`run_ring_buffer_checks.py` çalıştırır. İki gerçek kütüphane, iki thread
ile 7 boyutta ve hem `-O0` hem `-O2` altında sınanır. Toplam 5.600.000 byte
sırası/içeriği denetlenir. GSM sürümünde byte, blok ve zero-copy yolları
karıştırılır. C99, `__STDC_NO_ATOMICS__=1` ve eksik `stdatomic.h` için
beklenen derleme hataları kontrol edilir.

ARM kontrolü aynı girişe `ARM_CC=arm-none-eabi-gcc` verilerek etkinleştirilir;
derleyici PATH'te yoksa tam yolu kullanılmalıdır. GCC 14.3.rel1 ile M0/M0+
lock-free koşulunu sağlamadığı için reddedilir; M23/M3/M4/M7/M33 kabul
edilir. Karar CPU adından değil, gerçek atomik indeks türünün derleyici
garantisinden alınır. M33 `-O0`/`-O2` assembly çıktıları acquire/release
komutlarını ve atomik runtime, bölme, exclusive RMW veya IRQ maskelemesi
bulunmadığını sınar. ARM aracı verilmezse bu bölüm açıkça SKIP yazmalıdır.
Kanıt dosyaları `test/build/ring_buffer/` altındadır.

**Atomik erişim sınırı:** GSM USART1 ISR üreticidir; AT process tüketicidir.
RF için normal derlemede USART3, HIL derlemesinde UART4 üreticidir;
RF process tüketicidir. İndekslerin hazırlanması alım kesmesi açılmadan
önce tamamlanır. Veri sahipliği acquire/release ile devredilir; aynı
kuyrukta ikinci üretici veya tüketici desteklenmez. Veri byte'ları ve
değişmeyen boyut/pointer alanları atomik değildir. HTTP tek process
bağlamında aynı kütüphaneyi kullanır; ayrı atomik doluluk sayacı veya IRQ
kilidi yoktur. Kütüphane atomiklerini HTTP için kaldıran ikinci bir sürüm
eklenmemiştir. `libs` kurucusunda gereksiz `seq_cst` yerine paylaşım öncesi
`atomic_init` kullanılır. Blok okuma/yazma ve `libs` byte yazmada aynı
indeksin gereksiz ikinci kez okunması kaldırılmıştır; her işlem tek
snapshot kullanır.

**Sınır:** Thread testleri ve assembly incelemesi fiziksel ISR gecikmesini,
WCET'i veya cihaz üzerinde GSM/RF kabulünü kanıtlamaz. TSan çalıştırılmamıştır.

**Sonuç:** Son tam Ceedling paketi 1406/1406 geçti. `libs`, `gsm` ve
`web_auth` integration hedefleri geçti. ARM platform matrisi ve M33 assembly
kontrolü ayrıca çalıştırıldı; imzalı Release self-check geçti. Koşu kayıtları
`.temp_files/web-startup/ring-*.log` altındadır. Cihaza yükleme yapılmadı.


## Giriş ve gerçek ilerlemeli açılış — 16.10.2026

**Amaç:** Ana adreste küçük giriş ekranı sunmak, yalnız başarılı girişten
sonra yapılandırma sayfasını aktarmak ve gerçek ilerlemeyi göstermek.

**Kullanım yeri:** `/` ve `/loading.html` aynı küçük ekranı sunar.
`/index.html`, yükleme ekranına uğramadan mevcut yapılandırma sayfasını
ve kendi giriş ekranını açar. Doğrudan HTML erişimi bilerek herkese
çıktı verir; yapılandırma API'leri oturum korumasını sürdürür.

**Akış:** Gzip ile 2.662 baytlık giriş ekranı mevcut `/auth/login` ucunu
kullanır. Başarısız girişte büyük sayfa istenmez; mevcut deneme sınırı
korunur. Token ve rol doğrulandıktan sonra `/index.html.gz?t=...`
istenir. Bu uç oturum gerektirir; normal HTML'den farklı olarak
`Content-Encoding` olmadan `application/gzip` sunar. Uygulama gövdesi
41.518 bayttır. İlerleme `Content-Length` üzerinden hesaplanmalıdır;
KB gösterimi 1.000 bayt tabanlıdır. HTTP/TCP başlıkları, giriş isteği
ve küçük giriş ekranı bu göstergede sayılmaz.

Alınan toplam bildirilen boyutla eşleştiğinde tarayıcının
`DecompressionStream` API'si gzip verisini açar. Belge yapılandırma
HTML'iyle değiştirilir. Aynı pencerede bir kez tüketilen token/rol
bilgisi ikinci giriş ekranını önler; API yetkisini sunucu denetler.
Önceden saklanmış token tek başına doğrudan giriş ekranını atlatmaz.
Şifre tarayıcıda saklanmaz; POST başlatılırken formdan temizlenir.

**Arayüz:** Türkçe giriş formunu “Sayfa yükleniyor…” başlığı, gerçek KB
ve yüzde izler. Çift yükleme animasyonu, İngilizce başlıklar ve sürekli
gezinti bağlantıları kaldırılmıştır. Sistem yazı tipi kullanılır; dış
font, resim veya JavaScript paketi gerekmez. HTML/CSS/JavaScript mevcut
minifier (küçültücü) ile işlenir ve gzip ile sıkıştırılır. Aktarım veya tarayıcı uyumluluğu hatasında
tekrar deneme veya `/index.html` için “Doğrudan aç” bağlantısı sunulur.
Yanlış şifrede bu bağlantı gösterilmez.
Desteklenmeyen tarayıcı, geçersiz oturum yanıtı, HTTP hatası,
eksik/fazla veri, bozuk gzip ve 30 saniye yanıtsızlık denetlenir.

**Cache (önbellek):** Doğrulanmış gzip paketi ve ETag tarayıcının
`localStorage` alanında, `tsb23.pageCache` anahtarındaki tek JSON değeriyle
saklanır. Şifre, token veya rol bu kayda eklenmemelidir. Her giriş token'ı
paketin URL'sini değiştirdiği için yalnız tarayıcının normal URL cache'i
tekrar girişte yeterli değildir. Küçük ekran, güncel token'la yaptığı
isteğe `If-None-Match` ekler. Sunucu önce oturumu doğrular; aynı gzip
baytları için 304, değişen içerik için 200 ve tam gövde gönderir. Yanıt
ETag'i sıkıştırma türü ve gerçek içerikten SHA-256 ile üretilir. Paket
`private, no-cache` politikasındadır; loader'ın manuel doğrulama isteği
`cache: no-store` kullanarak otomatik tarayıcı cache'ini devreye almaz.
304 yanıtında saklanan ETag ile yanıt ETag'i eşleşmelidir.

304 sonrasında “Önbellekten açılıyor” gösterilir; ağdan alınmamış veri
indirilmiş gibi sayılmaz. Gzip yine açılıp doğrulanmalıdır; bozuk cached
gzip silinir ve hata bildirilir. Saklama alanı kapalı/doluysa veya kayıt
biçimi bozuksa normal indirme sürer. Ağdan doğrulama başarısızken eski
sayfa otomatik açılmaz; uygulama kaydı çevrimdışı giriş sağlamaz.

**Doğrulama:** `web_navigation/test_loading.js`, kaynak ve gömülü
script'in üretim girişini gerçek gzip/stream API'leriyle çalıştırır.
Giriş öncesi sıfır istek, admin/user rolü, çift gönderimi önleme,
şifreyi temizleme, token devri, gerçek bayt ilerlemesi ve 22 hata yolu
kapsanır. `test_navigation.js`, doğrudan giriş ile bir kez tüketilen
başarılı giriş devrini ve eşleşmeyen token davranışını sınar. Cache
testleri farklı giriş token'larıyla 304, değişen ETag, bozuk gzip, hatalı
304, kota ve depolama engelini kapsar.
`web_auth/run_transport_tests.py`, gerçek router ve handler'ları modem
sınırına kadar çalıştırır: ana adres küçük sayfayı sunmalı, oturumsuz
paket 401 vermeli, oturumlu paket ve doğrudan HTML baytları korunmalıdır.
304 yanıtı gövdesiz olmalı; içerik değişince tam paket gönderilmeli;
If-None-Match oturum denetimini atlatmamalıdır.
HTTP birim/senaryo testleri 91/91; iki web integration hedefi ve imzalı
ARM Release self-check geçmiştir. CubeIDE Debug/Release pre-build
adımları yeni HTML kaynağını da üretir; `loading_html.c` managed build'in
kaynak keşfine dahildir.

**Yerel tarayıcı denemesi:**
`python test/integration/web_navigation/serve_loading_preview.py`
komutu `http://127.0.0.1:8769/` adresini sunar. Yalnız bu yerel fixture
(örnek test sunucusu) için giriş `admin / preview` olur; gerçek cihaz
şifresi kullanılmamalıdır. `--chunk-delay`, her 1.024 baytlık parça
arasındaki gecikmedir. Fiziksel modem zamanlamasını temsil etmez.
Tarayıcıda yanlış giriş, ölçülen ilerleme ve ikinci kez giriş sormadan
uygulamanın açılması ayrıca gözlenmiştir.

**Sınır:** Büyük uygulama bu değişiklikte bölünmemiştir. İki HTML
gövdesinin toplamı 44.180 bayttır; giriş ve paket için ek HTTP istekleri
vardır. İlk ekran yaklaşık 2,7 KB'dır; büyük aktarım başarılı girişe
ertelenir. Fiziksel GSM aktarımı ve cihaz kabulü henüz yapılmamıştır.
Cihaza yükleme yapılmamıştır.

## RF silme ACK'i ve yeniden atama regresyonu — 16.10.2026

**Amaç:** Silme onayından sonra RF kimliğinin yeni keşif bildirimi
beklemeden yeniden atama listesine dönmesini doğrulamak.

**Kullanım yeri:** `test/` içinde aşağıdaki merkezi seçim çalıştırılır:

```text
ruby -S ceedling "test:pattern[(test_rf_error_retry_scenario|test_rf_discovery|test_rf_config_scenario|test_http_rf_group_routes|test_rf_config_parse)]"
```

Tek cihaz/toplu silme, ret yanıtı, ACK sonrası yeniden atama, kayıtlı
silmenin yeniden başlatma sonrası tekrarı, kimlik tekilleştirme ve RSSI
koruması üretim girişlerinden sınanır. Boş/geçersiz kimlik ve dolu liste
kontrolleri vardır; silme onayı ölçüm oluşturmamalıdır. Çalışma ağacında
156/156, yalnız commit'e seçilen bağımsız kopyada 144/144 geçti.
Bu sonuç fiziksel cihaz kabulü değildir. Davranış ve kalan sınırlar
üretim hazırlık raporunun aynı tarihli RF silme bölümündedir.


## RF ayar aktarımı hata açıklamaları — 16.10.2026

**Amaç:** Genel “Uygulama başarısız” sonucunun arkasındaki fideri, MH
nedenini ve sonraki kontrol adımını operatöre göstermek.

**Kullanım yeri:** RF Yapılandırma → RF ayar aktarımı. Mevcut
`/status/rf-group` JSON alanları kullanılır; RF protokolü, API alanları
ve otomatik tekrar davranışı değiştirilmez.

**Gösterim:** Başarısız işlemde ayar satırı ve fider, anlaşılır MH nedeni
ve yapılacak işlem teknik ayrıntılar açılmadan gösterilir. NOT_LIVE,
NO_INV, TIMEOUT, PARTIAL_COMMIT, MH yeniden başlaması ve CRC uyuşmazlığı
ayrı açıklanır. Bilinmeyen neden kodu saklanmaz; rapor yokken neden
uydurulmaz. UNCERTAIN sonucu ayar uygulanmamış gibi sunulmamalıdır.
Başlamamış gruba önceki grubun hata raporu bağlanmamalıdır. İptal düğmesi
yalnız mevcut `CanAbort` koşulu izin verdiğinde gösterilir. “Sonuç okundu”
mesajı aktarımın başarılı olduğu anlamına gelmez; alttaki işlem sonucu
ayrıca gösterilir.

**Kaynak ve sınır:** Güncel R2 belgesinin §4.9 tablosu ve
`rf_group.c` kaynak yolu esas alınır. FAILED bitmap'i başarısız üyeleri
kabul edilen WRITE sırasına göre belirtir; genel protokol faz sırası
varsayılmamalıdır. Mevcut JSON işlem üyesinin EUI snapshot'ını taşımaz.
Bu nedenle rapordan kesin cihaz/faz kimliği türetilmez; RF İzleme'de
ilgili fiderin üç fazının kontrolü istenir. Kullanıcının paylaştığı
metinde sayısal neden yoktur; o olayın fiziksel nedeni bu metinden
kesinleştirilemez.

**Doğrulama:** `web_navigation/test_navigation.js` gerçek kaynak ve
gömülü script'te neden 1–10, bilinmeyen kod, fider/satır, ana sonuçta
neden ve yapılacak işlem, belirsiz sonuç ve eski grubun gizlenmesini
Türkçe/İngilizce denetler. Yerel `serve_loading_preview.py --rf-preview`
yalnız örnek NOT_LIVE sonucu sunar; gerçek cihaza ulaşmaz. Cihazdaki RF
hatası giderildiği veya fiziksel neden bulunduğu iddia edilmez.


## DIP anahtarlarının web durum yanıtı — 16.10.2026

**Amaç:** SW1/SW2 konumlarını mevcut sistem durumundan web istemcisine
aktarmak.

**Veri yolu:** Mevcut `dip_switch_get()` debounce (kararlılık)
sonucunu kullanır; aynı kaynak `[DIP] SW1/2 -> ON/OFF` logunu besler.
`system_status_update()` zaten `dip_sw[2]` alanını doldurur.
`system_status_json_build()` mevcut `/status/board` yanıtına yalnız
`"DIP":[sw1,sw2]` dizisini ekler. ON=1, OFF=0 mantığı terslenmez.
NVRAM, GPIO, debounce veya IRQ eşzamanlama davranışı değiştirilmemiştir.
Eksik veya geçersiz web verisi OFF yerine bilinmiyor gösterilmelidir.
DIN ve röle durumları korunur; bütün göstergeler salt okunurdur.

**Doğrulama:** Merkezi `test_system_status` ve `test_http_` seçimi
100/100 geçti. Dört DIP kombinasyonu gerçek sistem durumu ve JSON
üretim yolundan geçirilir; DIN/RLY alanlarının korunması denetlenir.
`web_auth`, gerçek board HTTP handler yanıtında DIP ve GSM alanlarını
birlikte sınar. Fiziksel switch testi ve cihaz yüklemesi yapılmamıştır.


## Cihaz durumu kartları — 16.10.2026

**Amaç:** Mobil şebeke, güç kartı ve dijital durumların okunmasını
kolaylaştırmak; SW1/SW2 konumlarını konsolla aynı kaynaktan göstermek.

**Kullanım yeri:** Cihaz durumu → Cihazdan oku. Şebekede aktif teknoloji
ve kayıt durumu öne çıkar. Ana şebeke, dolaşım, arama ve kayıt reddi
ayrılır. Aktif teknolojinin sinyal ölçümleri ilk sıradadır; diğer
teknolojiler ve CSQ açılır ayrıntılarda korunur. Açıklama, sinyal
değerlerinin modemin son sorgusuna ait olduğunu belirtir. Mevcut sinyal
dönüşüm aralıkları ve değerlendirme eşikleri değiştirilmemiştir.

**Güç kartı:** Son okunan telemetri yaşı ve ölçüm geçerliliği ayrı
alanlardır. Yaşı 30 saniyeyi aşan kayıt eski, 255 veya eksik/kalitesiz
kayıt kullanılamaz gösterilir; bu eşik mevcut gösterim kuralıdır.
Alarmlar ayrı kutudadır; geçersiz alarm alanı “Alarm yok” sayılmaz.
İletişim/şarj ve ayar/güç ölçümü bitleri ayrı listelere açılır. Ham alan
karşılıkları teknik ayrıntıda korunur. Rezerve MH bağlantı değeri 3,
undefined yerine bilinmiyor gösterilir. Gösterim, cache nesnesinin ham
sentinel değerlerini değiştirmemelidir.

**DIP gösterimi:** Veri yolu yukarıdaki “DIP anahtarlarının web durum
yanıtı” bölümündedir. SW1/SW2 salt okunur ON/OFF/bilinmiyor göstergeleridir.

**Doğrulama:** Merkezi `test_system_status` ve `test_http_` seçimi
100/100 geçti. Dört DIP kombinasyonu gerçek sistem durumu ve JSON
üretim yolundan geçirilir; DIN/RLY alanlarının korunması denetlenir.
`web_auth`, gerçek board handler yanıtında DIP ve GSM alanlarını sınar.
`web_navigation`, kaynak/gömülü script'te Türkçe/İngilizce kayıt
sonuçlarını, aktif/pasif teknoloji görünürlüğünü, 0/30/31/255 yaş
sınırlarını, geçersiz güç verisini, rezerve durum bitini ve DIP
ON/OFF/bilinmiyor durumlarını denetler. İki integration hedefi ve ARM
Release imza self-check geçti. `serve_loading_preview.py --board-preview`
yalnız örnek verilerle görsel kontrol sağlar. Cihaza yükleme ve fiziksel
switch testi yapılmamıştır.

## 21.10.2026 gerçek web ve cihaz kabulü

Web v1.4.13 içeren imzalı üretim imajı (CRC AE80B796) XMODEM ile kuruldu.
Tam unit 1574/1574, diğer 13 entegrasyon paketi exit 0, gerçek IEC104
13/13 ve gerçek RS485 Modbus 29/29 geçti. `modbus_device/run_tests.py`
T10 artık ikinci rölenin ON/OFF echo/readback ve başlangıç durumuna
dönüşünü de kontrol eder. Güvenli yük kullanıcı tarafından teyit edildi.
Röle kontrolleri fiziksel kontak/gerilim ölçümü sayılmaz.

`web_navigation/test_industrial_audit.js` kaynak ve embedded sayfada
16 kontrol × 2 = 32 PASS verir. Ek kontrol, IEC104 0 periyodik gönderim
yardım metninin gerçek kapatma davranışıyla uyumudur. Cihaz kodunun
periyodik gönderim kararı değiştirilmedi.

Gerçek tarayıcı matrisi 245 PASS, 0 FAIL, maskeleme nedeniyle iki
sonuçlandırılamayan hassas alan kontrolüdür. AY EUI silme, reset
kalıcılığı ve discovery düğmesinden geri atama da sınandı. Üç gerçek AY
ile APPLIED kabulü açık kalır. Ayrıntılar ve kanıt sınırları
[cihaz kabul raporunda](reports/2026-10-21-web-device-acceptance.md).
