---
name: embedded-design
description: Mikrodenetleyici C/C++ firmware tasarımı ve uygulamasında API, bellek, zaman, ISR/DMA sahipliği ve hata davranışını belirler. Kod geliştirme ve tasarım değişikliklerinde kullanılır.
---

# Gömülü Tasarım ve Güvenilir Kod

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## D00. Amaç ve kullanım yeri

Üretim firmware'inde davranışı ve kaynak kullanımı sınırlı uygulama
geliştirme. Proje kökündeki AGENTS, hardware ve architecture okunur.
Bu paket içinde proje kökü SKILL.md konumundan iki üst dizindir.
Başka konuma kurulumda kullanıcının çalışma projesi esas alınır.

## D01. Sözleşme ve değişiklik

[ZORUNLU] Düzeltmeden önce bildirilen koşulun mevcut kaynakta
bulunduğu ve gerçek çağrı yolunda ulaşılabildiği kanıtlanmalıdır.
Tetikleyici ve gözlenebilir sonuç gösterilmeli; kapalı seçenek ile
etkin ürün davranışı ayrılmalıdır. Kullanılan HAL/driver/framework
sürümünde mevcut kontrol, dönüş değeri ve recovery (toparlanma)
yolları incelenmelidir. Olasılık veya donanım davranışı uydurulmamalıdır.

[ZORUNLU] Etkilenen gereksinim, çağrı bağlamı, girdiler, çıktıların
geçerliliği, hata sonucu, sahiplik ve iptal davranışı belirlenmelidir.
Mevcut API'nin hata sözleşmesi korunmalıdır; yeni global hata enum'u
kendiliğinden oluşturulmamalıdır.

[ZORUNLU] Katman yönü ve donanım erişim sınırı proje A03'ten alınmalıdır.
Yeni soyutlama, task veya bağımlılık ancak somut ihtiyaca bağlanmalıdır.

[ZORUNLU] Kanıt ve en küçük yeterli çözüm kodlamadan önce sunulmalıdır.
Mevcut altyapı ve hata/retry/reset yolları kullanılmalıdır. Yeni katman,
fallback (yedek yöntem) veya arka plan işi somut bir ihtiyaca
bağlanmalıdır. Kullanıcının önceki yetkisi, koruma ve erteleme kararları
proje A09/G03'e göre korunmalıdır.

## D02. Bellek ve kapasite

[ZORUNLU] G02 sınırları altında stack, statik alan veya sabit pool
seçilmelidir. Pool kapasitesi en yüksek eşzamanlı canlı nesne sayısı ve
payıyla hesaplanmalıdır. Blok boyutu/hizalaması, sahibi, bırakma zamanı,
tükenme sonucu, çift bırakma ve geçersiz blok kontrolü tanımlanmalıdır.
Pool tükenince büyüme veya heap'e dönüş yapılmamalıdır.

[ZORUNLU] Placement new yalnızca önceden ayrılmış, yeterli boyut ve
hizalamaya sahip alanda kullanılmalıdır. Nesne ömrü, destructor çağrısı,
yeniden kullanım ve sahiplik belirlenmelidir. Sözdiziminin kendisi
genel amaçlı heap izni sayılmamalıdır.

[ZORUNLU] Buffer (veri tamponu) kapasitesi ve geçerli veri uzunluğu
ayrılmalıdır. Aritmetik, pointer oluşturmadan ve kopyalamadan önce
kontrol edilmelidir. Stack bütçesi çağrı zinciri ve interrupt nesting
(kesmelerin iç içe girmesi) ile değerlendirilmelidir.

Örnek: offset + length taşmasına güvenmek yerine önce offset <= capacity,
ardından length <= capacity - offset kontrol edilir.

## D03. Durum ve zaman

[ZORUNLU] Başlatma, normal çalışma, timeout (zaman aşımı), iptal, hata
ve toparlanma geçişlerinde kaynak sahibinin kim olduğu yazılmalıdır.
Retry sayısı kadar toplam geçen süre de sınırlandırılmalıdır.

[ZORUNLU] Tick sarması, saat çözünürlüğü, en uzun ölçülebilir aralık ve
karşılaştırma yöntemi birlikte tanımlanmalıdır. Unsigned çıkarma tek
başına sınırsız süre ölçümünün kanıtı sayılmamalıdır.

[ZORUNLU] Cooperative kernel'de uzun iş parçalanmalı; her adımda
scheduler'a dönüş sınırı belirlenmelidir. Protothread yield/wait
üzerinden otomatik yerel değişkenlerin değer taşıdığı varsayılmamalıdır.
Kalıcı process durumu açık saklanmalı; static durumun reentrant
(yeniden girişli) olmadığı hesaba katılmalıdır.

## D04. Eşzamanlılık ve çevrebirimler

[ZORUNLU] ISR/task paylaşımında
[atomic-isr.md](references/atomic-isr.md) okunmalıdır.
S01'deki gerçek erişim ve sahiplik kontrolü tamamlanmadan atomic
işlem veya IRQ maskeleme seçilmemelidir. En basit yeterli koruma
kanıtıyla belirtilmeli; S03'teki dar kesme kapsamı uygulanmalıdır.
C++ paylaşımı için ayrıca [cpp-subset.md](references/cpp-subset.md)
C++ atomik sözleşmesi uygulanmalıdır.

[ZORUNLU] DMA başlatmadan önce buffer erişilebilirliği, hizalama,
sahiplik ve hedefe özgü cache bakımı belirlenmelidir. Timeout sonrasında
DMA'nın erişiminin bittiği doğrulanmadan buffer bırakılmamalıdır.
Abort/tamamlanma yarışı, hata bayrakları ve yeniden başlatma sırası
sürücü sözleşmesinde tanımlanmalıdır.

## D05. Hata ve emniyet

[ZORUNLU] Dış veri uzunluk, değer aralığı, enum/komut, CRC, güncellik ve
yetki açısından ilgili sınırda doğrulanmalıdır. Assert dış girdi
doğrulamasının yerine kullanılmamalıdır.

[ZORUNLU] Güvenli durum, watchdog besleme koşulu, reset döngüsü ve
manuel toparlanma proje A02/A07'den alınmalıdır. Her şeyi kapatmak
veya reset atmak genel çözüm olarak seçilmemelidir.

[ZORUNLU] Log boyutu, truncation (kısaltma), taşma, çağrı bağlamı ve
gizli veri politikası tanımlanmalıdır. Dış veri format string
yapılmamalıdır. Projeye özgü log kanalları A03'te yer alır.

## D06. Kalıcı veri ve dış protokol

[ZORUNLU] Yeni wire biçimi açık encode/decode ile tanımlanmalıdır.
Ham struct/bit-field yerleşimi endian veya hizalama sözleşmesi yerine
geçmemelidir. Mevcut kalıcı ABI kendiliğinden dönüştürülmemelidir.

[ZORUNLU] Kalıcı yazmada erase/program sınırları, güç kesintisinin her
adımı, commit (geçerli kayıt işareti), CRC kapsamı, semantik doğrulama,
sürüm uyumluluğu ve boot seçimi değerlendirilmelidir. Sequence sarması,
eşit sequence ve iki bozuk kopya için politika belirlenmelidir.

[ZORUNLU] Firmware güncellemede parça doğrulama, boyut/adres sınırı,
yarım aktarım, tekrar gelen parça, doğrulama sonrası etkinleştirme,
yetkilendirme ve kurtarma proje gereksinimine bağlanmalıdır.
CRC kimlik doğrulama yerine kullanılmamalıdır.

## D07. Kod ve doğrulama

[ZORUNLU] C için [c-rules.md](references/c-rules.md), C++ için
[cpp-subset.md](references/cpp-subset.md) uygulanmalıdır.
Değişiklik, gerçek modül test komutuyla sınanmalıdır.
Donanım etkisi host testiyle kapanmış gösterilmemelidir.

Çıktı: etkilenen sözleşme, uygulama, test kanıtı, kalan açıklar.

## D08. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Tasarım, sahiplik ve sınırlı kaynak kuralları |
| 2026-10-04 | 1.1.0 | D01: kanıt, mevcut toparlanma ve basitlik; D04: atomic seçiminden önce erişim ve sahiplik kontrolü |
