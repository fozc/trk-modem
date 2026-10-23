# JSON kopyasız atlama ve sabit ondalık yazım

**Tarih:** 23.10.2026

Bu kayıt bağımsız v0.2 kabul aşamasıdır. Ardından yapılan gerçek MCU
testleri ve DWT/stack ölçümü [cihaz raporunda](2026-10-23-json-device.md)
ayrı kaydedilmiştir.

## Amaç

Küçük token tamponuyla büyük bilinmeyen alanların atlanması ve RF gibi
alanlarda gereken sabit ondalık gösterim, bağımsız kütüphaneye eklenmiştir.
Ana uygulama veya xscanf değiştirilmemiştir.

## Kullanım yeri ve davranış

`json_skip_value`, mevcut `skipping` alanını kullanır. Append ve ASCII
kopyalama yolları bu durumda token tamponuna yazmaz; yeni durum veya
tampon eklenmemiştir. İç içe key/string/number içerikleri kapasiteden
bağımsız atlanır. UTF-8, escape, grammar ve derinlik kontrolleri korunur.
Normal `json_read` kapasite sınırı değişmemiştir. Atlanan değerin bitmesi
tüm belgenin doğrulanması değildir; sonda `JSON_DONE` aranmalıdır.

`json_write_float_fixed(writer, value, decimals)` 0–9 ondalık basamak
yazar. Mevcut exact binary32 digit üreticisi paylaşılır; formatter, heap,
double veya yeni dış bağımlılık eklenmemiştir. Negatif sıfır korunur.
Yuvarlama nearest/ties-to-even'dir. Örnek: 1.25, bir basamakta `1.2`;
1.75, bir basamakta `1.8` olur. Mevcut `xprintf` yarım ekleyerek yuvarlar;
özellikle tie ve negatif sıfır için byte eşitliği varsayılmamalıdır.
Endpoint davranışı bu fazda değiştirilmemiştir.

## Doğrulama

- 84 ayrı Unity testi normal ve coverage derlemesinde geçti.
- Yeni native testler: 2.000 B string'in sıfır token tamponuyla parçalı
  atlanması, tamponun değiştirilmemesi, normal alan okumaya dönüş,
  bozuk UTF-8/escape/syntax retleri, tam nesting sınırı ve bir fazlası.
- Sabit yazım: 0/9 basamak, ties-to-even, carry, negatif sıfır, en küçük
  subnormal ve FLT_MAX, NaN/Infinity/argument/state retleri, sayım eşitliği,
  her output segment boyutunda devam ve çıktı sınırı canary'si.
- 996.370 fixed float örneği Python'un binary32 değerini tam temsil eden
  binary64 değerin fixed formatıyla byte düzeyinde eşleşti. Her sonlu
  desen için 0–9 basamağın tamamı karşılaştırıldı; seed sabittir.
- 100.000 JSON mutation × üç parçalanmada sıfır tamponlu skip, normal
  okuyucuyla aynı kabul/ret, hata türü ve mutlak hata konumunu verdi.
- Önceki 2.005 belge, 100.000 binary32 çekilişi, 100.000 doğrudan float
  syntax mutation, 7.650 yuvarlama eşiği ve tüm Unicode scalar testleri geçti.
- 1.002 durum görüntüsü/kapasite eşitliği testi mevcut üretim writer'ıyla geçti.
- Coverage: 1.008/1.037 satır (%97,2), 863/919 branch (%93,9),
  83/83 fonksiyon (%100). GCC `-fanalyzer` ve sıkı uyarılar temizdir.

## Kaynak ölçümü

STM32 GCC 14.3.rel1, Cortex-M33 hard-float:

| Ölçüm | Önce | Sonra |
|---|---:|---:|
| Object text/read-only, `-Os` | 6.128 B | 6.502 B |
| Object text/read-only, `-O2` | 9.685 B | 10.421 B |
| Reader bağlamı | 108 B | 108 B |
| Writer bağlamı | 128 B | 164 B |
| Global data/bss | 0 B | 0 B |
| En büyük tek stack frame, `-Os`/`-O2` | 456/488 B | 456/488 B |

Writer sayı tamponu 16 → 50 B olmuştur; tam float aralığının 39 integer
rakamı, işaret, nokta ve dokuz kesir rakamı için gereklidir. Yalnız dokuz
anlamlı basamaklı API kullanılsa da bu bağlam kapasitesi ayrılır.
Caller-owned token ve transport tamponları ayrıca hesaba katılmalıdır.

Önceki benchmark'ın seçili kökleri fixed API'yi çağırmadığından birleşik
link profilindeki 14.751 B fixed API'nin tam link maliyeti değildir.
Bu kayıt bütün firmware'in RAM/Flash veya toplam stack kabulü değildir.
Undefined symbol listesi yalnız mevcut bellek/string libc yardımcılarıdır.

## Tekrar çalıştırma ve sınırlar

`test/` dizininde `ARM_CC` proje derleyicisinin tam yolu olmalıdır:

```text
ceedling json:check json:coverage json:arm
```

Yeni referanslar kalıcı `run_checks.py`/`json_bridge.c` içinde ve merkezi
Ceedling girişine bağlıdır. Çalışan parçalı örnekler ve bellek ömürleri
[kütüphane rehberindedir](../../libraries/json/README.md).

Bu turda fiziksel cihaz, DWT, transport teslimi ve endpoint transaction
testi yapılmamıştır. HTTP entegrasyonu ve gerçek cihaz kabulü ayrı kalır.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | Kopyasız skip, fixed float API, kalıcı referans/native testler, ARM kaynak ölçümü |
