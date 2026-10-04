---
name: embedded-debugging
description: Mikrodenetleyici arızalarında log, fault ve ölçüm kanıtlarından kontrollü deneylerle kök neden bulur. Reset, kilitlenme, veri kaybı ve zamanlama sorunlarının araştırılmasında kullanılır.
---

# Gömülü Hata Analizi ve Debugging

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## B00. Amaç ve kullanım yeri

Tekrarlanabilir belirtiyi kanıtla açıklama ve en dar düzeltmeyle giderme.
İlgili kod, proje hardware/architecture dosyaları, loglar ve firmware
kimliği okunur. Paket proje referansları iki üst dizindedir; taşınan
skill'de kullanıcının çalışma projesi esas alınır.

## B01. Kanıtı koruma

[ZORUNLU] Beklenen/gözlenen davranış, sıklık, tetikleyici, firmware,
kart, güç ve haberleşme koşulları kaydedilmelidir. Reset nedeni,
fault register'ları, stack ve loglar mümkünse müdahaleden önce
korunmalıdır. Gerçek sırlar rapora eklenmemelidir.

[ZORUNLU] Debugger halt, watchpoint ve ek logun zamanlama/çıkışlara
etkisi değerlendirilmelidir. Tehlikeli bench işlemleri mevcut yetki
ve proje emniyet koşulları dışında başlatılmamalıdır.

## B02. Hipotez ve deney

[ZORUNLU] Her hipotezin dayanağı ve onu yanlışlayacak deney yazılmalıdır.
Aynı deneyde mümkün olduğunca tek değişken değiştirilmelidir.
Donanım ayrıntısı tahmin edilmemelidir.

[ZORUNLU] Çözümden önce
[embedded-design D01](../embedded-design/SKILL.md) erişilebilirlik ve
mevcut HAL/driver toparlanması kontrolleri uygulanmalıdır. Raporlanmış
kusur, etkin çalışma yolundaki hata ve kullanıcı tarafından ertelenmiş
iş ayrı tutulmalıdır. Erteleme, kusurun giderildiği anlamına gelmemelidir.

| Hipotez | Kanıt | Ayırıcı deney | Beklenen sonuç | Gözlenen sonuç |
|---|---|---|---|---|
| İncelenen arızaya göre doldurulur | Log/ölçüm | Kontrollü işlem | Önceden belirlenir | Sonradan kaydedilir |

[YASAK] Sadece gecikme ekleyerek, timeout büyüterek veya reset
uygulayarak kök neden kapatılmış sayılmamalıdır.

## B03. Düzeltme ve sonuç

[ZORUNLU] Düzeltme ilgili nedeni hedeflemeli; mümkünse eski sürümde
hata, yeni sürümde geçiş gösterilmelidir. Regresyon ve komşu davranışlar
uygun kapsamda sınanmalıdır. ISR/DMA sorununda yalnızca host testine
dayanılarak hedef sorunu kapanmış sayılmamalıdır.

Çıktı: belirti, kanıt, elenen hipotezler, kök neden veya kalan belirsizlik,
düzeltme, doğrulama ve sonraki gerekli deney.

## B04. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Kanıt ve kontrollü deney akışı |
| 2026-10-04 | 1.1.0 | B02: gerçek çağrı yolu ve mevcut toparlanma kontrolü |
