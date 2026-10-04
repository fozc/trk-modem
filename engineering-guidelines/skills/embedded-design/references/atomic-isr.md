# Atomik İşlemler ve ISR Paylaşımı

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## S00. Amaç ve kullanım yeri

C11 ile ISR/main/task arasında RAM paylaşımı. Dil memory model'i
(bellek modeli), compiler'ın ISR uygulaması ve donanım erişimi birlikte
değerlendirilir. Donanım ayrıntıları proje hardware/A04'ten alınır.

## S01. Seçim ve olay semantiği

[ZORUNLU] Paylaşılan nesnenin üreticisi/tüketicisi, yazan bağlam sayısı,
yaşam süresi ve veri kaybı politikası tanımlanmalıdır. Atomic veya
kritik bölüm seçilmeden önce gerçek çağrı yolları kaynakta
gösterilmelidir. Main/task, tüm ISR'ler ve DMA erişimleri; okuma,
yazma, read-modify-write (oku-değiştir-yaz), yayımlama ve sıfırlama
adımları birlikte incelenmelidir. Mevcut HAL/driver/framework koruması
ve kullanılan compiler/CMSIS uygulaması kontrol edilmelidir.

[ZORUNLU] Protokol rolü ve aynı anda bekleyen işlem sayısı
belirlenmelidir. Master rolü tek başına yarış olmadığına kanıt
sayılmamalıdır. Tek bekleyen request/response (istek/yanıt) için,
gerçek çağrı akışı yeterliyse mevcut buffer ve kısa snapshot
(anlık kopya) korunmalıdır. Kuyruk veya yeni paylaşım katmanı ancak
gösterilmiş eşzamanlılık ihtiyacı için eklenmelidir.

[ZORUNLU] Sahiplik veya mevcut kritik bölüm çakışan erişimleri zaten
dışlıyorsa ek atomic işlem otomatik olarak eklenmemelidir. Tek ISR'nin
yazdığı buffer/sayaç için main erişiminin tamamında ilgili IRQ'nun
maskelenmesi yeterli bir seçenek olarak değerlendirilmelidir.
Diğer yazarlar, task preemption (görevin kesilmesi), iç içe ISR ve DMA
dışlanmadan bu yöntem yeterli kabul edilmemelidir. Hedefin kesme ve
compiler sözleşmesi kanıtlanmalıdır; volatile tek başına koruma değildir.

[ZORUNLU] Dışlanmadan kalan eşzamanlı scalar (tek değer) erişim için
uygun atomic işlem ve memory order seçilmelidir. Release/acquire,
değiştirilmeye devam eden buffer'ı tek başına tutarlı snapshot yapmış
sayılmamalıdır. Atomic ile IRQ maskeleme birlikte kullanılacaksa her
birinin çözdüğü ayrı ihtiyaç yazılmalıdır. C11 için
_Atomic(uint32_t) gibi standart biçimler kullanılmalıdır;
atomic_uint32_t varlığı varsayılmamalıdır.

| İhtiyaç | Yöntem ve sınır |
|---|---|
| Tek ISR yazar, main yalnızca o IRQ kapalıyken erişir | S03 koşulları kanıtlanırsa kısa kritik bölüm; ek atomic zorunlu değildir |
| Bağımsız olay bitleri | fetch_or relaxed; aynı bitin tekrarları birleşir |
| Bitleri oku/sıfırla | exchange relaxed; ilişkili veri yayınlanıyorsa sıralama yeniden tasarlanır |
| Her olayı sayma | Atomik sayaç ve taşma politikası |
| Her olayın verisini saklama | Sınırlı kuyruk/ring ve doluluk politikası |
| Veri yayımlama | Release/acquire ile birlikte sahiplik iadesi |
| Çok alanlı tutarlı snapshot | Kısa kritik bölüm veya doğrulanmış sahiplik protokolü |
| MMIO | Üreticinin volatile register erişimi; RAM atomikleri doğrudan uygulanmaz |

[ZORUNLU] Volatile RAM senkronizasyonunun yerine kullanılmamalıdır.
Donanımda tek talimatlık load/store, read-modify-write işlemini veya
başka verinin yayımlanmasını güvenli yapmış sayılmamalıdır.

## S02. Yeniden kullanılabilir mailbox

[ZORUNLU] Release store ile yayımlanan buffer, tüketici sahipliği geri
verene kadar üretici tarafından değiştirilmemelidir. Tüketici bitirdikten
sonra release ile boş durumunu yayımlamalı; üretici yeniden kullanmadan
önce acquire ile bu durumu gözlemlemelidir.

Örnek: [atomic_mailbox.c.txt](../../../examples/atomic_mailbox.c.txt), tek
üretici/tek tüketici ve kopyalanan tek değer için çalıştırılabilir
örnektir. Dolu durumda gönderim başarısız döner; çağıran drop/retry
kararını verir. ISR retry ile beklememelidir.
Bu örnek çok üretici, DMA veya kesme içinde yeniden giriş için geçerli
değildir. Başlangıç yalnızca eşzamanlı erişim başlamadan yapılır.

Pointer yayımlamak pointee (gösterilen nesne) ömrünü çözmez.
_Atomic(uint8_t *) atomik pointer'dır; _Atomic uint8_t * ise atomik
byte'a pointer'dır. Deklarasyonun hangi nesneyi atomik yaptığı
incelenmelidir.

## S03. Kritik bölüm

[ZORUNLU] Gerçek rakip erişimleri dışlayan en dar kesme kapsamı
seçilmelidir. Tek ilgili IRQ yeterliyse PRIMASK ile bütün normal
kesmeler kapatılmamalıdır. Birden fazla erişim yolu varsa gerekli
kapsam gerekçelendirilmelidir. IRQ maskeleme DMA'yı veya başka task'ı
durdurmuş sayılmamalıdır.

[ZORUNLU] Önceki maske kaydedilmeli, uygun kesmeler maskelenmeli,
kısa işlemden sonra önceki durum bütün çıkış yollarında geri
yüklenmelidir. Koşulsuz enable yapılmamalıdır. Maske altında bekleme,
log ve uzun işlem yapılmamalıdır.

[ZORUNLU] Kullanılan CMSIS/driver sürümünde disable/enable sırası ve
compiler/memory barrier (bellek bariyeri) davranışı incelenmelidir.
Pending (bekleyen) kesme veya çevrebirim verisi, koruma kurmak adına
kendiliğinden temizlenmemelidir. Paket kopyalama ve sayaç sıfırlama
tek işlem olarak korunmalı; decode ve log dışarıda yapılmalıdır.
Kritik bölümün alım kapasitesi ve gecikme etkisi değerlendirilmelidir;
hedefte ölçülmemiş süre donanım garantisi olarak sunulmamalıdır.

Örnek: birden fazla normal ISR'nin dışlanması için PRIMASK gerektiği
kanıtlanmışsa aşağıdaki hedefe özgü sıra uygulanabilir. Tek IRQ için
varsayılan yöntem değildir; derlenebilir bağımsız program değildir.

```text
saved_mask = __get_PRIMASK()
__disable_irq()
kisa_cok_alanli_islem()
__set_PRIMASK(saved_mask)
```

[ZORUNLU] Yetki seviyesi, RTOS kritik bölüm sözleşmesi ve TrustZone
kapsamı doğrulanmalıdır. PRIMASK bütün exception'ları, başka güvenlik
alanlarını veya DMA'yı durdurmuş sayılmamalıdır. NMI/HardFault ile
paylaşım ayrıca değerlendirilmelidir.
Contiki port kısıtları proje A04'te bulunur.

## S04. Lock-free ve süre

[ZORUNLU] ISR'de kullanılan gerçek türün lock-free olduğu hedefte
kanıtlanmalıdır. GCC için tanım yanında
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), ...)
kullanılabilir; nesne doğal hizalı olmalıdır. C++ karşılığı CP03'tedir.
GCC built-in ISO C sabit ifadesi değildir; sıkı pedantic kontrolde
örnekteki __extension__ yalnızca bu assert için kullanılır. Bu dar
compiler uzantısı hedef doğrulama kaydında belirtilmelidir.

[ZORUNLU] ATOMIC_INT_LOCK_FREE kullanılacaksa değer 2 aranmalı ve
kontrolün int türünü kapsadığı bilinmelidir. Değer 1 yeterli değildir.
Byte/bool/başka türler için int makrosu kanıt sayılmamalıdır.

[ZORUNLU] Assembly kaydı compiler sürümü, CPU/ABI bayrakları,
optimizasyon, LTO ve kullanılan işlemle ilişkilendirilmelidir.
Exclusive retry döngüsü lock-free olabilir fakat sabit sürede
bitmeyebilir. ISR süre sınırı ve iç içe kesmeler değerlendirilmelidir.
Seq_cst geçerli bir memory order'dır; yalnızca daha güçlü olduğu için
hatalı örnek olarak gösterilmemelidir.

## S05. DMA ve inceleme

[ZORUNLU] CPU memory order, DMA cache bakımının yerine geçirilmemelidir.
Buffer erişilebilirliği, cache-line paylaşımı, tamamlanma ve iptal
sonrası sahiplik hedefe göre doğrulanmalıdır. M7 kuralları bütün
MCU'lara varsayılan olarak kopyalanmamalıdır.

[ZORUNLU] Sequence counter gibi yöntemlerde atomik olmayan payload'a
eşzamanlı erişim ve ISR'nin beklediği yazarın preemption durumu
incelenmelidir. Sadece sequence'i tekrar okumak genel bir data-race
çözümü sayılmamalıdır.

## S06. Kaynaklar ve değişiklik

- [GCC atomik built-in işlevleri](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)
- [CMSIS core register erişimi](https://arm-software.github.io/CMSIS_6/latest/Core/group__Core__Register__gr.html)
- [C11 komite taslağı](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf), 7.17

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Sahiplik iadesi, maske geri yükleme, lock-free sınırı |
| 2026-10-04 | 1.1.0 | Erişim kanıtı, master rolü, en dar IRQ koruması ve gereksiz atomic işlemlerin önlenmesi |
