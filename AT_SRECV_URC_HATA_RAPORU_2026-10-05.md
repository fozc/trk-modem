# AT SRECV Binary Payload / URC Hata Raporu

**Tarih:** 05.10.2026 · **Sürüm:** 1.1

**Amaç:** Binary soket verisinin URC (modemin kendiliğinden gönderdiği bildirim) olarak ayrıştırıldığı hatayı kanıtlamak ve çözüm önerisini sunmak.

**Kullanım yeri:** AT motoru düzeltmesinin teknik incelemesi ve kabul testi.

**Kapsam:** Gerçek `Application/gsm/at_engine2.c`, GSM `utils.c` ve `ring_buff.c` host üzerinde çalıştırılmıştır. UART DMA, tick ve bildirim sınırları test çiftleriyle ayrılmıştır. Fiziksel modem veya saha arızası doğrulaması yapılmamıştır.

**Durum:** KAPATILDI (yazılım). Son kullanıcı talebiyle üretim kodu düzeltildi.
AT paketi 34/34, merkezi Ceedling 661/661 ve integration 9/9 geçmiştir.

**Okuma kuralı:** Bölüm 1–6 ilk hata raporunun tarihsel kaydıdır; kaynak satırları düzeltme öncesine aittir. Güncel uygulama, kanıt ve sınırlar bölüm 8'dedir.

## 1. Asıl sorun ve etkisi

`AT#SRECV` yanıtındaki binary payload (soket veri içeriği) içinde `SRING:` metni bulunabilir. Bu metin gerçek modem bildirimi değildir. AT motoru payload tamamlandıktan sonra yanıtın tamamını URC için tarar. Payload içindeki satırı URC callback'ine gönderir ve response buffer'dan (yanıt tamponu) siler.

Böylece soket verisi bozulur. Silme işlemi kaydedilen `srecv_payload_end` sınırını da geçersiz bırakır. Gerçek terminal `OK` bu eski sınırın önünde kalırsa yanıt tamamlanmış sayılmaz; timeout (zaman aşımı) riski oluşur.

Doğrudan ölçülen başarısızlık: gerçek URC bulunmayan yanıt için callback çağrı sayısı beklenen **0** yerine **1** olmuştur. Test ilk başarısız kontrolde durduğu için son `OK` assertion'ı bu koşuda çalışmamıştır. Tamamlanma riski kaynak akışından çıkarımdır; ayrı saha ölçümü değildir.

## 2. Kod kanıtı

- `Application/gsm/at_engine2.c:318`: `check_urc_in_response()` aramayı response buffer başlangıcından yapar; binary payload sınırını kullanmaz.
- Aynı fonksiyon callback sonrasında `response_buffer_remove()` ile bulunan satırı siler. Önceki CRLF ayırıcılarını da silme aralığına alabilir.
- `Application/gsm/at_engine2.c:708`: payload alınırken kontroller atlanır. Payload bittikten sonraki satır sonu yeniden URC taramasını tetikler (`:737`).
- `Application/gsm/at_engine2.c:752`: payload bitiş offset'i header işlenirken saklanır. `:474` ve `:493` terminal yanıtlarını bu sınıra göre değerlendirir.
- `Application/gsm/at_engine2.c:865` ve `:941`: reset/clear yollarında binary sayaç ve sınır temizliği ayrıca gözden geçirilmelidir. Yeni komut yolu bu alanları zaten sıfırlar (`:274–275`). Reset temizliği eksikliği bu rapordaki testte bağımsız bir arıza olarak gösterilmemiştir.

## 3. Yeniden üretim

Repo `test` dizininde:

```text
ceedling test:test_at_engine_scenario
```

Test: `test_at_srecv_binary_error_and_urc_patterns_are_not_dispatched`.

1. `AT#SRECV=3,1024\r` komutu başlatılır.
2. Aşağıdaki header ve tam **22 bayt** payload UART alıcı ring buffer'a verilir. `\0` gerçek NUL baytıdır; C string sonlandırıcısı olarak sayılmamıştır.
3. Modemin terminal yanıtı ayrı parçada verilir: `\r\n\r\nOK\r\n`.

```text
Header:  \r\n#SRECV: 3,22\r\n
Payload: \r\nERROR\r\nSRING: 3,6\r\n\0
Trailer: \r\n\r\nOK\r\n
```

Payload hex:

```text
0D 0A 45 52 52 4F 52 0D 0A 53 52 49 4E 47 3A 20 33 2C 36 0D 0A 00
```

Beklenti: payload aynen korunmalı, URC callback çağrılmamalı ve terminal yanıt `OK` olmalıdır.

Gözlenen: trailer sonrası `urc_calls` kontrolü `Expected 0 Was 1` ile başarısızdır. Kanıt: `test/build/production-audit-2026-10-03/at-engine-edges-before.log` ve `test-expansion-all.log`.

## 4. Önerilen dar çözüm

1. `check_urc_in_response()` içinde SRECV payload sınırı mevcutsa arama yalnız sınırdan sonraki metin bölgesinde yapılmalıdır. Sınır henüz buffer uzunluğunu aşıyorsa arama ertelenmelidir. Payload baytları hiçbir zaman URC taramasına katılmamalıdır.
2. Arama sonucunun yerel offset'i mutlak buffer offset'ine çevrilmelidir. Silme aralığı ve önceki CRLF'yi dahil eden mantık payload sınırının altına düşmemelidir; terminal ayırıcıları korunmalıdır.
3. Reset/clear yollarında `binary_bytes_remaining` ve `srecv_payload_end` sıfırlanmalıdır. Bekleyen gerçek URC'leri korumak için alıcı ring buffer temizlenmemelidir.
4. Header öncesindeki ve payload sonrasındaki gerçek URC'lerin işlenmesi korunmalıdır. Yeni kuyruk veya protokol değişikliği gerekli görünmemektedir.

Bu öneri kod incelemesine dayalıdır; uygulanmış veya testlerden geçmiş bir düzeltme olarak sunulmaz.

## 5. Kabul ve doğrulama

- Mevcut AT paketi **20/20** geçmelidir; başarısız regresyon atlanmamalı veya beklentisi değiştirilmemelidir.
- Binary NUL, `ERROR`, `OK` ve `SRING:` içeriği bayt bayt korunmalıdır.
- Gerçek URC header öncesinde ve payload sonrasında çalışmalıdır. SRECV sonrasında reset ile bekleyen gerçek URC'nin korunması mevcut pakette test edilir.
- Parçalı alım, payload sınırındaki CRLF ve sonraki normal AT komutu ayrıca doğrulanmalıdır.
- Merkezi Ceedling koşusu **553/553** geçmelidir. Sonrasında fiziksel modemle soket veri bütünlüğü ve bildirim sırası sınanmalıdır.

## 6. Tamamlanan test paketi ve sınırlar

| Paket | Unity testi | Sonuç |
|---|---:|---|
| SHA-256 edge case | 5 | 5 geçti |
| HMAC-SHA256 edge case | 4 | 4 geçti |
| AT motoru senaryoları | 20 | 19 geçti, 1 başarısız |
| Tüm Ceedling | 553 | 552 geçti, 1 başarısız, 0 atlandı |

Başlangıç 524/524'tür; **29 yeni Unity testi** eklenmiştir. SHA-256 için 10, HMAC için 56 bağımsız Python referans vektörü kullanılmıştır. Döngü içindeki split (parçalama) denemeleri ayrı Unity testleri olarak sayılmamıştır. Güncel kapsam yüzdesi üretilmemiştir; test sayısı coverage yüzdesi değildir. Integration ve ARM Release bu test ekleme adımında yeniden çalıştırılmamıştır.

## 7. Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 05.10.2026 | 1.0 | Binary payload/URC hatası, yeniden üretim, dar çözüm ve 553 test sonucu |
| 05.10.2026 | 1.1 | Uygulanan düzeltme; AT 34/34, Ceedling 661/661, integration 9/9 ve ARM/paket kanıtı |

## 8. Uygulanan düzeltme ve nihai kanıt

Kullanıcı ekteki ek incelemeyi dikkate alarak düzeltmeyi onaylamıştır.
Çalışma ağacı `28ca789` üzerine kuruludur; diğer RF/web/test değişiklikleri
korunmuştur. Ek incelemedeki saha ve probe iddiaları bu oturumun fiziksel
ölçümü olarak sunulmaz. Hata üretim AT motoruyla Ceedling'de yeniden
üretilmiş, gerçek callback tüketicisi kaynakta ayrıca doğrulanmıştır.

`gsm_process.c` içindeki gerçek tüketici, `SRING: 3,` ile IEC104 veri
bayrağını kurar; `NO CARRIER: 3` ile IEC104 soket durumunu kapalı yapar.
Parser artık bu içerikler payload içindeyken callback çağırmaz. Yeni
testler bu sınırda sıfır callback ve payload baytlarının tamamını doğrular.
Tüketicide ek kod değişikliği yapılmamıştır.

### 8.1. Küçük ve ortak sınır düzeltmesi

- `at_engine2.c:336–350`: URC araması `srecv_payload_end` sonrasından başlar. Sınır buffer uzunluğunu aşıyorsa tarama ertelenir. Bulunan yerel offset mutlak offset'e çevrilir.
- `:397`: önceki CRLF'yi silme işlemi metin sınırının altına inemez. Payload'ın son iki CRLF baytı korunur.
- `:462–467` ve `:517`: bütün terminal eşleşmeleri metin bölgesinde kalır. Genel CME araması da binary içerikten ve NUL'dan etkilenmez.
- `:746–747`: header bir kez sayacı kurar. Payload sonunda görünen sahte SRECV header'ı yeni binary pencere açamaz.
- Reset/clear/cancel binary sayacı ve bitiş sınırını sıfırlar. Reset/clear ring buffer'ı korur. Cancel'ın mevcut `rbuff_clear()` tam iptal davranışı korunur. Boş response buffer NUL ile sonlandırılır.

Yeni state, kuyruk, heap, protokol mesajı veya public API eklenmemiştir.
Normal AT yanıtları için metin başlangıcı sıfırdır; eski akış korunur.

### 8.2. Regresyon kapsamı ve sonuç

AT paketine **14 yeni Unity testi** eklenmiş, ilk hata testi bayt
bütünlüğü ve tamamlanmış işin sonradan timeout/retry olmaması kontrolüyle
güçlendirilmiştir. Düzeltme öncesi 33 senaryonun 11'i başarısızdır.
Son gerçek ERROR/CME testinin eklenmesiyle nihai paket **34/34** geçmiştir.

| Sınır | Doğrulanan davranış |
|---|---|
| Yedi URC türü ve CME metni | Binary içerik callback/yanıt sonucu üretmez; baytlar aynen korunur |
| NUL/0xFF ve payload sınırı | Metin taraması binary bölgeyi aşamaz; payload CRLF'si silinmez |
| Parçalı/bytewise alım | Birleşik 54 bayt örnek 55 split noktasında ve tek tek baytlarda aynı yanıtı üretir |
| Gerçek URC | Header öncesi, payload sonrası, NUL sonrası, kısmi ve birden çok satır doğru işlenir |
| İptal/toparlanma | Kısmi payload ve timeout sonrası reset, clear, cancel, retry ve sonraki normal komut çalışır |
| Payload uzunluğu | 0 ve talep edilen en büyük 1024 bayt örnek tamamlanır |
| Gerçek terminal hata | Binary NUL sonrası ERROR, CME 10 ve CME 42 doğru sonuç verir |
| DONE ve bekleyen URC | Geçerli yanıt korunur; fazladan retry oluşmaz; reset sonraki gerçek URC'yi kaybetmez |

| Doğrulama | Nihai sonuç | Kanıt |
|---|---|---|
| AT Ceedling | 34/34 | Tam JUnit ve sring-all-unit-final.log |
| Tüm Ceedling | 57 dosya, 661/661; failed/ignored=0 | sring-all-unit-final.log |
| Merkezi host integration | 9/9 paket | sring-all-host.log |
| ARM Release incremental build/link | Başarılı; warning/error yok | sring-release-final.log |
| Firmware paket self-check | Raw byte equality ve ECDSA geçti | sring-package-final.log |
| Git diff whitespace | Başarılı | git diff --check |

Log kökü `test/build/production-audit-2026-10-03/`'tür. Düzeltme öncesi
kanıt `sring-regression-before.log` içindedir. İlk birleşik host koşusunda
660 test geçmiştir; son test eklendikten sonra bütün Ceedling yeniden
661/661 geçmiştir. Diğer çalışma ağacı testleri toplamın içindedir; bu
SRING düzeltmesinin eklediği sayı yalnız 14'tür. Döngü kontrolleri ayrı
Unity testleri olarak sayılmamıştır.

Host derleme MinGW GCC 11.2.0, C11, pedantic, Wall/Wextra/Werror/shadow/
conversion/double-promotion/format=2 ve AT profiline özel LTO/O2 kullanır.
Eski GSM utils conversion istisnası yalnız helper include'u içinde kalır.
ARM Release mevcut GCC 14.3.rel1 Cortex-M33/Os yapılandırmasını kullanır.
Derleyicinin statik stack kayıtları URC için 32 B, response kontrolü için
24 B, consume için 32 B'dir; bunlar uçtan uca ölçülmüş stack üst sınırı değildir.
BSS alanına yeni tampon veya state eklenmemiştir.

### 8.3. Kabul sınırı

SRING yazılım regresyonu kapatılmıştır. Fiziksel modem/UART/IRQ/DMA,
cihazda firmware kurulum/boot ve saha kabulü yapılmamıştır. Sanitizer veya
tam MISRA analizi bu adımda çalıştırılmamıştır. Incremental Release sonucu
clean/Debug derlemesi için kanıt sayılmaz.

Uzunlukla belirtilen payload penceresinde bütün baytlar ham veridir.
Modemin bu pencere içine gerçek URC eklememesi mevcut protokol varsayımıdır;
uygulama içerideki bildirimi veriden ayıramaz. Hedef modem kabulünde
SRING/NO CARRIER içeren binary verinin bütünlüğü ve yanıt öncesi/sonrası
gerçek bildirimlerin sırası doğrulanmalıdır. Cihaza yükleme, reset veya
commit yapılmamıştır.
