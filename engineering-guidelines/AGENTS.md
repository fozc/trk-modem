# Gömülü Yazılım — Ortak Ajan Kuralları

Sürüm: 1.1.0 · Tarih: 2026-10-04 · Durum: repo yönergeleriyle uyumlu paket.

## G00. Amaç ve kapsam

Bu dosya, üretim C/C++ firmware geliştirme ve inceleme işlerinin girişidir.
Paketin kullanımı [KULLANIM.md](KULLANIM.md) içinde açıklanır.
Bu alt klasörün oluşturulması depo kökündeki yönergeleri değiştirmez.
Üst düzey talimatlar ve kullanıcının açık kapsamı geçerliliğini korur.
Paket üretim firmware'i veya sertifikasyon kanıtı değildir.

## G01. Başlangıç ve yönlendirme

[ZORUNLU] İlgili kod, mevcut kullanıcı değişiklikleri,
[hardware.md](hardware.md) ve [architecture.md](architecture.md)
incelenmelidir. Kabul ölçütü ve etkilenen davranış belirlenmelidir.

| İş | Okunacak kaynak |
|---|---|
| Tasarım ve kod geliştirme | [embedded-design](skills/embedded-design/SKILL.md) |
| Test ve kod inceleme | [embedded-verification](skills/embedded-verification/SKILL.md) |
| Hata analizi | [embedded-debugging](skills/embedded-debugging/SKILL.md) |
| Eksik proje tanımı | [embedded-project-intake](skills/embedded-project-intake/SKILL.md) |
| Teknik belge yazımı | [Belge rehberi](references/documentation.md) |

[ZORUNLU] Skill otomatik bulunamıyorsa bu tablodaki dosya açıkça okunmalıdır.
Skill'in yalnızca diskte bulunması yüklendiğinin kanıtı sayılmamalıdır.
İlgisiz referansların tamamı her görevde yüklenmemelidir.

## G02. Kaynak ve çalışma sınırları

[YASAK] Başlatma dahil üretim firmware'inde heap ayırma/serbest bırakma
kullanılmamalıdır. malloc/calloc/realloc/free, ayırma yapan new/delete,
RTOS heap API'leri ve dolaylı kütüphane ayırmaları kapsamdadır.
Host araçları/test framework'leri bu yasağın kapsamı dışındadır.

[YASAK] Recursion, VLA, alloca, setjmp/longjmp, C++ exception ve RTTI
kullanılmamalıdır. Sabit pool ve placement new koşulları tasarım
skill'inin D02 bölümündedir.

[ZORUNLU] İşlem başına iş miktarı, bekleme ve retry (yeniden deneme)
sınırlanmalıdır. Sürekli scheduler/main döngüsü izinlidir; her turdaki
iş sınırlı olmalı ve gerekli işlevlerin ilerlemesi izlenmelidir.

[YASAK] ISR içinde bloklayan çağrı, mutex bekleme, Flash silme/programlama
ve formatlı log yapılmamalıdır. Donanımın gerektirdiği kısa işlem
sırası, ilgili sürücüde süre sınırıyla tanımlanmalıdır.

## G03. Yetki, belirsizlik ve değişiklik kapsamı

[ZORUNLU] Kullanıcının önceden verdiği yetki korunmalıdır. Rutin,
kapsam içi uygulama ayrıntıları için tekrar onay istenmemelidir.
Commit/push/merge/rebase/reset, yıkıcı işlem ve henüz yetkilendirilmemiş
mimari/API/protokol/veri modeli/bellek yerleşimi kararı öncesinde
somut seçenek, öneri ve etki sunularak açık onay alınmalıdır.
Salt okunur git incelemesi bu değişiklik onayının kapsamı dışındadır.

[ZORUNLU] Mevcut projedeki koruma ve erteleme kararları
[architecture A09](architecture.md) üzerinden kontrol edilmelidir.
Son açık kullanıcı kararı esas alınmalı; erteleme düzeltme veya
üretim kabulü olarak kaydedilmemelidir.

[ZORUNLU] Bilinmeyen register, süre, güvenli çıkış durumu veya donanım
garantisi uydurulmamalıdır. Doğruluğu etkileyen eksik, proje referansında
etkilediği işle birlikte açık kaydedilmelidir. Yalnızca buna bağımlı
uygulama bekletilmeli; bağımsız analiz ve doğrulama hazırlığı sürdürülmelidir.
Kritik TODO bırakılması işin tamamlandığı anlamına gelmemelidir.

[YASAK] Kullanıcı değişiklikleri ezilmemeli; kapsam dışı yeniden düzenleme,
bağımlılık veya davranış değişikliği yapılmamalıdır.

## G04. Kural sahipliği ve dil

[ZORUNLU] Proje seçimi/değeri architecture veya hardware içinde,
genel yöntem ilgili skill içinde tutulmalıdır. Aynı kuralın uzun
kopyaları oluşturulmamalıdır. Çelişki, sessiz bir istisnayla değil
kaynağındaki düzeltme ve karar kaydıyla çözülmelidir.

[ZORUNLU] Kod ve kod yorumları ASCII; Türkçe belgeler doğru Türkçe
karakterlerle yazılmalıdır. API/register/protokol adları korunmalıdır.
Teknik anlam akıcılık uğruna değiştirilmemelidir.

## G05. Tamamlanma ölçütü

[ZORUNLU] İlgili build, test ve analiz sonuçları komut, sürüm ve kapsamıyla
raporlanmalıdır. Kod incelemesi, host testi ve hedef ölçümü ayrılmalıdır.
Çalıştırılmayan kontrol başarılı gösterilmemelidir. Uyarı bastırma,
test silme veya kabul ölçütünü düşürme ile başarı üretilmemelidir.

Rapor alanları: sonuç; değişiklik ve davranış etkisi; doğrulama kanıtı;
açık konu ve etkisi. Teknik kapılar V05 ve proje A11'de tanımlıdır.

## G06. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | İlk bütünlüklü paket |
| 2026-10-04 | 1.1.0 | G03: mevcut kullanıcı kararları; güncel tasarım/doğrulama skill'leri |
