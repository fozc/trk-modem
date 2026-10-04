# Yönerge Paketi — Kullanım

Sürüm: 1.0.0 · Tarih: 2026-09-14.

## Amaç ve kapsam

Bu klasör, önceki genel yönerge tasarımının mevcut modem projesiyle
birleştirilmiş sürümüdür. Mevcut kök yönergeleri ve firmware değiştirilmedi.
Dosyaların tamamı yereldir; başka bir görevde oluşturulmuş skill
sayfalarına erişim gerektirmez.

## Dosyalar

| Kaynak | İçerik |
|---|---|
| [AGENTS.md](AGENTS.md) | Ortak sınırlar ve iş yönlendirmesi |
| [hardware.md](hardware.md) | Kaynakları belirtilmiş donanım bilgileri ve açıklar |
| [architecture.md](architecture.md) | Proje kararları, gerçek test yolları ve açıklar |
| [Tasarım skill'i](skills/embedded-design/SKILL.md) | Kod, kaynak, zaman, API ve kalıcı veri |
| [Doğrulama skill'i](skills/embedded-verification/SKILL.md) | Test, inceleme, analiz ve teslim |
| [Debugging skill'i](skills/embedded-debugging/SKILL.md) | Kanıt, hipotez ve kök neden |
| [Proje tanımlama skill'i](skills/embedded-project-intake/SKILL.md) | Eksikleri soru-cevapla tamamlama |
| [C kuralları](skills/embedded-design/references/c-rules.md) | Düzeltilmiş C güvenlik ve stil ayrımı |
| [Atomik/ISR](skills/embedded-design/references/atomic-isr.md) | Sahiplik, memory order ve kritik bölümler |
| [C++ alt kümesi](skills/embedded-design/references/cpp-subset.md) | RAII, yaşam süresi ve hizalama |
| [Belge rehberi](references/documentation.md) | Dil, yapı ve protokol belgeleme |
| [Geçiş ve doğrulama](DOGRULAMA.md) | Eski bulguların karşılığı ve yapılan kontroller |

## Adımlar

1. Başlangıç için bu klasördeki AGENTS.md açıkça okunur.
2. İlgili işin skill'i ve proje referansları yüklenir.
3. Kaynak/koddan çıkarılan değerler, donanımda doğrulanmış değerlerden
   ayrı değerlendirilir.
4. Açık kritik konular ilgili görev başlamadan tamamlanır.
5. İşin sonucunda kanıt ve sınırlamalar raporlanır.

Örnek başlangıç isteği:

> engineering-guidelines/AGENTS.md ve ilgili skill'i oku. Proje
> referanslarını esas alarak istenen işi incele. Açık donanım veya emniyet
> kararını uydurma. Mevcut kullanıcı değişikliklerini koru.

## Kurulum sınırı

Bu klasör bir hazırlık paketidir. Kök AGENTS.md ve mevcut .github
dosyaları hâlâ geçerlidir. Yeni paketin depo çapında devreye alınması,
eski çelişkili kuralların birlikte temizlendiği ayrı bir geçiştir.
Sadece bu klasörü eklemek kökteki çelişkileri gidermez.

Skill klasörleri SKILL.md ile birlikte references alt klasörleri
korunarak taşınmalıdır. Otomatik keşif, kullanılan aracın desteklediği
konuma bağlıdır; bu paket otomatik kurulum yapmaz. Elle kullanımda
G01 tablosundaki yollar yeterlidir. Skill tek başına taşındığında
proje referansları için proje kökü kullanıcı bağlamından bulunmalıdır.

Başka projede yalnızca hardware ve architecture proje içerikleri
değiştirilir. Modem değerleri kopyalanmaz. Genel yöntem ve yasaklar
korunur. Dosya adları dışındaki doküman alanları ilgili proje bilgisiyle
doldurulur; bilinmeyenler açık bırakılır.

## Doğrulama

Paket klasöründe: `python scripts/validate_package.py`.
Örnek derleme/çalıştırma için: `python scripts/validate_package.py --examples`.
İkinci komut PATH üzerinde gcc ve g++ gerektirir; geçici çıktılarını
paketin .validation klasörüne yazar. Bu klasör teslim kaynağı değildir.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Yerel, eksiksiz paket ve kullanım sınırı |

Örnek dosyaları .c.txt/.cpp.txt uzantılıdır. Doğrulayıcı dili -x ile
seçer. Bu uzantılar CubeIDE'nin örnek main işlevlerini firmware'e
kendiliğinden dahil etmesini önler.
