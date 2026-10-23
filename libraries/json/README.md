# Bağımsız JSON çekirdeği

**Sürüm:** 0.5 — 23.10.2026

## Amaç

`json.c` ve `json.h`, heap kullanmadan ve bir platforma bağlanmadan JSON
okumalı ve yazmalıdır. Ana uygulamanın HTTP/JSON yolları bu çekirdeği
kullanır. Kütüphane modem iş kurallarını veya HTTP taşımasını içermez;
alan doğrulaması ve ayarların kalıcı kaydı uygulama katmanında kalır.

## Kullanım yeri

Tek tamponla veya parçalar halinde veri alan embedded uygulamalarda
kullanılmalıdır. Dosyalar C11 ile derlenmelidir. Farklı okuyucu ve yazıcı
örnekleri birbirinden bağımsızdır; aynı örneğin eşzamanlı kullanımını
çağıran taraf düzenlemelidir.

## Desteklenen işlemler

| Alan | Destek |
|---|---|
| JSON türleri | Object, array, string, number, Boolean, null; kökte herhangi biri |
| Metin | UTF-8, tüm JSON escape dizileri, `\uXXXX`, UTF-16 surrogate pair |
| Tam sayılar | İşaretli/işaretsiz 8, 16, 32 bit okuma; 32 bit yazma |
| Ondalıklı sayı | IEEE-754 binary32 `float`; subnormal ve negatif sıfır dahil |
| Sabit ondalık yazım | `json_write_float_fixed`, 0–9 ondalık basamak, exponent yok |
| Parçalı okuma | Token, UTF-8, kaçış ve sayının herhangi bir baytında bekleme |
| Parçalı yazma | Çıkış dolduğunda `JSON_NEED_OUTPUT`, sonra `resume` |
| String kaynak parçaları | `json_write_string_begin/chunk/end`; ikinci büyük tampon gerekmez |
| Boyut hesabı | Aynı writer ile yalnız sayım; üretilen JSON ile aynı uzunluk |
| Bilinmeyen alan | `json_skip_value`; iç içe değerler de doğrulanır |
| Hata | Tür ve mutlak bayt konumu; init yapılana kadar korunur |

`double`, 64 bit tam sayı dönüşümü, DOM, JSONPath ve otomatik struct
eşleme yoktur. Writer daha küçük tam sayı türlerini 32 bit fonksiyonlara
normal C dönüşümüyle kabul eder. `json_write_number`, geçerli bir sayı
metnini dönüştürmeden iletir; böylece okunan sayının yazımı korunabilir.

## Ortak kurallar

- Bağlamlar `init` ile başlatılmalıdır; alanları dışarıdan değiştirilmemelidir.
  `error`, `error_offset`, `consumed`, `used` ve `total` gözlem içindir.
- Tamponlar ve kaynak bellekleri bağlamla veya birbirleriyle çakışmamalıdır.
  Verilen işaretçi/uzunluk gerçekten erişilebilir belleği göstermelidir.
- Nesting (iç içe seviye) varsayılanı 32'dir. `JSON_MAX_DEPTH` değişirse
  bütün translation unit'ler aynı değerle derlenmelidir. Bağlam düzeni
  değiştiğinde kütüphane ve bütün çağıranlar birlikte yeniden derlenmelidir.
- Hatalar sessiz kırpma veya sayısal clamp ile gizlenmemelidir. Hata sonrası
  yeni belge için `init` çağrılmalıdır.
- Kütüphane JSON işlemlerinin tamamını uygulama ayarlarına uygulamaz.
  Ayarlar geçici adayda tutulmalı, belge `JSON_DONE` olduktan ve alan
  kuralları doğrulandıktan sonra etkin ayar değiştirilmelidir.

## Derleme seçenekleri ve span yardımcıları

`JSON_ENABLE_FLOAT` varsayılan olarak `1`'dir. Yalnız integer dönüşümü
gereken projelerde bütün translation unit'ler `-DJSON_ENABLE_FLOAT=0`
ile derlenmelidir. Bu durumda `json_token_float`, `json_write_float` ve
`json_write_float_fixed` bildirimleri ve uygulamaları kaldırılır. Writer
sayı tamponu 11 bayt olur; binary32 ve FENV bağımlılığı kaldırılır.
JSON sayı sözdizimi, exponent okuma ve `json_write_number` ile metni
dönüştürmeden aktarma desteği korunur. Float açıkken FENV kontrolü ve
mevcut yuvarlama garantisi korunur. Header C++ linkage desteği sağlar;
`json.c` yine C11 ile derlenmelidir.

`JSON_SPAN_INIT` static/global başlatıcılarda, `JSON_SPAN_LITERAL` ise
ifadelerde kullanılmalıdır. İkisi de yalnız string literal kabul eder;
pointer veya değişken dizi verilmesi derleme hatasıdır. Uzunluk NUL
sonlandırıcıyı içermez; literal içindeki gömülü NUL baytları korunur.

```c
static const json_span_t keys[] =
{
    JSON_SPAN_INIT("APN"),
    JSON_SPAN_INIT("enabled")
};

json_span_t expected = JSON_SPAN_LITERAL("internet");
bool matches = json_token_equals_span(&token, expected);
```

Uzunluğu bilinen değişken veri `{data, length}` ile verilmelidir.
`json_token_equals_span` yalnız key/string token'larını karşılaştırır;
NUL sonlandırıcı aramaz ve gömülü NUL baytlarını karşılaştırmaya katar.
`{NULL, 0}` boş span olarak geçerlidir; NULL ile sıfırdan büyük uzunluk
geçersizdir. `json_token_equals` NUL ile biten metinler için korunmuştur;
NULL metin verildiğinde false döner. NULL pointer'ı sessizce boş metne
çeviren bir C string yardımcısı eklenmemiştir.

Span veriyi sahiplenmez veya kopyalamaz. Writer kaynakları, yazma çağrısı
ve gerekiyorsa bütün `resume` çağrıları tamamlanana kadar geçerli ve
değişmeden kalmalıdır. Static/global bellek ömrü yeterli olsa da başka
bir işlem tarafından değiştirilmemelidir. Büyük çıkış tamponu bu kuralı
kaldırmaz. Reader token metni sonraki `read`/`skip` çağrısından önce
kullanılmalı veya kopyalanmalıdır.

## Okuma kuralları

1. Çağıran `json_reader_init` ile token tamponu vermelidir. Tamponun
   kapasitesi normal okunacak en uzun **çözülmüş** string/key veya sayı
   metnini karşılamalıdır. Atlanan değerler bu kapasiteyi kullanmaz.
   Token için sonlandırıcı bayt gerekmez; taşma `JSON_ERROR_BUFFER` verir.
2. `json_reader_feed` ile bir parça verilmelidir. Son parçada `final=true`
   kullanılmalıdır. Önceki parça tamamen tüketilmeden yenisi verilmemelidir.
3. `json_read`, `JSON_OK` döndüğünde bir token üretir. `JSON_NEED_INPUT`
   yeni parçayı bekler. `JSON_DONE`, tek kök ve son boşlukların son parçaya
   kadar doğrulandığını bildirir. Tek bir başarılı token belge kabulü değildir.
4. Input parçası `consumed == input.length` olana kadar yaşamalıdır.
   Token metni sonraki `read`/`skip` çağrısına kadar geçerlidir; gerekiyorsa
   önceden kopyalanmalıdır. Token'lar çağıranın tamponuna kopyalanır;
   okuyucu bu sürümde zero-copy (kopyasız okuma) yapmaz.
5. Token `text`, yalnız key/string/number için anlamlıdır; NUL ile bitmez.
   Gömülü sıfır uzunlukla korunur. `json_token_copy_string`, C string'e
   sessiz kırpmamak için gömülü sıfırı reddetmelidir.
6. Typed helper başarısız olduğunda hedef değer değişmez. Tam sayı
   helper'ları `1.0` ve `1e0` gibi metinleri tam sayıya çevirmemelidir.
7. Yinelenen key'ler sırasıyla görünür. Key'ler escape çözülmüş halde
   karşılaştırılmalıdır. Uygulama reddetme/ilk/son değer politikasını açıkça
   seçmelidir; genel key tablosu çekirdeğe eklenmemiştir.
8. Bilinmeyen alan, key token'ından sonra `json_skip_value` ile atlanmalıdır.
   `NEED_INPUT` sonrasında yine bu fonksiyon çağrılmalıdır. Atlanan değer
   de UTF-8, sözdizimi ve derinlik sınırlarına tabidir; token tamponuna
   kopyalanmaz. Böylece çok uzun bilinmeyen key/string/number değerleri
   küçük tamponla atlanabilir. Normal `json_read` kapasite sınırı korunur.
   Belgenin tamamını atlamak için `NULL, 0U` token tamponu yeterlidir.

Okuyucu BOM, comment, trailing comma, tek tırnak, NaN/Infinity,
gereksiz baştaki sıfır ve eşleşmemiş surrogate kabul etmez. Unicode
normalization (eşdeğer metinleri tek biçime getirme) yapmaz.

## Yazma kuralları

Writer sırasıyla object/array başlangıcı, key, değer ve kapanış çağrılarını
kabul etmelidir. Yanlış sıra `JSON_ERROR_STATE` verir. Son kontrol
`json_writer_finish` ile yapılmalıdır.

Uzun bir string kaynak parçalarıyla üretilecekse önce
`json_write_string_begin`, her parça için `json_write_string_chunk`,
sonunda `json_write_string_end` çağrılmalıdır. Her kaynak parçası tam UTF-8
karakterleri içermelidir; bir karakterin baytları iki kaynak parçasına
bölünmemelidir. Boş parçalar geçerlidir. Çıkış parçaları ise herhangi bir
baytta dolabilir; aynı `resume` sözleşmesi geçerlidir. Açık string varken
başka key/değer/container işlemi yapılamaz; kapatılmadan `finish` hata verir.

```c
json_writer_t writer;
char output[128];
json_span_t key = {"APN", 3U};
json_span_t value = {"internet", 8U};

/* Each call must be checked. This example's buffer is sufficient. */
json_status_t status = json_writer_init(&writer, output, sizeof(output));
if (status == JSON_OK)
{
    status = json_write_object_begin(&writer);
}
if (status == JSON_OK)
{
    status = json_write_key(&writer, key);
}
if (status == JSON_OK)
{
    status = json_write_string(&writer, value);
}
if (status == JSON_OK)
{
    status = json_write_object_end(&writer);
}
if (status == JSON_OK)
{
    status = json_writer_finish(&writer);
}
/* JSON_DONE: output[0..writer.used) is the complete document. */
```

Çıkış NUL ile bitmez; yalnız `used` kadar bayt alınmalıdır. Yer dolduğunda
üretilen parça alınmalı, `json_writer_set_output` ile yeni parça verilmeli
ve `json_writer_resume` çağrılmalıdır. Başlatan `json_write_*` çağrısı
tekrarlanmamalıdır. Kaynak span tamamlanana kadar yaşamalı ve sabit kalmalıdır.
Bu çağrı sırasının çalışan örneği `test/libs/test_json_writer.c` içindedir.

`json_writer_init_count` aynı çağrılarla gereken toplam uzunluğu hesaplar.
Sayım ve gerçek yazım arasında veri değişirse uzunluk eşitliği garanti
edilmez; HTTP snapshot (sabit veri görüntüsü) kararı entegrasyonda verilmelidir.

Writer çıktı yayınlandıktan sonra hata verirse önceki baytları geri alamaz.
Çağıran eksik/hatalı çıktıyı tam belge gibi kullanmamalıdır.

## Float kuralları

`float` yalnız binary32 platformda kullanılmalıdır; derleme bunu denetler.
NaN/Infinity yazılmamalıdır. Okumada sonsuza veya sıfıra yuvarlanan sıfır
olmayan değer `JSON_ERROR_RANGE` verir; hedef değişmez. Negatif sıfır korunur.

Okuma round-to-nearest, ties-to-even (en yakına, eşitlikte çift son bite)
kuralını uygulamalıdır. En çok 128 mantissa rakamı kabul edilir;
ondalık noktası/sayı işareti/exponent rakamları bu sayıya dahil değildir.
Sınır aşımı `JSON_ERROR_BUFFER` verir. Sözdizimi okuyucusu bu dönüşüm
sınırından bağımsızdır; daha uzun sayıyı kendi token tamponu kadar okuyabilir.

Yazım dokuz anlamlı basamağa yuvarlar, sondaki gereksiz sıfırları kaldırır
ve gerektiğinde exponent kullanır. Bu, `json_write_float` davranışıdır;
en kısa gösterim değildir. Sonuç geri okunduğunda aynı binary32 değeri verir.
Yazma ve kesin okuma yolu tamsayı hesabıyla yapılır. Kısa sayılarda
mantissa ≤2^24, exponent −10…+10 ve binary32 evaluation/nearest rounding
koşulları sağlanırsa C11 Clinger hızlı yolu kullanılır. `fegetround` yalnız
modu okur; diğer modlarda mevcut kesin yola dönülür. Sonucun yuvarlaması
moddan bağımsızdır; FPU exception flag'lerini koruma garantisi yoktur.
Libc `strtof`, formatter, locale, `errno` veya heap kullanılmaz.
Güncel host önce/sonra sonucu [Clinger raporundadır](../../test/reports/2026-10-23-json-clinger-host.md).

`json_write_float_fixed(writer, value, decimals)` tam olarak 0–9 ondalık
basamak üretir; sıfır basamakta nokta yoktur. Sondaki sıfırlar ve negatif
sıfır korunur. Aynı nearest/ties-to-even kuralı kullanılır. Örnek:
`1.25F, 1U` → `1.2`; `1.75F, 1U` → `1.8`; `-0.0F, 3U` → `-0.000`.
10 veya daha fazla basamak argument hatasıdır; NaN/Infinity range hatasıdır.
En büyük sonlu float dahil bütün aralık desteklenir; çıktı en fazla 50 B'dir.
Bu API'nin amacı sabit gösterimdir; yuvarlama sonrası float round-trip
garantisi verilmez. Mevcut `xprintf` farklı bir yuvarlama kullanır; endpoint
geçişinde özellikle eşik değerlerin çıktıları ayrıca değerlendirilmelidir.

## Parçalı kullanım örnekleri

Writer için çalışan örnek
`test/libs/test_json_writer.c` içindeki
`test_json_writer_fixed_float_resumes_at_every_output_size` testidir.
Başlatan çağrı yalnız bir kez yapılmalıdır. Örnek akış:

```text
init(writer, segment, segment_size)
status = json_write_float_fixed(writer, -123.125F, 9U)
while status == JSON_NEED_OUTPUT:
    segment[0..writer.used) teslim edilir
    json_writer_set_output(writer, segment, segment_size)
    status = json_writer_resume(writer)
status == JSON_OK ise son segment[0..writer.used) teslim edilir
json_writer_finish(writer) == JSON_DONE doğrulanır
```

Bu akış şeması C kodu değildir; her çağrının hatası ve transport teslim
sonucu uygulama tarafından kontrol edilmelidir. `segment` transport
tarafından hâlâ kullanılırken yeniden verilmemelidir. Transport bekliyorsa
uygulama mevcut `status` ve writer bağlamıyla daha sonra devam etmelidir.
Boyut hesabında aynı çağrı `json_writer_init_count` sonrası yapılmalıdır;
hesaplanan `total`, aynı değer/precision ile gerçek yazımın uzunluğudur.

Reader için çalışan örnek
`test/libs/test_json_reader.c` içindeki
`test_json_reader_skip_large_value_needs_no_token_buffer` testidir.
Her parçada `json_reader_feed` sonrası `json_skip_value` çağrılmalıdır.
`JSON_NEED_INPUT` gelirse yeni parça verilip aynı skip çağrısıyla devam
edilmelidir. `JSON_OK` yalnız atlanan değerin bittiğini bildirir; belgenin
kalanı `json_read` ile `JSON_DONE` olana kadar doğrulanmalıdır.

## Test ve kabul

`test/` dizininde:

```text
ceedling json:check
ceedling json:unit
ceedling json:coverage
ceedling json:arm
ceedling json:compare
```

`json:check`, Unity testleriyle Python JSON/host CRT karşılaştırmalarını
birlikte çalıştırır. Sonuncu hedef için `ARM_CC`, mevcut hedef derleyicinin
tam yolu olarak verilmelidir. `PYTHON`, `CC` ve `JSON_TEST_COUNT` test
ortamında değiştirilebilir; rastgele test seed'i sabittir.
Reference paketi, reader'a uğramadan float typed helper'a verilen bozuk
sayı metinlerini de bağımsız grammar ile sınar; hata hedefi değiştirmemelidir.

`json:compare`, mevcut üretim parser/writer fonksiyonlarıyla test
adaptörlerini aynı girdilerde ölçer. Host süreleri C batch (toplu çağrı)
içinde alınır. `ARM_CC` verilirse aynı hedef derleyici ve linker GC ile
boyut profilleri de çıkarılır. Bu hedef `json:check` içine dahildir.
Sonuçlar [karşılaştırma raporunda](../../test/reports/2026-10-23-json-comparison.md)
tutulmalıdır; host hız sonucu hedef cihaz hızı sayılmamalıdır.

Güncel kanıt ve kabul sınırları
[test raporunda](../../test/reports/2026-10-23-json-core.md) tutulmalıdır.
Gerçek MCU'da 84 Unity testi, DWT süreleri ve stack high-water sonucu
[cihaz raporundadır](../../test/reports/2026-10-23-json-device.md).
Bu eski fiziksel sonuçlar v0.2'ye aittir. Güncel modem entegrasyonu ve
doğrulama sınırları [geçiş raporundadır](../../test/reports/2026-10-23-json-modem-integration.md).

Writer, escape gerektirmeyen string bölümlerini ve raw sayı/literal
metinlerini mevcut çıkış kapasitesi kadar toplu kopyalar. Geçerli integer
dönüşümü tek tarama yapar; hatalı girişte hata önceliği korunur.
Integer taşma eşiği döngü öncesinde hesaplanır. Float typed helper geçerli
metinde syntax ve dönüşüm hazırlığını tek taramada yapar; mantissa sınırı
aşıldığında syntax önceliği için hata yolunda tekrar doğrulama yapılır.
Reader düz ASCII string bölümlerini input/token kapasitesi kadar toplu
kopyalar. Writer ASCII için hızlı doğrulama yolu kullanır; UTF-8,
Unicode escape ve hatalı çok baytlı karakter denetimleri korunur.
Varsayılan ARM bağlamı reader için 104 B, writer için 156 B'dir.
Float kapalı writer 120 B'dir. Çekirdeğin global data/BSS alanı yoktur.
Önceki özellik kabulü [v0.2 raporundadır](../../test/reports/2026-10-23-json-stream-fixed.md).
Aynı süreçte önce/sonra ölçmek için
`JSON_BASELINE_SOURCE`, aynı public API'ye sahip önceki `json.c` dosyasının
tam yolu olarak verilmelidir. Güncel hız/kod boyutu karşılığı
[tekrar ölçüm raporunda](../../test/reports/2026-10-23-json-simplification.md)
açıklanmıştır.
