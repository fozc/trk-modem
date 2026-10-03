# Derleyici uyarıları değerlendirmesi ve düzeltme sırası

**Sürüm:** 1.3
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
