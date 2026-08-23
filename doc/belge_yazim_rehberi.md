# Belge Yazım Rehberi

**Amaç:** Bu rehber, repoda üretilen doğal dilli belgelerin (kılavuzlar,
spesifikasyonlar, protokol dokümanları, okuma notları) nasıl yazılacağını
belirler. Kod ve kod yorumları AGENTS.md'deki ASCII kuralına tabidir;
bu rehber yalnızca doğal dil belgeleri içindir. Rehber kendi kurallarını
uygulayarak yazılmıştır — okurken örnek de görülmüş olur.

**Kullanım yeri:** Agent ya da insan, yeni bir teknik belge yazdığında,
var olanı düzenlediğinde veya dış kaynak belgeyi yeniden biçimlendirdiğinde
bu rehbere uyar.

---

## 1. Dil kuralları

1. **Günlük Türkçe kullanılmalıdır.** Kısa cümleler kurulmalı; bir cümle
   yalnızca bir fikir taşımalıdır. Edebî, eski ya da az kullanılan
   sözcüklerden kaçınılmalıdır ("şerh" → "not", "taahhüt" → "aldım
   işareti", "yuva" → "kayıt yeri / slot").

2. **Teknik İngilizce terimler aynen korunmalıdır** (çevrilmemelidir):
   frame, ACK, timeout, retry, commit, request, response, tail, head,
   broadcast, ring buffer vb. Terimin İlk kullanıldığı yerde parantez
   içinde Türkçe açıklama verilmelidir:
   "commit (bu kadarını aldım işareti)", "retry (yeniden deneme)".

3. **Emir kipi kullanılmamalıdır.** Kural cümleleri "-malıdır/-melidir"
   edilgen biçimiyle yazılmalıdır:
   "yaz" → "yazılmalıdır", "gönder" → "gönderilmelidir".

4. **Zorunluluk düzeyi etiketle belirtilmelidir:** satır başında
   **[ZORUNLU]**, **[ÖNERİ]** veya **[İSTEĞE BAĞLI]**. Etiket ile
   "-malıdır" birlikte kullanılabilir; bu pekiştirme sayılır, tekrar
   sayılmaz. Yasaklar **[YASAK]** etiketiyle yazılmalıdır.

5. **Kural ile örnek ayrı cümlelerde yer almalıdır.** Kural tek fikir
   taşımalı; örnek "Örnek:" etiketiyle kendi satırında verilmelidir.

6. **Her kural tek yerde tam yazılmalıdır.** Başka bir belge bölümünde
   hatırlatma gerekiyorsa yalnızca atıf yapılmalıdır: "bkz. 0x46
   Komutu". Kuralın kopya-yapıştır edilmesi yasaktır — farklı
   sürümlere ayrılan kopyalar, güncellemelerde çelişir.

7. **Sabit başlık etiketleri kullanılmalıdır** (okuyucu bir kez
   öğrenir, her sayfada aynı düzeni bulur):
   **Amaç:** · **Kullanım yeri:** · **İstek:** · **Yanıt:** ·
   **Kurallar:** · **Hata durumları:** · **Örnek:**

8. **Tablolar ve kurallar bağlayıcıdır; diyagramlar ve örnekler
   yardımcıdır.** Çelişki olursa kural ya da tablo esas alınmalı ve
   çelişki belge hatası olarak düzeltilmelidir.

9. Vurgu için yıldız/üçgen/emoji şerhleri kullanılmamalıdır; gerekirse
   "**ÖNEMLİ:**" yazılmalıdır.

10. Ölçüt her zaman açıkça yazılmalıdır (örn. "uyuşmazlıkta ölçüt cihaz
    firmware'idir").

## 2. Belge çeşidi ve yapı

Teknik/protokol belgeleri için önerilen iskelet (uluslararası merasim
kullanılmadan; bölümler ihtiyaca göre eksiltilebilir):

```
1. Bu belge nedir?      kim, ne için okur; kapsam dışı maddeler; sürüm
2. Kelimeler            20-25 terim, her biri tek cümleyle
3. Sistem ve roller    kim ne yapar, kim kime sorar
4. Ortak kurallar       çerçeveleme, adres, hata kodları... (her kural
                        YALNIZCA burada)
5. Komutlar             her komut aynı kalıpta tek sayfa (aşağıda)
6. Akışlar              ne zaman / adımlar / hata olursa ne yapılır
7. Zamanlama tablosu    tüm süreler tek tabloda
8. Örnekler             gerçek baytlar (doğrulanmış tel vektörleri)
9. Değişiklik geçmişi   tarih / sürüm / etkilenen bölüm tablosu
```

## 3. Komut sayfası kalıbı

```
### 0xXX KOMUT_ADI — kısa açıklama

Amaç:          Bu komut ne işe yarar.
Kullanım yeri: Hangi akışın hangi adımında kullanılır.
İstek:         Gövde tablosu (bayt / alan / tip / açıklama).
Yanıt:         ACK gövdesi tablosu ya da ERROR koşulları.
Kurallar:      [ZORUNLU]/[YASAK] maddeleri.
Hata durumları: Kod bazlı davranış.
Örnek:         Tek cümlelik somut senaryo.
```

## 4. Örnek sayfa (kalıbın uygulanmış hali)

### 0x46 LOG_CONSUME_TO — "bu kadarını aldım" işareti

**Amaç:** Okuyup merkeze gönderdiğimiz kayıtları cihaza bildiririz.
Böylece bekleyen kayıt sayısı (pending) düşer ve "okunacak kayıt var"
zili susar.

**Kullanım yeri:** Kayıt çekme akışının son adımı — kayıtlar merkeze
aktarıldıktan sonra gönderilir.

**Kurallar:**
- **[ZORUNLU]** Gövdeye, gerçekten okunan son kayıt numarasının bir
  fazlası yazılmalıdır. Numara dahil değildir: o kayıt henüz alınmamış
  sayılır.
- **[YASAK]** Okunmamış bir numara yazılmamalıdır. Cihaz bunu
  doğrulamaz; atlanan kayıtlar sessizce kaybolur ve hiçbir sayaç bu
  kaybı göstermez.

**Yanıt (ACK gövdesi, 4 bayt):** yeni tail (2 bayt) + kalan kayıt
sayısı `left` (2 bayt). `left = 0` olduğunda zil susar.

**Hata durumları:** Geriye gitme ya da henüz yazılmamış kayıt numarası
→ ERROR 0x02.

**Örnek:** 5 gönderildiğinde, "5 numaradan öncekiler merkeze ulaştı;
5 numaralı kayıt hâlâ bekliyor" anlamı taşır.

---

*Bakınız: AGENTS.md "Language & wording directive" bölümü kod yorumları
ve commit mesajları için ASCII kuralını içerir.*
