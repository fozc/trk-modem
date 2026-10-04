# Teknik Belge Yazım Rehberi

Sürüm: 1.0.0 · Tarih: 2026-09-14.

## R00. Amaç ve kullanım yeri

Teknik kılavuz, şartname, protokol, inceleme ve karar kaydı yazımı.
Paketin belge rehberidir; firmware veri modelini belirlemez.

## R01. Dil kuralları

[ZORUNLU] Günlük Türkçe kullanılmalı; teknik anlam, birim ve
verification/validation gibi gerekli ayrımlar korunmalıdır.
İngilizce terim yerleşikse korunmalı; ilk kullanımda gerektiğinde
Türkçe açıklaması verilmelidir. Kod isimleri çevrilmemelidir.

[ZORUNLU] Kural cümleleri -malıdır/-melidir biçiminde yazılmalıdır.
Amaç, açıklama ve örnekler düz anlatım olabilir.
Zorunlu, öneri, isteğe bağlı ve yasak maddeleri ayrılmalıdır.
Kural ile örnek ayrı verilmelidir.

## R02. Yapı ve kaynak

[ZORUNLU] Belge amacı/kapsamı, sürümü/tarihi ve değişiklik kaydı
bulunmalıdır. Kararlı bölüm veya kural kimlikleri kullanılmalıdır.
Her kural tek yerde tutulmalı; diğer yerlerden referans verilmelidir.

[ZORUNLU] Kaynak koddan çıkarılan mevcut davranış ile onaylanmış
gereksinim ayrılmalıdır. Çelişkide kaynaklar ve etki yazılmalıdır;
firmware'in mevcut davranışı otomatik olarak doğru gereksinim
kabul edilmemelidir.

## R03. Protokol/API profili

İskelet: amaç/kapsam, roller ve terimler, ortak kurallar, komutlar,
akışlar, zamanlama, örnekler, değişiklik geçmişi.
Komut sayfası: amaç, kullanım yeri, istek, başarılı yanıt,
hata durumları ve örnek.

[ZORUNLU] Alan tablosu Ofset/Boy/Alan/Tip/Birim/Geçerli değer/Açıklama
içermelidir. Byte ve bit sırası, rezerv alan gönderme/alma davranışı,
uzunluk, doğrulama ve hata yanıtları açık olmalıdır.

[ZORUNLU] Alan tablosu wire sözleşmesinin ölçütü olmalıdır.
C struct yalnızca gerekirse mantıksal veri örneği olarak verilmeli;
packed veya doğrudan pointer cast zorunlu tutulmamalıdır.
Encode/decode gereklilikleri D06/C03'te yer alır.
Boyut/ofset assert'leri endian, bit sırası veya aliasing kanıtı
sayılmamalıdır.

[ZORUNLU] CRC/çerçeveleme içeren örnek baytlar hesapla doğrulanmalı;
hesaplanmayan örnek taslak olarak işaretlenmelidir.
Yeni komut numarası veya protokol davranışı örnek uğruna uydurulmamalıdır.

## R04. Diğer profiller

| Tür | İçerik |
|---|---|
| Kılavuz | Amaç, ön koşul, adımlar, doğrulama, sık hata |
| İnceleme | Kapsam, kanıt, bulgu, etki, öneri, sınır |
| Karar kaydı | Bağlam, seçenekler, karar sahibi/tarih, sonuçlar |
| Kaynak notu | Belge adı/sürümü/sayfası, çıkarım, belirsizlik |

## R05. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Dil rehberi ve veri yerleşimi kararlarının ayrılması |
