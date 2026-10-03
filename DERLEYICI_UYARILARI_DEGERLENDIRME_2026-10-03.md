# Derleyici uyarıları değerlendirmesi ve düzeltme sırası

**Sürüm:** 1.8
**Tarih:** 2026-10-03
**İnceleme tabanı:** `ad40b75`
**Durum:** Kullanıcı kararıyla düşük riskli düzeltmeler uygulandı. Contiki kernel kapsam dışıdır; davranış ve veri modeli incelemesi isteyen işler ertelenmiştir. İlk düzeltmeler bölüm 10’da, yeniden değerlendirme bölüm 11’de, onaylanan JSON/Modbus düzeltmelerinin güncel sonucu bölüm 12’dedir.

## 1. Amaç

Derleyici uyarılarının hangilerinin gerçek davranış hatasına işaret
ettiğini ayırmak ve yeterli olan en küçük düzeltmeyi belirlemek.

**Kapsam:** Güncel ARM Release derlemesinin bütün uyarıları; Release'te
derlenen 110 Application C dosyasının zorunlu uyarı seçenekleriyle
kontrolü; özel çıktı fonksiyonlarına yönelik ek format denemesi.

**Kullanım yeri:** Üretim hazırlığı çalışmasının derleyici uyarıları maddesi.
Shell oturum/ISR tasarımı kullanıcı kararıyla ertelenmiştir. Bu belgede
shell için görülen tip/format uyarıları listelenir; oturum tasarımı açılmaz.
Dummy üreticiler ve `#if 0` blokları korunur.

## 2. İlk ölçüm ve sınırlar

| Kontrol | Düzeltme öncesindeki sonuç |
|---|---|
| `make -C Release -B -j8 main-build` | Çıkış 0; 34 uyarı |
| ARM syntax-only kontrolü | 110 dosya; 72 geçti, 38 geçmedi; 1.370 tanı mesajı |
| ARM `-Os` nesne derlemesiyle sıkı kontrol | 110 dosya; 72 geçti, 38 geçmedi; 1.378 tanı mesajı |
| Tekrarları ayırılmış sıkı tanılar | 1.370 farklı dosya/satır/sütun/mesaj kaydı |
| Ek format denemesi | 16 kaynak dosyasında 210 farklı format tanısı |
| Mevcut xsnprintf/xscanf Ceedling paketleri | 34 test geçti; scanner'ın iç paketi ayrıca 40/40 geçti |

Sıkı kontrolde mevcut Release seçeneklerine `-Wextra -Werror -Wshadow
-Wconversion -Wdouble-promotion -Wformat=2` eklenmiştir. `-Wall` zaten
vardır. Optimizasyon kontrolünde her dosya ayrı bir geçici nesneye derlenir;
üretim nesnelerinin yerine konmaz. Syntax-only kontrolü optimizasyona bağlı
bütün uyarıları yakalamadığı için ikinci kontrol de yapılmıştır.

Bir satır birkaç dönüşüm uyarısı üretebilir. Bir header birkaç kaynak
dosyasında yeniden tanı üretir. Bu sayılar ayrı cihaz arızası sayısı değildir.
Her tanının konumu ve yapılacak kontrol ek envanterde bulunur:
[Uyarı envanteri](DERLEYICI_UYARILARI_ENVANTER_2026-10-03.md).

Core, vendor ve Contiki kaynakları normal Release derlemesine dahildir;
110 dosyalık sıkı tarama yalnız Application kaynak birimlerini kapsar.
Application tarafından genişletilen LL header makroları da bu taramada
tanı üretebilir. Debug, başka özellik kombinasyonları ve cihaz davranışı
bu incelemeyle doğrulanmış değildir. Paket üretimi/flash işlemi yapılmadı.

Uyarıların anlamı için [GCC Warning Options](https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)
kullanılmıştır. Bir uyarı tek başına çalışma hatasının kanıtı değildir.
Karar için bu depodaki kaynak akışı ve mevcut korumalar esas alınmıştır.

## 3. Önce düzeltilmesi önerilen gerçek sorunlar

### 3.1 IEC CommonAddr ve OriginatorAddr alanları

**Kanıt:** `Application/types.h` içinde kalıcı `common_address` 16 bit,
`originator_address` 8 bittir. `json_config.h:63-64` içinde bu genişlikler
tersidir. `http_handlers.c:1055` mevcut 16 bit adresi 8 bit JSON alanına
kopyalar. POST önce mevcut config'i bu ara yapıya taşır, sonra kısmi JSON
değişikliğini uygular ve `set_iec_config()` ile geri yazar.

**Gerçek etki:** Mevcut CommonAddr 300 ise ara yapıda 44 olur. Kullanıcı
yalnız başka bir alanı güncellediğinde adres de 44 olarak kaydedilebilir.
JSON parser CommonAddr için `parse_uint8()` kullanır; 256 üzeri değerler
doğrudan girilemez. OriginatorAddr ise `parse_uint16()` ile alınır ve
`json_config.c:2177` üzerinde 8 bite daraltılır. 256 değeri 0 olur; bu
alan için 255 üst sınır kontrolü görülmedi. Bu bir stil uyarısı değildir.

**Öneri:** Yalnız JSON ara yapısını ve ilgili parser/validator imzalarını
mevcut NVRAM genişlikleriyle eşleştirmek. CommonAddr için 16 bit,
OriginatorAddr için 8 bit kullanılmalıdır. NVRAM layout/schema ve wire
alanı değiştirilmemelidir. 255/256/300/65535 sınırları ve sadece başka
alanın değiştiği kısmi POST test edilmelidir. Mevcut 255 broadcast yorumu,
16 bit common address alanıyla birlikte ayrıca gözden geçirilmelidir.

### 3.2 Modbus adreslerinin kontrolsüz daraltılması

**Kanıt:** `json_config.c:1108-1202` Modbus hat adreslerini uint32 array
olarak ayrıştırır. `parse_modbus_config_internal()` yalnız cihaz ID ve
baud rate doğrular. `set_modbus_configs()` içinde `2273-2293` adresler
16 bit alanlara atanır; arada 65535 üst sınır denetimi bulunamadı.

**Gerçek etki:** JSON'dan 65536 verilen adres 0 olur; yanlış register
eşlemesi kaydedilebilir. Bu davranış kaynak akışından doğrulanmıştır;
cihaz üzerinde register okuma testi yapılmamıştır.

**Öneri:** Mevcut JSON doğrulamasına kullanılan hatlar için adres sınır
kontrolü eklemek; sınır dışı isteği kayıttan önce reddetmek. Yeni config
katmanı gerekmez. 65535 kabulü, 65536 reddi ve reddedilen istekte eski
config'in korunması test edilmelidir.

### 3.3 SCADA IP oluştururken signed shift

**Kanıt:** `json_config.c:2150-2164` octet'leri `int` olarak toplar ve
ilkini 24 bit sola kaydırır. Geçerli `192.168.1.1` için `192 << 24`,
32 bit signed int'in temsil sınırını aşar. Bu, C açısından undefined
behavior (tanımsız davranış) içerir. Önceki `validate_ip_address()`
octet aralığını doğrular ama ifadenin signed tipini değiştirmez.

**Öneri:** Kaydırmadan önce octet'leri uint32'ye çevirmek. IP doğrulama
altyapısı zaten vardır; yeni parser kurmak gerekmez. İlk octet için
127/128/192/255 sınırları kontrol edilmelidir. STM32'de bugün yanlış IP
çıktığı gösterilmemiştir; sorun dil seviyesindeki güvencesizliktir.

### 3.4 Reset nedeni register'ı

**Kanıt:** `reset_source.c:50-54` ham RCC durumunu 32 bit olarak tutar.
`modbus_system_stats.c:124` bunu tek 16 bit register'a atar. Üst 16 bit
kaybolur. `reset_source_get_flags()` ayrıca mevcut normalleştirilmiş
bayrakları sunmaktadır; yeni bayrak altyapısı gerekmez.

**Öneri:** Önce yayımlanmış Modbus register sözleşmesinde bu alanın ham
RCC mi, normalleştirilmiş reset nedeni mi olduğuna bakılmalıdır. Tek
register'da mevcut normalize bayrakları kullanmak en küçük aday çözümdür.
Ham RCC gerekliyse iki register gerekir. Bu seçim protokol davranışını
etkiler; kullanıcı kararı alınmadan uygulanmamalıdır. Yalnız cast eklemek
kaybolan bilgiyi geri getirmez.

## 4. Risk var, fakat cihaz arızası kanıtlanmadı

### 4.1 GSM packed IP alanına pointer verme

Normal derlemedeki üç uyarı `gsm_engine.c:910,991,1227` üzerindedir.
`ip_addr_t` packed/aligned(1) union'dır. `ipv4_to_int()` normal uint32
pointer'ına yazmaktadır (`gsm/utils.c:414`). Çağrı üç listener bağlantı
cevabını işlerken erişilebilirdir.

Union'ın hizalama garantisi zayıftır; bu, söz konusu üç runtime adresinin
kesin hizasız olduğunu veya cihazın kesin HardFault verdiğini göstermez.

**Öneri:** Her çağrıda hizalı yerel uint32'ye parse etmek; parse başarılıysa
packed üyeye normal alan ataması yapmak. Yapıları unpack etmek, ABI veya
NVRAM değiştirmek gerekmez. Parse başarısızlığında eski IP'nin korunması
ve başarıda doğru IP/bağlantı kaydı test edilmelidir.

### 4.2 GSM uzunluk ve pointer hesabı

`gsm_engine.c:1977` iki pointer'ın farkını uint16'ya çevirir. Marker'ların
bulunması kontrol edilir, fakat `ok_ptr` ile `hash_ptr` sıralaması ayrı
denetlenmez. Bozuk cevapta negatif fark sayaç hesabına büyük pozitif
değer olarak yansıyabilir. Şu an ilgili veri iletim çağrıları yorumdadır;
bu satırdan aktif buffer taşması olduğu iddia edilmez.

`gsm/utils.c` delimiter, uzunluk ve indeks hesaplarında signed tipler
kullanır. Örneğin `str_substr()` zaten `len >= 0 && len < max_len`
kontrolüyle memcpy'yi korur. Bu mevcut koruma kaldırılmamalıdır.

**Öneri:** İlgili callback'in kısa/bozuk cevapları ve ters marker sırası
test edilmelidir. Sıra doğrulandıktan sonra fark daraltılmalıdır. Diğer
indekslerde de negatif sentinel ile boyut birbirinden ayrılmalıdır.
Uyarı sayısını azaltmak için bütün sonuçlara cast konmamalıdır.

### 4.3 SBO timeout ve bit alanları

`json_config.c:2175` 32 bit SBO timeout'u 16 bite aktarır. SBO açıkken
parser 1..300 denetimi yapar; bu yolda daraltma güvenlidir. SBO kapalıyken
bu denetim çalışmaz; çok büyük değer kalıcı alanda kırpılabilir.
En küçük çözüm açık/kapalı fark etmeksizin saklama sınırını denetlemektir.

IEC sequence, COT, tek bit durumlar ve tarih bit alanlarındaki uyarılar
ayrı değerlendirilmelidir. Batch uzunluğu için `iec104.c:1979,2014-2019`
zaten 253 bayt üst sınırını kullanır; burada taşma kanıtı yoktur.
Sequence sarımı ve durum alanlarına gelen değerler için mevcut üretici
sınırları kontrol edilmeli; geçerli aralık doğrulandıktan sonra maske veya
cast kullanılmalıdır. Tek bit alanına keyfi maske eklemek, geçersiz 2
değerini sessizce 0'a çevirebilir; boolean davranış isteniyorsa `!= 0`
ile açıkça normalleştirilmelidir.

## 5. Gerçek taşma veya eksik altyapı sayılmayan uyarılar

### 5.1 HTTP ve diğer çıktı uzunlukları

HTTP handler'daki 887 sıkı tanının büyük kısmı `int pos`, unsigned buffer
boyutu ve unsigned `xsnprintf()` dönüşünün birlikte kullanılmasındandır.
`xprintf.c:477-512` her çağrıda en çok `len-1` yazılmış bayt döndürür,
NUL için yer ayırır ve sıfır kapasitede 0 döndürür. Bu, standart snprintf
dönüşünden farklıdır; yeni bir writer katmanı eklemek gerekmez.

Mevcut canary (buffer dışını izleyen işaret) ve birikimli uzunluk testleri
geçmiştir. Uyarılardan doğrudan HTTP buffer taşması sonucu çıkarılamaz.
Buffer dolduğunda kesilmiş JSON üretilmesi ayrı davranıştır; uzun config
cevaplarında bu durum ayrıca test edilmelidir.

**Öneri:** Yerel pos/uzunluk tiplerini kullanılan API ile uyumlu yapmak;
signed HTTP gönderim sınırına geçerken mevcut buffer sınırını belgelemek
ve kontrollü dönüşüm yapmak. Bütün HTTP API'lerini bir anda değiştirmek
veya formatter'ı yeniden yazmak gerekli değildir.

### 5.2 xscanf başlatılmamış değer uyarısı

`xscanf.c:548-552` hex sonucu için uyarı verir. `parse_hex()` başarı
yolunda `*out = val` atar; atama yapmadan çıkan yollar err değerini
başarısız yapar. Çağıran çıktı değerini yalnız `XSCANF_OK` durumunda okur.
Bu nedenle bildirilen başlatılmamış okuma mevcut akışta doğrulanmamıştır.
Üretim GSM çağrıları ayrıca `%u8/%u32` kullanır; bu uyarı `%x` kolundadır.

**Öneri:** Yerel değeri 0 ile başlatmak ve hex başarılı/boş/geçersiz input
testlerini eklemek yeterlidir. Parser veya kurtarma akışı değişmemelidir.

### 5.3 Zaman sabitlerinin yeniden tanımı

`datetime.h:23-27` unsigned, `utils.h:36-40` signed yazımlı aynı sabitleri
tanımlar. Sayısal sonuçlar aynıdır; iki ayrı başlıkta tekrar edilmesi 16
normal uyarı oluşturur. Include sırası tip davranışını etkileyebilir.

**Öneri:** Mevcut datetime header'ı ortak kaynak yapmak ve utils tarafında
onu kullanmak. Birkaç sabit için yeni modül gerekmez. 30 günlük ay ve
360 günlük yıl hesabı bu uyarı düzeltmesi kapsamında değiştirilmemelidir.

### 5.4 Kullanılmayan fonksiyon, değişken ve callback parametreleri

Normal derlemede JSON'un beş private helper'ı, AT'nin `send_at_command()`
fonksiyonu, GSM `rat_str()`, GSM `index/apn`, BMS `payload_len/huart5`
uyarı üretir. Bunlar tek başına eksik ürün özelliğinin kanıtı değildir.
Örneğin aktif RF ZoneID yolu `json_config.c:1308` mevcut 0..7 validator'ını
kullanır; eski `validate_zone_id()` helper'ını sırf uyarı için bağlamak
yanlış sınır kuralını devreye sokabilir.

**Öneri:** Kullanılmayan aktif private helper/declaration'lar kaldırılabilir;
ileride saklanması istenen kod kullanıldığı feature guard altında tutulabilir.
`#if 0` bloklarına dokunulmamalıdır. Framework/HAL imzasının gerektirdiği
kullanılmayan parametrelerde `(void)param` yeterlidir; API değişmemelidir.
BMS payload minimum sınırları zaten vardır; kullanılmayan payload_len
uyarısı, minimum uzunluk kontrolünün bulunmadığını göstermez.

### 5.5 Kullanılmayan Contiki rtimer portu

`rtimer.c:66,71` weak init/schedule stub'ları açık `#warning` üretir.
Application/Core taramasında `rtimer_set()` veya `rtimer_init()` çağrısı
bulunmadı. Çalışan etimer/timer zinciri için eksik port olduğu gösterilmedi.

**Öneri:** Port uyarısını rtimer özelliği açıkken üretilecek şekilde
sınırlamak veya kullanılmayan modülü build kapsamından çıkarmak. Timer
donanımı kurmak gereksizdir. Build kapsamı seçimi uygulamadan önce
sunulmalıdır; kullanılacaksa gerçek port gereksinimi ayrıca kanıtlanmalıdır.

## 6. Format ve float uyarıları

`xprintf.h` fonksiyonlarında GCC `format` attribute'u yoktur. Bu yüzden
normal `-Wformat=2` kontrolünün sessiz kalması bütün logların doğru olduğu
anlamına gelmez. Geçici forced-include header ile üretim kaynakları
değiştirilmeden format denemesi yapılmış ve 210 tanı elde edilmiştir.

208 tanı argüman tipi/format eşleşmesi üzerindedir. Örneğin uint32 ARM
toolchain'de unsigned long iken `%u` kullanılır. Bu ABI'de int/long aynı
genişlikte olduğundan bütün bu satırların sahada bozuk çıktı ürettiği
iddia edilmez; formatter'ın `va_arg` tipiyle çağrı tipi uyumsuzdur.
`gsm_init.c:1011` kullanılmayan ekstra log argümanı;
`xmodem_process.c:203` `%00000X` tekrarlı zero flag içerir.

Ek bir gerçek formatter uyumsuzluğu vardır: `json_config.c:207,414`
üzerindeki üç `%zu` dönüşü GCC printf sözleşmesine uygun olsa da mevcut
`xprintf.c:311-372` `z` prefix'ini desteklemez; `zu` metni üretir.
Bu, format attribute'unun tek başına yeterli olmadığını gösterir.

**Öneri:** Desteklenen formatları koruyarak türleri `%lu/%ld/%lX` ve
açık argüman tipiyle eşleştirmek; `%zu` çağrılarını mevcut formatter'ın
desteklediği türe çevirmek. `%00000X` yerine gerçekten istenen genişlik
belirlenmelidir. Formatter'a yeni prefix desteği eklemek bu birkaç çağrı
için gerekli değildir. Ardından GCC format attribute'u eklenmesiyle yeni
uyumsuzlukların derlemede görülmesi sağlanabilir; `%b` gibi yerel uzantılar
ve bütün aktif çağrılar kontrol edilmeden attribute eklenmemelidir.

21 double-promotion uyarısının çoğu float'ın variadic (değişken argümanlı)
log çağrısında double'a dönüşmesidir. `xprintf.c:355` zaten double okur.
Bu otomatik dönüşüm gereklidir; precision kaybı veya yeni hesaplama hatası
olarak sınıflandırılmamalıdır. En küçük düzeltme log sınırında açık double
cast'idir. Float API'si veya telemetri veri modeli değiştirilmemelidir.

## 7. Yapılacak iş sırası ve kabul ölçütleri

| Sıra | İş | Neden önce / kabul ölçütü |
|---|---|---|
| 1 | IEC CommonAddr/OriginatorAddr ve Modbus JSON sınırları | Yanlış config kaydı; sınırlar ve kısmi POST eski değeri korumalı |
| 2 | SCADA unsigned IP oluşturma | Geçerli yüksek ilk octet için signed shift kaldırılmalı |
| 3 | Modbus reset_reason sözleşmesi | Ham veri kaybı; mevcut protokol belgesi ve kullanıcı kararıyla sonuç netleşmeli |
| 4 | Üç packed IP pointer'ı ve GSM marker uzunluğu | Yerel hizalı değer; başarısız parse/ters marker güvenli kalmalı |
| 5 | Makro tekrarları, unused öğeler ve xscanf başlangıç değeri | Düşük etkili temizlik; normal 34 uyarı giderilmeli |
| 6 | HTTP uzunluk tipleri ve format çağrıları | Mevcut sınırlar korunmalı; canary testleri ve büyük JSON çıktısı kontrol edilmeli |
| 7 | IEC/BSP bit alanları, GSM sonuç tipleri, driver byte dönüşümleri | Üretici aralıkları kanıtlanmalı; wire ve depolama değişmemeli |
| 8 | LL ADC vendor makrosu ve build politikası | Vendor dosyası değiştirilmemeli; yalnız dar kapsamlı sınır çözümü seçilmeli |
| 9 | Güncel ARM normal/sıkı derleme ve ilgili testler | Module testleri ve format kontrolü geçmeli; warning-free sonucu yeniden ölçülmeli |

**Kurallar:** Uyarılar global olarak kapatılmamalıdır. Dönüşümden önce
değer aralığı kanıtlanmalıdır. Modül değişiklikleri küçük gruplar halinde
yapılmalı ve ilgili Ceedling testleri çalıştırılmalıdır. Genel isim
değiştirme veya geniş refactor yapılmamalıdır. Shell oturum tasarımı,
dummy üreticiler ve kapalı kodlar bu çalışmanın dışında tutulmalıdır.

LL ADC tanısı mevcut vendor macro genişlemesinde oluşur
(`stm32u3xx_ll_adc.h:2767`). Elle vendor düzeltmesi önerilmez. Macro'nun
signed ara sonuçları ve sıcaklık API sınırı doğrulanmalı; gerekiyorsa
yalnız o genişlemeye ait dar bir diagnostic sınırı ayrıca değerlendirilmelidir.
Bu öneri bütün warning politikasını gevşetme kararı değildir.

Henüz bütün dönüşüm noktalarının giriş aralıkları host testleriyle
kanıtlanmamıştır. Ek envanterde bu satırlar açıkça sınır testi isteyen
adaylar olarak ayrılır. Hiçbirine yalnız uyarı sayısından kritik arıza
etiketi verilmez. Gerçek cihaz/ISR/hizalama ve güç kesme kabulü ayrıdır.

## 8. Kanıt dosyaları

Yerel ölçüm çıktıları `test/build/production-audit-2026-10-03/` altındadır:

- `warnings-current-release.log`: güncel baştan ARM derlemesi.
- `warnings-current-strict.json/.log`: syntax-only tanıları.
- `warnings-current-optimized.json/.log`: optimizasyonlu sıkı tanılar.
- `warnings-inventory.json`: tekrarları ayrılmış konum ve kaynak bağlamı.
- `warnings-format-probe.json/.log`: geçici format attribute denemesi.
- `warnings-format-inventory.json`: 210 format tanısının konumları.
- `warnings-library-tests.log`: iki mevcut kütüphane test paketinin sonucu.

Bu loglar Git dışında yerel build çıktılarıdır. Kalıcı rapor ve bütün
tanı konumları ek envanterde tutulmuştur. İlk ölçüm logları düzeltme öncesini gösterir. Güncel düzeltme ve doğrulama
sonuçları bölüm 10’dadır. `.cproject`, NVRAM yerleşimi ve protokol veri
yapıları değiştirilmemiştir; commit yapılmamıştır.

## 9. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-03 | 1.0 | Güncel ölçüm, tekil uyarı envanteri ve düzeltme planı |
| 2026-10-03 | 1.1 | Düşük riskli düzeltmeler, kapsam kararı ve doğrulama sonuçları |
| 2026-10-03 | 1.2 | Ertelenen maddelerin kaynak/test kanıtı, uygulanan işler ve karar bekleyen öneriler |
| 2026-10-03 | 1.3 | Onaylanan JSON/Modbus düzeltmeleri, aktif Ceedling testleri, web eşleştirmesi ve doğrulama |

## 10. Uygulanan düzeltmeler ve ertelenen işler

**Kullanıcı kararı:** Contiki kernel değiştirilmemelidir. Riskli alanlar
sonraya bırakılmalı; kalan düşük riskli işler düzeltilmelidir. Bölüm 7’deki
ilk öneri sırası bu karar doğrultusunda uygulanmıştır.

### 10.1 Yapılan işler

| Yer | Değişiklik ve gerekçe |
|---|---|
| `Application/utils.h` | Tekrarlanan zaman sabitleri kaldırıldı; mevcut `libs/datetime.h` ortak kaynak olarak kullanıldı. Sayısal süreler aynı kaldı. |
| `Application/web-server/json_config.c` | Kullanılmayan beş private yardımcı kaldırıldı. Formatter’ın desteklemediği `%zu` yerine açık `unsigned long` argümanıyla `%lu` kullanıldı. |
| GSM, BMS ve callback fonksiyonları | Kullanılmayan aktif yardımcılar ve değişkenler kaldırıldı; zorunlu callback parametrelerinin kullanılmadığı açıkça belirtildi. GSM sayaç getter’ındaki parametre gölgelemesi giderildi. |
| HTTP, log, shell, NVRAM dump ve xmodem çıktı çağrıları | 32 bit değerlerin formatları ARM toolchain tipleriyle eşleştirildi; float log argümanlarındaki mevcut double dönüşümü açık yazıldı. Hesaplama ve kayıt yapıları değiştirilmedi. |
| `Application/libs/w25qxx.c` | Özel formatter’da desteklenmeyen `%p`, mevcut hex formatıyla pointer adresi yazacak şekilde düzeltildi. Flash işlemleri değiştirilmedi. |
| `Application/libs/xscanf.c` | Hex okumanın yerel sonucu başlangıçta sıfırlandı. Mevcut hata kontrolü korundu; hatalı girdinin hedefi değiştirmediği test edildi. |
| `test/libs/test_xscanf_format_scenario.c` | En büyük 32 bit hex değer, sıfır, hatalı ve boş girdi için iki Ceedling testi eklendi. |
| `test/libs/test_xsnprintf_format_scenario.c` | `%lu`, `%ld`, `%08lX` ile 32 bit sınır değerlerini ve hex padding çıktısını doğrulayan Ceedling testi eklendi. |

Dummy üreticiler korunmuştur. Değişen C/H dosyalarında Git tabanındaki
bütün `#if 0` blokları, iç içe preprocessor blokları da dikkate alınarak
karşılaştırılmış ve değişmeden kaldığı doğrulanmıştır. Core, vendor ve
Contiki kernel kaynakları değiştirilmemiştir.

### 10.2 Doğrulama

| Kontrol | Sonuç |
|---|---|
| Baştan ARM Release derlemesi | Başarılı; 34 yerine 5 uyarı |
| Kalan normal derleme uyarıları | Contiki kernel’de kapsam dışındaki 2 uyarı; GSM’de ertelenen 3 packed pointer uyarısı |
| Son değişikliklerden sonra ARM Release derlemesi | Başarılı |
| Ek format kontrolü | 110 Application kaynak birimi geçti; format tanısı kalmadı |
| Bütün Ceedling testleri | 305/305 geçti; yeni testler mevcut altyapı ve Git kaynakları içindedir |
| İlgili iki formatter/scanner paketi | 38/38 geçti; scanner iç testleri ayrıca 40/40 geçti |
| Mevcut entegrasyon paketleri | 9/9 geçti |
| Sıkı syntax-only kontrolü | 110 birimden 81’i geçti; 29 birimde 1.301 tanı mesajı kaldı |
| `git diff --check` | Geçti |

Sıkı taramadaki 1.301 tanı mesajı ayrı cihaz hatası sayısı değildir.
Normal derleme ve ek format kontrolü ile farklı seçeneklerle yapılan
sıkı tip taraması birbirine karıştırılmamalıdır. Proje bütün zorunlu
uyarı seçenekleri altında henüz warning-free değildir.

Yeni kanıt çıktıları yerel `test/build/production-audit-2026-10-03/`
altında `warnings-low-risk-*` adlarıyla tutulur. Bu loglar build çıktısıdır;
regression (gerileme) testlerinin C kaynakları `test/libs/` altında repodadır.

### 10.3 Sonraya bırakılanlar

- IEC CommonAddr/OriginatorAddr JSON alan genişlikleri ve Modbus değer
  sınırları: bölüm 3’teki config kaydı sorunları hâlâ açıktır.
- SCADA IP oluşturmadaki signed shift ve Modbus reset_reason sözleşmesi:
  bölüm 3’teki öneriler uygulanmamıştır.
- GSM packed IP pointer’ları: güncel konumlar `gsm_engine.c:913`,
  `gsm_engine.c:994`, `gsm_engine.c:1230`. Veri hizalaması ayrıca
  doğrulanmalıdır. GSM marker uzunluğu da sonraki incelemede kalmıştır.
- HTTP buffer konumlarının tipleri, SBO sınırı, IEC/BSP bit alanları,
  GSM/driver daraltmaları ve LL ADC vendor macro tanısı: giriş aralığı ve
  ilgili protokol/driver etkisi kanıtlanmadan cast eklenmemelidir.
- Formatter’a yeni format desteği veya kalıcı GCC format attribute
  eklenmesi bu değişikliğe dahil değildir.

Cihaza firmware yüklenmemiştir. Donanım kabul testi, Debug derlemesi ve
başka özellik kombinasyonları bu sonuçla doğrulanmış sayılmamalıdır.

## 11. Ertelenen maddelerin yeniden değerlendirmesi

**Kullanım yeri:** Kullanıcının ertelenen maddelerde gerekli düzeltmeleri
uygulama talebi. Contiki kernel, dummy üreticiler ve `#if 0` blokları kapsam
dışında kalmıştır. Bu bölüm onay öncesindeki değerlendirmeyi kaydeder. JSON veri modeli ve
Modbus protokol anlamı kullanıcı tarafından onaylanmıştır; uygulanan
sonuçlar bölüm 12’dedir.

### 11.1 Uygulananlar

| Yer | Kanıt | En küçük düzeltme |
|---|---|---|
| `json_config.c`, `set_iec_config()` | Signed `octets[0] << 24`, ilk octet 128 ve üstünde signed 32 bit sınırını aşar. Parser mevcut IP doğrulamasını kullanır; getter/setter IP’si 32 bittir. | Octet ve biriktirme aritmetiği unsigned yapıldı. IP byte sırası değişmedi. `192.168.1.2` değeri gerçek setter üzerinden `0xC0A80102` olarak doğrulandı. |
| `gsm_engine.c`, üç listener callback’i | `ipv4_to_int()` normal `uint32_t *` alıp `*ip_int` yazar; çağrılan union `packed, aligned(1)` türündedir. Gerçek cihazda fault yaşandığı kanıtlanmış değildir. | Sonuç hizalı yerel `uint32_t` üzerinden alınıp packed üyeye atandı. Parser başarısız olursa önceki değer korunur. Tür/oturum yerleşimi değişmedi. |
| `gsm_engine.c`, yedi AT callback kontrolü | `strstr()` sonucu `ptr > 0` ile kontrol ediliyordu; bu pointer/integer sıralama karşılaştırması `-Wextra` tanısı üretir. İstenen koşul eşleşmenin bulunmasıdır. | Kontroller `NULL != ptr` yapıldı. Kapalı ve yorumdaki kodlar korundu. |

### 11.2 Gerçek hata olarak kapanmamış şüpheler

**GSM marker uzunluğu:** AT engine `response_buffer_len` sonunda NUL
terminator (sonlandırıcı) tutar (`at_engine2.c`, byte alma döngüsü) ve
getter aynı uzunluğu verir. `gsm_httprcv_cb()` son altı bayttan başlayarak
altı baytlık `\r\nOK\r\n` desenini arar. Bulunursa desen tamponun
sonundadır; çakışmayan `<<<` deseninin onun arkasında bulunması normal
buffer sözleşmesinde mümkün değildir. Fark en az 3, en çok buffer
uzunluğudur. Önceki rapordaki negatif fark şüphesi bu çağrı yolunda
kanıtlanmamıştır; yeni recovery (kurtarma) akışı eklenmemiştir.

Gerçek callback testleri, payload’ın 3 bayt sayılmasını, boş payload’ın
0 sayılmasını, eksik marker’ı, üç baytlık kısa cevabı ve sona eklenmiş
marker’ı doğrular. Bu callback’teki veri iletme satırları hâlâ yorumdadır;
aktif etki sayaç güncellemesidir.

**HTTP buffer uzunlukları:** `xsnprintf()` mevcut kapasiteyi aşmayan yazılan
uzunluğu döndürür. Önceki canary ve birikimli konum testleri korunmuştur.
Signed/unsigned tanıları tek başına taşma kanıtı değildir. Bir taşma veya
kırpılan cevabın kullanılabilirlik sorunu yeniden üretilmeden bütün HTTP
API’lerini değiştiren bir çalışma yapılmamıştır.

**IEC/BSP bit alanları ve driver daraltmaları:** Batch uzunluğundaki
mevcut 253 bayt sınırı, `str_substr()` uzunluk koruması ve unsigned
register word parçalama gibi mevcut korumalar dikkate alınmalıdır.
Bu gruplardaki her dönüşümün bütün üretici yolları bu çalışmada tamamen
kanıtlanmış değildir; topluca cast eklenmemiş ve kapanmış sayılmamıştır.
LL ADC vendor makrosu da değiştirilmemiştir.

### 11.3 Kod ve test önerisi hazır; karar bekleyenler

| Madde | Mevcut davranış / test kanıtı | Öneri |
|---|---|---|
| CommonAddr | `CommonAddr:300` gerçek parser’da reddediliyor; mevcut 16 bit config değeri HTTP ara yapısında 8 bite kesilebiliyor. | JSON CommonAddr ve parser 16 bit olmalı; kalıcı alan korunmalı. |
| OriginatorAddr | `OriginatorAddr:256` gerçek parser’da kabul ediliyor; kayıt alanı 8 bit. | JSON OriginatorAddr ve parser 8 bit olmalı. |
| SBO kapalıyken timeout | `SBO:false,SBOTimeout:65536` kabul ediliyor; kalıcı timeout 16 bit. | Açık/kapalı fark etmeksizin 16 bit saklama sınırı denetlenmeli; açıkken mevcut 1–300 kuralı korunmalı. |
| Modbus adresleri | Hat adresi 65536 parser’da kabul ediliyor. Setter’daki global adres 65536 de kayıt akışına giriyor. | Global adresler ve 21 hat adresi dizisi parser ve setter sınırında 65535 üstünü reddetmeli; hiçbir config yazımı başlamamalı. |
| Modbus 49009 reset_reason | Kaynak 32 bit ham CSR değerini 16 bite atıyor. Yerel STM32U375 CMSIS header’ında NRST=bit26, software=bit28, IWDG=bit29; bu nedenler kesiliyor. Belgede ham CSR olarak tanımlı. | Adresleri koruyup mevcut `reset_source_get_flags()` bitmask’ini yayınlamak ve register belgesini güncellemek. Protokol anlamı değiştiği için açık onay bekliyor. |

JSON için beş regression senaryosu mevcut kod üzerinde çalıştırılmış;
beklenen düzeltilmiş davranışın beşi de başarısız olmuştur. Bu, mevcut
sorunları yeniden üreten test kanıtıdır; uygulanan kodun test başarısızlığı
olarak raporlanmamalıdır. Testler öneri patch’inde tutulmuştur; onaydan
sonra üretim düzeltmesiyle birlikte aktif Ceedling paketine eklenmelidir.
Güncel aktif test paketi bu bekleyen senaryoları içermez.

İncelemeye hazır öneriler yerel kanıt dizinindedir:

- `deferred-json-proposed.patch`: JSON alan/sınır düzeltmesi ve beş test.
- `deferred-reset-proposed.patch`: 49009 bitmask çıktısı ve belge düzeltmesi.

Bu öneriler üretim dosyalarına uygulanmamıştır. NVRAM yerleşimi ve şema
sürümü değişmemiştir. Ek envanter düzeltme öncesinin konumlarını içerir.

### 11.4 Güncel doğrulama

| Kontrol | Sonuç |
|---|---|
| Baştan ARM Release derlemesi | Geçti; yalnız kapsam dışı iki Contiki kernel uyarısı kaldı |
| Ek format kontrolü | 110 Application kaynak birimi geçti |
| Güncel bütün Ceedling testleri | 312/312 geçti |
| GSM Ceedling paketleri | 54/54 geçti; altı yeni callback testi dahildir |
| Entegrasyon paketleri | 9/9 geçti |
| SCADA IP setter regression testi | Geçti; mevcut JSON Ceedling paketine eklendi |

GSM senaryosu gerçek `gsm_engine.c` ve `gsm/utils.c` kodlarını kullanır;
farklı bir parser kopyası içermez. Eski modülün kullanılmayan diğer
callback bağımlılıklarını düşürmek için yalnız bu host testinde LTO
kullanılır. Legacy kaynak ve CMock’un dizi/pointer imza üretimi için
uyarı istisnaları yalnız ilgili Ceedling senaryolarına eklenmiştir.
Üretim derleme uyarıları kapatılmamıştır.

Kanıt logları: `deferred-release.log`, `deferred-gsm-all.log`,
`deferred-json-reproduction.log`, `deferred-current-all-tests.log`.
Cihaza yükleme ve donanım kabul testi yapılmamıştır. Bu sonuç bütün sıkı
uyarı seçenekleri altında warning-free olduğu anlamına gelmez.

## 12. Onaylanan JSON ve Modbus düzeltmelerinin sonucu

**Karar:** Kullanıcı iki öneriyi de “düzeltelim” yanıtıyla onaylamıştır.
Bölüm 11’de karar bekleyen JSON ve 49009 işleri artık uygulanmıştır.

### 12.1 Yapılan işler

- JSON `CommonAddr` 16 bit, `OriginatorAddr` 8 bit yapıldı; mevcut
  parser’ın aralık kontrolü kullanıldı. JSON key adları aynı kaldı.
- SBO timeout açık/kapalı fark etmeksizin 65535 üstünde reddedilir.
  SBO açıkken mevcut 1–300 saniye kuralı korunur. Setter da saklama
  sınırını config yazımı başlamadan kontrol eder.
- Modbus’un iki global adresi ve 21 hat adresi dizisi mevcut 16 bit
  kalıcı alan sınırına göre doğrulanır. Parser ve setter aynı private
  kontrolü kullanır. Sınır dışı girişte config setter/sync çağrısı başlamaz.
  Doğrulanmış değerlerin daraltılması açık yazılmıştır.
- Modbus 49009 aynı UINT16 register adresinde mevcut
  `reset_source_get_flags()` bitmask’ini verir. Register haritası sürüm
  1.7’de maskeler ve örnek birlikte açıklanmıştır. Ham CSR snapshot’ı
  `reset_source_get_raw()` üzerinden diğer mevcut kullanımlar için kalır.
- Modbus 49010 sıcaklık değerinin mevcut 16 bit word kodlaması açık cast
  ile yazılmış ve negatif sıcaklık regression testiyle korunmuştur.
- Web arayüzünün CommonAddr/OriginatorAddr sınırları backend ile
  eşleştirilmiştir. SBO etiketindeki “ms” mevcut saniye sözleşmesine göre
  “s” yapılmış; açıkken 1–300, kapalıyken 0–65535 doğrulanmıştır.
  `types.h` alan açıklaması ve IEC varsayılanı saniyedir; yeni zaman
  birimi/protokol davranışı tanımlanmamıştır. Mock server örneği 30 saniyeye
  uyarlanmıştır; firmware dummy üreticileri korunmuştur.
- Gömülü HTML mevcut generator ile yeniden üretilmiştir. Generator’ın C/H
  header çıktıları da zorunlu `Author: Fatih Ozcan` ve e-posta biçimini
  korur. Firmware update sayfasının yalnız header metaverisi yenilenmiştir.

NVRAM şeması, kalıcı alan genişlikleri, register adresleri ve Contiki
kernel değişmemiştir. Bütün mevcut `#if 0` blokları korunmuştur.

### 12.2 Repodaki aktif testler

| Ceedling dosyası | Test sayısı | Doğrulanan davranış |
|---|---|---|
| `test/web_server/test_json_config_bounds.c` | 13 | CommonAddr 300/65535, OriginatorAddr 255/256, kısmi güncellemenin korunması, SBO açık/kapalı sınırları, 21 Modbus dizi key’inin 65535/65536 sınırı, geçersiz girdide yazım başlamaması, geçerli değerin kayıtta korunması, SCADA IP |
| `test/application/test_modbus_system_stats.c` | 5 | IWDG ve birleşik bitmask, bilinmeyen neden, komşu register adreslerinin korunması, negatif sıcaklığın word kodlaması |

Bu dosyalar `test/project.yml` içindeki mevcut arama yollarına dahildir;
`ceedling test:all` ile çalışır. Onay öncesindeki beş başarısız senaryo da
artık aktif JSON paketinin parçasıdır ve geçer. Ayrı bir test altyapısı
kurulmamıştır.

Web doğrulaması mevcut `test/integration/web_navigation/test_navigation.js`
paketine eklenmiştir. Kaynak HTML ve firmware’e gömülü gzip çıktısı aynı
sınırlarla ayrı ayrı kontrol edilmiştir.

### 12.3 Doğrulama ve sınırlar

| Kontrol | Sonuç |
|---|---|
| Bütün Ceedling testleri | 329/329 geçti |
| Entegrasyon paketleri | 9/9 geçti; web kaynak/gömülü sınır testleri HTML yenilendikten sonra ayrıca geçti |
| Baştan ARM Release derlemesi | Geçti; yalnız kapsam dışındaki iki Contiki kernel uyarısı |
| Gömülü HTML güncellemesinden sonra Release derlemesi | Geçti |
| Ek format kontrolü | 110 Application kaynak birimi geçti |
| `git diff --check` | Geçti |

Ceedling’in birden fazla `test:...` task’ı verilen çalışmalardaki özetinde
ilk paketi yeniden sayabilmesi nedeniyle paket sayıları her `.pass`
sonuç dosyasından doğrulanmıştır: JSON 13, Modbus system stats 5.
329 toplamı tek `test:all` çalışmasının sonucudur.

Yerel kanıtlar `test/build/production-audit-2026-10-03/` altındadır:
`approved-config-tests.log`, `approved-all-tests.log`,
`approved-release.log`, `approved-web-release.log`, `approved-format.*`.
Öneri patch’leri yalnız onay öncesinin kaydıdır; yeniden uygulanmamalıdır.

Cihaza yükleme ve donanım kabul testi yapılmamıştır. Bütün zorunlu sıkı
uyarı seçenekleri altındaki dönüşüm envanteri tamamlanmış sayılmaz;
bölüm 11.2’deki kalan inceleme sınırları geçerlidir.

Kod ve repodaki testler iki commit ile kaydedilmiştir:

- `6a0caa0`: çıktı formatları ve kullanılmayan bildirimler.
- `f8a8032`: JSON config sınırları, GSM/SCADA IP düzeltmeleri,
  reset nedeni register çıktısı, web uyumu ve davranış testleri.


## 13. Etkinleşen uyarılar ve HTTP uzunluk düzeltmesi

### Amaç ve kanıt

Kullanıcı Release için `-Wextra`, `-Wconversion`, `-Wdouble-promotion`
seçeneklerini etkinleştirdi. `-Wconversion` komutta iki kez bulunur;
`-Wshadow`, `-Wformat=2`, `-Werror` henüz görülmedi. Kullanıcının
`.cproject` ve language settings değişikliklerine müdahale edilmedi.

Baştan derlemede 1298 uyarı görüldü; 902 tanesi `http_handlers.c`
içindeydi. Çok sayıda tanı aynı `int pos` ile unsigned `xsnprintf()`
uzunluğu arasındaki dönüşümden doğar. Bunlar 1298 ayrı bug değildir.

`http_server.h` varsayılan tamponu 8192 byte olarak tanımlar;
`http_server.c` bu pozitif kapasiteyi handler başlangıcına verir.
`xsnprintf()` sıfır kapasitede yazmaz, diğer durumda dönüşünü kapasite
eksi 1 ile sınırlar. Böylece her eklemeden sonra konum kalan kapasiteyi
geçmez. Mevcut sözleşme bu dönüşüm uyarılarından buffer overflow
(tampon taşması) sonucu çıkarmayı desteklemez.

### Yapılan değişiklik ve davranış sınırı

Device/board/syslogs/IEC104/Modbus/discovery/RF monitor/fault records ve
firmware version/status handler'larının tampon konumu unsigned yapıldı.
Formatter imzası `unsigned int` olduğu için bu yerel tip kullanıldı.
Tampon kapasitesi mevcut pozitif int sınırından bir kez dönüştürülür;
gönderimde konum int'e açık dönüştürülür. Konum kapasiteden küçük kaldığı
için bu sınırdaki dönüşüm kayıpsızdır. Log formatları tipe uyarlandı.

Alan değerleri, JSON formatı, kayıt sırası, protokol, NVRAM ve HTTP API
imzaları değiştirilmedi. Shell yakalama, RF config builder ve firmware
upload uzunluklarının farklı sözleşmeleri bu toplu değişikliğe katılmadı.
Bu düzeltme tip sözleşmesini açık yapar; önceden kanıtlanmış bir taşmayı
kapatıyor olarak sunulmaz. Tampon dolduğunda yanıtın kırpılması mevcut
davranıştır; bu çalışma kırpılmış JSON'u kullanılabilir hâle getirmez.

### Doğrulama

- Ceedling: **340/340 geçti**. `test_xsnprintf_format_scenario.c` içinde
  1/2/32/8192 byte kapasitelerde 500 ardışık ekleme, metin prefix'i,
  NUL sonlandırma, gönderimde int uzunluğu ve canary (koruma baytı)
  denetlendi. Gerçek formatter kullanılır.
- Mevcut web_auth entegrasyon paketi geçti. Gerçek version/status
  handler gövdeleri kaynaktan çıkarılır; gerçek `xprintf.c` ile derlenir.
  1/2/8/32/128/8192 byte kapasitelerde beklenen yanıt prefix'i ve uzunluğu,
  koruma baytları, inactive/active/ready durumları ve UINT32_MAX
  değerleri kontrol edilir. Diğer sekiz handler'ın çıktısı bu testte
  uçtan uca doğrulanmış sayılmaz; bunların değişikliği aynı uzunluk
  invariant'ına (korunan sınıra) dayanır.
- Önceki HEAD sürümünün gerçek version/status handler gövdeleri de
  aynı yanıt oracle’ı (beklenen çıktı) ve kapasite senaryolarıyla geçti.
  Yerel kanıt: `test/build/web_auth/http-lengths-baseline.log`.
- Baştan Release derlemesi geçti: toplam **487**, HTTP **91** uyarı kaldı.
  Kalan uyarılar bastırılmadı. Bunlar sonraki kaynak akışı incelemesidir.
- Fiziksel cihaz testi yapılmadı; pozitif kapasite sözleşmesi korundu.

Kanıtlar `test/build/production-audit-2026-10-03/` altında
`enabled-warnings-release.log`, `http-lengths-full-release.log`,
`http-lengths-ceedling.log`; handler sonuçları
`test/build/web_auth/web-auth-integration-repro.log` içindedir.


## 14. 04.10.2026 dönüşüm uyarıları ve veri türü kuralı

### Amaç ve yapılan değişiklik

İlk HTTP düzeltmesinden sonra kalan 487 uyarıdan aynı sözleşme
uyuşmazlıklarını üreten iki grup düzeltildi:

- IEC104/Modbus HTTP okuma/yazma ve JSON setter fider döngüleri yalnız
  0..6 kimlikleri kullanır. `uint32_t` isteyen config API'lerine uyumlu
  fider kimliği kullanıldı. Döngü sınırı ve yazılan alanlar korunur.
- Özel `gsm_at_response_ready()` yardımcısı callback'lerin mevcut
  `int32_t` yanıt sözleşmesiyle eşleştirildi. Yardımcı yalnız 0 veya
  küçük, negatif olmayan sabit GSM kodları döndürür. Aktif unsigned
  query sonucu sınırında açık dönüşüm vardır; public (dışa açık) API
  korunur. Kapalı SMS bloğu değiştirilmez.
- HTTP tampon konumları/boyutları kullanıcı kararıyla `size_t` oldu.
  `xsnprintf()` ve mevcut log formatter sınırında `unsigned int`
  dönüşümü, gönderimde int dönüşümü pozitif int kapasitesine dayanır.
  Formatter kapasiteyi sınırladığı için biriken konum int sınırını aşmaz.
  IPv4 octet değerleri maske sonrası 0..255'tir; `uint8_t` kullanılır,
  `%u` argümanları formatter sınırında unsigned olarak verilir.

Bu maddeler önceden kanıtlanmış veri kaybı bug'ları olarak sunulmaz.
Gerekçe: sözleşmeyi doğru tipte ifade etmek ve aynı güvenli işlemdeki
tekrarlayan uyarıları gidermek. Sınırsız girişlerin daraltılması aynı
şekilde otomatik cast eklenerek düzeltilmemelidir.

### Repo kuralı

AGENTS.md ve CLAUDE.md'ye aynı politika eklendi: tamsayı verileri
`stdint.h` sabit genişlikli tipleri; boyut/uzunluk/indeks `size_t`;
pointer farkı `ptrdiff_t`; mantıksal durum `bool` kullanır. Metin `char`,
ölçüm `float`/`double`, durum kodu enum olarak kalabilir. Standart
kütüphane/HAL/mevcut API imzaları sınır istisnasıdır. Toplu tip değişimi
ve kanıtsız cast yasaktır.

### Doğrulama ve sınırlar

Baştan ARM Release derlemesi başarılıdır. Toplam uyarı **487 → 385**,
HTTP **91 → 42**, GSM **165 → 117** oldu. Kalanlar bastırılmadı.
Contiki kernel ve vendor kaynakları değiştirilmedi; `#if 0` blokları
korundu. Kullanıcının `.cproject` ayarları korunur.

Ceedling'e üç gerçek GSM yardımcı senaryosu eklendi: NONE/OK/ERROR/timeout
sonuçları; OK ve ERROR altında NO CARRIER; NO SIM altında hata kodu ve
normal moddan SIM error moda geçiş. Sonuç fake'i testte sırayla kuyruk
oluşturan mock yerine o anki sonuca dönen stub kullanır.

HTTP source handler/gerçek formatter entegrasyon paketi, `size_t`
uyarlamasından sonra yeniden geçti. Fiziksel cihaz testi yapılmadı.

Kanıtlar `test/build/production-audit-2026-10-03/` içinde
`conversion-followup-full-release.log`, `conversion-followup-ceedling.log`,
`conversion-followup-integration.log` dosyalarıdır. Dizin tarihi önceki
çalışmanın tarihidir; bu devam incelemesi 04.10.2026 tarihinde yapılmıştır.


## 15. 04.10.2026 varsayılan adresler, metin ve AT komut uzunlukları

### Kanıt ve yapılan değişiklik

- NVRAM varsayılan hesabı yedi fider ve üç faz kullanır. En büyük Modbus
  adresi 40626'dır; 16 bit sınırı aşılmaz. Hesap uint32_t olarak yapılır,
  kalıcı uint16_t alana dönüşüm bu kanıtlanan sınırda açık yazılır.
  `_Static_assert` fider sayısı ve en büyük adresi derleme sırasında
  korur. Mevcut varsayılan değerler, şema ve layout değişmez.
- `str_substr()` çağrıları GSM parser yolunda erişilebilirdir. Pozitif
  arama indeksinden sonra boyut hesabı size_t yapılır; negatif arama
  sonucu aynı şekilde reddedilir. Başlangıç/bitiş delimiter (ayırıcı)
  yoksa, kapasite yetmezse, boş içerik varsa eski dönüş ve tampon
  davranışı korunur. `str_end_withs()` strlen sonucunu size_t tutar.
- `ipv4_to_str()` octet değerini 0xFF maskeler. Ondalık karakterler
  48..57 aralığındadır; uint8_t dönüşümü kayıpsızdır. Mevcut üç haneli,
  sıfırla tamamlanan IPv4 metni değiştirilmez.
- Login parser'da strchr ile bulunan son konum başlangıçtan önce olamaz.
  Pointer farkı size_t yapılır; mevcut credential dizi sınırı korunur.
  IPv4 octet'leri 0..255'tir. Sabit HTTP yanıt metinleri int sınırından
  çok küçüktür; int gönderim API'sine açık dönüşüm bu kanıta dayanır.
- `gsm_engine_send_query()` komut uzunluğunu uint8_t içinde tutuyordu.
  Her formatter sonucu aynı dar alana ekleniyordu. Uzunluk size_t oldu;
  mevcut 128 byte sınır kontrolünden sonra uint16_t AT motoru parametresine
  kayıpsız dönüşür. Komut metni, retry ve timeout değerleri korunur.
  Uzun APN xsnprintf ile kırpıldıktan sonra mevcut kontrol tarafından
  reddedilir. Diğer xsprintf kullanan dalların bütün girişleri için
  buffer güvenliği kanıtlanmış sayılmaz; bu değişiklik yeni bir genel
  buffer koruması eklemez.

Kapalı SMS bloğunda önceki tur eklenen üç cast geri alındı. İncelenen
C dosyalarının `#if 0` blokları HEAD ile aynı içeriktedir. Çalışma akışı
olmayan my_itoa/dec_to_str yardımcılarına yalnız cast eklenmedi.

### Doğrulama

- **349/349 Ceedling testi geçti.** GSM paketinde gerçek üretim parser
  yardımcıları için kapasite sınırı, boş içerik, eksik delimiter, devam
  pointer'ı ve canary testleri vardır. IPv4 için bütün 256 octet değeri
  kontrol edilir. Normal E0/APN AT komut byte'ları, uzunluk, retry ve
  timeout gerçek query fonksiyonundan mock taşıma servisine kadar
  doğrulanır. 160 karakterlik APN gönderilmeden reddedilir ve busy
  durumu bırakılır.
- **NVRAM entegrasyonu 433 kontrol geçti, sıfır hata.** Gerçek fabrika
  başlangıç koduyla yedi fiderin 21 Modbus ve 21 IEC104 nokta adresi
  beklenen haritayla karşılaştırılır. Packed (sıkıştırılmış) üyeler
  hizasız pointer alınmadan değer olarak okunur.
- HTTP auth ve gerçek handler/formatter entegrasyon paketi geçti.
- Baştan ARM Release derlemesi geçti. Toplam uyarı **385 → 249**,
  GSM **117 → 20**, HTTP **42 → 28**, GSM utils **21 → 12**,
  NVRAM **16 → 0** oldu. Uyarılar bastırılmadı.

Kanıtlar aynı test/build dizinindeki `conversion-defaults-ceedling.log`,
`conversion-defaults-nvram.log`, `conversion-defaults-http.log`,
`conversion-defaults-full-release.log`, `conversion-at-command-tests.log`
dosyalarındadır. Fiziksel cihaz testi ve commit yapılmadı.

### Kalan işler

IEC104 bit alanları (61 tanı), flash driver adres byte'ları (25), kalan
HTTP/shell/FW sınırları (28) ve GSM parser daraltmaları (20) açık kalır.
Ayrıca diğer modüllerde uyarılar vardır. Bit alanına atılan değerlerin
üretici/tüketici sınırları ve mevcut kodlama kuralları doğrulanmalıdır;
maskeler yalnız uyarı kaybolsun diye eklenmemelidir. Contiki ve vendor
kaynakları mevcut kullanıcı kapsamı dışında tutulur.


## 16. 04.10.2026 IEC104 ve SPI flash kodlaması

### Amaç ve kullanım yeri

IEC104 paketleri ve SPI flash komutları oluşturulurken görülen daraltma
uyarılarını, mevcut byte ve bit kodlamasını koruyarak gidermek.
Bu devam çalışması `Application/libiec104/iec104.c` ve
`Application/libs/w25qxx.c` dosyalarını kapsar.

### Kanıt ve yapılan değişiklik

- IEC104 control (kontrol) alanındaki sıra numarası 15 bittir.
  VSQ nesne sayısı 7 bit, SQ ve P/N bayrakları 1 bit, cause alanı
  6 bittir. Önceki unsigned bit-field (bit alanı) atamaları yalnız
  ilgili düşük bitleri saklıyordu. Aynı davranış açık maskelerle yazıldı.
  Örnek: P/N girdisi 2 için 0, 3 ve 255 için 1 kodlanmaya devam eder.
  Boolean dönüşüm eklenmedi; sıra numarası yönetimi değiştirilmedi.
- Telemetri fider döngüsü config API ile aynı uint32_t türünü kullanır.
  Faz ve arıza kayıt indeksleri mevcut sınırları içinde uint8_t tutulur.
  Batch (toplu gönderim) hesabı mevcut `(253 - 10) / sizeof(object)`
  sınırını kullanır; hesaplanan APDU uzunluğu en fazla 253 byte'tır.
  Başlatma APDU'sunun byte uzunluğu ayrıca static assert ile korunur.
- SIQ/DIQ kalite bayraklarının maske sonucu 0 veya 1'dir. En yüksek
  bayraktaki gereksiz ara uint8_t dönüşümü kaldırıldı. Paket kodlaması
  aynı kaldı; bütün kalite bayraklarının gönderilen byte'ı test edilir.
- SPI driver, 24 bit adresi high/middle/low sırasıyla üç uint8_t
  parametreye gönderiyordu. Her byte artık 0xFF ile ayrılır; komut ve
  adres byte sırası korunur. Okuma, fast read, byte/page program,
  verify ve üç erase komutu gerçek driver ile test edilir.
- Flash kimlik byte'ları unsigned türde birleştirilir. JEDEC sonucunun
  en yüksek byte'ı önce int olarak sola kaydırılıyordu; bu byte'ın yüksek
  biti set olduğunda C dilinde signed kaydırma tanımlı değildir.
  uint32_t kaydırma bu riski giderir. Sahada gözlenmiş bir arıza iddiası
  değildir; mevcut byte sırası ve kimlik değeri korunur.
- Kapasite tablosunda bilinmeyen kimlik için kullanılan static compound
  literal host C11 derleyicisinde kabul edilmedi. Aynı statik ömürlü,
  sıfır kapasiteli sonuç normal static const nesne olarak yazıldı.
  Driver'ın dışa açık API'si değiştirilmedi.
- Flash tanılama sayfasının indeksleri size_t oldu. Test pattern (test
  deseni) 0..255 değerlerinin byte'a dönüşümü sayfa boyutu static assert'i
  ile korunur. Flash layout ve NVRAM şeması değiştirilmedi.

Bu uyarıların çoğu mevcut dar alan kodlamasının açık yazılmasıyla kapanır;
her uyarı ayrı bir lojik hata anlamına gelmez. Maske bir giriş doğrulama
mekanizması olarak sunulmaz; geçersiz girişlerin mevcut davranışı korunur.

### Doğrulama

| Kontrol | Sonuç |
|---|---|
| Baştan ARM Release derlemesi | Başarılı; toplam **249 → 163** uyarı |
| `libiec104/iec104.c` | **61 → 0** uyarı |
| `libs/w25qxx.c` | **25 → 0** uyarı |
| Tam Ceedling paketi | **361/361 geçti**, sıfır hata/atlanmış test |
| Yeni SPI testleri | **10 senaryo**; gerçek driver, mock SPI/BSP |
| Yeni IEC104 testleri | P/N düşük bit davranışı; SIQ/DIQ bayrak byte'ları |
| Mevcut IEC104 senaryoları | Tam 15 bit sıra döngüsü, receive wrap, batch arıza gönderimi ve paket sınırları geçti |
| `#if 0` içerikleri | Değiştirilen üretim modüllerinde HEAD ile aynı |

IEC104 Ceedling paketinin `-Wno-conversion` ve `-Wno-sign-conversion`
istisnaları kaldırıldı. IEC104 ve yeni SPI testleri mevcut sıkı host
seçenekleriyle, `-Werror` dahil geçer. Yeni bir uyarı bastırma eklenmedi.
`test/support/main.h` yalnız host derlemesindeki NOP shim'idir (uyarlayıcı);
MCU gecikmesini veya fiziksel flash zamanlamasını modellemez.

Kanıtlar `test/build/production-audit-2026-10-03/` dizinindeki
`wire-encoding-full-release.log` ve `wire-encoding-ceedling.log`
dosyalarındadır. Birim testleri mevcut Ceedling dizinlerine eklenmiştir.
Fiziksel cihaz/SCADA kabul testi ve commit yapılmadı.

### Kalan işler

Derlemede **149 Application**, **8 Contiki**, **5 vendor**, **1 Core**
uyarısı kalır. En büyük Application grupları HTTP handler (28), GSM
engine (20), fault log (16) ve GSM utils (12) dosyalarındadır. Diğerleri
formatter/scanner, UART/BSP, log ve uygulama adaptörlerindedir.
Contiki ve vendor kullanıcı kapsamı dışında kalır. Release uyarı ailesi
henüz tüm zorunlu seçenekleri içermez; proje warning-free sayılmaz.
Girişe bağlı uzunluk ve parser daraltmaları için sınır ve hata sözleşmesi
kanıtlanmalıdır; yalnız cast eklenerek kapatılmamalıdır.


## 17. 04.10.2026 kalan 163 uyarının kaynak incelemesi

### Amaç ve kapsam

Bölüm 16 sonrasında kalan tanıların kaynak sözleşmelerini kontrol etmek,
uygulama kaynaklarındaki uyarıları kapatmak ve korunan kaynakları açıkça
ayırmak. Uyarı bastırma veya toplu API değişikliği yapılmadı.

### Kanıt ve yapılan değişiklik

| Grup | Kaynaktaki sınır ve sonuç |
|---|---|
| UART/GPIO | ST LL flag fonksiyonları 0/1 döndürür. Mevcut int/uint8_t API sınırında açık dönüşüm kullanılır. Port okumasının düşük 16 biti önceki uint16_t dönüşümüyle aynı tutulur. Register ayarı değiştirilmez. |
| BSP tarih ve IEC104/fault bit alanları | Maskeler mevcut unsigned bit-field genişliğiyle aynıdır. RTC snapshot, bayrak anlamı ve kalıcı kayıt layout'u değişmez. |
| BMS/Modbus | CRC uzunluğunda mevcut minimum frame kontrolü, Modbus farkında mevcut register aralığı vardır. Pozitif ve küçük sonuçlar mevcut uint16_t API sınırında dönüştürülür. |
| Ring indeksleri | IEC104 TX slot ve fault log modulo sonuçları mevcut slot/kayıt sayısından küçüktür; byte alana kayıpsız dönüşür. Kuyruk ve kayıt sırası korunur. |
| GSM/AT uzunlukları | AT motorunun response tamponu 1280 byte'tır. URC ayırıcıları ve SRECV son satır hesabı bu tampon içindedir. Pozitif indeksler ve mevcut minimum uzunluk kontrollerinden sonra uint16_t dönüşüm yapılır. HTTP gönderim ara tamponu 1500 byte'tır. |
| HTTP/shell/log uzunlukları | Formatter dönüşü kapasiteyle sınırlıdır. Boyut ve konumlar uygun unsigned/size_t türünde tutulur; int gönderim API'sine dönüşüm pozitif kapasite sınırına dayanır. Sabit reboot yanıtı int sınırının çok altındadır. Shell prefix en fazla 63 karakterdir; oturum/ISR tasarımı değiştirilmez. |
| GSM durum kodları | Response enum ve TX direction sabitleri mevcut byte alana sığar. Dışa açık API imzaları korunur. Callback sonucu nonnegative GSM durum kodudur. |
| Float/hex metni | Mevcut integer → float hesap ve ASCII karakter dönüşümleri açık yazıldı. Hex nibble değerleri 0..15, rakam karakterleri 48..57 aralığındadır. Float ölçüm hesabı değiştirilmedi. |

### Gerçek sınır sorunları

- Trace/dialer `#SI` callback'leri iki signed ayırıcı indeksini daraltıp
  ters yönde sayıyordu. Ayırıcı sırası kontrol edilmeden hesaplanan
  negatif uzunluk büyük unsigned değere dönüşebilirdi. Mevcut `xscanf`
  altyapısı yeniden kullanıldı; yeni bir parser kurulmadı. Dört sayaç
  önce uint32_t olarak okunur. Mevcut uint16_t ACK alanına sığmayan
  değer `GSM_ERROR` olur; ACK sıfıra sarmaz. Listener ve SI-all yollarına
  da aynı dar alan kontrolü eklendi. SI-all mevcut cache temizleme
  davranışını korur; geçersiz cevap cache'i valid olarak işaretlemez.
- `+CCED` ayırıcıları eksik olduğunda pointer/uzunluk hesabı geçersiz
  olabiliyordu. Dört alan mevcut scanner ile yerel değişkenlere okunur;
  parse ve uint16_t aralık kontrolleri geçmeden cell bilgisi yazılmaz.
  Geçerli MCC/MNC decimal, LAC/cell hex kodlaması korunur. Bozuk yanıtta
  `GSM_ERROR` döner. Sahada oluşmuş bir arıza iddiası değildir.
- Formatter signed minimum değerinin büyüklüğünü `0 - v` ile signed
  türde hesaplıyordu. INT64_MIN için bu C dilinde overflow'dur (taşma).
  Büyüklük unsigned çıkarma ile hesaplanır; normal metin değişmez.
  Negatif dinamik width için de signed negation kaldırıldı.
- HTTP header 256 byte stack tamponuna sınırsız `xsprintf` ile yazılıyordu.
  Mevcut bounded `xsnprintf` kullanıldı. Header tamamlanmadıysa sabit,
  geçerli 500 yanıtı gönderilir ve çağıranın body verisi gönderilmez.
  int uzunluk API'sine sığmayan HTML resource da body okunmadan reddedilir.
  Mevcut gömülü sayfalar bu sınırların çok altındadır.

`my_itoa`/`dec_to_str` algoritmaları değiştirilmedi; yalnız 0..9 rakamının
ASCII byte'a dönüşümü açık yazıldı. Repoda aktif çağrıları bulunmadı.
Bu çalışma bu eski yardımcıların genel tampon sözleşmesini kanıtlamaz.
Benzer şekilde bütün sınırsız xsprintf çağrılarının güvenliği tamamlanmış
sayılmaz; bölüm 15'teki AT komut tamponu sınırı geçerlidir.

MinGW host math macro'ları double classification (sayı sınıflandırması)
sırasında float daraltma uyarısı üretiyordu. Repo GCC toolchain'inde
`__builtin_isnan`/`__builtin_isinf` kullanılarak double değer doğrudan
sınıflandırılır. NaN/INF/normal değer format testleri geçti. ChaN kaynak
lisansı korunur; formatter ve scanner testlerindeki eski conversion
istisnaları kaldırıldı. GSM monolitik host testinin önceki istisnaları
bu değişiklikte genişletilmedi.

### Doğrulama

| Kontrol | Sonuç |
|---|---|
| Baştan ARM Release `main-build` | Başarılı; **163 → 14** uyarı, sıfır error |
| Application kaynakları | **149 → 0** uyarı; mevcut Release seçenekleri altında |
| Tam Ceedling paketi | **377/377 geçti**, sıfır hata/atlanmış test |
| Tam entegrasyon | **9/9 paket geçti** |
| Yeni GSM senaryoları | 6 test: ACK 0/65535/65536, büyük sayaç, bozuk/eksik alan, valid cache ve cell alanlarının korunması |
| Yeni formatter senaryoları | 2 test: INT32_MIN/INT64_MIN metni; negatif/pozitif dinamik width |
| Yeni HTTP response senaryoları | 4 test: normal header/body byte eşitliği, uzun header reddi, gzip payload ve oversized resource reddi |
| Yeni application utils senaryoları | 4 test: float kesinlik sınırının üstünde integer, float yolları, tam 32 bit hex ve bütün 256 byte için encode/decode/canary |
| Korunan kod | Değiştirilen bütün Application dosyalarındaki `#if 0` blokları HEAD ile aynı |

SI-all overflow testinde önceki cache'in korunacağı varsayımı yanlıştı.
Kaynak her SI-all sorgusunda cache'i temizler; test bu mevcut sözleşmeyle
uyumlu düzeltildi. Hata eski cache'i valid saymaz; ACK state korunur.
Test varsayımı için üretim cache akışı değiştirilmedi.

Kanıtlar `test/build/production-audit-2026-10-03/` altında
`remaining-full-release.log`, `remaining-ceedling.log`,
`remaining-integration.log`, `remaining-formatter-tests.log` ve
`remaining-parser-tests.log` dosyalarındadır. Birim testleri Ceedling
altındadır; mevcut entegrasyon paketi korunmuştur. Fiziksel cihaz testi,
firmware yükleme veya commit yapılmadı. Release'te henüz bütün zorunlu
uyarı seçenekleri etkin değildir; bu sonuç o seçeneklere ilişkin genel
warning-free belgesi değildir.

### Kullanıcı kararıyla kapsam dışındaki 14 uyarı

- **8 Contiki:** Kullanıcı tarafından açıkça kapsam dışında bırakıldı.
- **5 ST vendor:** RTC BCD (1), UART FIFO sayısı (2), release assert'i
  silinince kullanılmayan RCC parametresi (1), ADC sıcaklık macro'sundaki
  signed/unsigned sabit (1). Üretim driver dosyaları değiştirilmedi.
- **1 Core:** `Core/Src/syscalls.c:74` içinde `__io_getchar()` int sonucunun
  char'a atanması. Dosyada USER CODE marker'ları yoktur. AGENTS.md'nin
  CubeMX kuralı korunur; kullanıcı bu kaynağı da kapsam dışında bıraktı.

**Kullanıcı kararı (04.10.2026):** “ST/CubeMX kaynaklarını koru; 14 uyarı
kapsam dışında kalsın.” Son altı kaynak için önerilen düzeltmeler
uygulanmadı. Contiki, ST driver ve Core dosyaları değiştirilmedi.
Kapsam içindeki uygulama uyarıları kapanmıştır; tam build log'unda bu
14 tanı görünmeye devam eder. Bu altı ST/Core tanısı sahada arıza
oluştuğunun kanıtı değildir.


### Commit kaydı

Bölüm 13–17'deki uygulama düzeltmeleri, Ceedling ve entegrasyon testleri,
veri türü kuralları `bf7ac1a` commit'inde kaydedilmiştir. CubeIDE Release
uyarı seçenekleri `5db75b8` commit'indedir. Önceki bölümlerdeki
“commit yapılmadı” ifadeleri ilgili inceleme anının durumunu anlatır.
Son ARM Release log'unda `-Wshadow` etkindir; `-Wformat=2` ve `-Werror`
etkin değildir. Application için sıfır uyarı sonucu mevcut seçenekler
altında geçerlidir; kullanıcı kararıyla kapsam dışındaki 14 tanı korunur.
