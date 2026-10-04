# Paket Geçişi ve Doğrulama Kaydı

Sürüm: 1.0.0 · Tarih: 2026-09-14.

## T00. Amaç ve kapsam

Önceki yönergelerdeki bulguların bu paketteki karşılığı ve doğrulama
durumu. Firmware test raporu değildir. Kök yönergeler değiştirilmedi.

## T01. Bulgu karşılıkları

| Önceki sorun | Bu pakette çözüm |
|---|---|
| Tekrarlanan ve çelişen kurallar | G04 tek sahip; proje seçimleri A10 |
| CMake/CubeIDE çelişkisi | A05/A11 gerçek proje build/test ayrımı |
| Ceedling varsayımı | A11 mevcut Makefile hedefleri |
| Koşulsuz interrupt enable | S03 önceki maskeyi geri yükleme |
| Tek yönlü publication örneği | S02 iki yönlü sahiplik iadesi ve dolu sonucu |
| Olay biti ile sayımın karışması | S01 olay/sayaç/kuyruk ayrımı |
| Lock-free değer 1'in kabulü | S04 her zaman lock-free ve gerçek tür |
| Sabit ARM talimatı iddiası | S04 compiler/ABI/assembly kanıtı |
| C++ nesne/tür hizalama karışması | CP04 hizalanmış tür örneği |
| C99 float typedef iddiası | C01 doğru tür ve float.h |
| Atomic exact-width typedef varsayımı | S01 _Atomic(T) |
| Packed'ın genel çözüm sayılması | D06/C03/R03 wire ile nesne yerleşimi ayrımı |
| Tüm struct'ları sıfırlama | C02 geçerli başlangıç |
| Sonsuz scheduler'ın yasaklanması | G02 tur başına sınırlı iş |
| Static initialization ve dolaylı heap | CP01/CP02 yaşam süresi/runtime |
| Genel ASCII/Türkçe çelişkisi | G04 kod ve belge kapsamı |
| Eksik yerel skill dosyaları | Dört SKILL.md ve bağlı referanslar mevcut |
| Belirsiz tamamlanma | G05/V05 kanıt katmanları ve teslim kapısı |

## T02. Kontroller

Paket doğrulayıcısı yerel Markdown bağlantılarını, UTF-8/LF biçimini,
skill giriş alanlarını ve örnek kaynaklarının ASCII olmasını kontrol
eder. --examples ile C/C++ örneklerini sıkı uyarılarla derler ve çalıştırır.
Bu kontrol bütün mühendislik kurallarının otomatik denetimi değildir.

[Doğrulama betiği](scripts/validate_package.py)

2026-09-14 sonuçları:

| Kontrol | Sonuç / kapsam |
|---|---|
| Paket dosyaları, 4 skill girişi ve yerel Markdown yolları | GEÇTİ |
| UTF-8/BOM yok, LF, örnek kaynaklarda ASCII | GEÇTİ |
| C11 mailbox ve olay bitleri | GEÇTİ; 1000 sıralı gönder/al, boş/dolu/NULL, olay birleşmesi ve oku-sıfırla |
| C++20 hizalanmış tür ve nesne dizisi | GEÇTİ; host static_assert ve adres kontrolü |
| Compiler | MinGW-w64 GCC/G++ 11.2.0, x86_64 |
| Bayraklar | -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2 -pedantic -O2; C++ için -fno-exceptions -fno-rtti |
| Skill Creator quick_validate.py | ÇALIŞTIRILAMADI: ortamda PyYAML yok; paket betiği dört girişin basit name/description biçimini kontrol etti |
| Hedef firmware build / modül testleri | ÇALIŞTIRILMADI; firmware değiştirilmedi |
| Eşzamanlı stress / gerçek ISR / DMA / Flash | ÇALIŞTIRILMADI; host sıralı kontroller bunları kanıtlamaz |

Çalıştırılan paket komutu: python engineering-guidelines/scripts/validate_package.py --examples
(depo kökünden). Ham son çıktı: .validation/host-validation.log.
Bu çıktı klasörü gitignore kapsamındadır.

İlk C derlemesinde GCC built-in ile pedantic sabit ifade tanısı alındı.
__extension__ yalnızca lock-free assert'lerine uygulandı; genel uyarılar
kapatılmadı. Son yeniden derleme ve çalıştırma geçti.

## T03. Devreye alma

Bu paket incelendikten sonra kök AGENTS/CLAUDE/Copilot girişleri
tek kural setine yönlendirilmeli; eski çelişkili kaynaklar aynı geçişte
güncellenmelidir. Bu işlem mevcut isteğin kapsamına dahil edilmedi.
Yeni klasörün varlığı otomatik yükleme veya kök değişikliği değildir.

## T04. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Bulgu eşlemesi ve doğrulama kaydı |
