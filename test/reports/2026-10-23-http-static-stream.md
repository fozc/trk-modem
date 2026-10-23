# Küçük HTTP kuyruğu ve Flash gövde gönderimi

**Tarih:** 23.10.2026
**Sürüm:** 1.0

## Amaç

Kullanıcının onayladığı [küçük tampon tasarımının](../../doc/HTTP_KUCUK_TAMPON_TASARIMI.md)
uygulamasını ve doğrulama sınırlarını kaydeder.

## Kullanım yeri ve uygulama

GSM HTTP kuyruğu 48 KiB'dan 10 KiB'a indirildi. JSON ve RFWU yanıtları
normal `send()` callback'iyle bu kuyruğa kopyalanır. Dinamik HTTP gövde
üst sınırı 8192 bayttır; header için 512 bayt pay korunur.

Sabit HTML/gzip kaynakları yeni `send_static()` sözleşmesini kullanır.
Header ring'e kopyalanır; gövde için yalnız Flash işaretçisi ve kalan
uzunluk tutulur. Yeni flag, heap, kuyruk veya zamanlayıcı eklenmedi.
GSM hazır olduğunda önce ring, ardından Flash gövdesi en fazla 1500 baytlık
parçalarla gönderilir. Modem kabul etmeden ring/kaynak konumu ilerlemez.

Mevcut `rbuff_get_read_block()` / `rbuff_skip()` yolu kullanıldı. GSM
gönderimi zaten kendi TX tamponuna kopyaladığı için eski 1500 bayt ara
tampon kaldırıldı. Sıfır kopyalı modem/UART gönderimi iddia edilmez.

Ring ve gövde bitmeden veya son modem işlemi tamamlanmadan yanıt bitmiş
sayılmaz. Reset/kapanış gövde konumunu temizler. Bekleyen HTTP RX parçası
gönderim sonrasına bırakılır; üzerine ek parça gelirse bağlantı yanıt
bitiminde kapatılır. Gövdeler birbirine karışmaz. Bu sınırlı bekletme tam
HTTP pipelining desteği değildir. RFWU mevcut kopyalama yolunu kullanır.

Header gönderimi tam kabul edilmediyse gövde başlatılmaz. Statik kaynak
callback'i kaydedilmemiş copy-only response kullanıcılarının eski davranışı
korunmuştur; gerçek GSM/web IO yolu yeni callback'i kaydeder.

## Bellek ölçümü

| Alan | Önce | Sonra |
|---|---:|---:|
| HTTP ring | 49.152 | 10.240 |
| Eski ara tampon | 1.500 | 0 |
| GSM HTTP durum yapısı | 2.056 | 2.064 |
| Response callback durumu | 4 | 8 |
| Web IO callback durumu | 4 | 8 |
| Toplam `.bss` | 161.880 | 121.488 |
| Toplam `.data` | 516 | 516 |

ARM linker map karşılaştırması net **40.392 bayt (~39,4 KiB)** RAM
azalması gösterir. Bölüm hizalaması nedeniyle tekil simge toplamıyla
birkaç bayt fark olabilir. Önceki map aynı çalışma ağacının bu değişiklik
öncesi Release çıktısıdır; başka agent özeti kullanılmamıştır.

## Doğrulama

| Kanıt | Sonuç |
|---|---|
| Merkezi Ceedling HTTP response | **54 PASS**, 0 fail/ignore; statik callback, header reddi ve JSON kopyalama dahil |
| Web auth/transport integration | Geçti; gerçek web_server, http_server, http_response, gsm_http_server, parser, ring ve SHA kaynakları birlikte derlendi |
| Statik aktarım | 1/1499/1500/1501/3000/102401 bayt kaynaklar, reddedilen gönderim ve son modem tamamlanması byte byte doğrulandı |
| Dinamik aktarım | 8192 bayt JSON kuyruğa alındıktan sonra kaynak tampon değiştirildi; eski doğru veri gönderildi |
| Hata/sıralama | Ring wrap, modem meşgul/reddi, header hatası, reset/kapanış, ikinci istek ve RX taşmasında gövde ayrımı geçti |
| Gerçek web kaynakları | Gömülü sayfa ve gzip paket endpoint'i, cache/ETag/304 ve kimlik doğrulama geçti |
| RFWU | Üretim demultiplexer/kopya yanıt taşıması geçti; ayrı RFWU auth paketi **33 test** geçti |
| Web navigation | Tam paket geçti; audit 42, profile 40 kontrol |
| ARM Release | Geçti; yeni boyut assert'i ve imzalı EFW self-check geçti |

Eski “HTML tamamen ring'e sığmalı” testi güncellendi. Artık dinamik
yanıtın ring'e sığması ve statik kaynak boyut makrolarının gerçek gzip
verisiyle eşleşmesi sınanır. Kuyruktan büyük statik gövdenin doğruluğu
üretim transport integration testinin sorumluluğundadır.

## Kalan sınırlar

Cihaza firmware yüklenmedi. Fiziksel GSM/UART aktarımı, gerçek modem AT
hata zamanlaması, bağlantı kesme ölçümü ve tarayıcı üzerinden donanım kabulü
yapılmadı. Modem sınırı testte kabul/red/tamamlanma davranışıyla taklit
edildi; TCP teslim garantisi veya fiziksel süre garantisi iddia edilmez.
Mevcut listener hata/toparlanma yolu korunur; yeni retry/reset eklenmedi.
Önceki açık Modbus/RF bulguları bu değişiklikle kapatılmadı. Kullanıcı talimatıyla değişiklikler commit edilmektedir; push yapılmadı.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 23.10.2026 | 1.0 | Onaylanan 10 KiB kuyruk ve statik kaynak akışının uygulanması |
