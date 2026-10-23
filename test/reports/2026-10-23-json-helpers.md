# JSON span yardımcıları ve derleme seçenekleri

**Tarih:** 23.10.2026

## Amaç

Son iki öneri incelemesinde uygun bulunan küçük yardımcılar eklenmiş,
bağlamların padding (hizalama boşluğu) maliyeti azaltılmış ve float
desteği isteğe bağlı yapılmıştır. Bağımsız çekirdek v0.4 olmuştur.

## Kanıt ve değişiklikler

- Önceki `json_token_equals` yalnız C string kabul ediyor ve `strlen`
  çalıştırıyordu. Yeni `json_token_equals_span`, uzunluğu belli metni ve
  gömülü NUL baytlarını karşılaştırır. Önceki fonksiyon yeni karşılaştırmayı
  kullanır; NULL ve geçersiz token türlerinde metne erişmeden false döner.
- `JSON_SPAN_LITERAL` ifadelerde, `JSON_SPAN_INIT` static/global
  başlatıcılarda kullanılır. İkisi de pointer/dizi değişkenlerini reddeder.
- `JSON_ENABLE_FLOAT=0`, üç float API'sini, binary32 varsayımlarını ve
  FENV bağımlılığını kaldırır. JSON sayı grammar'ı ve sayı metnini
  dönüştürmeden aktarma desteği korunmuştur. Varsayılan değer 1'dir.
- Bağlam alanları sıralanmıştır. Enum tipleri, `size_t` sayaçlar ve
  mevcut derinlik desteği korunmuştur; packed struct kullanılmamıştır.
- Header `extern "C"` koruması sağlar. Uygulama C11 olarak kalmıştır;
  C++20 yalnız istemci uyumluluk testinde kullanılmıştır.

## Ölçüm

STM32 GCC 14.3.rel1 / Cortex-M33 hard-float ile statik ölçüm:

| Ölçüt | Önceki v0.3 | v0.4 float açık | v0.4 float kapalı |
|---|---:|---:|---:|
| Reader bağlamı | 108 B | 104 B | 104 B |
| Writer bağlamı | 164 B | 156 B | 116 B |
| `-Os` nesne text/rodata | 6.886 B | 7.008 B | 4.678 B |
| `-O2` nesne text/rodata | 11.061 B | 11.229 B | 6.397 B |
| En büyük tek fonksiyon frame, `-Os` | 480 B | 480 B | 40 B |
| En büyük tek fonksiyon frame, `-O2` | 512 B | 512 B | 40 B |

Data/BSS sıfırdır. Yeni karşılaştırma API'si ve alan düzeni, float açık
nesnede `-Os` için toplam 122 B artışla sonuçlanmıştır. Bunlar modül nesne
boyutlarıdır; son firmware Flash farkı değildir. Linker kullanılmayan
fonksiyonları zaten kaldırır. Frame ölçümü bütün çağrı zincirinin stack
tepesi veya cihazda fiziksel ölçüm değildir.

Önceki değerlerin kaynağı [v0.3 host raporudur](2026-10-23-json-clinger-host.md).
Güncel derleyici, kaynak hash'leri ve ölçümler
[ölçüm dosyasındadır](2026-10-23-json-helpers-metrics.json).

## Doğrulama

- 89 ayrı Unity testi geçti. Son `json:unit json:coverage` koşusu aynı
  testleri iki derleme altında çalıştırdığı için toplam 178 geçiş verdi.
- Coverage: 1.055/1.084 satır (%97,3), 910/965 branch (%94,3),
  86/86 fonksiyon (%100).
- C11/C++20 istemcileri float açık/kapalı dört yapılandırmada geçti.
  Static literal tabloları, integer sınırları, tek bayt output ile resume,
  count, sayı metni aktarma ve hatalı `01` reddi sınandı. Yedi geçersiz
  istemci/derleme seçeneği beklenen şekilde derleme sırasında reddedildi.
- 2.005 belge; 100.000 byte mutation x üç parçalanma; 100.000 float deseni
  (99.606 finite); 100.000 sayı metni mutation; 100.000 Clinger girdisi;
  996.370 fixed float; 7.650 tam yuvarlama eşiği karşılaştırması geçti.
- 1.112.064 Unicode scalar x iki kodlama, 24.576 iki baytlı form ve
  2.048 eşleşmemiş surrogate kontrolü geçti.
- Üretim adaptörleriyle tam/kısmi/ters sıralı ayarlar, aralık hataları,
  1.002 status görüntüsü ve kapasite sınırları eşdeğer sonuç verdi.
- ARM float açık/kapalı iki optimization seviyesinde derlendi; boyut,
  stack ve undefined symbol kapıları geçti. Kapalı sürümde float API
  sembolleri ve `fegetround` bulunmadığı doğrulandı.

Testler `test/libs/test_json_reader.c`, `integration/json/configuration.c`,
`run_checks.py` ve `run_target_checks.py` içinde kalıcıdır.
`ceedling json:check`, `json:coverage` ve `json:arm` mevcut girişlerdir.
Yeni istemci matrisi için host GCC ile birlikte G++ da bulunmalıdır.

## Sınırlar ve alınmayan öneriler

NULL pointer'ı sessizce boş metne çeviren `json_span_cstr`, 6–9 basamak
deneyerek shortest iddiasında bulunan writer ve nearest varsayımıyla
FENV kontrolünü kaldırma eklenmemiştir. Mevcut float yazımı ve doğruluk
garantisi korunmuştur.

Yalnız `sizeof` denetleyen init eklenmemiştir. Aynı derleyicide depth
31/32 reader boyutu aynı, depth 32/33 writer boyutu aynı olabildiğinden
bu kontrol tek başına yapılandırma doğrulaması değildir. Ayrı binary
kütüphane dağıtımı için yapılandırma/ABI kontrolü açık kalmıştır.
Mevcut kaynak derlemesinde bütün translation unit'ler aynı depth/float
ayarını kullanmalı ve bağlam düzeni değişince birlikte derlenmelidir.

Ana uygulamaya entegrasyon, firmware yükleme ve yeni cihaz/DWT testi
yapılmamıştır. Önceki fiziksel sonuçlar yeni sürüme genellenmemelidir.
