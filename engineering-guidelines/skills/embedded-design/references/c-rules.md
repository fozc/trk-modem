# C Kuralları — Proje Uyarlaması

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## C00. Amaç ve kapsam

C kaynak/header geliştirme. Bu belge BARR-C veya MISRA'nın tam metni
ya da tam uygunluk iddiası değildir. Proje stil seçimleri architecture
A10'da tutulur; burada hata önleyen yöntemler tanımlanır.

## C01. Tür ve aritmetik

[ZORUNLU] Integer veri, sayaç, kimlik, protokol alanı ve saklanan
değerde stdint.h sabit genişlikli türleri; boyut, uzunluk ve indekste
size_t; pointer farkında ptrdiff_t; Boolean durumda bool; adlandırılmış
durum kodlarında enum kullanılmalıdır. Metinde char ve gerektiğinde
float/double korunmalıdır. Uygulama verisi için plain int/short/long
eklenmemelidir. Standart giriş noktası, HAL ve harici ABI'nin zorunlu
türleri korunmalı; sınırda dönüşüm aralık kanıtından sonra yapılmalıdır.

[ZORUNLU] Daraltma öncesinde aralık kontrol edilmeli; açık cast güvenlik
kanıtı sayılmamalıdır. İşaretli taşma ve geçersiz shift engellenmelidir.
UINT8_C/UINT16_C kullanımı integer promotion etkisini ortadan kaldırmaz.
Toplu tür değişimi veya yalnızca uyarı susturmak için cast
eklenmemelidir. Format argümanı, gerçek formatter'ın desteklediği
format ve beklediği türle eşleşmelidir.
Shift sayısı, yükseltilmiş sol operandın genişliği ve işaretiyle birlikte
incelenmelidir.

[ZORUNLU] Floating-point gerekiyorsa float/double veya projede kaynağı
belirtilmiş typedef kullanılmalıdır. float32_t/float64_t/float128_t
C99 standart türleri olarak sunulmamalıdır. DBL_DIG gibi özellikler
float.h üzerinden kontrol edilmelidir. Finite/range kontrolü ilgili
algoritmanın geçerlilik sınırına yerleştirilmelidir.

## C02. Header ve akış

[ZORUNLU] Header kendi kullandığı türleri tanımlayan header'ları
içermeli; tek başına derlenebilmelidir. Public header private header'a
bağımlı olmamalıdır. Dış arayüzü olmayan kaynak dosya için boş bir
header zorunlu tutulmamalıdır.

[ZORUNLU] Tek satırlık kontrol gövdelerinde de süslü parantez
kullanılmalıdır. Koşul içinde atama yapılmamalıdır. Switch default
içermeli; if/else-if zinciri else ile bitmelidir. Bilinçli fall-through
açıkça belirtilmeli ve seçilen analiz kurallarıyla uyumlu olmalıdır.

[ZORUNLU] Her nesne ilk okunmasından önce geçerli durumda
başlatılmalıdır. Sıfır her nesne için geçerli başlangıç kabul
edilmemelidir. Hata çıkışında çıktı parametrelerinin geçerliliği
tanımlanmalıdır.

[ZORUNLU] Projenin koruma ve author (yazar) kuralları A09/A10'dan
alınmalıdır. Kullanıcının koruduğu #if 0 blokları temizlik sırasında
kaldırılmamalıdır. Bu koruma tüm kullanılmayan kodu yeniden yazma veya
etkinleştirme izni sayılmamalıdır.

## C03. Yerleşim ve veri erişimi

[ZORUNLU] Wire/kalıcı veri için D06 uygulanmalıdır. Packed struct,
byte sırası, nesne ömrü, aliasing ve hizasız erişim sorunlarını
kendiliğinden çözmüş sayılmamalıdır. MMIO yerleşimi üretici
header'larından alınmalıdır.

[ZORUNLU] sizeof/offsetof yalnızca kanıtladığı boyut/ofset için
kullanılmalıdır; bit-field sırasını kanıtladığı söylenmemelidir.
Bit-field wire biçimi yerine açık mask/shift kullanılmalıdır.

[ZORUNLU] C11 _Alignof tür-adı ile kullanılmalıdır. Nesneye verilen
_Alignas, temel türün hizalamasını artırmış kabul edilmemelidir.
Hedefte nesne hizalaması linker/map ve ilgili ABI ile doğrulanmalıdır.

## C04. Sınırlar ve log

[ZORUNLU] Buffer pointer ile kapasite/uzunluk sözleşmesi birlikte
taşınmalıdır. Kopyalama, string sonlandırma ve overlap şartları
kontrol edilmelidir. Null pointer sıfır uzunlukta bile ilgili API
izin vermedikçe kütüphane fonksiyonuna geçirilmemelidir.

[ZORUNLU] API dönüşleri kontrol edilmeli; kasıtlı yok sayma gerekiyorsa
gerekçesi belirlenmelidir. Assert yalnızca iç invariant için kullanılmalı;
üretim assert/fault davranışı proje A07'den alınmalıdır.

## C05. Kaynaklar ve değişiklik

[C11 komite taslağı](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf):
6.5 erişim/aritmetik, 6.5.3.4 alignment, 7.7 float.h, 7.17 atomikler.
Bu kaynak dil semantiğidir; proje stil tercihleri ayrı tutulur.

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Tür, hizalama, başlatma ve veri biçimi düzeltmeleri |
| 2026-10-04 | 1.1.0 | C01: tür/format sınırları; C02: proje author ve kapalı kod kararları |
