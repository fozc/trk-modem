# IEC104 yapılandırma ve adres rehberi

Sürüm: 1.2
Tarih: 2026-10-03

## Amaç

Bu belge modem firmware'indeki aktif IEC104 yapısını, IOA (bilgi nesnesi
adresi) alanlarını ve arıza kayıtlarının adres hesabını açıklar. Kodun
mevcut davranışını anlatır; genel IEC standardının yerine geçmez.

## Kullanım yeri

SCADA nokta listesi hazırlanırken, web'den fider adresleri ayarlanırken ve
adres çakışması araştırılırken kullanılır. Tablolar yeni fabrika
varsayılanlarıdır. Mevcut cihazın kayıtlı değerleri ayrıca okunmalıdır.

## Terimler ve roller

| Terim | Anlam |
|---|---|
| Fider | Yedi güç hattından biri; kullanıcıya 1–7 olarak gösterilir |
| Fider indeksi | JSON dizilerinde ve adres hesabında kullanılan 0–6 değeri |
| Faz | R/L1=0, S/L2=1, T/L3=2 |
| IOA | 3 byte adres; yapılandırılan veri noktaları için 1–16777215 |
| CA / CommonAddr | Ortak adres; global yapılandırmada 1–65535 |
| OriginatorAddr | Gönderici adresi; 0–255 |
| ASDU | IEC104 uygulama verisini taşıyan bölüm |
| GI / interrogation | SCADA'nın veri sorgulaması |
| Replay | Bağlantı kesikken biriken olayların yeniden gönderilmesi |

Modem veri ve komut yanıtlarını üretir; SCADA sorguları ve komutları
gönderir. Taşıma ve bağlantı
işleri `iec104_process.c`/GSM; paketler `libiec104`; grup 3/4 süreçleri
`iec104_application.c`; biriken olaylar `iec104_replay.c` içindedir.

## Genel yapılandırma

| Web/JSON alanı | Yeni fabrika değeri | Açıklama |
|---|---:|---|
| Port | 2404 | IEC104 portu |
| CommonAddr | 1 | Ortak adres |
| OriginatorAddr | 1 | Gönderici adresi |
| PeriodicSend | 900 | Saniye; gerçek periyodik veri üretimi aşağıda açıklanır |
| T0 / T1 / T2 / T3 | 90 / 45 / 30 / 60 | Saniye |
| K / W | 64 / 24 | Gönderim/onay pencereleri |
| SBO | false | Select-before-operate etkinliği |
| SBOTimeout | 60 | Saniye; etkinse backend 1–300, depolama 16 bit |
| AkuUyarisi | 10000 | Akü uyarısı için ayrılmış IOA |
| ModemReset | 10001 | Reset komutunun yapılandırılabilir IOA'sı |

Akü uyarısı IOA'sı yapılandırmada vardır; incelenen aktif IEC104 kodunda
bu IOA'dan akü uyarısı yayımlayan bir yol bulunmamıştır. Alanın olması,
bu yayının tamamlandığı anlamına gelmez.

`C_RP_NA_1` reset işleyicisi IOA=0 veya yapılandırılan ModemReset adresini
kabul eder. QRP=1 için onay ve ardından gecikmeli reboot yapılır. QRP=2
mevcut kodda onaylanır; bekleyen olayları temizleme işi TODO durumundadır.
Diğer QRP değerleri olumsuz onay alır. Yapılandırmadaki sıfır IOA yasağı,
protokol reset komutunun IOA=0 kullanımını değiştirmez.

## Fiderin tek nokta adresleri

Her aktif fiderde 3 faz × 7 kategori = 21 yapılandırılabilir nokta vardır.
Yeni fabrika tabanı `B = 1000 + fider_indeksi × 100` değeridir.

| Kategori / JSON key sonu | R / S / T adresi | Tip | Kullanım |
|---|---|---|---|
| ArizaAkimi | B / B+1 / B+2 | M_ME_TF_1 | Son arıza akımı |
| ArizaSuresi | B+10 / B+11 / B+12 | M_ME_TF_1 | Son arıza süresi |
| ArizaTuru | B+20 / B+21 / B+22 | M_SP_TB_1 | Kalıcı arıza durumu; C alanı `ariza_kalicimi` |
| AnlikAkim | B+30 / B+31 / B+32 | M_ME_TF_1 | Anlık akım |
| EnerjiVarYok | B+40 / B+41 / B+42 | M_SP_TB_1 | Enerji durumu |
| NominalAkimVarYok | B+50 / B+51 / B+52 | M_SP_TB_1 | Nominal akım durumu |
| RfhabVarYok | B+60 / B+61 / B+62 | M_SP_TB_1 | RF haberleşme durumu |

JSON key örneği: `Hatlar.IOA_R_ArizaAkimi[0]` birinci fiderin R fazıdır.
M_ME_TF_1 float ölçüm ve zaman damgası, M_SP_TB_1 tek bit durum ve zaman
damgası taşır. Son arıza ile kayıt geçmişi ayrı adres bölümleridir.

## Geçici ve kalıcı arıza kayıtları

Her bölüm fider ve faz başına en fazla 15 kaydın adresini ayırır.
Bir kayıt dört alan taşır; faz başına 60, fider başına 180 adres gerekir.
Gerçek gönderim kayıt sayısına göre yapılır; ayrılmış bütün adreslerin
her sorguda gönderilmesi gerekmez.

| Alan | Kayıt içi ofset | Tip |
|---|---:|---|
| Arıza akımı | 0 | M_ME_TF_1 |
| Arıza süresi | 1 | M_ME_TF_1 |
| Enerji var/yok | 2 | M_SP_TB_1 |
| Nominal akım var/yok | 3 | M_SP_TB_1 |

Aktif alan getter'larının hesabı:

```text
IOA = taban + fider_indeksi × 180 + faz_indeksi × 60
      + kayıt_indeksi × 4 + alan_ofseti
```

Kayıt indeksi 0–14'tür. İlk kayıt `fault_log_read_nth` sıralamasındaki
0. kayıttır; SCADA olayın zamanını CP56Time2a zaman damgasından okur.
Taban değerinin üstüne fider ofseti ayrıca eklenir. Bu mevcut sözleşme
korunmuştur; taban doğrudan ilk gönderilen adres sanılmamalıdır.

Web/JSON'daki iki alan `Hatlar.TemporaryFaultBase` ve
`Hatlar.PermanentFaultBase` dizileridir. Her fider için tek taban vardır.
Örneğin ikinci fiderin geçici tabanı 101000 ise ilk adres 101180;
R fazı 101180–101239, S fazı 101240–101299, T fazı 101300–101359 olur.

## Yeni fabrika adres planı

| Fider | İndeks | Nokta tabanı B | Geçici taban | Kullanılan geçici aralık | Kalıcı taban | Kullanılan kalıcı aralık |
|---|---:|---:|---:|---|---:|---|
| 1 | 0 | 1000 | 100000 | 100000–100179 | 200000 | 200000–200179 |
| 2 | 1 | 1100 | 101000 | 101180–101359 | 201000 | 201180–201359 |
| 3 | 2 | 1200 | 102000 | 102360–102539 | 202000 | 202360–202539 |
| 4 | 3 | 1300 | 103000 | 103540–103719 | 203000 | 203540–203719 |
| 5 | 4 | 1400 | 104000 | 104720–104899 | 204000 | 204720–204899 |
| 6 | 5 | 1500 | 105000 | 105900–106079 | 205000 | 205900–206079 |
| 7 | 6 | 1600 | 106000 | 107080–107259 | 206000 | 207080–207259 |

Nokta alanları 1000–1662 içinde, global alanlar 10000/10001 adreslerinde,
geçici kayıtlar 100000 bölgesinde, kalıcı kayıtlar 200000 bölgesindedir.
Tablodaki aralıkların son değerleri dahildir. Kullanılmayan boşluklar
adres hesabına ek veri noktası koymaz.

Bu plan yalnız fabrika varsayılanlarını yüklerken uygulanır. Firmware
güncellemesi sırasında mevcut NVRAM adresleri otomatik değiştirilmez.
NVRAM şeması ve kalıcı alan genişlikleri değiştirilmemiştir.

## Sorgulama ve olay gönderim akışları

| İstek / yol | Aktif davranış |
|---|---|
| C_IC_NA_1, QOI=20 | Son arıza akımı/süresi, anlık akım; enerji, nominal akım ve RF durumu |
| QOI=21, grup 1 | Son arıza akımı/süresi ve anlık akım |
| QOI=22, grup 2 | Enerji, nominal akım ve RF durumu |
| QOI=23, grup 3 | Geçici arıza kayıtları; aktif fiderler, bütün fazlar |
| QOI=24, grup 4 | Kalıcı arıza kayıtları; aktif fiderler, bütün fazlar |
| Diğer QOI | Olumsuz interrogation onayı |
| Replay, COT=3 | Biriken olaydan son arıza akımı/süresi, kalıcı arıza, enerji ve nominal akım noktaları |

Grup 3/4 gönderimi kuyruk veya pencere dolarsa kaldığı yerden devam eder.
Bağlantı kapanırsa süreç sonlandırılır. Bu süreçler replay ile mevcut
semafor üzerinden sıraya girer.

Replay kayıt geçmişinin taban adreslerini kullanmaz; yukarıdaki beş son
olay noktasını kullanır. Bir fider sonradan kapatılmış olsa da kayıtlı
olay gönderilebilir. Adres doğrulaması kapalı fiderin noktaları arasındaki
çakışmayı değerlendirmez; kapalı fiderde bekleyen replay kaydı varken
adres planını değiştirmek ayrıca değerlendirilmelidir.

`ArizaTuru` için gönderim fonksiyonu vardır ve spontane olayda kullanılır;
mevcut GI 20/21/22 listesinde bu fonksiyon çağrılmaz. Periyodik timer'lar
vardır, fakat mevcut periyodik gönderim fonksiyonları veri yayını açısından
TODO durumundadır. Dummy veri üreticileri bu çalışmada korunmuştur.

## Kayıt kuralları ve hatalar

- Global adresler ve aktif fiderlerin bütün nokta/kayıt aralıkları kayıt
  başlamadan backend'de birlikte doğrulanmalıdır.
- Aktif noktalar sıfır olmamalı, 24 bit sınırını aşmamalıdır.
- Aktif arıza tabanı sıfır olmamalı; türetilen son adres de 24 bit içinde
  kalmalıdır. İki aralık sınırda aynı adresi paylaşmamalıdır.
- Kapalı fiderin arıza tabanları da 24 bit depolama sınırında kalmalıdır;
  kapalıyken sıfır taban saklanabilir. Açılırken tam doğrulama yapılmalıdır.
- Kısmi güncellemede gönderilmeyen arıza tabanları korunmalıdır.
- Geçersiz adres durumunda global config, fider config ve flash sync
  çağrıları yapılmamalıdır.

Web kayıt isteğinden önce nokta ve arıza aralıklarını kontrol eder.
Çakışan iki alan kırmızı işaretlenir; alan altında karşı fider/faz/alan
belirtilir. Hatalı fider sekmesi açılır. Hata düzeltilmeden POST gönderilmez.
Modbus sayfasında da aynı başlangıç adresi ve register örtüşmesi
kontrol edilir. Arıza akımı ve anlık akım iki register, diğer görünen
fider alanları bir register kullanır. Kapalı fiderler çakışma kontrolüne
katılmaz.

Backend aynı kontrolü tarayıcı dışındaki istekler için de yapar; HTTP 400 yanıtının düz metin
gövdesi ilk bulunan geçersiz alanı, çakışmada iki alanı belirtir.
Web yanıtta belirtilen alanları da işaretler; JSON key isimleri yerine
alan etiketlerini kullanır. Dizi indeksi sıfırdan,
parantezdeki fider numarası birden başlar.

Örnek hata:

```text
IOA overlap: Hatlar.IOA_R_ArizaAkimi[1] (feeder 2) and
Hatlar.TemporaryFaultBase[0] (feeder 1)
```

Eski varsayılanlarda çakışma veya daha önce sıfırlanmış taban bulunabilir.
Web'den değerler okunmalı; gerekli fiderlerin iki tabanı SCADA planına
uygun düzeltilip aynı istekle kaydedilmelidir. Fabrika reseti zorunlu değildir.
Adres değişikliği SCADA eşleştirmesiyle birlikte yapılmalıdır.

### Modbus adres aralığının görünümü

Modbus adres alanının altında veri tipi, register (16 bit kayıt) sayısı
ve kullanılan adres aralığı gösterilir. Başlangıç adresi yazılırken bilgi
hemen güncellenir. Arıza akımı ve anlık akım FLOAT32 olarak iki register;
diğer fider alanları UINT16 olarak tek register kullanır.

Örnek: L1 arıza akımı 40001 seçildiğinde `FLOAT32 · 2 register ·
40001–40002` görünür. L2 başlangıcı 40002 seçilirse ortak adres 40002'dir.
Save sırasında iki alan işaretlenir; hata metni ortak register adresini
ve diğer alanın fider/faz/etiketini gösterir. L2 başlangıcı 40003 yapılırsa
bu iki alanın aralığı çakışmaz. Bu görünüm register haritasını değiştirmez.

## Doğrulama ve kaynaklar

- `test/web_server/test_json_config_bounds.c`: gerçek parser/setter,
  nokta/nokta ve nokta/aralık çakışması, iki aralık, sınır taşması,
  kısmi kayıt, taban düzeltmesi ve hata metnindeki alan adları.
- `test/iec104/test_iec104_protocol_scenario.c`: gerçek arıza getter'larının
  yedi fider için ilk/son adresi; mevcut protocol loopback senaryoları.
- `test/integration/nvram/test_nvram_sync.c`: gerçek fabrika varsayılanları.
- `test/integration/web_navigation/test_navigation.js`: kaynak/gömülü HTML,
  bütün 23 IEC104 ve 21 Modbus adres alanının kategori/fiderler arası
  çakışması, POST engellenmesi ve alan üzerinde hata gösterimi.

Doğrulama sonucu: 339/339 Ceedling testi ve 9/9 entegrasyon paketi geçti.
ARM Release derlemesi başarılıdır. Adres kontrolünün ölçülen yerel stack
kullanımı GCC `.su` çıktısında 1504 byte; linker stack ayarı 4096 byte
olup değiştirilmemiştir. Bu ölçüm tüm çağrı zincirinin cihazda ölçülmüş
stack tepe değeri değildir.

Host testlerinde donanım servisleri mock/fake kullanır. Fiziksel cihazda
SCADA kabul testi bu çalışmada yapılmamıştır.

Kaynak dosyalar: `Application/nvram.c`, `Application/nvram.h`,
`Application/types.h`, `Application/libiec104/iec104_config.c`,
`Application/libiec104/iec104.c`, `Application/iec104_process.c`,
`Application/iec104_application.c`, `Application/iec104_replay.c`,
`Application/web-server/json_config.c`,
`Application/web-server/http_handlers.c`, `web-page/index.html`.

## Değişiklik geçmişi

| Sürüm | Tarih | Değişiklik |
|---|---|---|
| 1.0 | 2026-10-03 | Aktif noktalar, arıza adres hesabı, yeni varsayılan plan, kayıt hataları ve mevcut sınırlar |
| 1.1 | 2026-10-03 | Save öncesi adres kontrolü ve alan üzerinde hata gösterimi |
| 1.2 | 2026-10-03 | Modbus veri tipi ve canlı register aralığı, çakışan register adresinin hata metninde gösterimi |
