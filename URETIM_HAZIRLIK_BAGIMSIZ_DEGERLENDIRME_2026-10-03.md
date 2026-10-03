# Troika Smart Breaker Modem — Bağımsız Üretime Hazırlık Değerlendirmesi

| Belge künyesi | Değer |
|---|---|
| Sürüm | 1.2 |
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
| RFWU REBOOT/QUERY/ABORT kapıları | **Kaynak düzeyinde doğrulandı** | `.c:416,436,468` authenticated kontrolü var; negatif uçtan uca saldırı testi yapılmadı |
| Web admin kapıları | **Kaynak düzeyinde doğrulandı** | `Application/web-server/http_server.c:109-121,258-263`; POST `/serial` dahil admin kapısından geçiyor |
| BMS kapalı ama RX açık | **Doğrulandı** | `Application/app_main.c:331-332`; UART5 ISR `Core/Src/stm32u3xx_it.c:447-456`; BMS RX buffer'ı dolar ama reader süreci başlatılmıyor |
| PowerBoard NVM stub | **Doğrulandı** | `Application/power_board/power_board.c:407-432,707`; kayıt isteği/lastgasp kalıcı saklanmıyor. Her reset sonrası gerçek SoC'nin kesin %100 olduğunu bu depo tek başına kanıtlamaz; PowerBoard firmware'i de gerekir |
| SBO kapalı / eksik | **Doğrulandı; ürün kapsamına bağlı** | C_SC_NA_1_ENABLED derleme tanımlarında yok; `Application/breaker.c:167-213` altında initialized ve IOA eşleme eksikleri. Desteklenen özellik listesinden çıkarılması ürün kararıdır |
| Lifetime ve reset flush | **Doğrulandı** | `Application/app_main.c:100`; `modem_config_set_lifetime` yalnız RAM alanını değiştiriyor; `.c:15` sync ayrı. `Application/reboot.c:18` TODO; periodic reset yolu da flush yapmıyor |
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

### 6.2 Doğrulanan sorunlar ve düzeltmeler

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

### 6.4 RNG hata yolunun sadeleştirilmesi

**Kaynak kanıtı:** Depoda kullanılan `Drivers/STM32U3xx_HAL_Driver/Src/stm32u3xx_hal_rng.c` dosyasında `HAL_RNG_GenerateRandomNumber()` mevcut seed hatasını kontrol edip `RNG_RecoverSeedError()` çağırır. Önceki BSP ön kontrolü seed hata bayrağında bu çağrıya ulaşmadan dönüyordu. Ön kontrolde yalnız clock hatası denetimi bırakılmıştır. HAL okuması sonrası seed ve clock bayraklarının denetimi korunmuştur; hata işaretli veri token üretiminde kullanılmaz.

Kullanıcı kararıyla HAL'in mevcut kurtarması kullanılmıştır. Ek retry döngüsü, otomatik reset, bakım komutu, arka plan süreci veya yedek token algoritması eklenmemiştir. Hata durumunda yalnız yeni giriş HTTP 503 ve tekrar deneme açıklamasıyla reddedilir. Geçerli oturumun tokeni, rolü ve son etkinlik zamanı değiştirilmez. Normal süre aşımı ve çıkış kuralları geçerlidir. Rol ve token yalnız yeni tokenin tamamı başarıyla üretildikten sonra güncellenir.

12 token Ceedling testi ve 16 izole host kontrolü bu güncellemeden sonra geçmiştir. BSP kontrolleri HAL için test doubles kullanır; gerçek donanım kurtarmasının başarılı olduğunu kanıtlamaz. Güncel ARM Release derlemesi ve paket içerik/imza öz denetimi geçmiştir. Cihaz üzerinde hata kurtarma henüz denenmemiştir. Kalıcı RNG arızasında yeni web girişi hâlâ reddedilir.

Kaynaklara dayalı hata sıklığı verisi bulunmadığından RNG arızası için sayısal olasılık verilmemiştir. Bir hata bayrağı tek başına kalıcı donanım arızasının kanıtı değildir. İşe başlamadan gerçek çağrı yolunun ve mevcut altyapı çözümünün kanıtlanması; yeterli olan en basit yapının tercih edilmesi kuralları `AGENTS.md` dosyasına eklenmiştir.

### 6.5 Kalan riskler ve önerilen sıra

B seçeneği token tahmin edilebilirliğini giderir; IP tabanlı admin parolası hâlâ tahmin edilebilirdir. HTTP ve URL sorgusunda token taşınması da korunmuştur; taşıma güvenliği ve tarayıcı/ara katman kayıtları ayrı değerlendirilmelidir. Bu nedenle kimlik modeli kabul kapısı bütünüyle kapanmamıştır.

Bir sonraki kaynak incelemesinde RFWU kimlik doğrulamasının replay (aynı isteğin tekrar kullanılması) riskinin gerçek çağrı zincirinde doğrulanması önerilir. Protokol davranışı değiştirilecekse seçenekler kullanıcıya sunulmalıdır. Ardından kalıcılık ve ISR/shell bulguları sırasıyla ele alınmalıdır. Üretime hazır kararı için bu açık konuların ve gerçek cihaz kabul testlerinin tamamlanması gereklidir.

## 7. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-03 | 1.0 | İlk bağımsız değerlendirme; kaynak doğrulaması, host testleri, ARM derleme ve paket ölçümleri |

| 2026-10-03 | 1.1 | B seçeneği, RNG kaynaklı token, hassas log düzeltmeleri, kullanıcı kararları ve ek doğrulama sonuçları |

| 2026-10-03 | 1.2 | HAL seed kurtarmasına erişim, RNG hatasında oturum/rol korunması, sade akış ve kanıt/basitlik kuralları |
