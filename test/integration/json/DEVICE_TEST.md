# JSON gerçek cihaz testinin çalıştırılması

## Amaç

Hostla aynı Unity kaynaklarını ve mevcut JSON benchmark adaptörlerini
gerçek işlemcide çalıştırmak. Bu işlem firmware yükler ve cihazı yeniden
başlatır; donanım test yetkisi bulunmalıdır.

## Güncel benchmark kapsamı

Modem entegrasyonundan sonra karşılaştırma adaptörleri güncel üretim
kodunu kullanır. Testteki eski `old/new` sembol adları tarihsel fixture
kimlikleridir; ilk taraf artık entegrasyon öncesi firmware değildir.
Eski DWT tablosuyla yeni firmware arasında fiziksel önce/sonra sonucu
çıkarılmamalıdır. Böyle bir ölçüm için iki sürüm ayrıca sabitlenmelidir.

## Kullanım yeri ve ön koşullar

[Donanım rehberi](../../../doc/HW_TEST_VE_DEBUG_REHBERI.md) izlenmelidir.
Port her oturumda kimlik/`help` ile doğrulanmalıdır. 23.10.2026 koşusunda
COM18/230400 kullanılmıştır. Ceedling `json:unit` en az bir kez çalışmış
olmalı; Unity kaynakları `test/build/ceedling/vendor/unity/src` altında
bulunmalıdır. Seri/XMODEM aracı yeniden yazılmamalıdır.

## Adımlar

1. Önce normal `make -C Release -j8 all` derlemesi alınmalıdır. Bu normal
   imaj test sonunda geri kurulacak kancasız imajdır.
2. Repo kökünde hazırlama aracı çalıştırılmalıdır:

```text
python test/integration/json/build_device.py --cc <arm-none-eabi-gcc-tam-yolu>
```

3. Araç mevcut Release artifact'lerini ve son imzalı paketi
   `build/hardware/clean/` altında korur. Her yeni oturumda önceki hardware
   klasörü arşivlenmelidir. Aynı oturumun başarısız build'ini tekrar denemek
   için `--reuse-clean` kullanılabilir; başka sürümün eski temiz imajını
   yanlışlıkla kullanmak için bu seçenek verilmemelidir.
4. `build/hardware/test.efw` mevcut `serial_io.py` ve `xmodem_send.py` ile,
   rehberdeki BSL yolundan yüklenmelidir. Test paketi imza self-check'i
   geçmeli, hedef Flash'a sığmalı ve doğru temiz imaj önceden korunmalıdır.
5. Açılış/onay sonrası `su admin`, `jsonhw unit`, `jsonhw bench` çalıştırılıp
   ham konsol kaydı saklanmalıdır. Unity hata sayısı sıfır olmalı; DWT
   yoksa veya parity hatası varsa ölçüm başarılı sayılmamalıdır.
6. Başarılı veya başarısız test sonrası korunmuş `clean/firmware.efw`
   yeniden kurulmalıdır. Onay durumu ve test komutunun bulunmadığı
   doğrulanmalıdır. Yerel Release ELF/bin/map/hex/list de temiz kopyalardan
   geri konmalı; hash eşitliği ve test sembollerinin yokluğu denetlenmelidir.

## Ölçüm sınırları

DWT erişimi yalnız test portunun `target/bsp` sınırındadır. Sayaç kontrol
değerleri geri yüklenir. IRQ kapatılmaz. Unity testlerinde watchdog proxy'si
vardır; benchmark normal fonksiyonları doğrudan çağırır. Mevcut stack
monitor kullanılır; shell/test/IRQ yükü izole kütüphane stack'i sayılmamalıdır.
Testler üretim kaynaklarını değiştirmez; geçici hook link sırasında eklenir.
Mevcut logging başlığındaki GNU variadic macro için yalnız harness build'inde
`-Wno-variadic-macros` kullanılır; JSON ve testler diğer sıkı uyarılarla derlenir.

## Sonuç kaydı

Test sonuçları görüşmede paylaşılmalıdır; ham test çıktıları yerel build
dizininde kalmalıdır. Host referans sayıları cihazda çalışmış gibi
sunulmamalıdır.
