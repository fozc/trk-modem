---
name: embedded-verification
description: Firmware değişikliklerini gereksinim, hata senaryosu, host testi, statik analiz ve hedef ölçümleriyle doğrular. Kod incelemesi ve teslim değerlendirmesinde kullanılır.
---

# Gömülü Yazılım Doğrulama

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## V00. Amaç ve girdiler

İstenen davranışı kanıtlamak. İlgili kaynak/değişiklik, kabul ölçütü,
proje hardware/architecture dosyaları ve mevcut test düzeni okunur.
Bu pakette proje referansları SKILL.md konumundan iki üst dizindedir;
taşınmış skill için kullanıcının çalışma projesi esas alınır.

## V01. Test seçimi

[ZORUNLU] Değişen davranış ile onu sınayan test ilişkilendirilmelidir.
Beklenen sonuç uygulamanın aynı algoritması kopyalanarak üretilmemelidir.
Test gerçek üretim girişini ve gözlenebilir sonucu sınamalıdır;
mock/fake (taklit) donanım ve taşıma sınırında kullanılmalıdır.
Hata sonrası önceki geçerli durumun ve ilgili yan etkilerin korunması
uygun kapsamda kontrol edilmelidir. Regresyon testleri geçici betikte
bırakılmamalı; projenin mevcut test altyapısına eklenmelidir.
İstenen commit kapsamına bu testler de alınmalıdır; bu kural kendi
başına commit yetkisi vermez. Proje test girişleri A11'den alınmalıdır.
Düşük etkili biçim/belge değişikliklerine gereksiz firmware testi
eklenmemelidir.

| Risk | İlgili doğrulama |
|---|---|
| Parser/kopyalama | Boş, kısa, sınır, fazla uzun, bozuk ve parçalı veri |
| Durum/timeout | Tick sarması, iptal, tekrar olay, sınırlı retry |
| Pool/kuyruk | Dolu/boş, tükenme, tekrar bırakma ve backpressure |
| ISR paylaşımı | Kaybolan olay, sahiplik iadesi ve iç içe kesmeler |
| DMA | Başlatma/abort/tamamlanma yarışı ve buffer yeniden kullanımı |
| Kalıcı veri | Her yazma aşamasında güç kesintisi, bozuk kopya, migration |
| Firmware update | Eksik/tekrar/sırasız parça, sınır ve kurtarma |
| Emniyet işlevi | Tespit+tepki süresi, bayat veri, reset ve güvenli durum |

## V02. Kanıt katmanları

[ZORUNLU] Saf mantık host üzerinde; IRQ, DMA, Flash ve zamanlama hedef
üzerinde doğrulanmalıdır. Mock davranışı donanım garantisi sayılmamalıdır.
Host sanitizer desteği yoksa açık kaydedilmelidir.

[ZORUNLU] Build/analiz komutları proje A11'den alınmalıdır.
Vendor/üretilmiş kod uyarıları ile değiştirilen uygulama uyarıları
ayrılmalı; bastırmalar dar kapsamlı ve gerekçeli olmalıdır.
MISRA/BARR uygunluğu seçilmiş kurallar, sapmalar ve kanıtla
sınırlandırılmalıdır.

## V03. Kaynak ve concurrency

[ZORUNLU] ISR paylaşımı incelemesinde
[atomic-isr.md S01/S03](../embedded-design/references/atomic-isr.md)
seçim koşulları kontrol edilmelidir. Gerçek erişim yollarının seçilen
korumayla dışlandığı gösterilmelidir. Atomic ve IRQ koruması birlikte
varsa ayrı ihtiyaçları aranmalıdır. IRQ maskelemede önceki kapalı
durumun korunması ve yeniden açılınca gelen byte/olayın silinmemesi
uygun regresyonla sınanmalıdır. Host testiyle doğrulanan işlem sırası,
hedefte ölçülmesi gereken kesme gecikmesinden ayrılmalıdır.

[ZORUNLU] Kritik atomikler için gerçek compiler/ABI/optimizasyonla
assembly incelenmelidir. Runtime çağrısı, hizalama ve retry döngüsü
kontrol edilmelidir. Lock-free olmak sabit süre garantisi sayılmamalıdır.

[ZORUNLU] Stack, RAM/Flash, kuyruk doluluğu ve deadline (son süre)
proje bütçesine göre değerlendirilmelidir. Ölçülen en yüksek süre
kanıtlanmış WCET olarak adlandırılmamalıdır. Compiler/LTO değişiminde
etkilenen hedef kanıtları yeniden değerlendirilmelidir.

## V04. İnceleme çıktısı

[ZORUNLU] Her bulgu için konum, tetikleyici, gözlenebilir sonuç,
önem ve düzeltme önerisi verilmelidir. Doğrulanmamış olasılık gerçek
hata gibi sunulmamalıdır. Stil tercihi davranış hatasından ayrılmalıdır.

## V05. Teslim kapısı

[ZORUNLU] İlgili build ve testler geçmeli; kritik açıklar kapanmalı
veya etkilediği teslim kapsamı açıkça sınırlandırılmalıdır. Geçemeyen
kapı kullanıcıya bildirilmelidir. Bir kontrolün çalıştırılmaması geçiş
olarak kaydedilmemelidir.

Kayıt: tarih, kaynak sürümü/çalışma ağacı, compiler sürümü ve bayraklar,
komut, sonuç, çıktı yolu, donanım revizyonu, kalan sınırlar.
Düzeltmede uygun regresyon aranır; gerekli kontroller geçtikten sonra
ilgisiz testler genişletilmez.

## V06. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Gereksinimden teslim kanıtına doğrulama |
| 2026-10-04 | 1.1.0 | V01: gerçek davranış ve repoda regresyon; V03: koruma seçimi ve IRQ yeniden açılma yarışı |
