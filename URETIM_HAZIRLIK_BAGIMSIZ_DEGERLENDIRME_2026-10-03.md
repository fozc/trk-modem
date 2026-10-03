# Troika Smart Breaker Modem — Bağımsız Üretime Hazırlık Değerlendirmesi

| Belge künyesi | Değer |
|---|---|
| Sürüm | 1.7 |
| Tarih | 03.10.2026 |
| İncelenen commit (kod sürümü) | `a57686279eb913801a766e53f471e4b4c18abfd5` |
| Uygulama sürümü | 1.0.1 |
| Önceki rapor | `URETIM_HAZIRLIK_RAPORU_2026-10.md`, 02.10.2026 |
| Sonuç | **Seri üretim ve saha dağıtımı için hazır değil** |

**Amaç:** Önceki raporun bulgularını değerlendirmek ve mevcut kaynak kodun üretime hazırlığını bağımsız kontrollerle ölçmek.

**Kullanım yeri:** Sürüm hazırlığı, düzeltme önceliği ve üretim kabul planı.

**Kapsam:** Bu depodaki uygulama kaynakları, testler, CubeIDE ayarları, Release derlemesi, paket üretimi ve seçilmiş güvenlik/veri/kalıcılık yolları. Ölçüt mevcut kaynak kod ve bu oturumda üretilen test/derleme çıktılarıdır. Bütün kodun eksiksiz denetlendiği veya ürün sertifikasyonunun tamamlandığı iddia edilmez.

## 1. Sonuç ve gerekçe

Önceki raporun “üretime hazır değil” sonucu doğrulanıyor. En önemli neden, temel ölçüm ve arıza olay zincirinin gerçek cihaz verisine bağlanmamış olmasıdır. IEC-104 başlangıcında dummy (kurgusal test verisi) üretiliyor. Üretim kaynaklarında bu veriyi gerçek telemetriyle değiştiren bir çağrı bulunamadı. Kimlik doğrulama modeli de cihaz IP adresinden türetilen parola ve tahmin edilebilir oturum tokenine dayanıyor.

Olumlu sonuçlar da somut: 272 birim testi ve 7 entegrasyon paketi geçti. Güncel Release kaynakları ARM toolchain ile baştan derlendi. İmzalı ve şifreli paket üretildi; paket aracının içerik ve imza kontrolü geçti. Bunlar önceki rapordaki ARM derleme belirsizliğini bu uygulama sürümü için azaltıyor. Cihazda çalışma, bootloader ile gerçek kurulum ve saha kabulü bu oturumda doğrulanmadı.

Üretim standardındaki uyarı politikası uygulanınca önemli bir ek engel ortaya çıkıyor: 107 Application kaynak dosyasının 38'i zorunlu uyarı bayraklarıyla yapılan syntax-only (nesne üretmeden kaynak denetimi) kontrolünden geçemiyor. Normal Release derlemesi de 34 uyarı veriyor.

Üretim önerisi: Bölüm 5'teki kapılar kapanmadan seri üretim sürümü yayımlanmaması. Bu bir teknik değerlendirmedir; tasarım değişikliği veya risk kabulü kararı alınmamıştır.

## 2. Yöntem ve doğrudan test sonuçları

### 2.1 İnceleme tabanı

Başlangıç ve bitişte takip edilen dosyalarda değişiklik yoktu. Mevcut izlenmeyen raporlar ve `engineering-guidelines/` dizini korunmuştur. Bu değerlendirme için yeni belge ve git tarafından yok sayılan test/derleme çıktıları oluşturuldu. Firmware kaynakları değiştirilmedi; commit, push veya cihaza yazma yapılmadı.

Kurallar için `AGENTS.md`, `CLAUDE.md`, `.github` C/C++/atomic yönergeleri ve `doc/belge_yazim_rehberi.md` okundu. Derleme için `troika-hwtest` skill'i ve `doc/HW_TEST_VE_DEBUG_REHBERI.md` kullanıldı.

Normal komut ortamı sandbox başlatma hatası verdi. Depo okuma ve yerel test/derleme komutları onaylı yükseltilmiş yürütme ile tamamlandı. Bu sorun test sonucu olarak sayılmadı.

### 2.2 Ölçülen sonuçlar

| Kontrol | Yöntem | Sonuç ve sınır |
|---|---|---|
| Host testleri | `ruby test/run_all.rb all` | Çıkış 0; 272 test, 0 failure; 7 entegrasyon paketi PASS |
| RF hub simülasyonu | Merkezi entegrasyon koşusu | 74 pass, 0 fail; gerçek radyo/telemetri bağlantısının kanıtı değildir |
| Baştan Release derleme | GCC ARM 14.3.1, `make -C Release -B -j8 main-build` | Çıkış 0; bütün ilgili nesneler yeniden derlendi; 34 uyarı |
| Pre/post-build ve paket | `make -C Release -j8 all` | Çıkış 0; web varlık üretimi ve EFW paketleme başarılı |
| EFW self-check (paket öz denetimi) | `bin2efw.py` içindeki kontrol | Ham içerik eşitliği ve ECDSA doğrulaması PASS; bootloader üzerinde kurulumu kanıtlamaz |
| Zorunlu ARM uyarıları | Mevcut Release seçeneklerine `-Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2`, syntax-only | 107 Application dosyası; 69 geçti, 38 geçmedi. Test alt dizinleri hariç; vendor ve Core için tam politika denetimi yapılmadı |
| ELF yerleşimi | `arm-none-eabi-objdump -h` | `.isr_vector` 0x08014000; Release linker tabanı ile uyumlu |
| VTOR | `Core/Src/main.c:111-114` | DEBUG dışında 0x14000; Release vektör adresiyle uyumlu |
| HardFault giriş kodu | `objdump --disassemble=HardFault_Handler` | Beklenen altı komut; prologue yok |
| Heap sembolleri | ELF `nm`, kaynak taraması | ELF'te malloc/calloc/realloc/_sbrk sembolü görülmedi; uygulamada doğrudan heap çağrısı bulunmadı. Kaynak ve linker politikası ayrı açık madde |
| Stack (çağrı yığını) | `.su` dosyaları ve linker | 4096 bayt MSP rezervi; güncel üretim fonksiyonlarında örneğin `send_fault_me_tf_1` 1000 bayt. Toplam çağrı zinciri + ISR üst üste kullanımı ölçülmedi |
| Cihaz kabulü | Yapılmadı | Cihaza bağlanma, reset, flash, güç kesme veya arıza enjeksiyonu yapılmadı |

Release `size` çıktısı: text 286668, data 504, bss 124832 bayt. Flash bütçesi için text+data yaklaşık 287172 bayt, 432 KiB bölgenin yaklaşık %64,9'u. Binary dosyası 287176 bayt; hizalama farkı normaldir. Map dosyasında gerçek `.bss` 0x1D59C bayt, yani 120220 bayt; `size` bss toplamı heap/stack rezervini de içerir. RAM2 ve dinamik stack kullanımı yalnız bu toplamdan değerlendirilemez.

Üretilen paket: `Release/troika-smart-breaker-modem_v1.0.1_20261003_100454_a5768627.efw`, 155517 bayt. ECDSA-P256, LZMA1_ARMTHUMB ve AES-128-CTR zinciri yerelde çalıştı. Dosya adındaki saat araç çıktısıdır. İmzalama anahtarının içeriği rapora alınmadı.

Kanıt dosyaları:

- `test/build/production-audit-2026-10-03/host-tests.log`
- `test/build/ceedling/artifacts/test/junit_tests_report.xml`
- `test/build/production-audit-2026-10-03/release-build.log`
- `test/build/production-audit-2026-10-03/release-package.log`
- `test/build/production-audit-2026-10-03/strict_arm_check.py`
- `test/build/production-audit-2026-10-03/strict-arm-diagnostics.json`
- `test/build/production-audit-2026-10-03/strict-arm-diagnostics.log`

Bu çıktılar `test/build/` altında yereldir; sürüm kabul kanıtı olarak kullanılacaksa saklanan sürüm arşivine eklenmelidir. Strict denetim syntax-only olduğu için optimizasyonla ortaya çıkan bütün uyarıları yakalamaz; tam üretim derlemesinin yerini tutmaz.

## 3. Önceki rapordaki bulguların değerlendirmesi

| Önceki madde | Bağımsız değerlendirme | Kanıt / düzeltme |
|---|---|---|
| IEC-104 dummy veri ve kopuk telemetri | **Doğrulandı; kritik üretim engeli** | `Application/iec104_process.c:93-123,431`; `breaker_set_feeder_data` üretim çağrısı dummy üreticisinde; RF proactive default yolu `Application/rf/rf_comm.c:487` |
| Arıza olay üreticisi yok | **Doğrulandı** | `Application/iec104_replay.c:189` tanımı var; Application/Core taramasında `iec104_report_fault_event` çağrısı bulunmadı |
| IP'den parola ve tahmin edilebilir token | **Doğrulandı; kritik güvenlik engeli** | `Application/web-server/http_handlers.c:375-377,407-415`; başarılı giriş için deneme kilidini aşmak gerekmez |
| RFWU ortak default ve replay riski | **Doğrulandı** | `Application/web-server/raw_tcp_fw_update.h:55`; `.c:240-246,270`; CRC32(key) XOR boyut, nonce yok. CRC32 bir kimlik doğrulama MAC'i değildir |
| RFWU REBOOT/QUERY/ABORT kapıları | **Host üzerinde doğrulandı** | HELLO öncesinde reddedilir; disconnect yetkiyi siler. Replay HELLO tekrar yetki verir; reboot callback ve ABORT kabul edilir. Gerçek ağ/cihaz testi yapılmadı; bkz. 6.8 |
| Web admin kapıları | **Kaynak düzeyinde doğrulandı** | `Application/web-server/http_server.c:109-121,258-263`; POST `/serial` dahil admin kapısından geçiyor |
| BMS kapalı ama RX açık | **Doğrulandı** | `Application/app_main.c:331-332`; UART5 ISR `Core/Src/stm32u3xx_it.c:447-456`; BMS RX buffer'ı dolar ama reader süreci başlatılmıyor |
| PowerBoard NVM stub | **Doğrulandı** | `Application/power_board/power_board.c:407-432,707`; kayıt isteği/lastgasp kalıcı saklanmıyor. Her reset sonrası gerçek SoC'nin kesin %100 olduğunu bu depo tek başına kanıtlamaz; PowerBoard firmware'i de gerekir |
| SBO kapalı / eksik | **Doğrulandı; ürün kapsamına bağlı** | C_SC_NA_1_ENABLED derleme tanımlarında yok; `Application/breaker.c:167-213` altında initialized ve IOA eşleme eksikleri. Desteklenen özellik listesinden çıkarılması ürün kararıdır |
| Lifetime ve reset flush | **Kısmen doğrulandı; RAM-only ifadesi düzeltilmiştir** | Sayaç NVRAM RAM görüntüsündedir; başarılı herhangi bir sync ile saklanır. Düzenli kayıt ve normal reset öncesi kayıt yoktur. Gerçek NVRAM host testi son kayıttan sonraki süre kaybını gösterir; bkz. 6.9 |
| ARM uyarı bayrakları hiç yok | **İfade yanlış; esas uyumsuzluk doğru** | `.cproject` içinde açık seçenek bulunmaması varsayılanı kanıtlamaz. `Release/Application/subdir.mk` ve gerçek komutlar `-Wall` içeriyor. Diğer zorunlu bayraklar yok; 34 uyarı ve 38 strict başarısız dosya ölçüldü |
| CI yalnız host derliyor | **Workflow kapsamında doğrulandı** | `.github/workflows/host-tests.yml` ARM derlemiyor. Uzak son CI koşusu sorgulanmadı; workflow varlığı PASS kanıtı sayılmadı |
| İmaj tek makinede üretilebilir | **Kanıt yetersiz; süreç riski açık** | Bu makinede anahtarlarla paket üretildi. Anahtarların git dışında olması doğrudur; başka makinede yedek/erişim bulunmadığı buradan çıkarılamaz. Kurtarma ve yetkili yeniden üretim prosedürü kabul kanıtı ister |
| HTTP dökümü fabrika ayarında açık | **Düzeltme gerekli** | HTTP GSM seviyesini gerçekten paylaşır (`console_logger.c:264`); VERBOSE seviye doğru. Ancak `nvram_set_defaults` yapıyı sıfırlar, cslog_enabled için true ataması bulunmadı. `console_logger.c:106,259` genel enable kapısı uygular. Fabrika ayarında kapalı; sonradan açılan loglarda body/token sızıntısı sürer |
| Heap rezervi ve _sbrk | **Politika açığı doğrulandı; runtime heap kullanımı kanıtlanmadı** | BOOT linker'da 512 bayt rezerv var; `Core/Src/sysmem.c` _sbrk ve errno içeriyor. Güncel ELF'te ilgili semboller GC ile elenmiş görünüyor; “cihaz heap kullanıyor” sonucu çıkarılamaz |
| GSM kritik düzeltmeleri | **Seçilmiş kaynak örnekleri olumlu** | `gsm_engine.c:3475-3498` uzunluk ve gönderim sonucu kontrollü. Önceki raporun bütün GSM kapanışları bağımsız uçtan uca doğrulanmış sayılmadı |
| Flash çip kimliği/dönüş kontrolü | **Kaynak düzeyinde doğrulandı** | `Application/libs/w25qxx.c:211-235`; `Application/app_main.c:244`. Fiziksel BOM (parça listesi) bu oturumda teyit edilmedi |
| 272 test + 7 paket | **Yeniden çalıştırılarak doğrulandı** | Bölüm 2.2 |
| Web CSQ'ya dBm etiketi | **Kaynak zinciri doğrulandı** | `Application/system_status.c:85-86`, `http_handlers.c:679`, `web-page/index.html:309` |
| Sabit firmware fallback | **Doğrulandı** | `Application/web-server/http_handlers.c:1738` 1.0.0; uygulama 1.0.1. Kullanılan ekrana göre etkisi değerlendirilmelidir |
| TODO / boş dizin / ölü araçlar | **Düşük öncelikli bakım işi** | Her TODO üretim engeli değildir. Eksik özellik ile bakım artığı aynı önem sınıfına alınmamalıdır |

Önceki rapordaki “2–3 gün” tahminleri yeterli kabul kapsamına dayanmıyor. Telemetri protokolü, kimlik provizyonu, NVRAM değişikliği, bootloader uyumu ve donanım testleri netleşmeden süre taahhüdü verilmemesi önerilir.

“Beş küme kapanınca bilinen engel kalmaz” ifadesi de sınırlanmalıdır: önceki raporun kendi doğrulanmamış maddeleri ve bu turdaki ARM tanıları ayrıca değerlendirilmelidir. Test sayısı üretime hazır olmanın tek ölçütü değildir.

## 4. Bağımsız ek kontroller ve riskler

### 4.1 Uyarısız ARM derleme koşulu sağlanmıyor — yüksek

Normal derlemede 34 uyarı var. Bunlar farklı dosyalarda yinelenen makro tanımları, kullanılmayan kod, pointer signedness (işaretlilik), packed member (sıkıştırılmış yapı alanı) adresleri ve xscanf initialized-state (başlatılma durumu) uyarısını kapsıyor. 34 sayısı benzersiz hata sayısı değildir.

Zorunlu bayraklarla 38 dosya geçmiyor. Tanıların bir kısmı aynı include/makro sorunundan veya legacy format kullanımından türeyen çok sayıda mesajdır. Mesaj sayısı bağımsız bug sayısı olarak sunulmamalıdır. Vendor başlıkları üzerinden gelen tanılar da ayrıca ayrılmalıdır.

Örnekler: `gsm_engine.c:910,991,1227` packed IPv4 alanının adresini iletiyor. Cortex-M üzerinde etkisi alan hizası ve çağrılan fonksiyonun üretilen komutlarına bağlıdır; sırf uyarı nedeniyle kesin HardFault iddia edilmez. ARM disassembly ve hizalama kanıtı gerekiyor.

`Application/libs/xscanf.c:548-552` için normal optimized derleme “val may be used uninitialized” uyarısı verdi. `parse_hex` başarı yolunda out alanını yazıyor, erken dönüşlerde hata ayarlıyor. Okunan kontrol akışında kesin başlatılmamış okuma gösterilemedi; şimdilik uyarı/politika açığıdır. Gerçek hata olarak kapanış öncesi hedefli test ve derleyici incelemesi gerekir.

### 4.2 Shell iki bağlamı ve yetki ömrü — yüksek

`Application/libs/shell.c:58-70` içinde session_level, RX buffer, indeks ve ready alanları ortak durumdur. Seri ISR bu durumu beslerken web yolu da `web_shell.c:113-117` ile aynı `shell_on_rx_received` ve `shell_process` fonksiyonlarını kullanır. `shell_su` yetkiyi tek globalde yükseltir; `shell_exit` düşürür. Session timeout (oturum zaman aşımı) halen TODO'dur (`shell.c:535`). Başlangıç parola parametresi NULL'dır (`app_main.c:271`); shell default parolası devrededir.

HTTP admin kapısı bulunduğu için bu bulgu tek başına anonim web erişimi anlamına gelmez. Fakat RX sahipliği, seri/web oturum ayrımı ve yetkinin ne zaman düşeceği tanımlı değildir. Kaynakta yarış riski var; cihazda eşzamanlı seri ve web yüküyle gösterilmiş bir bozulma bu oturumda yoktur.

BMS tekrar açılırsa aynı kontrol zorunludur: `bms_reader.c:14-15,33-40,122...` içinde ISR ve process tarafından erişilen indeks/buffer için güvenli teslim mekanizması gösterilmelidir. Tek init satırını açmak üretim çözümü değildir.

### 4.3 Önceden doğrulanmamış log/watchdog maddeleri

- Sequence (sıra numarası) sarmasında boş log kararı: `spi_flash_log.c:534` artık next_seq değerini boşluk ölçütü yapmıyor. Önceki Y9.3 için kaynak düzeyinde kapanış kanıtı var. Bu turda mevcut log entegrasyon testleri geçti; tam 16-bit sequence sınırı için yeni test eklenmedi.
- Ring sektör silmede watchdog kick (besleme): ring `ops.erase_sector` üzerinden `w25qxx_erase_sector` çağırıyor. `w25qxx.c:385` beslemeyi erase öncesinde yapıyor. “Ring erase yolunda hiç kick yok” iddiası mevcut kaynakta doğru değil.
- Uzun busy bekleyişinde periyodik kick yok: `w25qxx_wait_for_write_or_erase` döngüsü poll sayısı ile sınırlandırılmış, gerçek zaman deadline (son süre) değil. EWDT penceresi, worst-case erase süresi ve scheduler gecikmesi donanımda ölçülmeden güvenli kabul edilemez. Kaynaktaki 45–400 ms yorumu bu oturumun ölçümü değildir.

### 4.4 Latent kernel ve shell tablo kontrolleri

`contiki-kernel/sys/int-master.c:48-67` ARM dalında enable fonksiyonu IRQ kapatıyor; read-and-disable dönüş değeri eksik. Mevcut portta ARM dalı aktif olmadığı için bu sürümün çalıştığı yolda doğrudan hata gösterilmedi. Platform değişiminde yüksek risk taşıyor; önceki Y2.1 açık kalmalıdır.

Shell komut kaydında `cmd_list_counter >= SHELL_MAX_CMD_LIST_COUNT` sınır kontrolü var (`shell.c:159`). Doğrudan tablo taşması bu yolda gösterilmedi. Ancak 32 eleman kapasiteye sığmayan kayıtlar reddedilir; init çağıranlarının dönüş kontrolleri ve cihaz `help` listesi bütünlüğü ayrıca kabul edilmelidir.

### 4.5 Test kapsamı ve sahte güven

Mevcut test başarısı gerçek RF ölçümünün SCADA'ya ulaştığını, tam AT motorunun bütün hata yollarını veya web login/config güvenliğini kanıtlamaz. Merkezi testte BMS/Modbus/libmodbusrtu için ayrı birim test grubu yok; BSP, main init, HTTP auth, RFWU ve gerçek update/boot akışı tam uçtan uca kapsanmıyor.

Modbus akü uyarısı `Application/modbus_process.c:434` içinde hâlâ sabit 23U. Bu bir bakım TODO'sundan daha önemlidir: alarm işlevi ürün kapsamındaysa gerçek kaynak ve stale-data (eski/geçersiz veri) davranışı kabul edilmeden yayımlanamaz.

Modbus baud değişimi artık aktif DMA'yı iptal edip RX'i temizliyor (`modbus_process.c:534-558`); “idle kontrolü hiç yok” anlatımı güncel kodu temsil etmez. İptal edilen yanıtın kabul davranışı ve RS-485 echo (kendi gönderimini alma) konusu cihazda test ister. Default yazılımsal gap ve opsiyonel HW RTO ayrı yollar olarak değerlendirilmelidir.

### 4.6 Ürün kapsamı ve donanım sınırları

BMS, SBO ve PowerBoard kalıcılığının ilk ürün sürümünde zorunlu olup olmadığı bu incelemeden çıkmaz. Bunlar için açık kapsam veya erteleme kararı gerekiyor. İsimleri yalnız “kapalı” işaretlemek ürünün vaat edilen işlevini doğrulamaz.

PA7/RF_IO1 paylaşımı, RF kanal kaynağı, EWDT penceresi, RS-485 transceiver davranışı, güç kaybında lastgasp süresi ve seri üretim kart toleransları ölçülmedi. Sibling bootloader kaynakları, option bytes (MCU güvenlik ayarları), imza public key eşleşmesi, debug kilidi ve rollback (eski sürüme dönüş) politikası bu denetim kapsamı dışındadır. Paket öz denetimi bunların yerine geçmez.

## 5. Üretim kabul kapıları

Aşağıdaki tabloda önerilen kabul ölçütleri bulunur. Mimari, protokol, API, veri modeli ve bellek yerleşimi değişiklikleri için AGENTS.md gereği kullanıcı kararı alınmalıdır.

| Öncelik / kapı | Kabul ölçütü | Gerekli kanıt |
|---|---|---|
| P0 — gerçek veri | Üretim init'inde iyi kaliteli dummy yayımlanmamalıdır. RF ölçüm/arıza → veri modeli → IEC-104/Modbus zinciri tamamlanmalı; kayıp/eski veri kalite davranışı tanımlanmalıdır | Gerçek veya onaylı protokol vektörü ile uçtan uca ölçüm, timeout, tekrar bağlantı ve arıza replay kayıtları |
| P0 — kimlik modeli | IP bilgisinden admin erişimi üretilememeli; token tahmin edilememeli; RFWU kimlik doğrulaması replay'e dayanmalı; gizli değerler loglarda açığa çıkmamalıdır | Onaylı provizyon/anahtar modeli, negatif auth/replay testleri, üretim log denetimi |
| P1 — kapsam | BMS, kesici komutları, akü alarmı ve PowerBoard persist için destek/erteleme kararı verilmelidir | Özellik matrisi, gerçek işlev testleri, kullanma kılavuzuyla tutarlılık |
| P1 — kalıcılık | Lifetime, ayarlar ve gerekli enerji/SoC alanlarının reset ve güç kaybındaki kayıp sınırı tanımlanmalıdır; flash aşınma bütçesi hesaplanmalıdır | Reset/power-cut kayıtları, dual-copy kurtarma testi, endurance hesabı |
| P1 — ISR ve shell | RX sahipliği, oturum yetki ömrü ve veri yayımı güvenli olmalıdır | Seri/web eşzamanlı yük testi, atomic/critical-section denetimi, gerekiyorsa ARM komut kanıtı |
| P1 — derleme | Onaylı üretim uyarı politikası tam ARM derlemesinde geçmelidir | Uyarısız Release logu; gerekçeli, kapsamı belli vendor istisnaları; güncel ELF/map |
| P1 — cihaz update/kurtarma | Doğru paket kurulmalı; hatalı imza, bozuk paket ve kesilmiş update güvenli reddedilmeli veya kurtarılmalıdır | BSL FW_INSTALL_OK/FW_APPROVED kayıtları, A/B güncelleme, güç kesme ve rollback testleri |
| P1 — zaman ve watchdog | En uzun flash/log/update yolu EWDT ve protokol sürelerini bozmayacak şekilde çalışmalıdır | Donanım ölçümü, besleme aralığı, scheduler gecikmesi, reconnect/soak testleri |
| P1 — sürüm üretimi | Yetkili başka ortamda paket üretimi ve anahtar kurtarma yöntemi gösterilmelidir | Saklanan araç sürümleri, kaynak kimliği, paket hash'i, imza doğrulaması; anahtar yedek/erişim prosedürü |
| P2 — belge ve operasyon | Desteklenen protokoller, kurulum, güvenli giriş, güncelleme ve kurtarma adımları yayımlanan sürümle eşleşmelidir | Onaylı kılavuz, release notes (sürüm notları), üretim seri no/provizyon kaydı |

Kabul sırası önerisi: önce P0 işlev ve kimlik modeli; ardından kapsam/kalıcılık/ISR düzeltmeleri; sonra uyarısız ARM derlemesi ve aynı imaj üzerinde cihaz testleri. Son testten sonra kaynak değişirse etkilenen kabul kanıtları yeniden üretilmelidir.

**Nihai değerlendirme:** Mevcut sürüm geliştirme ve kontrollü laboratuvar çalışması için test edilebilir bir tabana sahip. Seri üretime çıkış önerilmez. Kritik veri ve güvenlik engelleri açık; ARM kalite politikası sağlanmıyor; donanım kabul kanıtı bu sürüm için tamamlanmış değil.

## 6. B seçeneği uygulaması ve doğrulama

**Tarih:** 03.10.2026. Yukarıdaki bölümler ilk incelemenin durumunu gösterir. Bu bölüm, kullanıcı kararıyla yapılan sonraki değişiklikleri ve kalan sınırları kaydeder. CubeMX çıktıları ve uygulama/test değişiklikleri kullanıcı kararıyla ayrı commitlerde kaydedilir; ilk incelemenin commit kimliği bu değişiklikleri içermez.

### 6.1 Kullanıcı kararları

- Gerçek veri ve altyapı henüz hazır olmadığından dummy üreticiler korunmuştur. Gerçek veri zinciri tamamlanıncaya kadar bu bulgu ertelenmiştir; üretim kabul ölçütü karşılanmış sayılmamalıdır.
- Her cihaza ayrı parola atanamadığından B seçeneği uygulanmıştır: mevcut IP tabanlı parola korunmuş, oturum güvenliği ve hassas loglar düzeltilmiştir.
- Kullanıcı CubeMX üzerinden RNG (rastgele sayı üreticisi) ve clock error detection (saat hatası algılama) özelliğini etkinleştirmiştir. Oluşan HAL ve proje değişiklikleri korunmuştur.
- `#if 0` ile kapatılmış kodlar korunmalıdır. Kullanıcı açıkça kaldırılmasını istemedikçe silinmemelidir. Kaldırılan GSM HTTP bloğu geri alınmış ve bu kural `AGENTS.md` dosyasına eklenmiştir.

### 6.2 B seçeneğinin ilk uygulaması (1.1–1.2)

Eski oturum tokeninin tick değeri ve kullanıcı adından türetilmesi gerçek bir sorundur; gerçek giriş ve token doğrulama fonksiyonlarıyla host ortamında yeniden üretildi. Mevcut IP parolasıyla giriş davranışı korunmuştur. Yeni token donanım RNG kaynağından dört adet 32 bit değer alır ve 32 hex karakterle taşınır. RNG okuması başarısızsa, saat veya seed (başlangıç değeri) hatası varsa ya da üretilen değer bütünüyle sıfırsa giriş HTTP 503 ile reddedilir; tahmin edilebilir bir yedek token üretilmez. Başarısız yeni giriş denemesinde mevcut token, rol ve son etkinlik zamanı korunur; bu davranış 1.2 güncellemesinde kullanıcı onayıyla değiştirilmiştir.

Token sorgusu tam uzunluk ve hex karakterler bakımından denetlenir. Eski 8 karakterlik tokenler kabul edilmez. Yeni giriş önceki tokeni geçersiz kılar; mevcut logout (çıkış), 15 dakikalık boşta kalma süresi ve başarısız giriş kilidi korunmuştur.

Aktif oturum tokeni, HTTP yanıt gövdesi ve IEC ayarlarının tam JSON gövdesi loglardan çıkarılmıştır. AT motorunun VERBOSE logunda SRECV yanıtındaki HTTP içeriğini yazdığı ayrıca doğrulanmıştır; bu yolda yalnız sonuç ve byte sayısı kaydedilir. Kısmi yanıt ve timeout (zaman aşımı) durumunda da içerik gizlenir. Normal modem tanılama yanıtları korunmuştur. GSM HTTP dosyasındaki `#if 0` bloğu zaten kapalıydı; bu blok aktif sızıntı olarak değerlendirilmemelidir.

### 6.3 Kontrol sonuçları

| Kontrol | Sonuç ve sınır |
|---|---|
| Yeni kalıcı token birim testleri | 12/12 geçti; uzunluk, karakter, farklı token, null giriş ve RNG hata davranışı kapsandı |
| Tam host test çalışması | 284/284 Ceedling testi ve 7 entegrasyon paketi geçti |
| Son AT log değişikliği sonrası GSM testleri | 36/36 geçti |
| Gerçek fonksiyonların izole host kontrolleri | 16/16 geçti; giriş, eski token reddi, tekrar giriş, RNG hatasında oturum ve rol korunması, yetki artırmanın engellenmesi, süre aşımı, çıkış, kilit, AT log gizleme ve BSP hata yolu doğrulandı. Donanım ve taşıma katmanı test doubles (taklit bileşenler) ile çalıştırıldı |
| ARM Release derlemesi | Geçti; RNG sürücüleri derlemeye dahil edildi. Bu sonuç, önceki rapordaki tüm uyarıların giderildiği anlamına gelmez |
| Üretilen firmware paketi | İçerik eşitliği ve imza öz denetimi geçti |
| Gerçek cihaz | Firmware yüklenmedi; RNG çalışma davranışı, saat hatası ve web giriş akışı donanımda henüz doğrulanmadı |

Test ve derleme kayıtları yerel `test/build/production-audit-2026-10-03/` klasöründedir. Yeni token testleri kaynak ağacında `test/web_server/test_http_session_token.c` dosyasındadır. 16 izole kontrolün kalıcı kaynağı `test/integration/web_auth/run_tests.py` dosyasına taşınmış ve merkezi test çalıştırıcısına eklenmiştir. Taşıma sonrasında tam merkezi koşuda 284 Ceedling testi ve yeni web_auth dahil 8 integration paketi geçmiştir. Üretilen çıktılar `test/build/web_auth/` altındadır; tam cihaz testi yerine geçmez.

### 6.4 RNG hata yolunun önceki uygulaması (1.2)

**Kaynak kanıtı:** Depoda kullanılan `Drivers/STM32U3xx_HAL_Driver/Src/stm32u3xx_hal_rng.c` dosyasında `HAL_RNG_GenerateRandomNumber()` mevcut seed hatasını kontrol edip `RNG_RecoverSeedError()` çağırır. Önceki BSP ön kontrolü seed hata bayrağında bu çağrıya ulaşmadan dönüyordu. Ön kontrolde yalnız clock hatası denetimi bırakılmıştır. HAL okuması sonrası seed ve clock bayraklarının denetimi korunmuştur; hata işaretli veri token üretiminde kullanılmaz.

Kullanıcı kararıyla HAL'in mevcut kurtarması kullanılmıştır. Ek retry döngüsü, otomatik reset, bakım komutu, arka plan süreci veya yedek token algoritması eklenmemiştir. Hata durumunda yalnız yeni giriş HTTP 503 ve tekrar deneme açıklamasıyla reddedilir. Geçerli oturumun tokeni, rolü ve son etkinlik zamanı değiştirilmez. Normal süre aşımı ve çıkış kuralları geçerlidir. Rol ve token yalnız yeni tokenin tamamı başarıyla üretildikten sonra güncellenir.

12 token Ceedling testi ve 16 izole host kontrolü bu güncellemeden sonra geçmiştir. BSP kontrolleri HAL için test doubles kullanır; gerçek donanım kurtarmasının başarılı olduğunu kanıtlamaz. Güncel ARM Release derlemesi ve paket içerik/imza öz denetimi geçmiştir. Cihaz üzerinde hata kurtarma henüz denenmemiştir. Kalıcı RNG arızasında yeni web girişi hâlâ reddedilir.

Kaynaklara dayalı hata sıklığı verisi bulunmadığından RNG arızası için sayısal olasılık verilmemiştir. Bir hata bayrağı tek başına kalıcı donanım arızasının kanıtı değildir. İşe başlamadan gerçek çağrı yolunun ve mevcut altyapı çözümünün kanıtlanması; yeterli olan en basit yapının tercih edilmesi kuralları `AGENTS.md` dosyasına eklenmiştir.

### 6.5 Kalan riskler ve önerilen sıra

Normal RNG yolu token tahmin edilebilirliğini giderir; 6.6 bölümündeki fallback bu güvenceyi sağlamaz. IP tabanlı admin parolası hâlâ tahmin edilebilirdir. HTTP ve URL sorgusunda token taşınması da korunmuştur; taşıma güvenliği ve tarayıcı/ara katman kayıtları ayrı değerlendirilmelidir. Bu nedenle kimlik modeli kabul kapısı bütünüyle kapanmamıştır.

Bir sonraki kaynak incelemesinde RFWU kimlik doğrulamasının replay (aynı isteğin tekrar kullanılması) riskinin gerçek çağrı zincirinde doğrulanması önerilir. Protokol davranışı değiştirilecekse seçenekler kullanıcıya sunulmalıdır. Ardından kalıcılık ve ISR/shell bulguları sırasıyla ele alınmalıdır. Üretime hazır kararı için bu açık konuların ve gerçek cihaz kabul testlerinin tamamlanması gereklidir.

### 6.6 RNG kullanılamadığında bağlantı sürekliliği (1.3)

**Kullanıcı kararı:** RNG hatasında web girişinin devam etmesi için daha
zayıf bir fallback kabul edilmiştir. Bu bölüm 6.2–6.4 bölümlerindeki
HTTP 503 davranışının yerine geçen güncel uygulamayı açıklar.

Başarılı `bsp_random_word()` okumaları RAM'deki 32 bit birikime eklenir.
Birikim başlangıçta bilgisayarda bir kez rastgele seçilen sabit değerden
başlar; bu değer firmware'de bulunur ve gizli anahtar değildir. Toplama
unsigned olarak modulo 2^32 sarar. Hatalı okumalar birikime eklenmez.
Her üretilen HAL dahili değeri değil, BSP tarafından kabul edilen okumalar
biriktirilir.

Donanım RNG ile token üretimi tamamlanamazsa birikim, `bsp_get_tick()` ve
mevcut `gsm_get_rxtx_counters()` sonuçları RAM'de ilerleyen fallback durumuna
karıştırılır. Bu sayaçlar genel GSM RX/TX byte toplamlarıdır; ayrıca web
sayacı eklenmemiştir. xorshift32 ile 32 hex karakterlik token oluşturulur.
Doğru parola kontrolü ve mevcut oturum süresi/kilit kuralları geçerlidir.
Başarılı fallback girişi de yeni rol/token yayımlar ve önceki tokeni
geçersiz kılar. Fallback kullanımı loglanır; token ve birikim loglanmaz.

Kullanıcı yönlendirmesiyle HAL başlatması main içindeki mevcut
`MX_RNG_Init()` akışında bırakılmıştır. BSP içinde ikinci başlatma yoktur;
main tarafından hazırlanmış RNG handle doğrudan kullanılır. Core dosyalarında
bu uygulamaya ait değişiklik kalmamıştır. NVRAM alanı veya vendor sürücü
değişikliği yapılmamıştır; `#if 0` blokları korunmuştur.

**Kapsam sınırı:** Fallback kriptografik bir üretici değildir. 32 hex
karakter, 128 bit güvenlik gücü olarak değerlendirilmemelidir. Tick ve GSM
sayaçları gizli rastgele kaynaklar değildir. RAM birikimi/durumu reset ile
başlangıca döner; aynı girdilerle ayrı açılışlarda aynı token oluşabilir.
Bu değişiklik, RNG hatasının girişe etkisini azaltır; ağ/modem, güç veya
sistem saati arızalarında erişim garantisi sağlamaz. Mevcut RNG init,
sistem saati ve HAL MSP saat yapılandırması fatal hata yolları değiştirilmemiştir. Donanım arızası olasılığına sayısal değer verilmemiştir.

**Doğrulama:** 16 token Ceedling testi ve 18 kalıcı izole host kontrolü
geçmiştir. Tam host koşusunda 288 Ceedling testi ve 8 integration paketi
geçmiştir. Son kullanıcı yönlendirmesiyle HAL başlatması main içinde bırakıldıktan
sonra 18 kontrol ve Release derlemesi yeniden doğrulanmıştır; paket içerik/imza öz denetimi başarılıdır.
Gerçek cihaz üzerinde RNG okuma hatası ve fallback girişi henüz denenmemiştir.
Bu kaynak değişiklikleri kullanıcı kararıyla BSP random modülü commitinde kaydedilir.

### 6.7 Ayrı BSP random modülü (1.4)

RNG okuması, kabul edilen değerlerin RAM birikimi, fallback karıştırma ve
xorshift32 durumu `Application/bsp/bsp_random.c/.h` modülüne taşınmıştır.
HAL başlatması main içinde kalır. Modül GSM veya HTTP'ye bağımlı değildir;
fallback girişleri tick/TX/RX parametreleri olarak web katmanından verilir.
Web katmanı yalnız 32 hex karaktere biçimlendirme, token doğrulaması ve
oturum yönetimini yapar. 6.6 bölümündeki güvenlik sınırları korunmuştur.

Yeni modülün 9 Ceedling testi gerçek kaynak dosyasını CMock HAL ile derler.
Web token modülünün 12 Ceedling testi korunmuştur; 19 kalıcı izole host
kontrolü çalıştırılmıştır. Token üreticisi doğrudan `bsp_random_word()`
çağırır; random işlev sağlayıcısı parametresi kaldırılmıştır. RNG/fallback
seçimi BSP içinde yapılır. Fallback geçişinde bir kez cslog bildirimi
verilir; sağlıklı okumadan sonra yeni hata olursa bildirim tekrar verilir.
Güncel tam koşuda 293 Ceedling testi ve 8 integration paketi geçmiştir. ARM Release derlemesi yeni `bsp_random.c` dosyasını derleyip
bağlamıştır; paket içerik/imza öz denetimi başarılıdır. CubeIDE yeni kaynağı
BSP dizininden keşfeder; yerel generated Release kaynak/nesne listeleri
derleme doğrulaması için güncellenmiştir. Cihaz testi yapılmamıştır.

Dosya başlıklarında `Author: Fatih Ozcan` ve alt satırda
`fatihozcan@gmail.com` kullanılması kuralı AGENTS.md dosyasına eklenmiştir.
Bu çalışmada yeni veya düzenlenen C başlıkları bu biçime getirilmiştir.

### 6.8 RFWU kimlik doğrulaması: kanıtlanan açık (1.5)

**İnceleme tabanı:** `622f050`. Üretim kodu değiştirilmeden gerçek
`raw_tcp_fw_update.c` ve `efw_crc.c` host shared library olarak derlenmiştir.
TCP gönderimi, flash callback, NVRAM, IPC ve log bağımlılıkları test double
ile karşılanmıştır. Paket CRC biçimi mevcut PC istemcisiyle aynıdır:
`zlib.crc32(data) XOR 0xFFFFFFFF`.

`test/integration/rfwu_auth/run_tests.py` içindeki 9 kontrol geçmiştir.
Bu sonuç açığın kapandığı anlamına gelmez; üç kontrol mevcut açık davranışı
bilerek doğrular. Standart dışı bir test anahtarıyla şu sonuçlar alınmıştır:

- Yanlış token ve bozuk paket CRC reddedilir; HELLO öncesi DATA ve
  QUERY/REBOOT/ABORT kapıları çalışır. TCP parçalanması desteklenir.
- Disconnect yetkiyi siler. Aynı HELLO paketi, aynı sequence ile yeniden
  gönderilince kabul edilir; sonrasında reboot callback çağrılır.
- File hash değiştirilse de eski token kabul edilir.
- Ele geçirilen HELLO alanlarından `yeni_token = eski_token XOR eski_boyut
  XOR yeni_boyut` hesaplanır. Anahtar kullanılmadan farklı boyut/hash ile
  HELLO kabul edilir; DATA yazma callback ve ABORT çalışır.

Sadece default anahtarı değiştirmek veya sequence kontrolü eklemek bu
formüldeki açığı kapatmaz. Ağdan ele geçirme için RFWU trafiğine erişim
ön koşuldur; default anahtar kullanılan cihazda kaynak/istemci bilgisi
geçerli token hesaplamak için yeterlidir. Web login kapısı RFWU yolunda
yer almaz (`web_server.c:64-77`). Firmware paketinin ECDSA imzası ayrı
bir korumadır; parser tarafından kabul edilen reboot, ABORT veya staging
alanına veri yazma işlemlerini yetkilendirmez. Keyfi firmware çalıştırma
bu testte gösterilmemiştir; bootloader/cihaz kurulumu sınanmamıştır.

Kullanıcının sonraki maddeye geçme yönlendirmesiyle protokol düzeltmesi
beklemededir. Açık kapanmış sayılmamalıdır. Çözüm cihaz ve PC aracında
birlikte uygulanmalıdır; mevcut CRC tabanlı doğrulama korunarak güvenli
kimlik doğrulama sağlandığı iddia edilmemelidir.

### 6.9 Ömür sayacı ve kayıt/reset zinciri: kapsam düzeltmesi (1.5)

**Amaç:** Önceki RAM-only iddiasını güncel altyapıyla karşılaştırmak ve
son kayıttan sonra hangi verinin kaybolduğunu belirlemek.

`app_main.c:97-100` sayacı saniyelik timer ile artırır.
`modem_config.c:13,230-238` ayrı bir config kopyası kullanmaz; getter ve
setter doğrudan NVRAM'in RAM görüntüsüne erişir (`nvram.c:1030-1038`).
`modem_config_sync()` mevcut `nvram_sync(false)` işlevini çağırır.
Bu nedenle başarılı bir config, event log, RFWU veya başka NVRAM kaydı
ömür değerini de saklar. Ömür alanı ve çift kopyalı kayıt altyapısı zaten
vardır; yeni NVRAM alanı, schema değişikliği veya ortak flash_store
katmanı bu bulguyu düzeltmek için gerekli değildir.

**Gerçek sorun:** Garantili bir düzenli kayıt yoktur. `reboot_system()`
ve `bsp_system_reset()` kayıt yapmadan reset verir. Factory reset yolu
ise defaults kaydının başarılı olmasını bekler; bütün reset yolları için
"kayıt yok" ifadesi doğru değildir. Son başarılı kayıttan sonra artan
sayaç reset/güç kesilmesinde kaybolabilir. Uzun süre kayıt yapılmıyorsa
kaybın süre sınırı yoktur; sayaç her reset sırasında mutlaka sıfırlanmaz.

**Doğrulama:** Gerçek modem_config modülünün yeni Ceedling kontrolü,
sayaç setter'ının NVRAM RAM görüntüsünü değiştirdiğini ve otomatik sync
yapmadığını kanıtlar. Gerçek NVRAM çekirdeğinin host senaryosu 120 saniye
kayıtlı değer üzerinde çalışır: kaydedilmeyen 150 değeri yeniden yüklemede
120'ye döner; kaydedilen 180 değeri korunur; A yazması silme sonrası
kesilirse son sağlam 180 değeri B'den geri yüklenir.

**Önerilen sade çözüm:** Mevcut ana process içinde saatlik
`nvram_sync(false)` ve kontrollü normal resetlerden önce bir kayıt
çağrısı kullanılmalıdır. Yeni process, NVRAM layout veya depolama katmanı
kurulmamalıdır. ISR, hardfault ve watchdog kurtarmasına bloklayıcı flash
kaydı eklenmemelidir. Kayıt hatası görünür biçimde loglanmalı; sonraki
normal periyotta yeniden denenmelidir. Saatlik başarılı kayıt varsa
ani güç kaybında son kayıt sonrası yaklaşık bir saatlik süre kaybı
kabul edilir; flash hatasında bu sınır garanti edilmez. Kayıt sıklığı ve
normal reset sırasında kayıt hatasına verilecek davranış kullanıcı
kararı beklemektedir. Bu oturumda firmware akışı değiştirilmemiştir.

**Güncel test sonucu:** Merkezi koşuda 294 Ceedling testi ve 9 integration
paketi geçmiştir. NVRAM paketi 103 kontrol, RFWU paketi 9 kontrol içerir.
Log: `test/build/production-audit-2026-10-03/rfwu-lifetime-host.log`.
Cihazda güç kesme/reset ve ARM derleme bu kaynak incelemesinde yapılmamıştır.

### 6.10 Kullanıcı kararı: mevcut NVRAM'de 25 saatlik lifetime kaydı (1.6)

Kullanıcı lifetime'ın mevcut NVRAM alanında kalmasını seçmiştir. Ayrı flash
sektörü, veri taşıma, NVRAM schema veya layout değişikliği yapılmamıştır.
Mevcut değer korunur. 6.9 bölümündeki saatlik kayıt önerisinin yerine
25 saatlik aralık uygulanmıştır.

Ana process'in mevcut saniyelik lifetime adımı `modem_config_lifetime_tick()`
çağırır. Bu işlev sayacı artırır; 90.000 saniyelik sayaç adımında
`modem_config_sync()` çağırır. Kayıt başarısızsa log verir ve sonraki
25 saatlik aralığa kadar yeniden kayıt istemez. Yeni process/zamanlayıcı
oluşturulmamıştır; sayımın mevcut heartbeat zamanlamasına bağımlılığı
korunmuştur. ISR veya fault kurtarma yoluna flash işlemi eklenmemiştir.

`periodic_reset_tick()` reset vermeden önce aynı sync işlevini çağırır.
Kayıt başarısızsa log verilir, reset sürer. Diğer normal reset yolları
bu kullanıcı kararının kapsamı dışında kalmıştır. Periyodik resetler
ve diğer NVRAM sync çağrıları toplam yazma sıklığını artırabilir;
25 saat bütün flash yazmaları için bir hız sınırı değildir. Yeni
kayıt mekanizması kurulmadığından config ve lifetime aynı CRC/görüntünün
parçası olarak kalır. Mevcut çift kopyalı kayıt korumaları korunur.

Kalıcı Ceedling testleri 25 saat sınırını, hatada saniyelik retry
olmamasını, sonraki aralıkta yeniden denemeyi ve periyodik resetten önce
sync sırasını doğrular. Kayıt hatasında resetin devamı da sınanmıştır.
Merkezi koşuda 297 Ceedling testi ve 9 integration paketi geçmiştir.
ARM Release derlemesi ve paket içerik/ECDSA öz denetimi başarılıdır.
Önceden var olan uyarılar warning-free üretim şartını karşılamamaktadır.
Cihaza yükleme veya cihaz üzerinde güç kesme testi yapılmamıştır.

Loglar: `test/build/production-audit-2026-10-03/lifetime-25h-host.log`
ve `lifetime-25h-release.log`. RFWU planı ayrı belgede değerlendirilmiştir:
`RFWU_GUVENLIK_PLANI_DEGERLENDIRME_2026-10-03.md`. Bu değerlendirme RFWU
protokol değişikliği uygulaması değildir.

### 6.11 RFWU v2 uygulaması ve review (1.7)

Kullanıcının ortak ürün anahtarı ve sade RFWU düzeltmesi yönlendirmesiyle
challenge-response uygulanmıştır. 16 bayt donanım RNG nonce'u tek kullanımlık
ve aynı bağlantıda 60 saniye geçerlidir. HELLO, "RFWU2" domain'i ile nonce,
size ve mevcut file hash üzerinde HMAC-SHA256'nin ilk 16 baytını taşır.
Eski CRC/XOR HELLO reddedilir. Beş hatalı denemeden sonra 60 saniye kilit
TCP kopuşunda korunur. NVRAM schema/layout değiştirilmemiştir.

Ortak key/header Git dışında tutulur. PC aracı aynı anahtarı yerel dosyadan
alabilir; transfer, force restart, QUERY ve ABORT girişleri yeni challenge
kullanır. Web sayfası ayrı admin HTTP yolunda kaldığından değişiklik gerekmez.
HTTP login availability fallback'i korunur; RFWU challenge hardware-only
BSP random API'sini kullanır. Ayrıntı ve ortak anahtar/aktif TCP müdahale
sınırları `RFWU_GUVENLIK_DUZELTME_PLANI_2026-10.md` 0.3 sürümündedir.

302 Ceedling testi, 9 integration paketi ve ARM Release/paket öz denetimi
geçmiştir. Gerçek parser, SHA/HMAC ve PC yardımcılarıyla 25 RFWU kontrolü
geçmiştir. İncelemede gerçek BSP tick API adı, GUI yetki bayrağı ve host
ctypes ABI uyumu düzeltilmiştir. Bu sonuç bootloader/cihaz kabulü değildir.
Genel üretime hazır kararı diğer açıklar ve gerçek cihaz testleri nedeniyle
henüz verilmemiştir. Kaynak değişiklikleri kullanıcı onayıyla commit kapsamına alınmıştır.

## 7. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-03 | 1.0 | İlk bağımsız değerlendirme; kaynak doğrulaması, host testleri, ARM derleme ve paket ölçümleri |
| 2026-10-03 | 1.1 | B seçeneği, RNG kaynaklı token, hassas log düzeltmeleri, kullanıcı kararları ve ek doğrulama sonuçları |
| 2026-10-03 | 1.2 | HAL seed kurtarmasına erişim, RNG hatasında oturum/rol korunması, sade akış ve kanıt/basitlik kuralları |
| 2026-10-03 | 1.3 | Kullanıcı onaylı RNG birikimi ve genel GSM sayaçlarıyla fallback; başlangıç, testler ve güvenlik sınırları |
| 2026-10-03 | 1.4 | Ayrı BSP random modülü, modül CMock testleri ve yazar başlığı kuralı |
| 2026-10-03 | 1.5 | RFWU replay/token türetme host kanıtı; ömür sayacının kayıt sınırı, rapor düzeltmesi ve kalıcı testler |
| 2026-10-03 | 1.6 | Lifetime mevcut NVRAM’de korunarak 25 saatlik kayıt ve periyodik reset öncesi sync; test/build sonuçları ve RFWU plan incelemesi |
| 2026-10-03 | 1.7 | RFWU v2, ortak anahtar, PC uyumu, secure RNG ve review/test/build sonuçları |
