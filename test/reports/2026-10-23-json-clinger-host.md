# C11 Clinger hızlı yolu: host kabulü

**Tarih:** 23.10.2026

## Amaç

Kısa ondalık sayılar mevcut binary search'e girmeden doğru binary32
değerine dönüştürülür. Çalışma tamamen C11'dir; C++ köprüsü yoktur.
Bu aşamada cihaza yükleme veya yeni DWT ölçümü yapılmamıştır.

## Kullanım yeri ve sınırlar

Hızlı yol, tam olarak temsil edilen `uint32_t` mantissa ≤2^24 ve
ondalık exponent −10…+10 için kullanılır. 10^0…10^10 değerleri binary32'de
tam temsil edilir; mantissa da tamdır. Bir binary32 çarpma/bölme ve
nearest/ties-to-even yuvarlaması yeterlidir. Bu aralıkta sonuç normal
aralıktadır; overflow/underflow yoktur. Negatif sıfır korunur.

Mevcut `number_next` ve `parse_exponent` grammar kuralları paylaşılır.
Sözdizimi, 128 mantissa rakamı sınırı ve hata hedefini koruma davranışı
değişmemiştir. Hızlı yol kabul etmezse `parse_float_exact` eski kesin
dönüşümü yapar. Bazı uzun girdilerde bu deneme ek tarama maliyeti getirir.

`FLT_EVAL_METHOD != 0` veya `fegetround() != FE_TONEAREST` durumunda
hızlı yol kullanılmaz. Yuvarlama modu değiştirilmez; dört modda sonuç
aynı nearest/ties-to-even bit desenidir. Normal FPU aritmetiği exception
status flag'lerini etkileyebilir; bu flag'lerin korunması API garantisi değildir.

Yöntem, LLVM'nin kullandığı Clinger yaklaşımıyla aynı prensibe dayanır;
LLVM'nin C++ uygulaması taşınmamıştır. Algoritmanın örnek uygulaması
[LLVM kaynağındadır](https://github.com/llvm/llvm-project/blob/main/libc/src/__support/str_to_float.h).

## Ölçüm yöntemi ve sonuç

MinGW GCC 11.2, x86-64, `-O2`; aynı süreçte değişiklik öncesi ve sonrası
DLL'ler ölçülmüştür. 1.000 ısınma ve dokuz turda 100.000 C çağrısı vardır.
Python çağrısı iç döngüye dahil değildir. Sıra turlar arasında döndürülür.
"Önce" mevcut firmware değildir; Clinger eklenmeden önceki JSON çekirdeğidir.

| Girdi | Önce µs | Sonra µs | Önce/sonra |
|---|---:|---:|---:|
| `-123.125` | 0,3643 | 0,0284 | 12,85 |
| `0.016` | 0,3477 | 0,0253 | 13,72 |
| `123456789.125`, kesin yola döner | 0,3791 | 0,3959 | 0,96 |

Mevcut üretim helper'ı aynı `-123.125` örneğinde 0,0086 µs ölçülmüştür;
yeni kod hâlâ yaklaşık 3,3 kat süre harcar. Özellik/doğrulama kapsamı aynı
değildir. `0.016` için eski helper doğru binary32 referansı değildir;
bu satırda esas karşılaştırma iki yeni çekirdek sürümü arasındadır.

Bu koşuda diğer config/status süreleri de host yüküyle değişmiştir;
float içermeyen işlemlerdeki farklar bu algoritmanın kazancı sayılmamalıdır.
Ham ölçümün kalıcı kopyası [metrics JSON dosyasındadır](2026-10-23-json-clinger-host-metrics.json).
Kalıcı benchmark iki ek timing-only sayı fixture'ı içerir.

## Doğrulama

- 86 ayrı Unity testi normal ve coverage derlemesinde geçti.
- İki yeni native test hızlı yol sınırlarını, hemen dışındaki değerleri,
  exponent gösterimini, negatif sıfırı ve dört FPU yuvarlama modunu sınar.
- Yeni 100.000 deterministik mantissa/exponent girdisi host CRT `strtof`
  referansıyla bit düzeyinde eşleşti; seed `0x239378`'dir.
- Önceki 100.000 binary32 çekilişi, 7.650 midpoint, 100.000 lexical mutation,
  996.370 fixed format ve tüm Unicode/JSON/skip referansları geçti.
- Coverage: 1050/1079 satır (%97,3), 897/955 branch (%93,9),
  85/85 fonksiyon (%100). GCC sıkı uyarıları ve statik analiz temizdir.

## ARM statik kaynak kontrolü

Firmware yüklenmeden mevcut ARM compiler kapıları da çalıştırılmıştır.
`fegetround` yeni standart C bağımlılığıdır. Bu toolchain'in gerçek libm
object'i `VMRS FPSCR`, maskeleme ve dönüşten oluşur; undefined symbol,
heap veya errno bağımlılığı yoktur. Kaynak kapısına yalnız bu doğrulanmış
sembol eklenmiştir.

| Ölçüm | Önce | Sonra |
|---|---:|---:|
| Modül object text/read-only, `-Os` | 6.502 B | 6.886 B |
| Modül object text/read-only, `-O2` | 10.421 B | 11.061 B |
| En büyük tek frame, `-Os` | 456 B | 480 B |
| En büyük tek frame, `-O2` | 488 B | 512 B |
| Reader / writer bağlamı | 108 / 164 B | 108 / 164 B |
| Global data/bss | 0 / 0 B | 0 / 0 B |

Object boyutuna dış `fegetround` kodu dahil değildir. Bu değerler bütün
firmware'in Flash farkı veya toplam çağrı zinciri stack tüketimi değildir.
Hızlı ve kesin dönüşümün yerel değişkenleri ayrı fonksiyonlara alınarak
mevcut 512 B tek frame kapısı korunmuştur; sınır yükseltilmemiştir.

## Tekrar çalıştırma ve sonraki adım

`test/` dizininde:

```text
JSON_BASELINE_SOURCE=<önceki-json.c-tam-yolu>
ceedling json:check json:coverage
```

PowerShell'de değişken `$env:JSON_BASELINE_SOURCE` ile verilmelidir.
Bu oturumun snapshot'ı `integration/json/build/json_before_clinger.c`'dir.
Önceki [DWT ölçümleri](2026-10-23-json-device.md) bu Clinger sürümüne ait
değildir. Bir sonraki gerçek cihaz koşusunda aynı benchmark yeniden
çalıştırılmalıdır; host hızlanması ARM'e doğrudan uygulanmamalıdır.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | C11 Clinger hızlı yolu, rounding guard, kalıcı referans/native testler ve host önce/sonra ölçümü |
