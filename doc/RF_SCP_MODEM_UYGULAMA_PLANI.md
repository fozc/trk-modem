# RF-SCP R1 modem uygulama planı

**Tarih:** 05.10.2026
**Durum:** Kullanıcı R1 davranışlarının uygulanmasını onayladı.
İlk beş adımın codec, istek, açılış/envanter ve canlı veri servisleri
tamamlandı. RF web monitorü güncel modele geçti. Altıncı adımda otomatik
olay çekme, 8 KB ham kayıt ve mevcut geçici/kalıcı arıza listesine aktarım
bağlandı. 101/105 alarm bağlantısı ve diğer ürün bağlantıları bekliyor.

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
| K5 | İstenen ayar NVRAM'e ne zaman yazılacak; APPLIED sonrası mı, daha önce desired (istenen) olarak mı? PARTIAL_COMMIT'te otomatik yeniden uygulama mı, operatör kararı mı? | İstenen/uygulanan ayrımı açık gösterilsin; kontrolsüz otomatik yeniden uygulama yapılmasın | 7 |
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
