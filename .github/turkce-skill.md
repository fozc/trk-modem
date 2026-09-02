---
name: turkce-dokuman-ve-icerik-dil-standardi
description: AI tarafından Türkçe doküman, teknik doküman, gereksinim, tasarım belgesi, rapor, açıklama, kullanıcı arayüzü metni ve benzeri içeriklerin doğal, anlaşılır, terminolojik olarak doğru ve tutarlı biçimde üretilmesini sağlar. Günlük kullanımda yaygın Türkçe ifadeleri, alanın genel kabul görmüş teknik terminolojisini ve gerektiğinde İngilizce teknik terimlerin kontrollü kullanımını standartlaştırır.
---

# Türkçe Doküman ve İçerik Dil Standardı

## 1. Amaç

Bu skill, AI tarafından Türkçe olarak üretilen doküman ve içeriklerin:

- doğal ve akıcı Türkçe ile yazılmasını,
- günlük kullanımda yaygın ve genel kabul görmüş kelime ve kelime öbeklerinin tercih edilmesini,
- teknik alanlarda yerleşik ve genel kabul görmüş terminolojinin kullanılmasını,
- gerektiğinde İngilizce teknik terimlerin korunmasını,
- ilk kullanımda Türkçe karşılık ile İngilizce teknik terimin ilişkilendirilmesini,
- doküman boyunca terminolojik ve dilsel tutarlılığın korunmasını,
- kelime kelime çeviri ve yapay Türkçe ifadelerden kaçınılmasını,
- teknik doğruluğun ve okuyucu tarafından anlaşılabilirliğin birlikte korunmasını
sağlar.

Bu skill, yalnızca imla denetimi yapan bir kural seti değildir. Modelin **hangi kelimeyi neden seçmesi gerektiğini**, teknik terimlerle nasıl çalışacağını ve oluşturduğu metni hangi ölçütlerle kontrol edeceğini belirler.

---

## 2. Uygulama Kapsamı

Bu skill aşağıdaki içeriklerde varsayılan olarak uygulanmalıdır:

- teknik dokümanlar,
- yazılım ve gömülü sistem dokümanları,
- mimari ve tasarım dokümanları,
- gereksinim dokümanları,
- API ve arayüz dokümanları,
- kullanım ve kurulum kılavuzları,
- hata ve sorun açıklamaları,
- test dokümanları,
- raporlar,
- teknik analizler,
- proje ve süreç dokümanları,
- kullanıcı arayüzü metinleri,
- genel Türkçe açıklama ve bilgilendirme içerikleri.

Kullanıcı farklı bir dil, terminoloji veya kurum standardı açıkça belirtiyorsa o talimat önceliklidir.

---

## 3. Temel Dil İlkesi

Her Türkçe çıktı için temel kural şudur:

> Türkçe konuşan ve konuya hâkim bir profesyonelin doğal biçimde yazacağı şekilde yaz.

Dil seçiminde öncelik sırası:

**Doğallık → Anlaşılırlık → Terminolojik doğruluk → Tutarlılık → Resmiyet**

Resmiyet hiçbir zaman doğal ve anlaşılır anlatımın önüne geçirilmemelidir.

Metin daha profesyonel görünsün diye gereksiz biçimde ağırlaştırılmamalıdır.

---

## 4. Kelime Seçimi

### 4.1 Genel kullanım

Birden fazla Türkçe ifade mümkün olduğunda günlük ve profesyonel kullanımda daha yaygın, açık ve doğal olan tercih edilmelidir.

Örnek tercih tablosu:

| Kaçınılması gereken | Tercih edilmesi gereken |
|---|---|
| gerçekleştirilmektedir | yapılmaktadır |
| müteakiben | ardından / sonrasında |
| akabinde | ardından / sonrasında |
| söz konusu olan | ilgili / bu |
| haizdir | sahiptir |
| istifade etmek | kullanmak |
| temin etmek | sağlamak / almak |
| vuku bulmak | oluşmak / meydana gelmek |
| icra etmek | yapmak / gerçekleştirmek |
| marifetiyle | aracılığıyla / kullanarak |
| binaenaleyh | bu nedenle |
| mezkûr | ilgili / belirtilen |

Bu tablo mutlak bir yasak listesi değildir. Hukuk, mevzuat, resmî kurum yazışmaları veya belirli kurum standartları gibi bağlamlarda farklı kullanım gerekebilir.

### 4.2 Ağır ve eski kelimeler

Metni daha profesyonel göstermek amacıyla günlük kullanımda yaygın olmayan, eski veya yapay ifadeler kullanılmamalıdır.

Özellikle aşağıdaki türde ifadeler gereksiz yere kullanılmamalıdır:

- müteakip,
- mukabilinde,
- binaenaleyh,
- mezkur,
- mündemiç,
- hasebiyle,
- işbu,
- keza,
- akabinde.

Ancak mevzuat, sözleşme veya resmî metnin özgün terminolojisinin korunması gerektiğinde bunlar aynen korunabilir.

---

## 5. Teknik Terminoloji

Teknik terimlerde kelime kelime çeviri değil, **alanın yerleşik kullanımı** esas alınmalıdır.

Terim seçerken şu kaynak önceliği uygulanmalıdır:

1. Resmî standart veya spesifikasyon,
2. İlgili teknolojinin/protokolün resmî dokümantasyonu,
3. İlgili mühendislik veya bilim dalındaki yaygın sektörel kullanım,
4. Yerleşik Türkçe teknik kullanım,
5. Hedef kitlenin yaygın olarak kullandığı ifade,
6. Kelime kelime çeviri.

Kelime kelime çeviri son seçenek olmalıdır.

### 5.1 Teknik terim seçim kuralları

Aşağıdaki sorular terim seçilirken dikkate alınmalıdır:

1. Türkçede yerleşik bir karşılık var mı?
2. Bu karşılık gerçekten teknik çevrede kullanılıyor mu?
3. İngilizce terim sektörde daha yaygın mı?
4. Türkçe karşılık anlam kaybına veya yanlış anlaşılmaya neden oluyor mu?
5. Hedef okuyucu hangi kullanımı daha kolay anlayacaktır?
6. Dokümanın önceki bölümlerinde hangi terim kullanıldı?
7. Standart veya ürün dokümanı belirli bir terimi zorunlu kılıyor mu?

Sonuçta tek bir terminoloji tercihi yapılmalı ve doküman boyunca mümkün olduğunca korunmalıdır.

---

## 6. İngilizce Teknik Terimlerin Kullanımı

Türkçede yerleşik karşılığı bulunan terimler gereksiz yere İngilizce bırakılmamalıdır.

Bununla birlikte bazı teknik terimlerin İngilizcesi sektörde çok daha yerleşiktir. Böyle durumlarda İngilizce terim kullanılabilir ve hatta tercih edilmelidir.

İlk kullanımda mümkün olduğunda şu biçim kullanılmalıdır:

**Türkçe karşılık (English Term)**

Örnekler:

- durum makinesi (**State Machine**),
- çevresel birim (**Peripheral**),
- hata ayıklama (**Debugging**),
- güç kesintisi (**Power Loss**),
- kalıcı bellek (**Non-Volatile Memory, NVM**),
- doğrudan bellek erişimi (**Direct Memory Access, DMA**),
- gerçek zamanlı işletim sistemi (**Real-Time Operating System, RTOS**),
- uygulama programlama arayüzü (**Application Programming Interface, API**).

İlk tanımlamadan sonra doküman içinde uygun kısaltma veya yerleşik teknik kullanım kullanılabilir.

Örnek:

> Doğrudan Bellek Erişimi (Direct Memory Access, DMA) kullanılır. DMA yapılandırıldıktan sonra...

### 6.1 İngilizce terimin doğrudan kullanılabileceği durumlar

Aşağıdaki durumlarda İngilizce terim tek başına bırakılabilir:

- Türkçede doğal ve yerleşik bir karşılığı yoksa,
- Türkçe karşılık teknik çevrede neredeyse hiç kullanılmıyorsa,
- standart, protokol veya ürün dokümanında resmî terim İngilizceyse,
- yazılım geliştirme ortamında İngilizce kullanım fiilen standart hâle gelmişse,
- İngilizce kullanım teknik belirsizliği azaltıyorsa.

Örnek teknik terimler:

- firmware,
- bootloader,
- watchdog,
- callback,
- handler,
- driver,
- middleware,
- linker,
- debugger,
- commit,
- pull request.

Bu terimler için yapay veya teknik çevrede karşılığı olmayan Türkçe sözcükler üretilmemelidir.

---

## 7. Yaygın Terim Ayrımları

Benzer görünen ancak teknik olarak farklı kavramlar keyfî biçimde aynı kelimeye indirgenmemelidir.

Örneğin bağlama göre:

- configuration → yapılandırma,
- setting → ayar,
- parameter → parametre,
- option → seçenek,
- initialization → başlatma,
- implementation → gerçekleştirim / uygulama,
- requirement → gereksinim,
- specification → belirtim / teknik özellik,
- interface → arayüz,
- verification → doğrulama,
- validation → geçerleme / doğrulama; bağlama göre ayrım korunmalıdır.

Bir karşılık seçildiğinde aynı kavram için doküman boyunca tutarlı kullanılmalıdır.

---

## 8. Kelime Kelime Çeviriden Kaçınma

İngilizce kaynaklı düşünce yapısı Türkçeye mekanik olarak aktarılmamalıdır.

Model, İngilizce ifadenin sözcük sırasını değil **anlamını, bağlamını ve işlevini** Türkçede doğal biçimde ifade etmelidir.

Örnekler:

- `make sure` → "emin olun" / "kontrol edin",
- `as needed` → "gerektiğinde",
- `in order to` → çoğu durumda yalnızca "-mek/-mak için",
- `at this point` → bağlama göre "bu aşamada",
- `it should be noted that` → çoğu durumda kaldırılabilir,
- `please note that` → çoğu durumda doğrudan bilgi verilebilir.

İngilizce cümle yapısını korumak amacıyla Türkçe cümle yapaylaştırılmamalıdır.

---

## 9. Yapay ve Gereksiz Kalıp İfadeler

Metni uzatmak veya resmî göstermek amacıyla aşağıdaki türde kalıplar gereksiz yere kullanılmamalıdır:

- "Bu kapsamda...",
- "Bu doğrultuda...",
- "Söz konusu...",
- "Belirtilen husus...",
- "Yukarıda da ifade edildiği üzere...",
- "Ayrıca belirtmek gerekir ki...",
- "Önemle ifade etmek gerekir...",
- "Sonuç olarak değerlendirildiğinde...",
- "Bu noktada dikkat edilmesi gereken husus...",
- "Mevcut durumda...",
- "İlgili işlem gerçekleştirilmektedir...".

Bu tür ifadeler yalnızca gerçekten anlatıma katkı sağladığında kullanılmalıdır.

---

## 10. Cümle Yapısı

Teknik ve profesyonel metinlerde:

- kısa ve orta uzunlukta cümleler tercih edilmelidir,
- her cümlede mümkün olduğunca tek ana fikir bulunmalıdır,
- gereksiz edilgen yapı kullanılmamalıdır,
- anlamı bozmadığı sürece aktif anlatım tercih edilmelidir,
- aşırı sayıda yan cümle içeren cümleler gerektiğinde bölünmelidir.

Örnek:

Karmaşık:

> Sistem başlatıldığında yapılandırma dosyası okunarak ilgili parametreler kontrol edilir ve parametrelerden herhangi birinin geçersiz olması durumunda varsayılan değerlere dönülerek hata kaydı oluşturulur.

Daha okunabilir:

> Sistem başlatıldığında yapılandırma dosyası okunur. Parametreler kontrol edilir. Geçersiz bir parametre varsa ilgili parametre için varsayılan değer kullanılır ve hata kaydı oluşturulur.

Kısa cümle kullanımı bir dogma değildir. Teknik ilişkiyi bozacaksa cümle gereksiz yere parçalanmamalıdır.

---

## 11. Teknik Doğruluk ve Kesinlik

Teknik dokümanlarda belirsiz veya süslü ifadeler yerine ölçülebilir ve açık ifadeler tercih edilmelidir.

Kaçınılması gereken:

> Sistem mümkün olduğunca hızlı şekilde verileri işler.

Tercih edilmesi gereken:

> Sistem verileri en fazla 10 ms içinde işler.

Kesin değer bilinmiyorsa model değer uydurmamalıdır.

Belirsiz veya doğrulanmamış durumlarda açıkça:

- "belirtilmemiştir",
- "doğrulanmalıdır",
- "bağlama bağlıdır",
- "mevcut bilgilerle kesin olarak belirlenememektedir"

gibi ifadeler kullanılabilir.

---

## 12. Kısaltmalar

Teknik bir kısaltma ilk kez kullanıldığında mümkünse açık adı verilmelidir.

Örnek:

> Gerçek Zamanlı İşletim Sistemi (Real-Time Operating System, RTOS)

Sonraki kullanımlarda:

> RTOS

kullanılabilir.

Çok yaygın kısaltmalar hedef kitleye göre doğrudan kullanılabilir:

- CPU,
- RAM,
- ROM,
- USB,
- TCP/IP,
- HTTP,
- UART,
- SPI,
- I²C.

Kısaltma açılımı vermek metni gereksiz yere uzatacaksa hedef kitlenin bilgisi dikkate alınmalıdır.

---

## 13. Terim Tutarlılığı

Aynı kavram doküman boyunca gereksiz biçimde farklı kelimelerle ifade edilmemelidir.

Örneğin ilk bölümde:

> yapılandırma

kullanılmışsa sonraki bölümlerde aynı kavram için sebepsiz şekilde:

> konfigürasyon / ayar / configuration

kullanılmamalıdır.

Ancak kavramlar gerçekten farklıysa ayrım korunmalıdır.

Tutarlılık yalnızca kelimeler için değil, aşağıdakiler için de geçerlidir:

- başlıklandırma,
- kısaltmalar,
- teknik terimler,
- birim gösterimleri,
- komut adları,
- durum isimleri,
- hata isimleri,
- API isimleri,
- kavramsal sınıflandırmalar.

---

## 14. Başlıklar

Başlıklar kısa, açık ve doğrudan olmalıdır.

Tercih edilen:

- Sistem Mimarisi,
- Veri Akışı,
- Hata Yönetimi,
- Güç Kesintisi Senaryosu,
- Yapılandırma Parametreleri,
- API Kullanımı,
- Test Gereksinimleri.

Kaçınılması gereken:

- Sistem Mimarisi Hakkında Genel Değerlendirmeler,
- Veri Akışının Gerçekleştirilmesine İlişkin Hususlar,
- Hata Yönetimi Konusuna İlişkin Açıklamalar.

Başlık yalnızca gerekli bilgiyi taşımalıdır.

---

## 15. Kullanıcı Arayüzü Metinleri

Kullanıcı arayüzlerinde teknik doküman dili kullanılmamalıdır.

Tercih edilen:

- Kaydet,
- İptal,
- Sil,
- Yenile,
- Devam et,
- Bağlantıyı kes,
- Ayarları kaydet,
- Dosya seç.

Kaçınılması gereken:

- Kaydetme işlemini gerçekleştir,
- İşlemi iptal etme işlemini gerçekleştir,
- Seçili dosyanın sisteme yüklenmesini başlat.

Arayüz metinleri kısa, doğrudan ve kullanıcı odaklı olmalıdır.

---

## 16. Sayılar, Birimler ve Teknik Gösterimler

SI birimleri ve alanın yerleşik gösterimleri kullanılmalıdır.

Örnekler:

- 10 ms,
- 100 MHz,
- 3,3 V,
- 25 °C,
- 128 KB,
- 4 KiB.

Sayı ile birim arasında uygun boşluk bırakılmalıdır.

Teknik semboller ve standart gösterimler gereksiz yere değiştirilmemelidir.

Örnek:

- bit,
- byte,
- kbit/s,
- Mbit/s,
- MHz,
- GHz,
- mA,
- µs,
- ms.

Alan standardı farklı bir gösterim gerektiriyorsa o standart korunmalıdır.

---

## 17. Kod, API ve Teknik İsimler

Kaynak kodunda veya teknik arayüzde bulunan aşağıdaki öğeler Türkçeleştirilmemelidir:

- değişken adları,
- fonksiyon adları,
- API adları,
- makrolar,
- register isimleri,
- protokol alanları,
- dosya isimleri,
- komutlar,
- hata kodları,
- sabitler.

Örnek:

```c
flashlog_write();
DMA
CRC
M_EI_NA_1
```

Kod içindeki teknik isimler aynen korunmalıdır. Açıklama metni Türkçeleştirilebilir.

---

## 18. Teknik Görünmek İçin Dili Ağırlaştırma

Kaliteli teknik doküman:

- daha fazla İngilizce kelime kullanan,
- daha uzun cümleler kuran,
- daha resmî görünen,
- daha fazla edilgen yapı kullanan

doküman değildir.

Kaliteli teknik doküman:

**doğru terimi kullanan, açık, kısa, tutarlı ve yanlış anlaşılmaya kapalı dokümandır.**

---

## 19. Hedef Kitleye Göre Dil Seviyesi

Teknik doğruluk korunurken anlatım seviyesi hedef kitleye göre ayarlanmalıdır.

### Teknik uzmanlara yönelik içerik

Alan terminolojisi doğrudan kullanılabilir.

### Teknik ve teknik olmayan okuyucuların birlikte bulunduğu içerik

İlk kullanımda kısa açıklama yapılmalıdır.

### Son kullanıcıya yönelik içerik

Gereksiz teknik terimler azaltılmalı, gerekli terimler açıkça açıklanmalıdır.

Aynı teknik gerçek, hedef kitleye göre farklı ayrıntı seviyelerinde ifade edilebilir.

---

## 20. İngilizce Karşılık ile Türkçe Karşılığın Birlikte Kullanımı

İlk kullanımda Türkçe ve İngilizce ifadeyi birlikte vermek, teknik dokümanlarda özellikle tercih edilmelidir; ancak her kelime için zorunlu değildir.

Şu durumda birlikte kullanım özellikle uygundur:

- teknik terim ilk kez tanımlanıyorsa,
- İngilizce terim sektörde çok yaygınsa,
- daha sonra kısaltma kullanılacaksa,
- okuyucunun İngilizce dokümantasyonla çalışması bekleniyorsa,
- terimin farklı Türkçe karşılıkları arasında belirsizlik varsa.

Gereksiz yere her cümlenin İngilizcesi yazılmamalıdır.

---

## 21. Terminoloji Uyuşmazlığı

Bir terim için birden fazla karşılık bulunduğunda model rastgele seçim yapmamalıdır.

Şu öncelik sırasını kullanmalıdır:

**Standart → Resmî teknik dokümantasyon → Sektörel kullanım → Yerleşik Türkçe teknik kullanım → Hedef kitlenin kullanımı → Kelime kelime çeviri**

Belirsizlik çözülmezse en güvenli ve en yaygın teknik kullanım tercih edilmelidir. Kesinlik yoksa model bunu açıkça belirtmeli ve uydurma bir standardizasyon yapmamalıdır.

---

## 22. Dilbilgisi ve İmla

Türkçe yazım kurallarına mümkün olduğunca uyulmalıdır.

Özellikle:

- büyük/küçük harf kullanımı,
- noktalama işaretleri,
- birleşik ve ayrı yazılan kelimeler,
- özel isimler,
- kısaltmalar,
- Türkçe karakterler,
- sayı ve birim gösterimleri

tutarlı olmalıdır.

Teknik semboller, kod parçaları ve standart isimleri kendi özgün yazımlarını korur.

---

## 23. Türkçede Doğal Karşılığı Bulunan İngilizce Kelimeler

Türkçede yerleşik bir karşılık varken sırf daha teknik görünmek için İngilizce kelime kullanılmamalıdır.

Örneğin çoğu teknik bağlamda:

- configuration → yapılandırma,
- requirement → gereksinim,
- interface → arayüz,
- initialization → başlatma,
- error handling → hata yönetimi,
- data processing → veri işleme.

Ancak ilgili alanın gerçek kullanımında İngilizce terim baskınsa İngilizce tercih edilebilir.

Amaç Türkçeyi yapay biçimde tamamen yabancı terimlerden arındırmak değil, **doğal Türkçe ile gerçek teknik terminolojiyi dengeli biçimde kullanmaktır.**

---

## 24. Modelin Terim Kararı İçin Pratik Karar Ağacı

Bir teknik terimle karşılaşıldığında aşağıdaki karar süreci uygulanmalıdır:

### Adım 1 — Yerleşik Türkçe karşılık

Türkçede yerleşik ve doğal bir karşılık varsa onu kullan.

### Adım 2 — Sektörel kullanım

Türkçe karşılık olmasına rağmen sektör İngilizce terimi belirgin biçimde daha yaygın kullanıyorsa İngilizce terimi tercih et veya ilk kullanımda Türkçe + İngilizce biçiminde ver.

### Adım 3 — Standart terminolojisi

Terim bir standart, protokol, API veya ürün dokümanında tanımlıysa resmî terminolojiyi koru.

### Adım 4 — Yapay çeviri kontrolü

Türkçe karşılık yalnızca sözlükte bulunan, ancak gerçek teknik kullanımda olmayan yapay bir ifade ise bunu kullanma.

### Adım 5 — Tutarlılık kontrolü

Dokümanda aynı kavram daha önce nasıl adlandırıldıysa mümkün olduğunca aynı kullanımı koru.

### Adım 6 — İlk kullanım açıklaması

Gerekliyse ilk kullanımda:

**Türkçe karşılık (English Term, KISALTMA)**

biçimini kullan.

### Adım 7 — Sonraki kullanımlar

Sonraki bölümlerde gereksiz tekrar yapmadan yerleşik kısa biçimi kullan.

---

## 25. Modelin Çıktı Öncesi Kontrolü

Model, nihai cevabı vermeden önce oluşturduğu Türkçe metni içsel olarak aşağıdaki dört kontrolden geçirmelidir.

### 25.1 Dil kontrolü

- Cümle Türkçede doğal mı?
- Bir Türkçe konuşan profesyonel bunu gerçekten böyle söyler mi?
- İfade İngilizceden kelime kelime çevrilmiş gibi görünüyor mu?
- Gereksiz derecede resmî veya eski bir kelime var mı?
- Gereksiz kalıp ifadeler metne eklenmiş mi?

### 25.2 Terminoloji kontrolü

- Teknik terim doğru mu?
- İlgili sektörde yaygın mı?
- Türkçe karşılık yerinde mi?
- İngilizce kullanım gerektiğinde korunmuş mu?
- İlk kullanım için açıklama veya kısaltma gerekli mi?

### 25.3 Tutarlılık kontrolü

- Aynı kavram farklı isimlerle ifade edilmiş mi?
- Kısaltmalar tutarlı mı?
- Başlık ve gövde metni aynı terminolojiye sahip mi?
- Birim ve teknik gösterimler tutarlı mı?
- Kod ve API adları değiştirilmiş mi?

### 25.4 Doğruluk kontrolü

- Model bilmediği bir değeri uydurmuş mu?
- Teknik terimin anlamı değiştirilmiş mi?
- Çeviri sırasında teknik anlam kaybolmuş mu?
- Belirsiz bilgi kesin gerçek gibi sunulmuş mu?

Bu kontroller sonucunda sorun varsa çıktı verilmeden düzeltilmelidir.

---

## 26. Doküman İçinde Tutarlılık için Terim Kilitleme

Bir teknik doküman oluşturulurken önemli kavramlar ilk kez tanımlandığında kullanılan terminoloji mümkün olduğunca sabitlenmelidir.

Örneğin:

> yapılandırma (**configuration**)

şeklinde tanımlanmışsa dokümanın geri kalanında aynı kavram için sebepsiz yere:

> konfigürasyon / ayar / configuration

arasında geçiş yapılmamalıdır.

Bunun istisnası, kavramların teknik olarak gerçekten farklı olmasıdır.

---

## 27. Özel Alan ve Standartlar için İstisna

Bu skill genel Türkçe dil standardıdır; alan-spesifik bir standardın terminoloji kuralları varsa alan standardı önceliklidir.

Örneğin:

- IEC / ISO / IEEE terminolojisi,
- programlama dili terminolojisi,
- protokol spesifikasyonu,
- üretici dokümantasyonu,
- mevzuat,
- kurum içi terminoloji standardı

ile bu skill arasında çatışma varsa teknik olarak bağlayıcı kaynak tercih edilmelidir.

Ancak standardın adını veya teknik terimini kullanmak, tüm cümleyi yapay Türkçe ile yazmayı gerektirmez.

**Standart terim korunur, anlatım doğal Türkçe kalır.**

---

## 28. Genel Yasaklar

Model aşağıdaki davranışlardan kaçınmalıdır:

- Türkçeyi kelime kelime İngilizceden çevirmek,
- yapay teknik Türkçe kelimeler uydurmak,
- sırf profesyonel görünmek için dili ağırlaştırmak,
- aynı kavram için rastgele farklı terimler kullanmak,
- gereksiz İngilizceleştirme yapmak,
- teknik terimleri yanlış Türkçeleştirmek,
- kod/API isimlerini değiştirmek,
- doğrulanmamış teknik bilgi uydurmak,
- gereksiz kalıp cümlelerle metni uzatmak,
- her İngilizce terimi otomatik olarak Türkçeleştirmek,
- her teknik terimi otomatik olarak İngilizce bırakmak.

---

## 29. Varsayılan Uygulama Standardı

Kullanıcı özel bir dil standardı belirtmediği sürece model Türkçe içerik üretirken aşağıdaki kuralları varsayılan olarak uygulamalıdır:

1. Doğal Türkçe kullan.
2. Günlük ve profesyonel kullanımda yaygın kelimeleri tercih et.
3. Gereksiz resmî, eski ve ağır ifadelerden kaçın.
4. Teknik terimlerde alanın yerleşik kullanımını esas al.
5. Türkçede yerleşik karşılık varsa onu kullan.
6. İngilizcesi sektörde belirgin biçimde daha yaygınsa İngilizce terimi koru.
7. İlk kullanımda gerektiğinde `Türkçe karşılık (English Term)` biçimini kullan.
8. Gerekli kısaltmayı ilk kullanımda açıkla.
9. Aynı kavramı doküman boyunca aynı terimle ifade et.
10. Kelime kelime çeviri yapma.
11. Yapay Türkçe teknik karşılık üretme.
12. Kod, API ve protokol isimlerini değiştirme.
13. Bilinmeyen bilgiyi uydurma.
14. Cümleleri açık, doğrudan ve gereksiz yere uzun olmayan biçimde kur.
15. Hedef kitlenin teknik seviyesine uygun anlatım kullan.
16. Teknik doğruluğu korurken metni gereksiz biçimde karmaşıklaştırma.
17. Çıktıyı vermeden önce dil, terminoloji, tutarlılık ve doğruluk kontrolü yap.

---

## 30. Nihai İlke

Bu skill'in bütün kuralları tek bir temel ilkeye indirgenebilir:

> **Doğal Türkçe kullan; teknik terminolojide sektörün gerçek kullanımından kopma; ilk kullanımda gerektiğinde Türkçe karşılığı İngilizce terimle ilişkilendir; aynı kavramı tutarlı biçimde adlandır; kelime kelime çeviri, yapay Türkçe ve gereksiz resmiyetten kaçın.**

Üretilen metin, hem bir Türkçe okuyucuya doğal gelmeli hem de gerektiğinde ilgili İngilizce teknik dokümantasyonla eşleştirilebilmelidir.
