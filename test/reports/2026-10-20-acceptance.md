# 20.10.2026 cihaz ve host kabul kaydı

**Amaç:** Tam test koşusunu, ek kabul işlemlerini, düzeltilen hataları ve
kalan fiziksel sınırları tek kayıtta özetlemek.

**Kullanım yeri:** Bu kayıt test kapsamını gösterir; fiziksel ölçümün yerine
host sonucu kullanılmamalıdır. Ham kanıtlar çalışma makinesindeki
`.temp_files/full-test-20261020/` ve `.temp_files/closure-20261020/`
dizinlerindedir. Ham dizinler repoya eklenmemiştir.

## Kimlik ve yetki

Kaynak tabanı `9a9a1b52513267c29fd15b4abd198a8e50fed67a` ve kirli çalışma
ağacıdır. Önceden var olan kullanıcı değişiklikleri korunmuştur.
Firmware yükleme/reset, güvenli çıkışlarla test, geçici haberleşme ayarları,
RTC sync ve sentetik kayıt yazımı kullanıcı tarafından onaylanmıştır.
UART toparlanması başarısız olduğunda otomatik reset uygulanmaması kararı
korunmuştur. Harici watchdog'un HardFault sonrasındaki reseti ayrı yoldur.

RTU konsolu COM18/230400; RS485 COM10; AY konsolları COM15 ve COM16;
web/IEC104 hedefi 188.59.76.59'dur. Ek güç anahtarı, akım enjeksiyon
cihazı veya osiloskop/logic analyzer bulunmadığı kullanıcı tarafından
belirtilmiştir; mevcut düzenekle yapılabilen kontroller uygulanmıştır.

## Sonuçlar

| Kontrol | Sonuç | Kanıt |
|---|---|---|
| Tam Ceedling | 1571/1571, fail/ignore 0, 112 C test dosyası | `closure-20261020/host-unit-final.log`, `unit-inventory-final.md` |
| Merkezi integration | 14 giriş PASS | `host-integration-final.log`; hedef verilmeyen fiziksel wrapper'lar SKIP olarak ayrılır |
| Son eklenen NTP host gate'i | PASS | `iec104-host-final.log`; güncel merkezi IEC104 girişi ayrıca çalıştı |
| Coverage koşusu | 1571/1571 PASS | `coverage-final.log`, `gcov-inventory.md`; 75/124 dosyada 10861/14158 satır (%76,7), 6678/10023 dal (%66,6); 49 dosya ölçülmedi, bütün firmware oranı değildir |
| Release ve Debug -Og | PASS | `production-final-build.log`, `production-final-package.log`, `debug-final-build.log`; vendor/Contiki warning-free iddiası yok |
| Modbus üretim cihazı | Son tekrar 23 PASS/0 FAIL | `modbus-final-paced.log`; ADC bloğu dahil |
| Web üretim cihazı | PASS | `host-integration-final.log`; gerçek hedef, ADCValid/örnek yaşı ve StopBits geri yüklemesi |
| IEC104 ilk kapsamlı koşu | 13/13 PASS | `full-test-20261020/iec104-production-full.log`; o koşunun üretim CRC'si 1AFCFA00 |
| IEC104 güncel saat kabulü | PASS | `clock-iec104.log`; CP56 18:08:01.533 doğrudan uygulandı, konsol karşılaştırma penceresinde fark en çok 1 s |
| NTP güncel reset kabulü | PASS | `clock-ntp-reset/result.json`; UTC 18:16:31 + TimeZone 3 = 21:16:31; sonraki cihaz log damgasıyla uygulama doğrulandı |
| İlk yedi gerçek MH vakası | Farklı koşularda PASS | `full-test-20261020/real-mh-*`; ACK/201 kesin APPLIED değildir |
| İlk 47 HIL vakası | 42 PASS + düzeltme sonrası 5/5 PASS | `full-test-20261020/hil-final-matrix.json` |
| Yeni d5 halka taşması | PASS | `hil-wrap/manifest.json`; aynı HIL CRC 1029CF2B, önce/sonra kimlik doğrulandı |
| Modbus timeout / HAL dönüş enjeksiyonu | PASS | `device-fault/results-verified.json`; fiziksel DMA arızası değildir |
| Gerçek HardFault / watchdog / kalıcı elog | PASS | Aynı fiziksel koşu; kayıt register eşleşmesiyle doğrulandı |
| Gerçek MH üç üyeli APPLIED/CRC | AÇIK | Eksik canlı üçüncü üye; aşağıdaki deneyler |

HIL d5 üç çevrimde 229,9 saniyede tamamlandı. Toplam 366 kayıt üretildi.
Taşma sonrasındaki floor/total/ilk-son consume değerleri sırasıyla
23/122/27–122, 145/244/149–244 ve 267/366/271–366 oldu. Yeni tail öncesine
ve toplam sonrasına consume gönderilmedi; kalan son kayıt tüketildi.
Önceki 47 vaka ve bu yeni vaka farklı koşulardır; yeni tek bir 48-vaka
koşusu gibi gösterilmemelidir. Bu süre saha ölçeğinde soak değildir.

## Düzeltmeler ve regresyonlar

1. Debug -O0 FLASH sınırını 49580 B aşıyordu. Onaylanan -Og seçimi ile
   çözüldü; linker/bellek yerleşimi değişmedi.
2. Web StopBits testi hata yolunda ayar bırakabiliyordu. Özgün ID/baud/
   stop bits her çıkışta geri yüklenir ve GET ile doğrulanır.
3. MH runner sıralı log onayı bekler; reset öncesi konsol sınırı eski
   epoch kayıtlarını dışlar. Programmer 2.23 için UR seçeneği kullanılır.
4. HIL yeni ve tam LIVE örneğini mevcut ONLINE/OFFLINE/GECERSIZ alanlarıyla
   okur. IEC104 ön boşaltma bütçesi başlangıç kuyruk sayısı/ACK aralığından
   bir kez hesaplanır; takılan kuyrukta süre uzatılmaz.
5. NTP'de geçersiz ay/tarih offset hesabına girebiliyordu. Ay 13 iken
   gün tablosuna sınır dışı erişim mümkündü. Yeni test önce 41/42 sonucu
   verdi; mevcut `dt_init` doğrulaması hesabın önüne alınınca 42/42 geçti.
   UTC sapması ve IEC104 davranışı değiştirilmedi.
6. Gerçek MH için opt-in `m8_configuration` eklendi. Başarı yalnız RTU/MH
   APPLIED, üç WRITE, tam bitmap, reason 0 ve eşleşen CRC ile kabul edilir.
7. Yeniden üretilebilir geçici fault fixture derleyicisi ve fiziksel
   kanıt denetleyicileri eklendi. Test kancaları üretim kaynaklarında
   bırakılmaz; build yardımcısı kaynakları hata dahil `finally` ile geri alır.
8. Modbus host-pacing ölçümüne gerçek write-call aralıkları eklendi.
   İlk son koşuda T5 nedeniyle 22/23 oldu; ilk koşunun aralıkları ölçülmediği
   için kök neden kesin atanmadı. Tekrar 23/23; 1,5 ms hedefi gerçekte
   1,624–2,022 ms, 5 ms hedefi 5,489–5,636 ms oldu. İlk başarısız kayıt
   korunmuştur; bu sonuç tel üstü T1.5 güvencesi değildir.

İlk aşamada 18 Python regresyon yöntemi eklendi. Ek aşamada yedi C testi
ve 12 Python testi eklendi: fault 4, wrap 3, MH 2, NTP reset 3. Negatif
host senaryolarının beklenen FAIL çıktıları, unittest çıkış kodundan ayrı
okunmalıdır. Testler gerçek üretim girişlerini ve gözlenen sınırları kullanır.

## Fiziksel fault kanıtı

Gerçek UART4 DMA tamamlanma IRQ'su maskelenerek üretim process timer'ının
TX timeout'u çalıştırıldı. Enjekte HAL hatası olmayan timeout ve enjekte ilk abort hata dönüşü
sonrasında yeni FC03 yanıtı [3] oldu. DeInit hata dönüşü enjeksiyonunda
elog failed, sonraki okuma yanıtsız, UART busy/DMA reset durumunda kaldı;
sekiz saniyelik gözlemde otomatik MCU reseti olmadı. Fixture IRQ'yu geri
açar; başarısız transport'u yazılımdan READY yapmaz. Enjeksiyon gerçek HAL
yan etkilerinden sonra hata dönüşünü değiştirir; fiziksel DMA arızası değildir.

Gerçek `udf #0` fault'unda CFSR 00010000, HFSR 40000000, PC 0801CAF2,
LR 0801CAF3 görüldü. Watchdog sonrası kalıcı elog aynı dört değeri içerdi.
İlk açılış logu fault dump'ından yaklaşık 846 ms sonra görüldü; bu tek
koşu ölçümüdür, tüm donanım için zaman garantisi değildir. Reset cause PIN,
harici watchdog ile uyumlu gözlemdir; hat üzerinde reset darbesi ölçülmedi.

Fiziksel fault paketinin CRC'si 5D3D2A8D, boyutu 357556 B idi. Düzeltilen
bool fixture ve yeniden üretilebilir build yardımcısı ayrıca ARM derlemesiyle
kontrol edildi; bu ikinci fixture paketinin ayrı bir fiziksel koşusu yoktur.

## Gerçek MH ve fiziksel kaynak sınırı

Başlangıç eşlemesinde R=00124B0038C9F11A, S=00124B0038C9F1AE,
T=AABBCCDDEEFF0003'tür. COM16 gerçek AY kimliği 00124B0038C9F1BE, COM15
kimliği 00124B0038C9F1AE olarak okunmuştur.

İlk grup 250 denemesinde üç WRITE ve COMMIT ACK'i alındı; terminal rapor
FAILED, bitmap 04, reason 1, CRC 0000; beklenen CRC D07F oldu.
SCP R2 §4.9'a göre reason 1 NOT_LIVE, bitmap sorunlu üyeyi gösterir.
Bu, dummy üçüncü üyenin canlı olmamasıyla tutarlıdır; başarı sayılmaz.

COM16 kartının ayrı üçüncü kaynak olabileceği araştırıldı. Kart geçici
faz 3 yapıldı ve RTU T EUI'si gerçek F1BE ile eşlendi. L3 ONLINE olurken
eski L1 43956 ms yaşla OFFLINE oldu. Böylece COM16'nın mevcut L1 kaynağı
olduğu görüldü; üç ayrı canlı kart oluşmadı. Web Kaydet'in başlattığı grup
STAGED kaldı; bitmap 06 görüldü, sonra grup 1 FAILED/bitmap 01/reason 5
oldu. Bu sırada m8'in ayrı işlem başlatması mevcut meşgul korumasıyla
reddedildi; bu bir APPLIED denemesi başarısı değildir.

AY phase=1 ve RTU'nun özgün RF eşlemesi `finally` içinde geri alındı ve
GET/config ile doğrulandı. Ham kayıtlar `real-mh-three/` altındadır.
Tam APPLIED için üç ayrı gerçek AY ve doğru EUI/fider/faz eşlemesi gerekir.
L1'deki F11A/F1BE yapılandırma farkı kayda alınmıştır; test sonunda
başlangıç ayarlarını koruma kararı gereği kalıcı olarak değiştirilmemiştir.

## Saat sözleşmesi ve son durum

Kullanıcı kararı: IEC104 CP56 takvimi doğrudan; NTP UTC + web TimeZone.
Kaynaklar farklı takvimler verdiğinde kaynak geçişindeki saat farkı bu
sözleşmenin sonucudur; IEC104'e ikinci kez offset eklenmez. Tüm kaynaklar
ortak UTC saklıyor iddiası yoktur. NTP açılış senkronu normal saati geri aldı.

Son üretim paketi CRC 9440E6B2, profil 0, 356844 B'dır. EFW SHA256:
`5186b9029de46f7cf957395de0478e00caad577dfa9a2de085304bfc20a7eeb6`.
Üretim ELF'inde fixture/HAL wrapper simgeleri yok; cihaz `hwtest` komutunu
tanımıyor. Firmware approved YES doğrulandı. Son log seviyeleri cslog ON,
RF NORMAL, GSM VERBOSE, IEC104 NORMAL. Mevcut kayıtlar topluca silinmedi.

Son RF/IEC104/Modbus GET karşılaştırmasının üçü de başlangıçla eşleşti.
`restore-verification.json` / `device-final.json` son okumada RLY=[0,0],
ADCValid=1 ve TimeZone=3 değerlerini içerir. AY phase=1 ayrıca config
çıktısıyla doğrulandı.

## Açık kalan işler

- Üç gerçek AY ile tam APPLIED/bitmap/CRC ve gerçek kartlarda kalıcı ayar
  doğrulaması; mevcut eşleme farkının işletme kararına göre düzeltilmesi.
- Kontrollü fiziksel güç kesintisi, gerçek arıza akımı ve fiziksel DMA
  arızası enjeksiyonu; ilave düzenek gerekir.
- T1.5/echo'nun tel üzerindeki ölçümü; logic analyzer/osiloskop gerekir.
- Saha süresinde uzun soak; bu rapordaki 229,9 saniyelik test yerine geçmez.

Bu sınırlar kapatılmış gibi gösterilmemelidir. Çalıştırılan son host
regresyonları ve ekipmanla tamamlanan kabul sonuçları kendi kapsamlarında
geçerlidir.
