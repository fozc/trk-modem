# Belge Yazım Rehberi

**Amaç:** Bu rehber, repoda üretilen doğal dilli belgelerin (kılavuzlar,
spesifikasyonlar, protokol dokümanları, okuma notları, karar kayıtları)
nasıl yazılacağını belirler.

**Kullanım yeri:** Agent ya da insan, yeni bir teknik belge yazdığında,
var olanı düzenlediğinde veya dış kaynak belgeyi yeniden biçimlendirdiğinde
bu rehbere uyar.

**Yapı:** Rehber iki katmandan oluşur. **Bölüm A — Genel Çekirdek** her
belgeye uygulanır. **Bölüm B — Belge Türü Profilleri** çekirdeği belge
türüne göre genişletir; yalnızca ilgili profil geçerlidir. **Bölüm C**
uygulanmış bir örnektir. Rehber kendi kurallarını uygulayarak yazılmıştır.

---

# BÖLÜM A — GENEL ÇEKİRDEK

Bu bölümdeki kurallar **her belge türüne** uygulanır.

## A.1 Dil kuralları

1. **Günlük Türkçe kullanılmalıdır.** Kısa cümleler kurulmalı; bir cümle
   yalnızca bir fikir taşımalıdır. Edebî, eski ya da az kullanılan
   sözcüklerden kaçınılmalıdır. Örnek: "şerh" → "not".

2. **Belgeler Türkçe karakterle yazılmalıdır** (ç, ş, ı, ö, ü, ğ). Kodun
   ASCII kuralı (AGENTS.md) **belgelere uygulanmaz**; yalnızca kod ve kod
   yorumları ASCII'dir.

3. **Teknik İngilizce terimler aynen korunmalıdır** (çevrilmemelidir):
   timeout, retry, request, response, frame, broadcast vb. Terimin ilk
   kullanıldığı yerde parantez içinde Türkçe açıklama verilmelidir.
   Örnek: "timeout (zaman aşımı)", "retry (yeniden deneme)".
   Alan/kavram özel terimleri (commit, tail, head gibi) ilgili profilde
   tanımlanır; bkz. Bölüm B.

4. **Anlatımda kişisel özne kullanılmamalıdır.** "biz/ben" yerine **rol adı**
   ya da edilgen biçim yazılmalıdır. İki taraflı belgelerde "biz" hangi
   tarafı gösterdiği belirsizdir. Örnek: "biz göndeririz" → "RTU gönderir".

5. **Kural cümleleri "-malıdır/-melidir" edilgen biçimiyle yazılmalıdır.**
   Bu kural yalnızca **Kurallar** bölümüne uygulanır; Amaç, Kapsam gibi
   betimleyici bölümlerde düz anlatım serbesttir. Örnek: "yaz" →
   "yazılmalıdır".

6. **Kural ile örnek ayrı cümlelerde yer almalıdır.** Örnek, "Örnek:"
   etiketiyle kendi satırında verilmelidir.

7. **Vurgu için yıldız, üçgen ya da emoji işareti kullanılmamalıdır;**
   gerekirse "**ÖNEMLİ:**" yazılmalıdır.

8. **Ölçüt her zaman açıkça yazılmalıdır.** Örnek: "uyuşmazlıkta ölçüt
   cihaz firmware'idir".

## A.2 Zorunluluk etiketleri (normatif belgeler)

Zorunluluk etiketleri yalnızca **normatif** belgelerde (spesifikasyon,
standart, protokol) kullanılır. Betimleyici belgelerde (kılavuz, okuma
notu) etiket gerekmez.

- **[ZORUNLU]** satır başına yazılmalı; uyulması şart olan kurallarda
  kullanılmalıdır.
- **[ÖNERİ]** önerilen ama gerekçeyle sapılabilen kurallarda kullanılmalıdır.
- **[İSTEĞE BAĞLI]** serbest bırakılan seçeneklerde kullanılmalıdır.
- **[YASAK]** yasaklarda kullanılmalıdır.

Etiket ile "-malıdır" birlikte kullanılabilir; bu pekiştirmedir.

İngilizce kaynak belgelerle uyum için karşılıklar:

| Etiket | RFC 2119 | Anlam |
|---|---|---|
| [ZORUNLU] | MUST / SHALL | mutlaka |
| [ÖNERİ] | SHOULD | önerilir, gerekçeyle sapılabilir |
| [İSTEĞE BAĞLI] | MAY | serbest |
| [YASAK] | MUST NOT | asla |

## A.3 Yaşam döngüsü etiketleri (açık kalemler)

Gelişen belgelerde henüz karara bağlanmamış maddeler işaretlenmelidir.

- **[AÇIK]** karar bekleyen madde satır başına yazılmalıdır.
- **[KARAR: YYYY-AA-GG]** kapatılan madde, tarihiyle işaretlenmelidir.

Örnek: "[KARAR: 2026-08-14] 0x05 hata kodu tahsis edildi."

## A.4 Yapı ve atıf

1. **Her kural tek yerde tam yazılmalıdır.** Başka bölümde hatırlatma
   gerekiyorsa yalnızca atıf yapılmalıdır. Örnek: "bkz. 0x46 komutu".
   Kuralın kopya-yapıştır edilmesi yasaktır; kopyalar güncellemelerde
   çelişir.

2. **Bölüm ve komutlar kararlı bir kimlik taşımalıdır** (numara ya da kod).
   Atıflar bu kimliğe yapılmalıdır; başlık metni değişince atıf kırılmamalıdır.

3. **Tablolar ve kurallar bağlayıcıdır; diyagramlar ve örnekler
   yardımcıdır.** Çelişki olursa kural ya da tablo esas alınmalı ve çelişki
   belge hatası olarak düzeltilmelidir.

4. **Belge künyesi bulunmalıdır:** sürüm ve tarih. Değişiklik geçmişi tablo
   (tarih / sürüm / etkilenen bölüm) olarak tutulmalıdır.

## A.5 Ortak başlık etiketleri

Her belge türünde aynı başlıklar kullanılmalıdır (okuyucu bir kez öğrenir):

**Amaç:** · **Kapsam:** · **Kullanım yeri:** · **Kurallar:** · **Örnek:**

Türe özgü ek başlıklar (İstek, Yanıt, Adımlar vb.) ilgili profilde tanımlanır.

---

# BÖLÜM B — BELGE TÜRÜ PROFİLLERİ

Çekirdeğe ek olarak, belge türüne göre yalnızca ilgili profil uygulanır.

## B.1 Protokol / API profili

Wire ya da API sözleşmesi tanımlayan belgeler için.

### B.1.1 İskelet

```
1. Bu belge nedir?   kim/ne için okur; kapsam dışı; sürüm
2. Kelimeler         terimler, her biri tek cümleyle (gerektiği kadar)
3. Sistem ve roller  kim ne yapar, kim kime sorar
4. Ortak kurallar    çerçeveleme, adres, hata kodları, byte/bit sırası
                     (her kural YALNIZCA burada)
5. Komutlar          her komut aynı kalıpta tek sayfa (B.1.3)
6. Akışlar           ne zaman / adımlar / hata olursa
7. Zamanlama tablosu tüm süreler tek tabloda
8. Örnekler          doğrulanmış tel vektörleri
9. Değişiklik geçmişi tarih / sürüm / etkilenen bölüm
```

### B.1.2 Byte ve bit sırası

**[ZORUNLU]** Ortak kurallar bölümünde çok-baytlı alanların byte sırası
(little-endian / big-endian) tek yerde belirtilmelidir.
**[ZORUNLU]** Bit alanları için bit numaralama (bit 0 = en düşük anlamlı)
ve dizi kimlikleri için byte sırası (ör. EUI-64 MSB-first) yazılmalıdır.

### B.1.3 Komut sayfası kalıbı

```
### 0xXX KOMUT_ADI — kısa açıklama

Amaç:           Bu komut ne işe yarar.
Kullanım yeri:  Hangi akışın hangi adımında kullanılır.
İstek:          Gövde tablosu (B.1.4) + C struct (B.1.5).
Yanıt:          Başarılı yanıtın (ACK) gövdesi: tablo + C struct.
Kurallar:       [ZORUNLU]/[YASAK] maddeleri.
Hata durumları: ERROR kodları ve kod bazlı davranış (yalnızca hatalar).
Örnek:          Tek cümlelik somut senaryo.
```

**ÖNEMLİ:** "Yanıt" yalnızca **başarılı** yanıtı (ACK gövdesi) anlatır.
ERROR kodları yalnızca "Hata durumları"nda yazılır; iki başlık karıştırılmamalıdır.

### B.1.4 Alan tablosu sütunları

Her gövde/blok tablosu şu sütunları taşımalıdır:

`Ofset | Boy | Alan | Tip | Birim | Geçerli değer | Açıklama`

**[ZORUNLU]** Rezerv/dolgu alanlar tabloda açıkça yer almalı; gönderende
hangi değerin yazılacağı (ör. 0x00) ve alıcının davranışı (yok say)
belirtilmelidir.

### B.1.5 Veri alanı → C struct

**[ZORUNLU]** Bir gövde ya da blok için bayt yerleşim tablosu tanımlandığında,
ona birebir karşılık gelen bir **C struct** da verilmelidir. Kurallar:

- **[ZORUNLU]** Sabit genişlikli tipler kullanılmalıdır (`uint8_t` .. `uint32_t`;
  boyut/indeks için `size_t`).
- **[ZORUNLU]** Yerleşim önemliyse struct `__attribute__((packed))` olmalıdır.
  Karışık boyutlu alanlar (ör. `uint8_t` ardından `uint32_t`) derleyici
  dolgusu (padding) üretir ve wire yerleşimini bozar; packed bunu engeller.
  Tüm alanlar zaten hizalıysa packed şart değildir, yine de açıklık için
  önerilir.
- **[ZORUNLU]** Derleme-zamanı bekçileri konmalıdır: toplam boyut için
  `_Static_assert(sizeof(...) == N)`, kritik alanlar için `offsetof` ile
  ofset doğrulaması.
- **[ZORUNLU]** Rezerv alanlar struct'ta açıkça yer almalıdır.
- **[ÖNERİ]** Çok-baytlı alanlar wire'da belirtilen byte sırasındadır (B.1.2).
  Packed struct doğrudan eşleme yalnızca aynı byte sıralı host'ta (ör.
  little-endian Cortex-M) geçerlidir; taşınabilirlik için serialize/
  deserialize yardımcıları tercih edilmelidir.

Packed'in gerektiği durum:

```c
#include <stdint.h>
/* u8 sonra u32: packed olmadan derleyici 3 bayt dolgu ekler (sizeof=8),
 * wire yerlesimi bozulur. Packed ile 5 bayt. */
typedef struct __attribute__((packed))
{
    uint8_t  code;    /* ofset 0 */
    uint32_t value;   /* ofset 1 (packed sayesinde) */
} example_msg_t;

_Static_assert(sizeof(example_msg_t) == 5U, "example_msg_t 5 bayt olmali");
```

### B.1.6 Örnekler

**[ZORUNLU]** Örnek baytlar doğrulanmış tel vektörü olmalıdır (CRC/COBS
yeniden hesaplanıp doğrulanmalı). **[ÖNERİ]** Örnek, çerçeveleme öncesi
mantıksal paketi ve çerçeveleme sonrası telde gidecek baytları ayrı
göstermelidir.

## B.2 Kılavuz / tutorial profili

Bir işi adım adım yaptıran belgeler için. Zorunluluk etiketi gerekmez.

```
Amaç:      Ne öğretilir / ne yaptırılır.
Ön koşul:  Başlamadan önce gerekenler.
Adımlar:   Sıralı, her adım tek işlem.
Doğrulama: Adımın başarılı olduğu nasıl anlaşılır.
Sık hatalar: Sık görülen tuzaklar ve çözümleri.
```

## B.3 Okuma notu / OCR profili

Dış kaynak belgeyi (PDF/OCR) yeniden biçimlendiren notlar için.

- **[ZORUNLU]** Kaynak damgası bulunmalıdır: kaynak belge adı, sürümü, sayfa.
- **[ZORUNLU]** Ölçüt açıkça yazılmalıdır (ör. "ölçüt kaynak belgedir").
- **[ÖNERİ]** OCR'den gelen kuşkulu değerler işaretlenmelidir.

## B.4 Karar kaydı (ADR) profili

Bir tasarım kararını kalıcılaştıran kısa belgeler için.

```
Bağlam:    Karar neden gerekti.
Karar:     Ne kararlaştırıldı.
Sonuçlar:  Artıları, eksileri, etkilediği yerler.
Tarih:     Kararın tarihi ve sürüm.
```

---

# BÖLÜM C — UYGULANMIŞ ÖRNEK (Protokol / API profili)

### 0x46 LOG_CONSUME_TO — "bu kadarını okudum" imleci

**Amaç:** Okunup merkeze aktarılan kayıtlar cihaza bildirilir. Böylece
bekleyen kayıt sayısı (pending) düşer ve "okunacak kayıt var" bildirimi susar.

**Kullanım yeri:** Kayıt çekme akışının son adımı — kayıtlar merkeze
aktarıldıktan sonra gönderilir.

**İstek:** `[index u16]` — gerçekten okunan son kaydın bir fazlası.

**Kurallar:**
- **[ZORUNLU]** Gövdeye, gerçekten okunan son kayıt numarasının bir fazlası
  yazılmalıdır. Numara dahil değildir: o kayıt henüz okunmamış sayılır.
- **[YASAK]** Okunmamış bir numara yazılmamalıdır. Cihaz bunu doğrulamaz;
  atlanan kayıtlar sessizce kaybolur ve hiçbir sayaç bu kaybı göstermez.

**Yanıt (ACK gövdesi, 4 bayt, little-endian):**

| Ofset | Boy | Alan | Tip | Birim | Geçerli değer | Açıklama |
|---|---|---|---|---|---|---|
| 0 | 2 | tail | uint16 LE | kayıt indeksi | 0..99 | tüketim imlecinin yeni konumu |
| 2 | 2 | left | uint16 LE | adet | 0..100 | hâlâ bekleyen kayıt sayısı |

```c
#include <stdint.h>
#include <stddef.h>   /* offsetof */

/* 0x46 LOG_CONSUME_TO ACK govdesi - 4 bayt, wire little-endian.
 * LE host'ta (Cortex-M) dogrudan eslesir. */
typedef struct __attribute__((packed))
{
    uint16_t tail;   /* tuketim imlecinin yeni konumu (0..99) */
    uint16_t left;   /* hala bekleyen kayit sayisi (0..100)   */
} log_consume_ack_t;

_Static_assert(sizeof(log_consume_ack_t) == 4U,
               "log_consume_ack_t 4 bayt olmali");
_Static_assert(offsetof(log_consume_ack_t, left) == 2U,
               "left alani ofset 2 olmali");
```

`left = 0` olduğunda "okunacak kayıt var" bildirimi susar.

**Hata durumları:** Geriye giden ya da henüz yazılmamış kayıt numarası →
ERROR 0x02 (INVALID_PARAM).

**Örnek:** 5 gönderildiğinde "5 numaradan öncekiler merkeze ulaştı; 5
numaralı kayıt hâlâ bekliyor" anlamı taşır.

---

*Bakınız: AGENTS.md "Language & wording directive" bölümü, kod ve kod
yorumları için ASCII kuralını içerir; bu rehber doğal dilli belgeler içindir.*
