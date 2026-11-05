# Yeni IEC104 kütüphanesi — tamamlama kapsamı ve iş listesi

**Sürüm:** 1.0.0

**Tarih:** 31.10.2026

**Kaynak kod tabanı:** `d89d0e0`; yeni stack’in son protokol değişikliği `da2ac83`

**Durum:** Önerilen geliştirme kapsamı; maddeler henüz uygulanmış veya mimari karar olarak onaylanmış sayılmaz.

## 1. Amaç ve kapsam

**Amaç:** Yeni stack’in mevcut durumunu göstermek ve genel amaçlı bir IEC104 kütüphanesine giderken eklenecek işleri tek tek tanımlamak.

**Kullanım yeri:** Kütüphane geliştirme sıralaması, uygulama/kütüphane sınırı ve özellik bazında test kabulü.

**Kapsam:** `Application/iec104_stack` çekirdeği, genel server servisleri, veri tipleri, isteğe bağlı client/güvenlik/genişleme modülleri ve ayrı modem adaptörünün kabulü.

“Tam IEC104” için tek bir evrensel özellik listesi yoktur. İnteroperability (birlikte çalışabilirlik) profilinde rol, veri tipleri ve servisler seçilir. Standart bir server’ın bütün legacy 101 tiplerini, client rolünü, dosya servisini ve bütün güvenlik uzantılarını aynı anda içermesi gerekmez. FreyrSCADA’nın [server interoperability belgesi](https://www.freyrscada.com/docs/FreyrSCADA-IEC-60870-5-104-Server-Interoperability.pdf), s. 3 ve 7–17, profil seçimlerini değerlendirmek için kullanılabilir.

Bu belge üç hedefi ayırır:

| Hedef | Anlamı | Tamamlama ölçütü |
|---|---|---|
| H1 — Modem server profili | Gerçek ürünün kullandığı tip ve servisler | Seçilen profil eksiksiz uygulanır; gerçek port/donanım kabulü geçer. |
| H2 — Genel amaçlı server | Yaygın monitoring, komut, read, counter ve parametre servisleri | İlgili D maddeleri ve Ek A’daki seçili tipler tamamlanır; farklı uygulama örnekleriyle doğrulanır. |
| H3 — Genişletilmiş ürün ailesi | Client, redundancy, dosya ve güvenli profiller | Seçilen isteğe bağlı modüller ayrıca tamamlanır; taban core zorunlu olarak büyütülmez. |

**Kurallar:** Ürün IOA anlamı, ölçüm kaynağı ve fiziksel cihaz işlemleri uygulamada kalmalıdır. Core, NVRAM/GSM/RF/RTC bilmemelidir. Sabit bellek ve tek handler yaklaşımı korunmalıdır. Her özellik için uygulanmış kod ve test kanıtı aranmalıdır; enum tanımı veya örnek kod tek başına servis desteği sayılmamalıdır.

## 2. Terimler

| Terim | Açıklama |
|---|---|
| Core | APCI, TCP byte parçaları, sequence, ACK, pencere ve timer işleyen çekirdek. |
| ASDU | Type ID, COT, CA/OA ve information-object (bilgi nesnesi) taşıyan uygulama veri birimi. |
| Encoder/decoder | Host alanlarını wire byte’larına dönüştüren / wire byte’larını host alanlarına çözen yardımcı. |
| GI | General interrogation; istasyon veya grup verilerinin sorgulanması. |
| SBO | Select-before-operate; seçimden sonra eşleşen Execute gerektiren kontrol. |
| Backpressure | K penceresi veya transport doluluğu nedeniyle gönderimin bekletilmesi. |
| Golden vector | Beklenen paketi üretim encoder’ından hesaplamadan doğrulayan sabit referans byte dizisi. |
| P1 / P2 / P3 | Sırasıyla temel/genel kullanım önceliği, kapsam genişletme ve seçili profile bağlı modül. Takvim veya standart zorunluluk derecesi değildir. |

## 3. Bugün mevcut olanlar

| Alan | Mevcut kapsam | Kaynak |
|---|---|---|
| Core | I/S/U frame, TCP parçalama/birleştirme, START/STOP/TEST, 15-bit sıra, K/W, T1/T2/T3, hata/timeout kapanışı | [core](iec104_core.c) |
| ASDU | Header parse, SQ=0/SQ=1 builder, uzunluk ve IOA sınırları, CP56 saat parse, encoded extension | [ASDU](iec104_asdu.c) |
| Tipli değerler | Monitoring 1/13/30/31/36; Type 45 SCO encoder/parser | [API](iec104_asdu.h) |
| Server | GI 20–36 mekanizması; saat 103; kısıtlı reset 105; test 107; başlangıç 70; reset IOA’sında opsiyonel Type 45 SBO | [server](iec104_server.c) |
| Yayın | Spontane COT=3, periyodik COT=1 ve hazırlanmış ASDU yayını; uygulama zamanlaması | [server API](iec104_server.h) |
| Uygulama | Ayrı modem klasörü; GI 20–24, mevcut NVRAM IOA’ları, RF/RTC/PowerBoard ve ACK sonrası kalıcı replay | [adaptör](../iec104_modem/README.md) |
| Genişletme | Tek `handle_request`, callback içinde IOA/değer seçimi; generic encoded point; Type 7 örneği | [örnekler](examples/README.md) |

**Kanıt sınırı:** Yeni stack’in son doğrulanmış sonucu 126 Ceedling, 21 TCP/c104, 6 bağımsızlık/kapasite testi ve dört ARM modül derlemesidir. Son merkezi 380 IEC104 testi eski ve yeni stack’i birlikte içerir; 380 sayısı yalnız yeni stack’in test sayısı değildir. Bu belgede yeni test koşusu veya fiziksel ölçüm yapıldığı iddia edilmez. Genel IEC104 uygunluk sertifikası henüz yoktur.

## 4. Kaynaklar ve FreyrSCADA’nın kullanım yeri

| ID | Kaynak | Kullanım ve sınır |
|---|---|---|
| R1 | [lib60870 sabit revision](https://github.com/mz-automation/lib60870/blob/7a388e3e133999e1ca77ba7521d55d074b7cd2bc/lib60870-C/src/iec60870/cs104/cs104_slave.c) | ACK, STOP, timer ve server dispatch algoritması için kaynak incelemesi. |
| R2 | [lib60870 user guide](https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc) | Genel API/servis ve read/GI kullanım karşılaştırması. `master` zamanla değişebilir; R1’in revision’ıyla aynı sürüm varsayılmaz. |
| R3 | [Yerel IEC104 referansı](../../../iec104/docs/IEC60870-5-104_Referans.md) | Tip, qualifier ve akış başvurusu; standardın normatif metninin yerini almaz. |
| R4 | [FreyrSCADA IEC104 deposu](https://github.com/FreyrSCADA/IEC-60870-5-104) ve [SDK açıklaması](https://github.com/FreyrSCADA/IEC-60870-5-104/tree/master/IEC104-Linux-SDK) | Public repo SDK, örnek ve hazır kütüphane dağıtımı içeriyor. Tam stack kaynak kodu açıkça incelenmiş sayılmaz; algoritma kopyalama kaynağı olarak kullanılmaz. |
| R5 | [FreyrSCADA server interoperability PDF](https://www.freyrscada.com/docs/FreyrSCADA-IEC-60870-5-104-Server-Interoperability.pdf) | İncelenen belge 17 sayfa, Stack Version 21.05.006. Profil ve test kapsamı kontrol listesi için yararlı; güncel bütün ürün sürümlerinin davranış kanıtı sayılmaz. |
| R6 | [FreyrSCADA Python paketi açıklaması](https://github.com/FreyrSCADA/IEC-60870-5-104/tree/master/IEC-104-Python) | ctypes/native kütüphane kullanan ikinci istemci adayı. Kurulum, kullanım koşulları ve otomasyona uygunluğu ayrıca doğrulanmalıdır; bu çalışmada kurulup çalıştırılmadı. |

FreyrSCADA’nın client interoperability PDF bağlantısına erişim bu incelemede tamamlanamadı; içeriği doğrulanmış dayanak olarak kullanılmadı. Windows/Linux SDK arşivlerinin içi ve ticari stack algoritması incelenmedi. Böylece public doküman incelemesi ile gerçekten çalıştırılmış bağımsız istemci testi birbirinden ayrılır.

## 5. İş listesi özeti

Aşağıdaki her D kimliği bölüm 6’da tek yerde ayrıntılandırılır. Bunlar önerilen işlerdir; P3 maddeleri seçilmeyen bir profil için zorunlu eksik değildir.

| ID | Öncelik | İş |
|---|---|---|
| [D01](#d01) | P1 | Uyumluluk profili ve hedef kapsam |
| [D02](#d02) | P1 | Servise göre COT ve hata yönlendirmesi |
| [D03](#d03) | P1 | Tek IOA okuma — C_RD_NA_1 / Type 102 |
| [D04](#d04) | P2 | Sayaç sorgusu — C_CI_NA_1 / Type 101 |
| [D05](#d05) | P1 | Ortak değer, qualifier ve zaman doğrulaması |
| [D06](#d06) | P2 | Eksik zamansız monitoring encoder’ları |
| [D07](#d07) | P2 | Eksik CP56 monitoring encoder’ları |
| [D08](#d08) | P2 | Koruma olayı veri tipleri — 38, 39, 40 |
| [D09](#d09) | P1 | Genel information-object decoder ve iterator |
| [D10](#d10) | P2 | Genel zamansız process command desteği — 45–51 |
| [D11](#d11) | P2 | Zamanlı process command desteği — 58–64 |
| [D12](#d12) | P1 | SBO’yu resetten bağımsızlaştırma |
| [D13](#d13) | P1 | Komut kabulü ile fiziksel tamamlanmayı ayırma |
| [D14](#d14) | P2 | Genel reset süreci — Type 105 |
| [D15](#d15) | P2 | Parametre yükleme ve aktivasyon — 110–113 |
| [D16](#d16) | P3 | Dosya aktarımı ve directory — 120–126 |
| [D17](#d17) | P3 | Standart log sorgusu — Type 127 |
| [D18](#d18) | P3 | 101/legacy CP24 tipleri — yalnız özel profil |
| [D19](#d19) | P3 | Legacy sistem komutları — 104 ve 106 |
| [D20](#d20) | P1 | GI tip politikası ve tutarlı snapshot |
| [D21](#d21) | P2 | Background scan ve çift bildirim politikaları |
| [D22](#d22) | P3 | Tek TCP oturumunda çoklu CA ve adres politikası |
| [D23](#d23) | P2 | Başlangıç bildirimi ve readiness profili |
| [D24](#d24) | P3 | Master/client rolü |
| [D25](#d25) | P3 | Çoklu peer ve redundancy |
| [D26](#d26) | P3 | TLS transport ve sertifika yönetimi |
| [D27](#d27) | P3 | Application-layer authentication uzantıları |
| [D28](#d28) | P1 | Kaynak bütçesi, backpressure ve teşhis |
| [D29](#d29) | P1 | Fuzz, bağımsız karşılaştırma ve uygunluk testleri |
| [D30](#d30) | P1 | Taşınabilir paket, sürüm ve CI |
| [D31](#d31) | P1 | Gerçek modem portu ve donanım geçiş kabulü |
| [D32](#d32) | P2 | Private ASDU ve genişletme sözleşmesi |

## 6. Tek tek yapılacaklar

<a id="d01"></a>

### D01 — Uyumluluk profili ve hedef kapsam

**Öncelik:** P1

**Mevcut durum:** Uygulanan profil README ve örnek kataloğunda dağınık; ürünler arasında doldurulabilir interoperability listesi yok.

**Eklenecek:** Server/client rolü, desteklenen Type ID–COT–yön eşleşmeleri, GI grupları, test-bit politikası, zamanlı tip kullanımı ve kapasite sınırları tek profil olarak tanımlanmalıdır.

**Kullanım yeri:** Doküman ve profil. **Bağımlılık:** Yok.

**Kabul ölçütü:** Seçili her özellik bir test kimliğiyle eşleşmeli; seçilmeyen özellik açıkça desteklenmiyor görünmelidir.

<a id="d02"></a>

### D02 — Servise göre COT ve hata yönlendirmesi

**Öncelik:** P1

**Mevcut durum:** process_asdu() temel olarak ACT=6 kabul ediyor; DEACT=8 istisnası Type 45 ile sınırlı. response_init() genel cevabı ACT_CON mantığına bağlıyor.

**Eklenecek:** COT doğrulaması ve cevap akışı servise göre ayrılmalıdır. REQUEST=5, counter sonuçları, parametre ve dosya akışları desteklenecekse ilgili COT sabitleri ve doğru hata eşlemesi eklenmelidir.

**Kullanım yeri:** Server ve protocol. **Bağımlılık:** D01.

**Kabul ölçütü:** Bir servisin geçerli COT değeri başka servise kabul edilmemeli; yanlış COT/CA/IOA için tam frame hata vektörleri doğrulanmalıdır.

<a id="d03"></a>

### D03 — Tek IOA okuma — C_RD_NA_1 / Type 102

**Öncelik:** P1

**Mevcut durum:** Type ID tanımlı; COT=5 ortak kontrolde reddediliyor. Uygulamaya IOA ile giden çalışan read servisi yok.

**Eklenecek:** IOA, mevcut tek handle_request callback’ine iletilmelidir. Uygulama gerçek veri tipini/değerini hazırlamalı; server ölçüm ASDU’sunu COT=REQUEST ile döndürmelidir. GI veya ACT_CON/ACT_TERM zinciri bu servise uygulanmamalıdır.

**Kullanım yeri:** Server ve uygulama. **Bağımlılık:** D02.

**Kabul ölçütü:** Bilinen IOA doğru tip/değerle dönmeli; bilinmeyen IOA 47 ile reddedilmeli; TX/K reddinde veri ve komut yan etkileri tekrarlanmamalıdır.

<a id="d04"></a>

### D04 — Sayaç sorgusu — C_CI_NA_1 / Type 101

**Öncelik:** P2

**Mevcut durum:** QCC, freeze ve counter-interrogation işlem akışı yok.

**Eklenecek:** QCC seçimi, sayaç grupları, freeze/read/reset istekleri ve 37–41 counter COT değerleri uygulanmalıdır. Sayaçların gerçek okunması ve izin verilen freeze/reset işlemleri uygulamada kalmalıdır.

**Kullanım yeri:** ASDU, server ve uygulama. **Bağımlılık:** D02, D06 veya D07.

**Kabul ölçütü:** Seçili sayaç grubu ACT_CON/veri/ACT_TERM ile tamamlanmalı; freeze/reset reddi sayacı değiştirmemeli; kısmi gönderim doğru devam etmelidir.

<a id="d05"></a>

### D05 — Ortak değer, qualifier ve zaman doğrulaması

**Öncelik:** P1

**Mevcut durum:** Mevcut setter’larda IOA/kalite sınırları var. write_time() host alanlarını doğrudan kodluyor; encoder tarafında tam tarih/saat semantik doğrulaması yok.

**Eklenecek:** SIQ/DIQ/QDS/BCR, signed değerler, normalize/ölçekli değerler ve qualifier bitleri için ortak sınır kontrolleri tamamlanmalıdır. Saat ayarlama ile IV=1 taşıyabilen monitoring zamanının doğrulama politikaları ayrılmalıdır.

**Kullanım yeri:** ASDU. **Bağımlılık:** D01.

**Kabul ölçütü:** Geçersiz IV=0 tarih/saat ve rezerve bitler reddedilmeli; geçerli IV=1 monitoring durumu korunmalı; setter hatasında önceki nesne değişmemelidir.

<a id="d06"></a>

### D06 — Eksik zamansız monitoring encoder’ları

**Öncelik:** P2

**Mevcut durum:** Üretim setter’ları Type 1 ve 13’ü kapsıyor. Type 7 yalnız örnek encoder olarak var.

**Eklenecek:** Type 3, 5, 7, 9, 11, 15, 20 ve 21 için tipli host alanları ve encoder’lar eklenmelidir. Step/transient, bitstring, normalized/scaled ölçüm, BCR ve packed-status anlamları ayrı korunmalıdır.

**Kullanım yeri:** ASDU. **Bağımlılık:** D05.

**Kabul ölçütü:** Her Type ID için tekli/çoklu SQ=0 frame, izin verilen SQ=1, alt/üst signed değerler ve kalite bitleri bağımsız golden vektörlerle doğrulanmalıdır.

<a id="d07"></a>

### D07 — Eksik CP56 monitoring encoder’ları

**Öncelik:** P2

**Mevcut durum:** Type 30, 31 ve 36 hazır; 32, 33, 34, 35 ve 37 için tipli setter yok.

**Eklenecek:** Step, bitstring, normalized/scaled ölçüm ve integrated-total değerlerinin CP56 sürümleri eklenmelidir. Zamanlı/zamansız eşlerin veri alanı doğrulaması ortak kullanılmalıdır.

**Kullanım yeri:** ASDU. **Bağımlılık:** D05, D06.

**Kabul ölçütü:** Her yeni tip için zaman, IV/SU, kalite ve çoklu nesne vektörleri doğrulanmalı; yanlış tip/zaman eşleşmesi sessizce kodlanmamalıdır.

<a id="d08"></a>

### D08 — Koruma olayı veri tipleri — 38, 39, 40

**Öncelik:** P2

**Mevcut durum:** M_EP_TD_1, M_EP_TE_1 ve M_EP_TF_1 yalnız enum’da mevcut.

**Eklenecek:** Koruma olayı, başlangıç olayı ve çıkış devresi bilgileri; olay kalitesi, ilgili süre alanı ve CP56 zamanı ile tipli olarak modellenmelidir. Modemdeki mevcut özel arıza alanları bu standart tiplerle kendiliğinden eşdeğer sayılmamalıdır.

**Kullanım yeri:** ASDU ve uygulama. **Bağımlılık:** D05.

**Kabul ölçütü:** Olay bit kombinasyonları, süre sınırları ve zaman/kalite alanları doğrulanmalı; GI/spontane seçimi D20/D21 profiline uymalıdır.

<a id="d09"></a>

### D09 — Genel information-object decoder ve iterator

**Öncelik:** P1

**Mevcut durum:** iec104_asdu_parse() header/payload görünümünü çözüyor; genel monitoring decoder’ı yok. Host parse yardımcıları SCO ve saat ile sınırlı.

**Eklenecek:** Desteklenen tipler için tipli decoder ve SQ=0/SQ=1 nesne dolaşımı eklenmelidir. Payload ömrü ve pointer sahipliği açık olmalı; ek tampon/heap gerektirmeyen görünüm tercih edilmelidir.

**Kullanım yeri:** ASDU. **Bağımlılık:** D05; ilgili encoder maddeleriyle birlikte.

**Kabul ölçütü:** Kısa/uzun payload, count uyuşmazlığı, 24-bit IOA taşması, rezerve bitler ve yabancı encoder’dan alınan bağımsız vektörler sınanmalıdır.

<a id="d10"></a>

### D10 — Genel zamansız process command desteği — 45–51

**Öncelik:** P2

**Mevcut durum:** Type 45 SCO yardımcıları var; hazır uygulama akışı reset IOA’sına özel. Diğer komutların qualifier/değer modelleri yok.

**Eklenecek:** Single/double/regulating-step, normalized/scaled/float set-point ve bitstring komutları için encoder/decoder ve servis doğrulaması eklenmelidir. Değerin cihazda uygulanmasına tek application handler karar vermelidir.

**Kullanım yeri:** ASDU, server ve uygulama. **Bağımlılık:** D02, D05, D12, D13.

**Kabul ölçütü:** Direct/Select/Execute/izinli cancel, hatalı değer/qualifier ve negatif onaylar tip bazında doğrulanmalı; tekrar TX komutu yeniden uygulamamalıdır.

<a id="d11"></a>

### D11 — Zamanlı process command desteği — 58–64

**Öncelik:** P2

**Mevcut durum:** Enum var; CP56 içeren kontrol komutlarının parser ve işlem akışı yok.

**Eklenecek:** D10’daki komutların CP56 sürümleri eklenmelidir. Kabul edilecek komut yaşı, saat geçersizliği ve clock-source anlamı profil/uygulama tarafından belirlenmelidir; keyfî zaman toleransı konmamalıdır.

**Kullanım yeri:** ASDU, server ve uygulama. **Bağımlılık:** D05, D10.

**Kabul ölçütü:** Geçerli/geçersiz zaman, eski komut, zamanlı Select/Execute eşleşmesi ve kaynak saatinin kullanılamadığı durumlar sınanmalıdır.

<a id="d12"></a>

### D12 — SBO’yu resetten bağımsızlaştırma

**Öncelik:** P1

**Mevcut durum:** server config/state içinde reset_ioa, reset_sbo ve seçilmiş reset komutu var. Genel IOA/komut SBO yöneticisi yok.

**Eklenecek:** Seçim takibi ürün resetinden ayrılmalıdır. Bağlantı/OA/CA/IOA/Type ID/değer/qualifier ve timeout eşleşmesi korunmalı; SBO gerekip gerekmediğine uygulama karar vermelidir. Gereksinim tek seçimse tek sabit işlem yeterlidir.

**Kullanım yeri:** Server ve uygulama. **Bağımlılık:** D01, D02.

**Kabul ölçütü:** Başka IOA/OA/tip/değer ile Execute reddedilmeli; seçim timeout, cancel ve bağlantı kapanışında bitmeli; eşleşmeyen giriş gerçek cihaz işlemi üretmemelidir.

<a id="d13"></a>

### D13 — Komut kabulü ile fiziksel tamamlanmayı ayırma

**Öncelik:** P1

**Mevcut durum:** Tek callback anlık sonuç veriyor. Type 45 reset için özel ACT_CON/ACT_TERM akışı var; genel gecikmeli işlem tamamlama API’si yok.

**Eklenecek:** Protokolün gerektirdiği komutlarda kabul, yürütülüyor ve tamamlandı/başarısız aşamaları yönetilmelidir. Uygulama geç gelen sonucu bildirebilmeli; eski oturuma ait sonuç yeni oturumda kullanılmamalıdır.

**Kullanım yeri:** Server ve uygulama. **Bağımlılık:** D02, D12.

**Kabul ölçütü:** ACT_CON alındığında cihaz işlemi tamamlandı sayılmamalı; başarı/başarısızlık, timeout, disconnect ve stale-completion senaryoları test edilmelidir.

<a id="d14"></a>

### D14 — Genel reset süreci — Type 105

**Öncelik:** P2

**Mevcut durum:** QRP=1 ve request_reset cihaz callback’i hazır. Alternatif QRP’ler ve reset anlamı modem profiline bağlı.

**Eklenecek:** Desteklenen QRP işlemleri application’a sorulmalıdır. Genel/reset-pending-data politikası veriyi gerçekten yönetebilen uygulamaya ait olmalıdır. Type 105’in S/E alanı olmadığı korunmalıdır; mevcut özel IOA istisnası D01’de açıklanmalıdır.

**Kullanım yeri:** Server ve uygulama. **Bağımlılık:** D02, D12, D13.

**Kabul ölçütü:** Desteklenmeyen reset negatif dönmeli; hiçbir kayıt izin/onay olmadan silinmemeli; onay gönderimi ile fiziksel reset sırası sınanmalıdır.

<a id="d15"></a>

### D15 — Parametre yükleme ve aktivasyon — 110–113

**Öncelik:** P2

**Mevcut durum:** Genel callback bazı activation isteklerini alabilir; tipli parametre ve aktivasyon servisi yok.

**Eklenecek:** Normalized/scaled/float parametreler, QPM/QPA ve aktivasyon/deaktivasyon süreçleri eklenmelidir. Bekleyen parametre ile kalıcı/aktif değer uygulama tarafından ayrılmalıdır.

**Kullanım yeri:** ASDU, server ve uygulama. **Bağımlılık:** D02, D05, D13.

**Kabul ölçütü:** Yanlış değer/adres/qualifier mevcut ayarı bozmamalı; yükle/aktive et/iptal et sırası ve kalıcı yazma hataları doğrulanmalıdır.

<a id="d16"></a>

### D16 — Dosya aktarımı ve directory — 120–126

**Öncelik:** P3

**Mevcut durum:** ASDU builder mevcut; dosya/section/segment işlem makinesi yok.

**Eklenecek:** Dosya seçimi, directory, section/segment, son segment ve dosya/section ACK akışları eklenmelidir. Akış kontrollü sabit boyutlu parçalarla çalışılmalı; dosya içeriğinin tamamı RAM’e alınmamalıdır.

**Kullanım yeri:** Ayrı dosya servisi ve uygulama storage portu. **Bağımlılık:** D02, D09, D28.

**Kabul ölçütü:** Boş/büyük dosya, yanlış dosya/section, checksum, kesinti/iptal/timeout ve son ACK olmadan tamamlandı saymama test edilmelidir.

<a id="d17"></a>

### D17 — Standart log sorgusu — Type 127

**Öncelik:** P3

**Mevcut durum:** F_SC_NB_1 yalnız enum’da var. Özel modem replay’i bu servis değildir.

**Eklenecek:** Zaman aralığıyla log sorgusu ve ilgili dosya/cevap ilişkisi seçili profile göre uygulanmalıdır. Sorgu kapsamı, log sahipliği ve kayıtların tüketilme politikası uygulamada kalmalıdır.

**Kullanım yeri:** Ayrı dosya/log servisi. **Bağımlılık:** D02, D16.

**Kabul ölçütü:** Ters/geçersiz zaman aralığı, boş sonuç, yetkisiz log, parçalı aktarım ve bağlantı kesintisi test edilmelidir.

<a id="d18"></a>

### D18 — 101/legacy CP24 tipleri — yalnız özel profil

**Öncelik:** P3

**Mevcut durum:** 2/4/6/8/10/12/14/16/17/18/19 enum’da var; encoder/servis yok.

**Eklenecek:** Bu tipler olağan IEC104 kapsamına otomatik eklenmemelidir. 101 desteği veya açık bir legacy ürün sözleşmesi seçilirse CP24 ve ilgili tipler ayrı uyumluluk modülünde geliştirilmelidir.

**Kullanım yeri:** İsteğe bağlı ASDU uyumluluk modülü. **Bağımlılık:** D01, D05, D09.

**Kabul ölçütü:** Varsayılan 104 profili bu tipleri destekli göstermemeli; seçilirse zamanın saat/tarih tamamlama politikası ayrıca test edilmelidir.

<a id="d19"></a>

### D19 — Legacy sistem komutları — 104 ve 106

**Öncelik:** P3

**Mevcut durum:** C_TS_NA_1 ve C_CD_NA_1 uygulanmamış. Type 107 testi ve Type 103 saat senkronu zaten mevcut.

**Eklenecek:** Type 104 zamansız test ve Type 106 gecikme ölçümü yalnız seçilen 104/legacy profile uygunsa eklenmelidir. Mevcut Type 107’nin eksikliği gibi listelenmemelidir.

**Kullanım yeri:** İsteğe bağlı sistem servisi. **Bağımlılık:** D01, D02.

**Kabul ölçütü:** Profil dışındayken desteklenmiyor sonucu korunmalı; eklenirse sabit test alanı/CP16, IOA/COT ve hata cevapları sınanmalıdır.

<a id="d20"></a>

### D20 — GI tip politikası ve tutarlı snapshot

**Öncelik:** P1

**Mevcut durum:** GI 20–36 mekanizması hazır; ürün 20–24 gruplarını kullanıyor. Grup 21/22’de zamanlı tip seçimi uygulama profiline bırakılmış.

**Eklenecek:** Grup başına izinli Type ID/COT eşleşmeleri interoperability matrisiyle kesinleştirilmelidir. Genel kütüphane açısından GI’nin zamanlı veri seçimi otomatik standart uyumluluğu sayılmamalıdır. Sorgu boyunca canlı örnek veya tutarlı snapshot gerekip gerekmediği uygulamada seçilmelidir.

**Kullanım yeri:** Server profil doğrulaması ve uygulama. **Bağımlılık:** D01, D02.

**Kabul ölçütü:** K=1, ikinci GI, boş grup, desteklenmeyen grup, tip değişimi, sorgu sırasında veri değişimi ve STOP/START devamı seçilen sözleşmeyle doğrulanmalıdır.

<a id="d21"></a>

### D21 — Background scan ve çift bildirim politikaları

**Öncelik:** P2

**Mevcut durum:** Spontane COT=3 ve periyodik COT=1 hazır. Background COT=2 ve bir değişim için zamanlı/zamansız çift bildirim hazır ürün servisi değil.

**Eklenecek:** Seçilen profilde background ve double-transmission gerekiyorsa COT/Type ID sınırları ve uygulama zamanlama politikası eklenmelidir. Ürün zamanlayıcısı ve içerik seçimi core içine taşınmamalıdır.

**Kullanım yeri:** Protocol/server yayın girişi ve uygulama. **Bağımlılık:** D01, D02, ilgili encoder’lar.

**Kabul ölçütü:** Bir değişim yalnız seçilen çift bildirimi üretmeli; pencere doluyken konum korunmalı; periyodik/spontane/GI öncelikleri bozulmamalıdır.

<a id="d22"></a>

### D22 — Tek TCP oturumunda çoklu CA ve adres politikası

**Öncelik:** P3

**Mevcut durum:** Her server instance tek common_address bekliyor; 0 ve 65535 init sırasında reddediliyor.

**Eklenecek:** Bir bağlantıda çoklu istasyon veya izinli broadcast gerekiyorsa CA yönlendirmesi ve tipe göre broadcast kuralları eklenmelidir. COT=2, CA=2, IOA=3 standart 104 uzunlukları korunmalıdır.

**Kullanım yeri:** Server ve uygulama yönlendirmesi. **Bağımlılık:** D01, D02.

**Kabul ölçütü:** Aynı IOA’nın farklı CA’larda karışmaması, OA’nın doğru dönmesi ve broadcast komutunun uygunsuz yan etki üretmemesi doğrulanmalıdır.

<a id="d23"></a>

### D23 — Başlangıç bildirimi ve readiness profili

**Öncelik:** P2

**Mevcut durum:** Type 70 var; COI=0 sabit. Her yeni server_open sonrasında bildirim yeniden hazırlanıyor.

**Eklenecek:** Cihaz başlangıcı, yerel parametre değişimi ve TCP reconnect anlamı ayrılmalıdır. COI ve bildirim zamanı uygulama readiness durumuna göre yapılandırılabilmelidir.

**Kullanım yeri:** Server ve uygulama. **Bağımlılık:** D01.

**Kabul ölçütü:** Cihaz hazır değilken hazır bildirimi verilmemeli; reconnect ve STOP/START sırasında seçilen profilin izin verdiğinden fazla başlangıç bildirimi oluşmamalıdır.

<a id="d24"></a>

### D24 — Master/client rolü

**Öncelik:** P3

**Mevcut durum:** Core, slave/server rolüne göre yazılmış; master istek/cevap takibi ve bağlantı açma işlemi yok.

**Eklenecek:** Çift rollü kütüphane hedeflenirse ayrı client modülü eklenmelidir. START/STOP act gönderimi, confirmation takibi, Type 107 sayacı, GI/read/komut transaction’ları ve T0 bağlantı timeout’u portla birlikte yönetilmelidir.

**Kullanım yeri:** Ayrı client modülü ve transport portu. **Bağımlılık:** D02, D09, D13.

**Kabul ölçütü:** Gerçek referans server’a bağlanma, timeout/reconnect, yanlış cevap/CA/IOA/TSC ve birden fazla istek sınırı test edilmelidir.

<a id="d25"></a>

### D25 — Çoklu peer ve redundancy

**Öncelik:** P3

**Mevcut durum:** Ayrı instance oluşturulabilir; ortak veri yönlendiren çoklu peer/redundancy yöneticisi yok.

**Eklenecek:** Gerekirse peer başına bağımsız APCI durumu ve aktif/pasif redundancy grupları eklenmelidir. Olayın bir peer tarafından ACK edilmesi ile tüm hedeflere teslim edilmesi ayrı modellenmelidir.

**Kullanım yeri:** İsteğe bağlı bağlantı yöneticisi. **Bağımlılık:** D01, D28.

**Kabul ölçütü:** Aktif peer değişimi, yeniden bağlanma, farklı ACK hızları ve bir peer kopunca diğerinin sequence/timer durumunun korunması sınanmalıdır.

<a id="d26"></a>

### D26 — TLS transport ve sertifika yönetimi

**Öncelik:** P3

**Mevcut durum:** Kütüphane TCP byte portu bekliyor; TLS handshake/sertifika servisi sunmuyor.

**Eklenecek:** Güvenli profil seçilirse doğrulanmış bir TLS transport adaptörü kullanılmalıdır. Sertifika/hostname veya peer kimliği doğrulaması, güven deposu, zaman ve anahtar yenileme politikası tanımlanmalıdır; özel kripto yazılmamalıdır.

**Kullanım yeri:** Transport ve platform güvenlik servisi. **Bağımlılık:** D01, D28.

**Kabul ölçütü:** Geçersiz/sona ermiş sertifika, yanlış peer, handshake kesintisi, kısmi TLS yazımı ve RAM sınırları test edilmelidir. Güvenli profil seçildiğinde bu madde zorunlu olur.

<a id="d27"></a>

### D27 — Application-layer authentication uzantıları

**Öncelik:** P3

**Mevcut durum:** Security Type ID’leri enum’da var; oturum/anahtar/mesaj doğrulama akışı yok.

**Eklenecek:** IEC 62351-5 / IEC 60870-5-7 uyumlu bir profil seçilirse ilgili doğrulama ve key/session süreçleri ayrı servis olarak geliştirilmelidir. TLS sağlanması bu servislerin tamamlandığı anlamına gelmez.

**Kullanım yeri:** İsteğe bağlı güvenlik servisi. **Bağımlılık:** D01, D09, D28.

**Kabul ölçütü:** Replay, kimlik doğrulama hatası, anahtar yenileme, restart ve mesaj bütünlüğü bağımsız testlerden geçmelidir. Normatif sürüm seçimi ayrıca kaydedilmelidir.

<a id="d28"></a>

### D28 — Kaynak bütçesi, backpressure ve teşhis

**Öncelik:** P1

**Mevcut durum:** K/W/T1/T2/T3, 255 bayt frame ve tek bekleyen komut cevabı mevcut. K üst sınırı 64; ikinci bloke komut oturumu kapatıyor.

**Eklenecek:** Sabit kapasite, poll süresi, en yüksek çağrı zinciri stack kullanımı ve overload politikası yayımlanmalıdır. Düşük maliyetli hata nedenleri/istatistik portu değerlendirilmeli; yeni kuyruk yalnız somut ihtiyaçla eklenmelidir.

**Kullanım yeri:** Core/server yapılandırma ve port sözleşmesi. **Bağımlılık:** D01.

**Kabul ölçütü:** K=1/64, uzun TX reddi, yoğun RX, STOP ve kontrol yanıtı beklemesi sınanmalı; partial-write portu frame’i kısmen kabul edip false dönmemelidir.

<a id="d29"></a>

### D29 — Fuzz, bağımsız karşılaştırma ve uygunluk testleri

**Öncelik:** P1

**Mevcut durum:** Yeni stack için mevcut kayıtta 126 Ceedling, 21 TCP/c104 ve 6 bağımsızlık/kapasite testi var. Fuzz/sanitizer ve bağımsız uygunluk kanıtı bu kayıtlarda yok.

**Eklenecek:** ASDU/core/server girişleri sanitizer ve fuzz ile sınanmalıdır. lib60870/c104 ve uygun ikinci üreticiyle karşılaştırmalı paket/akış testi yapılmalıdır. Her yeni özellik geçerli/geçersiz tam-frame ve transaction testleriyle kapanmalıdır.

**Kullanım yeri:** Merkezi test altyapısı. **Bağımlılık:** Her ilgili geliştirme maddesiyle birlikte.

**Kabul ölçütü:** Tek oracle olarak aynı serializer kullanılmamalı; seed, referans revision, beklenen davranış ve replay edilebilir hata örneği saklanmalıdır. Test adedi uygunluk sertifikası sayılmamalıdır.

<a id="d30"></a>

### D30 — Taşınabilir paket, sürüm ve CI

**Öncelik:** P1

**Mevcut durum:** Kütüphane kendi include klasörüyle derleniyor; ARM kontrol betiği bu projenin Release include/define bilgilerini okuyor. Doxygen ayarı var.

**Eklenecek:** Ürün deposundan bağımsız örnek build, sürüm/ABI politikası ve CI paketi hazırlanmalıdır. C11, endian/alignment, kapasite define’larının tüm translation unit’lerde eşliği ve Doxygen kontrolleri belgelenmelidir.

**Kullanım yeri:** Paketleme, örnek ve CI. **Bağımlılık:** D01.

**Kabul ölçütü:** Temiz başka klasörde yalnız kütüphane ile örnek derlenmeli; ürün header’ı/HAL/Contiki gerekmemeli; CI host ve ARM kontrollerini tekrar edebilmelidir.

<a id="d31"></a>

### D31 — Gerçek modem portu ve donanım geçiş kabulü

**Öncelik:** P1

**Mevcut durum:** Yeni adapter host ortamında çalışıyor; çalışan firmware halen eski stack’i kullanıyor. NVRAM/RF/event-log erişimi adapter’da; bazı mevcut libiec104 yardımcıları yeniden kullanılıyor.

**Eklenecek:** Geçiş ayrı kabul işi olarak yürütülmelidir. GSM tam-frame kabulü, gerçek ayarlar, RTC, fault replay, reset ve reconnect cihazda doğrulanmalıdır. Eski yardımcı bağımlılıkları taşınabilir kütüphane ile ürün adapter’ı arasında doğru sınırda tutulmalıdır.

**Kullanım yeri:** Modem adapter’ı ve donanım test altyapısı. **Bağımlılık:** D01, D28, D29, D30.

**Kabul ölçütü:** Mevcut troika-hwtest süitiyle K/TX doluluğu, güç/bağlantı kesintisi ve ACK öncesi kalıcı kayıt korunması doğrulanmalı; değişen test ayarları geri yüklenmelidir.

<a id="d32"></a>

### D32 — Private ASDU ve genişletme sözleşmesi

**Öncelik:** P2

**Mevcut durum:** Type 128–255 generic encoded/builder ile taşınabilir; bu otomatik servis doğrulaması sağlamıyor. Ortak activation kısıtı farklı servisleri de etkiliyor.

**Eklenecek:** Private tiplerin COT/IOA/uzunluk ve cevap aşamalarını tanımlayan küçük genişletme sözleşmesi oluşturulmalıdır. Tek application handler korunmalı; her tip için boş callback veya ikinci yönlendirme tablosu zorunlu tutulmamalıdır.

**Kullanım yeri:** Server genişletme sınırı ve doküman. **Bağımlılık:** D02, D05, D28.

**Kabul ölçütü:** Özel tip standart servisleri gevşetmemeli; yanlış payload/cevap, unknown-type ve bekleyen transaction sınırları sınanmalıdır.

## 7. Önerilen uygulama sırası ve akış

| Aşama | İşler | Çıkış koşulu |
|---|---|---|
| A — Sözleşme | D01, D02, D05, D20 | Profil, servis/COT matrisi, zaman ve GI tip seçimi açıktır. |
| B — Genel kullanım | D03, D09, D12, D13; D28–D30 eş zamanlı | Tek IOA okuma ve ürün resetinden bağımsız kontrol çalışır; API geriye uyumluluğu değerlendirilmiştir. |
| C — Veri ve kontrol kapsamı | D04, D06–D08, D10–D11, D14–D15, D21, D23, D32 | Seçili monitoring/komut/counter/parametre tipleri örnek ve testleriyle tamamlanır. |
| D — Seçilen genişlemeler | D16–D19, D22, D24–D27 | Yalnız kararlaştırılan modüller eklenir; taban profile ait kaynak bütçesi korunur. |
| E — Ürüne kabul | D31; D29/D30 sonuçlarıyla birlikte | Gerçek GSM/modem/donanım kabulü ve eski stack ile davranış karşılaştırması tamamlanır. |

Örnek planlanan read akışı; mevcut çalışır API olduğu iddia edilmez:

```text
TCP -> core (APCI/sequence/ACK)
    -> server (Type 102, COT=5, CA/IOA ve nesne uzunlugu)
    -> ayni application handle_request (istekteki IOA)
    -> application: o IOA'nin tipli degerini response'a ekler
    -> server: monitoring ASDU / COT=5
    -> core -> transport
```

Bu akışa ikinci `next_ioa` veya tip başına ayrı callback zinciri eklenmesi gerekmez. Liste dolaşımı GI’da uygulamada kalır; read yalnız istenen IOA’yı çözer.

## 8. Her madde için ortak tamamlanma kapısı

Bir D maddesi ancak aşağıdaki ilgili kontroller geçince tamamlandı sayılmalıdır:

1. Profil, Type ID, yön, izinli COT ve hata davranışı tanımlanmalıdır.
2. Ürün bağımlılığı portable core/ASDU içine taşınmamalıdır.
3. Tekli/çoklu nesne, izinli SQ ve bağımsız tam-frame vektörleri eklenmelidir.
4. K/TX reddi, partial RX, tekrar istek/ACK, timeout ve reconnect sınanmalıdır.
5. Komut kabulü, transport kabulü, SCADA ACK’i ve fiziksel uygulama sonucu ayrı doğrulanmalıdır.
6. Kaynak bütçesi ARM derleyicisinde ölçülmeli; host sonucu fiziksel zamanlama kanıtı sayılmamalıdır.
7. README/Doxygen, örnek ve interoperability satırı aynı kapsamı göstermelidir.
8. Unsupported işlem sahte başarıyla kapanmamalıdır.

## Ek A. Type ID bazında mevcut durum ve iş eşlemesi

81 satır mevcut `iec104_protocol.h` enum’undan çıkarılmıştır. Bu liste 101/104 ve güvenlik uzantılarını birlikte kapsar; 81 tipin tamamının olağan IEC104 profilinde zorunlu olduğu anlamına gelmez. Hazır tiplerin de genel decoder ihtiyacı D09 kapsamında kalır. Özel Type ID 128–255 sözleşmesi D32’dedir.

| ID | Type ID | Bugünkü durum | İlgili iş |
|---|---|---|---|
| 1 | `M_SP_NA_1` | Tipli encoder | D09/D20 |
| 2 | `M_SP_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 3 | `M_DP_NA_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 4 | `M_DP_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 5 | `M_ST_NA_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 6 | `M_ST_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 7 | `M_BO_NA_1` | Yalnız test edilmiş örnek encoder | D06 |
| 8 | `M_BO_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 9 | `M_ME_NA_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 10 | `M_ME_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 11 | `M_ME_NB_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 12 | `M_ME_TB_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 13 | `M_ME_NC_1` | Tipli encoder | D09/D20 |
| 14 | `M_ME_TC_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 15 | `M_IT_NA_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 16 | `M_IT_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 17 | `M_EP_TA_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 18 | `M_EP_TB_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 19 | `M_EP_TC_1` | Uygulama yok; enum tanımı mevcut | D18 |
| 20 | `M_PS_NA_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 21 | `M_ME_ND_1` | Uygulama yok; enum tanımı mevcut | D06 |
| 30 | `M_SP_TB_1` | Tipli encoder | D09/D20 |
| 31 | `M_DP_TB_1` | Tipli encoder | D09/D20 |
| 32 | `M_ST_TB_1` | Uygulama yok; enum tanımı mevcut | D07 |
| 33 | `M_BO_TB_1` | Uygulama yok; enum tanımı mevcut | D07 |
| 34 | `M_ME_TD_1` | Uygulama yok; enum tanımı mevcut | D07 |
| 35 | `M_ME_TE_1` | Uygulama yok; enum tanımı mevcut | D07 |
| 36 | `M_ME_TF_1` | Tipli encoder | D09/D20 |
| 37 | `M_IT_TB_1` | Uygulama yok; enum tanımı mevcut | D07 |
| 38 | `M_EP_TD_1` | Uygulama yok; enum tanımı mevcut | D08 |
| 39 | `M_EP_TE_1` | Uygulama yok; enum tanımı mevcut | D08 |
| 40 | `M_EP_TF_1` | Uygulama yok; enum tanımı mevcut | D08 |
| 41 | `S_IT_TC_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 45 | `C_SC_NA_1` | SCO yardımcıları; reset IOA’sında sınırlı servis | D10/D12/D13 |
| 46 | `C_DC_NA_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 47 | `C_RC_NA_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 48 | `C_SE_NA_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 49 | `C_SE_NB_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 50 | `C_SE_NC_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 51 | `C_BO_NA_1` | Uygulama yok; enum tanımı mevcut | D10/D12/D13 |
| 58 | `C_SC_TA_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 59 | `C_DC_TA_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 60 | `C_RC_TA_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 61 | `C_SE_TA_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 62 | `C_SE_TB_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 63 | `C_SE_TC_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 64 | `C_BO_TA_1` | Uygulama yok; enum tanımı mevcut | D11 |
| 70 | `M_EI_NA_1` | COI=0 başlangıç bildirimi | D23 |
| 81 | `S_CH_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 82 | `S_RP_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 83 | `S_AR_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 84 | `S_KR_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 85 | `S_KS_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 86 | `S_KC_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 87 | `S_ER_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 90 | `S_US_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 91 | `S_UQ_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 92 | `S_UR_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 93 | `S_UK_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 94 | `S_UA_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 95 | `S_UC_NA_1` | Uygulama yok; enum tanımı mevcut | D27 |
| 100 | `C_IC_NA_1` | GI mekanizması; grup seçimi uygulamada | D20 |
| 101 | `C_CI_NA_1` | Uygulama yok; enum tanımı mevcut | D04 |
| 102 | `C_RD_NA_1` | Uygulama yok; enum tanımı mevcut | D03 |
| 103 | `C_CS_NA_1` | Clock komut akışı; RTC uygulamada | D05/D09 |
| 104 | `C_TS_NA_1` | Uygulama yok; enum tanımı mevcut | D19 |
| 105 | `C_RP_NA_1` | QRP=1 reset akışı | D14 |
| 106 | `C_CD_NA_1` | Uygulama yok; enum tanımı mevcut | D19 |
| 107 | `C_TS_TA_1` | TSC/CP56 echo test servisi | D29 |
| 110 | `P_ME_NA_1` | Uygulama yok; enum tanımı mevcut | D15 |
| 111 | `P_ME_NB_1` | Uygulama yok; enum tanımı mevcut | D15 |
| 112 | `P_ME_NC_1` | Uygulama yok; enum tanımı mevcut | D15 |
| 113 | `P_AC_NA_1` | Uygulama yok; enum tanımı mevcut | D15 |
| 120 | `F_FR_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 121 | `F_SR_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 122 | `F_SC_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 123 | `F_LS_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 124 | `F_AF_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 125 | `F_SG_NA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 126 | `F_DR_TA_1` | Uygulama yok; enum tanımı mevcut | D16 |
| 127 | `F_SC_NB_1` | Uygulama yok; enum tanımı mevcut | D17 |

## Ek B. Kaynak kodunda hızlı kontrol noktaları

| Kontrol | Kaynak işaretçisi | Açıklama |
|---|---|---|
| COT=5 engeli | `iec104_server.c::process_asdu()` | Ortak ACT/Type45 cancel kontrolü; D02/D03 bulgusu. |
| Genel cevap aşaması | `response_init()`, `execute_request()`, `confirmation_cause()` | Read/counter/parametre için yalnız activation cevabı yeterli değildir. |
| Reset özel bağı | `is_reset_control()`, `check_reset_request()`, `reset_request_accepted()` | D12/D14 genelleştirme sınırı. |
| Monitoring zaman kontrolü | `iec104_asdu.c::write_time()`, `point_set_*()` | Encoder doğrulaması ile clock-parse doğrulaması aynı şey değildir. |
| Tipler | `iec104_protocol.h`, `examples/README.md` | Enum ve gerçek tip/servis desteği ayrı gösterilir. |
| Paketleme/port | `iec104_core_send()`, `iec104_server_publish_asdu()`, `core_io.write` | Tam frame kabulü ve K/TX beklemesi korunmalıdır. |

## 9. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 31.10.2026 | 1.0.0 | Mevcut kapsam, 32 iş kalemi, 81 Type ID eşlemesi ve FreyrSCADA kaynak değerlendirmesi |
