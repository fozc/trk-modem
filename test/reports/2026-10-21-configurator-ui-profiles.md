# Configurator tasarımı ve ortak ayar dosyaları

**Tarih:** 21.10.2026
**Sürüm:** 1.0

## Amaç

Masaüstü programın yeni ekran düzenini, web ile ortak ayar dosyalarını
ve doğrulama kapsamını kaydeder.

## Kullanım yeri

Program kaynakları `../troika-configurator` deposundadır. Web kaynakları
bu depoda `web-page/` altında bulunur. Masaüstü program cihazın mevcut
HTTP/JSON API'sini kullanır; web sayfasını indirmesi gerekmez.

## Bulgular ve değişiklikler

Önceki ekranda ham JSON ve uzun dikey formlar vardı. Ana pencere sabit
sol menü, ayrı bağlantı penceresi, görev sekmeleri ve sabit işlem çubuğu
ile yeniden düzenlendi. Cihaz ayarları bağlantı, çalışma/zaman ve cihaz
bilgileri olarak ayrıldı. RF ekranında fider seçimi, tanımlama, koruma,
gelişmiş alanlar, faz ölçümleri, keşif ve uygulama sonucu ayrı gösterilir.
F5 açık bölümün okumasını başlatır; Ctrl+K bağlantı penceresini açar.

Okumadan değerler gösterilmez. İstek sürerken kontroller kilitlenir ve
bekleme gösterilir. Başarısız okuma veya eski oturum verisi yazılabilir
durum gibi gösterilmez. Ölçüm geçerliliği ve sentinel değerleri kullanıcı
metnine dönüştürülür; JSON kullanıcı ekranına basılmaz.

Program ve web bölüm bazlı `troika-config` v1 dosyasını kullanır.
İçe aktarma yalnızca formu doldurur; cihaza yazmak ayrı işlemdir.
Doğrulama hatasında mevcut form korunur. Dosya salt okunur cihaz bilgisini
taşımaz; APN parolalarını içerir. Bu sürüm NVRAM şeması değildir.

Web'de gelişmiş RF alanları, SCADA IP ve devreye alma zamanı tamamlandı.
Devreye alma alanının düzenlenebilir ve salt okunur görünümlerindeki aynı
HTML kimliği ayrıldı. Reset aralığında 3700 saniye gibi değerlerin saat
dönüşümünde kesilmesi giderildi. APN/NTP uzunlukları ve timeout sınırları
mevcut firmware parser sınırlarıyla eşleştirildi.

## Doğrulama

| Kanıt | Sonuç ve sınır |
|---|---|
| Configurator pytest | 34 geçti; HTTP, hata/rol/oturum, dosya doğrulama, GUI ve eski protokol testleri |
| Güncel GUI smoke | Giriş, bölüm okumaları, tablolar ve disconnect geçti |
| Kaynak/gömülü web dosya testleri | 40 kontrol geçti; yanlış dosya, rol, okumadan işlem, POST yapılmaması ve asıl kaydın korunması |
| Tam web navigation | Geçti; endüstriyel audit 36 kontrol, 0 hata |
| ARM Release | Web 1.4.15 derlendi; imzalı paket self-check geçti |
| Gerçek cihaz, yeni GUI yolu | Dört ayar bölümü form/dosya dönüşümü, durum ve ayrıntılı RF okuması: 6 kontrol geçti; yalnız okuma |
| Yerel gerçek tarayıcı | Sentetik sunucuda okumadan boş görünüm, okuma sonrası düğmeler ve dışa aktarma bildirimi gözlendi |
| Görsel kontrol | Üretim Qt widget'larıyla beş ekran render edildi; örnek veri kullanıldı |

İçe aktarmanın tarayıcı dosya seçicisi uçtan uca denenmedi; üretim
doğrulama/form doldurma yolu kaynak ve gömülü JavaScript testlerinde
çalıştırıldı. Masaüstü dosya/form yolu GUI testlerinde doğrulandı.

## Kalan sınırlar

Yeni web imajı cihaza yüklenmedi. Bu çalışma gerçek EUI silme, RF yazma,
röle sürme, reset veya fiziksel kontak testi içermez. Önceki Modbus T5
bulgusu bu değişiklikle kapatılmış sayılmaz. Programın yazılım güncelleme
sekmesi yer tutucudur; tam güncelleme akışı henüz bağlanmadı.

Configurator gereksinimi PySide6 6.7+ olarak güncellendi; kurulu 6.11.2
ile çalıştırıldı. UTC tarih editörünün kullandığı `setTimeZone` API'si
[Qt belgesinde](https://doc.qt.io/qt-6/qdatetimeedit.html#timeZone-prop)
6.7 itibarıyla tanımlıdır.

İlk uygulama sırasında commit/push yoktu. 23.10.2026 kullanıcı talimatıyla
web değişiklikleri `085d591`, küçük HTTP kuyruğu `22e6cf8` ile kaydedildi.
Push yapılmadı. Ayrıntılı program raporu ilgili
depodaki `COMPATIBILITY_REPORT.md` dosyasındadır.

## 22.10.2026 validator ve eksik ekranlar

IP validator'ın `(state, pos)` dönüşü `(state, text, pos)` olarak düzeltildi.
QLineEdit klavye girişi Qt uyarı yakalayıcısıyla test edildi; geçersiz IP
kaydetmeden önce reddedilir. Ortak formun yanlış `role` erişimi giderildi.

Modbus ekranı önceden `RegisterMap` verisini göstermiyordu. Artık Genel
Ayarlar / Register adresleri sekmeleri vardır. Fider, faz, canlı/geçici/
kalıcı liste ve kayıt seçimleri çalışan firmware metadata'sına uygulanır;
register/PDU aralıkları, tür, birim ve erişim gösterilir. Metadata yoksa
adres uydurulmaz. POST yalnız yazılabilir alanları içerir.

IEC104 Genel Ayarlar / Fiderler sekmelerine ayrıldı. Günlükler Sistem
Günlükleri / Arıza Kayıtları sekmelerinde ayrı tutulur; disconnect'te
temizlenir. Menü ve temel alan adları web sözlüğüyle eşleştirildi.
Kontrol genişlikleri ve menü boşlukları küçültüldü. Üretim widget'ları
örnek veriyle 1200×820 ve 960×680 boyutlarında görsel olarak incelendi.

**39 pytest ve GUI smoke geçti.** Yeni regresyonlar validator uyarısını,
register/PDU hesabını, seçicileri, metadata'nın yazılmamasını, eksik
harita durumunu, günlük ayrımını, admin düğmelerini ve fiderler arası
adreslerin korunmasını sınar. Test haritası firmware'in gerçek metadata
üreticisi çıktısıdır; uygulamada sabit harita fallback'i yoktur. Bu koşu
host/test sunucusundadır; yeni fiziksel cihaz doğrulaması yapılmadı.

## 22.10.2026 kompakt sayısal alanlar

Platformun varsayılan ok alanı host testinde 14×12 piksel ölçüldü;
ok merkezine tıklama tam sayıyı değiştiriyordu. Kullanıcının bütün
başarısız tıklamaları yeniden üretilmiş sayılmaz. Temaya görünür SVG
okları ve 22×14 piksel up/down alanları eklendi. Mevcut Qt kontrolleri
korundu; ayrı değer durumu veya yeni widget sınıfı eklenmedi.

Varsayılan 1.0 ondalık adımı IsSafety 0.1..0.3 aralığında ara değerleri
atlıyordu. Adım IsSafety için 0.01, genişliği en fazla 10 olan diğer
ondalık aralıklar için 0.1, kalanlar için 1.0 oldu. Min/max korundu.
Sayısal alan genişliği en fazla 140 piksel, yüksekliği yaklaşık 28 piksel
oldu; bölüm boşlukları küçültüldü ve grid satırları hizalandı.

**41 pytest geçti.** Üretim widget'larında gerçek QTest fare olayları,
klavye yukarı tuşu, sınırlarda davranış, 0.10→0.11→0.10 ve 1.0→1.1
geçişleri doğrulandı. Ok tıklama alanı ve kontrol boyutu sınandı.
Yeni örnek veri render'ları incelendi. Fiziksel cihaz testi yapılmadı.

## Değişiklik geçmişi

### 22.10.2026 boşta periyodik ekran kilidi

Configurator ana penceresinin 10 saniyelik bağlantı timer'ı, `ping()`
üzerinden `/status/board` GET isteği yapıyordu. Üretim HTTP istemcisinin
`busy_changed` sinyali bu arka plan okumasında da çalışma alanını
kilitliyordu. Zorunlu timer kaldırıldı; bağlantı/oturum hataları gerçek
işlem yanıtından anlaşılır. Kullanıcı okuma/yazma beklemesi ve isteğe
bağlı durum yenilemesi korundu.

35 pytest geçti. Yeni GUI regresyonu gerçek Qt olay döngüsünü 10.5 saniye
çalıştırarak boşta ek HTTP isteği oluşmadığını, gezinmenin açık kaldığını
ve elle okumanın cevap gelene kadar kilitlediğini doğruladı. Bu düzeltme
firmware değişikliği veya cihaz testi içermez.

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 21.10.2026 | 1.0 | Yeni masaüstü tasarımı, ortak dosyalar ve doğrulama kaydı |
