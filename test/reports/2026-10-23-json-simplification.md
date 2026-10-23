# JSON sadeleştirme ve tekrar ölçüm

**Tarih:** 23.10.2026

Sonraki v0.2 özellik genişletmesi ve güncel kabul
[kopyasız skip/fixed float raporundadır](2026-10-23-json-stream-fixed.md).
Bu kayıttaki ölçümler ilgili optimizasyon aşamalarının tarihsel sonuçlarıdır.

Bu kayıt ilk sadeleştirme, ASCII hızlandırması ve sayısal tarama olmak üzere
üç aşama içerir. Güncel kaynak kabul değerleri "Sayısal tarama ve xscanf
değerlendirmesi" bölümünden alınmalıdır.

## Amaç

Bağımsız JSON çekirdeğindeki tekrar taramalarını ve byte başına yazma
maliyetini azaltmak; değişiklik öncesi ve sonrasını aynı koşuda ölçmek.
İlk karşılaştırmanın yöntemi ve uyumluluk sınırları
[önceki raporda](2026-10-23-json-comparison.md) açıklanmıştır.

## Kullanım yeri ve değişiklik

`integer_value`, geçerli integer metnini önceden syntax (sözdizimi)
doğrulamasından geçirmek yerine tek taramada dönüştürür. Hatalı veya
integer olmayan girdilerde mevcut hata önceliği korunur. Örneğin taşan
sayının sonundaki geçersiz karakter yine syntax hatası verir; hedef değişmez.

Writer, escape (kaçış) gerektirmeyen string bölümlerini ve raw sayı/literal
metinlerini `memcpy` ile toplu kopyalar. Tarama mevcut çıkış kapasitesiyle
sınırlıdır; tek byte tamponla devam ederken kalan string tekrar tekrar
taranmaz. Escape, prefix ve suffix devam işlemleri mevcut yolu kullanır.
Yeni tampon, flag, public API veya bağlam alanı eklenmemiştir.

## Ölçüm yöntemi

MinGW GCC 11.2 `-O2`, x86-64, Windows QueryPerformanceCounter kullanılmıştır.
Mevcut firmware, değişiklik öncesi çekirdek ve güncel çekirdek aynı süreçte
ölçülmüş; dokuz turda ilk ölçülen taraf döndürülmüştür. Ortancalar aşağıdadır.
Önceki koşunun süreleriyle doğrudan oran hesaplanmamıştır.

Değişiklik öncesi `json.c` SHA-256:
`2a64180fdadff7ff6203f83ab6895385c390a5c4c929f348179b718f13831741`.
Yerel snapshot `test/integration/json/build/json_before_simplification.c`
dosyasındadır; build dizini Git'e alınmaz. Kalıcı benchmark, aynı public
API'ye sahip herhangi bir önceki kaynak dosyasını kabul eder.

`test/` dizininde, `ARM_CC` mevcut hedef derleyici tam yolu olmalıdır:

```powershell
$env:JSON_BASELINE_SOURCE = '<önceki-json.c-tam-yolu>'
ceedling json:check json:coverage json:arm
```

Yalnız karşılaştırma için `ceedling json:compare` kullanılmalıdır.
Python girişi `--baseline-source <path>` seçeneğini de kabul eder.
Ham süre aralıkları, kaynak hash'leri ve iki ARM profil grubu
`test/integration/json/build/comparison.json` içine yazılır.

## Host süre sonucu

| İş | Mevcut firmware µs | Çekirdek önce µs | Çekirdek sonra µs | Önce/sonra |
|---|---:|---:|---:|---:|
| uint32 okuma | 0,0125 | 0,0316 | 0,0128 | 2,46 |
| Float okuma | 0,0085 | 0,3478 | 0,3492 | 1,00 |
| Modbus POST | 0,2069 | 0,5399 | 0,5064 | 1,07 |
| Modbus + bilinmeyen değer | 0,5365 | 1,1921 | 1,1231 | 1,06 |
| Cihaz ayarı POST | 2,0200 | 3,6318 | 3,5255 | 1,03 |
| Sistem durumu GET | 2,0336 | 3,6126 | 2,7474 | 1,31 |
| Float yazma, aynı çıktı örneği | 0,0764 | 0,1221 | 0,1119 | 1,09 |

Integer ve durum yazımında belirgin kazanç vardır. Küçük POST farkları
host yükü ve ölçüm değişkenliğiyle birlikte değerlendirilmelidir.
Float okuma algoritması değiştirilmemiştir; ölçümde fark yoktur.
Çekirdek durum writer'ı bu koşuda mevcut firmware'den hâlâ 1,35 kat,
Modbus adaptörü 2,45 kat daha uzun sürmüştür.

## ARM kaynak sonucu

STM32 GCC 14.3.rel1, Cortex-M33, hard-float, `-Os`, linker GC kullanılmıştır.
Bu değerler seçili bağımsız link profilleridir; bütün firmware farkı değildir.

| Flash profili | Önce B | Sonra B | Fark B |
|---|---:|---:|---:|
| uint32 okuma | 380 | 428 | +48 |
| Float okuma | 1.122 | 1.122 | 0 |
| Modbus parser/adaptör | 2.946 | 2.994 | +48 |
| Cihaz parser/adaptör | 3.987 | 4.027 | +40 |
| Durum writer, formatter dışarıda | 3.129 | 3.295 | +166 |
| Durum writer, formatter korunur | 8.433 | 8.597 | +164 |
| Birleşik profil, formatter korunur | 14.239 | 14.423 | +184 |

Mevcut firmware'in birleşik örnek profili 10.463 B'dir. `xprintf` diğer
yollarda gerektiğinden yeni çekirdeğin aynı profilde ek maliyeti
3.960 B'dir. Toplu kopyalama kontrol akışı hızı artırırken kodu küçültmemiştir.

Tüm modül object boyutu `-Os` için 5.790 → 5.972 B, `-O2` için
9.217 → 9.433 B olmuştur. Global `.data/.bss` yine sıfırdır.
Reader 108 B, writer 128 B; en büyük tek stack frame `-Os` 456 B,
`-O2` 488 B olarak kalmıştır. Bunlar toplam çağrı zinciri stack ölçümü değildir.

## Doğrulama

- 68 ayrı Unity testi geçti; aynı testler coverage derlemesinde de geçti.
- Yeni beş test: büyük string/escape için bütün çıkış parça boyutları,
  sıfır kapasite, SIZE_MAX tam sınırı/taşması, çıktı canary'leri ve
  integer syntax hata önceliği.
- Bağımsız 2.005 belge, 100.000 bozuk/rastgele girişin üç parçalanması,
  100.000 float çekilişi, 7.650 tam yuvarlama eşiği ve bütün Unicode
  scalar taraması geçti. UTF-8 ve tek surrogate ret testleri de geçti.
- Durum çıktısında 1.002 görüntü ve kapasite sınırları hem önceki hem
  güncel çekirdekle mevcut üretim writer'ına byte düzeyinde eşit çıktı verdi.
- Coverage: 931/964 satır (%96,6), 787/849 branch (%92,7),
  79/79 fonksiyon (%100). Bağımsız Python vakaları bu kapsama eklenmemiştir.
- Sıkı C11 derlemesi, GCC `-fanalyzer` ve ARM kaynak sınırları geçti.

## Karar ve sınırlar

Sadeleştirmeler uygulanmıştır; public API ve JSON çıktısı korunmuştur.
Hız kazancı karşılığında seçili birleşik profile 184 B eklenmiştir.
Hız/kod küçültme gerekçesiyle ana programa toplu geçiş hâlâ önerilmemektedir.

RF için sabit 1/2/3 ondalık basamak davranışı değiştirilmemiş; yeni float
writer'ın dokuz anlamlı basamağı korunmuştur. Endpoint geçişi yapılmamıştır.
Host süreleri Cortex-M33 çevrim ölçümü değildir. Fiziksel cihaz testi,
DWT ölçümü, firmware yükleme veya reset yapılmamıştır.

## ASCII hızlandırması ve toplu okuma

### Kapsam ve kanıt

Kullanıcı kararıyla UTF-8 korunmuştur. `valid_utf8`, ASCII karakterlerini
genel UTF-8 state (durum) işlemlerine girmeden geçirir. Bekleyen çok baytlı
karakter varsa ASCII byte bu yolu kullanamaz; geçersiz continuation
(devam byte'ı) hâlâ reddedilir.

`read_string`, escape veya bekleyen UTF-8 devam byte'ı yokken düz ASCII
metni `memcpy` ile kopyalar. Tarama hem mevcut input hem token kapasitesiyle
sınırlıdır. Quote, backslash, kontrol ve ASCII dışındaki byte'lar mevcut
doğrulama yoluna bırakılır. Kapasite dolduğunda mevcut yol aynı byte'ta
aynı hatayı verir. Yeni tampon veya bağlam alanı eklenmemiştir.

`emit_bytes` yalnız `write_source` tarafından çağrılır; bu çağrı zaten
çıkış kapasitesini sınırlar. İkinci kapasite kontrolü kaldırılmış, toplam
boyut taşması kontrolü korunmuştur. Float dönüşümü ve sabit ondalık
basamak ihtiyacı bu aşamada değiştirilmemiştir.

### Aynı koşuda önce ve sonra

Önceki bölümle aynı derleyiciler ve dokuz turlu ölçüm kullanılmıştır.
Bu bölümün "önce" kaynağı ilk sadeleştirmenin sonucudur; SHA-256:
`3cfcee9768ba7b1ec3f5dc0bef01c4b0c041c95704cd3bc404497db271e1758c`.
Yerel snapshot `test/integration/json/build/json_before_ascii.c` dosyasındadır.
`JSON_BASELINE_SOURCE` bu dosyaya verilerek aynı komutlar çalıştırılmıştır.
Güncel kaynak SHA-256:
`99c07cbe0ac25d3e8f5c3c458941122000023387d6aa0a165f4fcb532a8b2ed5`.

| İş | Mevcut firmware µs | Önce µs | Sonra µs | Önce/sonra |
|---|---:|---:|---:|---:|
| uint32 okuma | 0,0127 | 0,0123 | 0,0128 | 0,96 |
| Float okuma | 0,0124 | 0,4058 | 0,3651 | 1,11 |
| Modbus POST | 0,2486 | 0,4842 | 0,3775 | 1,28 |
| Modbus + bilinmeyen değer | 0,5120 | 1,0908 | 0,9565 | 1,14 |
| Cihaz ayarı POST | 1,8164 | 3,0418 | 2,2073 | 1,38 |
| Sistem durumu GET | 2,2152 | 2,8270 | 2,3423 | 1,21 |
| Float yazma, aynı çıktı örneği | 0,0766 | 0,1152 | 0,1109 | 1,04 |

Integer ve float dönüşüm algoritmaları bu aşamada değiştirilmemiştir;
bu satırlardaki farklar algoritma kazancı olarak yorumlanmamalıdır.
Ölçüm aralıkları host yüküyle değişir; hızlanma oranları garanti değildir.

| ARM Flash profili | Önce B | Sonra B | Fark B |
|---|---:|---:|---:|
| Modbus parser/adaptör | 2.994 | 3.100 | +106 |
| Cihaz parser/adaptör | 4.027 | 4.107 | +80 |
| Durum writer, formatter korunur | 8.597 | 8.605 | +8 |
| Birleşik profil, formatter korunur | 14.423 | 14.511 | +88 |

Tüm modül object boyutu `-Os` 6.056 B, `-O2` 9.605 B'dir.
Global `.data/.bss` sıfır, reader 108 B, writer 128 B ve en büyük tek
stack frame 456/488 B olarak kalmıştır. Mevcut firmware'in birleşik
örnek profili 10.463 B olduğundan yeni profile geçiş maliyeti 4.048 B'dir.
Bu, bütün firmware entegrasyonunun map farkı değildir.

### Güncel doğrulama ve sınırlar

72 ayrı Unity testi hem normal hem coverage derlemesinde geçti.
Eklenen dört test; uzun ASCII/UTF-8/escape karışımının her input parça
boyutunu, token kapasitesi ve canary sınırlarını, tam hata konumunu,
UTF-8 lead byte sonrasında geçersiz ASCII'yi ve writer'ın hatalı UTF-8
girişinde çıktı üretmemesini sınar. Testler merkezi Ceedling paketindedir.

2.005 belge, 100.000 mutation × üç parçalanma, 100.000 float çekilişi,
7.650 yuvarlama eşiği, bütün 1.112.064 Unicode scalar × iki encoding,
24.576 iki byte dizisi ve 2.048 tek surrogate testi geçti.
1.002 durum görüntüsü ve kapasite sınırlarında üretim writer'ıyla
çıktı eşitliği hem önceki hem güncel çekirdekte doğrulandı.

Coverage: 953/984 satır (%96,8), 812/871 branch (%93,2), 80/80 fonksiyon
(%100). GCC sıkı uyarıları ve `-fanalyzer`, ARM boyut/stack/bağımlılık
kapıları geçti. Public API ve header değişmemiştir.

Yeni yapı bu host koşusunda mevcut firmware'e göre durum yazımında
1,06, Modbus okumada 1,52, cihaz ayarı okumada 1,22 kat süre harcamıştır.
Bu nedenle yalnız hız veya Flash küçültme gerekçesiyle toplu geçiş sonucu
çıkarılmamalıdır. Fiziksel cihaz/DWT ölçümü veya firmware geçişi yapılmamıştır.

## Sayısal tarama ve xscanf değerlendirmesi

### Değişiklik ve kaynak kanıtı

Integer taşma sınırının bölüm ve kalan değerleri döngü öncesinde hesaplanır.
ARM `-Os` assembly kontrolünde önceki `integer_value` içinde `UDIV` digit
döngüsündeyken yeni sürümde döngü öncesindedir. Bu, hedefte fiziksel süre
ölçümü değildir. Hata sınıfları ve hedefi hata halinde koruma davranışı aynıdır.

Float dönüşümünde `valid_number` ile ön tarama kaldırılmış; mevcut
`number_next` grammar (sözdizimi) durumları `decimal_ratio` ve
`parse_exponent` içine alınmıştır. Geçerli sayı typed helper içinde bir
kez taranır. Mantissa kapasitesi aşıldığında syntax hata önceliğini korumak
için yalnız hata yolunda `valid_number` kullanılır. Binary32 yuvarlama
algoritması, UTF-8 ve public API değiştirilmemiştir.

Değişiklik öncesi kaynak SHA-256:
`99c07cbe0ac25d3e8f5c3c458941122000023387d6aa0a165f4fcb532a8b2ed5`.
Yerel snapshot `test/integration/json/build/json_before_numeric.c`.
`JSON_BASELINE_SOURCE` bu dosyaya verilerek önceki bölümlerdeki komut
çalıştırılmıştır. Güncel kaynak SHA-256:
`44cb3b81f0868eb4b34bfc1f610aeb548b47e76c82f5e9c76fa8500a39937ab9`.

### Son ölçüm

| İş | Önce µs | Sonra µs | Önce/sonra |
|---|---:|---:|---:|
| uint32 okuma | 0,0120 | 0,0117 | 1,03 |
| Float okuma | 0,3673 | 0,3566 | 1,03 |
| Modbus POST | 0,2960 | 0,2965 | 1,00 |
| Modbus + bilinmeyen değer | 0,7765 | 0,8336 | 0,93 |
| Cihaz ayarı POST | 1,7789 | 1,8120 | 0,98 |
| Sistem durumu GET | 2,3546 | 2,4431 | 0,96 |
| Float yazma | 0,1135 | 0,1127 | 1,01 |

Bu koşuda sayısal helper farkları yaklaşık %3'tür; bütün işlemlerde
belirgin veya tutarlı bir host hız kazancı gösterilmemiştir. Writer
değişmediği halde görülen farklar da host ölçüm değişkenliğine işaret eder.

`-Os` ARM profili: uint32 428 → 452 B, float okuma 1.122 → 1.170 B,
birleşik/formatter korunan profil 14.511 → 14.583 B (**+72 B**).
Tüm modül object boyutu `-Os` 6.128 B, `-O2` 9.685 B'dir. Global RAM,
108/128 B bağlamlar ve 456/488 B en büyük tek stack frame değişmemiştir.
Mevcut firmware birleşik örneğine göre ek Flash 4.120 B'dir; bütün
firmware map farkı olarak yorumlanmamalıdır.

### xscanf için öneri ve test kanıtı

Mevcut JSON `Application/web-server/json_config.c:272` ve `:371` içinde
kendi `parse_uint32`/`parse_float` yardımcılarını kullanır. `xscanf`
GSM AT cevaplarında kullanılır; örnek çağrı `Application/gsm/gsm_engine.c:1447`.
`xscanf.h` format listesinde ve `xscanf.c` dispatch kodunda float yoktur.
Bu nedenle JSON çekirdeğinin `xscanf` bağımlılığı alması önerilmemektedir.

İki yeni kalıcı test `test/libs/test_xscanf_edges.c` içindedir:

- `%u32`, `01` metnini 1 kabul eder. `1e2` metninde 1 döndürüp ilk byte'ta
  durur. `42949672950` metninde ilk on rakamı okuyup UINT32_MAX döndürür;
  son rakam tüketilmez. Bunlar scanner sözleşmesidir; JSON doğrulaması değildir.
- `%f` için `XSCANF_ERR_INVALID_FORMAT` döner; hedef float korunur.

`xscanf_ex.input_pos` ile tam tüketim kontrolü yapılabilir; fakat JSON
grammar, leading zero ve float desteği yine eklenmelidir. Format yorumlama
ve bağımlılık getirisi için hız/boyut kazancı ölçülmemiştir. Genel scanner
JSON katmanının yerine geçirilmemiştir; scanner kodu değiştirilmemiştir.

Tekrar kontrol komutu, `test/` dizininde:

```text
ceedling test:test_xscanf_edges test:test_xscanf_format_scenario
```

19 ayrı scanner Unity testi ve mevcut embedded senaryonun 40 iç kontrolü
geçti. JSON için 75 ayrı Unity testi normal ve coverage derlemelerinde,
önceki bağımsız JSON/Unicode/float paketinin tamamı ve iki sürümde 1.002
durum görüntüsü/kapasite eşitliği kontrolleri geçti. Coverage 965/996 satır
(%96,9), 825/883 branch (%93,4), 81/81 fonksiyon (%100). Sıkı GCC
derlemesi, `-fanalyzer` ve ARM kaynak kapıları geçti.

Atlanan değerlerin kopyasız okunması, farklı float algoritması ve sabit
ondalık API bu aşamada eklenmemiştir. Cihaza yükleme/DWT ölçümü ve ana
programa geçiş yapılmamıştır; host sonucu hedef hız kanıtı sayılmamalıdır.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | Writer toplu kopyalama, integer tek tarama, beş regresyon testi ve aynı süreçte önce/sonra ölçüm |
| 23.10.2026 | UTF-8 korunarak ASCII hızlandırması, reader toplu kopyalama, tekrarlanan kontrolün kaldırılması ve 72 testle tekrar ölçüm |
| 23.10.2026 | Integer cutoff, float syntax/dönüşüm taramasının birleştirilmesi, 75 JSON testi ve xscanf uyumluluk değerlendirmesi |
