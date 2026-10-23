# Cihaz ayar yazımı ve cihaz test betiği düzeltmeleri

**Sürüm:** 1.0  
**Tarih:** 2026-10-23

## 1. Amaç ve kullanım yeri

Kullanıcının düzeltme talebiyle
[inceleme raporundaki](2026-10-23-json-device-e2e-review.md) üretim ve test
bulguları ele alındı. Değişiklikler modem HTTP ayar kaydı ve canlı cihaz
testinin güvenilirliğini kapsar. Bu çalışmada cihaza yazılım yüklenmedi;
fiziksel RTC, GSM reconnect ve RF uygulama kabulü yapılmadı.

## 2. Üretim değişiklikleri

`modem_config_set()` artık `web_first_data_timeout_sec`,
`web_idle_timeout_sec`, `iec104_first_data_timeout_sec`,
`iec104_idle_timeout_sec` ve `commissioning_time` alanlarını mevcut NVRAM
modeline aktarır. Yeni alan, schema, kuyruk veya kalıcılık zamanlayıcısı
eklenmedi; mevcut sync yolu korunur. Gereksiz dört ayrı setter yerine
toplu setter içindeki eksik aktarımlar tamamlandı.

HTTP ayar POST'u, `Time` anahtarının varlığını mevcut ek seçenek taramasında
belirler. `parse_output_retention` yerine `parse_device_options` kullanılır;
üçüncü parametre açık Time varlığını bildirir. JSON belge doğrulaması
tamamlanmadan saat veya ayar işlemi yapılmaz. `Time` yoksa RTC çağrılmaz.
Varsa mevcut datetime dönüşümü ve `rtc_sync()` kullanılır. Böylece donanım
RTC, yazılım saati ve epoch aynı servis üzerinden güncellenir; 2000..2099
calendar sınırı ve RTC servisinin mevcut tarih kabulü korunur.

Ayar sync başarısızsa saat güncellenmez. Ayar kaydı başarılı ama RTC işlemi
başarısızsa HTTP 500 ve `Configuration saved but clock update failed`
döner; genel başarı bildirilmez. Bu iki ayrı fiziksel işlem atomik değildir:
bu durumda diğer ayarlar kaydedilmiştir, saat için başarı iddiası yoktur.
RTC saati mevcut donanım kalıcılığına aittir; NVRAM'daki eski `time`
alanı canlı saatin kaynağına dönüştürülmedi. GET mevcut RTC epoch'unu okur.

## 3. Test betiği değişiklikleri

- IEC adreslerinin geri yüklemesi normal akışta ve finally içinde ortak
  fonksiyondan geçer. Geçici açılan hat için önce hat açıkken eski IOA'lar
  yazılır; sonra başlangıç etkinlik durumu geri alınır. Son GET tam
  karşılaştırılır. Temizlik, mutasyon POST'undan önce hazırlanır; cevap
  kaybolsa bile başarılı olmuş yazmanın geri alınması denenir.
- SBOTimeout için başlangıçtan farklı değer seçilir ve tam eşitlik aranır.
  Son geri yükleme GET hataları FAILURES'a eklenir; sessiz başarılı çıkış
  engellenir. Authentication retry tükenmesi de açık hata üretir.
- RF config POST varsayılan çalışmaz. `--allow-rf-apply` açıkça verilirse
  aynı ayarların bile gerçek RF uygulaması başlatabileceği kabul edilir.
  Bu seçenek RF değer mutasyonu veya fiziksel RF başarı kanıtı değildir.
- Device ayar ve geri yükleme gövdeleri `Time` içermez. Böylece eski
  snapshot saati tekrar uygulanmaz. Saat yazma kanıtı host senaryosundadır.
- RF discovery GET şeması eklendi; RF monitor'de mevcut HasData/Live/Trip
  koşullarına göre ek anahtarlar denetlenir. Veri olmayan dallar canlı
  telemetrinin iç alanlarını doğrulamış sayılmaz.
- Escape helper non-BMP Unicode karakterlerini doğru surrogate çiftiyle
  kodlar. Boş IOA test penceresi bulunamazsa adres taşırmak yerine hata
  verir. Kapsam seçili alanlar olarak adlandırılır; aynı değer escape ve
  geçersiz değer reddi, geçerli yeni değerin saklanma kanıtı sayılmaz.

## 4. Doğrulama ve tekrar kullanım

Kalıcı Ceedling testleri toplu setter'ın beş alanını, yalnız açık Time
anahtarında RTC çağrısını, epoch-calendar dönüşümünü, RTC başarısızlığının
500 dönmesini ve seçenek parser'ının hata/partial/escape yollarını kapsar.
RTC ve NVRAM sınırları mock edilir; üretim JSON parser ve HTTP handler
gerçektir. Kısmi POST testinde beklenmeyen RTC çağrısı mock hatasıdır.

`python test/integration/web_device/test_roundtrip_helpers.py`: 10/10
cihazsız regresyon testi geçti. Kayıp mutasyon cevabı hem başlangıçta açık
hem kapalı IEC hattında enjekte edildi; iki durumda da eski IOA ve etkinlik
durumu geri geldi. Yazmayı yok sayan sınır örneğinde SBOTimeout testi
artık başarısızdır. Eski acceptance helper paketi de 5/5 geçti. Testlerin
hata enjekte ettiği alt senaryolarda konsola FAIL yazması beklenir;
Python unittest sonucu bu beklenen davranışı değerlendirir.

Gerçek HTTP JSON fixture ve web auth host paketleri geçti. ARM Release
derlemesi ve imzalı EFW self-check geçti. Tam Ceedling sonucu **1.685/1.685**, 0 hata ve 0 atlanan testtir.
Ölçüm ve kaynak hashleri
[ölçüm kaydındadır](2026-10-23-json-device-fixes-metrics.json). Yerel ayrıntılı loglar:
`test/integration/json/build/device_fixes_unit.log` ve
`device_fixes_release.log`.

Son Release: text 362.864 B, data 516 B, BSS 126.096 B, ham uygulama
363.384 B. Önceki 18:57 paketine göre Flash +304 B; statik RAM farkı
sıfırdır. Paket
`Release/troika-smart-breaker-modem_v1.0.1_20261023_192746_ff219be7.efw`
219.344 B, ham CRC `D9E098EA`; self-check ham bayt eşitliği ve ECDSA
imzasını doğruladı. Değiştirilen kaynakların Release derlemesinde warning
ve error görülmedi. Bu, tüm eski modüllerin temiz derlemesi iddiası değildir.

## 5. Kurallar ve kalan sınırlar

Cihaz testi yalnız test düzeneğinde çalıştırılmalıdır. RF apply seçeneği
gerçek RF işlemi yetkisiyle açılmalıdır. Bağlantı tamamen kaybolursa yazılım
geri yüklemeyi garanti edemez; başarısızlığı görünür kılmalıdır. Tampon
sınırı, kullanım dışı IEC IOA semantiği ve mevcut modem dinleyici yaşam
döngüsü bu düzeltmede değiştirilmedi. Beş timeout/commissioning alanının
reboot sonrası fiziksel kalıcılığı ve RTC donanım hata yolu bu sürümle
cihaz üzerinde yeniden doğrulanmalıdır. Önceki 79 PASS / 5 FAIL koşusu bu
paketin fiziksel kabulü değildir.

## 6. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-23 | 1.0 | Eksik ayar aktarımı, RTC yolu, güvenilir geri yükleme ve host testleri |
