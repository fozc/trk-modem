# IEC104 kütüphanesi

## Amaç

Sabit bellek kullanan C11 IEC 60870-5-104 slave (kontrol edilen istasyon)
kütüphanesidir. Bu klasörde ürün verisi, NVRAM, RF, RTC, GSM, HAL veya
işletim sistemi bağımlılığı yoktur. Heap ve thread kullanılmaz.

## Kullanım yeri

Başka projeye bu klasördeki üç `.c` ve dört `.h` dosyası kopyalanır.
Derlemeye `iec104_asdu.c`, `iec104_core.c`, `iec104_server.c` ve bu klasörün
include yolu eklenir. Ürün değişiklikleri uygulama adaptöründe yapılmalıdır.
Bu depodaki örnek [modem adaptörü ve akış rehberidir](../iec104_modem/README.md).
Çalışan firmware'in eski stack seçimi değiştirilmemiştir.

## Yapı

| Modül | Görev |
|---|---|
| `iec104_protocol.h` | Type ID, COT, QOI, kontrol ve kalite sabitleri |
| `iec104_asdu.c/.h` | Host veri tipleri, tipli nokta hazırlama, ASDU parse/builder ve CP56 |
| `iec104_core.c/.h` | TCP parçaları, I/S/U frame, sıra numarası, ACK, pencere ve timer |
| `iec104_server.c/.h` | Genel sorgu/komut akışı, callback ve handler yönlendirmesi |

Nesneler çağıran tarafından ayrılır. Her nesne bir bağlantıyı yönetir.
Global singleton, ikinci olay kuyruğu veya cihaz durumuna erişim yoktur.

## Veri hazırlama

`iec104_time_t`, byte düzeninden bağımsız zaman alanlarını taşır.
`iec104_point_t` IOA ve hazırlanmış bilgi elemanını saklar. Struct belleği
TCP üzerinden doğrudan gönderilmemelidir.

| Fonksiyon | Zaman NULL ise | Zaman verilirse |
|---|---|---|
| `iec104_point_set_single()` | Type 1 | Type 30 |
| `iec104_point_set_float()` | Type 13 | Type 36 |
| `iec104_point_set_double()` | Kabul edilmez | Type 31 |

Örnek nokta hazırlama:

```c
iec104_time_t time = { .milliseconds = 12500U, .minute = 30U, .hour = 14U,
                       .day = 30U, .month = 10U, .year = 26U };
iec104_point_t point;
bool valid = iec104_point_set_float(&point, 100U, 12.5F, 0U, &time);
```

Uygulama desteklenen tiplerde `value[]` offset'lerini ve `value_size`
değerini hesaplamamalıdır. Setter hatasında önceki nokta korunur. Kaynak
saati mevcut değilse `time.invalid = true` kullanılır. Özel yeni tipler
encoded (kodlanmış) `value` ve `value_size` alanlarını kullanabilir;
boyut sınırı `IEC104_POINT_VALUE_MAX` değeridir. Varsayılan 12 byte
korunur; tüm derleme birimlerinde aynı ayarla 12–240 arasında seçilebilir.
`iec104_point_set_encoded()` yeni tip encoder'larının sınır kontrollü
kopyalama yoludur; servis/type semantiğini kendisi doğrulamaz.

## Callback sözleşmesi

Uygulamaya istek için yalnız `handle_request(context, request, response)`
bağlanır. Ayrı grup kabulü, IOA listeleme veya nokta okuma callback'i yoktur.
`iec104_request_t`, ayrıştırılmış ASDU header'ını, çözülmüş IOA'yı,
QOI/QRP qualifier'ını, Type 103 için host zaman alanlarını ve continuation
(devam çağrısı) bilgisini taşır.

| Sonuç | Kütüphanenin davranışı |
|---|---|
| `IEC104_REQUEST_ACCEPTED` | Hazır cevabı gönderir; boş komut cevabında pozitif echo üretir |
| `IEC104_REQUEST_UNSUPPORTED` | Bilinen komuta negatif ACT_CON; bilinmeyen tipe UNKNOWN_TYPE gönderir |
| `IEC104_REQUEST_REJECTED` | Negatif ACT_CON veya iptalde negatif DEACT_CON gönderir |
| `IEC104_REQUEST_UNKNOWN_IOA` | UNKNOWN_IOA (47) gönderir |
| `IEC104_REQUEST_ERROR` | Başarılı sonuç bildirmeden bağlantıyı kapatır |

Uygulama hazır tipli noktaları `iec104_response_add(response, &point)` ile
cevap parçasına ekler. Bu fonksiyon ASDU header ve byte düzenini yönetir;
aynı tip/boyut ya da paket kapasitesi uygun değilse false döndürür ve
mevcut parça korunur. Uygulama o noktayı kendi konumunda bekletip sonraki
parçada yeniden ele almalıdır.

`write` taşıma callback'i zorunludur. `handle_request = NULL` geçerlidir;
isteme uygun hata cevabı üretilir, sahte başarı verilmez. `request_reset`,
pozitif reset cevabı gönderime kabul edildikten sonraki cihaz eylemidir;
ikinci bir istek yönlendirme callback'i değildir. `event` ve `delivered`
bağlantı/protokol ACK bildirimleridir. Callback/context ömrü server'dan
kısa olmamalı; aynı nesneye callback içinden yeniden giriş yapılmamalıdır.

## Paket ve sorgu akışı

```text
TCP -> server_process_data -> core_process_data -> asdu_parse
    -> ortak protokol kontrolu
    -> application: handle_request(request, response)
    -> kutuphane: ACT_CON / veri / ACT_TERM ve I-frame gonderimi
```

GI isteğinin IOA'sı 0'dır; cevap IOA'larını application kendi verisinden
seçer. Kütüphane QOI 20–36 aralığını kontrol eder, desteklenen grubu ve
veri içeriğini seçmez. İlk callback, grup desteklenmiyorsa UNSUPPORTED
verebilir. Kabul edilen boş sorgu ACT_CON/ACT_TERM ile tamamlanır.

GI birden fazla parça gerektiriyorsa uygulama `response.more = true`
yapar. Kütüphane devam gerektiğinde aynı callback'i
`request.continuation = true` ile çağırır. Veri konumu application context'i
içindedir; kütüphanede ürün listesi veya IOA indeksi yoktur. Her veri poll'u
en fazla bir devam callback'i çalıştırır. Boş ama more=true cevap sonraki
poll'a bırakılır; iç döngüde tekrar tekrar çağrı yapılmaz.

Hazır cevap, taşıma/pencere doluyken saklanır. Gönderim retry'si uygulamayı
tekrar çağırmaz ve komutu yeniden yürütmez. İlk callback isterse ilk veri
parçasını da hazırlayabilir; yine önce ACT_CON gönderilir.

İlk istekte `request.asdu.payload` callback süresince geçerli, ödünç bir
view'dır. GI devamında yalnız header/IOA/qualifier alanları korunur;
ham payload pointer'ı tutulmaz. Uygulama ilk çağrının pointer'ını
saklamamalıdır. Özel komutlarda count/SQ/IOA ve türe özgü uzunluk/alan
kontrolü application handler'ında yapılmalıdır. Ortak CA/COT ve test-bit
kontrolü ile yerleşik 45/100/103/105/107 boyut kontrolü kütüphanededir.

Saat byte'ları kütüphanede çözülür; application Type 103 isteğinde gerçek
saat servisini çağırır. Genel reset QRP=1 ve cevap sırası kütüphanededir;
application'ın kabulünden sonra fiziksel reset yalnız `request_reset`
bildirimiyle yürütülür. Kalıcı olay yalnız `delivered` ile tüketilmelidir;
`write=true` SCADA ACK'i anlamına gelmez.

## Periyodik yayın

`iec104_server_publish_periodic(server, points, count)` uygulamanın seçtiği
aynı tipteki bir nokta grubunu COT=1 (periodic) ile gönderir. Mevcut
`iec104_server_publish()` spontane COT=3 kullanır. Aralık, gönderilecek
noktalar ve devam konumu application'a aittir; library ikinci bir zamanlayıcı
veya kuyruk oluşturmaz. False dönerse uygulama konumunu ilerletmemelidir.
GI/komut/başlangıç cevabı beklerken periyodik gönderim kabul edilmez.
Derlenebilir içerik örneği `iec104_example_publish_periodic()` fonksiyonudur.

Noktaları ara diziye koymadan paketlemek için `iec104_builder_begin()` ve
`iec104_response_add()` ile aynı tipteki noktalar tek cevap yapısına eklenebilir.
`iec104_server_publish_asdu(server, &response.asdu, tag)` hazırlanmış ASDU'yu
kopya bir nokta dizisi oluşturmadan gönderime sunar. COT=1 veya COT=3, CA/OA,
nesne boyutu ve paket uzunluğu server tarafından doğrulanır. Bu giriş de
GI/komut/başlangıç cevabının önüne geçmez. Uygulama konumu yalnız `true`
sonucunda ilerletilmelidir; `false` durumunda builder korunur.


## Reset için opsiyonel SBO

`server_config.reset_sbo=true` ve `sbo_timeout_ms>0` seçilirse,
yapılandırılmış `reset_ioa` Type 45 (`C_SC_NA_1`) ile Select → Execute
kullanır. Type 105 (`C_RP_NA_1`) QRP alanında S/E biti yoktur; ona özel
bir bit veya özel qualifier eklenmez.

```text
Type 45, reset_ioa, COT=6, SCO=0x81 -> Select, pozitif ACT_CON
Type 45, reset_ioa, COT=6, SCO=0x01 -> Execute, pozitif ACT_CON
                                      -> ACT_TERM (COT=10)
                                      -> son cevap TX kabulunden sonra request_reset
```

Seçim aynı TCP instance'ı, yapılandırılmış CA/IOA, originator, komut değeri
ve QU ile eşleşmelidir. Deadline `elapsed < sbo_timeout_ms` koşuludur;
eşitlikte seçim bitmiştir. Execute/iptal denemesi seçimi tüketir; yeniden
Execute için yeniden Select gerekir. STOP alındığında (ACK bekleyen
STOPPING dahil), bağlantı kapanışında ve timeout'ta seçim temizlenir.

Type 45 COT=8, eşleşen seçimi iptal eder ve DEACT_CON (9) alır; reset
çalışmaz. Test bitli veya reserved SCO bitli istek seçim/reset oluşturmaz.
Select onayı veya Execute ACT_CON/ACT_TERM cevabı beklerken retry, callback'i yeniden
çalıştırmaz ve seçim süresini uzatmaz.

SBO açıkken Type 105, IOA=0 üzerinden de yapılandırılmış adresten de
reddedilir; doğrudan reset yolu SBO'yu atlayamaz. SBO kapalıyken mevcut
Type 105 davranışı korunur; Type 45 Execute doğrudan kullanılabilir,
Select reddedilir. Uygulama bu reset IOA'sında state=1, QU=0 kabul eder.
Diğer IOA'ların ve başka komut tiplerinin SBO politikası bu özellikle
otomatik etkinleştirilmez. Uygulama kabulü yine tek `handle_request`
callback'inden geçer.

Referans: [lib60870 direct/SBO komut açıklaması](https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc).
Select işareti için `SingleCommand_create(..., selectCommand, qu)` modeli
esas alınır. c104 `SELECT_AND_EXECUTE` birlikte çalışması ACT_CON/ACT_TERM
zinciriyle doğrulanır; onay/cihaz eylemi ayrımı bu stack'in callback sözleşmesidir.

## Doğrulama

Repo kökünden:

```powershell
cd test
ruby -S ceedling "test:pattern[iec104_stack]"
cd ..
python -m unittest discover -s test/integration/iec104_stack -p test_runner.py -v
python test/integration/iec104_stack/run_tests.py
```

Ceedling, gerçek ASDU/core/server ve ayrı modem adaptörünü sınar. Tipli
setter'lar Type 1/13/30/31/36 tam frame golden vektörleriyle doğrulanır.
QOI 36 sunan farklı uygulama profili ve desteklenmeyen grup reddi sınanır.
Offline sınır testi üç kütüphane kaynağını yalnız kendi include klasörü ve
standart C header'larıyla strict C11/Werror altında derler.
Python TCP paketi aynı C kodunu c104/lib60870 SCADA istemcisiyle çalıştırır.
Host sonuçları fiziksel GSM/RTC/Flash kabulü değildir.

## Uygulama katmanı test komutu

Type 107 (`C_TS_TA_1`) için count=1, SQ=0, IOA=0 ve COT=6 kabul edilir.
Server 2 bayt TSC sayacını ve 7 bayt CP56Time2a alanını değiştirmeden COT=7
ile geri döndürür. Bu komut application callback'ine gitmez; RTC veya reset
servisini çağırmaz. Zaman alanı saat ayarlamak için kullanılmadığından IV=1
dahil aynen korunur. Yanlış CA/COT/IOA uygun 45–47 hata cevabını alır;
bozuk nesne uzunluğu veya VSQ bağlantıyı kapatır. Test bitiyle gelen
komutları reddeden mevcut profil kararı burada da korunur.

## Referansla algoritmik karşılaştırma

Karşılaştırma kaynağı [MZ Automation/lib60870 cs104_slave.c,
revision 7a388e3e133999e1ca77ba7521d55d074b7cd2bc](https://github.com/mz-automation/lib60870/blob/7a388e3e133999e1ca77ba7521d55d074b7cd2bc/lib60870-C/src/iec60870/cs104/cs104_slave.c)
dosyasıdır. Ürün kaynaklarına upstream kod kopyalanmamıştır.

| Yol | Bu stack'in davranışı ve doğrulama sınırı |
|---|---|
| ACK/K | N(R) yalnız mevcut onaydan pencere sonuna kadar ilerler; aynı onay tekrarlanabilir. Geriye/ileriye taşan onay reddedilir; 32767→0 sarma test edilir. |
| STOP | STOPPING son onayları bekler. STOPPED durumunda yalnız U-frame kabul edilir. START aynı TCP oturumunun sayaçlarını korur. |
| T1/T2/T3 | T1 en eski ACK bekleyen frame'i izler; kısmi ACK kalan süreyi yenilemez. T2/W alım onayını tetikler. T3 son tamamlanmış alınan APDU'dan ölçülür; yerel gönderim peer'in canlı olduğunu kanıtlamaz. |
| ASDU | 249 bayt sınırı, SQ/nesne uzunluğu, CA/COT/IOA ve komut yapısı ayrı doğrulanır. |
| GI/komut | GI için ACT_CON/veri/ACT_TERM sırası, komutlar için tek callback çalışması test edilir. Type 107 testi sayaç/zamanı aynen döndürür. |

Referansın thread, heap, çoklu bağlantı ve genel kuyruk mimarisi bu ürüne
alınmaz. Bu uygulama tek bağlantı ve sabit bellek profilini korur. Tip
numarasının tanımlı olması servis uygulaması anlamına gelmez; destek
kapsamı [tip kataloğunda](examples/README.md) gösterilir.

## Örnekler ve bütün tipler için genişleme yolu

[Örnekler ve 81 Type ID kataloğu](examples/README.md), her tipin mevcut
desteğini ve yeni encoder/handler/servis ihtiyacını ayrı gösterir.
`examples/iec104_example.c/.h` tek istek callback'i ve uygulama içindeki veri seçimiyle
Type 1/13/30/31/36 kullanımını ve Type 7 extension encoder'ını gösterir.
Örnek callback'ler merkezi Ceedling testinde gerçek server/core ile çalışır;
firmware'e veya gerçek veri üreticilerine otomatik bağlanmaz.

Tip başına boş callback veya handler tablosu yoktur. Uygulama tek
`handle_request` içinde Type ID ve qualifier ile gereken işlemi seçer. Değişken boyutlu dosya servisi ve güvenlik
uzantıları yalnız Type ID ekleyerek tamamlanmış sayılmaz; işlem akışları ve
servise uygun COT/uzunluk doğrulaması ayrıca uygulanmalıdır.

## Doxygen ve referans

Bu klasörde `doxygen Doxyfile` çalıştırılır. API açıklamaları header'larda,
paket şemaları ilgili fonksiyonların yanında bulunur. Adaptör kaynakları
uygulamanın Doxygen INPUT listesine ayrıca eklenebilir.
Davranış referansı [MZ Automation lib60870](https://github.com/mz-automation/lib60870)
ve kardeş `iec104` deposundaki `docs/IEC60870-5-104_Referans.md` belgesidir.
Upstream kaynak kodu kopyalanmamıştır; test bağımlılığının lisansı kendi paketindedir.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 25.10.2026 | Bağımsız C11 core/server ve test altyapısı |
| 30.10.2026 | Ayrı modem adaptörü, tipli noktalar/host zaman alanları ve uygulama GI kabul callback'i |
| 30.10.2026 | Tek GI handler + IOA iterator/okuyucu; callback typedef'leri, extension API ve test edilen örnek kataloğu |
| 30.10.2026 | Ayrı GI/IOA callback'leri kaldırıldı; tek handle_request ve hazır cevap parçası modeli |
| 30.10.2026 | Ayrı periyodik COT=1 yayın girişi ve opsiyonel Type 45 reset SBO/iptal akışı |
| 31.10.2026 | Referans karşılaştırması: ACK/STOP/T3 sınırları düzeltildi; Type 107 protokol testi eklendi |
