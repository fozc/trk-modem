# RF Hub (MH) simülatörü ve HIL test planı

**Tarih:** 06.10.2026  
**Sürüm:** 1.2  
**Durum:** Kaynak incelemesiyle güncellenmiş taslak. Çalışma ağacındaki eşzamanlı HIL geliştirmeleri ayrıca doğrulanmalıdır; bu incelemede cihaz koşusu yapılmadı.

## 1. Amaç, kapsam ve kullanım yeri

PC üzerinde MH rolünü oynayan bir simülatör ile STM32 modem firmware'inin
RTU tarafını test etmek. İki kullanım vardır: otomatik senaryo koşusu ve
günlük sanal hub kullanımı. HIL (Hardware-in-the-Loop, gerçek cihazla
kapalı döngü test) STM32'nin gerçek UART/SCP ve uygulama yollarını çalıştırır.
Donanımsız simülatör testi ise yalnız PC modelini doğrular.

Uyum hedefi belgelenmiş **RTU–MH UART sözleşmesidir**. Simülatör; fiziksel
LoRa haberleşmesi, ayırıcı ölçümü/açması veya Powerboard iç yazılımının
birebir yerine geçmez. Bu testlerin geçmesi fiziksel RF/koruma kabulü değildir.

İlk faz RF hattı, konsol ve mevcut shell üzerinden yapılır. Web HTTP,
Modbus master ve IEC104 SCADA üzerinden uçtan uca doğrulama ikinci fazdır.
Shell `rf cfg-apply` grup servisinin yolunu sınar; HTTP yetki, form ve
Kaydet/Uygula yollarının sınandığı anlamına gelmez.

### 1.1 Kaynaklar ve ölçüt

- [BOLATeX Teslim4 R1](doc/BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/MH_STM32_RTU_SCP_Arayuzu_R1.md): MH'nin belgelenmiş sözleşmesi.
- [Örnek akışlar](doc/BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/Ornek_SCP_Akislari/BENIOKU.md): golden vector (beklenen sabit bayt dizisi).
- [RTU uygulama planı](doc/RF_SCP_MODEM_UYGULAMA_PLANI.md) ve [BOLATeX soruları](doc/BOLATEXE_SORULACAKLAR.md): kullanıcı kararları ve açıklama bekleyen sınırlar.
- Firmware kaynakları: mevcut RTU'nun gerçek davranışı; protokolü tek başına yeniden tanımlamaz. İnceleme tabanı `f352bed` commit'idir; §4.1 yerel, henüz commit edilmemiş HIL transport değişikliğini ayrıca kaydeder.
- [Donanım rehberi](doc/HW_TEST_VE_DEBUG_REHBERI.md) ve [merkezi test rehberi](test/README.md).

Metin ile CSV akışı çelişirse normatif tablo/kural esas alınır; çelişki
ayrıca raporlanır. Simulator ile firmware'in aynı yanlış varsayımı paylaşması
uyum kanıtı sayılmamalıdır. Açık BQ konuları testte sessizce karara bağlanmaz.

## 2. Terimler

| Terim | Anlam |
|---|---|
| DUT | Test edilen STM32 modem ve çalışan firmware |
| MH | RF hub; bu testte PC simülatörü |
| AY / PWRB | Simüle edilen ayırıcı / güç kartı |
| Harness | Senaryoyu yürüten, kanıt toplayan test sürücüsü |
| Oracle | Beklenen sonuç için bağımsız doğrulama kaynağı |
| Fault injection | Hata enjeksiyonu; normal profilden ayrı işaretlenir |
| Trace | Zamanlı ham bayt ve olay kaydı |
| Quick / soak | Kısa kontrol takımı / uzun süreli kontrol takımı |

## 3. Kanıtla doğrulanmış mevcut durum

USART3, `Core/Src/main.c` içinde PC4=TX, PC5=RX ve 230400 8N1 olarak
kuruludur. Modbus UART4/RS-485 üzerindedir. RF için COM numarası değil,
adaptörün hangi fiziksel hatta bağlı olduğu belirleyicidir. COM10'un
adaptör türü bu incelemede fiziksel olarak doğrulanmadı.

Mevcut `tools/rf-hub-sim` eski bir manuel C aracıdır; güncel uyum oracle'ı
olarak kullanılmamalıdır. Kaynakta doğrulanan sapmalar:

| Alan | Mevcut C simülatörü | Güncel beklenti |
|---|---|---|
| LOG_HEAD | Varsayılan eski 8 B; `--r1` yalnız bu yanıtı değiştirir | 10 B ve tail alanı |
| LIVE_DATA | `src=1` gönderir | Atanmış fider/faz: `(fider << 2) | phase`; normal ilk faz için 5 |
| TRIP_NOTIFY | event=4, timestamp=250 | Bu sürümde event=1; timestamp alanı kullanılmaz, 0 |
| CFG_READ_ALL | Taze cihaz bloğu döndürebilir | Bu sürümde daima ERROR 05 |
| Kapasite | 16 cihaz | Envanter 21 girdi; RF hizmeti fider 1–4 |
| Epoch | 5 s hızlandırılmış davranış | RTU'da ACK sonrası fider başına 30 s bekleme |
| Olay halkası | Wrap karşılaştırmasında basitleştirme | Wrap/overwrite/tail ve tam halka durumları ayrı doğrulanır |
| Powerboard | E1–E8 ailesi yok | §7'deki PWRB modeli gerekir |

Eski araç/self-test kaldırılmaz. README'sine daha sonra güncel kapsam
uyarısı eklenebilir; henüz bulunmayan yeni araca “hazırdır” denmez.
Donanımsız `make -C tools/rf-hub-sim test` 74/74 geçti; kanıt
`build/rf-hil-plan-legacy-selftest.log`. Eski modelin kendi testlerinin
geçmesi, yukarıdaki uyumsuzlukları gidermiş veya güncel RTU uyumunu
kanıtlamış sayılmaz.

Firmware'de PWRB kontrolü artık vardır: `power_board_control.c` E5 GET/SET,
E6 akü değişti/iptal, E7 sonuç GET ve E8 ham GET yollarını içerir. Web
Kaydet/Uygula ayrıdır. Modbus anlık akım/RF ve IEC104 anlık akım/RF artık
SCP cache'ini kullanır; tüm ölçüm alanlarının dummy olduğu iddiası yanlıştır.
Kalan enerji/nominal-yük/arıza göstergelerinin kaynağı ayrı değerlendirilir.

## 4. Kurallar ve bench ön koşulları

```
STM32 USART3_TX (PC4)  ->  USB-UART RX
STM32 USART3_RX (PC5)  <-  USB-UART TX
GND                   <-> USB-UART GND
```

- Adaptör, kart şemasıyla doğrulanan UART gerilim seviyesine uygun olmalıdır. USB-TTL 3.3 V beklenir; gerçek adaptör seviyesi ve kart bağlantı noktası bench'te ölçülüp kaydedilmelidir. RS-485 A/B veya RS-232 doğrudan bu hatta bağlanmamalıdır.
- PC simülatörü bağlanmadan gerçek MH'nin UART TX/RX bağlantısı uygun fixture ile ayrılmalıdır. İki TX sürücüsü aynı hattı sürmemelidir. Reset'in MH TX'i yüksek empedansa aldığı şema/üretici kanıtı olmadan varsayılmamalıdır.
- Ortak GND, güç kaynağı, pin erişimi ve DTR/RTS bağlantıları kaydedilmelidir. DTR/RTS güç beslemesi değildir; reset hattına etkisi bilinmeden otomatik açılıp kapanmamalıdır.
- RF portu `--rf-port <RF_COM>`, konsol portu `--console <CONSOLE_COM>` ile verilmelidir. Portlar farklı ve tek sahibi olan bağlantılar olmalıdır. COM16/230400 konsol mevcut bench varsayılanıdır; port kimliği her koşuda doğrulanmalıdır.
- C seri sürücüsü zaten `\\.\COMx` yolunu kullanır; COM10 desteğinin yalnız Python'da olduğu iddiası doğru değildir.
- **Normal RTU HIL sırasında `rf-bridge` kapalı olmalıdır.** Köprü açıkken normal SCP gönderimi/alımı devre dışı kalır ve LPUART1 saydam hatta dönüşür. Köprü üzerinden sanal hub bağlamak, normal RTU RF uygulamasını test etmez.
- RF parser testinde konsol metni ayrı RF hattına enjekte edilir. Test sürücüsünün kendi stdout/log metni RF seri hattına karıştırılmamalıdır.
- RTC, NVRAM envanteri, üç EUI, aynı bölge, kullanılan group_id ve test öncesi kayıt sayıları fixture olarak kaydedilmelidir. `rf inv` görüntüleme komutudur; envanteri kendiliğinden atamaz. DISCOVERY otomatik atama demek değildir.
- Teste uygun firmware'in gerçekten çalıştığı doğrulanmalıdır. Açılış Git kimliği boot superblock'tan gelebilir; yalnız çalışma ağacının HEAD'i veya derlenmiş dosya adı kanıt değildir.
- Bu belgeyi düzenleme talebi cihaz reseti, firmware yükleme, NVRAM değiştirme veya bench koşusu yetkisi değildir. Sonraki açık HIL çalıştırma yetkisi kapsamında olan adımlar için tekrar tekrar onay sorulmaz; mevcut yetki kapsamı korunur.

Cihaz güncellemesi gerekiyorsa mevcut donanım rehberindeki imzalı EFW/BSL
akışı kullanılır. Üretim USART3 planı firmware değişikliği gerektirmez; §4.1 alternatifinde
ayrı HIL firmware/build gereksinimi vardır. Vendor kaynaklarına yama
planlanmaz.

### 4.1 Yerel çalışma ağacında görülen alternatif bench modu

İnceleme sırasında başka bir çalışma `Application/rf/rf_hil_transport.h`
ve ilgili firmware değişikliklerini ekledi. Bu inceleme o kodu değiştirmedi
ve HIL build'inin doğru çalıştığını kabul testiyle doğrulamadı.

| Profil | RF/SCP hattı | Koşu kapsamı |
|---|---|---|
| Üretim, `RF_SCP_OVER_MODBUS_PORT=0` | USART3 PC4/PC5, 230400 8N1 | §4 bağlantısı; UART4 Modbus olarak kalır |
| Yerel HIL build, `RF_SCP_OVER_MODBUS_PORT=1` | UART4/RS-485, kodda 230400 olarak ayarlanır | `app_main` Modbus process'ini başlatmaz; RF TX/RX UART4'e yönlenir, USART3 RF dispatch'i devre dışıdır |

COM10 gerçekten RS-485'e bağlıysa ikinci profil bir yazılım/firmware
alternatifidir; bu nedenle “yazılımla hiçbir şekilde çözülemez” iddiası
geçerli değildir. Aynı üretim binary'siyle yalnız kablo/baud değiştirmek
ise yeterli olmaz. Profil seçimi, build flag'i ve çalışan imajın kanıtı
manifestte zorunludur; iki profilin sonuçları birleştirilmemelidir.

İkinci profil yarı çift yönlüdür. PC'nin proaktif TX'i ile DUT isteğinin
çakışması, DE/RE dönüş süresi ve yerel echo ayrı ele alınmalıdır. Planlı
bus-turn/idle guard (hat sahipliği ve boşluk koruması) ile model TX sırası
kaydedilir; guard tek başına çakışma olmadığını kanıtlamaz. Echo ham trace'te
DUT isteği diye sayılmamalıdır. Yarı çift yönlü hata, tam çift yönlü USART3
protokol hatası diye raporlanmamalıdır.

Bu profilde aynı UART4 üzerinden Modbus master testi yapılamaz. İkinci faz
Modbus kabulü için üretim transport build'ine dönülür. HIL imajının yükleme
ve üretim imajına geri dönüş adımları donanım rehberine/yetkiye bağlıdır;
plan değişikliği bu işlemleri çalıştırmaz. Üretim profili pin/IRQ/TX zamanlama
kabulü, yalnız RS-485 HIL koşusuyla tamamlandı sayılmaz.

## 5. Mimari öneri ve dosya yerleşimi

Yeni Python 3 simülatörü için stdlib + pyserial önerilir. Gerekçe senaryo,
CSV ve rapor araçlarıyla bütünleşmedir; “C ile yapılamaz” iddiası değildir.
Uygulama başlamadan bu öneri kabul edilmelidir. Başlangıçta tek model,
tek RF port sahibi ve tek zamanlayıcı yeterlidir; ayrı süreç/thread veya
fazladan abstraction (soyutlama) yalnız somut ihtiyaç varsa eklenir.

### 5.1 Konsol erişimi

`tools/hwtest/serial_io.py` stdin'in tamamını başlangıçta okur, komutları
0.4 s arayla gönderir, sonra yalnız kayıt alır. Komutların cevaplarını
kayıt açılmadan önce gönderdiği ve açılışta input buffer'ı temizlediği için
zaman çizelgesiyle senkron etkileşimli harness yerine doğrudan kullanılamaz.

Öneri, bu ortak helper'a mevcut `capture` davranışını bozmayan etkileşimli
bir mod/API eklemektir. Harness'teki `console.py` ince bir adaptör olabilir;
ayrı ikinci seri konsol sürücüsü kopyalanmamalıdır. v1.1'deki bağımsız
sürücü önerisi, [troika-hwtest skill](.agents/skills/troika-hwtest/SKILL.md)
“yeni console helper yazma” kuralıyla birlikte değerlendirilmelidir;
istisna seçilirse kullanıcı kararı açıkça kaydedilmelidir.

Tek oturum hem komut hem ham kayıt sahibi olur. CR satır sonu, login/rol,
komut başına deadline (son bekleme süresi), çıktı eşlemesi ve port kapatma
hata yolları test edilir. Sürekli arka plan logları shell cevaplarıyla
karıştırılmaz. Kayıt açılmadan komut gönderilmez; senaryolar arasında
input buffer'ın temizlenmesi kanıtı kaybetmemelidir.

### 5.2 Önerilen bileşenler

| Yer | İçerik |
|---|---|
| `tools/rf-hil/rf_hub_sim.py` | Günlük/HIL modu; tek CLI biçimi: `--rf-port`, `--profile`, `--scenario`, isteğe bağlı `--control` |
| `sim/scp_codec.py` | Bağımsız framing/COBS/CRC; kısmi ve art arda çerçeveler, sınırlar |
| `sim/hub_model.py` | §6–7 durumları; PHY/RF/PWRB varsayımlarından ayrılmış MH modeli |
| `sim/scenario.py` | Deterministik olay sırası, fault injection ve JSONL trace; başlangıçta ayrı küçük dosyalar zorunlu değildir |
| `sim/control.py` | İsteğe bağlı localhost JSON-lines kontrolü; yalnız 127.0.0.1, tek model sahibi |
| `test/integration/rf_hil/` | Donanımsız unittest ve codec/model testleri; merkezi host koşusuna bağlanır |
| `test/system/rf_hil/` | Gerçek cihaz koşusu: `run_hil.py`, konsol adaptörü, kontroller ve JSON senaryoları |
| `test/build/hil-rf/<run_id>/` | Manifest, trace, konsol baytları, rapor ve model snapshots |

Donanımsız self-testler `test/system` altında varsayılan cihaz gerektiren
koşuyla karıştırılmaz. Normal host koşusu COM açmaz. İnceleme sonunda `tools/rf-hil/` ve `test/system/rf_hil/` altında yerel
dosyalar görüldü. Bu tablo onların kabul edilmiş/eksiksiz uygulaması
olduğu anlamına gelmez; yeni dosyalar bu incelemede çalıştırılmadı.
Mevcut `test_sim_selftest.py` system dizinindeyse donanımsız host kapısına
taşınması veya açıkça bağlanması hâlâ uygulanacak bir düzenlemedir.

## 6. MH modeli ve ortak protokol kuralları

Tel sözleşmesinin ölçütü R1 §2–6'dır; payload tabloları burada yeniden
kopyalanmaz. Temel codec kapısı: 7 B başlık, en çok 244 B DATA, CRC dahil
253 B mantıksal paket ve en çok 256 B fiziksel frame (çerçeve). CRC
CCITT-FALSE, başlangıç FFFF, telde low byte önce; DATA endian istisnaları
E5 CFG2 bayt 11–12 ve E8 ham bloktur.

Model aşağıdaki ayrımları korumalıdır:

| Konu | Beklenti |
|---|---|
| Komut haritası | Aralık listesinden türetilmez; yalnız R1 komut tablosundaki CMD/TYPE çiftleri vardır. `01` GET_STATUS'tur, ayrılmış komut değildir. PING TYPE=05/CMD=00'dır |
| Yön/adres | MH=01, RTU=02. MH isteğine bilinmeyen CMD/TYPE ERROR 01; DUT'a bilinmeyen bildirim göndermek aynı şey değildir, RTU'nun MH gibi ERROR üretmesi beklenmez |
| Broadcast | Genel kural yanıtsız işleme; E5–E8 yalnız RTU kaynağı ve broadcast olmayan istekte kabul edilir. SRC=00/FF atılır; yanlış DST/SRC ayrı sınanır |
| Tekrar | GET/PING taze işlenir. SET replay cache'i SRC+SEQ ve araya başka istek girmemesi koşulunu taşır; tüm istek baytları izlenir |
| ERROR/timeout | Timeout'ta aynı istek/SEQ; izinli geçici ERROR'da yeni SEQ ve ortak sınırlı retry bütçesi. `46 ERROR 03` MH replay cache'ine alınmaz |
| Geç cevap | 700 ms'lik ilk GET ACK'i, 500 ms'de aynı SEQ ile tekrar başlamışsa hâlâ geçerli olabilir. Tamamlanmış/restart ile iptal olmuş veya başka SEQ'li işin ACK'i ayrı negatif senaryodur |
| Boot/envanter | İlk BOOT yaklaşık 1 s; artan aralık üst sınırı 30 s. İlk geçerli 04/06 tekrarları durdurur; 10 s içinde yeni girdi/END yoksa tekrar başlar. Boş END ACK alır ama loaded olmaz ve BOOT sürer |
| Envanter | 21 girdi, tek bölge; fider 1–7 kabul, RF hizmeti 1–4. Aynı EUI güncellenir; aynı fider/fazda son EUI geçerli. Silme 06/fider=0'dır |
| LOG_AVAILABLE | Pending 0→pozitif olduğunda bildirim; bekleyen kayıt varken 60 s tekrar ve left=0 sonrası susma ayrı sınanır |
| Saat | IV=1 veya aralık dışı TIME_SYNC → ERROR 02. Normal RTU geçersiz RTC'de TIME_SYNC göndermez; envantere devam eder, sonra geçerli saatle eşitler |
| CFG_READ_ALL | Bu sürümde daima ERROR 05; başarılı 96 B okuma normal profilde sunulmaz |
| Grup | Önce kullanılmamış kimlik kontrolü; yeni kimlik 28'de genellikle ERROR 02 ile ayrılır. WRITE ×3 aynı 96 B bloğu taşır; her yeni istek yeni SEQ alır |
| Sonuç | ACK≠APPLIED. APPLIED için doğru group_id, bitmap=07 ve cfg_crc eşleşir. Bitmap bitleri kabul edilen WRITE sırasıdır; envanter indeksinin bitleri değildir |
| PARTIAL_COMMIT | FAILED sebep 6'da bitmap biti 1 uygulama onayı alınmayan üyeyi gösterir; APPLIED bitmap'i gibi yorumlanmaz. Olay 122 eşlemesi BQ-06 bekler |
| cfg_crc | Yazılabilir 22 alan, blok baytları 3..56 (54 B) üzerinde CCITT-FALSE. Tam blok CRC'si veya 94..95 alanı değildir. Maskeli alanlar 0..2 ve 57..67; rezerv 68..93 ve yazmada 94..95 sıfır |
| Halka | 100 yuva; head/tail mod 100, pending en çok 100, total u32, wrap u16. 99→0, çoklu wrap, overwrite ve 32/16 bit sayaç sarması ayrı sınanır |
| head=tail | RTU kabulü boş; eşleşen LOG_AVAILABLE pending=100 dolu halkayı ayırır. Bildirim kaybıyla ayrım BQ-02 açıktır; ücretsiz yuva sayısından kesin doluluk türetilmez |
| FRAM | free_slots yaklaşık; degraded=1 iken ilk altı bayt karar verisi değildir. Busy ile bozulmuş kip farklı profillerdir |
| Reset | MH reboot, RTU reset ve MH kart değişimi ayrı aksiyonlardır. MH reboot olay deposu/tail ve CFG2'yi korur; saat/envanter/alarmlar gibi volatile durumları yeniler. MH reboot PWRB boot-session değişimi değildir |

### 6.1 Kaynak verisi ve belirsizlikler

Normal LIVE/TRIP/anomali paketleri atanmış fider/faza ait olmalıdır.
TRIP event=1 taşır; ayrıntılı arıza büyüklüğü/32 bit süre/zaman 60 B olay
kaydından gelir. TRIP timestamp alanı kullanılmaz; TRIP LIVE tazeliğini
yenilemez. Anomali global RESET `src=FF,state=2`dir.

Simülatör 60 B olay iç CRC'sini dış SCP CRC'sinden ayrı üretir/bozar.
ERR06 ile yuva atlama ve CRC'si bozuk ACK gövdesindeki kaydı atlama aynı
politika değildir. Bozuk iç CRC'de başarılı prefix (ön kayıtlar) ötesine
consume gönderilmemelidir; ERR06'daki tek yuva atlaması ayrı testtir.
Başarılı saklama/telafi sonrası 46 için ACK eşleşmesi ve tail wrap doğrulanır.
Reset/kayıp consume sonucu tekrar kayıt mümkün olduğundan kalıcı exactly-once
(tam bir kez kayıt) şartı uydurulmamalıdır. Olay 1/7 kalıcı, 3 geçici
mevcut kullanıcı kabulüdür; BQ-10 üretici sınıflaması hâlâ açıktır.

BQ-01/02/06/07/08/09/10/11/12/13/14/15 gibi açıklama bekleyen konular
simülatör varsayımıyla kapatılmaz. Bir varsayım profili gerekiyorsa adı ve
raporu `ASSUMPTION` taşır; normatif PASS sayılmaz.

## 7. Powerboard modeli

| Konu | Normal profil ve negatif test |
|---|---|
| E1/E3 | 39/11 B, doğru sürüm, signed değerler, 32 bit alarm maskesi. E1 RTU'ya 10 s; PWRB'nin MH'ye 1–10 s telemetri ayarı farklıdır. RTU kalite eskimesi 30 s kullanıcı tercihidir |
| Son nefes | Aynı PWRB session/neden tekrarları, tahmin→gerçek neden güncellemesi, iptal, PWRB restart ve MH-only restart ayrı sınanır; ham sayaç benzersiz kesinti sayısı sayılmaz |
| E5 | GET 23 B → yalnız bit 9/13/14 maskeli SET 16 B → ACK yeni GEN. İlk ayarda gerçek kapasite; b7 korunur. GET tarihsel c2_red ile SET ret maskesi ayrı tutulur |
| Yankı | `max(20 s, 2 × periyot + 12 s)` bekleme; geçerli yankı ve güncel GEN, m1 & DF=0, m2 & 7F=0. Kapasite/oran için ayrıca geçerli E1 durum2 ve değer eşleşmesi gerekir |
| Çatışma | GEN uyuşmazlığı eski SET'in kör tekrarına dönmez; yeniden deneme yeni GET ile başlar. m1 b5 / m2 b7 ret değildir |
| Ayarsız ayar | Kapasite/oran 0/255 için yalnız ECHO_ACCEPTED; APPLIED beklenmez (BQ-12). Ayarsız periyot kontrol servisinde kapalıdır (BQ-14) |
| E6/E7 | Ürün komutu 05/A5, iptal 00. ACK yalnız kabul/SIRA'dır; erken bildirim, yanlış/eski SIRA, GET telafisi, iptal b1 ve geç b2/b3 sonuçları ayrı sınanır |
| FF sonucu | FF/yayın≥1/b1-b3=0 hem bekleme hem yanıtsız bitişle çakışabilir (BQ-13). Otomatik yeni 05 veya uydurma bitiş timeout'ı beklenmez |
| CSV PARAM | PWRB_04 örneği bilerek 05/00 kullanır ve sonuç 02'dir. Ürün `battery-replaced` komutu 05/A5 gönderir; normal sonuç 00 örnek retle karıştırılmaz |
| E8 | GET/istenmeden SET 96 B; ham korunur, alan düzeni bu belgede tanımlanmaz. BQ-08 nedeniyle BE alanlar uydurulmaz; verilen corpus'ta E8 örneği yoksa sentetik vektör diye etiketlenir |

Konsol yolları: `pwrboard show`, `alarms`, `control`, `cfg-read`,
`cfg-set <capacity|rate|period> <value>`, `battery-replaced`, `cancel`,
`result-get`, `raw-get`, `raw`. Hepsi bir komutun alt argümanlarıdır.
`cfg-set` bir ayarı değiştirir; başlaması uygulanmış sonucu değildir.

## 8. Zamanlama ve hata enjeksiyonu

| Ölçüt | Mevcut RTU / MH beklentisi |
|---|---|
| Kısa GET, envanter, saat, consume | 500 ms; 3 ek deneme, toplam en çok 4 gönderim |
| 44 aralık okuma | 500 ms; 2 ek deneme, toplam en çok 3 gönderim |
| WRITE/COMMIT/ABORT, E5 SET/E6 | 1000 ms; 3 ek deneme |
| Epoch | 1000 ms; 1 ek deneme; ACK sonrası ilgili fiderde 30 s bekleme |
| RTU status / LOG_HEAD / TIME_SYNC | 10 s / 60 s / 3600 s, hat boşken gönderilir; mutlak zaman çizelgesi sapması nedenleri raporlanır |
| LIVE / link eskimesi | Normalde cihaz başına 5 s, olay aktarırken 10 s / 30 s |
| PWR özeti / RTU kalite eskimesi | 10 s / 30 s |
| MH grup üst süreleri | STAGED→DELIVERED yaklaşık 60 s; DELIVERED→APPLIED yaklaşık 120 s. Bunlar RTU'nun yeni otomatik deadline'ı değildir |
| Parser boşluğu | RTU kodunda elapsed >=100 ms; sınır self-testleri 99/100/101 ms |

HIL'de DUT saati değiştirilmez; model/harness `monotonic` saat kullanır.
Donanımsız test sanal saatle hızlı yürür. HIL profili, modelin saatini
hızlandırıp gerçek RTU timer'ını hızlandırmış gibi sonuç üretmez. Erken
FAILED enjeksiyonu yalnız hata işleme testidir; gerçek 60/120 s sınır testi
diğerinden ayrı raporlanır. Quick süre tahmini koşulmadan garanti edilmez;
60/120/3600 s testleri ayrı soak planındadır, E5 en az bir 20–32 s pozitif
doğrulama quick takımında da yer alır.

UART→USB tamponlama ve Windows zamanlaması nedeniyle PC write/read zamanı
DUT parser zamanı değildir. RTU ring'i process bağlamında boşaltılır;
100 ms arayla gönderilmiş iki parça aynı poll'de işlenirse fiziksel boşluk
aynı şekilde gözlenmeyebilir. HIL yakın sınır testinde gerçek gönderim
zamanı, poll koşulu ve jitter (sapma) kaydedilir; mümkünse mantık analizörü
kullanılır. Kesin 99/100/101 ms parser kanıtı sanal saatli host testindedir.
HIL'de küçük pozitif boşluk ve geniş negatif boşluk, ardından sağlam
çerçeveyle resync (yeniden hizalanma) ölçülür. Karşılanmayan zaman koşulu
altyapı sorunu olarak ayrılır; yanlış firmware PASS/FAIL üretilmez.

Fault injection iki ayrı olay taşımalıdır: **istek işlenmeden düşürme**
ve **işlenip yalnız yanıtın kaybolması**. İkincisinde tekrar SET'in yan
etkisi ikinci kez oluşmamalıdır. Üç WRITE arasında bildirim, ERROR sonrası
yeni SEQ, SEQ FF→00, üretici F0–FF bandının yok sayılması ve büyük/burst
trafik ayrı sınanır. RF RX ring'i 1024 B'dir; aşırı yük testi normal profille
karıştırılmaz. Seri kısa write, bağlantı kopması ve trace kaybı başarısız
altyapı olarak görünür olmalı; wire'a eksik gönderim başarı sayılmamalıdır.

## 9. Senaryo kataloğu ve kabul ölçütü

Her senaryo başlangıç fixture'ı, tetik, beklenen DUT ve model davranışı,
gözlem kaynağı, zaman bütçesi, cleanup (sonlandırma) ve açık BQ bağımlılığını
belirtir. Modelin kendine ürettiği cevabı kontrol etmesi tek başına DUT testi değildir.

| Grup | Zorunlu kapsam / ayırt edici kabul |
|---|---|
| A Açılış | Geçerli ve geçersiz RTC; geçersizde TIME_SYNC yok, envanter devam eder; geç saatte sync envanteri yeniden başlatmaz. Major uyuşmazlığı, boş/kısmi/tam envanter, BOOT tekrar durma/yeniden başlama, işlem sırasında MH reboot |
| B Atama | DISCOVERY tek başına otomatik atama başlatmaz; operatör ataması ve ACK mirror ayrı. Aynı EUI güncelleme, aynı fider/faz değişimi, 21 sınırı, bölge çatışması, silme. MH kabul sınırları model self-testinde; shell'in reddettiği istek DUT'tan beklenmez |
| C Canlı | Tüm atanmış üç faz, doğru source/EUI, 5/10 s geliş, 30 s sınırı, NaN/Inf/negatif RMS, durmuş uptime, yeniden başlayan AY, Trip_Failed latch ve ACK, FSM/anomali reset |
| D Olay | TRIP sınırlı cache; ayrı 1/7/3 ve diğer 60 B olaylar. 47 kaybı, 40/44/46, doğru prefix consume, ERR06, iç/dış CRC ayrımı, 99→0, tam halka, overwrite, okuma sırasında değişen HEAD/total/tail, reset/kayıp consume sonucu tekrar |
| E Grup | Üç aynı blok, doğru hedef/EUI, yeni kimlik/SEQ, erken/geç status/ACK, yanlış CRC/bitmap, FAILED 1/2/5/6/8/9/10, kısmi uygulama, bilinen kimlikle abort, pre-COMMIT belirsizlikte BQ-11, restart ve ID_IN_USE. Otomatik yeniden uygulama beklenmez |
| F Epoch | Yalnız yüklü envanter, fider 1–4, aynı ACK tek başarı; 30 s ilgili fider beklemesi ve diğer fiderin bağımsızlığı. MH reboot ile yeni MH kartı/key/epoch koşulu ayrı |
| G PWRB | §7'nin tamamı; özellikle MH ACK≠PWRB uygulaması, E1 telafi, quality/sentinel, aynı session tekilleştirme, GEN çatışması, early E7, iptal sonrası geç sonuç ve E8 ham koruma |
| H Hat | Yanlış COBS/CRC/LEN, kısa/uzun/bitişik çerçeveler, metin gürültüsü, parser boşluğu, yanlış yön/adres/tip/SEQ, eski ACK ve üretici bandı; sağlam frame'den sonra toparlanma |
| I Kesinti | MH sessizliği ve tekrar BOOT; PING boş ACK, bekleyen komut sırasında PING ve bildirimler; Port kesilmesi/yeniden açma altyapı kapsamı |
| J Kalıcılık | MH reboot'ta olay deposu/tail ve CFG2 korunur. DUT reboot'ta yerel kayıt/istenen ayar geri yüklenir; APPLIED RAM geçmişi veya otomatik apply varsayılmaz. DUT Flash hata/güç kesintisi ayrı host/bench kanıtı gerektirir |

Takımlar parametrik olmalıdır; açıklama satırındaki sabit “N senaryo”
sayıları gerçek manifestten hesaplanır. Geçersiz TIME_SYNC, doğrudan
model self-testinde sınanabilir; normal DUT onu üretmediğinden HIL'de
uydurma istek beklenmez. MH'nin geçerli isteği reddetmesi farklı bir
fault injection senaryosudur.

## 10. Bağımsız doğrulama ve kanıt

CSV corpus'u mevcut `generate_rf_scp_vectors.py` validator'ıyla kontrol
edilir; referans baytları yeni encoder'ın çıktısıyla değiştirilmez.
Bu incelemede `python test/scripts/generate_rf_scp_vectors.py --check`
14 CSV/175 frame için geçti. Güncel sayım ve hash her koşu manifestine yazılır. E5 örnek erken GET zamanları ve diğer CSV/metin
farkları kuraldan ayrı tutulur. Tam CSV'yi canlı DUT'a körlemesine replay
etmek, farklı SEQ/EUI/RTC ve çift yönlü istekler nedeniyle doğru değildir.

Codec için byte-for-byte round-trip; canlı modelde ise DUT'un güncel
istek SEQ/alanlarıyla korelasyon kullanılır. Bağımsız Python cfg_crc
hesabı R1 byte tablosundan yapılır; C fonksiyonu aynen kopyalanmaz. AY_06
0x096D gibi golden ve sınır vektörleri, ayrıca mevcut C codec'iyle
karşılaştırma birlikte kullanılır. Aynı algoritmanın iki kopyasının
birbirini doğrulaması tek oracle olmamalıdır.

RF trace şunları kanıtlar: DUT hangi isteği gönderdi, hangi bayt/sırayla
sim cevap verdi, gerçekten ne kadar gecikme/düşürme yapıldı. Trace'te
consume görülmesi **yerel Flash kalıcılığı kanıtı değildir**. Yerel raw
kayıt, geçici/kalıcı arıza ve IEC104 event log için ayrı geri okuma/reboot
kanıtı gerekir. RF taraflı ERROR enjeksiyonu, DUT SPI Flash yazma hatasını
simüle etmez. Mevcut gözlem API'si yetmiyorsa ilgili kontrol BLOCKED veya
host testine ait olarak raporlanır; yalnız log metniyle PASS verilmez.

Konsol gözlemleri: `rf status`, `rf inv`, `rf live <fider> <faz>`,
`rf cfg-state`, `rf cfg-status <id>`, `pwrboard show`, `pwrboard control`,
`elog dump`, `iec104elog dump`. `rf log` yalnız log seviyesini ayarlar;
yerel 60 B kayıt dökümü değildir. Komutların tam argümanları `help`/kaynakla
teyit edilir. Verbose logging süre/throughput testinin koşullarını
etkileyebildiği için normal ve verbose koşular ayrı raporlanır.

İkinci fazda gerçek HTTP auth/admin, Kaydet/Uygula/MatchesDesired,
Modbus FLOAT32 NaN ve RF durumunun ayrı olması; IEC104 alım zamanı/CP56 IV
ile ölçüm QDS IV/NT ayrımı doğrulanır. Sadece console getters veya sim
trace bu yayınların gerçekten SCADA'ya gittiğini kanıtlamaz.

## 11. Rapor ve koşu yaşam döngüsü

Manifest: koşu kimliği, kaynak/firmware commit ve dirty (yerel değişiklik)
bilgisi, çalışan firmware kanıtı, Python/pyserial sürümü, port kimlikleri,
baud/gerilim/izolasyon, fixture ve CSV hash'leri, profil, seed (rastgelelik
başlangıç değeri), tüm zaman bütçeleri ve başlangıç/bitiş model snapshots.
Simulated MH reboot snapshot'u persistent/volatile alanları ayırır; PC
simülatör prosesini kapatmak aynı olay değildir. Proses restart kalıcılığı
gerekiyorsa ayrı state-file gereksinimi ve testi eklenir.

Rapor: `report.md`, `results.json`, `sim_trace.jsonl`, ham konsol kaydı ve
kaynak/time-korelasyonlu assertion satırları. Trace; RX/TX ham frame,
monotonic zaman, DATA yorumu, aktör ve fault etiketi içerir. PC ve DUT
saatleri aynı saat sanılmaz. Senaryo tamamlanmadan PASS yazılmaz.

| Durum | Anlam |
|---|---|
| PASS | Seçilen, belgelenmiş ölçüt ve gerekli bağımsız gözlemler sağlandı |
| FAIL | Belgelenmiş/uygulanması gereken DUT beklentisi karşılanmadı |
| BLOCKED | Bağlantı, fixture, yetki veya gerekli gözlem sağlanamadı |
| ERROR | Harness/model/serial/raporlama arızası |
| ASSUMPTION / DEFERRED | Üretici açıklaması veya ürün kararı açık; normatif PASS değildir |

Çıkış sözleşmesi: 0 yalnız tüm seçilen zorunlu kontroller PASS; 1 DUT
FAIL; 2 altyapı/eksik ölçüt nedeniyle tamamlanamama. Fail ile altyapı hatası
birlikteyse 2 ve ayrıntılı sayımlar raporlanır. Zorunlu senaryo sessizce
atlandığında toplam koşu 0 dönmez. Test method, assertion ve frame sayıları
ayrı sayılır; 74 eski C kontrolü, unittest ve Ceedling sonuçları toplanmaz.

Başlangıçta port/izolasyon/bridge/firmware/fixture gate (ön koşul kapısı)
çalışır. Her senaryodan sonra yalnız o senaryonun değiştirdiği ayar/profil
geri alınır. Başarısızlıkta trace ve rapor korunur; serial sahipleri ve
başlatılan PC süreçleri finally bloğunda kapatılır. Plansız DUT reset,
Flash erase, NVRAM silme veya firmware fallback yapılmaz. Son RF/PWRB
ayarları ve paket envanteri raporun kapanışına yazılır.

## 12. Fazlama ve uygulama sırası

1. **Faz 0 — donanımsız uyum:** codec/golden/negatif vektörler, halka/replay/grup/PWRB model testleri. Mevcut merkezi host koşusuna eklenir; serial port açmadan test edilir.
2. **Bench kapısı:** §4 kanıtı ve fixture, ortak console helper/API seçimi, çalıştırma yetkisi. Donanımsız işler donanımı beklemez.
3. **Faz 1 — gerçek RTU:** A–I quick senaryoları ve gerekli J kontrolleri; shell/trace kanıtları. En az bir normal E5 yankı/E1 doğrulaması dahildir.
4. **Soak:** gerçek MH grup 60/120 s sınırları, LOG_HEAD 60 s, E5 kayıp yankı/geç uygulama, saatlik TIME_SYNC ve uzun kesinti. Saatlik davranış yalnız kaynak sabitinden görülürse “statik kontrol”dür; runtime PASS değildir.
5. **Faz 2 — ürün çıkışları:** HTTP/Modbus/IEC104 gerçek istemci kabulleri; fiziksel RF/Powerboard ölçüm testleri ayrı projelendirilir.
6. Raporlar, `tools/rf-hil/README.md`, merkezi `test/README.md` ve eski C aracının kapsam notu güncellenir. Commit yalnız açık kullanıcı isteğiyle yapılır.

Bu inceleme yalnız planı düzenledi; firmware, eski simülatör, konsol helper'ı
veya donanımı değiştirmedi. Diğer HIL geliştirmeleri korundu. İlk test
çağrısı kota nedeniyle çalışmadı; kota düzeldikten sonra donanımsız
74/74 kontrol ve 175 frame CSV doğrulaması geçti. Yeni Python simülatörü
ve DUT HIL koşusu burada çalıştırılmadı. Planın güncellenmesi HIL kabulü
anlamına gelmez.

## 13. Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 06.10.2026 | 1.0 | İlk PC MH/HIL önerisi |
| 06.10.2026 | 1.1 | IV, TRIP kapsamı, E5/E6 desteği, eski C referansı ve etkileşimli konsol ihtiyacı düzeltildi |
| 06.10.2026 | 1.2 | Kaynak karşılaştırması; bağlantı/bridge/port izolasyonu, replay ve wrap, CRC/bitmap, güncel ürün yolları, bağımsız oracle, zamanlama, açık BQ kabul sınırları ve yerel UART4/RS-485 HIL profil farkları netleştirildi |
