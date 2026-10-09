# BOLATeX'e RF-SCP soruları ve RTU uygulama bildirimleri

**Sürüm:** 0.18

**Tarih:** 08.10.2026

**Durum:** BQ-01–20 yanıtları alındı. Aşağıdaki 08.10.2026 soru ve
bildirim metni gönderim geçmişidir. 09.10.2026 yanıtının güncel uygulama
durumu [R2 uygulama planında](RF_SCP_MODEM_UYGULAMA_PLANI.md#r2-uyumu--09102026)
ve [R2 analizinde](BOLATEX_R2_DURUM_ANALIZI_2026-10-09.md) tutulur.
R2.1 önerileri yayımlanmış özellik değildir.

## Amaç

RF-SCP entegrasyonunda kalan dört soruyu ve RTU tarafında uygulanan
kararları tek belgede BOLATeX'e iletmek. Bu belge tek başına okunabilir;
başka bir RTU raporunun veya uygulama planının eklenmesi gerekmez.

## Kullanım yeri

BOLATeX ile teknik görüşme ve cevap takibi. Kaynaklar BOLATeX Teslim #4
MH–STM32 RTU SCP Arayüzü R1, BOLATeX RF-SCP Soruları R0 yanıtı ve
R0 Ek-1 yanıtıdır. R0/Ek-1 ile güncellenen davranışlar esas alınır.
Planlanan üretici değişiklikleri mevcut firmware özelliği sayılmaz.

08.10.2026 gerçek MH gözleminde GET_STATUS firmware kimliği `6dc02267`,
BOOT SCP major değeri 1'dir. Powerboard `915bb945` kimliği R1 belgesinin
referansıdır; burada Powerboard sürümünün ayrıca ölçüldüğü iddia edilmez.

Bu belgedeki “RTU kararı” ifadeleri üretici yanıtı veya BOLATeX onayı
olarak değerlendirilmemelidir. Uygulanan tercihler ve açık sınırlar
bildirimler bölümünde tam olarak verilmiştir.

## Terimler

| Terim | Anlam |
|---|---|
| RTU | STM32 üzerinde çalışan modem yazılımı |
| MH — Modem Hub | Modem üzerindeki RF hub |
| AY | Fiderin fazına atanmış ayırıcı |
| EUI-64 | Ayırıcının 8 baytlık cihaz kimliği |
| head / tail | Sonraki yazma yuvası / ilk tüketilmemiş yuva |
| pending | Tüketilmemiş kayıt sayısı |
| binding (kabul edilmiş atama) | MH'nin ACK verdiği bölge/fider/faz/EUI eşleşmesi |
| epoch | MH değişimi bakımında yenilenen RF ağ dönemi |
| APPLIED | MH sonucu, üye bitmap'i ve beklenen ayar CRC'siyle doğrulanan uygulama sonucu |
| latch (tutulan alarm) | Kayıt ve onay tamamlanana kadar korunan alarm durumu |
| replay (yeniden gönderim) | Kalıcı günlükteki gönderilmemiş kayıtların bağlantı sonrasında gönderilmesi |
| IOA | IEC104 bilgi nesnesi adresi |
| IV | Zamanın/ölçümün geçersiz olduğunu belirten kalite biti |

## Kurallar

- Her yanıt ilgili BQ kimliğiyle, tarih ve geçerli MH/AY/Powerboard
  firmware sürümü belirtilerek verilmelidir.
- Mevcut firmware davranışı ile planlanan değişiklik ayrı belirtilmelidir.
- RTU ürün tercihi, üretici tarafından tanımlanmış protokol davranışı
  olarak sunulmamalıdır.
- Yanıtlanmış BQ-01–16 bu gönderimde yeniden sorulmamalıdır; bunlarla
  ilişkili yeni belirsizlikler BQ-17–20 altında ele alınmalıdır.

## Gönderimde beklenen geri dönüş

1. BQ-17–20 için mevcut firmware davranışı ve önerilen RTU akışı.
2. Bildirimlerde mevcut MH/AY davranışıyla çelişen bir tercih varsa
   ilgili bildirim numarası ve değiştirilmesi gereken davranış.
3. Yeni firmware gerektiren konularda sürüm ve geçiş kuralı.

## Açık sorular

### BQ-17 — RTU tek başına yeniden başladığında yeniden kurulum

**Durum:** 09.10.2026 üretici yanıtı alındı. Aşağıdaki metin önceki gönderimin sorusudur.

**Kaynak:** R1 §1.1, §1.10 ve §4.3.

**Kanıt:** MH ayakta ve envanteri yüklüyken yalnız RTU yeniden
başlatıldığında MH tekrar BOOT göndermeyebiliyor. RTU, `MH SCP major: 0`
ve `Envanter: BEKLIYOR` durumunda kalıyor. GET_STATUS ve Powerboard E1
iletişimi devam ederken saat/yeniden envanter/olay çekme/grup işlemleri
BOOT bekliyor. Bu durum 06.10.2026 bench gözleminden sonra 08.10.2026
MH `6dc02267` sürümüyle de görüldü.

Kullanıcının bağımsız MH reseti sonrasında BOOT, TIME_SYNC,
HEAD/CONSUME koruması, altı envanter girdisi ve END tamamlandı;
RTU durumu YUKLU oldu.

**Sorular:**

1. RTU yeniden başladıktan sonra MH BOOT göndermiyorsa hangi güvenli
   yeniden kurulum yolu kullanılmalıdır? GET_STATUS cevabı alınan MH'ye
   RTU saat ve envanter gönderebilir mi; protokol major bilgisi nasıl
   güvenilir biçimde öğrenilmelidir?
2. MH, yeniden başlayan RTU'nun poll'una BOOT veya başka bir kurulum
   bildirimi göndermeli midir? Mevcut firmware'de böyle bir yol var mıdır?
3. RTU restart anında MH'de süren bir config grubu nasıl sorgulanmalı ve
   ele alınmalıdır? MH-restart için tanımlanan FAILED/8 burada geçerli midir?
4. Son grup kimliği RTU restart'ında saklanmıyorsa güvenli sorgulama ve
   yarım işlemden çıkış nasıl yapılmalıdır? Bildirim 1'deki tercihimiz
   bu soruyla birlikte değerlendirilmelidir.

**Mevcut RTU davranışı:** Uyumlu BOOT beklenir; major sürümü tahmin eden
bir fallback (alternatif kurtarma yolu) eklenmemiştir. Bağımsız MH reseti
mevcut bench kurtarma yoludur; kalıcı çözüm olarak sunulmaz.

### BQ-18 — Normal sayaç taşması ile olay deposu resetinin ayrımı

**Durum:** 09.10.2026 üretici yanıtı alındı. Aşağıdaki metin önceki gönderimin sorusudur.

**Kaynak:** R0 BQ-05; HEAD içindeki total u32 ve wrap u16.

**Kanıt:** R0'daki reset karşılaştırması uygulanmaktadır. Ancak wrap
65535→0 olduğunda `total − (wrap × 100 + head)` değişir; total
UINT32_MAX→0 olduğunda total küçülür. Normal taşma da reset koşulunu
sağlayabilir.

**Sorular:**

1. Normal taşmayı depo resetinden ayırmak için hangi modüler
   karşılaştırma kullanılmalıdır?
2. Bu geçişte eski RAM batch'ini (kayıt grubunu) bırakıp güncel tail'den
   okumaya devam etmek yeterli midir? Özel CONSUME kuralı var mıdır?

**Mevcut RTU davranışı:** Verilen reset koşulu uygulanır; eski tüketme
imleci bırakılır ve güncel tail yeniden alınır. Normal taşma için yeni
bir kurtarma algoritması eklenmemiştir.

### BQ-19 — Boot counter sıfırken alarm tekilleştirmesi

**Durum:** 09.10.2026 üretici yanıtı alındı. Aşağıdaki metin önceki gönderimin sorusudur.

**Kaynak:** R0 BQ-03 ile Ek-1 BQ-16.

**Kanıt:** R0, ham kayıt için boot_counter=0 iken tekilleştirme
yapılmamasını ister. Ek-1 ise onaylanmış 101/105 kopyasının alarmı yeniden
açmamasını ister. LIVE açılış sayacı taşımaz; farklı açılışların kayıt
alanları aynı olabilir.

**Sorular:**

1. Boot counter=0 olan 101/105 için alarm düzeyinde event/boot_counter/
   uptime/CRC eşleşmesi kullanılmalı mıdır?
2. Farklı AY açılışlarından gelen aynı alanlı kayıt nasıl ayrılmalıdır?
3. RTU restart sonrasında onay bilgisinin korunması için önerilen kayıt
   kapsamı nedir? Bildirim 6'daki 128 kimlik/RAM tercihi için gerekli
   bir alt sınır veya kalıcılık şartı var mıdır?

**Mevcut RTU davranışı:** Ham kayıtlar tekilleştirilmez. Son 128 alarm
kimliği, EUI ve kabul edilmiş atamayla birlikte RAM'de tutulur. MH
restart'ında korunur; RTU restart'ında sıfırlanır. Mutlak tekilleştirme
veya kalıcı onay geçmişi garantisi verilmez.

### BQ-20 — Eksik olay dizisi ve başka fazdan geç gelen kayıt

**Durum:** 09.10.2026 üretici yanıtı alındı. Aşağıdaki metin önceki gönderimin sorusudur.

**Kaynak:** R0 BQ-10 sınıflaması ve BQ-03 halka kayıp sınırı.

**Kanıt:** RTU bir 3 kaydını çekerken önceki 6/100/101/117 halkadan
silinmiş olabilir. Aynı arızanın diğer fazındaki 1/7 daha sonra MH'ye
gelebilir. HEAD'in boş olması, yalnız o anda MH'de bekleyen kayıt
olmadığını gösterir.

**Sorular:**

1. Önceki dizi bilinmiyorsa 3, sınıflandırılmadan yalnız ham kayıt
   olarak mı saklanmalıdır?
2. Diğer fazın 1/7 kaydı için garanti edilen aktarım sırası veya süre
   sınırı var mıdır?
3. 100/101'in tek kalıcı arıza olduğuna karar vermek için hangi dizi
   kapanış ölçütü kullanılmalıdır?
4. Bildirim 3'teki faz listesini koruma ve fider sayımını ayrı tutma
   yaklaşımı, geç/eksik kayıtta üretici beklentisini karşılıyor mu?

**Mevcut RTU davranışı:** Kanıtı eksik olaylar ham günlükte korunur.
Kesin aynı-arızaya ait olma kanıtı yoksa kesin fider toplamı üretilmez.
Sonradan başka fazdan gelen kayıt yüzünden eski faz listesi kaydı silinmez.

## RTU uygulama bildirimleri

Aşağıdaki tercihler RTU tarafında uygulanmıştır. Bunlar BOLATeX cevabı
olarak sunulmaz. Özellikle Bildirim 1 ve 5, üretici önerisinden ayrılan
ürün tercihleridir; mevcut MH/AY davranışıyla çelişen yönleri belirtilmelidir.

### Bildirim 1 — Son grup bilgisinin kalıcılığı (BQ-11/BQ-17)

Son group_id/fider şimdilik NVRAM'e yazılmaz; RTU startup (başlangıç)
sırasında sıfırlanır. Eski APPLIED sonucu geri yüklenmez. İstenen ayar
NVRAM'de tutulmaya devam eder. Bu tercih, BQ-11'in son grup bilgisini
kalıcı saklama önerisinin ertelenmesidir. RTU-only restart sonrası süren
MH işi için güvenli yol BQ-17 kapsamında açık kalır.

### Bildirim 2 — Kesici açmadan geçen arıza (BQ-10)

4/5→3 dizisinde arada 6 yoksa kayıtlar yalnız ham olay geçmişinde kalır;
geçici arıza listesine eklenmez. 6→3, kalıcı sonuç yoksa geçici arızadır.
1/7 kalıcı faz sonucudur; 100/101 de kalıcı sonuç olarak değerlendirilir.
Aynı açma için önceki 1/7 biliniyorsa 100/101 ikinci faz arızası olarak
eklenmez. 105 yalnız alarmdır. Geçerli ham paket olay sınıfından bağımsız olarak ayrı korunur.

### Bildirim 3 — Faz listeleri ve fider sayımı (BQ-10/BQ-20)

Faz sonuçları mevcut geçici/kalıcı listelerde korunur. Güvenilir saat
kalitesi ve eşleşme bulunduğunda aynı bölge/fiderin aynı sınıftaki
1 saniye içindeki sonuçları fider sayımında birleştirilir. Saat kalitesi
0/2 veya dizi/eşleşme kanıtı eksikse belirsiz sayım kullanılır.

Sayaçlar RTU'nun mevcut çalışma oturumunda işlenen kayıtlara aittir;
RTU restart'ında sıfırlanır. Fider sayımı mekanik kontak konumunu
kanıtlamaz. Fidersiz 117, atanmış-olmayan ayrı sayımda ve ham günlükte
kalır; bir fider/faz listesi uydurulmaz.

100/101 önce gelip başka fazın 1/7'si sonra gelirse aynı sonuç için fider
sayımı yeniden artırılmaz; daha önce saklanan faz sonucu silinmez.
Eksik/geç dizinin kesin kapanış ölçütü BQ-20'de sorulmaktadır.

### Bildirim 4 — Envanter değişimi ve eski olaylar (BQ-09)

Online AY'lerin LIVE log_pending alanı sıfır olana kadar MH kayıt çekimi
sürer. Offline AY canlı bekleme adımını tutmaz. MH halkası boşaltılır;
taze HEAD cevabının total değeri değişim sınırı olarak RAM'de tutulur.
Bekleyen alarmın kalıcı kaydı da tamamlandıktan sonra envanter değişir.

Eski kabul edilmiş atamanın RTU satırı boşaltma sırasında korunur.
Tek 0x06 güncelleme hatasında eski binding değişmez. Tam 0x04/0x05
yükleme PARTIAL politikasını korur; toplu atomik rollback (geri alma)
varsayılmaz. Aktif ayar işlemi, envanter yükleme/boşaltma ve tamamlanmamış
BOOT koruması ilgili çakışan işlemleri engeller.

Atama sonrasında gelen eski etiketli kaydın kimlik belirsizliği sürer.
Sınırdan sonra gelmiş olması kaydın yeni AY'ye ait olduğunu kanıtlamaz;
mevcut 60 bayt olayda EUI özeti varmış gibi davranılmaz.

### Bildirim 5 — Alarmın kalıcı kayıt sonrası otomatik onayı (BQ-16)

Alım onayı kaynağı operatör yerine RTU kalıcı kayıt servisidir. Alarm
kalıcı IEC104 gönderim günlüğüne yazılıp gönderim/replay durumu
senkronlandıktan sonra otomatik receipt acknowledgement (teslim alma
onayı) verilir. Kalıcı yazım/senkronlama başarısızsa onay verilmez.

Güncel LIVE Trip_Failed=1 ise alarm onaylanmış fakat etkin kalır. Yerel
onay SCP komutu üretmez ve MH/AY bayrağını temizlemez. Manuel terminal
onayı kaldırılmıştır. Bu ürün tercihi, BOLATeX'in operatör onayı
anlatımının RTU tarafındaki uygulamasından ayrılır.

### Bildirim 6 — Alarm kopyaları ve RAM kapsamı (BQ-19)

Son 128 alarm kimliği RAM'de tutulur. Olay numarası, bölge/kaynak,
boot_counter, uptime_sec, olay CRC'si ve kabul edilmiş EUI eşleşmesi
kullanılır. Onaylanmış kopya yeniden alarm açmaz; ham 60 bayt yine saklanır.

Bu geçmiş MH restart'ında korunur, RTU restart'ında sıfırlanır. 128
sınırının dışı ve boot_counter=0 kimlik çakışması için mutlak garanti
verilmez. Ham kayıtlar tekilleştirilmez; yeniden okuma/kayıp CONSUME
sonucunda ham kopya oluşabilir.

### Bildirim 7 — SCADA gönderimi, zaman ve kalite

101/105 alarmı ve LIVE alarm geçişleri faz başına ayrı IEC104 M_SP_TB_1
noktasına taşınır. Geçici/kalıcı arıza listeleri ayrıca gönderilir.
Bağlantı açık ve yerel gönderim başarılıysa kayıt sent (gönderilmiş)
işaretlenir. Bağlantı, TX veya k-window (gönderim penceresi) yetersizse
unsent (gönderilmemiş) kalır; sonraki bağlantıda kalıcı replay gönderir.
Replay sonunda güncel RF/alarm durumu tekrar yayımlanır.

Yerel TX başarısı SCADA'nın uygulama düzeyindeki alarm kabulünü
kanıtlamaz. MH CONSUME için SCADA kabulü ayrıca beklenmez; ilgili
kalıcı ham/arıza/alarm ve gönderim kayıtlarının tamamlanması esas alınır.

LIVE bildirimlerinde RTU alım zamanı kullanılır. RTC geçersizse IV=1'dir.
Tarihsel olayın zamanı ve clock_quality bilgisi korunur; kalite 0 ise
RTU işleme zamanı IV=1 ile türetilen listede/alarmda kullanılır. Ham olay
paketi değiştirilmez. İlerlemeyen uptime ölçüm kalitesini düşürür;
RF online bilgisi ayrı tutulur. FSM hata bayrağı tek başına bütün
ölçümleri geçersiz yapmaz.

Varsayılan alarm IOA'ları 1070/1071/1072 + 100×sıfır tabanlı RTU ayar
satır indeksidir; webden değiştirilebilir. Modbus enerji/yük değeri 0/1,
ayrı 49500–49520 kalite bloğu faz başına akım/enerji/yük geçerliliğidir;
geçersiz akım NaN olur. Bunlar RTU ürün adresleridir; SCP wire adresleri
veya BOLATeX standart adresleri olarak sunulmaz.

### Bildirim 8 — Web Kaydet ve sıralı ayar uygulaması

Operatör Kaydet onayı verdiğinde istenen ayar NVRAM'e yazılır. Kayıt
başarısından sonra tüm etkin fiderler otomatik, sıfır olmayan grup
kimliğiyle sırayla uygulanır. Ayrı Uygula/Yalnız kaydet seçeneği yoktur.
Kayıt başarısı APPLIED anlamına gelmez; APPLIED üye bitmap'i ve beklenen
writable (yazılabilir alan) CRC'siyle ayrıca doğrulanır.

Hata veya belirsizlikte sıra durur. PARTIAL/FAILED ayar otomatik
tekrarlanmaz; sebep 6 için aynı ayar ve yeni grup kimliğiyle manuel
yeniden gönderim operatöre gösterilir.

Kaydet sırasında istenen atama, MH'nin ACK ile kabul edilmiş envanteriyle
karşılaştırılır. EUI-64, fider, bölge, RF channel (kanal), etkinlik veya RTU
satırı değiştiyse Bildirim 4'teki eski kayıt boşaltması yapılır, ardından
tam envanter yüklenir. Tam başarıdan sonra ayar sırası başlar. Yükleme
kısmi/hatalı kalırsa ayarlar gönderilmez; sonraki Kaydet eksik envanteri
yeniden yükler. Yalnız koruma eşiği değiştiğinde envanter yüklenmez.
Tüm fiderler kapatıldığında eski envanter boşaltılıp kaldırılır.
Kullanıcı kararıyla envanter boşaltma aşamasında İptal kapalıdır. Bekleme
nedeni, toplam süre ve mevcut nedende geçen süre RTU RAM'inde tutulur;
web yenilemesi bunları sıfırlamaz. RTU kayıt yazımı/alarm kuyruğu ve SCP
meşguliyeti de görünürdür. Süre aşımı kayıt tüketimini veya yeni envanter
gönderimini zorlamaz. RTU restart'ı süreleri sıfırlar; kalıcı zaman kaydı
eklenmedi.

Bölge değişikliğinde §4.3'teki MH yeniden başlatma kuralı geçerlidir.
MH'nin ilk geçerli envanter girdisi bölgeyi belirler; aynı açılışta farklı
bölge `ERROR 0x02` ile reddedilir. Web Kaydet MH'yi otomatik resetlemez.
Yeni bölgeyle çalışmak için MH yeniden başlatılmalı ve etkin envanter
girdileri aynı bölgeyi taşımalıdır. Bu ret üreticiye yeni bir soru değildir.

Yeni ayar doğrulanmış Flash kopyasına yazılamazsa önceki RF RAM ayarı ve
NVRAM RF görüntüsü korunur. Ana A kopyası doğrulanıp yedek B başarısız
olursa yeni ayar korunur; web yedek kayıt hatasını bildirir. Bu durumda
envanter/ayar gönderimi başlamaz. Operatör yeniden Kaydet seçtiğinde
mevcut NVRAM onarımı tamamlanır ve uygulama akışı başlatılabilir.

**COMMIT öncesi toparlanma (BQ-11):** WRITE yanıtı kaybolduktan sonra
operatör aynı satırı ve aynı, henüz COMMIT edilmemiş grup kimliğini seçerek
üç üyeyi baştan yazabilir. RTU üç EUI, bölge ve yazılabilir bloğun ilk işle
birebir aynı olduğunu ve kabul edilmiş envanterle eşleştiğini denetler.
COMMIT sonrası belirsizlik bu yolla yeniden başlatılmaz. Otomatik tekrar,
bilinmeyen kimlikle ABORT veya başka fidere temizlik uygulanmaz. Bu yol
mevcut konsol komutundadır; web Kaydet kilidi korunur. Son grup kimliğinin
RTU restart'ı boyunca kalıcılığına ilişkin Bildirim 1 sınırı değişmedi.

### Bildirim 9 — MH değişimi bakımında tek EPOCH tekrarı (BQ-07)

MH kart değişimi BOOT'tan otomatik çıkarılmaz. Operatör mevcut
`rf epoch N` ile fider bakımını açıkça başlatır. Başarılı EPOCH ACK'inden
sonra en az 90 saniye beklenir; başka fiderin EPOCH'u da bu bekleme
sınırını geçmeden gönderilmez. Sürenin dolması fiziksel RF tamamlanma
kanıtı sayılmaz; sonraki uygulama sonucu ayrıca doğrulanır.

Bu bakımdan sonraki ilk yerel yapılandırma FAILED/reason 5 verirse o
fider için bir EPOCH tekrar edilir. Bu tekrar ayar işlemini yeniden
başlatmaz; ayar sırası durmuş kalır. İlk başarılı sonuç, başka hata,
rutin işlem veya ikinci başarısızlık yeni otomatik EPOCH başlatmaz.
Tek bütçe fider bazındadır; yeniden kurulumda RAM yetkisi temizlenir.

FAILED bitmapindeki sorunlu üyeler kabul edilmiş WRITE sırasındaki EUI
listesiyle eşlenir; bitmap faz numarası olarak yorumlanmaz. İkinci
başarısız uygulama operatör/BOLATeX incelemesi için açık kalır.

### Bildirim 10 — Powerboard ayar tercihleri ve kayıt düzeni

Powerboard tüketicileri SCP E1/E3 modelini kullanır; eski I²C process'i
başlatılmaz. E8 ham telemetri korunur. Kapasite yazımı 7–54 Ah'tır;
kapasite için ayarsız yazma seçeneği yoktur. C-oranı ayarsızsa yankı
kabulü ile fiziksel uygulama doğrulaması ayrı sonuç gösterilir.
Telemetri periyodu yazımı 1–10 s ile sınırlıdır; GET'teki b7 korunur,
ayarsız periyot seçeneği açılmaz.

Taze özette kapasite 7 Ah, durum2 b3:2=0 ve b6:4=3 birlikteyse kapasite
bilinmiyor bakım uyarısı gösterilir. Eski/eksik veriyle uyarı üretilmez;
kapasite doğrulanmadan akü-değişti komutu başlatılmaz.

Tam 60 bayt RF olayı ayrı 8 KB ham günlükte saklanır. Mevcut faz arıza
listeleri korunur; süre alanı 32 bit ms'ye genişletilmiştir. Yeni SCADA
alarm IOA'ları emekli arıza-tipi adres alanını kullanır. NVRAM schema 3,
boyut 2476 bayt ve CRC ofseti 2472'dir. Saha cihazı olmadığından eski
schema için migration (veri taşıma) eklenmemiştir.

Kalıcı IEC104 fault/alarm günlüğü 25 bayt payload (kayıt içeriği) ve
29 bayt entry (günlük girdisi) kullanır. Mevcut 32 KB alan korunur:
sektörde 141, sekiz sektörde 1128 kayıt; sektör dönüşünde 987 kayıt.
Ham RF günlüğü 8 KB kalır; son değişikliklerde yeni Flash/linker alanı
eklenmemiştir. Sonlu günlükler sınırsız arşiv garantisi vermez.

## Yanıtlanmış BQ-01–16 özeti

Bu konular için R0/Ek-1 cevapları alınmıştır. Aşağıdaki özet, açık
soruların ve bildirimlerin bağlamıdır; yeni cevap talebi değildir.

| BQ | Alınan cevabın konusu ve RTU durumu |
|---|---|
| 01 | Geç gelen 101/105 alarm kabulü; güncel uygulama Bildirim 5–7'de açıklandı. |
| 02 | MH halkası en çok 99 bekleyen kayıt; head=tail boş halka. |
| 03 | Açılışta HEAD/mevcut tail koruması; her CONSUME öncesi taze HEAD ve sıra kontrolü; ERROR 06/02 özel yolları. Atomik/koşullu tüketme gelecekteki değişiklik olarak tutulur. |
| 04 | İstek dışı SET bildirimine ACK üretilmez; örnekler request/response neden sırasıyla değerlendirilir. |
| 05 | Olay deposu resetinde total/konum karşılaştırması; normal taşma BQ-18'de açık. |
| 06 | Kısmi uygulama olayı tek başına APPLIED kanıtı değildir; sebep 6'da aynı ayar/yeni kimlik yönlendirmesi. |
| 07 | MH değişimi EPOCH'u ve 90 s/fiderler arası bekleme; tek istisna ilk FAILED/5 sonrası bir tekrar. Bildirim 9. |
| 08 | Powerboard SCP kaynağı ve eski I²C yolunun kapalı tutulması. |
| 09 | Atama değişiminden önce eski kayıtların boşaltılması; geç eski etiket belirsizliği sürer. Bildirim 4. |
| 10 | Diziye göre geçici/kalıcı sınıflama, faz listeleri ve fider sayımı. Eksik/geç dizi BQ-20'de açık. |
| 11 | Sıfır group_id COMMIT için kullanılmaz; hedef fider/üyeler doğrulanır. Son grup kalıcılığı önerisinin ertelenmesi Bildirim 1'de açıklandı. COMMIT öncesi ABORT gelecekteki MH özelliği sayılmaz. |
| 12 | Kapasite/C-oranı için yankı ve taze E1 doğrulaması; kapasite-bilinmiyor bakım durumu. Bildirim 10. |
| 13 | E6 ACK ile E7 sonuç ayrımı, CMD 05 eşlemesi, yanıtsız bitiş ve cancel sonrası GET. |
| 14 | Periyot b7 korunur; normal 1–10 s tercihi kullanılır. |
| 15 | LIVE alım zamanı ve kalite; sabit uptime'da geçersiz ölçüm. Bildirim 7. |
| 16 | Alarm onayının kapsamı; otomatik RTU alım onayı ürün tercihi olarak Bildirim 5'te açıklandı. |

## Doğrulama ve sınırlar

Merkezi birim testleri 1014/1014 geçti. Kaynak/gömülü web, gerçek HTTP
handler kontrolleri, 26 modülün strict (sıkı uyarı kontrollü) Cortex-M33
C11 derlemesi ve Release paket self-check'i geçti.

Kayıtlı PC simülatörü HIL (donanımın test döngüsüne katılması) koşusunda
35/35 vaka PASS; paket kimliği her vaka öncesi/sonrası doğrulandı. Gerçek
MH ile BOOT, saat eşitleme, altı envanter girdisi ve Powerboard E1/E3
iletişimi ayrıca gözlendi. Bu sonuçlar gerçek AY'nin RF LIVE/APPLIED veya
fiziksel açma doğrulaması, SCADA uçtan uca kabulü ve enerji kesintisi
kabulünün yerine geçmez. BQ-17–20 bu sonuçlarla kapanmış sayılmaz.

08.10.2026 gerçek web/MH testinde EUI değişimi ve geri dönüşü, fider 1 → 4
değişimi, yalnız akım eşiği değişimi ve hatalı bölge sonrası toparlanma
izlendi. Envanter değişimlerinde taze HEAD, altı ACK ve END'den sonra
WRITE/COMMIT başladı; yalnız eşik değişiminde envanter yüklenmedi.
Bölge ret durumunda ayar gönderimi başlamadı. Yeni ayar grupları
NOT_LIVE (reason=1) ile sonuçlandı; APPLIED elde edilmiş sayılmadı.
Test sonunda 105 web form alanı taze cihaz okumasıyla başlangıç değerleriyle
aynı bulundu. Kalıcı kayıt hata enjeksiyonu host testindedir; fiziksel
Flash arızası/enerji kesintisi testi yapılmadı.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 05–07.10.2026 | 0.1–0.12 | İlk sorular, BQ-01–16 cevap takibi, BQ-17–20 ve RTU kararları toplandı. |
| 08.10.2026 | 0.13 | Web Kaydet/sıralı uygulama ve MH değişimine özel tek EPOCH tekrarının özeti eklendi. |
| 08.10.2026 | 0.14 | Tek başına gönderilecek belge: dört açık soru öne alındı; on ayrıntılı bildirim aynı dosyaya taşındı; cevaplanmış sorular özetlendi, diğer rapor bağımlılıkları kaldırıldı. |
| 08.10.2026 | 0.15 | Bildirim 8: web atama değişikliğinde boşaltma/envanter/ayar sırası ve kalıcı kayıt hata ayrımı eklendi. |
| 08.10.2026 | 0.16 | Gerçek web/MH test kapsamı ve §4.3 bölge değişikliği için MH reset kuralı eklendi. |

| 08.10.2026 | 0.17 | Bildirim 8: BQ-11 aynı üyelerle açık WRITE yeniden denemesi ve sınırları eklendi. |

| 08.10.2026 | 0.18 | Bildirim 8: iptal olmadan RTU RAM bekleme nedeni ve süre gösterimi. |
