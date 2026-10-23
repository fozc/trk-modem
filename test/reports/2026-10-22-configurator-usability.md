# Configurator ve web günlük kullanım düzeni

**Tarih:** 22.10.2026
**Sürüm:** 1.0

## Amaç

Cihaz bilgileri, durum paneli, sistem/arıza günlükleri ve servis konsolunun
kullanıcı talebine göre düzenlenmesini ve test kapsamını kaydeder.

## Kullanım yeri

Masaüstü kaynakları `../troika-configurator` deposundadır. Web kaynakları
bu depoda `web-page/` altındadır. HTTP API ve firmware kayıt modeli
değiştirilmedi; mevcut `/syslogs?offset=...&limit=...` kullanıldı.

## Bulgular ve uygulama

- Salt okunur cihaz metinleri dar QLineEdit kutularında kesiliyordu.
  Satır kaydıran, seçilip kopyalanabilen QLabel alanlarına dönüştürüldü.
  Cihaz yönetimi sekmeleri tek kaydırılabilir sayfada bilgi, mobil ağ,
  erişim, yeniden başlatma ve zaman bölümlerine dönüştürüldü.
- Checkbox seçimi tema içinde belirgin değildi. Seçiliyken renkli zemin
  ve beyaz tik eklendi; ayar metni Etkin/Kapalı olarak güncellenir.
- Durum paneline akü gerilimi, SOC ve mobil şebeke özetleri eklendi.
  Ayrıntılar aynı ölçüm geçerliliği denetimlerini kullanır. Uzun durum
  metinleri daha geniş satırlara alındı; okunmadan değer gösterilmez.
- Sistem günlüklerinde tek sayfa/sonraki sayfa yerine Toplamı oku,
  Aralığı oku ve Tümünü oku eklendi. Toplam, okunan/hedef ve aralık
  görünürdür. Numara 1 en yeni kaydı gösterir. Aralıklar kullanıcıya
  1 tabanlı, mevcut API'ye 0 tabanlı gönderilir.
- Arıza kayıtları fider, L1/L2/L3 ve geçici/kalıcı sekmelerine ayrıldı.
  Her fiderin okunmuş verisi ayrıdır; okunmamış fider eski veriyi göstermez.
- Konsola koyu terminal görünümü ve en çok 100 komutluk oturum geçmişi
  eklendi. Yukarı/aşağı okları geçmişte dolaşır; en sona dönünce taslak
  geri gelir. Disconnect'te geçmiş ve çıktı temizlenir; diske yazılmaz.

Web günlük arayüzü aynı üç okuma işlemini kullanır. Filtreler yapılandırma
değişikliği sayılmaz. Türkçe/İngilizce etiketler ve okuma sırasında sayaç
güncellemesi vardır. Web sürümü **1.4.16** olarak güncellendi.

## Günlük okuma kuralları ve sınırlar

Toplam `limit=0` ile alınır. Veri istekleri en fazla 100 kayıt ister;
cihazın cevap tamponu daha az kayıt verebildiği için sonraki offset
istenen limit kadar değil, gerçekten dönen satır sayısı kadar ilerler.
Seçilen aralık bitince istekler durur. Eksik boş sayfa, hatalı yanıt veya
değişen toplam okumayı durdurur; alınan kayıtlar ve ilerleme korunur.

Mevcut API snapshot/cursor sağlamaz. Yeni olaylar aynı toplam korunurken
de ring içeriğini kaydırabilir; bu çalışma sabit snapshot garantisi
vermez. Her iki arayüzde bu sınıra ilişkin açıklama vardır. Arayüzden
toplamın okunması kayıtların tamamının okunduğu anlamına gelmez.

## Doğrulama

| Kanıt | Sonuç |
|---|---|
| Configurator tam pytest | **46 geçti**, son koşu 32.81 s |
| GUI smoke | Giriş, ayar/durum/günlük, konsol, RF ve disconnect geçti |
| Yeni GUI/HTTP testleri | 237 kayıt, 17 kayıtlık kısa sayfa, aralık 11–45, tümünü okuma, eksik sayfada durma, hatalı aralıkta istek göndermeme, checkbox metni, uzun bilgi alanları, arıza veri ayrımı ve komut taslağı/geçmişi |
| Tam web navigation | Geçti; kaynak/gömülü audit **42 kontrol**, profile **40 kontrol** |
| Yerel gerçek tarayıcı | Son kaynakla toplam 237/okunan 0; aralık 11–45/okunan 35; tümünü okuma 237/237 gözlendi |
| Görsel kontrol | Üretim Qt widget'larıyla cihaz bilgileri, durum paneli, arıza sekmeleri ve terminal render'ları incelendi |
| Web üretimi | Kaynak ve gzip gömülü C kaynakları üretildi; **bu turda ARM firmware derlenmedi veya yüklenmedi** |

Tarayıcı ve Qt görselleri sentetik veridir. Fiziksel cihaz, RF uygulama,
röle, reset veya Flash testi yapılmadı. Önceki açık donanım bulguları
bu değişiklikle kapanmış sayılmaz. İlk uygulama sırasında commit yoktu;
23.10.2026 kullanıcı talimatıyla web değişiklikleri `085d591` ile
kaydedildi. Push yapılmadı.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 22.10.2026 | 1.0 | Tek cihaz sayfası, günlük okuma akışı, arıza sekmeleri ve terminal geçmişi |
