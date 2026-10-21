# Röle son durumlarının korunması — 21.10.2026

## Amaç

Röle ON/OFF komutları yeniden başlatmada geri yüklenmelidir. Web cihaz
ayarındaki “Yeniden başlatmada son çıkış durumlarını koru” seçeneği bu
davranışı yönetmelidir. Kapalıyken iki röle OFF başlamalıdır. Tercihin
değiştirilmesi o anda çalışan çıkışları değiştirmemelidir.

## Kullanım yeri ve uygulama

Mevcut iki röle, shell ve Modbus girişlerinden aynı `relay_set()` yolunu
kullanır. Durum değişikliği mevcut çift kopyalı NVRAM kaydı üzerinden
saklanır; ayrı kayıt alanı, zamanlayıcı veya kuyruk eklenmemiştir.
Tekrar eden aynı komut Flash yazmamalıdır. Kayıt başarısızsa komut
uygulanmamalıdır; Modbus FC06 exception 0x04 dönmelidir. Yalnız birinci
kopya doğrulanmışsa yeni durum kalıcıdır ve mevcut yedek onarımı kullanılır.

Kullanıcı talimatıyla schema **1** kalmıştır; migration yoktur.
NVRAM 1984 → 1988 bayt olmuştur. Eski uzunluk varsayılanlarla açılır;
bu, yaşam süresi gibi salt okunur alanları da kapsar. Test kartının dört
ayar grubundaki yazılabilir alanlar yükleme öncesi yedekten geri alınmıştır.
Eski NVRAM içeriğinin tamamının korunduğu iddia edilmemelidir.

Varsayılan tercih açık, başlangıç komutları OFF'tur. Kapalı tercihle
açılışta RAM komutu temizlenir; sonradan etkinleştirme eski ON kaydını
canlandırmamalıdır. Ayrıntılar `doc/ROLE_CIKIS_KALICILIGI.md` içindedir.

## Önceki çalışmaların commit'leri

- `03d1f73`: EUI silme ve seri hat sınırı testleri.
- `cb041d4`: web oturum, doğrulama ve operatör görünümü düzeltmeleri.
- `74cf52c`: iki röle kabulü ve web cihaz testi raporu.

Yeni kalıcılık geliştirmesi bu üç commit'ten sonra yapılmıştır.

## Doğrulama

| Kanıt | Sonuç ve kapsam |
|---|---|
| Tam Ceedling | 1581 PASS, 0 FAIL, 0 IGNORED |
| Gerçek NVRAM çekirdeği / host Flash sınırı | 559 PASS, 0 FAIL; dört maske, tekrar yazmama, A/B hata yolları, eski uzunluk |
| Web navigation | Paket geçti; kaynak ve embedded audit 34/34 |
| Web auth | Paket geçti |
| ARM Release ve imza self-check | Geçti; layout assert'leri hedef toolchain'de doğrulandı |
| Cihaz kalıcılık senaryosu | 25 kontrol geçti; dört ON/OFF birleşimi ve tercih aç/kapat sonrası reset |
| Gerçek tarayıcı | Kayıtlı iki ON varken tercih kapatılıp kaydedildi; reset sonrası OFF/OFF ve tercih false okundu; tekrar açılıp kaydedildi; sonraki reset sonrası OFF/OFF ve tercih true doğrulandı |
| Tam gerçek RS485 Modbus | **28 PASS / 2 FAIL / 4 NOT**; aşağıdaki açık bulgu |

İmzalı EFW XMODEM ile kurulmuştur: web **1.4.14**, firmware 1.0.1,
358520 bayt, CRC **BF9490B4**, derleme 21.10.2026 17:30:27,
taban commit 74cf52c0. Çalışan imaj yeni commit edilmemiş geliştirmeyi
içerir; taban commit kimliği tek başına kaynak sürümünü tanımlamaz.
Son reset konsolunda firmware approved YES görülmüştür.

### Modbus yanıt süresi ve açık aralıklı gönderim bulgusu

Kalıcılık açıkken durum değiştiren FC06 yanıtı test düzeneğinde yaklaşık
221–241 ms ölçülmüştür. Eski 200 ms test penceresi komut uygulanmasına
rağmen yanıtı kaçırmıştır. Test alıcısı varsayılan 1 s pencereye alınmış;
bu değer cihazın garanti edilen üst sınırı sayılmamalıdır. Master timeout'u
Flash kayıt süresini kapsamalıdır. Aynı komut tekrarları yaklaşık 17–28 ms
ölçülmüştür. USB dönüştürücü katkısı bu sürelerin içindedir.

T5'in hedef 1,5 ms aralıklı pozitif gönderiminde echo ve readback
başarısızdır. Test, echo tamamlanmadan sonraki isteği göndermeyecek biçimde
düzeltilmesine rağmen iki başarısız sonuç kalmıştır. Host write çağrıları
arasında 1,769–2,133 ms ölçülmüştür; tel üzerindeki gerçek bayt aralıkları
ölçülmemiştir. Firmware'deki UART sessizlik/timer yolu bu geliştirmede
değişmemiştir. Bu sonuç kalıcılık hatası veya kesin UART kusuru olarak
sınıflandırılmamalı; hat analizörüyle zamanlama doğrulamasına açık kalmalıdır.
Tam Modbus paketi geçmiş sayılmamalıdır.

## Kanıt sınırları ve kalan işler

- Saklanan bilgi komuttur; fiziksel kontak geri bildirimi değildir.
- PWM açılışta OFF başlar, kayıtlı ON mevcut peak/hold ile uygulanır.
  Reset boyunca kesintisiz kontak tutulması kanıtlanmamıştır.
- Flash yazarken güç kesintisi ve Flash ömür testi yapılmamıştır.
  Sık ON/OFF işlemleri mevcut NVRAM Flash yazma sayısını artırır.
- Kart tanımlarında dört dijital giriş (DIN1–DIN4) ve iki röle çıkışı
  vardır. Kullanıcıyla ayrı dijital çıkış bulunmadığı netleştirilmiştir.
  Kalıcılık yalnız iki röle içindir; girişlerin durumu geri yüklenmez.

Yerel ham kanıtlar `.temp_files/relay-persistence-20261021/` altındadır.
`device-results.json`, `modbus-device.log`, `final-reset-console.log`,
`web-retention-enabled.png` ve `web-retention-disabled.png` kullanılır.
Ayar yedeği ve ham konsol kayıtları hassas bilgiler içerir; yayımlanmamalıdır.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 21.10.2026 | 1.0 | Kalıcılık, web tercihi, cihaz testi ve açık zamanlama bulgusu |
