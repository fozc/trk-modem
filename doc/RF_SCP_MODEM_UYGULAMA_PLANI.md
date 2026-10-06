# RF-SCP R1 modem uygulama planı

**Tarih:** 05.10.2026
**Son güncelleme:** 06.10.2026
**Durum:** Kullanıcı R1 davranışlarının uygulanmasını onayladı.
İlk beş adımın codec, istek, açılış/envanter ve canlı veri servisleri
tamamlandı. RF web monitorü güncel modele geçti. Altıncı adımda otomatik
olay çekme, 8 KB ham kayıt ve mevcut geçici/kalıcı arıza listesine aktarım
bağlandı. IEC104 spontane/replay aktarımı eklendi. 101/105'in belirsiz
geç kayıt davranışı BOLATeX cevabını bekliyor. Normal grup ayarı sıralayıcısı
ve sonuç doğrulaması eklendi; COMMIT öncesi belirsiz temizleme BOLATeX
cevabını bekliyor. İstenen ayarın Kaydet ile kalıcı tutulması ve Uygula
işleminin ayrı olması kullanıcı kararıyla netleştirildi. Powerboard özet/alarm tüketicileri SCP
kaynağına geçirildi. Powerboard müşteri ayar/komut servisi ve ham teşhis
bağlandı; BOLATeX teyitleri ve kalan ürün bağlantıları açıktır.

## Amaç

BOLATeX R1 arayüzünü STM32 modem tarafına, mevcut SCP ve RF modülleri
üzerinden eklemek. Her adımı üretim kodunu çalıştıran Ceedling testleriyle
doğrulamak. Bu belge firmware veya ürün davranışını değiştirmez.

## Kullanım yeri

Uygulama sırasının ve açık kararların değerlendirilmesinde kullanılır.
Ana kaynak [R1 arayüz belgesidir](BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/MH_STM32_RTU_SCP_Arayuzu_R1.md).
Bu belgedeki MH, RTU'nun UART üzerinden konuştuğu RF hub'dır.
Modem firmware'i RTU rolündedir; AY ve MH'nin kendi RF işlemleri burada
yeniden uygulanmaz.

## Kapsam

[KARAR: 2026-10-05] Kullanıcı sahada cihaz olmadığını ve eski RF modeline
geriye dönük uyumluluk gerekmediğini bildirdi. RF veri modeli ve monitor
JSON'u son dokümana göre kurulmuştur; eski alan/API adapter'ları tutulmaz.
Bu karar güncel protokolün istediği olay/alarm kayıtlarını silmek anlamına
gelmez. İleride kalıcı düzen değişirse mevcut layout/schema kuralları
uygulanır; eski saha görüntülerine migration (veri taşıma) gereksinimi yoktur.

R1'de tanımlı sistem, envanter, zaman, canlı veri, açma, anomali, olay,
ayırıcı yapılandırması ve PWRB aileleri kapsamdadır. Desteklenmeyen
`0x03` ve `0x20` komutlarının hata davranışı da test edilir.

R1 §8'de planlanan OTA komutları, gelecekteki alan bazlı ayarlar,
fider 5–7 RF hizmeti ve gelecekteki alarm/komutlar uygulanmaz.
Mevcut RFWU, R1'de henüz tanımlanmamış AY/MH OTA ailesinden ayrıdır.
`0xE8` ham bloğunun alan düzeni R1'de verilmemiştir; bu aşamada ham
96 bayt olarak taşınması önerilir. Mevcut I²C decoder'ının aynı biçimi
çözdüğü varsayılmaz.

## Mevcut durum ve kanıt

Kaynaklar 05.10.2026 tarihinde okunmuştur. Bu çalışma sırasında test
ve hedef derlemesi yapılmamıştır; aşağıdakiler kaynak incelemesidir.
Depodaki mevcut kullanıcı değişiklikleri korunmuştur.

| Alan | Kaynakta görülen durum | Plan için sonucu |
|---|---|---|
| Hat | `Core/Src/main.c`, `MX_USART3_UART_Init`: 230400 baud | R1 hızına uygun; yeni UART kurulumu gerekmez |
| Çerçeveleme | `Application/libscp/scp.h/.c`: SCP 1.0.0, COBS, CCITT-FALSE, 244 B payload, 100 ms alım boşluğu kontrolü | Mevcut katman korunur; R1 örnekleriyle sınanır |
| Kaynak adresi | `scp.c` sıfır SRC'yi reddeder; `0xFF` için ret yoktur. RF yanıt eşleşmesi CMD/SEQ ile sınırlıdır | RF sınırında MH kaynak/adres doğrulaması gerekir |
| İstek | `rf_comm.c`: tek aktif istek, timeout (zaman aşımı), aynı SEQ ile retry (yeniden deneme), ortak iki retry bütçesi | Yeni bağımsız retry katmanı kurulmaz; R1 hata ayrımı eklenir |
| NOT_AVAILABLE | `scp_on_response`: `0x05` sonrası aynı SEQ ile hemen yeniden gönderir | R1 §2.3/§6.1'e göre hata yanıtından sonraki yeni istek yeni SEQ taşımalıdır; komuta göre politika gerekir |
| Açılış | BOOT → TIME_SYNC → envanter mevcut. Sürüm farklıysa uyarı verip devam eder. Saat başarısızsa envantere geçmez | Sürüm uyuşmazlığı ve geçersiz RTC davranışı karara bağlanmalıdır |
| Envanter | `rf_inventory.c`: hatalı girdiyi atlar; END ACK alınca boş/kısmi durumda da loaded yapabilir | Yerel başarı durumu ile MH'nin yüklenmiş durumu ayrılmalıdır |
| Channel | Envanter gövdesi channel alanını 0 gönderir; kaynak TODO'su vardır | R1 bu alanın bilgi amaçlı olduğunu açıklıyor; 0 veya mevcut ayar kaynağı seçilir |
| Bildirim | `handle_proactive`: yalnız BOOT ve DISCOVERY işlenir. Keşifte en az 8 B yeterli görülür, RSSI tutulmaz | Diğer bildirimler ve tam uzunluk doğrulaması eklenmelidir |
| Ayar modeli | `rf_types.h`: 96 B blok ve layout assert'leri; `rf_config.c`: RAM/NVRAM store ve staging mevcut | Veri modeli topluca değiştirilmeden yeniden kullanılır |
| Yazma bloğu | `rf_config_for_write`: cihaz bloğu yokken varsayılan Fider_ID=0 kalır; writable kopyası @3–56'dır | R1 §4.10 uyarınca hedef fider açıkça yazılmalıdır; üç üyeye aynı blok gider |
| Ayar CRC | Tam blok CRC ve @3–56 writable CRC ayrı yardımcılarla hesaplanır | R1 cfg_crc alan sırası örneklerle doğrulanır; iki CRC birbirine karıştırılmaz |
| Canlı model | `rf_monitor_t`: bazı ölçümler dar/tamsayı alanlarda; RSSI unsigned; R1 alanlarının tamamı yok | R1 snapshot ve mevcut tüketiciler arasındaki eşleme onaylanmalıdır |
| Dummy | `app_main.c` tick çağırır; init çağrısı yorumdadır. `rf_dummy_tick` initialized değilse döner | Dummy silinmez; gerçek veriyle üretici sahipliği açık seçilir |
| Olay aktarımı | `fault_log_t`: 16 bit süre, sınırlı arıza alanları. `iec104_report_fault_event`: void; gelen olay zamanını parametreyle almıyor ve tüm kalıcı yazma sonuçlarını bildirmiyor | 60 B kayıt için doğrudan kullanım kayıpsız değildir; tüketme öncesi başarı ölçütü gerekir |
| Güç kartı | `power_board_init` aktif; telemetri mevcut I²C slave'den alınır. `system_status` ve `modbus_power_stats` bu API'yi kullanır | Kullanıcı kararı: PWRB bildirimleri artık MH üzerinden SCP ile gelir; mevcut tüketiciler bu kaynağa geçirilir |
| Test | Ceedling 1.0.1; `test/scp`, `test/rf`, ortak destek kaynakları, merkezi runner ve `rf_hub_sim` paketi mevcut | Yeni protokol testleri merkezi düzene eklenir |
| HAL | Yerel `stm32u3xx_hal.h` sürümü 1.4.0 | Donanım sınırları mevcut sürüm üzerinden incelenir; vendor değişikliği önerilmez |

## Kurallar

[KARAR: 2026-10-05] Kaynak, API ve test adları belge sürümünü
taşımamalıdır. Modül adı `rf_scp_codec`, API adları
`rf_scp_build_packet`, `rf_scp_decode_message`, `rf_scp_decode_event`
ve `rf_scp_validate_config` olarak kullanılmalıdır. R1 bilgisi kaynak
protokolün sürüm atfıdır; ürün işlevinin adı değildir.

1. İstekler mevcut tek aktif istek mekanizmasını kullanmalıdır. Bir UART
   timeout tekrarında aynı paket ve SEQ korunmalıdır. ERROR sonrasındaki
   yeni girişim R1 kurallarına göre yeni SEQ kullanmalıdır. Kalıcı hatalar
   tekrar edilmemelidir. `0x20` bu sürümde otomatik sorgu döngüsüne alınmamalıdır.
2. R1 SET bildirimlerine ACK gönderilmemelidir. Bildirimler aktif istek
   sırasında da işlenmelidir. PING'in boş ACK davranışı korunmalıdır.
3. Decode (baytlardan çözümleme) sınırında uzunluk, kaynak, sürüm ve anlamlı
   alanlar doğrulanmalıdır. Hatalı paket önceki geçerli durumu bozmamalıdır.
   Ayrılmış alanlar R1 kurallarına göre yok sayılmalıdır.
4. Durum makineleri Contiki process bağlamında ilerlemelidir. UART ISR
   yalnız mevcut alım yolunu beslemelidir. Paylaşım değişirse gerçek
   ISR/process erişimleri ve mevcut ring buffer koruması önce incelenmelidir.
5. Heap kullanılmamalıdır. İlk uygulama adımında statik RAM, stack ve
   Flash maliyeti ölçülmeli; yeni tampon/kuyruk kapasitesi gerekçelendirilmelidir.
6. NVRAM, Flash yerleşimi ve public API değişiklikleri ilgili kullanıcı
   kararı olmadan yapılmamalıdır. Git commit/push ayrı yetki gerektirir.
7. Yerel kayıt zamanı ve R1 clock_quality bilgisi korunmalıdır. Kaynak
   zamanı geçersizse kayıt atılmamalı; merkeze aktarımda R1 IV kuralı
   uygulanmalıdır. `Line_ID=0` kayda sonradan fider uydurulmamalıdır.
8. Alarm, tanı, konum ve arıza akımı birbirine karıştırılmamalıdır.
   TRIP_NOTIFY akımı ayrıntılı arıza akımının yerine kullanılmamalıdır.
   LIVE_DATA Trip_Failed bayrağı konum doğrulaması sayılmamalıdır.

## Uygulama adımları

Her adım ayrı incelenebilir değişiklik olarak hazırlanır. İlgili testler
aynı adımda eklenir; kullanıcı kabulünden sonra sonraki adıma geçilir.
Dosya adları öneridir, public API tasarımı bu planla kesinleşmez.

| Adım | İş ve mevcut altyapı | Ceedling kapsamı | Tamamlanma ölçütü |
|---|---|---|---|
| 1 | R1 komut/uzunluk haritası; mevcut SCP/RF davranışının karakterizasyonu; CSV fixture (sabit test verisi) hazırlığı | `test/scp`, `test/rf`; CSV frame'lerinin COBS/CRC/başlık/payload kontrolleri | Örnekler üretim parser'ından geçer; çelişkiler listelenir; başlangıç test ve bellek durumu kaydedilir |
| 2 | `rf_scp` saf codec'lerinin genişletilmesi: tüm R1 istek gövdeleri, yanıtlar, SET bildirimleri, endian ve sürüm kontrolleri | Her codec için normal/sınır/hatalı uzunluk/null; çıktı durumunun korunması; 60 B iç CRC ve 96 B ayar | Tüm R1 alanları doğru birim ve türle çözülür; üretim girişleri test edilir |
| 3 | `rf_comm` yanıt kaynağı ve CMD/SEQ/TYPE denetimi; komut bazlı timeout/retry; SET dispatch; bridge davranışı | Kayıp ACK, geç ACK, yanlış kaynak, yeni SEQ, ortak retry bütçesi, wraparound, bildirim sırasında istek | Bir işlem diğerinin yanıtını tüketmez; retry sınırlıdır; bildirimlere ACK yoktur |
| 4 | Açılış, periyodik saat ve envanter zinciri; kısmi/boş envanter; keşif RSSI; `0x06` ekleme/silme; `0x2A` servis akışı | BOOT tekrarları, işlem ortasında restart, major farkı, geçersiz RTC, aynı bölge, boş/kısmi envanter, fider/faz/EUI eşlemesi | MH açılışı sonrası onaylanan politika ile zincir tamamlanır; yerel loaded durumu gerçeği yansıtır |
| 5 | `0x11`, `0x10`, `0x12`: canlı snapshot, iletişim var/yok, alarm ve anomali; mevcut monitor/tüketici bağlantısı | Fider 1–4, 3 bit çözümleme, fazlar, signed RSSI, float/birimler, geliş aralığı, reboot ve Trip_Failed, reset bildirimi | Gerçek veri doğru yuvayı günceller; eski veri ve bilinmeyen alanlar sağlıklı veri gibi gösterilmez |
| 6 | `0x40/42/44/46/47` olay çekme; `0x02` FRAM tanısı; kayıt doğrulama, aktarım ve onaylanan consume (tüketme) koşulu | 0–4 kayıt, kısa batch, 99→0 sarma, iç CRC hatası, `0x06`, BUSY/degraded, aktarım/yazma hatası, kayıp bildirim, yeniden başlama ve tekrar kayıt | Başarısız teslimde tail ilerlemez; bilinmeyen olay korunur; hatalı yuva politikası gözlenebilir |
| 7 | Ayırıcı grup ayarı: 22 alan doğrulama, hedef fider, üç aynı blok, WRITE×3→COMMIT→STATUS/GET; ABORT, EPOCH; RAM/NVRAM durumları | Tüm aralıklar ve bağımlı eşikler; NaN/Inf; ACK≠APPLIED; kayıp status, yanlış group, kabul sırasına göre bitmap, partial commit ve MH restart | Hedef fider doğru; APPLIED ve cfg_crc doğrulanmadan uygulanmış denmez; kısmi durum açık gösterilir |
| 8 | `0xE1/E3`: SCP kaynaklı güç snapshot, tazelik/geçerlilik, alarm maskesi, RESET ve son nefes; mevcut I²C kaynaklı üretim tüketicilerinin geçiş hazırlığı | 39/11 B, ver farkı, signed ölçümler, doygun yaş, stale bayrakları, oturum ve tekrar last gasp, kayıp alarmın özetle toparlanması | Geçersiz/bayat değerler geçerli ölçüm sayılmaz; aynı oturum last gasp iki kesinti üretmez; eski kaynak yeni veriyi ezmez |
| 9 | `0xE5/E6/E7/E8`: GET→GEN ile SET→echo/özet doğrulama; kapasite sonrası akü değişti; komut sonuç takibi; ham tanı | GEN uyuşmazlığı, ret maskeleri, b7 koruma, endian istisnası, yankı bekleme, belirsiz/iptal/geç sonuç, sıra eşleşmesi | ACK başarı sonucu sayılmaz; yalnız izinli müşteri alanları yazılır; servis komutları gönderilmez |
| 10 | Shell/web bağlantıları, IEC104/Modbus eşlemeleri; uçtan uca host akışları; simülatör R1 uyumu; rapor ve test rehberi | Mevcut RF JSON/IEC104/Modbus regresyonları ve paketler; CSV akışları ile birleşik senaryolar | Merkezi host koşusu geçer; ARM derlemesi ve bellek farkı raporlanır; kalan cihaz kabulü ayrıca listelenir |

Adım 7'deki ayar builder düzeltmesi adım 2'de hazırlanabilir; cihazlara
ayar uygulama zinciri ve kalıcılık kararı adım 7'de tamamlanır.
PWRB codec'leri de adım 2'de test edilir. Adım 8'de SCP veri kaynağı
hazırlanır; adım 10'da durum, Modbus, IEC104 ve web tüketicileri
onaylanan alan eşlemeleriyle bu kaynağa bağlanır.

### Powerboard veri kaynağı geçişi

[KARAR: 2026-10-05] Powerboard bildirimleri RTU'ya MH üzerinden SCP
ile gelecektir. RTU'da ikinci bir doğrudan I²C telemetri kaynağı veya
otomatik kaynak fallback'i (yedek kaynağa geçiş) tasarlanmaz.

`0xE1` özet, `0xE3` alarm ve `0xE7` komut sonucu aynı SCP dispatch
yolunda işlenir. R1 §5.7'deki isteğe bağlı unsolicited `0xE8` ham
telemetri de ACK verilmeden kabul edilir; ham blok özetin yerine
karar verisi olarak kullanılmaz. `0xE5`–`0xE8` istekleri adım 9'dadır.

Mevcut `system_status`, `modbus_power_stats` ve diğer Powerboard
tüketicileri alan, birim, tazelik ve geçerlilik açısından incelenmelidir.
SCP'de karşılığı olmayan eski alanlara değer uydurulmamalıdır. Alan
eşlemesi ve eksik alan davranışı ürün bağlantısı adımında belirlenmelidir.
Eski I²C process'inin aynı ölçüm/alarm durumunu güncellemesi önlenmelidir;
bu geçiş eski kaynak dosyalarının silinmesini gerektirmez.

Ceedling testleri SCP bildirimi sonrası gerçek tüketici çıktısını,
kayıp/bayat telemetriyi, desteklenmeyen sürümü, alarm RESET'ini ve
yinelenen son nefes/komut sonucu bildirimlerini doğrulamalıdır.

## Test akışları

Örnekler [örnek akış dizininden](BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/Ornek_SCP_Akislari/BENIOKU.md)
alınır. CSV (noktalı virgülle ayrılmış kayıt) test üretiminde kullanılır;
gerekli ham frame'ler Ceedling'in okuyacağı sabit fixture'lara dönüştürülür.
Dönüşüm aracı `test/scripts`, fixture'lar `test/fixtures` altında tutulur.
Orijinal kayıtlar değiştirilmez. Veri tabloları üzerinde analiz bu
planın amacı değildir; ham protokol örnekleri kullanılır.

| Örnek grubu | Testteki kullanımı |
|---|---|
| AY_01, AY_02, AY_03 | Açılış/envanter, keşif ve saat |
| AY_04, AY_05a, AY_08 | Canlı veri, açma ve anomali |
| AY_05b | Olay head/range/consume ve iç CRC |
| AY_06, AY_06b, AY_07 | Grup yazımı, önceki ayara dönüş ve epoch |
| PWRB_01, PWRB_02 | Özet ve alarm |
| PWRB_03, PWRB_04 | Ayar GEN/yankı ve komut sonuçları |
| tlm_ay_live, tlm_pwrb, olay_zaman_cizelgesi | Birim ve olay beklentileri için yardımcı karşılaştırma; tek başına wire oracle sayılmaz |

**Örneklerin sınırı:** AY_05b içinde RTU'nun `0x47` bildirimine ACK'i
vardır; R1 §2.2/§6.2 bildirimlere yanıt verilmemesini ister. Ayrıca kayıt
zamanları istek/yanıt nedenselliğini her satırda yansıtmaz: range isteği,
head yanıtından önce görünür. Bu nedenle her satırın CRC/decode doğruluğu
ayrı sınanır; işlem testi R1 nedensel sırasıyla yapılır. Gözlenen ACK,
firmware için beklenen davranış olarak kopyalanmaz.

AY_05a ve AY_05b aynı olaya ait değildir; bu iki dosyadan duplicate
(tekrar kayıt) ilişkisi çıkarılmaz. PWRB_04 bilerek geçersiz PARAM=0
kullanır; beklenen sonuç 0x02'dir. Geçerli PARAM=0xA5 akışı için ayrıca
test oluşturulur. Gönderimde maskeli alan/CRC için R1'in izin verdiği
birden fazla bayt düzeni vardır; örneğe tam eşitlik yalnız seçilmiş aynı
kodlama politikası için aranır, semantik uyumluluk ayrıca denetlenir.

Testler gerçek `scp_process_byte`, RF dispatch, işlem ilerletme ve
observable state (dışarıdan görülen durum) üzerinden çalışmalıdır.
UART/tick/Flash/merkez bağlantısı taklit edilir; doğrulanan algoritma
taklit edilmez. Gönderilecek istek için beklenen baytlar test edilen
codec'ten yeniden üretilerek karşılaştırılmaz.

Her adımda ilgili Ceedling dosyaları çalıştırılır. Birleşik değişikliklerde
`ruby test/run_all.rb unit` ve ilgili integration paketleri çalıştırılır.
Son kabulde `ruby test/run_all.rb` kullanılır. Coverage raporu yeni
başarısızlık dallarının görülmesi için değerlendirilir; tek başına
yüzde hedefi veya fiziksel doğrulama sayılmaz.

## Açık kararlar

| ID | Karar ve seçenekler | Öneri | Etkilenen adım |
|---|---|---|---|
| K1 | [KARAR: 2026-10-05] MH modem üzerindeki RF hub'dır; STM32 RTU tarafındadır. RTU–MH arayüzünde yalnız SCP kullanılır; PWRB bilgisi de buradan gelir | İki taşıma profili tasarlanmaz. Mevcut I²C üretim tüketicilerinin geçişi ilgili adımda incelenir; bu karar dosya silme yetkisi değildir | 8–10 |
| K2 | [KARAR: 2026-10-05] Önce RF-SCP servisleri ve shell, ardından web/IEC104/Modbus bağlantıları | Kullanıcı uygulama sırasını ve R1 uygulamasını onayladı; ilk dört adım tamamlandı | 5, 6, 10 |
| K3 | [KARAR: 2026-10-05] RTU gönderdiğinde veya kayıt yaptığında olay tüketilebilir | Karşı taraf ACK'i ayrıca beklenmez. Başarılı gönderim ya da başarılı kalıcı kayıt doğrulanmadan `0x46` gönderilmez; yalnız RAM kopyası kalıcı kayıt sayılmaz | 6 |
| K4 | Kullanıcı eski yapının R1'e uymasını istedi. Wire kayıt modeli 60 B R1 düzenini esas alır. Tüm olayların kalıcı saklanması ve üst protokol eşlemesi henüz kararlaştırılmadı | Arıza dışındaki olaylar da çözülür; mevcut dar arıza modeli wire sözleşmesi olarak kullanılmaz. Flash yerleşimi ve migration ayrı karar olarak açık kalır | 6, 10 |
| K5 | [KARAR: 2026-10-06] Kaydet istenen ayarı hemen NVRAM’e yazar; Uygula ayrı başlatılır. APPLIED ayrı RAM işlem sonucudur. PARTIAL/FAILED otomatik yeniden uygulanmaz | NVRAM düzeni değişmez; reset sonrasında istenen ayar yüklenir, uygulanmış sonucu varsayılmaz | 7, 10 |
| K6 | [KARAR: 2026-10-05] Kullanıcı, geçersiz RTC'de saatin atlanıp envanterin yüklenmesini; saat geçerli olunca eşitlenmesini seçti | Uygulandı. Uyumlu BOOT/major ve boş/kısmi envanter durumu R1'e göre işlenir; sonraki saat ACK'i envanteri yeniden başlatmaz | 4 |
| K7 | Başlangıç zamanları ve retry sayısının anlamı | R1 §6.2; 'yineleme' ek retry olarak açık tanımlansın. Canlı veri kaybı 30 s, TIME_SYNC 1 saat, bildirim telafisi için LOG_HEAD 60 s önerisi | 3–6 |
| K8 | Discovery atama/silme ve MH değişimi/epoch tetiklemesi | Operatörün mevcut envanter atamasıyla güncelleme; otomatik faz/fider ataması yok. MH değişimi operatör bildirimiyle EPOCH_REFRESH | 4, 7 |
| K9 | Alarm/olayların IEC104 IOA ve Modbus adresleri; Trip_Failed latch (alarmı açık tutma) ve operatör onayı kanalı | Mevcut adresler korunur; R1'deki yeni bilgilerin adresleri ve onay yolu ayrıca kararlaştırılır | 5, 8, 10 |
| K10 | Fixture ACK çelişkisi ve kayıt sırası için yetkili ölçüt | Mevcut uygulama için R1 metni; cihaz firmware'i farklıysa BOLATeX açıklaması gerekir | 1, 3 |

K7'de LOG_HEAD 60 s RTU için bu planın önerisidir; R1'de zorunlu RTU
poll periyodu olarak verilmemiştir. Timeout ve işlem sonuç bekleme süreleri
ayrıdır. TIME_SYNC periyodik yenilemesi her seferinde envanteri tekrar
yükleyen mevcut callback'e doğrudan bağlanmaz.

### Kararların açıklaması

Olay kaydı yalnız arıza kaydı değildir. R1 §4.7/§4.8; koruma/açma,
hat enerjilenmesi, atama, ayar uygulama, yeniden başlama, ölçüm/RF tanısı,
depo sıfırlaması ve servis erişimi olaylarını aynı 60 B düzende tanımlar.
Kayıt olay kimliği, kaynak, zaman/clock_quality, ölçümler, sayaçlar,
boot_counter, uptime ve iç CRC taşır. Alanların anlamı olay kimliğine
göre değişir. Bilinmeyen olay atılmaz. PWRB alarm geçmişi §5.3'te ayrı
tanımlıdır; MH'nin 60 B olay deposuyla aynı kayıt türü olduğu varsayılmaz.

K5, ayırıcıya gönderilecek koruma ayarının RTU'da ne zaman kaydedileceği
ile ilgilidir. R1 §4.10 RTU'nun kendi kalıcı ayarından blok üretmesini
önerir; kullanıcı Save işleminin NVRAM zamanını tanımlamaz. MH'nin WRITE
ve COMMIT ACK'i ayarın ayırıcıda uygulandığını kanıtlamaz. Bunun sonucu
APPLIED/FAILED ile gelir. İstenen ayar ile doğrulanmış uygulama durumu
ayrılmalıdır; reset ve PARTIAL_COMMIT politikası kullanıcı kararıdır.

K6/K7 için kaynak ayrımı: R1 §4.3 major farkında envanter göndermemeyi
önerir ve boş envanterin loaded sayılmayacağını tanımlar. §4.4 IV=1 saati
reddeder; RTC geçersizken envantere devam etme davranışı kullanıcı
kararıyla uygulanmıştır. §4.5 iletişim kesintisi için en az 30 s, §4.4 saat yenilemesi
için 1 saat önerir. §6.2 timeout/retry tablosu başlangıç önerisidir.
RTU'nun LOG_HEAD sorgusu için 60 s bu planın önerisidir; MH'nin LOG_AVAILABLE
bildirimini en geç 60 s'de yinelemesi ise §4.6'da tanımlıdır.

K9 yalnız ekran gösterimi değildir. Alarm state (durum) takibi, olay
geçmişi ve IEC104/Modbus yayını da kapsar. R1 §4.5, ayırıcı yeniden
başladığında Trip_Failed 0 olsa bile 101/105 alarmının operatör onayına
kadar açık tutulmasını tarif eder. RF iletişim yok, FSM iç hata,
anomali, yapılandırma başarısız/kısmi uygulama, MH depo sıfırlaması,
PWRB seviye alarmları ve son nefes gibi başka bilgiler de vardır.
R1 bu bilgilerin RTU web alanlarını, IOA/register adreslerini veya
operatör onayı arayüzünü tanımlamaz. Bunlar son ürün bağlantısı adımında
karara bağlanabilir.

## Kalan sınırlar

### İlk adımın doğrulaması — 05.10.2026

`test/scripts/generate_rf_scp_vectors.py`, 14 senaryo CSV'sinden
175 ham çerçeveyi `test/fixtures/rf_scp_vectors.h` içine alır.
`--check` ile fixture güncelliği kontrol edilmiş; bütün çerçeve CRC'leri
ve örnekte bulunan 60 B olayların iç CRC'leri doğrulanmıştır.

`test/scp/test_scp_capture_scenario.c` içindeki 6 Ceedling testi
geçmiştir. Üretim SCP parser/encoder'ı, yakalanan baytlarla çözümleme,
yeniden kodlama, her parçalama noktası, konsol metni, arka arkaya alım,
bozuk CRC ve 100 ms yarım çerçeve toparlanması için sınanmıştır.
175 vektör ve parçalama döngüleri ayrı Unity test sayısı değildir.

Bu adımda firmware kaynağı değiştirilmemiştir. Yeni protokol servislerinin
RAM/stack/Flash ölçümü ilk üretim değişikliğinde yapılacaktır. R1
bildirim işleyicileri, olay tüketimi, ayırıcı yapılandırma işlemleri ve
PWRB tüketici geçişi bu sonuçla tamamlanmış sayılmaz. Kullanıcı talebi
R1'de tanımlanan davranışlar için uygulama yetkisidir; dokümanın
tanımlamadığı kalıcı veri yerleşimi ve merkez adreslerini kesinleştirmez.

### İkinci adımın doğrulaması — 05.10.2026

`Application/rf/rf_scp_codec.c/.h`, R1 isteklerini doğrulayıp MH/RTU
adresleriyle kurar; ACK/ERROR/SET payload'larını çözer. Komut sabitleri
ve codec hata sonuçları mevcut `rf_scp.h` içinde genişletilmiştir.
Mevcut API'ler korunmuştur. Codec durumsuzdur; heap veya statik RAM yoktur.

Olay kayıtlarının tüm tanımlı alanları, 32 bit süre, bilinmeyen olay
kimliği ve geçersiz saat bilgisi korunur. Tanı kaydındaki NaN ölçüm
atılmaz. Batch kayıtları ham tutulur; her yuvanın iç CRC'si ayrı event
decoder'ıyla sınanır. Bozuk bir yuvanın diğer kayıtları silmemesi olay
çekme servisinin sorumluluğundadır. Henüz consume işlemi eklenmemiştir.

PWRB özetinin signed (işaretli) ölçümleri ve birimleri, geçerlilik/yaş
bayrakları, alarm/komut sonuçları ve CFG2 yankı alanları çözülür.
CFG2 içindeki big-endian baytlar ve `0xE8` ham bloğu aynen korunur.
Codec bu bilgileri mevcut Powerboard tüketicilerine henüz yayınlamaz.

Gönderim öncesi 22 ayırıcı ayarı, fider, envanter, saat, olay indeksleri
ve müşteri PWRB maskesi doğrulanır. PWRB servis komutları gönderilmez.
Geçersiz örnek PARAM=0 reddedilir; akü değişti için PARAM=0xA5 gerekir.
CFG2 ilk-yazım kapasite koşulu, GEN karşılaştırması ve telemetri periyodu
b7'nin önceki GET'teki değeriyle korunması işlem servisine aittir;
durumsuz codec bu bilgiyi kendiliğinden varsaymaz.

Yeni codec dosyasında 18/18 Ceedling testi geçmiştir. İlgili RF config,
retry, JSON, SCP ve COBS testleriyle birlikte toplam 71/71 geçmiştir.
Komut:

```text
ceedling "test:pattern[(test_rf_|test_scp|test_cobs)]"
```

GCC 14.3.rel1, Cortex-M33, hard-float, `-Os`, C11 ve proje sıkı uyarı
seçenekleriyle yeni modül bağımsız object olarak hatasız derlenmiştir.
`arm-none-eabi-size`: text 3292 B, data 0 B, bss 0 B.
`-fstack-usage`: decode 272 B, event decode 160 B; build_request ve
config validate 16 B yerel frame bildirir. Bunlar toplam çağrı zinciri
stack sınırı veya linked (bağlanmış) firmware boyutu değildir.
Harici semboller yalnız memcpy, memset ve mevcut rf_scp_decode_status'tur.
Kanıt `build/rf-scp-r1/rf_scp_r1.o/.su` altındadır.

Tam CubeIDE build listesi yenilenmesi, yeni codec'in `rf_comm` dispatch
yoluna bağlanması, timeout/retry ve işlem durumları üçüncü ve sonraki
adımlardadır. Bu adımda NVRAM/Flash yerleşimi veya dummy üreticiler
değiştirilmemiştir. Cihaza yükleme, reset veya fiziksel kabul yapılmamıştır.

### Üçüncü adımın doğrulaması — 05.10.2026

Codec kaynak/API/test adlarından sürüm eki çıkarılmıştır. Mevcut
`scp_send_command` API'si korunmuş; `scp_send_request` doküman başlangıç
timeout/retry değerlerini seçen giriş olarak eklenmiştir. Geçersiz
istek, eksik transport (taşıma) veya bridge (saydam köprü) modunda
gönderim yapılmaz; SEQ tüketilmez.

`rf_comm` gelen yanıtı MH/RTU adresleri, CMD/SEQ, TYPE ve payload
ile doğrular. CFG2 GET/SET için ACK uzunluğu ayrıca istek türüyle
eşleşir. Boş veya hatalı ERROR gövdesi bekleyen isteği tamamlamaz.
CFG2'nin uzatılmış hata biçimleri BUSY+GEN için 2 B, INVALID_PARAM+ret
maskesi için 3 B ile sınırlıdır.

Timeout tekrarında aynı paket ve SEQ korunur. BUSY veya CFG2/ham PWRB
telemetrisinde NOT_AVAILABLE için mevcut deadline süresi kadar beklenir;
sonra yeni SEQ ile gönderilir. Ayrı retry katmanı veya sayaç eklenmemiştir.
Timeout ve ERROR denemeleri aynı bütçeyi tüketir. CFG2 GEN uyuşmazlığı
callback'e (sonuç işlevi) verilir; otomatik SET yapılmaz. CFG_READ_ALL
NOT_AVAILABLE yanıtı bu firmware'de kalıcı destek eksikliğidir; tekrar
edilmez. Kalıcı hata kodları da otomatik tekrarlanmaz.

SET bildirimleri codec doğrulamasından geçer ve aktif istek sırasında
da dispatch (mesaj yönlendirme) yoluna gelir; ACK üretilmez. PING ise
CMD=0/LEN=0 ve doğru MH/RTU adreslerinde boş ACK alır. BOOT/DISCOVERY'nin
mevcut işleyicileri korunmuştur. Diğer bildirimlerin durum/veri tüketici
işleri sonraki adımlardadır. Bridge açıkken yeni istek, PING yanıtı ve
retry engellenir; aktif komutun bütçesi ilerletilmez. Bridge mevcut
tasarımda yalnız reset ile kapanır.

`test_rf_error_retry_scenario.c` içinde 25/25 test; ilgili sekiz RF/SCP
test dosyası birlikte 87/87 geçmiştir. Yeni senaryolar gerçek RX ISR
girişi, ring buffer, SCP parser ve dispatch yolunu çalıştırır. UART
gönderimi, bridge durumu ve tick (zaman sayacı) test sınırlarıdır.
Geç ACK, yanlış kaynak/hedef/CMD/SEQ, bildirim araya girmesi, CFG2
hataları, retry bütçesi, callback'ten yeni istek ve sayaç sarma sınanmıştır.
Boş envanterin loaded durumu gibi dördüncü adım işleri henüz düzeltilmiş
sayılmaz.

CubeIDE managed Release build yeni `rf_scp_codec.c` dosyasını build
listesine eklemiş ve link (bağlama) tamamlanmıştır: 0 hata, kapsam dışında
korunan 14 Contiki/ST/Core uyarısı. Değişen rf_comm, rf_inventory ve
rf_scp_codec ayrıca Cortex-M33/C11 ve tüm sıkı uyarı seçenekleriyle
`-Werror` altında hatasız derlenmiştir. Kanıtlar
`build/scp-release-build.log`, `build/scp-strict-compile.log` ve
`build/scp-target/` altındadır. Release ELF text/data/bss değerleri
300308/524/125144 B'dır; bu toplamlar diğer mevcut kullanıcı değişikliklerini
de içerir ve yalnız bu adımın farkı değildir.

Release yerel stack frame'leri: scp_send_command 296 B,
scp_on_response 272 B, RF process 328 B, codec decode 272 B.
Toplam çağrı zinciri ve ISR preemption (kesme ile bölünme) payı fiziksel
kabulde ayrıca ölçülmelidir. Linker'in mevcut 4096 B stack alanı
değiştirilmemiştir. Cihaza yükleme veya reset yapılmamıştır.

### Dördüncü adımın doğrulaması — 05.10.2026

Uyumlu BOOT'ta geçerli yerel saat varsa TIME_SYNC, ardından envanter
girdileri ve END gönderilir. Kullanıcı kararı: geçersiz RTC veya takvim
değeri TIME_SYNC olarak gönderilmez; envanter yüklenir. Saat isteği
bekletilir ve saat geçerli olduğunda gönderilir. Bu sonraki ACK, bitmiş
envanteri yeniden yüklemez ve süren yüklemenin imlecini başa döndürmez.
Saatlik yenileme mevcut Contiki timer altyapısını kullanır.

BOOT, eski bekleyen isteği SCP_CMD_RESTARTED sonucu ile sonlandırır;
uçucu envanter durumunu temizler ve yeni SEQ ile zinciri kurar. BOOT
tekrarı ile gerçek MH restart'ı tek baytlık gövdeden ayırt edilemediği
için her geçerli BOOT yeniden eşitleme/yükleme talebi olarak işlenir.
Major uyuşmazlığında envanter/yazma engellenir; tanı için GET_STATUS/PING
kalır. Eski ACK yeni işlemi tamamlayamaz. BOOT sırası dışındaki
ANOMALY/PWR_ALARM reset işleyicileri ilgili sonraki servis adımlarındadır.

Envanter durumları IDLE/LOADING/EMPTY/PARTIAL/READY/ERROR olarak ayrılır.
EMPTY hiçbir zaman loaded değildir. PARTIAL, MH'nin kabul ettiği
nonempty (boş olmayan) envanterdir; tam başarı anlamına gelmez.
`rf_inventory_is_loaded` READY/PARTIAL için true, shell durum adı ise
ikisini ayrı gösterir. Hatalı alan veya bölge uyuşmazlığı yüklemeyi
sonsuz bekletmez; girdi atlanır ve kısmi/hatalı durum gösterilir.
Yerel 10 s sınırı en son gönderilen upload isteği üzerinden izlenir;
tam 10 s kabul edilir, aşılması ERROR ile durdurulur. Bu host sınırı
gerçek UART/MH işlem süresinin garantisi değildir.

Envanter channel alanı mevcut rf_channel ayarından alınır; R1'de yalnız
bilgi amaçlıdır. `rf_inventory_update` atama/silme isteğini gönderir;
kalıcı kaydın sahibi çağırandır. ACK'ten önce discovery girdisi kaldırılmaz.
Discovery EUI/RSSI bilgisi korunur; yinelenen rapor RSSI'yi yeniler,
kuyruk doluysa yeni cihaz mevcut listeyi bozmaz. EUI/signed RSSI getter'ı
eklenmiş, mevcut kimlik getter'ı korunmuştur.

`rf time` saat yenilemesi ister. `rf epoch <1..4>`, nonempty inventory
yüklendikten sonra MH değişimi için EPOCH_REFRESH gönderir; ACK yalnız
broadcast kuyruğunu bildirir. Shell, yapılandırmadan önce yaklaşık
30 s beklemeyi belirtir. Otomatik MH kart değişimi çıkarımı veya yeni
kalıcılık alanı eklenmemiştir. Yapılandırma servisindeki epoch bekleme
bağlantısı yedinci adımdadır. Shell yanıtları SHELL_LOG kullanır.

Haberleşme/açılış paketinde 46/46 test, ilgili dokuz RF/SCP/CP56Time2a
dosyasında toplam 122/122 geçmiştir. Gerçek timer.c ve cp56time2a.c de
kullanılır; RTC/tick/UART/NVRAM erişim sınırları taklit edilir.
Kanıt: `test/build/scp-startup-tests.log` ve Ceedling sonuçları.

rf_comm, rf_inventory, rf_discovery, rf_shell ve rf_scp_codec GCC
14.3.rel1/Cortex-M33/C11 sıkı uyarılar ve -Werror ile derlenmiştir.
Release main-build/link tamamlandı: text/data/bss 302136/524/125200 B.
Bu toplamlar çalışma ağacındaki diğer kullanıcı değişikliklerini de
içerir. Kanıtlar `build/scp-startup-strict-compile.log` ve
`build/scp-startup-release-build.log` içindedir. Linker/NVRAM düzeni,
dummy üreticiler ve BMS reader durumu korunmuştur. Cihaza yükleme yoktur.

Web Save işleminin update/delete veya yeniden yükleme servisine
bağlanması onuncu adımdadır; yükleme sürerken store değişimiyle ilgili
ürün bağlantısı henüz tamamlanmış sayılmaz. Fiziksel BOOT/UART/RF
zamanlaması, grup ayarı, olay tüketimi ve Powerboard kaynak geçişi
bu adımın host doğrulamasıyla tamamlanmış sayılmaz.

### Beşinci adımın doğrulaması — 05.10.2026

`rf.c/.h` artık EUI-64, R1 float32 ölçümleri, int16 MCU sıcaklığı,
signed RSSI, canlı veri sıra/uptime/durum/bayrakları ve log sayaçlarını
tutar. Eski rf_monitor_t, 32 bit DEVICEID ve dokümanda olmayan
DC/5V/3V3/LQI/arızaya ait sahte monitor alanları kaldırılmıştır.
Güncel model RAM'dedir; NVRAM'e yeni alan eklenmemiştir.

Canlı pakette yalnız src bulunduğu için eşleme desired (istenen) store'dan
değil, MH'nin ACK verdiği envanterden yapılır. ACK aynası atama, silme,
aynı EUI'nin taşınması ve aynı fider/fazın değiştirilmesini izler.
Web Save MH'den önce değişirse eski veri yeni EUI'ye etiketlenmez.
ACK alınmamış/bilinmeyen kimlik ölçüm üretmez. R1'in 1–4 aktif fider
sınırı korunur; fider 4 çözümü 3 bit maskeyle yapılır.

İletişim tazeliği son LIVE gelişinden itibaren 30 s'dir; SEQ boşluğu
kayıp sayılmaz. NaN/Inf ve negatif RMS kalite kontrolünden geçmez;
raw (ham) ölçüm korunur, JSON'da geçersiz ölçüm null gösterilir.
TRIP bildirimi canlı veri süresini yenilemez, ana arıza akımını veya
mekanik konumu temsil etmez. Anomali iki path (yol) için bilgi olarak
tutulur; koruma, ölçüm ve açma alarmını değiştirmez. Global RESET,
BOOT'a göre sıradan bağımsız temizlenir.

Trip_Failed 1→0 aynı AY açılışında ilerleyen uptime ile kalkabilir.
Uptime düşerse aktif alarm latched (operatör onayı gerekli) olarak kalır;
MH BOOT bu alarmı silmez. Aynı EUI yeni atamaya taşınırsa kendi alarm
durumu korunur; farklı EUI başka cihazın durumunu devralmaz.
`rf live <fider> <faz>` veriyi gösterir; `rf alarm-ack <fider> <faz>`
yalnız latched alarmı ve live flag sıfırken onaylar. 101/105 olaylarından
alarm oluşturma bağlantısı olay çekme adımında eklenecektir. Bu RAM
durumu RTU güç kesintisi sonrası kalıcı alarm geçmişi garantisi değildir.

Web /monitor/rf JSON'u LineId ve Phases dizisi kullanır. Her fazda
Eui64/Online/LiveAgeMs, Live, Trip, TripFailedAlarm/TripFailureLatched,
UptimeStalled ve AnomalyLive/AnomalyTrip bulunur. Live içindeki Irms A,
VTrip/VRec V, Temp derece C, RSSI dBm, Uptime saniye birimindedir.
Eksik veri HasData=false; geçersiz sayı null'dır. TRIP InstantAmps
yalnız anlık akım, RxMs RTU monotonic alım sayacıdır; event time değildir.
Eski JSON alanları veya integer ölçek adapter'ı tutulmaz. Türkçe ve
İngilizce tablo ile gömülü HTML aynı schema (veri düzeni) kullanır.

Yeni bounded (tampon sınırını denetleyen) JSON builder en büyük alan
değerleri ve dört fiderin 12 fazıyla mevcut 8192 B HTTP tamponunda
test edilmiştir. Küçük tamponda sınır aşılmaz, yarım JSON gönderilmez.
Simülasyon modülü yalnız açıkça istenen sentetik R1 örneği üretir;
envanter, ACK aynası veya gerçek RF veri modelini değiştirmez. Init
varsayılan olarak kapalıdır; diğer modüllerin dummy kararları bu adımla
değiştirilmemiştir.

Haberleşme/snapshot/gösterim paketinde 64/64 test; dokuz ilgili RF/SCP/
CP56Time2a dosyasında 140/140 geçmiştir. Web navigation paketi kaynak
ve gömülü HTML'de Türkçe/İngilizce R1 monitorünü sınamıştır.
Kanıtlar `test/build/scp-live-tests.log` ve ilgili Ceedling sonuçlarıdır.
Sekiz RF/monitor modülü Cortex-M33/C11 ve tüm sıkı uyarılarla -Werror
altında geçmiştir. Yeni JSON kaynağı managed build listesine eklenmiş,
Release link tamamlanmıştır: text/data/bss 305168/524/126048 B.
Kanıtlar `build/scp-live-strict-compile.log` ve
`build/scp-live-final-build.log` içindedir. Tam boyut diğer mevcut
kullanıcı değişikliklerini de içerir; görev farkı değildir.

IEC104/Modbus producer/eşleme, kalıcı olay/alarm saklama ve donanım/SCADA
kabulü halen sonraki adımlardadır. RF web monitorünün geçişi, kullanıcı
eski modeli istemediği için bu adımda öne alınmıştır. Cihaz yükleme veya
reset yapılmamıştır.

Host testleri gerçek MH/AY/PWRB firmware'inin iç davranışını, RF gecikmesini,
fiziksel UART taşmasını, enerji kesintisi dayanımını veya koruma davranışını
kanıtlamaz. Hedef derlemesi de donanım kabulü sayılmaz.

Sonraki cihaz kabulünde BOOT zinciri, üç faz veri alımı, RF kesintisi,
olay batch/sarma ve tüketme, grup ayarı ve MH restart, PWRB GEN/yankı ve
akü komutları gerçek cihazlarla sınanmalıdır. Cihaz işlemleri ayrı kullanıcı
yetkisi ve donanım test rehberiyle yürütülür. Bu plan fiziksel açma veya
akü sayaçlarını sıfırlama yetkisi vermez.

### Altıncı adımın kalıcı kayıt ve örnek iletişim bölümü — 05.10.2026

**Kullanıcı kararı:** Kalıcı/geçici arıza alanları korunur. Tam 60 bayt
RF paketi için ayrı 8 KB alan ve mevcut spi_flash_log kullanılır.
Yeni alan 0x232000–0x233FFF adreslerindedir; uygulama başlangıcında açılır.
NVRAM ve önceki Flash alanlarının adresleri değişmez.

| Alan | Boyut |
|---|---|
| Ham RF kaydı, ayrılmış baytlar ve iç CRC dahil | 60 B |
| spi_flash_log sıra numarası ve CRC | 4 B |
| Flash'ta tek kayıt | 64 B |
| Bir 4096 B sektörde kayıt | 64 adet |
| İki sektörde toplam kapasite | 128 adet / 8192 B |

Halka dolduğunda kütüphanenin mevcut sektör silme davranışı kullanılır.
Bir sektör silinirken diğer sektördeki 64 kayıt korunur; sonraki yazmayla
65 kayıt olur ve sayı yeniden 128'e kadar çıkar. Bu alan sınırlı geçmiş
tutar; sınırsız merkez kesintisinde saklama garantisi değildir. Tekrar
ekleme aynı olayı iki kez saklayabilir; kalıcı tekilleştirme eklenmedi.

`rf_event_log_append` iç CRC/uzunluk kontrolü, gerçek Flash yazımı ve
geri okumadaki sıra numarası/tüm bayt eşleşmesi sonrasında başarı döner.
Kütüphane yeniden başlatma taramasını, yarım yazımı ve sektör geçişini
yönetir. Okuma sürücüsü void döner; hatayı doğrudan bildiremez. Geri
okuma ve günlük CRC kontrolü bozuk/eksik veriyi reddeder; genel sürücü
okuma hatası ile boş Flash ayrımının sınırı sürer. Yeni kuyruk veya
retry katmanı eklenmedi; yalnız cooperative process bağlamı kullanılır.

AY_05b CSV paketleri UART ring → gerçek SCP parser → rf_comm yolundan
geçirilir. HEAD head=37/wrap=42/total=4237/tail=36, RANGE bir 60 B kayıt,
CONSUME tail=37/left=0 değerleri doğrulanır. Bütün kayıt alanları ayrıca
sabit beklenen değerlerle sınanır. Kısa batch kabulü, eksik kayıt reddi
ve önceki çıktının korunması test edilir.

**Örnek/doküman farkı:** CSV'de 0x47 bildirimi için RTU ACK'i vardır;
R1 §4.8 ACK istemez. Uygulama ve test beklentisi R1'e uyar. CSV'deki
bazı istek zamanları yanıt zamanından önce görünür; test HEAD yanıtı
sonrasında RANGE, RANGE yanıtı sonrasında CONSUME sırasını kullanır.
Ham yanıt paketleri ve SEQ değerleri değiştirilmez. Bu test CONSUME
komutunun taşımasını doğrular; otomatik kalıcı teslim servisi değildir.

Kalıcı kayıt paketinde 11/11; on bir ilgili RF/SCP/CP56/SPI-log dosyasında
157/157 test geçmiştir. Yazma/CRC/geri okuma hatası, önceki aynı kayıtla
yanlış doğrulama, yeniden açılış, sektör sarma/silme hatası, ham baytlar
ve eski alan adresleri sınanmıştır. 175 frame fixture kontrolü geçmiştir.
Kanıt `test/build/scp-event-tests.log` içindedir. Dokuz RF/monitor modülü
Cortex-M33/C11 sıkı uyarılar ve -Werror ile geçmiştir. CubeIDE Release
link: text/data/bss 305348/524/126096 B. Değişen kaynakların derlemesi
sıfır uyarıyla bitmiştir; kapsam dışı vendor/Contiki uyarı kararı sürer.
Kanıtlar `build/scp-event-strict-compile.log` ve
`build/scp-event-release-build.log`; toplam diğer kullanıcı değişikliklerini
de içerir. Host sonuçları fiziksel enerji kesintisi/UART/RF kabulü değildir.

Bu bölümden sonraki otomatik servis aşağıda tamamlanmıştır. 101/105
alarm bağlantısı, merkez aktarımı ve Powerboard kaynak geçişi henüz bitmedi.

### Otomatik olay servisi ve arıza listesine aktarım — 05.10.2026

**Kullanıcı kararları:** Olay 1 ve 7 kalıcı, olay 3 geçici arıza listesine
yönlendirilir. Bu eşleme fault_log_type_t enum açıklamalarına eklenmiştir.
Süreyi sınırlamak yerine mevcut kayıt uint32_t süreye genişletilmiştir.
head=tail boş kuyruk kabul edilir; aynı head için pending=100 bildirimi
varsa dolu halka olarak okunur. Bildirim kaybı/dolu halka ayrımının HEAD
gövdesindeki sınırı yaklaşık free_slots ile kapatılmaya çalışılmaz.

`rf_events` mevcut tek bekleyen komut mekanizmasını kullanır. Envanter
ACK'i tamamlanınca HEAD çekilir; bildirim ve 60 s periyodik kontrol de
çekmeyi başlatır. READY/PARTIAL/EMPTY durumları kabul edilir; uyumsuz
major, yüklenen envanter veya meşgul taşıma sırasında istek başlamaz.
HEAD'deki tail esas alınır; MH açılışı ve depo sıfırlanması için eski
yerel imleç devam ettirilmez. Tek tampon en çok dört 60 B kayıt taşır.
Her process poll'unda bir kayıt işlenir; yeni process/kuyruk eklenmedi.

Önce ham paket doğrulanıp 8 KB günlüğe yazılır. 1/7/3 ise zone ve
Fider_ID ile mevcut store satırı bulunur; Phase_ID 1–3 liste indeksine
0–2 dönüştürülür. Liste indeksi Fider_ID−1 varsayılmaz. Kaynak zamanı
korunur; clock_quality=1 için IV=0, diğerlerinde IV=1 kullanılır.
Amper değeri mevcut x10 helper'ıyla saklanır. energy_status aynı anlamla
aktarılır; yük akımı var biti eski listede Below biti olduğundan terslenir.
Line_ID=0, eşleşmeyen bölge/fider veya temsil edilemeyen arıza ölçümü
için bir liste kaydı uydurulmaz; tam paket ham günlükte kalır.

Ham yazma ve gerekli fault_log_append/sync başarılı olunca CONSUME
gönderilir. Yazma hatası tail'i tutar. Sync hatası sonrası aynı RAM kaydı
yeniden eklenmeden mevcut sync yolu yeniden denenir; raw kayıt da ikinci
kez yazılmaz. RTU/MH restart veya kayıp CONSUME sonucu yeniden çekilen
kayıt yine eklenebilir; kalıcı tekilleştirme garantisi verilmez.

Yerel yazma hatası nedeniyle beklenmişse CONSUME öncesi HEAD yeniden
çekilir. İlk tail ve total ile karşılaştırılır; yeni yazımlar eski yuvayı
ezmişse eski CONSUME gönderilmeden güncel tail'den tekrar başlanır.
Halka sarıp indeks eski değerine dönse de total farkı bu durumu ayırır.
Normal başarılı örnek akışa ek sorgu eklenmez. Bu kontrol protokolün
SCP yanıtı ile sonraki istek arasına atomik bir işlem eklemez; fiziksel
MH yazma/RTU tüketme zamanlamasının kabul testi gereksinimi sürer.

Kısa batch geçerlidir. İç CRC/body hatasında yalnız başarıyla saklanan
önek tüketilir; bozuk kayıt tüketilmez. MH ERROR 0x06 bildirirse yalnız
ilgili bir yuva atlanır. 99→0 sarma uygulanır. ERROR 0x02 sonrasında
aynı tüketme isteği tekrarlanmaz; yeni HEAD ile durum alınır. Ortak
retry bütçesi sonrası BUSY sürerse FRAM tanısı çekilir; degraded=1
olay akışını sonraki BOOT'a kadar durdurur. Periyodik kontrol, yeniden
çekme ve yerel depolama hatası denemesinde 60 s kullanılır; bu süre
RTU plan tercihidir, dokümanın zorunlu RTU poll süresi değildir.

fault_log_t artık 20 B, duration ofseti 11 ve CRC ofseti 16'dır.
Fider görüntüsü 1852 B'dır; mevcut 4096 B ana/yedek sektörlerine sığar.
FAULT_LOG_SCHEMA_VERSION 2 olmuştur; NVRAM düzeni değişmemiştir.
Sahada cihaz olmadığı için eski 18 B görüntü migration'ı yoktur.
IEC104 günlük entry boyutu 24 B, sektör kapasitesi 170 kayıttır; sekiz
sektörde en çok 1360, sektör silinirken 1190 kayıt korunur. IEC104 süre
aktarımı mevcut float ölçüm tipindedir; çok büyük sürelerde wire (hat)
üzerinde her milisaniyenin tam gösterimi garanti değildir. Kalıcı
uint32_t kayıt ve ham günlükte tam değer korunur.

15 ilgili RF/SCP/CP56/IEC104/SPI-log/arıza dosyasında 246/246 test geçti.
Olay servisi 23 testi kapsar.
Arıza listesi için mevcut NOR/çift kopya paketi merkezi Ceedling'e
bağlandı; 53 eski kontrol ve kaynak zaman/UINT32_MAX yeniden açılış
testi gerçek fault_log koduyla çalışır. Servis testleri taşıma ve kayıt
sınırlarını taklit eder; bağımsız kayıt paketleri gerçek Flash algoritmasını
NOR modelleriyle sınar. Önceki UART/SCP örnek testleri dispatch'in yeni
servise pending/head aktardığını da doğrular. 14 CSV/175 frame kontrolü
geçti; kaynak örnekler değiştirilmedi. Kanıtlar
`test/build/scp-event-service-tests.log`.

On RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti.
Release link 307860/524/126568 B text/data/bss ile tamamlandı. Loglar
`build/scp-event-service-strict.log`, `build/scp-event-service-release.log`
ve `build/scp-event-service-final-build.log`. Toplam diğer kullanıcı
değişikliklerini de içerir. Cihaza yükleme/reset veya commit yapılmadı.
101/105 alarm bağlantısı, IEC104 replay/spontane yeni üretici bağlantısı,
grup ayarı, Powerboard tüketici geçişi ve fiziksel kabul açık kalmıştır.

### Commit sonrası IEC104 bağlantısı — 05.10.2026

Buraya kadarki RF-SCP kodu/testi/belgesi `a635d61` commit'ine alındı.
Ortak belgelerde RF kapsamı ayrıldı; bağımsız çalışma ağacı değişiklikleri
bu commit'e eklenmedi. Referans protokol ve 14 örnek CSV de takip edilir.

Sonraki parçada 1/7/3 için kaynak zamanı ve 32 bit süresi korunmuş hazır
fault_log_t mevcut IEC104 günlüğüne aktarılır. Hat açıksa mevcut
iec104_emit_evtlog_record spontane girişi çağrılır; kabul edilirse aynı
seq mark_sent ile işaretlenir. Hat kapalı veya gönderim reddedilmişse
kayıt replay için unsent kalır. Mevcut yeniden en eskiye replay süreci
ve NVRAM state kullanılır; yeni public API, process veya kuyruk yoktur.

Gerekli arıza listesi, IEC104 log ekleme ve IEC104 state sync başarılı
olmadan MH tüketilmez. State sync hatasında aynı olay yeniden eklenmez
ve anlık gönderim yeniden denenmez. Önceki ham/list yazımları korunur.
Bu garanti RAM'de aynı batch boyunca geçerlidir; reboot'ta yeniden
çekilen olay yine kopya olabilir. Hazır fault kaydı yeniden deneme boyunca
tutulduğundan config değişimi kaynak zamanını/fiderini değiştirmez.

27 olay servisi testi dahil 15 ilgili dosyada 250/250 test geçmiştir.
Dört yeni test online kabul/mark_sent, gönderim reddi/replay, IEC104
yazma hatası ve NVRAM sync hatasını sınar. Transport ve kayıt API sınırları
taklit edilir; gerçek IEC104 frame üretimi mevcut protokol paketiyle ayrıca
çalışır. Kanıt `test/build/scp-replay-tests.log`. On RF/monitor modülü
Cortex-M33/C11 sıkı uyarılar/-Werror ile geçmiştir. Mevcut IEC104 Init
makrosu HAL struct alanıyla çakıştığından HAL include'u IEC104 önündedir;
vendor kod veya macro tanımı değiştirilmedi. Release link toplamı
307972/524/126584 B text/data/bss; diğer kullanıcı değişikliklerini de
içerir. Loglar `build/scp-replay-strict.log`, `build/scp-replay-release.log`.

101/105'in geç kayıt davranışı kullanıcı isteğiyle BOLATeX'e sorulacaklar
belgesine taşındı; bkz. BQ-01/BQ-09. Geçici bir alarm politikası seçilmedi.
Mevcut LIVE alarm takibi korunuyor. IEC104 parçası henüz commit edilmedi;
fiziksel cihaz ve SCADA kabulü yapılmadı.

### BOLATeX soruları ve grup bloğu hazırlığı — 05.10.2026

Kullanıcı, benzer protokol belirsizliklerinin bugünkü notlarla birlikte
ayrı belgede toplanmasını ve 101/105'in cevapla netleştirilmesini istedi.
Soruların tek kaynağı [BOLATeX'e sorulacaklar](BOLATEXE_SORULACAKLAR.md)
belgesidir. BQ-01–09 açık kalır; RTU tarafında alınmış kararlar ayrı
tabloda gösterilir. Bu belge BOLATeX'e gönderilmedi. Belirsiz geç kayıt
alarmı bekletilir; sıradaki grup hazırlığı ilerler.

Kaynakta rf_config_for_write device/default Fider_ID'yi koruyor ve 96 B
blok CRC'sini hesaplıyordu. R1 §4.10 hedef Fider_ID'nin RTU ayarından
alınmasını, reserved/CRC baytlarının sıfır gönderilmesini tarif eder.
Bu hazırlık API'sinin üretim çağıranı henüz yoktur; mevcut aktif grup
akışında bir saha arızası düzeltilmiş sayılmaz.

API imzası korunmuştur. Mevcut codec'in alan doğrulaması kullanılır;
geçersiz fider/NaN/Inf veya null girdide önceki çıktı değiştirilmez.
Hazırlık önce yerel 96 B blokta yapılır; alias (aynı tampon) kullanımı
güvenlidir. Target Fider_ID RAM'den, writable alanlar aynı RAM ayarından
gelir. İzinli diğer masked alanlar mevcut optional tanı/default tabanını
kullanabilir; ayırıcıdan CFG_READ_ALL otomatik çağrılmaz. Ayrılmış baytlar
ve blok CRC sıfırdır; writable cfg_crc helper'ı ayrı korunur. Mevcut
RAM/NVRAM Save zamanlaması veya APPLIED politikası değiştirilmedi.

AY_06/AY_06b'nin altı gerçek WRITE bloğu hazırlanıp üretim request
codec'inden geçirilir; writable baytlar kaynak örneğe aynen eşlenir.
CRC/masked baytlar için dokümanın izin verdiği hazırlık uygulanır;
örnek bütün gövdeyle zorunlu byte eşitliği ölçütü yapılmaz. Üç yeni
senaryo capture girdileri, invalid fider/NaN/null ve alias/Inf reddini
kapsar. Ayar paketi 10/10; 15 ilgili dosya 253/253 test geçti. Yeni codec
bağımlılığı RF JSON ve NVRAM integration build'lerine eklendi; NVRAM
paketinde 433/433 kontrol geçti. Kanıtlar
`test/build/scp-group-block-regression.log`, `build/scp-group-nvram-integration.log`.

On bir RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti.
Release link 307972/524/126584 B text/data/bss ile tamamlandı; henüz
aktif çağıranı olmayan helper link'te tutulmayabilir. Toplam diğer kullanıcı
değişikliklerini içerir. Kanıtlar `build/scp-group-block-strict.log` ve
`build/scp-group-block-release.log`. Commit, cihaz yükleme/reset yoktur.

Sıradaki parça aynı 96 B bloğun üç EUI'ye WRITE edilmesi, COMMIT,
bildirim/STATUS_GET ile sonuç ve cfg_crc denetimidir. APPLIED ile desired
kalıcılık ayrımı plan K5'te RTU ürün kararıdır; BQ-06 özel olay 122
eşlemesinin BOLATeX açıklamasıdır. Bu ikisi aynı karar sayılmaz.

### Arıza sınıflama teyidi ve grup durum sorgusu — 05.10.2026

Kullanıcı geçici/kalıcı arızaların BOLATeX tarafından tanımlanmasını
istedi. BQ-10, yalnız 1/3/7 eşlemesini değil bütün event_trigger tablosunu,
4/5/6 ile terminal olayların ilişkisini, 100/101/105'i ve iki arıza sayacının
artış anını sorar. Mevcut 1/7 kalıcı, 3 geçici kabulü değiştirilmedi;
enum üstünde geçici RTU eşlemesi ve BQ-10 teyit notu eklendi. Bu kabul
vendor tarafından onaylanmış sayılmaz. Soruların tek kaynağı
[BOLATeX'e sorulacaklar](BOLATEXE_SORULACAKLAR.md) belgesidir.

Grup işleminde WRITE group_id taşımadığı halde COMMIT öncesi ABORT'un
hangi kimlikle yapılacağı açık değildir; BQ-11'e eklendi. Bu soru cevap
gelmeden otomatik yarım grup temizliği yazılmadı. Bağımsız normal durum
sorgusu `rf cfg-status <0..255>` shell komutuyla eklendi. Mevcut request
codec/tek bekleyen komut kullanılır; 0x28 GET bir bayt group_id gönderir.
Callback MH state, kabul sırasına göre member bitmap, reason, cfg_crc ve
attempts gösterir. Bu çıktı MH raporudur; desired yapılandırmanın RTU'da
uygulanmış olarak işaretlenmesi veya NVRAM Save değildir. Yanıttaki group_id
istekle eşleşmezse gösterim reddedilir. Busy başarısız çağrı mevcut
isteğin group_id bilgisini değiştirmez. Yanıt SHELL_LOG kanalındadır.

Üç test gerçek UART/SCP hattında örnek APPLIED bildirim gövdesinin aynı
gövdeli STATUS_GET yanıtı olarak alınmasını, invalid argüman/busy durumunu
ve 0/255 sınırlarını sınar. Haberleşme paketi 69/69; 15 ilgili dosya
256/256 geçti. On bir RF/monitor modülü Cortex-M33/C11 sıkı uyarılar ve
-Werror ile geçti. Release link 308704/524/126584 B text/data/bss ile
tamamlandı; toplam diğer kullanıcı değişikliklerini de içerir. Kanıtlar
`test/build/scp-group-status-regression.log`, `build/scp-group-status-strict.log`
ve `build/scp-group-status-release.log`. Commit veya cihaz işlemi yapılmadı.

WRITE×3/COMMIT sıralayıcısı, BQ-11 temizliği, BQ-06 eski 122 eşlemesi ve
K5 kalıcılık seçimi halen açık kalır. Sorgunun eklenmesi bu alanların veya
101/105 alarm kararının tamamlandığı anlamına gelmez.

### Normal grup işlemi ve sonuç doğrulaması — 06.10.2026

`rf_comm` içinde WRITE×3/COMMIT sıralayıcısı yoktu ve 0x21 bildirimleri
işlenmiyordu. `rf_group` tek RAM işlemiyle mevcut komut mekanizmasına
bağlandı. API/shell başlangıcı mevcut store'u snapshot (sabit kopya) olarak
alır; yeni NVRAM alanı, Save politikası, process veya retry katmanı yoktur.
Üç farklı, sıfır olmayan EUI-64'ün ACK envanteri ve zone/fider/fazı kontrol
edilir. Fider 4 dahil aktif 1–4 sınırı korunur; store satırı fider−1
varsayılmaz. Aynı doğrulanmış 96 B blok üç EUI'ye kabul sırasıyla gönderilir.

Önce seçilen group_id 0x28 ile sorgulanır: kimlik güncel grupta kullanımda
ise yeni WRITE başlamaz. Gerekçe, R1'in APPLIED olmuş aynı kimliğin COMMIT'ini
yeniden uygulamadan ACK etmesidir. IDLE/kimlik uyuşmazlığı yanıtı sonrasında
üç WRITE ve COMMIT ilerler. Her yeni istek SEQ'ini mevcut komut mekanizması
verir; timeout retry aynı SEQ, izinli ERROR retry yeni SEQ kuralını korur.

COMMIT ACK yalnız WAITING yapar. APPLIED başarı için üç üye bitmap'i ve
beklenen writable cfg_crc birlikte eşleşmelidir. Crc/bitmap uyuşmazlığı
ayrı yerel REPORT_MISMATCH olur; MH'nin ham sonucu değiştirilmez. WRITE
kabul sırası status.members dizisinde tutulur; FAILED bitmap'i bu sıraya
göre sorunlu üyedir. FAILED/PARTIAL_COMMIT otomatik yeniden uygulama yapmaz.
CRC'si farklı ama MH'de terminal tam APPLIED olan iş için operatör yeni
kimlikle tekrar başlatabilir. Eksik bitmap belirsizliği kesin GET ile
çözülmeden yeni grup başlamaz.

0x21 kaybolursa WAITING durumunda 5 s aralıkla 0x28 çekilir. 5 s RTU
poll tercihidir; yeni retry bütçesi değildir. MH'nin yaklaşık 60/120 s
STAGED/DELIVERED süreleri RTU tarafından sahte FAILED sonucuna çevrilmez;
nihai MH sonucu esas alınır. Komut retry bütçesi tükenirse UNCERTAIN
gösterilir. Erken APPLIED/FAILED bildirimi geç COMMIT ACK'i veya timeout ile
ezilmez; gecikmiş progress terminal sonucu geri çevirmez. Fresh GET, erken
uyuşmayan bildirimi düzeltebilir. USER_ABORT reason=10 CANCELLED bilgisi
olarak gösterilir; açma/koruma alarmı oluşturmaz.

WRITE/COMMIT öncesi ACK envanteri yeniden denetlenir. Desired store işlem
ortasında değişse bile blok ve üyeler karışmaz. EPOCH_REFRESH ACK'inden
sonra ilgili fider için 30 s yerel bekleme uygulanır; diğer fiderler
bağımsızdır. Bu yalnız minimum beklemedir, RF teslim kanıtı değildir;
BQ-07 açık kalır. BOOT işlemi RESTARTED olarak sonlandırır; envanter kabulü
yeniden tamamlanmadan yeni grup başlatılmaz.

Henüz WRITE gönderilmemiş yerel iş iptal edilebilir; read-only probe
timeout'ı yeni deneme için MH reseti gerektirmez. WRITE gönderilmiş fakat
sonuç belirsizse yeni iş kilitlidir; BQ-11 cevaplanmadan bilinmeyen kimlikle
ABORT gönderilmez. Kimliği bilinen aktif COMMIT işi operatörle ABORT
edilebilir; ERROR yanıtı iptal başarısı sayılmaz. Olay 122 eski/güncel işlem
eşlemesi BQ-06 cevabı gelmeden uygulanmış kanıtı olarak kullanılmaz.

Shell kullanımı:

```text
rf cfg-apply <store satırı 1..7> <yeni group_id 0..255>
rf cfg-state
rf cfg-status <group_id 0..255>
rf cfg-abort
```

cfg-state yerel sonucu ve son MH raporunu ayrı gösterir. Komut cevapları
SHELL_LOG üzerinden aktif terminale gider. Komutların eklenmesi fiziksel
cihazda ayar gönderimi yapılmış anlamına gelmez; bu çalışmada yürütülmedi.

Grup paketinde 23/23, haberleşme paketinde 73/73; 16 ilgili RF/SCP/CP56/
IEC104/arıza dosyasında 283/283 test geçti. Ayar store/codec/CRC üretim
kodudur; NVRAM ve transport/ACK envanter sınırları taklit edilir. AY_06'nın
üç EUI ve writable ayarı, 0x096D cfg_crc ve STAGED/DELIVERED/APPLIED
gövdeleri kullanıldı. Aynı blok, değişen store, eski kimlik, yanlış/eksik
APPLIED, erken/geç ACK, timeout, yanlış grup/tip, PARTIAL bitmap, ABORT,
probe iptali, tick wrap ve fider 4/epoch bekleme sınandı. Orijinal wire
bildiriminin gerçek RX/dispatch üzerinden servise aktarılması ve ACK
üretilmemesi ayrıca test edildi. Kanıt `test/build/scp-group-service-regression.log`.

On iki RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti.
CubeIDE yeni kaynağı managed build'e ekledi; son Release link toplamı
311704/524/126768 B text/data/bss. Diğer kullanıcı değişikliklerini de
içerir. Loglar `build/scp-group-service-strict.log`,
`build/scp-group-service-release.log`, `build/scp-group-service-final-build.log`.
Host/derleme fiziksel RF süresi, enerji kesintisi veya üç AY kabulü değildir.
Yeni commit veya cihaz yükleme/reset yapılmadı.

K5 desired/APPLIED kalıcılık seçimi, BQ-06/BQ-07/BQ-11 ve Powerboard
tüketici geçişi açık kalır; normal grup akışının bitmesi bunları kapatmaz.

## Powerboard tüketici geçişi — 06.10.2026

### Amaç ve kullanım yeri

`power_board_scp` modeli E1 özetini ve E3 alarmını mevcut RF RX/dispatch
(alım/yönlendirme) yolundan alır. `system_status`, web JSON, Modbus ve
`pwrboard [show|alarms]` shell komutu bu modeli okur. `app_main` artık eski
I²C Powerboard process'ini başlatmaz. Eski kaynaklar ve saf decode testleri
korunur; aynı ölçümlere ikinci bir üretici bağlanmaz. Fiziksel GPIO
power-panic yolu ve kapalı BMS reader bu geçişle değiştirilmemiştir.

### Kurallar ve alınan kararlar

[KARAR: 06.10.2026] Kullanıcı 49200 tabanından yeni SCP Modbus haritasını
onayladı. Blok 38 register'dır; 49200–49237 / PDU 9200–9237 aralığındadır.
Alan, birim ve geçerlilik bitlerinin tek kaynağı
[Modbus haritası §7.2](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md)
olmalıdır. `modbus-tools/power_stats.mbp` aynı 38 alanı okur.

E1'in 10 s periyoduna karşı RTU'da önerilen 30 s yerel eskime sınırıyla
ilerlenmiştir. Bu süre R1'in zorunlu kuralı değildir. E1/E3 yenilenmezse
ilgili geçerlilik bitleri temizlenir; önceki ham değer silinmez. MH BOOT
ölçüm/alarm geçerliliğini kaldırır; Powerboard son nefes oturumu korunur.
Web geçersiz ölçümleri `null` ve ekranda boş gösterir. SCP özetinde olmayan
panel akımı, ortam sıcaklığı ve ısıtıcı bilgisine değer uydurulmamalıdır.
Signed (işaretli) akım ve göreli SOC korunur; SOC'nin mutlak/göreli oluşu
`BatterySOCAbsolute` alanıyla belirtilir. Sıcaklık ve SOH mevcut ekran
birimlerine dönüştürülür; Modbus özetin kendi birimlerini taşır.

E3 tam 32 bit active mask'i (etkin alarm bitleri) günceller. E1 mask'i
bildirim kaybından sonra durumu düzeltir. RESET, yinelenen SEQ ve aynı
Powerboard oturumundaki son nefes/iptal/restart bildirimleri ayrı ele alınır.
Tahmini son nefesin gerçek kesintiye dönüşmesi aynı oturumun nedenini
netleştirir; yeni kesinti oturumu oluşturmaz. E1 `last_gasp_count` alanı ham
bildirim sayısıdır; benzersiz kesinti sayısı diye sunulmamalıdır.

Olay günlüğü kodu 53 (`ELOG_PWR_SCP_ALARM`) E1 mask düzeltmesini ve E3
alarmını kaydeder. 16 bayt info düzeni: [0] kaynak E1/E3, E3 için [1] code,
[2] state, [3] seq, [4] suppressed, [6..7] value0/value1; E1 için [3] seq,
[5] session; [8..11] yeni ve [12..15] önceki mask little-endian'dır.
Mevcut `elog_add` API'si dönüş değeri vermediğinden bu yol kalıcı yazma
başarısını bildirime ACK verme şartı olarak kullanmaz.

### Doğrulama ve sınırlar

20 ilgili Ceedling dosyasında 319/319 test geçti. Yeni model 13, sistem
JSON 5, gerçek Modbus FC03 paketi 6 test içerir. Örnek akışlardaki E1/E3
wire (hat) paketlerinin UART ring/COBS/parser/dispatch üzerinden servise
ulaşması ve ACK üretilmemesi ayrıca sınandı. Ham fixture kontrolü 14 CSV'de
175 frame için geçti. Geçersiz girişte mevcut durumun korunması, eskime,
MH restart, tick wrap, negatif SOC/akım, sıcaklık sentinel, alarm mask'i,
RESET ve son nefes tekrarları kapsandı.

Web kaynak/gömülü sayfa navigation ve gerçek handler web_auth paketleri
geçti. 16 modül Cortex-M33/C11 sıkı uyarılar/-Werror ile derlendi; Release
link başarılıdır (text/data/bss: 307400/504/126792 B; eşzamanlı diğer
değişiklikler de dahildir). Kanıtlar `test/build/scp-power-regression.log`,
`build/scp-power-strict.log`, `build/scp-power-final-build.log` ve
`build/scp-power-web-auth.log` içindedir. Bu sayı seçili paketlerin
sonucudur; başka çalışmada raporlanan GSM CESQ başarısızlıklarını kapatmaz.
Fiziksel UART/RF, enerji kesintisi veya cihaz kabulü bu adımda sınanmadı.

E5/E6 Powerboard ayarları, E7 sonuç tüketicisi ve E8 ham alan eşlemesi
sonraki adımdadır; BQ-08 açık kalır. K9'daki yeni IEC104 alarm adresleri ve
mevcut dummy batarya uyarı register'ı bu geçişte değiştirilmemiştir.
Yeni commit veya cihaz yükleme/reset yapılmadı.

## Powerboard ayar ve komut servisi — 06.10.2026

### Amaç ve kullanım yeri

`power_board_control` E5–E8 için mevcut tek SCP request (istek) mekanizmasını
kullanır. Müşteri ayarları ve operatör komutları `pwrboard` shell üzerinden
başlatılır. Yeni NVRAM alanı yoktur; CFG2'yi kalıcı saklayan taraf R1'e göre
MH'dir. Eski STATBLK yüzdeli C-oranı ve time calibration (saat kalibrasyonu)
API'si bu yola taşınmaz; yeni oran binde birimindedir.

### Akış ve kurallar

Ayar yazımı önce E5 GET yapar; GEN alınır. Yalnız müşteri bitleri 9, 13 ve
14 kullanılır; işletme ayarlarına yazılmamalıdır. İlk blok yokken gerçek
7–54 Ah kapasite aynı istekte verilmelidir. Normal periyot 1–10 s'dir;
b7 GET'teki gibi korunur ve b6=0'dır. Kapasite/C-oranında ayarsız 0/255
kabul edilir; ayarsız periyot kullanıcı kararıyla BQ-14 cevabını bekler.

SET ACK'i `written_gen` ve MH'ye kayıt kabulüdür; uygulanmış sonucu değildir.
`max(20 s, 2 × periyot + 12 s)` sonra bir E5 GET başlatılır. Güncel GEN,
geçerli yankı, eşleşen yankı GEN'i ve `(m1 & 0xDF)==0`, `(m2 & 0x7F)==0`
arar. Kapasite/C-oranı değiştiyse ayrıca geçerli E1'de `durum2` b6:4=0 ve
yazılan değerlerin eşleşmesi gerekir. Aynı anda iki alan yazımı servis
API'sinde desteklenir. Normal kapasite/C-oranı talebi uygulanmadan akü
değişti komutu başlatılmamalıdır; ret veya belirsiz yazım da bunu açmaz.

Yankı henüz yoksa veya verification GET (doğrulama sorgusu) başarısızsa
uygulandı/ret sonucu uydurulmaz. Operatör `cfg-read` ile yeniden sorgular;
ek arka plan poll (periyodik sorgu) veya tahmini üst bekleme süresi eklenmez.
GEN çatışmasında eski SET körlemesine yinelenmez; operatör aynı `cfg-set`
talebini yeniden verdiğinde yeni GET ile başlar. Ortak SCP timeout/ERROR
retry kuralları korunur. Yazım timeout'ı belirsizdir; yeni müşteri yazımı
önce GET yapar. Son SET'in ret alanları ile GET'teki tarihsel `c2_red`
ayrı gösterilir; tarihsel ret başarılı yazımın başarısızlığı sayılmamalıdır.

[KARAR: 06.10.2026] Ayarsız kapasite/C-oranında MH kayıt ve yankı kabulü
ayrı gösterilir; `ECHO_ACCEPTED` sonucu `APPLIED` diye sunulmamalıdır.
BQ-12'de uygulama doğrulaması BOLATeX'e sorulur. Bu karar diğer normal
kapasite/C-oranı değerinin uygulanmadan akü komutu gönderilmesini açmaz.

`battery-replaced` yalnız E6 `[05 A5]` gönderir; servis komutları 01–04
ürün API'sinde sunulmaz. ACK yalnız KOMUT/SIRA kabulüdür. E7 bildirimi veya
GET raporu beklenen KOMUT/SIRA ile eşleşirse sonucu günceller; erken
bildirim ACK sonrasında eşlenir. Başarılı sonuç/b3 uygulamayı, b1 izlemeyi,
b2 doğrulanamamayı gösterir. SONUC=FF ve bit bulunmaması bitiş sayılmaz;
BQ-13 açık kalır. Kayıp bildirim `result-get` ile sorgulanır. Otomatik yeni
05 gönderilmemelidir. `cancel` ACK'i güç kartının sayaçları sıfırlamadığını
kanıtlamaz; önceki SIRA'nın ayrı akıbet raporu izlenir.

E8 GET ve istenmeden SET, son 96 baytı aynen saklar; alan düzeni yorumlanmaz.
`raw` hex (onaltılık) gösterir. Blok karar/ölçüm kaynağı değildir; yaşı E1
üzerinden değerlendirilmelidir. BQ-08 bu eklemeyle kapanmaz. MH BOOT
kontrol sonuçlarını, config ve raw geçerliliğini kaldırır; eski işlemler
kendiliğinden tekrar gönderilmez. Tüm kontrol erişimleri cooperative
process/shell bağlamındadır; ISR yalnız mevcut RX ring'e bayt ekler.

### Kullanım

```text
pwrboard cfg-read
pwrboard cfg-set capacity 12
pwrboard cfg-set rate 22
pwrboard cfg-set period 2
pwrboard control
pwrboard battery-replaced
pwrboard result-get
pwrboard cancel
pwrboard raw-get
pwrboard raw
```

`cfg-set` bir alan değiştirir; her talep yeni GET ile başlar. İş sürerken
başka yazım başlatılmaz. Yukarıdaki liste otomatik çalıştırılacak bir
sıra değildir. Özellikle `battery-replaced` yalnız gerçekten akü değiştiyse
ve önceki kapasite/C-oranı yazımı uygulanmışsa kullanılmalıdır. Komutların
eklenmesi cihazda çalıştırıldığı anlamına gelmez. Shell cevabı SHELL_LOG
ile aktif terminale gider; asenkron durum `control` ile okunur.

### Doğrulama ve açık işler

Yeni servis/shell paketinde 22/22, 21 ilgili Ceedling dosyasında 343/343
geçti. Üretim codec ve kontrol servisi gerçektir; transport ve E1 snapshot
sınırları taklit edilir. PWRB_03'ün GET/SET ACK/yankı gövdeleri, PWRB_04'ün
sonuç bekleme ve bilerek geçersiz PARAM ret bildirimleri kullanıldı.
E7 wire örnekleri gerçek RX/COBS/parser/dispatch'ten servise ulaşır.
E8 CSV örneği bulunmadığından ayrı sentetik 96 bayt ve 95 bayt ret testi
kullanıldı; alan anlamları doğrulanmış sayılmadı. GEN çatışması, ilk yazım,
yankı ret bitleri, stale E1, birlikte kapasite/oran, ayarsız ayar, iptal
akıbeti, erken bildirim, timeout, BOOT ve tick wrap kapsandı.

18 modül Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti. CubeIDE yeni iki
kaynağı managed build'e aldı; Release link başarılıdır.
Son link text/data/bss: 311164/504/126952 B; eşzamanlı diğer değişiklikler
de dahildir. Web navigation kaynak/gömülü ve web_auth entegrasyonu geçti.
Kanıtlar:
`test/build/scp-power-control-regression.log`,
`build/scp-power-control-strict.log`, `build/scp-power-control-release.log`,
`build/scp-power-control-final-build.log`. Seçili test sonucu tam firmware
suite sonucu değildir. Fiziksel UART/RF, enerji kesintisi ve güç kartı
uygulaması sınanmadı. Commit veya cihaz yükleme/reset yapılmadı.

BQ-08/BQ-12–14, K5/K9 ve fiziksel kabul açık kalır. Web/Modbus ayar yazma
arayüzü, yeni alarm IOA'ları ve otomatik akü değişimi tetikleyicisi eklenmedi.

## RF web Kaydet/Uygula bağlantısı — 06.10.2026

### Amaç ve kullanım yeri

Powerboard değişiklikleri `179205b` commit'ine alındı. Ardından RF ayar
ekranı mevcut grup servisine bağlandı. Kullanıcı K5 için istenen ayarın
Kaydet ile hemen NVRAM'e yazılmasını, Uygula'nın ayrı başlatılmasını ve
PARTIAL/FAILED sonucunda otomatik tekrar yapılmamasını onayladı.

### Kurallar

Kaydet mevcut staging/parse/commit/`rf_store_sync` yolunu kullanır;
uygulama başlatmaz. Uygula formdaki geçici değişikliği değil, store'daki
istenen ayarı snapshot (anlık kopya) olarak alır. Kaydedilmemiş değişiklik
ve boş/aralık dışı grup kimliği web'de gönderimden önce reddedilir.
Yeni grup kimliği operatör tarafından verilmelidir; MH'de kullanılmış
kimlik mevcut preflight (ön kontrol) ile reddedilir.

Grup servisi üç aynı WRITE, COMMIT ve sonuç/CRC doğrulamasını korur.
HTTP `started=true` yalnız yerel işlemin başlatılmasıdır; uygulanmış
sonucu sayılmamalıdır. Uygulama/iptal POST uçları mevcut admin kontrolünden
geçer. Durum GET'i oturum açmış izleme kullanıcısına açıktır; GET ayar
uygulayamaz. Başlatılamayan işlem 409, biçim/aralık hatası 400 döndürür.

Grup durumuna bir tabanlı store satırı ve `MatchesDesired` eklendi.
Karşılaştırma hedef fider/bölge, üç EUI ve writable CRC üzerinden yapılır.
Önceki snapshot APPLIED iken istenen ayar değişirse eski APPLIED sonucu
korunur, eşleşme false olur; ekran bunun önceki ayara ait olduğunu söyler.
Kaydet/yeniden oku web durum önbelleğini temizler. Eski ayar nesnesi için
başlamış ve geç dönmüş GET cevabı yeni durum önbelleğini dolduramaz.

APPLIED yalnız RAM işlem sonucudur; istenen ayarı veya NVRAM'i değiştirmez.
Reset sonrası eski APPLIED sonucu geri yüklenmez ve otomatik uygulama
başlatılmaz. Tek grup servisi son işlemi gösterir; her fider için kalıcı
uygulama geçmişi eklenmedi. NVRAM şeması ve SCP wire biçimi değişmedi.

### Kullanım

1. RF ayarını okuyup düzenleyin ve Kaydet ile saklayın.
2. Uygulama kartından etkin ayar satırını ve yeni grup kimliğini seçin.
3. Uygula ile başlatın; Yenile ile yerel sonucu ve son MH raporunu okuyun.
4. APPLIED ve CRC doğrulaması görülmeden ayırıcıların ayarı uyguladığı
   söylenmemelidir. İptal mevcut grup servisi kurallarını kullanır;
   BQ-11 açıkken bilinmeyen COMMIT öncesi kimlikle ABORT gönderilmez.

Yeni EUI/atama/bölge kaydı, MH'nin ACK verilmiş envanterinin aynı üyeleri
bildirdiği anlamına gelmez. Uygula bunu denetler; eşleşmeyen envanterle
ayar göndermez. Bu adım Kaydet'e otomatik envanter yükleme eklememiştir.

### API

| Yol | Kullanım |
|---|---|
| GET `/status/rf-group` | Son yerel işlem: State, Line, Feeder, GroupId, WritesAcked, ExpectedCRC, MatchesDesired; HasReport ve son MHState/MemberBitmap/Reason/ReportedCRC/Attempts |
| POST `/config/rf/apply/<satır>/<group_id>` | Store satırı 1–7, yeni group_id 0–255; request body kullanılmaz |
| POST `/config/rf/abort` | Mevcut işlemi mevcut iptal kurallarıyla sonlandırmayı başlatır |

Satır store indeksidir; SCP fider kimliğiyle aynı sayı olmak zorunda değildir.
Durum ekranında bu ikisi karıştırılmamalıdır. Ayrı arka plan web poll'ü
veya HTTP üzerinden otomatik yeniden uygulama eklenmemiştir.

### Doğrulama ve sınırlar

RF grup/adaptör/yetki paketleri 32/32; 24 ilgili Ceedling dosyası 378/378
geçti. Gerçek grup servisi ve CSV APPLIED raporu gerçek JSON'a bağlandı;
istenen ayar değişince eski APPLIED/eşleşme ayrımı ve NVRAM'e tekrar yazmama
sınandı. Gerçek HTTP router auth/admin/GET sınırları, handler yanıtları,
hatalı/taşan satır ve grup kimliği, küçük buffer, Kaydet/Uygula ayrımı,
boş kimlik, geç web yanıtı ve kaynak/gömülü Türkçe/İngilizce ekran sınandı.
Transport/NVRAM ve HTTP sınırları test çiftleridir; grup ve JSON algoritması
taklit edilmemiştir.

19 modül Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti; Release link
başarılıdır: text/data/bss 314144/504/126952 B. Eşzamanlı diğer kullanıcı
çalışmalarını da içerir. Kanıtlar `test/build/rf-group-web-regression.log`,
`build/rf-group-web-auth.log`, `build/rf-group-web-strict.log`,
`build/rf-group-web-release.log`, `build/rf-group-web-final-build.log`.
Seçili sonuç tam suite veya fiziksel RF/AY kabulü değildir.

Bu web adımı henüz commit edilmedi. K9 yeni alarm adresleri, BOLATeX
soruları ve fiziksel kabul açık kalır; K5 kalıcılık seçimi artık açık değildir.
Cihaz yükleme/reset veya fiziksel ayar gönderimi yapılmadı.

## Modbus SCP canlı veri bağlantısı — 06.10.2026

RF web adımı `ba578b9` commit'ine alındı. Sonraki kaynak incelemesinde
`iec104_process_init` içindeki `generate_dummy_test_data` çağrısının
`breaker` faz verisini doldurduğu; mevcut IEC104 ve Modbus okuyucularının
bu veriyi okuduğu doğrulandı. Gerçek RF cache hazır olmasına rağmen bu
canlı akım/RF alanlarına bağlı değildi.

[KARAR: 06.10.2026] Kullanıcı Modbus'ta mevcut adresleri koruyup geçersiz
akımda NaN kullanılmasını, RF durumunun ayrı gösterilmesini onayladı.
Anlık akım ve RF haberleşme getter (okuyucu) yolları `rf_get_phase_data`
kaynağına geçirildi. Aynı FC03 içinde RF kalite değerlendirmesi için tek
HAL zaman örneği alınır; tüm register callback'leri aynı cooperative
process bağlamında tamamlanır. Böylece 30 s sınırındaki iki FLOAT32 word
farklı geçerlilik kararları üretmez. Ek veri cache'i, IRQ kilidi veya
Modbus library API değişikliği eklenmedi.

Adres ve veri kurallarının tek kaynağı
[Modbus haritası §5.2.1](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md)
olmalıdır. Etkin olmayan satır/rezerve adres davranışı değişmedi.
Dummy üretici diğer henüz bağlanmamış alanlar için korunur; yeni iki
Modbus alanı bu veriye fallback (yedek kaynağa dönüş) yapmaz. Enerji bit 0
R1'de açık tanımlıdır; bit 1 yük akımıdır, eski nominal akım alanıyla aynı
olduğu varsayılmamalıdır. Bu kalan alanlar tamamlandı sayılmadı.

Gerçek RF cache/FC03 ortak fixture ile sabit ve yapılandırılabilir haritada
5'er yeni test; mevcut Powerboard FC03 6 testiyle alt küme 16/16 geçti.
Mevcut yanlış dummy değeri yerine gerçek 1.25 A, eksik/eski/negatif veri,
ölçüm geçersizken RF'nin güncel kalması, MH restart ve FLOAT32 eskime sınırı
sınandı. RF modeli ve Modbus core gerçektir; envanter/store/hardware
sınırları test çiftleridir. İlgili 30 dosyada 411/411 geçti; bunlar çalışma
ağacındaki diğer Modbus regression (gerileme kontrolü) paketlerini de
kapsar. Tam suite veya fiziksel IRQ/RF süre kabulü değildir.

20 modül sıkı Cortex-M33/C11 uyarılar/-Werror ile geçti; Release link
314224/504/126952 B text/data/bss ile başarılıdır. Eşzamanlı diğer kullanıcı
çalışmaları da dahildir. Kanıtlar `test/build/modbus-rf-focused.log`,
`test/build/modbus-rf-regression.log`, `build/modbus-rf-strict.log`,
`build/modbus-rf-release.log` içindedir.

LIVE_DATA ölçüm saati taşımaz. IEC104 için RTU alım saati / daima IV=1
seçimi bu Modbus adımında açık kalmıştı; sonraki IEC104 bölümünde kullanıcı
kararı ve uygulaması kaydedildi. Bu Modbus adımı IEC104
canlı okuyucularını veya tarihlerini değiştirmedi. Yeni Modbus adımı henüz
commit edilmedi; NVRAM düzeni, yeni IOA/register adresi ve cihaz
uygulama/reset işlemi değişmedi. K9 ve BOLATeX soruları açık kalır.

## IEC104 canlı veri ve RTU alım zamanı — 06.10.2026

[KARAR: 06.10.2026] Kullanıcı şimdilik RTU alım saatinin kullanılmasını
ve BOLATeX'e sorulmasını istedi. BQ-15, ayrı soru listesine eklendi.
Gönderilmiş veya üretici tarafından onaylanmış cevap olarak sunulmaz.

RF dispatch geçerli LIVE_DATA'yı çözerken geçerli yerel RTC'yi bir kez
CP56Time2a'ya çevirir. RTC geçersiz veya tarih alanları hatalıysa IV=1
kalır. `rf_phase_data_t.received_time` bu zamanı RAM'de saklar. Saf model
API'sinde verilmemiş zaman da IV=1'dir. Hatalı paket önceki geçerli
ölçüm/zamanı değiştirmez. Bu alan ayırıcının ölçüm zamanı veya UART'ın ilk
bayt zamanı değildir; RTU process'inin geçerli paket kabul saatidir.

IEC104 anlık akım ve RF haberleşme okuyucuları artık bu cache'i kullanır.
Sorgu sırasında yeni RTC okunmaz; eski kayıt geç gelen RTC düzeltmesiyle
veya yeni okuma saatiyle yeniden zamanlandırılmamalıdır. Yeni paket yeni
alım zamanını getirir. Olay kaydı aktarımındaki kaynak zamanı ve
clock_quality değişmez. Yeni NVRAM/Flash alanı ve IOA adresi eklenmedi.

Anlık akım geçersiz/eksik olduğunda değer 0 ve QDS IV=1 kullanılır. Eski
ama son ölçümü geçerli kayıt tutulur; QDS IV=1/NT=1 ile eskime gösterilir.
RF güncelken yalnız akım ölçümünün geçersiz olması RF SPI'yi 0 yapmaz.
Eski bağlantı SPI=0 ve NT=1 olarak, son paket alım zamanı ile gösterilir.
Kaynak/veri bilinmiyorsa SIQ IV=1 ve CP56 IV=1 kalır. Zaman IV bilgisi
ölçüm QDS kalitesiyle aynı şey değildir; geçerli akım, RTC bilinmiyorken
QDS IV=0 ve zaman IV=1 taşıyabilir.

Gerçek RF cache ve kayıtlı üretim okuyucuları için 5 yeni test, gerçek
RX/COBS/codec yolunda RTC/zaman korunması için 2 yeni test eklendi.
31 ilgili dosyada 418/418 geçti. Geç okuma, stale, negatif ölçüm/güncel
RF ayrımı, olmayan kaynak, NULL/aralık hatası, geçersiz RTC, RTC'nin sonradan
geçerli olması ve kısa paket sonrası zamanın korunması sınandı. Model/
okuyucu/codec gerçektir; clock, envanter/store ve hardware sınırları test
çiftleridir. Monolitik IEC104 testinde LTO/O2 yalnız ilgisiz socket/process
bağlantılarını link dışı bırakır; test edilen okuyucular üretim kodudur.

21 modül sıkı Cortex-M33/C11 uyarılar/-Werror ile ve Release link başarıyla
geçti. Link text/data/bss 314472/504/127048 B; alım zamanı metadata'sı RF
cache RAM'ini 96 B artırdı. Diğer eşzamanlı çalışmalar da linke dahildir.
HAL header'ının eklenmesi libiec104'ın mevcut Init makrosuyla çakıştı;
HAL tipleri bu makrodan önce parse edilerek include sırası düzeltildi,
vendor/HAL kaynaklarına yama yapılmadı. Kanıtlar
`test/build/iec104-rf-live-regression.log`,
`build/iec104-rf-live-strict.log`, `build/iec104-rf-live-release.log`.
Host/derleme fiziksel RF gecikmesi veya ölçüm zamanı doğruluğu kanıtı değildir.

Enerji, nominal/yük ve kalan alarm eşlemeleri bu adımın dışında kaldı;
bunların eski dummy üreticisi korunur. BQ-15 ve K9 açıktır. Bu yeni IEC104
adımı commit edilmedi; cihaz yükleme/reset veya dışarı mesaj gönderimi
bu çalışmada yapılmadı.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 05.10.2026 | 0.1 | R1, mevcut RF-SCP ve merkezi test altyapısına göre ilk öneri |
| 05.10.2026 | 0.2 | Kullanıcı K1–K3 kararları, R1 olay modelinin esas alınması ve açık konuların kaynak ayrımı |
| 05.10.2026 | 0.3 | Powerboard bildirimlerinin SCP kaynağı ve mevcut tüketicilerin geçiş kapsamı netleştirildi |
| 05.10.2026 | 0.4 | R1 uygulama onayı ve ilk fixture/çerçeve testlerinin 6/6 sonucu kaydedildi |
| 05.10.2026 | 0.5 | R1 codec/alan doğrulaması, 18 yeni test, 71/71 ilgili regresyon ve Cortex-M33 object/stack ölçümü |
| 05.10.2026 | 0.6 | Sürüm bağımsız adlar, rf_comm codec/timeout/retry/SEQ bağlantısı, 87/87 ilgili test ve CubeIDE Release build |
| 05.10.2026 | 0.7 | RTC kullanıcı kararı, açılış/envanter/discovery/epoch servisleri, 46/46 senaryo, 122/122 ilgili test ve Release link |
| 05.10.2026 | 0.8 | Geriye uyumluluk gerekmeyen R1 model/monitor geçişi, ACK kimlik aynası, 140/140 ilgili test ve web/Release doğrulaması |
| 05.10.2026 | 0.9 | Kullanıcı onaylı 8 KB RF olay günlüğü, örnek iletişim, 157/157 test ve Release doğrulaması; otomatik olay servisi bekliyor |
| 05.10.2026 | 0.10 | Otomatik olay çekme/tüketme, 1/7 kalıcı ve 3 geçici yönlendirme, 32 bit süre ve 246/246 ilgili regresyon |
| 05.10.2026 | 0.11 | a635d61 commit'i; sonraki IEC104 spontane/replay bağlantısı ve 250/250 test; 101/105 geç kayıt kararı bekleniyor |
| 05.10.2026 | 0.12 | BQ-01–09 ayrı belgeye taşındı; grup bloğu hedef/CRC/alan doğrulaması ve 253/253 ilgili test |
| 05.10.2026 | 0.13 | BQ-10 arıza sınıflaması ve BQ-11 ABORT kimliği; shell STATUS_GET ve 256/256 ilgili test |
| 06.10.2026 | 0.14 | Normal grup/kimlik/CRC sıralayıcısı, epoch bekleme ve shell akışı; 283/283 ilgili regresyon |
| 06.10.2026 | 0.15 | SCP Powerboard modeli, tüketiciler, 38 register haritası ve 319/319 ilgili test |
| 06.10.2026 | 0.16 | Powerboard E5–E8 kontrol/shell, uygulama doğrulaması, BQ-12–14 ve 343/343 ilgili test |
| 06.10.2026 | 0.17 | Powerboard commit 179205b; K5 kararı, ayrı RF web Kaydet/Uygula/durum/iptal ve 378/378 ilgili test |
| 06.10.2026 | 0.18 | RF web commit ba578b9; mevcut Modbus akım/RF alanlarının SCP kaynağı, NaN tercihi ve 411/411 ilgili test |
| 06.10.2026 | 0.19 | IEC104 canlı akım/RF kaynağı, kullanıcı onaylı RTU alım zamanı, BQ-15 ve 418/418 ilgili test |
