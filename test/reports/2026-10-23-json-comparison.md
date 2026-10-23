# Mevcut JSON ve bağımsız çekirdek karşılaştırması

**Tarih:** 23.10.2026

Bu kayıt ilk karşılaştırmanın ölçümleridir. Sadeleştirmeler uygulanmış;
güncel aynı süreçte önce/sonra sonuçları ve kabul kanıtı
[tekrar ölçüm raporuna](2026-10-23-json-simplification.md) eklenmiştir.

## Amaç

Mevcut JSON okuma/yazma yolu ile `libraries/json/json.c` aynı iş üzerinde
karşılaştırılmalı; süre, bağlı kod boyutu ve davranış farkları geçiş
kararından önce görünür olmalıdır.

**Sonuç:** Bu sürüm hız veya mevcut firmware'de Flash küçülmesi sağlamamıştır.
Toplu geçiş yapılmamıştır. Yeni çekirdeğin kazancı daha sıkı doğrulama,
Unicode, doğru binary32 dönüşümü ve parçalı çalışma yeteneğidir.

## Kullanım yeri ve tekrar çalıştırma

`test/` dizininde:

```text
ceedling json:compare
```

`ARM_CC` mevcut `arm-none-eabi-gcc.exe` tam yolu olarak verilirse host ve
hedef boyutları birlikte ölçülür. Ayrı Python girişi:

```text
python test/integration/json/run_comparison.py --arm-cc <compiler-path>
```

`json:compare`, `json:check` ve merkezi JSON integration paketine bağlıdır.
Hız eşiği konmamıştır; çıktı eşdeğerliği ve hata testleri başarısız olursa
komut başarısız olur. Yerel çıktılar:

- `test/integration/json/build/comparison.json`: host + ARM ölçümü;
- `comparison_host.json`: yalnız host ölçümü;
- `compare_legacy.inc`: mevcut üretim fonksiyonları, her koşuda yeniden çıkarılır;
- `compare_status.inc`: üretim durum alanlarıyla oluşturulan test adaptörü;
- `size_*.elf`, `compare_arm_*.su`: bağımsız link ve stack kayıtları.

Kalıcı test kaynakları `run_comparison.py` ve `compare.c` dosyalarıdır.
Üretilen dosyalar kaynak yerine elle düzenlenmemelidir. Ham rapor kaynak
SHA-256 değerlerini, derleyici/clock bilgisini ve dokuz turun aralığını içerir.

## Ölçüm yöntemi

### Gerçek üretim girişleri

Eski parser için güncel `json_config.c` içindeki `parse_modbus_config` ve
`parse_device_config` ile onların gerçek yardımcıları kullanılmıştır.
Üretim algoritması mock edilmemiştir; fonksiyon gövdeleri her derlemede
kaynaktan çıkarılır. Fonksiyon bulunamaz veya tek değilse test durur.

Yeni tarafta yalnız test dizinindeki sade adaptörler kullanılmıştır.
Key eşlemesi ve tür kontrolünden sonra gerçek model alanlarına açık atama
yapılır; core'a struct mapping eklenmemiştir. Aynı port/PIN/timezone/baud/ID
doğrulayıcıları paylaşılır. Modem getter sabit bir mevcut yapı döndürür;
okunabilir alanlar ve kısmi güncellemede korunacak salt okunur alanlar
iki tarafta aynıdır. Alanlar, getter ve veri kaynağı geçiş için değiştirilmemiştir.

Writer için `system_status_json_build` üretim gövdesi kullanılmıştır.
Yeni adaptör aynı alan listesini kaynaktan alır. GSM getter'ları iki
tarafta aynı sabit snapshot (veri görüntüsü) değerlerini verir; UART/GSM
ve getter erişim süresi bu karşılaştırmanın konusu değildir.

Float örneklerinde okuma `-123.125`, yazma aynı binary32 değeriyle
yapılmıştır; eski `%.3f` ile yeni dokuz basamaklı writer bu örnekte
**aynı metni** üretir. Bu, genel float biçimlerinin eşdeğer olduğu anlamına
gelmez; aşağıdaki fark tablosuna bakılmalıdır.

### Host süreleri

MinGW GCC 11.2, `-O2`, x86-64; Python 3.14.6
`perf_counter_ns` → Windows `QueryPerformanceCounter` kullanılmıştır.
Her örnekte 1.000 çağrı ısınma, ardından 20.000 veya 100.000 çağrılık C
döngüsü ölçülür. Python/ctypes çağrısı **döngünün içinde değildir**.
Dokuz turda ilk çalıştırılan taraf değiştirilir; ortanca süre kaydedilir.
Çıktı/checksum tüketilir; derleyicinin işi kaldırması önlenir.

Küçük C dispatch/checksum maliyeti süreye dahildir; özellikle onlarca
nanosaniyelik primitive sonuçlar yalnız algoritma gecikmesi gibi
yorumlanmamalıdır. HTTP, modem, Flash/NVRAM ve log I/O süresi dahil değildir.
Loglar iki tarafta da mevcut `NO_CONSOLE_LOG` seçeneğiyle kapalıdır.

### ARM kod boyutu

STM32 GCC 14.3.rel1, Cortex-M33, Thumb, `fpv5-sp-d16`, hard-float,
`-Os`, function/data sections, nano/nosys libc ve linker GC kullanılır.
Başlangıç kodu eklenmez; her profil yalnız ilgili fonksiyon köklerini
tutar. Bu ELF dosyaları firmware değildir ve cihaza yüklenmemelidir.
Flash değeri `.text`/read-only veri + `.data` yüküdür.

Bu yöntem gerçek derleyici/linker maliyetini gösterir. Bütün firmware'e
uygulanmış bir değişikliğin map farkı değildir; IEC/RF iş kuralları,
HTTP handler'ları ve mevcut diğer ortak bağımlılıklar tamamen taşınmamıştır.

## Host süre sonucu

Son ölçümün ortancaları aşağıdadır; kesin tekrar değerleri ham raporda
tutulmalıdır. CPU yüküne göre özellikle device/status sonuçları değişmiştir.

| İş | Eski µs | Yeni µs | Yeni/eski |
|---|---:|---:|---:|
| uint32 okuma | 0,0153 | 0,0382 | 2,49× |
| Float okuma, tam temsil edilen örnek | 0,0090 | 0,3938 | 43,65× |
| Modbus POST, 60 B | 0,1654 | 0,3973 | 2,40× |
| Modbus + bilinmeyen iç içe değer, 138 B | 0,3886 | 0,8478 | 2,18× |
| Cihaz ayarı POST, 390 B | 1,9883 | 2,4812 | 1,25× |
| Sistem durumu GET | 3,8852 | 5,9569 | 1,53× |
| Float yazma, aynı çıktı | 0,0733 | 0,1234 | 1,68× |

Yeni reader token kopyalama, grammar/UTF-8 denetimi ve typed helper
kontrolleri yapar. Mevcut sayısal yardımcıların yaptığı iş daha dardır.
Yeni float dönüşümünde tam binary32 yuvarlama için bounded integer
hesabı kullanılır; eski parser basit float toplama/bölme döngüsüdür.
Writer'daki byte bazlı devam işlemleri de maliyet taşır. Hız farkları
yalnız özellik farkına bağlanarak kapatılmış sayılmamalıdır; toplu kopyalama
ve gereksiz tekrar taramalarını azaltma ayrıca değerlendirilebilir.

## ARM bağlı Flash sonucu

| Profil | Eski B | Yeni B | Fark |
|---|---:|---:|---:|
| uint32 okuma | 413 | 380 | −33 |
| Float okuma | 505 | 1.122 | +617 |
| Modbus parser/adaptör | 1.587 | 2.946 | +1.359 |
| Cihaz parser/adaptör | 2.901 | 3.987 | +1.086 |
| Durum writer, formatter başka yerde gerekmiyorsa | 6.977 | 3.129 | −3.848 |
| Float writer, formatter başka yerde gerekmiyorsa | 5.380 | 1.678 | −3.702 |
| Birleşik seçili profil, formatter tamamen kalkarsa | 10.463 | 8.943 | −1.520 |
| Birleşik profil, `xprintf` korunursa | 10.463 | 14.239 | **+3.776** |

**Bu projede `xprintf` log ve diğer çıktı yollarında kalmaktadır.**
İzole writer küçülmesini firmware kazancı olarak sunmak doğru değildir.
Örneğin yalnız durum writer geçişinde formatter korunursa 6.977 → 8.433 B
olur; kazanç yerine 1.456 B eklenir. Birleşik profil artışı yaklaşık
**3,7 KiB**'dir. Bu değer gerçek bütün firmware geçişinin kesin artışı değildir.

Fixture durum yapısının 76 B `.data` alanı her iki tarafta ortaktır;
formatter kalınca 8 B `.bss` de korunur. Core'un global RAM'i sıfırdır;
caller-owned reader 108 B, writer 128 B ve token/output tamponları ayrıca
gereklidir. Bu adaptörde reader token tamponu 256 B'dir. İki tarafa aynı
8 KiB çıkış tamponu verildiğinden bu ölçümde streaming RAM tasarrufu yoktur.

## Eşdeğerlik ve gözlenen farklar

Doğrulananlar:

- Ölçülen bütün geçerli girdilerde aynı binary sonuç veya aynı JSON baytları;
- Tam/kısmi/ters sıralı cihaz ve Modbus alanları; aynı aralık retleri;
- 1.002 durum görüntüsü: sıfırlar, bütün integer sınırları, rastgele bit
  örnekleri, işaretli değerler, UINT32_MAX ADC yaşı, tüm alanlar ve array'ler;
- Writer kapasitesinde 0, 1, gereken boyutun altı, tam sınır ve NUL payı;
- `json:check` altında 63/63 Unity ve mevcut bağımsız Unicode/float/
  mutation kabul paketi geçti.

| Girdi/durum | Mevcut davranış | Yeni çekirdek/adaptör |
|---|---|---|
| `{"CihazID":07}` | Kabul | Ret |
| Son virgül `{"CihazID":7,}` | Kabul | Ret |
| Kök JSON'dan sonra fazlalık | Kabul | Ret |
| APN değeri `\u0069nternet` | `u0069nternet` saklanır | `internet` çözülür |
| 20 karakter APN, alan kapasitesi 16 B | 15 karaktere sessiz kırpılarak kabul | Kapasite hatasıyla ret |
| Bilinmeyen 300 B string | Atlanır/kabul | Bu adaptörün 256 B token sınırıyla ret |
| Float `1e2` | Eski yardımcı tüm metni tüketmez | 100 olarak kabul |
| Float `0.016` | `0.01599999889731407` | Doğru binary32: `0.01600000075995922` |
| Float yazımı `0.123456789f` | `%.3f` → `0.123` | `0.123456791` |

Bu farklar otomatik API uyumluluğu sayılmamalıdır. Eski parser kusurları
bu görevde düzeltilmemiştir; yeni çekirdeğin doğru davranışı henüz gerçek
endpoint'lere taşınmamıştır. Unknown-field token sınırı ve RF'nin mevcut
1/2/3 ondalık basamak sözleşmesi geçişte ayrıca kararlaştırılmalıdır.

## Karar ve sınırlar

Hız/kod küçültme gerekçesiyle toplu geçiş önerilmemiş ve uygulanmamıştır.
Önce writer'ın toplu kopyalama gibi basit iyileştirmeleri ve sabit ondalık
biçim ihtiyacı ele alınmalı, sonra yeniden ölçülmelidir. Geçiş yapılacaksa
tek bir endpoint ile başlanmalı; iş kuralları/kalıcı durum davranışı
korunarak ilgili gerçek HTTP testlerinden geçilmelidir.

Host sürelerinden Cortex-M33 cycle sonucu çıkarılmamalıdır. Özellikle
mevcut ARM formatter profilinde `__aeabi_f2d`, `__aeabi_dmul`,
`__aeabi_ddiv` ve diğer yazılımsal double yardımcıları görülmüştür;
x86-64 aynı maliyeti taşımaz. Float'ın hedefteki hız kararı için gerçek
cihazda DWT cycle ölçümü ayrıca yapılmalıdır. Bu görevde DWT/cihaz
ölçümü, firmware entegrasyonu, yükleme veya reset yapılmamıştır.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | Üretim fonksiyonlarıyla eşdeğerlik, C batch süreleri, ARM link profilleri ve geçiş değerlendirmesi |
