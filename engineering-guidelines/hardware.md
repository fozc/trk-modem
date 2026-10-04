# Hardware — Smart Breaker Modem

Sürüm: 1.0.0 · Tarih: 2026-09-14 · Durum: kaynak incelemesi; hedef doğrulaması açık.

## H00. Amaç ve kayıt yöntemi

Fiziksel platform bilgileri. Yazılım davranışı architecture içindedir.
Aşağıdaki bağlantılar bu paketin bir üstündeki mevcut depoya gider.
Başka projeye taşınırken bu bilgiler o projenin kaynaklarından yenilenir.
KODDAN_OKUNDU fiziksel ölçüm anlamına gelmez.

## H01. Kimlik ve clock

| Alan | Değer | Kaynak | Durum |
|---|---|---|---|
| MCU | STM32U375VETx | [.cproject](../.cproject) | KODDAN_OKUNDU |
| Çekirdek / güvenlik | Cortex-M33 / Non-Secure | [Mevcut proje tanımı](../CLAUDE.md), .cproject | BELGEDEN_OKUNDU |
| Sistem clock | 96 MHz olarak tanımlı | .cproject / mevcut proje tanımı | BELGEDEN_OKUNDU; ölçülmedi |
| Pin/clock/peripheral konfigürasyonu | CubeMX projesi | [IOC](../troika-smart-breaker-modem.ioc) | Kaynak mevcut; ayrıntılı eşleme açık |
| Kart/silikon revizyonu | AÇIK | Şema ve cihaz kimliği gerekli | AÇIK |
| Üretici datasheet/RM/errata sürümleri | AÇIK | Sürüm ve belge kimliği gerekli | AÇIK |

## H02. Bellek ve fiziksel erişim

| Alan | Kaynakta görülen değer | Kaynak | Sınır |
|---|---|---|---|
| Standalone Flash bölgesi | 0x08000000; 512K | [FLASH linker](../STM32U375VETX_FLASH.ld) | Linker tanımı; silikon garantisi değildir |
| Bootloader sonrası uygulama | 0x08014000; 512K - 80K | [BOOT linker](../STM32U375VETX_BOOT.ld) | Yazılım tahsisi; A05 |
| RAM/DMA erişim haritası | AÇIK | Linker + üretici RM gerekli | Hedef erişim doğrulanmadı |
| Flash program/erase geometrisi ve süre | AÇIK | Üretici belgesi gerekli | Uygulama değişikliğinde bloke edici |
| Harici Flash özellikleri | AÇIK | Parça kimliği ve datasheet gerekli | NVRAM kodu fiziksel garanti değildir |
| Cache/MPU özellikleri | AÇIK | Hedef konfigürasyon ve üretici belgesi | Başka Cortex-M kuralları varsayılmaz |

## H03. Çevrebirimler ve çıkışlar

Proje tanımı UART, I2C, SPI, timer, ADC, RTC ve GPDMA kullanımı bildirir.
Tam pin, IRQ/DMA request ve aktif seviye tablosu henüz çıkarılmadı.

| Kritik fiziksel bilgi | Durum / gereken kaynak |
|---|---|
| Kesici kontrol hatlarının reset/boot/güç kaybı seviyeleri | AÇIK; şema + bench ölçümü |
| Harici pull/sürücü ve bağımsız koruma | AÇIK; şema |
| Güç/BOR/watchdog clock ve toleransları | AÇIK; IOC + üretici belgesi |
| Flash sırasında CPU/bus stall ve read-while-write | AÇIK; üretici belgesi |
| Debug halt sırasında çıkış/timer/watchdog etkisi | AÇIK; hedef konfigürasyon + ölçüm |

## H04. Açık konular

| ID | Eksik | Etkilediği iş | Sonraki işlem |
|---|---|---|---|
| H-01 | Kart/revizyon, şema ve üretici belge kimlikleri | Donanım sınırları | Kaynakları belirleme |
| H-02 | Fiziksel çıkışların güç/reset davranışı | Emniyet A02 | Şema inceleme ve yetkili bench doğrulaması |
| H-03 | Flash/DMA/cache ayrıntıları | Kalıcı kayıt ve update | İlgili donanım belgeleriyle tamamlama |
| H-04 | Clock, IRQ ve ölçüm düzeneği | Süre/stack kanıtı | Hedef test ortamı tanımı |

## H05. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Kaynaklı ilk proje profili; ölçülmeyen alanlar açık |
