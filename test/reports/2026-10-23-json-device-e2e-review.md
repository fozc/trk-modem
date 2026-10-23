# Cihaz JSON testinin kaynak incelemesi

**Sürüm:** 1.0  
**Tarih:** 2026-10-23

Güncelleme: Bu incelemedeki yazılım/test düzeltmeleri daha sonra
[uygulama raporunda](2026-10-23-json-device-fixes.md) ele alındı. Aşağıdaki
bulgular inceleme anındaki kodu anlatır.

## 1. Amaç ve kapsam

Diğer oturumun cihaz testi raporu, kalıcı logu ve test betiği güncel
üretim koduyla karşılaştırıldı. Bu incelemede cihaz bağlantısı veya ayar
yazımı yapılmadı. Önceki oturumun fiziksel ölçümleri yeniden çalıştırılmadı.

## 2. Doğrulanan bulgular

`Application/modem_config.c:31` içindeki toplu setter dört timeout ve
commissioning_time alanını aktarmıyor. JSON parser bunları okuyor; handler
bu setter üzerinden sync yapıp başarılı cevap veriyor. Beş alanın
uygulanmaması kaynakla doğrulanıyor; bu dosya JSON geçişinde değişmemiştir.

Ek olarak `Time` da parser tarafından kabul edilip `config.time` içine
yazılıyor; toplu setter bunu aktarmıyor. GET ise `rtc_get_epoch()` okuyor.
Dolayısıyla HTTP üzerinden saat ayarlama yolu da eksiktir. Bu bulgu
kaynak incelemesidir; cihaz saati değiştirilerek sınanmadı. Yalnızca
`modem_config_set_time()` çağırmak RTC'yi ayarladığını kanıtlamaz.

Kullanım dışı IEC hattında IOA alanlarının atlanması ve HTTP 4.608 B gövde
sınırı mevcut kodla uyumludur. IOA davranışının mevcut olması, bunun en
uygun API sözleşmesi olduğunu tek başına kanıtlamaz.

Diğer oturumun iki test beklentisi değişikliği mevcut handler davranışıyla
uyumludur. `ceedling test:test_http_iec_config_scenario` yeniden
çalıştırıldı: 13/13 geçti. Boş firmware gövdesi flash init çağrısı olmadan
400 döner; küçük Modbus tamponu ortak 500 yoluna gider.

## 3. Test betiğinde bulunan sorunlar

1. `run_roundtrip.py:596` içindeki IEC finally yalnız eski snapshot'ı
   gönderiyor. Başlangıçta kullanım dışı hat geçici açılıp IOA'ları
   değiştirildikten sonra hata oluşursa eski snapshot hattı kapatır.
   Üretim setter'ı kapalı hat için IOA yazmadığından adresler değişmiş
   kalır. Hata enjekte edilen cihazsız sınır simülasyonunda tekrarlandı:
   hat kapandı, örnek IOA 1200 yerine 6101 kaldı. Normal başarılı koşunun
   geri yüklenmiş olması bu hata yolunu kapsamaz.
2. `run_roundtrip.py:504-508`, SBOTimeout için gönderilen değeri
   karşılaştırmak yerine yalnızca 1..300 aralığını kontrol ediyor.
   Yazmayı yok sayan sınır simülasyonunda 30 istendi, 45 kaldı ve test PASS
   verdi. Alanın uygulanması böyle kanıtlanamaz.
3. RF'ye aynı gövdenin POST edilmesi yan etkisiz değildir.
   `http_handlers.c:1564` her başarılı adayı `rf_apply_save()` yoluna sokar.
   `rf_apply.c:94` batch'i RUNNING yapar ve etkin fiderleri hedefler.
   Uygun durumda inventory ve grup uygulaması başlatılabilir. Ayarların
   son GET'te eşit olması RF işlem durumunun değişmediğini kanıtlamaz.
4. `run_roundtrip.py:734` son durum kontrolündeki taşıma/JSON hatasını
   yalnız yazdırıyor; FAILURES'a eklemiyor. Önceki kontroller yeşilse
   geri yükleme doğrulanmadan başarılı exit code çıkabilir.
5. Aynı değerin escape edilmiş biçimini yazıp eski değeri okumak parser'ın
   alanı gerçekten uyguladığını kanıtlamaz; alanı atlayan uygulama da
   geçebilir. Geçersiz değer reddi de geçerli değerin saklandığını
   kanıtlamaz. RF monitor kontrolü yalnız LineId/Phases/Phase anahtarlarını
   sınar; bütün monitor alanlarını doğrulamaz. RF discovery GET bu betiğin
   şema listesinde yoktur. Bu nedenle kapsam "tüm alanlar" diye
   sunulmamalıdır.

## 4. Kanıt sınırları ve öneri

Kaydedilmiş e2e logunda 79 PASS / 5 FAIL bulunur. Ara anlatımlardaki
83/86 PASS sayıları bu son logun sonucu değildir. "PASS ... verbatim body
rejected" mesajı da sabit hata metninin başarıda yazılmasından kaynaklanır.
RegisterMap dışlanıp JSON yeniden kodlandığından bu adım tam GET gövdesinin
bayt bayt aynen POST edilmesi değildir.

Önceki oturumun raporladığı kurulu imaj 17:42, ham 363.104 B ve CRC
CC9D60CD'dir. Son modem entegrasyon paketi 18:57, ham 363.080 B ve CRC
F173B5D6'dır. Önceki cihaz sonucu son paketin fiziksel kabulü sayılamaz.
Kurulum kaydı bu incelemede cihazdan yeniden okunmadı.

Önce hata durumunda geri yükleme, tam eşitlik ve son kontrol hata kaydı
düzeltilmelidir. RF testinin apply yan etkisi açıkça kapsama alınmalıdır.
Ardından cihaz alanlarının gerçek yazma yolu düzeltilip hedef paketle
yeniden test edilmelidir. Beş alan için NVRAM alanları zaten vardır;
yalnız eksik aktarımı düzeltmek yeni schema gerektirmez. RTC saat ayarı
ayrıca gerçek saat altyapısıyla ele alınmalıdır.

## 5. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-23 | 1.0 | Kaynak incelemesi, 13 senaryo testi ve iki cihazsız hata tekrarının kaydı |
