# BOLATeX'e sorulacak RF-SCP konuları

**Sürüm:** 0.3

**Tarih:** 06.10.2026

## Amaç

RTU uygulaması sırasında protokolde açık kalan noktaları ve örnek paketlerle
metin arasındaki farkları BOLATeX'e sorulabilecek biçimde toplamak.

## Kullanım yeri

BOLATeX ile teknik görüşme ve cevap takibinde kullanılır. Bu belge henüz
gönderilmemiştir. Sorular protokol değişikliği veya uygulama onayı değildir.
Referans [SCP arayüz belgesi R1](BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/MH_STM32_RTU_SCP_Arayuzu_R1.md);
künyedeki MH/AY firmware `6dc02267`, PWRB firmware `915bb945` esas alınır.
Örnekler [Ornek_SCP_Akislari](BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/Ornek_SCP_Akislari/BENIOKU.md)
klasöründendir. Gerçek cihaz davranışı bu çalışmada ölçülmemiştir.

## Terimler

| Terim | Anlam |
|---|---|
| RTU | Bu projedeki STM32 modem yazılımı |
| MH | Modem üzerindeki RF hub |
| AY | Ayırıcı |
| head / tail | Sonraki yazma yuvası / ilk tüketilmemiş yuva |
| pending | Tüketilmemiş kayıt sayısı |
| backlog | Birikmiş, henüz çekilmemiş kayıtlar |
| epoch | MH değişimi sonrası yapılandırma için yenilenen ağ dönemi |

## Kurallar

- BOLATeX cevabı, tarih ve ilgili firmware sürümüyle kaydedilmelidir.
- Geçici RTU tercihi BOLATeX tarafından doğrulanmış davranış sayılmamalıdır.
- Doküman tablosuyla örnek çelişiyorsa fark kayıt altına alınmalıdır.
- Cevap uygulama davranışını değiştiriyorsa ilgili Ceedling testi ve plan
  güncellenmelidir.

## Açık sorular

### BQ-01 — Geç gelen 101/105 kaydı ve alarmın çözülmesi

**Durum:** [AÇIK] Alarm olay bağlantısı cevap gelene kadar bekletiliyor.

**Kaynak:** R1 §4.5 LIVE_DATA ve §4.8 olay 101/105.

**Kanıt:** Doküman aynı açılıştaki Trip_Failed 1→0 geçişini çözülme sayar;
uptime sıfırlanırsa operatör onayı ister. Olayda 101 bulunup LIVE'da bayrak
hiç 1 görülmemişse de başarısız açma kabul edilir. Olay kaydı boot_counter
taşır; LIVE aynı açılış kimliğini taşımaz.

**Örnek:** RTU önce Trip_Failed=0 olan LIVE alır; sonra eski 101/105 kaydını
çeker. Olay uptime'ı 100 s, son LIVE uptime'ı 200 s olabilir. Son LIVE aynı
açılıştan da, AY restart sonrası başka açılıştan da olabilir.

**Sorular:**

1. Geç gelen olay hangi durumda aktif alarm oluşturmalı, hangi durumda
   yalnız olay geçmişinde kalmalıdır?
2. LIVE'da 1→0 geçişi görülmediyse son 0 değeri çözülme kanıtı sayılır mı?
3. Olay 101 için verilen özel kural aynı durumda 105 için de geçerli midir?
4. LIVE'a boot_counter veya eşdeğer açılış kimliği eklenebilir mi?

**Mevcut uygulama:** Ham 101/105 kaydı saklanıyor. LIVE üzerinden mevcut
alarm takibi korunuyor; belirsiz geç kayıt için yeni latch davranışı
uygulanmadı. Otomatik çözülme veya operatör onayı önerisi kesinleşmedi.

**Cevap:** Bekleniyor.

### BQ-02 — head=tail durumunda dolu ve boş halkanın ayrımı

**Durum:** [AÇIK] Mevcut RTU kabulü var; MH davranışı doğrulanmadı.

**Kaynak:** R1 §4.6 LOG_READ_HEAD, LOG_AVAILABLE_NOTIFY.

**Kanıt:** MH halkası 100 yuva olarak tanımlıdır. HEAD yalnız head, wrap,
total ve tail verir. pending yalnız kaybolabilen bildirimdedir. free_slots
§4.2'de yaklaşık değer olarak tanımlanır.

**Sorular:**

1. Tüketilmemiş 100 kayıt varsa head=tail oluşur mu; yoksa bir yuva boş
   bırakılarak en çok 99 tüketilmemiş kayıt mı tutulur?
2. pending=100 bildirimi kaybolmuşsa RTU yalnız HEAD ile dolu/boş ayrımını
   nasıl yapmalıdır?
3. HEAD'e pending veya açık full alanı eklenmesi gerekir mi?

**Mevcut uygulama:** Kullanıcı cevabıyla head=tail boş kabul ediliyor.
Aynı head için pending=100 bildirimi varsa dolu halka okunuyor. Yaklaşık
free_slots kesin tüketme kararı için kullanılmıyor.

**Cevap:** Bekleniyor.

### BQ-03 — Üzerine yazılan yuva ve CONSUME güvenliği

**Durum:** [AÇIK] Yerel depolama beklemesi için kontrol uygulanmış durumda.

**Kaynak:** R1 §4.6 halka dolunca en eski kaydın üzerine yazılması ve
LOG_CONSUME_TO'nun yalnız indeks taşıması.

**Örnek:** RTU tail=36'daki kaydı okur. Flash hatası nedeniyle beklerken MH
yeni kayıtlar yazar; eski yuva ezilir. Halka sarınca indeks yeniden 36 olur.

**Sorular:**

1. Üzerine yazma sırasında MH tail'i tam olarak nasıl ilerletir?
2. Eski indeksi taşıyan CONSUME isteği yeni kayıtları yanlışlıkla tüketebilir
   mi; MH eski isteği hangi bilgiyle ayırır?
3. HEAD kontrolünden CONSUME işlenmesine kadar yeni overwrite oluşursa
   mevcut protokolün güvenli işlem sırası nedir?
4. Beklenen wrap/total veya kayıt kimliğiyle koşullu tüketme desteklenmeli mi?

**Mevcut uygulama:** Yerel yazma hatası nedeniyle beklenmişse tüketmeden
önce HEAD tekrar çekilir. Tail ve total değişimiyle ezilmiş yuva denetlenir;
durum değişmişse eski CONSUME yerine güncel tail'den tekrar başlanır.
Bu iki SCP isteği arasında atomik işlem garantisi sağlamaz.

**Cevap:** Bekleniyor.

### BQ-04 — Örnek bildirim ACK'i ve paket zaman sırası

**Durum:** [AÇIK] RTU beklentisi metne göre uygulanıyor.

**Kaynak:** R1 §2.2/§4.6/§6.2 ve AY_05b_olay_kaydi_cekme.csv.

**Kanıt:** CSV satır 2, 0x47 bildirimi için RTU ACK'i içerir. Metin bu
bildirimlere yanıt verilmemesini ister. CSV'de RANGE isteği HEAD yanıtından,
CONSUME isteği de RANGE yanıtından önce zamanlanmış görünür.

**Sorular:**

1. ACK, eski RTU/MH sürümüne mi aittir, yoksa örnekten kaldırılmalı mıdır?
2. Timestamp'ler gerçek hat sırasını mı, farklı tarafların kayıt zamanını mı
   gösterir? Testte kullanılacak kesin nedensel sıra hangisidir?
3. Künyedeki firmware için düzeltilmiş örnek paket seti paylaşılabilir mi?

**Mevcut uygulama:** ACK üretilmiyor. Paket CRC/decode kontrolleri ham
örneklerle yapılıyor; işlem testleri HEAD yanıtı → RANGE yanıtı → CONSUME
sırasıyla yürütülüyor. Kaynak CSV değiştirilmedi.

**Cevap:** Bekleniyor.

### BQ-05 — Depo sıfırlanması ve total/wrap sayaçları

**Durum:** [AÇIK] İlk güncel HEAD yeniden başlangıç için esas alınıyor.

**Kaynak:** R1 §4.6 LOG_READ_HEAD ve depo sıfırlanması.

**Kanıt:** total cihaz ömründeki toplam kayıt sayısı diye tanımlanır.
Servis silmesinde head/wrap/tail sıfırlanır; total için aynı açıklama yoktur.
MH açılışta biçimlendirme yaparsa ilk olay 138 olur.

**Sorular:**

1. Servis silmesinde ve MH biçimlendirmesinde total sıfırlanır mı?
2. Normal MH restart'ında total ve wrap aynen korunur mu?
3. total ve wrap taşması nasıl davranır; 138 olayı iki sıfırlama türünde de
   üretilir mi?

**Mevcut uygulama:** BOOT sonrası eski RAM imleci sürdürülmüyor. Ham 138
kaydı korunuyor. Depo silinmişken eski yuvayı tüketmek için otomatik
varsayım yapılmıyor.

**Cevap:** Bekleniyor.

### BQ-06 — PARTIAL_COMMIT olay 122'sinin işlemle eşlenmesi

**Durum:** [AÇIK] Grup işlemi sonrası doğrulama için gerekli.

**Kaynak:** R1 §4.9 sebep 6 sonrası doğrulama ve §4.7/§4.8 olay 122.

**Kanıt:** Sorunlu bitmap üyesinin bu işleme ait 122 olayı varsa uygulamış
sayılması istenir. Standart olay kaydı group_id veya uygulanan cfg_crc
taşımaz; yalnız fider/faz/zaman/açılış/uptime alanlarıyla eşleme gerekir.

**Sorular:**

1. Backlog içindeki eski 122 ile güncel grubun 122 kaydı nasıl ayrılmalıdır?
2. Saat geçersizken bu işlemle eşleme hangi alanlarla yapılmalıdır?
3. 122'ye group_id/cfg_crc eklenebilir mi; olayın işleme kesin aidiyetini
   sağlayan başka bir bilgi var mıdır?

**Mevcut uygulama:** Eski 122, güncel grubun APPLIED kanıtı sayılmıyor.
Ham kayıt korunuyor; bu özel doğrulama henüz bağlanmadı.

**Cevap:** Bekleniyor.

### BQ-07 — MH değişiminden sonra EPOCH_REFRESH tamamlanması

**Durum:** [AÇIK] Operatörün mevcut epoch komutu kullanılabilir.

**Kaynak:** R1 §4.3 EPOCH_REFRESH ve önerilen yaklaşık 30 s bekleme.

**Sorular:**

1. ACK yalnız kuyruğa alındığını gösteriyorsa, üç AY'nin epoch yenilediğine
   dair kesin bildirim/alan var mıdır?
2. 30 s sonrası yeni grup ayarı güvenle başlatılabilir mi; RF iletişimi
   kesilirse bu süre tek başına yeterli midir?

**Mevcut uygulama:** ACK kesin tamamlanma sayılmıyor. Otomatik MH değişimi
çıkarımı yapılmıyor; envanterden sonra operatör epoch yenileyebiliyor.

**Cevap:** Bekleniyor.

### BQ-08 — PWRB 0xE8 ham telemetri biçimi

**Durum:** [AÇIK] Güç kartı tüketici geçişinde gerekli.

**Kaynak:** R1 §5, PWR_TELEMETRY 96 B big-endian ham blok.

**Sorular:**

1. Alan/ofset/tip/birim/geçerlilik tablosu ve blok sürümü paylaşılabilir mi?
2. Eski doğrudan I²C bloğuyla birebir aynı düzen olduğu doğrulanıyor mu?
3. Geçersiz/bayat alanlar, sentinel değerler ve blok içi CRC nasıl yorumlanır?

**Mevcut uygulama:** 96 B ham veri korunuyor. Eski I²C decoder'ının aynı
biçimi çözdüğü varsayılmıyor; PWR_SUMMARY'nin tanımlı alanları ayrı çözülüyor.

**Cevap:** Bekleniyor.

### BQ-09 — Envanter değişiminden sonra eski olayın ayırıcı kimliği

**Durum:** [AÇIK] Geç alarm ve olay 122 eşlemesini etkiliyor.

**Kaynak:** R1 §4.3 EUI-64 envanteri ve §4.7 olay kaydı.

**Kanıt:** Canlı veri mevcut ACK envanteriyle EUI-64'e bağlanır. 60 B
olay kaydında EUI-64 yoktur; zone/fider/faz vardır. Aynı fider/faz başka
bir AY'ye atanmışken eski kayıt çekilebilir.

**Sorular:**

1. MH eski kaydın zone/fider/faz alanlarını kayıt anındaki haliyle mi
   saklar; okuma anında güncel envanterle yeniden etiketler mi?
2. AY değişimi sonrası eski 101/105 veya 122 kaydının yeni ayırıcıya
   bağlanmasını önlemek için hangi kaynak kimliği kullanılmalıdır?
3. Kayıt kaynağı EUI-64'ü ayrı metadata olarak alınabilir mi; envanter
   değişiminde olay çekme için zorunlu işlem sırası var mıdır?

**Mevcut uygulama:** Ham kayıt değiştirilmeden saklanır; Line_ID=0'a fider
uydurulmaz. Mevcut arıza listesi devre/faz geçmişidir ve EUI taşımaz.
Eski 101/105'in yeni ayırıcıda alarm açması veya eski 122'nin yeni grup
başarısı sayılması uygulanmadı. BQ-01 ve BQ-06 cevaplarıyla birlikte
ele alınmalıdır.

**Cevap:** Bekleniyor.

### BQ-10 — Geçici ve kalıcı arızanın tanımı ve olay eşlemesi

**Durum:** [AÇIK] Kullanıcı BOLATeX'in sınıflamayı netleştirmesini istedi.

**Kaynak:** R1 §4.7 arıza alanları, §4.8 olay tablosu ve
total_permanent_faults / total_temporary_faults sayaçları.

**Kanıt:** Olay 1 açıkça kalıcı arıza açmasıdır. Olay 3 arızanın kesici
açmadan kendiliğinden geçmesidir. Olay 7 başka fazın RF isteğiyle açmadır.
4/5 arıza algılama/onaylama; 6 üst kesicinin açması; 100 açmanın iptali;
101 açmanın yapılamaması; 105 açma sonrası akımın geri gelmesidir.
Arıza ölçümleri taşıyan olay listesiyle geçici/kalıcı liste sınıflaması
aynı şey olarak tanımlanmamıştır.

**Sorular:**

1. Geçici ve kalıcı arızanın kesin tanımı nedir? Ayrım arıza süresine,
   hattın yeniden enerjilenmesine, arıza sayacına veya ayırıcının gerçekten
   açmasına mı bağlıdır?
2. Bütün event_trigger değerleri için geçici / kalıcı / yalnız olay veya
   tanı sınıfını gösteren kesin tablo paylaşılabilir mi?
3. 1 ve 7'nin kalıcı, 3'ün geçici listesine yazılması doğru mudur? Olay 7,
   yerel fazda arıza olmasa da kalıcı arıza mı sayılmalıdır?
4. 4/5 algılama, 6 kesici açması ve sonraki 1/3/7 kayıtları tek arızanın
   aşamaları mıdır? Listede aynı arızanın iki kez sayılmasını önleyen
   terminal (sonuç) olay hangisidir?
5. 100/101/105 için arıza listesi ve açma başarısızlığı alarmı birlikte mi
   tutulmalıdır; bu olaylar hangi arıza sayacını artırır?
6. total_permanent_faults / total_temporary_faults hangi olayda artar?
   Her kayıttaki değer olaydan önceki mi, sonraki mi sayaçtır?

**Mevcut uygulama:** Kullanıcı onaylı geçici RTU eşlemesi 1/7 kalıcı ve
3 geçicidir. Bu eşleme BOLATeX tarafından doğrulanmış sınıflama değildir.
Diğer bütün olaylar tam ham günlükte korunur; yeni bir sınıf eklenmedi.
Cevap geldikten sonra enum notu, liste/IEC104 yönlendirmesi ve Ceedling
beklentileri birlikte gözden geçirilmelidir. BQ-01'deki alarm davranışı
bu sınıflama tablosunun yerine geçmez.

**Cevap:** Bekleniyor.

### BQ-11 — COMMIT öncesi yarım WRITE grubunun ABORT kimliği

**Durum:** [AÇIK] Grup sıralayıcısının hata temizliğini etkiliyor.

**Kaynak:** R1 §4.9 CFG_WRITE / CFG_COMMIT / CFG_ABORT.

**Kanıt:** WRITE yalnız EUI-64 ve 96 B blok taşır; group_id COMMIT ile
verilir. COMMIT gelmeyen WRITE için MH'nin kendiliğinden timeout yapmadığı
ve ABORT ile kapatılması gerektiği yazılıdır. ABORT group_id ister.

**Sorular:**

1. Birinci/ikinci WRITE sonrasında COMMIT gönderilemeden RTU vazgeçerse
   ABORT hangi group_id ile gönderilmelidir?
2. Henüz COMMIT ile tanıtılmamış yeni group_id, hazırlanan grubun ABORT'u
   için kabul edilir mi; eski uygulanmış grup kimliğiyle ilişkisi nedir?
3. RTU restart'ı veya kayıp WRITE ACK'i sonrası hazırlanmış üyeler/kimlik
   nasıl sorgulanır; aynı üç EUI'ye yeniden WRITE ile devam etmek güvenli mi?
4. Başka fidere geçmeden önce temizliğin tamamlandığı nasıl doğrulanır?

**Mevcut uygulama:** Otomatik yarım grup temizliği uygulanmadı; bilinmeyen
kimlikle ABORT veya dördüncü EUI ekleme yapılmıyor. 06.10.2026 normal grup
sıralayıcısı eklendi. COMMIT kimliği ACK/durumla biliniyorsa operatör ABORT
gönderebilir; WRITE henüz hiç gönderilmemişse yalnız yerel iş iptal edilir.
Yarım WRITE sonucu belirsizse yeni grup kilitlidir. Mevcut group_id için
STATUS_GET normal sorgu olarak kullanılabilir. Bunlar COMMIT öncesi
kimlik sorusunu çözülmüş saydırmaz.

**Cevap:** Bekleniyor.

## RTU tarafında alınmış kararlar

Bu tercihler BOLATeX cevabı diye sunulmamalıdır. Bugünkü uygulama kararları
[uygulama planında](RF_SCP_MODEM_UYGULAMA_PLANI.md) tutulur.

| Konu | RTU kararı |
|---|---|
| Geçersiz RTC | TIME_SYNC atlanır, envanter yüklenir; saat geçerli olunca eşitlenir |
| Tüketme koşulu | Başarılı kalıcı kayıt veya RTU gönderimi yeterlidir; remote ACK ayrıca beklenmez |
| Ham günlük | Ayrı 8 KB; tam 60 B paket korunur, eski arıza alanları tutulur |
| Arıza sınıflaması | Olay 1/7 kalıcı, 3 geçici RTU kabulüdür; BQ-10 teyidi beklenir. Süre uint32_t olur |
| HEAD sorgusu | 60 s RTU poll tercihi; dokümanda zorunlu RTU süresi değildir |
| Tekrar kayıt | Restart/kayıp tüketme sonucunda kopya mümkündür; kalıcı tekilleştirme garantisi yoktur |
| Sahadaki eski biçim | Saha cihazı yoktur; eski RF model/JSON/görüntü migration'ı istenmemiştir |

İstenen ayarın NVRAM'e kaydedilme zamanı, PARTIAL_COMMIT'te otomatik tekrar
ve yeni IOA/Modbus adresleri RTU ürün kararlarıdır; BOLATeX'in protokol
cevabı bu ürün kararlarının yerine geçmez. Bu konular plan K5/K9'da açıktır.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 05.10.2026 | 0.1 | Bugünkü RF-SCP incelemelerinden BQ-01–09 ve RTU kararları toplandı |
| 05.10.2026 | 0.2 | BQ-10 arıza sınıflaması/sayaçlar ve BQ-11 COMMIT öncesi ABORT kimliği eklendi |
| 06.10.2026 | 0.3 | BQ-11'in normal grup/COMMIT sonrası ABORT uygulama durumu güncellendi; soru açık kaldı |
