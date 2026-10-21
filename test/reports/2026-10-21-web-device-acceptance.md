# Web arayüzü ve cihaz kabul testleri

**Sürüm:** 1.0 — mevcut düzenekte cihaz kabulü tamamlandı  
**Tarih:** 21.10.2026  
**Amaç:** Yeni firmware'in yüklenmesini, gerçek web arayüzünü ve cihaz haberleşmesini doğrulamak.  
**Kullanım yeri:** Endüstriyel arayüz incelemesinin cihaz kabulü.  
**Kapsam:** Kullanıcının tam test, firmware yükleme ve tarayıcıyla tüm alanları kontrol etme talimatı. Röle ON/OFF için güvenli test yükü ayrıca teyit edildi.

## 1. İmaj ve düzenek

Son imzalı paket `Release/troika-smart-breaker-modem_v1.0.1_20261021_134749_a2901519.efw` dosyasıdır. Firmware v1.0.1, web v1.4.13, üretim profili 0; uygulama 357676 B, CRC32 **AE80B796**. Build ve EFW self-check (raw byte equality + ECDSA verify) başarılıdır. Depo dirty durumdadır; önceden mevcut kullanıcı C değişiklikleri de imaja dahildir.

Paket COM18 üzerinden mevcut imzalı XMODEM akışıyla yüklendi. EOT onayı alındı. Boot logunda FW_INSTALL_OK sıra 1043 ve firmware approved YES gözlendi. Farklı firmware doğrudan ST-Link ile yazılmadı. İlk kapsamlı tarayıcı turu v1.4.12 ile yapıldı; yalnız IEC104 yardım metni ve sürüm etiketi değiştirilerek son v1.4.13 imajı üretildi. Son imaj ayrıca cihaz testlerine alındı.

| Bağlantı | Doğrulanan hedef |
|---|---|
| COM18, 230400 | RTU konsolu |
| COM15 / COM16, 230400 | AY, sırasıyla L2 / L1 |
| COM10, 115200, ID 23, 1 stop bit | Gerçek RS485 Modbus |
| HTTP | Test cihazı web arayüzü, port 80 |
| IEC104 | Aynı cihaz, port 2404 |
| MH | Gerçek MH firmware 022555bf |

Fider 1'de iki gerçek AY vardır; L3 EUI `AABBCCDDEEFF0003` test tanımıdır. Üç gerçek AY ile APPLIED sonucu bu düzenekte doğrulanamaz. Dummy üreticiler veya kapalı BMS reader bu test için etkinleştirilmedi.

## 2. Test sonuçları

| Kanıt | Sonuç ve sınır |
|---|---|
| Tam Ceedling unit | 1574/1574 PASS, 0 FAIL, 0 IGNORE; fiziksel zamanlama kanıtı değildir |
| Entegrasyon | Röle paketi dışındaki 13 paket exit 0; cihaz kapıları olan paketlerin host kontrolleri ile fiziksel koşuları ayrı değerlendirilir |
| Kalıcı web audit regresyonu | 16 kontrol × kaynak/gömülü = 32 PASS; v1.4.13 yardım metni dahil |
| Gerçek HTTP API | Oturum, board schema, RF durum/parser, sabit Modbus haritası, ETag/304 ve StopBits geçici değişimi/geri yükleme PASS |
| Gerçek RS485 Modbus | İlk tur 23 PASS; ikinci röle döngüsü eklenen son paket 29 PASS, 0 FAIL. Host gönderim aralığı ve USB dönüştürücü dahil süreler telde fiziksel zamanlama ölçümü değildir |
| Gerçek MH | m1 firmware, m2 envanter, m3 canlı veri: 3 PASS; 12 canlı frame, iki kaynak, Boot_Counter > 0 |
| IEC104 | 13 PASS, 0 FAIL/SKIP/DEFERRED/ERROR → ACCEPTANCE PASS; bağımsız c104 ve socket master ile |
| Tarayıcı | Son matris 245 PASS, 0 FAIL, 2 sonuçlandırılamayan hassas alan taslak kontrolü; ilk tur v1.4.12, son sürüm/kalıcılık/discovery kontrolleri v1.4.13 |
| Reset ve NTP | Silinen EUI resetten sonra boş kaldı; discovery'den yeniden atama da son reset/karşılaştırmada korundu. NTP UTC + timezone=3 takvimi gerçek konsol kaydıyla PASS |

Entegrasyon paketleri: contiki_process, fault_log, gsm, libs, nvram, rfwu_auth, rf_hub_sim, rf_hil, rf_real_mh, iec104_master, web_auth, web_navigation, web_device. `rf_hil` burada simulator/codec host testidir; HIL profilli fiziksel simulator koşusu yapılmış sayılmaz. Gerçek MH ve IEC104, ayrıca açık hedef verilerek çalıştırılır.

`web_device` helper self-test çıktısındaki kasıtlı FAIL satırları, hatalı restore/HTTP sonuçlarını yakalayan test senaryolarıdır; unittest sonucu OK'dir. Gerçek cihaz koşusunun PASS satırları ayrı bölümde bulunur. Ham loglar bu ayrım korunarak okunmalıdır.

## 3. Tarayıcı alan kontrolü

| Ekran | Yapılan kontrol |
|---|---|
| IEC104 | Genel alanlar ve görünen dört satırın IOA alanları; boş sayı reddi, etkinlik, SBO aralığı, T2/T1 ilişkisi, çakışan IOA, TR/EN taslak koruma. PeriodicSend 901 kaydedilip cihazdan okundu; 900'e geri döndürüldü |
| Modbus | ID tam sayı doğrulaması ve seçenekler; ID 100/19200/2 tarayıcıdan kaydedildi, gerçek RS485 FC03 yanıtı alındı; 23/115200/1'e dönüldü |
| Register haritası | Dokuz veri grubu; dört fider × üç faz × üç görünüm ve son arıza indeksi dahil 41 seçim kontrolü |
| RF yapılandırma | Görünen dört satır, sayısal alanlar, çalışma modu/frekans, EUI biçimi ve tekrar eden EUI reddi, Ia/nominal ilişkisi, Zone kilidi, Temizle/Kaydet onayı |
| RF izleme | Dört fider, fazlar, EUI ve geçersiz ölçüm sunumu; silinen L2 için ölçüm yok, L1 korunuyor |
| Cihaz durumu | Güç/ADC/alarmlar/GSM alanları, teknoloji ayrıntıları, teknik ayrıntılar, bilinmeyen/geçersiz değer sunumu |
| Cihaz yönetimi | On salt okunur kimlik alanı, dokuz sayısal ayarın boş giriş reddi, PIN aralığı, APN/NTP metin taslakları, dil değişimi; son imajda başlangıç değerleriyle gerçek Kaydet kabul edildi |
| Arıza kayıtları | Dört fider; ilk fiderde L1/L2/L3 geçici/kalıcı kayıtlar, boş fiderlerde kayıt yok |
| Sistem günlükleri | İlk 30 kayıt ve sonraki sayfa; firmware/konfigürasyon kayıtları |
| Servis konsolu | whoami → root yanıtı gerçek web çıkışında, ekranı temizleme |
| Firmware ekranı | Oturumlu açılış, dosyasız başlat/uygula/duraklat kapalı, durum sorgusu → etkin aktarım yok |
| Kullanıcı rolü | Admin çıkışı sonrası eski sayfa temizleniyor, user cihaz durumu ile başlıyor, ayrıcalıklı menüler yok, RF/Modbus salt okunur ve Kaydet yok |

Form taslakları sunucuya kaydedilmiş ayar gibi sayılmamalıdır. SIM/APN, NTP hedefi, web portu ve erişimi kesebilecek diğer ağ alanları keyfî değerlere değiştirilmedi. Üretim ayarlarının tüm olası değerlerinin fiziksel kabulü yapılmış sayılmaz. Web firmware ekranında gerçek dosya aktarımı bu turda yapılmadı; imzalı yükleme XMODEM üzerinden doğrulandı.

Tarayıcı aracı APN kullanıcı adı/şifresini `<redacted>` olarak maskeler. Bu iki alanın özel karakterli taslağını araç üzerinden karşılaştırmak güvenilir sonuç vermedi; ürün hatası olarak kaydedilmedi. Taslaklar kaydedilmeden cihazdan yeniden okundu. İlk son-sürüm EN karşılaştırmasında Türkçe seçili kaldı ve kontrol kırmızı oldu; yeni oturumda görünür İngilizce metin, yüklü formda TR'ye dönüş ve tarayıcı error/warn kaydının boş olduğu ayrıca doğrulandı. İngilizce yardım metni düzeltmesi PASS; ilk düğme gözleminin nedeni belirlenmedi. Ham alan matrisi `.temp_files/full-web-20261021/browser-checks.json` içindedir.

## 4. AY EUI silme

Gerçek Fider 1/L2 `00124B0038C9F1AE` için Temizle → Kaydet uygulandı. RTU'nun boş EUI kaydı cihazdan tekrar okundu. MH, INVENTORY_UPDATE seq 55, iki INVENTORY_SET ve INVENTORY_END seq 58 için ACK verdi; envanter iki cihaza indi. RF İzleme'de L2 EUI ve ölçümleri yok oldu; L1 korundu. Eski EUI tekrar yazılınca üç cihazlık envanter yeniden tamamlandı.

AY COM15 `config` çıktısında Zone 1/Phase 2 kaldı ve `src=0x06` LIVE yayını devam etti. Dolayısıyla **RTU/MH atamasının kaldırılması, AY korumasının veya yayınının durması anlamına gelmez**. BQ-26'ya tarihli yerel kanıt eklendi; üretici yanıtı veya yeni dış gönderim yoktur.

Son v1.4.13 imajında silme tekrarlandı. Yeni cihaz okumasında L2 boş, L1 eski EUI olarak kaldı. UI, MH envanteri doğrulandı ve eksik üyeli fider atlandı sonucunu gösterdi. RTU resetlendi; konsolda iki cihazlık envanter yeniden yüklendi, tarayıcıdan taze okumada L2 yine boştu. Sonra L2 eski EUI'ye döndürüldü; iki gerçek AY'nin RF İzleme durumu Güncel oldu. Aynı ayar tekrar gönderildiğinde yalnız bulunmayan üçüncü üye NOT_LIVE olarak raporlandı. Böylece envanter başarısı ile koruma ayarlarının APPLIED sonucu ayrıldı.

**Discovery üzerinden yeniden atama ayrıca test edildi:** Kullanıcının takip sorusu üzerine EUI yeniden silindi; `/discovery/rf` yenilemesinde `00124B0038C9F1AE` listelendi. Fider 1 seçili bırakılıp listedeki L2 düğmesine basıldı; EUI forma geldi ve dirty oldu. Bu denemede EUI elle yazılmadı. Kaydet sonrası cihazdan yeni okumada L2 eşleşti; discovery yenilendiğinde atanan EUI listeden çıktı. İlk geri atama başlangıç snapshot'ındaki EUI alana yazılarak, bu ek deneme ise doğrudan listedeki düğmeyle yapıldı.

Kaynakta `rf_inventory.c` silme ACK yolunda `rf_discovery_add(&body[3])` çağırır; bu yol yeni RSSI ölçümü üretmez. Gerçek SCP DISCOVERY_REPORT ayrı `rf_discovery_report()` yoludur. Dolayısıyla burada listelenme, silinen bilinen EUI'nin atanmamışlar kuyruğuna alınmasını kanıtlar; yeni kablosuz discovery mesajı alındığı iddia edilmez.

Reset testi elektrik beslemesinin fiziksel kesilmesi veya Flash yazması sırasında güç kesintisi testi sayılmaz. Kanıt görselleri: `eui-deleted-after-reset.png`, `final-rf-live-restored.png`, `discovery-deleted-eui.png`, `discovery-reassigned-readback.png`.

## 5. Yeni bulgular ve kalan sınırlar

- IEC104 yardım metni, 0 değerinin gönderimi kapatmadığını söylüyordu. Üretim kodundaki `periodical_send_interval != 0` koşulu ve mevcut protocol scenario testi 0'ın kapalı olduğunu doğruladı. Yalnız TR/EN açıklaması düzeltildi; kalıcı kaynak/gömülü kontrol eklendi. Web v1.4.13 bu düzeltmeyi içerir.
- Cihaz yönetiminde RF firmware v0.0.0 görünürken gerçek MH 022555bf bildirdi. HTTP alanı NVRAM `rf_firmware_version` değerini gösterir; MH GET_STATUS kimliğiyle aynı veri kaynağı değildir. Bu alan doğrulanmış çalışan MH sürümü olarak kullanılmamalıdır.
- MCC/MNC/LAC/CI alanları boş görüldü; hücresel bağlantı çalışıyordu. JSON bu alanları `config->coordinates` üzerinden üretir. Güncel baz istasyonu kimliği olduğu kanıtlanmadı.
- Cihaz saati ve bazı kayıtlar UTC etiketiyle gösteriliyor; mevcut NTP yolu timezone=3 eklenmiş RTC kullanırken IEC104 saat komutu alınan UTC değerini doğrudan uygular. Fiziksel testte tarayıcı UTC ile cihazın başlangıç saati üç saat farklıydı. Saat sözleşmesi ayrı çözülmelidir; bu testte sessizce değiştirilmedi.
- 850 px altındaki CSS, aktif hedef/rol ve web sürümünü taşıyan header hint alanlarını gizler. Test görünümünde bu kimlikler sürekli görünür değildir. Küçük ekranlarda da hedef/rol görülebilmelidir.
- Tek HTTP istemcisi sınırı test düzenini etkiledi. Son browser turu kapatıldıktan sonra ayrı API istemcisinin login denemeleri WinError 10053 ile kesildi; konsol Web CONNECTED/IDLE gösteriyordu. Bunun kök nedeni bu testte kesinleştirilmedi. Son kalıcılık/ayar karşılaştırması için cihaz yeniden başlatılıp tarayıcı kapalı tutuldu. Eşzamanlı çok istemci kabulü yapıldığı iddia edilmez.
- Üç gerçek AY bulunmadığından APPLIED/üç cihaz CRC kabulü açık kalır. Eksik/dummy üçüncü üye için NOT_LIVE sonucu başarılı ayar aktarımı gibi yorumlanmamalıdır.
- Fiziksel DIP kombinasyonları, analog doğruluk, röle kontağı/gerilim ölçümü, RF menzil ve elektromanyetik dayanım bu yazılım testiyle doğrulanmış sayılmaz.

## 6. Kanıt dosyaları

Ham kayıtlar `.temp_files/full-web-20261021/` altındadır: `host-unit.log`, `integration-*.log`, `modbus-device.log`, `modbus-device-both-relays.log`, `iec104.log`, `iec104/apdu.jsonl`, `real-mh/report.md`, `rf-removal-console.log`, `COM15-after-delete.log`, `browser-checks.json`, `final-release.log`, `final-xmodem-transfer.log`, `final-approval.log`, `final-bootlog.log`. Başlangıç ve son snapshot dosyaları erişim bilgileri içerdiğinden rapora kopyalanmamalıdır.

### 6.1 Son cihaz durumu

Discovery üzerinden yeniden atama sonrasında RTU tekrar resetlendi.
Envanter üç kayıtla yeniden yüklendi. `final-restoration.log` ve
`restoration.json`, RF, IEC104, Modbus ve cihazın bütün değiştirilebilir
alanlarında başlangıç kopyasına göre sıfır fark gösterir; son karşılaştırma
hiçbir ek ayar yazması gerektirmedi. İlk geçici IEC104 inUse değişikliği
önceden geri alınmıştı. Son konsol: röle CH1 OFF, CH2 OFF ve RF grup IDLE.
Modbus ID 23/115200/1, IEC104 PeriodicSend 900 ve başlangıçtaki kapalı
hatlar korundu. NTP kaynak UTC + timezone=3 takvimi uygulandı.

Son kanıtlar: `final-restoration.log`, `restoration.json`,
`discovery-restore-reset/result.json`,
`discovery-restore-reset/console.log`, `final-close-console.log`.
İlk başarısız son-karşılaştırma denemesindeki login WinError 10053,
son başarılı koşunun sonucu gibi okunmamalıdır. Sentetik IEC104 olayları
116–127 test kayıtlarıdır; ACK/replay testleri sonrası unsent=0 doğrulandı.
Mevcut günlükler topluca silinmedi. Commit/push yapılmadı.

## 7. Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 21.10.2026 | 1.0 | Firmware yükleme, alan matrisi, gerçek MH/Modbus/HTTP ve kalan kabul koşulları |
