---
name: embedded-project-intake
description: Mevcut veya yeni firmware projesinde hardware ve architecture referanslarını kod/belge incelemesi ve tek ana soruluk turlarla tamamlar. Eksik proje tanımı olduğunda kullanılır.
---

# Gömülü Proje Tanımlama

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## I00. Amaç ve girdiler

Donanım gerçekleri ile yazılım kararlarını kaynaklarıyla ayırmak.
Mevcut hardware/architecture içeriği, şema, üretici belgeleri, build
ayarları, linker, sürücüler ve testler başlangıç girdileridir.
Bu pakette referanslar iki üst dizindedir; başka konumda çalışma
projesinin referansları kullanılır.

## I01. Adımlar

[ZORUNLU] Mevcut bilgiler okunmalı; dolu dosyaların üzerine boş şablon
yazılmamalıdır. Koddan cevaplanabilen sorular önce araştırılmalıdır.

[ZORUNLU] Kullanıcının mevcut yetkisi ve önceki açık kararları
korunmalıdır. Aynı kararı yeniden sormadan önce oturum/proje kaydı
incelenmelidir. Kullanıcının sonraki açık düzeltmesi eski karardan
önceliklidir. Ertelenmiş iş tamamlandı veya kendiliğinden yeniden
açılmış sayılmamalıdır. Projeye özgü karar architecture içinde,
genel yöntem ilgili skill'de tutulmalıdır.

[ZORUNLU] Her turda tek ana soru sorulmalı; cevap beklenirken bağımsız
bilgi toplama sürdürülebilmelidir. Kritik olmayan seçimler gerekçeli
öneri olarak yazılmalı; karar alınmış gösterilmemelidir.

[ZORUNLU] Her bilgi için değer, birim/koşul, kaynak, durum ve gerekiyorsa
karar sahibi/tarih kaydedilmelidir. Kullanıcı beyanı ölçüm sayılmamalıdır.
Çelişen kaynaklar yan yana kaydedilmeli; doğru olan uydurulmamalıdır.

## I02. Doldurma sırası

1. Ürünün işlevi, fiziksel etkisi, kart/MCU kimliği.
2. Kritik çıkışların boot/reset/güç kaybı davranışı ve emniyet gereksinimi.
3. Bellek, clock, IRQ/DMA, çevrebirim ve üretici errata bilgileri.
4. Modüller, veri sahipliği, zaman/kaynak bütçeleri ve hata davranışı.
5. Protokol, kalıcı veri, güncelleme, güvenlik ve servis erişimi.
6. Gerçek build/test komutları, kaynak sürümleri ve teslim kapıları.

## I03. Kayıt şablonu ve tamamlanma

| ID | Bilgi/karar | Değer ve koşul | Kaynak | Durum | Sorumlu/sonraki işlem |
|---|---|---|---|---|---|
| Projeye göre atanır | Tek konu | Birimli değer | Dosya/belge/ölçüm | AÇIK | Gerekli kanıt |

Durumlar: KODDAN_OKUNDU, BELGEDEN_OKUNDU, HEDEFTE_DOĞRULANDI,
KULLANICI_BEYANI, ÖNERİ, AÇIK, gerekçeli UYGULANMAZ.
Karar kabulü ayrıca tarih/sahibiyle tutulur.

[ZORUNLU] hardware fiziksel gerçekleri; architecture gereksinim, davranış,
tasarım kararı ve doğrulama yollarını içermelidir. Genel kurallar
buraya kopyalanmamalıdır. Oturum sonunda kalan sorular ve bloke olan
işler kaydedilmelidir. Bütün alanların dolması ürün doğrulaması
olarak sunulmamalıdır.

## I04. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Kaynaklı proje tanımı ve soru-cevap |
| 2026-10-04 | 1.1.0 | I01: önceki yetki, son kullanıcı kararı ve ertelemelerin korunması |
