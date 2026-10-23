# JSON gerçek cihaz kabulü ve DWT ölçümü

**Tarih:** 23.10.2026

## Amaç

Bağımsız JSON kütüphanesi gerçek STM32U375 üzerinde doğrulanmış; mevcut
JSON yardımcılarıyla cycle (çevrim) ve stack karşılaştırması yapılmıştır.
HTTP endpoint'leri yeni kütüphaneye geçirilmemiştir.

## Kullanım yeri ve kurulum

RTU konsolu USB kimliği ve `help` çıktısıyla COM18/230400 8N1 olarak
doğrulandı. İlk erişim başka uygulama portu tuttuğu için reddedildi;
kullanıcının yeniden deneme isteğinden sonra bağlantı açıldı.

Önce normal Release derlendi. Kancasız ELF/bin/map/hex/list ve imzalı
paket `test/integration/json/build/hardware/clean/` altında korundu.
Temiz raw imaj 360.904 B, CRC `133B3EE8`'dir.

Geçici test imajı ek object'ler ve linker'ın `--wrap=shell_init` seçeneğiyle
oluşturuldu. Üretim kaynaklarına başlangıç çağrısı/test komutu eklenmedi;
CubeIDE/linker script/NVRAM düzeni değiştirilmedi. Yalnız bu imajda root
seviyesinde `jsonhw unit|bench` komutu bulunur. Raw test imajı 407.876 B,
CRC `E77D1268`'dir. İki paket de mevcut EFW aracıyla imzalanıp şifrelendi;
paket self-check geçti. Yükleme mevcut XMODEM/BSL yoluyla yapıldı.

## Test sonucu

**84 Tests, 0 Failures, 0 Ignored.** Hostta kullanılan üç Unity dosyası
aynı fonksiyonlarla ARM'e derlendi. Float round-trip, Unicode, hatalı
giriş, kapasite, SIZE_MAX, parçalı okuma/yazma, kopyasız skip ve fixed
float testleri gerçek işlemcide geçti.

Testler sırasında watchdog mevcut `bsp_kick_wdt` ile beslendi. Uzun
Unity float döngülerindeki çağrılar test-only bir proxy üzerinden 256
çağrıda bir watchdog besler; benchmark doğrudan JSON fonksiyonunu çağırır.
Unity çıktısı aktif shell kanalına yönlendirildi.

Hosttaki 100.000 rastgele referans paketi ve bütün Unicode scalar taraması
cihazda tekrar edilmemiştir; cihaz kabulü 84 Unity testidir. Bu ayrım
korunmalıdır.

## DWT ölçüm yöntemi

Runtime `SystemCoreClock` 96.000.000 Hz olarak kaydedildi. Yerel CMSIS
CM33 DCB/DWT tanımları kullanıldı; register adresi tahmin edilmedi. DWT
sayacının ilerlediği doğrulandı; önceki kontrol değerleri sonunda geri kondu.
Donanım erişimi test harness'in BSP sınırındadır.

Her iş için 128 çağrı × dokuz tur ölçüldü. Tabloda çağrı başına cycle
ortancası ve 96 MHz karşılığı verilmiştir. Kesmelere dokunulmadı; minimum
ve maksimumlar JSON kanıtında bulunur. Ölçüm dispatch, adapter, checksum
ve input/output erişimini içerir. Shell'e sonuç basma ölçüm dışındadır.

Mevcut parser/serializer gövdeleri güncel üretim kaynaklarından alınmıştır.
Gerçek getter yerine iki tarafa aynı sabit snapshot verilir. Her fixture
için eski/yeni çıktı byte eşitliği ölçüm öncesinde doğrulandı. Ayar/NVRAM
yazılmaz. UART/GSM/HTTP aktarım süresi bu ölçüme dahil değildir.

## Süre sonucu

| İş | Mevcut cycle | Yeni cycle | Mevcut µs | Yeni µs |
|---|---:|---:|---:|---:|
| uint32 okuma | 391 | 310 | 4,073 | 3,229 |
| Float okuma, `-123.125` | 254 | 41.102 | 2,646 | 428,146 |
| Modbus POST | 2.737 | 7.323 | 28,510 | 76,281 |
| Modbus + bilinmeyen değer | 6.343 | 16.921 | 66,073 | 176,260 |
| Cihaz ayarı POST | 33.617 | 48.507 | 350,177 | 505,281 |
| Sistem durumu GET | 66.412 | 57.975 | 691,792 | 603,906 |
| Float yazma, aynı çıktı | 13.390 | 6.492 | 139,479 | 67,625 |

Yeni uint32 okuma 1,26 kat, durum yazımı 1,15 kat, float yazımı 2,06 kat
hızlıdır. Yeni float okuma bu örnekte yaklaşık 162 kat daha uzun sürer.
Eski helper daha dar kapsamlıdır; exact rounding ve exponent desteğiyle
eşdeğer değildir. Buna rağmen maliyet gerçektir ve geçişte hesaba katılmalıdır.
Yeni Modbus yolu yaklaşık 2,68 kat, cihaz ayarı yolu 1,44 kat süre harcar.

## Stack sonucu

Mevcut `stack_monitor_reset/get_stats` kullanıldı. Her benchmark öncesi
watermark yenilendi; sonuç basılmadan önce ölçüm alındı. Bu değerler
shell çağrı zincirinin stack tepesidir; JSON fonksiyonuna ait izole frame
veya yalnız iki sonuç arasındaki kütüphane stack farkı sayılmamalıdır.
Kesme yükü ve test altyapısı dahildir; gözlenmeyen en kötü durum garantisi yoktur.

| İş | Mevcut tepe B | Yeni tepe B |
|---|---:|---:|
| uint32 okuma | 944 | 960 |
| Float okuma | 920 | 1.448 |
| Modbus POST | 968 | 1.424 |
| Modbus + bilinmeyen değer | 1.016 | 1.456 |
| Cihaz ayarı POST | 1.216 | 1.656 |
| Sistem durumu GET | 1.792 | 1.576 |
| Float yazma | 1.136 | 1.504 |

84 testin toplam koşusunda tepe **2.912 / 4.096 B**, gözlenen boş pay
1.184 B'dir. Fixture/output tamponları test imajında statiktir; bu düzen
gerçek endpoint tampon yerleşiminin doğrulaması değildir.

## Temizleme ve son durum

Testten sonra korunmuş temiz paket XMODEM ile yeniden kuruldu. Boot logunda
test kurulumu seq 1048, temiz kurulum seq 1049 `FW_INSTALL_OK` olarak görüldü.
Temiz açılışta `Firmware approved successfully!` ve `boot status` için
`Firmware approved: YES` gözlendi. Yeni tarihli bir `FW_APPROVED` boot-log
satırı bu kayıtta kanıt olarak kullanılmamıştır; eski satırlar tarihsel kayıttır.

`jsonhw bench` temiz imajda `Command not found` döndürdü. Başlangıçta ve
sonda iki röle de OFF idi. Yerel Release dosyaları korunmuş temiz dosyalarla
SHA-256 düzeyinde aynı hale getirildi; ELF'de geçici test sembolü bulunmadı.

## Kanıt ve tekrar kullanım

- [Cycle/stack ölçümleri ve hash'ler](2026-10-23-json-device-metrics.json)
- [Ayıklanmış cihaz konsol kanıtı](2026-10-23-json-device-evidence.log)
- Ham seri kayıtlar ve iki imaj yerel `test/integration/json/build/hardware/`
  altındadır; build dizini Git dışında kalır.
- Kalıcı hazırlama aracı `test/integration/json/build_device.py` ve
  test-only driver/BSP `target/` altındadır. Çalıştırma adımları
  [donanım test açıklamasındadır](../integration/json/DEVICE_TEST.md).

## Karar ve sınırlar

Kütüphane cihaz testinden geçmiştir. Okuma yolundaki hız ve daha önce
ölçülen Flash artışı nedeniyle toplu geçiş önerilmemektedir. İleride
geçiş yapılırsa tek endpoint, mevcut ayar transaction'ı ve gerçek transport
testleriyle başlanmalıdır. Bu koşu röle kontak ölçümü, RF/IEC104/Modbus
protokol kabulü veya web endpoint entegrasyonu değildir.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | Gerçek MCU'da 84 Unity testi, DWT/stack kıyası, temiz imajın geri kurulması |
