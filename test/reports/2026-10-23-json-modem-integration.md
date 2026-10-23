# JSON çekirdeğinin modem HTTP yollarına alınması

**Sürüm:** 1.0  
**Tarih:** 2026-10-23

## 1. Amaç ve kullanım yeri

Kullanıcının tüm geçiş onayıyla bağımsız C11 JSON v0.5 çekirdeği modem
HTTP okuma/yazma yollarına alındı. Bu rapor kaynak değişikliklerini,
yeniden çalıştırılan testleri ve fiziksel doğrulamanın sınırlarını kaydeder.
Ölçüm özeti ve kaynak SHA-256 değerleri
[ölçüm dosyasındadır](2026-10-23-json-modem-integration-metrics.json).

## 2. Kapsam ve davranış

| Üretim yolu | Sonuç |
|---|---|
| Cihaz, IEC104, Modbus ve röle durumu ayarları | Ortak reader (okuyucu), mevcut alan doğrulamaları |
| RF ayarları ve staging (geçici hazırlama) | Ortak reader, mevcut doğrulama ve kaydetme/iptal akışı |
| Cihaz durumu, RF durum/monitor/grup cevapları | Ortak writer (yazıcı), mevcut alan ve veri kaynakları |
| Modbus register bilgileri | Aynı writer içinde iç içe nesne; ikinci JSON birleştirmesi kaldırıldı |
| Login, terminal komutu, firmware metadata | Tam belge doğrulaması ve tür kontrolü |
| Sistem günlükleri, arıza kayıtları, terminal çıktısı | Tek JSON string içinde parçalı yazım ve ortak escape işlemi |
| Sabit JSON cevapları | Sabit metin korunur; uzunluk `strlen` ile hesaplanır |

`web_json.h`, mevcut HTTP tamponu üzerinde çekirdeği kullanan küçük bir
yardımcı katmandır. Heap, ikinci tam cevap tamponu veya yeni gönderim kuyruğu
eklenmedi. HTTP/GSM gönderim modeli ve NVRAM yerleşimi değiştirilmedi.
Yeni `json_write_string_begin/chunk/end` API'si günlük ve terminal
çıktısının tek string olarak parça parça yazılmasını sağlar.

Geçerli isteklerin alan adları ve kısmi ayar güncellemesi korundu. Sayısal
gösterimde mevcut xprintf yuvarlama ve basamak sayıları korundu; yalnızca
sayısal metin üretimi xprintf kullanır, JSON yapısı ve escape ortak çekirdeğe
aittir. Böylece fixed float API'sinin farklı yarım değer yuvarlaması mevcut
HTTP cevaplarına taşınmadı.

Kaynak ve testlerle doğrulanan düzeltmeler:

- Geçersiz escape, UTF-8, gömülü NUL, başta sıfırlı sayı, hatalı virgül ve
  belge sonundaki fazla veri reddedilir. Bilinen alanın uzun metni sessizce
  kesilmez. Toplam iç içe yapı sınırı kök dahil 32'dir.
- RF kısa veya boş dizileri yalnızca verilen elemanları değiştirir. Önceki
  geçici dizinin yazılmamış kuyruğundan ayar aktarılması giderildi. Geçersiz
  belgede mevcut staging iptal yolu korunur.
- Terminal komutu belge tamamen doğrulanmadan çalıştırılmaz. Login içinde
  yinelenen kimlik bilgisi alanları reddedilir.
- Tampona sığmayan dinamik cevap geçerliymiş gibi kesilerek gönderilmez;
  HTTP 500 döner. Sistem günlüklerinde bir sonraki kaydın escape sonrası
  boyutu ortak writer ile ölçülerek sayfalama yapılır.
- 13 sabit cevap uzunluğu hatası giderildi. Bunlar gövdenin eksik gitmesine
  veya literal sonundan fazla bayt okunmasına yol açabiliyordu. Artık elle
  yazılmış uzunluk yoktur; kalıcı test sabit gövdeleri de bağımsız çözümler.

## 3. Doğrulama

| Yeniden çalıştırılan kontrol | Sonuç |
|---|---|
| `ceedling test:all` | 1.682 / 1.682 geçti; hata ve atlanan test yok |
| JSON çekirdek Unity testleri | 92 ayrı test geçti |
| JSON coverage (kapsama) | Satır 1.090/1.119 (%97,4), dal 931/989 (%94,1), fonksiyon 89/89 (%100) |
| `ceedling json:http json:compare json:coverage` | Geçti; gerçek üretim fonksiyonları ve 1.002 durum snapshot karşılaştırması |
| Bağımsız referans paketi | Geçti; aşağıdaki sayı/Unicode ve parçalanma kontrolleri |
| Web auth ve transport paketleri | Geçti; session/lockout, RF iptal, kapasite, statik akış/reset/reddetme |
| Web navigation paketi | Geçti; gerçek Modbus metadata, minifier, i18n, ayar profili ve yükleme hata yolları |
| ARM Release ve EFW self-check | Geçti; ham bayt eşitliği ve ECDSA doğrulaması |

Bağımsız paket 2.005 belgeyi, 100.000 mutation (girdi değişikliği) için üç
parçalanmayı, 100.000 rastgele float desenini (99.606 sonlu değer), 100.000
sayısal metin değişikliğini ve 100.000 Clinger örneğini kapsar. Ayrıca
996.370 fixed float, 7.650 sınır örneği, 1.112.064 Unicode scalar için iki
kodlama, 24.576 iki baytlı biçim ve 2.048 eşleşmemiş surrogate kontrolü geçti.
C11/C++20, float açık/kapalı istemciler ve yedi geçersiz derleme örneği
de kontrol edildi.

Yeni `json:http` testi gerçek üretim handler fonksiyonlarını kaynaktan
alır; JSON algoritmasını mock etmez. Depolama, saat ve terminal sınırları
test ortamına uyarlanır. Python JSON çözücüsü cihazın escape edilmiş
ayarlarını, IEC IOA sütunlarını, iç içe Modbus haritasını, günlük aralığını,
90 arıza kaydını ve terminal çıktısını bağımsız doğrular. Bozuk komutlarda
çalıştırılmama, firmware metadata hataları ve küçük tampon HTTP 500 yolu
da sınanır. Sabit cevap uzunluğu düzeltmesinden sonra HTTP ve auth testleri
yeniden geçti; tam 1.682 test bu son sabit metin düzeltmesinden önce geçti.

Kalıcı tekrar çalıştırma girişleri `test/README.md` ve
`test/integration/json/Makefile` içindedir. Ayrıntılı bu çalıştırma logları
yerel `test/integration/json/build/production_{unit,acceptance,release}.log`
dosyalarındadır. `json:compare` artık geçiş öncesi firmware ölçümü değildir;
güncel üretim fonksiyonunu bağımsız adaptörle karşılaştırır.

## 4. Kaynak maliyeti

| ARM Release ölçümü | Geçiş öncesi yerel ELF | Son ELF | Fark |
|---|---:|---:|---:|
| text | 360.384 B | 362.560 B | +2.176 B |
| data | 516 B | 516 B | 0 B |
| bss | 126.096 B | 126.096 B | 0 B |
| Ham uygulama dosyası | 360.904 B | 363.080 B | +2.176 B |

Başlangıç değeri ilk yeniden derlemeden önce mevcut yerel ELF'ten alındı;
temiz iki Git commit'inin karşılaştırması değildir. Aynı çalışma ağacındaki
geçişi gösterir. ARM GCC 14.3.rel1 kullanıldı. Son değiştirilen modüllerin
derleme logunda warning/error görülmedi; tüm eski kaynaklar için temiz
derleme iddiası yoktur.

Bağımsız çekirdek object text/rodata boyutu `-Os` ile 7.280 B, `-O2` ile
11.403 B'dir; data/BSS sıfırdır. Float kapalı karşılıkları 4.950/6.595 B'dir.
Reader bağlamı 104 B, writer float açık 156 B ve kapalı 120 B'dir.

Statik tek fonksiyon stack frame (yığın çerçevesi) örnekleri: sistem
günlükleri 920 B, RF ayar POST 800 B, terminal 664 B, arıza kayıtları 496 B,
cihaz GET 368 B, IEC GET 296 B. Çekirdeğin en büyük tek çerçevesi
`-Os/-O2` için 480/512 B'dir. Bunlar toplam çağrı zinciri veya kesme anındaki
yığın tepe ölçümü değildir.

Üretilen paket:
`Release/troika-smart-breaker-modem_v1.0.1_20261023_185719_ff219be7.efw`
(219.192 B; ham uygulama CRC `F173B5D6`). İmza, sıkıştırma ve şifreleme
sonrası self-check geçti.

## 5. Kurallar ve kalan sınırlar

Parçalı writer kaynak verisi, ilgili `resume` tamamlanana kadar geçerli
tutulmalıdır. String chunk sınırı UTF-8 karakterini bölmemelidir. HTTP
yardımcıları eşzamanlı tamamlanan cevaplar içindir; kapasite hatasında
cevap iptal edilmelidir. Makro ayarları bütün derleme birimlerinde aynı
olmalıdır. Kullanım sözleşmesi `libraries/json/README.md` içindedir.

Bu çalışmada cihaz flash edilmedi; yeni firmware için fiziksel tarayıcı,
DWT süre veya çalışan cihaz yığın ölçümü yapılmadı. Önceki v0.2 cihaz
sonuçları bu sürüme genellenemez. Host testleri fiziksel GSM zamanlamasını
kanıtlamaz. Runtime ABI doğrulamalı init eklenmedi; tek firmware derlemesinde
ortak header ve derleme ayarları kullanılır.

## 6. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-23 | 1.0 | Modem geçişi, testler, Flash/RAM ölçümü ve fiziksel doğrulama sınırları |
