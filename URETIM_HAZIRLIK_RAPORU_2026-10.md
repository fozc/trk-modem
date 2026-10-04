# Troika Smart Breaker Modem — Üretim Hazırlık Raporu (Production Readiness)

| | |
|---|---|
| Rapor tarihi | 02.10.2026 |
| İncelenen sürüm | `a576862` (master, v1.0.1, çalışma ağacı temiz; izlenmeyen 4 belge dışında) |
| Taban çizgisi | `ANALIZ_RAPORU_2026-09.md` (03.09.2026, `aad8830` üzerinden, ~160 bulgu) |
| Yöntem | Eylül denetiminin Faz 0/Faz 0.5 blokörleri maddeler halinde HEAD'de yeniden doğrulandı; web güvenliği, GSM, yarım alt sistemler ve build/CI/test altyapısı olmak üzere dört alanda kaynak okuma; her bulgu dosya:satır kanıtıyla yazıldı; host testleri bu makinede koşuldu |
| Kanıt kuralı | Bu rapordaki her "KAPATILDI/AÇIK" kararı okunmuş kod satırına dayanır. Yalnızca commit mesajına dayanan veya bu turda okunamayan maddeler §6'da ayrıca listelenmiştir |

**Güncelleme — 03.10.2026:** İlk inceleme aşağıda tarihsel taban olarak
korunmuştur. Bu oturumda yapılan işlerin güncel durumu bölüm 9'dadır.
Önceki AÇIK ifadeleri bölüm 9 ile birlikte okunmalıdır. Uyarı bayrakları
hakkındaki ilk tespit de düzeltilmiştir: üretilen ARM derleme komutlarında
`-Wall` zaten vardır; bütün zorunlu uyarı ailesi etkin değildir.

Önem ölçeği (Eylül raporuyla aynı):

- **KRİTİK (K):** Üretim engeli — çökme, veri bozulması, protokol ihlali, güvenlik açığı, ürünün temel işlevinin yanlış çalışması.
- **YÜKSEK (Y):** Sahada büyük olasılıkla arıza/yanlış davranış üretir.
- **ORTA (O) / DÜŞÜK (D):** Sınır koşulu riski / bakım, tutarlılık.

---

## Yönetici Özeti

**Genel durum:** Eylül denetiminden bu yana `aad8830 → a576862` arasında ~100 commit'lik, denetim bulgularını tek tek kapatan bir düzeltme dalgası yapıldı. Denetimin altı Faz 0 kümesinden **dördü büyük ölçüde kapandı** (IEC-104 protokol bütünlüğü, GSM + yazılım güncelleme zinciri, flash çip kimliği, saat tekilleştirme) ve üç KRİTİK bulgu daha bu turda kanıtıyla kapatılmış görüldü (kopuş nedeni parametreli, flash dönüş kontrolü, JEDEC parça tablosu). Test altyapısı sıfırdan merkezi bir düzene taşındı: **272 Ceedling birim testi + 7 entegrasyon paketi bu rapor için yerelde koşuldu, tümü geçiyor**; CI 32-bit ABI doğrulaması + kapsama (coverage) üretiyor.

Ancak **üretime çıkış için hâlâ hazır değil.** Kalan engeller artık dar ve tanımlı; beş küme:

1. **IEC-104 veri kaynağı yok (en ağır fonksiyonel engel):** Üretim init'i hâlâ koşulsuz olarak kurgusal (dummy) ölçüm üretiyor (`iec104_process.c:431`); gerçek RF telemetri paketleri hiç işlenmiyor (`rf_comm.c:487` "henuz islenmiyor"); `breaker_set_feeder_data()`'nın tek çağıranı dummy üreticisi. SCADA'ya kalitesi "iyi" görünen uydurma ölçüm sunuluyor. Ayrıca arıza olay zincirinin üreticisi yok: `iec104_report_fault_event()`'in hiç çağıranı yok — fault_log ve IEC-104 olay günlüğü/replay mekanizması kurulu ama beslemsiz.
2. **Web/RFWU kimlik modeli:** Parolalar hâlâ cihaz IP'sinden türetiliyor (`http_handlers.c:369-377`); oturum tokeni tahmin edilebilir (`tick ^ sabit`, `http_handlers.c:407-410`); RFWU paylaşımlı anahtarı kaynak kodda gömülü `"SMAR"`, NVRAM alanı varsayılana düşüyor, challenge-response yok. Yumuşatıcılar: login deneme kilidi, GET admin kapısı ve fw uçlarının admin kapısı eklendi; bootloader ECDSA-P256 doğrulaması en kötü senaryoyu (keyfi firmware çalıştırma) engelliyor.
3. **Yarım kalmış alt sistem kararları:** BMS tamamen kapalı ama UART5 RX kesmesi açık (`app_main.c:331-332`); PowerBoard NVM kalıcılığı hâlâ stub (`power_board.c:407-432`); SBO/komut zinciri (`C_SC_NA_1_ENABLED`) tanımsız olduğu için bilinçli-ölü kod.
4. **Ömür/flush zinciri:** `lifetime` yalnız RAM'de artıyor, periyodik kalıcıya yazılmıyor; `reboot.c:18`'de reset öncesi NVRAM sync hâlâ `//TODO`.
5. **Üretim disiplin altyapısı:** üretilen ARM komutları `-Wall` içeriyor, fakat AGENTS.md içindeki tam uyarı ailesi etkin değil; CI ARM yazılımını derlemiyor (yalnız host testleri); CHANGELOG/release notu yok; kullanım kılavuzu 0.1 taslak; Release imzalama anahtarları (`keys/private_key.pem`, `keys/aes_key.bin`) git dışı — imaj tekrarlanabilirliği tek makineye bağlı.

**Önerilen karar:** 1 ve 2 numaralı kümeler gerçek mühendlik işi (sırasıyla veri yolu bağlama ve kimlik modeli tasarımı; her birine 2-3 gün). 3 ve 4 numaralı kümeler çoğunlukla "aç veya bilinçli kapat" kararları — birkaç günde kapanır. 5 numaralı küme süreç işi. Bu beş küme kapandığında Eylül denetiminden kalma bilinen bir KRİTİK engel kalmıyor; ardından saha doğrulaması (ARM derlemesi + cihaz üzerinde uçtan uca senaryolar) gerekiyor.

---

## 1. Eylül Denetimi Faz 0 Kapanış Dökümü

### 0.1 Güvenlik yüzeyi (Web + RFWU) — KISMEN KAPANDI

| Bulgu | Durum | Kanıt (HEAD) |
|---|---|---|
| K6.1 Parolalar IP'den türetiliyor | **AÇIK** (yumuşatıldı) | `http_handlers.c:369-377`: hâlâ `"admin%d", ip_d + 1` / `"user%d", ip_a + 1`; NVRAM'de web kimlik bilgisi alanı yok. Login deneme kilidi **eklendi**: 5 hatalı girişte 60 sn kilit (`http_handlers.c:53-57, 322-331, 398-399`, commit `3de80f5`). `doc/Web_Giris_Sifresi_Plani.md` (R2) IP türetmesini bilinçli tasarım olarak belgeliyor — risk kabulü yazılı karar gerektirir |
| K6.2 Oturum tokeni tahmin edilebilir; URL'de taşınıyor; konsola basılıyor | **AÇIK** | `http_handlers.c:407-410`: `bsp_get_tick() ^ 0x5A5A0000UL ^ username[0]`; donanım RNG kullanımı yok (grep: `rng`/`trng` Application/ içinde sıfır vuruş). Token hâlâ `?t=` query'de (`http_server.c:84-91`) ve konsolda (`http_handlers.c:415`); yanıt gövdesi dökümü `#if 1` ile derlemede, fabrika log seviyesi VERBOSE (`nvram.c:255`) olduğundan default açık (`http_response.c:126-132`) |
| K6.3 RFWU REBOOT/QUERY/ABORT kimliksiz | **KAPATILDI** | `raw_tcp_fw_update.c:416-419, 436-439, 468-471`: üç komut da `if (!s.authenticated) send_nack(RFWU_ERR_AUTH)`; oturum HELLO ile kurulup kopuşta temizleniyor (`:285, :616`, commit `d5947e3`) |
| K6.4 RFWU anahtarı gömülü `"SMAR"`, replay'e açık | **AÇIK** (etkisi sınırlı) | `raw_tcp_fw_update.h:54-55`: `RFWU_DEFAULT_SHARED_KEY 0x534D4152`; NVRAM alanı (`types.h:196`) hiç ilklendirilmiyor, kod 0 göründe default'a düşürüp NVRAM'e **default'u yazıyor** (`raw_tcp_fw_update.c:242-243, 270`); token = `CRC32(key) ^ total_size`, nonce yok. Yumuşatıcı: bootloader ECDSA-P256 imza doğrulaması (Eylül §0b) keyfi firmware kurulumunu engelliyor; kalan risk SPI flash'a çöp yazma/DoS |
| Y6.5 GET uçlarında admin kapısı yok | **KAPATILDI** | `http_server.c:109-121`: `/config/device`, `/config/iec104`, `/syslogs`, `/serial` admin-only; fw POST uçları (`/fw_start`…`/fw_apply`) blanket admin kapısında (`http_server.c:258-263`, commit `cdb7067` + `af8cb0d`). SIM PIN / APN şifresi GET yanıtında hâlâ maskesiz (`http_handlers.c:569-572`) ama artık admin arkasında |

**Bu turdaki değerlendirme:** Eylül raporunun "internet'ten fiilen anonim admin" senaryosu deneme kilidi + admin kapılarıyla zayıfladı; ancak parola/token modeli değişmedikçe kimlik doğrulama hâlâ cihaz IP'sini bilen herkese karşı zayıf. CSRF yok, şifre değiştirme ucu yok (grep, sıfır vuruş).

### 0.2 IEC-104 RX/TX bütünlüğü — KAPANDI (önceki turlar), veri kaynağı AÇIK

- K5.1–K5.5, Y5.6, Y5.10, O-O1: Eylül §0/§0c'de kapatıldı; loopback host testi (11 senaryo/40 assertion, mutasyonla doğrulanmış) test setine dahil — bugün `test/iec104/test_iec104_protocol_scenario.c` CI'da koşuyor.
- Y5.12 kopuş nedeni hep `reason=0`: **KAPATILDI** — `iec104_elog_disconnected(iec104_elog_disc_reason_t reason)` parametreli (`iec104_elog.h:71-91`), çağıran `map_disc_reason()` ile besliyor (`iec104_process.c:83`).
- Y5.9 koşulsuz dummy veri: **KISMEN** — yapı sıfırlama ve geçerli sabit zaman damgası düzeltildi (commit `d24a3fc`), geçersiz CP56 damgalarını IV ile işaretleme altyapısı geldi (`cp56time2a.c:199-207`, commit `bc59de7`). Ancak `generate_dummy_test_data()` üretim init'inde **koşulsuz** çağrılıyor (`iec104_process.c:431`), dummy veri IV işaretli değil ve **gerçek veriyi bekleyen hiçbir yol yok** (aşağıda 3.1).
- Y5.11 C_DC/C_SC komutları: derleme anahtarı `C_SC_NA_1_ENABLED` her iki konfigürasyonda da tanımsız (`.cproject:46-50, 153-157`) → komut zinciri ölü kod; "destekleniyor" listesinden çıkarılıp çıkarılmadığı bu turda doğrulanmadı (§6).

### 0.3 GSM kalıcılığı ve fw-update zinciri — KAPANDI

| Bulgu | Durum | Kanıt (HEAD) |
|---|---|---|
| K4.1 `#SGACT` CR ofseti | **KAPATILDI** | `gsm_engine.c:3108-3120`: kimlik eki artık mutlak ofsetle `xsnprintf(at_buff + at_len, sizeof(at_buff) - at_len, ...)`; CR yazımı öncesinde uzunluk koruması `gsm_engine.c:3475-3484` (`at_len + 2U > sizeof(at_buff)` → red + `gsm_set_free()`), `buff_ptr[at_len-2]` == `at_buff[at_len]` artık doğru konum |
| K4b.1 `gsm_reset_process_old` tek listener | **KAPATILDI** | `gsm_process.c:305-315`: `for (i < GSM_LISTENER_COUNT)` her iki listener'ı (WEB + IEC104) IDLE'a çekiyor; öncesinde AT motoru/BUYUK kilidi de temizleniyor (`:291-293`) |
| K4b.2/K3.3 fw hedef bölmesi | KAPATILDI (Eylül §0b, `f8cc9b8`) | — |
| K3.5 XMODEM flash dönüşleri + WDT | KAPATILDI (Eylül §0b, `8a28da1`) | — |
| Y4.2 AT gönderim dönüşleri | **KAPATILDI** (canlı yollar) | `gsm_engine.c:3489-3498` gönderim başarısızlığında `query_state` kurulmuyor; socket send yolları dönüş kontrollü (`gsm_engine.c:1919-1927, 1939-1947`; çağıranlar `gsm_listener_process.c:371, 417`). Kalıntı: çağrılmayan `gsm_engine_send_at_cmd` (`gsm_engine.c:2916`) dönüşü yok sayıyor — ölü kod, silinmeli |
| O4b.3 stuck-FREE kilitleri | **KAPATILDI** | `gsm_wtd.c:67-121, 241`: liveness damgası + 10 dk sessizlik → kademeli kurtarma (`gsm_request_module_restart` ×2, sonra hard reset); damga AT motorunda FSM sonrası noktada vuruluyor (`at_engine2.c:892-901`, commit `6237015` + `c4c9bf67`) |
| K3.6 `set_imei` uzunluk | **KAPATILDI** | `modem_config.c:335-350`: sınırlı tarama + zorla NUL |

### 0.4 Flash çip kimliği çelişkisi — KAPATILDI

`w25qxx.c:199-235`: JEDEC kimliği artık bilinen-parça tablosuyla doğrulanıyor (`s_known_flash[]`: AT25SF321B 4 MB board parçası ilk sırada, `:211-213`); ve `app_main.c:244` **dönüşü kontrol ediyor** (`if (w25qxx_init() != W25QXX_RES_OK)`). Eylül raporundaki "koruma ölü" maddesi de böylece kapandı (commit `5d54d18`).

### 0.5 Yarım alt sistemler — KARAR BEKLİYOR (aşağıda §3)

### 0.6 Saat tekilleştirme — KAPATILDI

- `time_service_tick()` artık TIM17 kesmesinde çağrılıyor (`bsp.c:39-41`); SBO seçim saati `millis()` ile yazılıp aynı kesmede artan `system_ticks` ile karşılaştırılıyor — iki saat artık kilit adım (`iec104.c:651, 663`, `time_service.c:59-62`).
- 49,7 gün sarması: epoch artık wrap-muhasebeli 64-bit toplamdan türetiliyor (`life_timer_total_ms()`, PRIMASK korumalı — `bsp.c:156-173`).
- RTC makul-taban: 2026-01-01 öncesi zaman kaynağı reddediliyor (`rtc.c:28 RTC_EPOCH_MIN`, `rtc.c:292-300`, commit `326fc0f`); üretim çağıranları `iec104.c:763`, `gsm_engine.c:1368, 2811`.

---

## 2. Faz 1 Seçilmiş Maddeler (HEAD durumu)

| Bulgu | Durum | Kanıt |
|---|---|---|
| K3.2 lifetime kalıcılığı | **AÇIK** | `app_main.c:97-101`: saniyelik artış yalnız RAM; `modem_config_sync()` çağıranları yalnız SIM numarası değişimi (`gsm_engine.c:1578`) ve web config kaydı (`json_config.c:2123`) — ikisi de periyodik değil |
| Y3.10 reset öncesi flush | **AÇIK** | `reboot.c:18`: `//TODO: nvram sync()` hemen `NVIC_SystemReset()` önünde; `periodic_reset.c` reset öncesi sync yapmıyor |
| K10.1 shell RX çift bağlam | **KISMEN** | Shell dosyası bootloader ile bayt-pariteye alındı (commit `4429fc1`), ama RX durum makinesi (`shell.c:56-61`) hâlâ hem LPUART1 ISR (`stm32u3xx_it.c:503-504`) hem web terminal (`web_shell.c:113-117`) beslemeli; `session_level` tek global (`shell.c:58`) |
| Y2.2 ARM uyarı bayrakları | **KISMEN / AÇIK** | 03.10.2026 düzeltmesi: `.cproject` içinde açık option görülmemesi derleyicinin uyarısız çalıştığını kanıtlamaz. Üretilen Debug/Release `Application/web-server/subdir.mk` komutları `-Wall` içerir. `-Wextra/-Wshadow/-Wconversion/-Wdouble-promotion/-Wformat=2/-Werror` ailesinin tamamı ARM tarafında etkin değildir. Uyarı düzeltmeleri ve kalan inceleme için bölüm 9.1/9.3 geçerlidir. |
| Y2.3 heap rezervi | **AÇIK** | `_Min_Heap_Size = 0x200` üç linker betiğinde de (`STM32U375VETX_{FLASH,BOOT,RAM}.ld:43`); `Core/Src/sysmem.c` çalışır `_sbrk` içeriyor |
| Y3.9 version.h elle bakım | **DEĞİŞTİ (iyileşti)** | Kimlik artık boot superblock'dan okunuyor (`boot.c:162`; tüketenler `elog.c:901`, `http_handlers.c:546`); EFW paketi `bin2efw.py -H version.h` ile sürüm + git hash gömüyor. Kalıntılar: `http_handlers.c:1738` sabit fallback `"1.0.0"` (setter'ının çağıranı yok), öksüz ve kırık araçlar `tools/update_git_hash.py` + `tools/post-build.py` |
| Y6b.2 Modbus hat sayısı belgesi | **KAPATILDI** | Kayıt haritası v1.6 (2026-09-25): 7 fider nihai, `ibus_ma` INT16, sıcaklık sentinel -9990 — kod/belge uzlaşmış (commit `f71e50c` + `c74053e`) |
| Y6b.4 "Aku uyarı" sabit 23 | **AÇIK** | `modbus_process.c:434`: `TODO: bind to the real battery-warning source` duruyor |
| Y5.8 `iec104evtlog` komut çakışması | KAPATILDI (Eylül §0, `a6aac14`) | — |
| Y10.2 ftoa eşiği + xprintf/xscanf testleri | **KAPATILDI (test tarafı)** | `test/libs/test_xsnprintf*.c`, `test_xscanf*.c` test setinde; sınır senaryosu x87 hesabına uyumlu (`427f3cc`) — bu turda koşuldu, geçiyor |

**Host testleri (bu rapor için koşuldu, 02.10.2026, Windows/x64 LP64):** `ruby test/run_all.rb all` → **Ceedling 272 test / 0 başarısız** (JUnit: `tests="272" failures="0"`), 7 entegrasyon paketi (contiki_process, fault_log, gsm, libs, nvram, rf_hub_sim, web_navigation) ve rf-hub-sim 74/74 — **tümü PASS**. CI eşdeğeri Linux/32-bit (`host-tests.yml`: ABI `_Static_assert` + ELF32 doğrulaması + gcovr kapsama).

---

## 3. Kalan Üretim Engelleri (önerilen öncelik sırasıyla)

### 3.1 IEC-104 veri kaynağı zinciri — KRİTİK (fonksiyonel)

Ürünün temel işlevi SCADA'ya gerçek ölçüm/olay sunmak; şu an üçü de kopuk:

1. **Dummy üretimi koşulsuz:** `iec104_process.c:431` her açılışta `generate_dummy_test_data()` — 0.5 A'dan 62.5 A'ya kurgusal arıza akımları, kalite bitleri "iyi", geçerli görünen sabit zaman damgası (`iec104_process.c:95-106`).
2. **Gerçek RF telemetri yolu yok:** `rf_comm.c:487` bilinmeyen paket türleri "proactive (henuz islenmiyor)" loguyla düşüyor; `rf_set_monitor()`'un tek çağıranı inert dummy modülü (`rf_dummy.c:352`); `breaker_set_feeder_data()`'nın tek çağıranı dummy üreticisi (`iec104_process.c:123`). Yani RF haberleşme açılsa bile veri modeline ulaşmıyor.
3. **Olay üreticisi yok:** `iec104_report_fault_event()` (`iec104_replay.c:189`) hiçbir üretim kodu tarafından çağrılmıyor; fault_log ve IEC-104 olay günlüğü/replay makinesi (`iec104_replay.c:181-219`) kurulu ama yalnız shell testi besliyor.

**Seçenekler (karar gerektirir):** (a) RF telemetri işleyicilerini bağlayıp dummy'yi kapatmak — tam işlev; (b) ilk sürümde fider nesnelerini IV=1 (geçersiz) kaliteyle yayıp dummy'yi kaldırmak — dürüst ama işlevsiz; (c) dummy'yi derleme anahtarına bağlamak ve belgelemek — riskli, önerilmez. Her durumda `iec104_report_fault_event`'in üreticileri (PowerBoard/kesici arıza kaynakları) tanımlanmalı ya da modül üretimden çıkarılmalı.

> RFWU replay/sahtecilik zafiyeti 03.10.2026'de sayısal kanıtla doğrulandı ve tam kapanış planı ayrı belgeye taşındı: bkz. `RFWU_GUVENLIK_DUZELTME_PLANI_2026-10.md` (challenge-response + HMAC-SHA256 + 128-bit anahtar; iş listesi, kabul kriterleri, host test planı).

### 3.2 Web/RFWU kimlik modeli — KRİTİK (güvenlik)

§0.1'deki kanıtların özeti: parola IP-türevli (bilgili karar belgesi var ama risk açık), token tahmin edilebilir + URL'de + konsolda, RFWU PSK gömülü/nonce'suz, yanıt gövdesi dökümü fabrika ayarında açık. STM32U375'te donanım RNG mevcut olmasına rağmen hiçbir yerde kullanılmıyor. En düşük maliyetli paket: parolayı ilk açılışta RNG ile NVRAM'e üret + seri etiketine bas; token RNG'den; RFWU PSK cihaza özgü + HELLO'da nonce; gövde/token dökümlerini üretimde derlemeden çıkar.

### 3.3 Yarım alt sistem kararları — YÜKSEK

| Alt sistem | Durum | Yapılacak |
|---|---|---|
| BMS | `bms_reader_init()` yorumda (`app_main.c:331`) ama UART5 RX kesmesi açık (`:332`) — bayt akıyor, hiç işlenmiyor; Modbus BMS bloğu sonsuza dek sıfır | **Aç** (init+süreç+tüketici; Eylül Y8.4-Y8.7 hâlâ geçerli) **veya tam kapat** (kesme dahil) — tek satırlık gerekçe yorumuyla |
| PowerBoard NVM persist | Stub, yalnız log (`power_board.c:407-432`, `TODO(NVM)` ×2) — her reset sonrası SoC %100 | Aşınma dengeli küçük bölgeye RESTORE görüntüsü (Eylül çözüm önerisi geçerli) ya da ilk sürümde bilinçli erteleme kararı |
| SBO/komut zinciri | `C_SC_NA_1_ENABLED` tanımsız; `breaker_init` bayrağı set etmiyor (`breaker.c:102-112`); IOA eşleşme TODO (`breaker.c:180,197`) | Uçtan uca aç ya da destek listesinden çıkar + ölü kod temizliği |
| Power panic | **KAPATILDI** — PE15 EXTI15 rising/falling (`main.c:1137-1154`), PA7 aynalama ISR girişte+çıkışta (`stm32u3xx_it.c:202-246`), bölüm (episode) modeli, tek-atımlı log (`power_panic.c`). Sınırlar: panik yolunda GSM susturma ve NVM persist yok; **PA7/RF_IO1 hattının donanımsal çakışmazlığı teyit edilmemiş** (§8) | Donanım teyidi + istenirse susturma/persist adımı |

### 3.4 Ömür/flush zinciri — YÜKSEK

Lifetime RAM-only (§2 tablosu); reset yolları dirty-flush yapmıyor (`reboot.c:18` TODO). Çözüm: saatlik düşük-sıklıklı sync + reset öncesi tek noktadan flush (Eylül K3.2/Y3.10 önerisi aynen geçerli; `flash_store` ortak çekirdek speki `nvram-common.md` Sürüm 3 hazır ama implementasyon adımı atılmamış).

### 3.5 Üretim disiplin altyapısı — YÜKSEK (süreç)

1. **Uyarı bayrakları:** `-Wall` üretilen ARM komutlarında zaten vardır. Diğer zorunlu uyarılar mevcut envanter üzerinden kademeli değerlendirilmelidir. Host tarafındaki tam seçenekler ARM tarafında henüz tamamen etkin değildir; bütün seçenekleri birden açıp mevcut derlemeyi durdurmak kapanış sayılmaz.
2. **CI'da ARM derlemesi yok:** `host-tests.yml` yalnız host testleri derliyor. CubeIDE headless derlemesi Release imajını da kurmalı (imza/şifreleme adımı anahtar yönetimine bağlı — aşağıda 4.4).
3. **CHANGELOG yok; kök README yok;** kullanım kılavuzu "0.1 Taslak" (`doc/kullanim_kilavuzu/…`, 27.09.2026). Sürüm v1.0.1'e bump edilmiş — yayına giderken changelog + kılavuz 1.0 şart.
4. **Anahtar yönetimi:** Release post-build `keys/private_key.pem` (ECDSA) ve `keys/aes_key.bin` kullanıyor; ikisi de git dışı (izlenecek dosya listesinde yalnızca public key başlıkları var). Gizlilik açısından doğru; ancak anahtar kaybı/yedekliği/usulü belgelenmediği sürece imaj tekrarlanabilirliği tek geliştirici makinesine bağlı.
5. Kalan hijyen: `_Min_Heap_Size=0x200` + `_sbrk` (heap yasağı politikasıyla çelişki), 50 TODO / 8 `#if 0` bloğu (AGENTS.md "yorumlanmış kod yasak"), öksüz araçlar (`update_git_hash.py`, `post-build.py`), bayat include yolları (`../libefw`, `../Application/httpserver`), boş `test/unit/*` dizinleri.

---

## 4. Altyapı Durumu

### 4.1 CI (`.github/workflows/host-tests.yml`)

Tek iş: Ubuntu 24.04, PR + push. 32-bit ABI doğrulaması (`sizeof(void*)==4`, ELF32, `-Wall -Wextra -Werror`), `ruby test/run_all.rb all`, JUnit XML varlık kontrolü, gcovr kapsama (Cobertura + HTML), rapor yükleme. **ARM yazılımı derlenmiyor.** Son çalışma durumu bu makineden sorgulanamadı (`gh` yok); eşdeğer komut yerelde PASS (§2).

### 4.2 Test envanteri

27 Ceedling birim test dosyası (application 7, libs 6, iec104 3, efw/gsm/rf/scp/web_server 2'şer, power_board 1) + 7 entegrasyon paketi + Node tabanlı web_navigation. **Kapsam dışı kalanlar:** libmodbusrtu (tüm Modbus katmanı), gsm_engine/at_engine (yalnız ring_buff + wtd liveness testli), http_handlers/json_config (yalnız parser + rf_json), bsp, app_ipc, xmodem, iec104_process/replay, boot, elog, app_main. Eylül §11 önerisinin (modbus adres aritmetiği testi) hâlâ karşılıksız olduğu görülüyor.

### 4.3 Dokümantasyon

MODBUS kayıt haritası v1.6 (kod ile uzlaşmış). Donanım test rehberi + hwtest skill + betikler mevcut. Kullanım kılavuzu taslak. CHANGELOG/README yok. `nvram-common.md` (flash_store ortak çekirdek, Sürüm 3) yalnız sözleşme — implementasyon bekliyor.

### 4.4 Release zinciri

Release post-build: `bin2efw.py --sign-key keys/private_key.pem --encrypt-key keys/aes_key.bin --compress --thumb` → imzalı + şifreli + Thumb-BCJ sıkıştırmalı EFW; bootloader ECDSA-P256 + CRC + vektör denetimi ile kurulum (Eylül §0b'de kanıtlandı). Web sayfaları pre-build'de üretiliyor ve üretim tekrarlanabilir (commit `712c797`). Kimlik (sürüm/hash) EFW meta verisinde ve boot superblock'ta taşınıyor.

---

## 5. Bu Turda Yeni Doğrulanan Bulgular

| No | Önem | Bulgu | Kanıt |
|---|---|---|---|
| N1 | ORTA | Web arayüzünde GSM sinyal göstergesi ham CSQ (0-31) değerini **"dBm" etiketiyle** gösteriyor. CESQ yolu doğru çeviriyor (rxlev-111, rscp-121, rsrp-141; `gsm_info.c:444-563`), Modbus doğru belgeli (`modbus_gsm_stats.c:39` "raw 0..31") — yanlış yalnız web: `system_status.c:85-86` → `http_handlers.c:679 "GsmSig"` → `web-page/index.html:309 ['GsmSig',…,'dBm']` | Okuma + kod yolu |
| N2 | ORTA | RFWU `shared_key` NVRAM alanı hiç ilklendirilmiyor; kod 0 göründe gömülü default'a düşürüp default'u NVRAM'e kalıcı yazıyor — "cihaza özgü anahtar" hedefiyle çelişen davranış | `types.h:196`, `raw_tcp_fw_update.c:242-243, 270` |
| N3 | ORTA | HTTP yanıt gövdesi dökümü `#if 1` ile derlemede; default log seviyesi VERBOSE olduğundan üretimde konsol trafiği ve gecikme üretir | `http_response.c:126-132`, `nvram.c:255` |
| N4 | DÜŞÜK | `http_handlers.c:1738` sabit `fw_version = "1.0.0"` fallback; üstelik setter'ının çağıranı yok — v1.0.1 görüntüsünde yanlış sürüm göstergesi riski | okuma + grep |
| N5 | DÜŞÜK | SSENDEXT/si_all_zero koruması `#if 0`'da ama besleyen veri canlı tutuluyor — bilinçli erteleme; karar ve tarih belgelenmeli | `gsm_listener_process.c:257-280` |
| N6 | DÜŞÜK | Test ağacı artıkları: boş `test/unit/*` (8 dizin), yalnız build artığı integration dizinleri, `test/integration/nvram` içinde adı `-p` olan dosya | dizin listesi |
| N7 | DÜŞÜK | `rf_dummy.h:14` "RF_DUMMY_ENABLE tanımlı değilse no-op" diyor; böyle bir makro yok — başlık kodu/yorum uyumsuzluğu (runtime `initialized` bayrağı fiilen inert) | `rf_dummy.c:328, 340` |

---

## 6. Bu Turda Doğrulanamayanlar (Eylül raporunda açık kalan)

Aşağıdaki maddeler Eylül raporunda açık görünüyor ve bu tur HEAD'de kanıt okunmadı; kapanış/idame kararı ayrı bir tur gerektirir:

- Y2.1 `int_master_enable` ARM dalı (latent tuzak) — derlenme koşulu değişmedi mi, kontrol edilmedi.
- Y6b.1 baud değişimi erteleme; Y6b.5 RS-485 echo koruması (donanıma bağlı).
- Y9.3 seq sarması boş-log okuma hatası; O9.7 ring erase WDT kick'i (commit `3913163` yalnız `log_read_all` taraması için kick ekliyor görünüyor — ring erase yolu doğrulanmadı).
- Y-Y4 shell komut tablosu sınırı; T-T1 (IEC-104'te beş host uyarısı).
- Y5.11'in belge tarafı: "desteklenen tipler" listesi temizliği.
- O5.15 katman sızıntısının veri kaynağı bağımlılıkları (Eylül §0c'de bilinçli taşınmış).
- Dialer ölü kod paketi (O4.4) durumu.

---

## 7. Üretime Çıkış Kontrol Listesi

Önerilen sıra (bağımlılık ve risöre göre):

1. **IEC-104 veri kaynağı kararı** (§3.1) — RF telemetri bağlama **veya** IV=1 dürüst yayım; `iec104_report_fault_event` üreticileri. *Kabul:* SCADA'ya kurgusal "iyi" kaliteli ölçüm gitmiyor; arıza olayı oluştuğunda evtlog + replay çalışıyor.
2. **Web/RFWU kimlik modeli** (§3.2) — RNG parola/token, cihaza özgü PSK + nonce, üretimde döküm kapalı. *Kabul:* cihaz IP'sini bilen kişi admin olamıyor; RFWU token'i replay'lenemiyor.
3. **Yarım alt sistem kararları** (§3.3) — BMS aç/tam-kapat, PowerBoard persist, SBO zinciri, PA7/RF_IO1 donanım teyidi. *Kabul:* her satırda ya çalışan zincir ya tek-satırlık bilinçli-kapat gerekçesi.
4. **Ömür/flush** (§3.4) — lifetime periyodik persist + reset öncesi flush. *Kabul:* periyodik reset sonrası ömür sayaçları süreklilik gösteriyor.
5. **Disiplin altyapısı** (§3.5) — uyarı bayrakları envanteri ve işletmesi, CI'a ARM derlemesi, CHANGELOG + kılavuz 1.0, anahtar usulü, heap rezervinin kaldırılması, TODO/`#if 0` envanteri.
6. **Saha doğrulaması** — Eylül raporunun şartı hâlâ geçerli: IEC-104 düzeltmeleri ve power-panic dahil hiçbir tur **ARM'da derlenip cihazda doğrulanmış sayılmamalı**; uçtan uca senaryolar (SCADA bağlantısı kes/tekrar bağla, OTA A→B, güç kesintisi, EWDT pencresi).

## 8. Açık Sorular (donanım / dış karar — tarafınızca doğrulanmalı)

1. **PA7/RF_IO1 hattı:** power-panic aynalaması PA7'yi (RF_IO1) sürüyor — RF tarafıyla çakışma riski donanımda teyit edilmiş değil.
2. **EWDT zaman penceresi:** uzun flash işlemlerinde (sector erase 45-400 ms) reset riski; S2'nin kalan kick'leriyle birlikte değerlendirilmeli.
3. **RS-485 transceiver RX-mute davranışı** (kendi gönderimini duyuyor mu — Y6b.5).
4. **Modbus "aku uyarı" kaynağı** (Y6b.4): gerçek koşul hangi sinyalden türetilmeli (PowerBoard alarm biti mi SoC eşiği mi)?
5. **RF kanal parametrelerinin kaynağı** (`rf_inventory.c:108` TODO: "BOLATeX'e sorulacak").

---

## Sonuç

Kod tabanı Eylül denetimine göre belirgin ve ölçülebilir biçimde olgunlaştı: protokol dizisini ve güncelleme zincirini koruyan otomatik testler geldi, GSM'in en tehlikeli üç hatası kapandı, saat ve flash kimlik sorunları kökten çözüldü, power-panic gibi bir donanım yolu gerçekten kuruldu. Kalan iş iki başlıkta toplanıyor: **(1) ürünün veri ve olay zincirini gerçek kaynağa bağlamak** (bu olmadan cihaz SCADA'ya kurgusal veri sunar) ve **(2) kimlik modeli + süreç sertleştirmesi** (bu olmadan cihaz internete açılamaz ve sürüm süreci tekrarlanamaz). Her iki başlık da tanımlı, kanıtlanmış bulgu listeleriyle önünüzde; tahmini efor sırasıyla 2-3 gün ve 2-3 gün + süreç işleri. Bunların ardından bilinen-engelsiz bir ilk saha sürümü için engel kalmıyor; nihai kapı ARM derlemesi ve cihaz üzerinde uçtan uca doğrulamadır.

## 9. 03.10.2026 güncellemesi — yapılan işler ve kalanlar

### 9.1 Yapılan işler

Bu bölüm mevcut kaynak kod ve host doğrulamalarına dayanır. Kullanıcının
bilinçli ertelediği işler tamamlanmış sayılmamıştır.

| Konu | Güncel durum | Kaynak / kapsam |
|---|---|---|
| Web oturum tokeni | **DÜZELTİLDİ** | `http_session_token.h` doğrudan `bsp_random_word()` kullanır. RNG/fallback altyapısı `Application/bsp/bsp_random.c` içindedir. Main'in HAL başlangıcını tekrarlamaz. Fallback erişilebilirlik içindir; donanım entropy güvencesi sayılmaz. |
| Hassas web logları | **DÜZELTİLDİ** | Token/yanıt gövdesi dökümü aktif yoldan çıkarılmıştır; `http_response.c` yalnız yanıt durumu ve uzunluğunu loglar. Bu durum URL'de token kullanımını ortadan kaldırmaz. |
| RFWU kimlik doğrulaması | **V2 UYGULANDI** | `raw_tcp_fw_update.c/.h`: tek kullanımlık challenge nonce, HMAC doğrulaması, boyut/resume kimliği bağlama ve timeout vardır. RNG yoksa challenge reddedilir. Web RFWU istemcisi v2'ye uyarlanmıştır. Cihaz başı anahtar bu turda kullanıcı kararıyla ertelenmiştir. |
| Lifetime kalıcılığı | **İSTENEN KAPSAM TAMAM** | `modem_config_lifetime_tick()` 25 saatte bir sync dener; `periodic_reset.c` periyodik reset öncesi sync yapar. Lifetime mevcut NVRAM alanında kalır; ayrı flash alanına taşınmamıştır. Her reset yolunun flush yaptığı iddia edilmez; `reboot_system()` TODO'su durur. |
| Format ve normal derleme uyarıları | **DÜZELTİLDİ / SINIRLI KAPSAM** | Çıktı tipleri, unsigned sabitler, kullanılmayan aktif bildirimler ve xscanf başlangıç değeri düzeltildi. Contiki kernel kapsam dışıdır. Ayrıntılı ilk envanter ve sonuçlar `DERLEYICI_UYARILARI_DEGERLENDIRME_2026-10-03.md` içindedir. Bütün sıkı dönüşüm uyarıları kapanmış sayılmaz. |
| HTTP uzunluk tipleri | **DÜZELTİLDİ / SINIRLI KAPSAM** | JSON üreten 10 handler boyut/konum için size_t kullanır; gönderim API’si korunur. Baştan etkin uyarılı Release derlemesinde toplam 1298 → 487, HTTP 902 → 28. Son devam derlemesinde toplam 249 uyarı kaldı. Kanıt ve sınırlar uyarı değerlendirmesi bölüm 13–15’tedir. |
| Varsayılan adres ve GSM dönüşümleri | **DÜZELTİLDİ / SINIRLI KAPSAM** | NVRAM adres hesap sınırı derleme zamanı kontrolüyle korunur; bütün varsayılan nokta adresleri test edilir. GSM metin uzunluğu ve AT komut uzunluğu tipleri uyumludur; normal komut ve uzun APN reddi Ceedling’de doğrulanır. Kalan bit alanı/parser uyarıları kapatılmış sayılmaz. |
| JSON alan genişlikleri | **DÜZELTİLDİ** | CommonAddr 16 bit, OriginatorAddr 8 bit; SBO saklama sınırı; Modbus 21 alan ve global adreslerin 16 bit sınırı parser/setter'da doğrulanır. Web aynı sınırlara uyarlanmıştır. |
| SCADA/GSM IP hesapları | **DÜZELTİLDİ** | SCADA IP unsigned hesaplanır. GSM listener IP'si packed alana pointer verilmeden yerel değişken üzerinden yazılır. GSM cevap senaryoları Ceedling'e eklenmiştir. |
| Modbus reset nedeni | **DÜZELTİLDİ** | Register 49009 mevcut reset nedeni bitmask API'sini kullanır. Register adresi ve 16 bit boyutu korunur. Harita v1.7'de maskeler açıklanmıştır; negatif sıcaklık word kodlaması regression testiyle korunur. |
| IEC104 IOA çakışmaları | **DÜZELTİLDİ** | 21 nokta/fider, iki global IOA ve geçici/kalıcı kayıt aralıkları kayıt öncesi doğrulanır. Yeni fabrika arıza bölgeleri ayrılmıştır. Mevcut cihaz adresleri otomatik yeniden numaralandırılmaz. |
| IEC104 arıza tabanlarının korunması | **DÜZELTİLDİ** | Kısmi HTTP güncellemesinde mevcut tabanlar yüklenir; setter mevcut fider yapısını korur. TemporaryFaultBase/PermanentFaultBase web'den okunup değiştirilebilir. NVRAM şeması değişmemiştir. |
| Web Save öncesi kontrol | **DÜZELTİLDİ** | IEC104'ün 23 fider adres alanı ve Modbus'un 21 fider alanı kategori/faz/fider ayrımı olmadan karşılaştırılır. Çakışan iki alan kırmızı işaretlenir; diğer alanın fider/faz/etiketi gösterilir; hata düzeltilmeden POST gönderilmez. İki word kullanan Modbus alanlarının örtüşmesi de engellenir. |
| Modbus adres açıklaması | **EKLENDİ** | Her fider adresinin altında FLOAT32/UINT16, register sayısı ve canlı adres aralığı gösterilir. Çakışma mesajı ortak register adresini ve diğer alanı belirtir; 40001/40002 örneği test edilmiştir. |
| Modbus backend çakışma koruması | **DÜZELTİLDİ** | Aynı adres ve iki-register örtüşmesi parser/setter'da da reddedilir. Mevcut sıfır/atanmamış adres sözleşmesi korunur. Geçersiz config yazılmadan reddedilir. |
| IEC104 dokümantasyonu | **EKLENDİ** | `doc/IEC104_YAPILANDIRMA_VE_ADRES_REHBERI.md`: nokta tipleri, varsayılan adresler, kayıt hesabı, GI/replay ve henüz tamamlanmamış yollar açıklanır. |

Bu tabloda anlatılan mevcut çakışma kontrolü **aktif fiderleri** kapsar.
Kapalı fiderlerde saklanan adreslerin de benzersiz olması için kullanıcıya
soru iletilmiştir; henüz cevap alınmamıştır. Bu kapsam genişletilmemiştir.

### 9.2 Testler ve doğrulama

| Kontrol | Sonuç ve sınır |
|---|---|
| Son tam Ceedling çalışması | **377/377 geçti**; `test/build/production-audit-2026-10-03/remaining-ceedling.log` |
| Son tam entegrasyon çalışması | **9/9 paket geçti**; `iec-modbus-save-tests.log`; sonrasında eklenen bütün-alan web senaryoları kaynak ve gömülü HTML üzerinde ayrıca geçti |
| ARM Release | **Derlendi**; son dönüşüm düzeltmeleri dahil baştan `remaining-full-release.log`. Bu sonuç tam zorunlu uyarı ailesi altında warning-free sonucu değildir. |
| JSON/config testleri | `test/web_server/test_json_config_bounds.c`, **22 senaryo**: gerçek parser/setter; limitler, kısmi kayıt, bütün alanların çakışması, geçersiz girdide kayıt çağrısının olmaması |
| Modbus register testleri | `test/application/test_modbus_system_stats.c`: gerçek register okuma, bitmask ve signed sıcaklık kodlaması |
| GSM cevap testleri | `test/gsm/test_gsm_response_scenario.c`: gerçek callback/parser; marker ve listener IP senaryoları |
| IEC104 adres hesabı | `test/iec104/test_iec104_protocol_scenario.c`: gerçek getter'ların yedi fider için ilk/son adresleri |
| Fabrika varsayılanları | Mevcut `test/integration/nvram/test_nvram_sync.c` gerçek NVRAM başlangıç kodunu kullanır |
| Canlı Modbus aralığı | Kaynak ve gömülü HTML testleri: 21 alanın tip/register sayısı, başlangıç ve değişen değer, boş alan, 40001/40002 çakışması ve POST engeli; `modbus-register-hints.log` |
| Save hata görünümü | `test/integration/web_navigation/test_navigation.js`: her render edilen alan, farklı kategori/fider, iki-word örtüşmesi, alan işaretleme, HTTP hata yanıtı ve POST'un engellenmesi |
| Fiziksel cihaz/SCADA kabul testi | **Bu güncellemede yapılmadı** |

Host testleri gerçek uygulama fonksiyonlarını çalıştırır; fiziksel flash,
HAL ve taşıma servisleri uygun yerde mock/fake kullanır. Test sonucu bütün
cihaz davranışlarının veya gerçek veri kaynaklarının hazır olduğu anlamına
gelmez. Birim testleri mevcut Ceedling altyapısına, web/NVRAM senaryoları
mevcut entegrasyon paketlerine eklenmiştir.

### 9.3 Bilinçli açık bırakılanlar ve sıradaki iş

- Gerçek RF telemetri/arıza üreticileri henüz kurulmadığı için dummy
  üreticileri kullanıcı talebiyle korunmuştur. Veri kaynağı engeli kapanmaz.
- IP'den türeyen giriş parolası halen vardır. RNG token düzeltmesi parola
  modelini değiştirmez. Cihaz başı anahtar/kimlik konusu ertelenmiştir.
- Shell oturum/ISR tasarımı kullanıcı kararıyla ertelenmiştir.
- BMS, PowerBoard NVM kalıcılığı, SBO/komut zinciri ve periyodik IEC104
  veri yayını bu değişikliklerle tamamlanmamıştır. Mevcut `#if 0` blokları
  korunur; bunların kaldırılması önerilmez.
- Genel `reboot_system()` reset öncesi sync TODO'su durur. Uygulanan
  politika 25 saatte bir ve periyodik reset öncesi kayıttır.
- Contiki kernel kapsam dışıdır. Sıkı dönüşüm uyarılarında kalan şüpheler
  gerçek üretici/tüketici akışıyla doğrulanmadan bug sayılmamalıdır.
- CI halen host testlerini çalıştırır; ARM CI derlemesi, release notları,
  anahtar yedekleme süreci ve fiziksel kabul testi ayrı işlerdir.

**04.10.2026 devamı:** Fider indeksleri ve özel GSM yardımcı yanıt tipi
uyumlu hâle getirildi. Veri türü kuralı AGENTS.md/CLAUDE.md içinde
netleştirildi. Bu değişiklikler `bf7ac1a` commit'inde kaydedilmiştir.

**04.10.2026 IEC104/SPI devamı:** IEC104 çekirdeğindeki 61 ve SPI flash
driver içindeki 25 uyarı kapandı. Sıra numarası, bit alanları ve SPI adres
byte sırası korundu. Ceedling'e 10 SPI ve iki IEC104 senaryosu eklendi;
toplam 361 test geçti. IEC104 testindeki dönüşüm uyarısı istisnaları
kaldırıldı. Ayrıntı ve sınırlar derleyici raporunun bölüm 16'sındadır.
Bu değişiklikler `bf7ac1a` commit'inde kaydedilmiştir.

**04.10.2026 kalan uyarılar devamı:** Application kaynaklarında mevcut
Release seçenekleri altında **149 → 0** uyarı kaldı. GSM bozuk yanıt ve
ACK daraltmaları mevcut scanner ile kontrol edilir. Formatter signed
minimum hesabı düzeltildi; HTTP header bounded formatter kullanır ve
sığmayan header için body göndermez. **377/377 Ceedling, 9/9 entegrasyon**
geçti. Ayrıntı derleyici raporunun bölüm 17'sindedir. Değişiklikler `bf7ac1a` commit'inde
kaydedilmiştir.

**Kalan 14 tanı:** 8 Contiki kullanıcı kararıyla kapsam dışı; 5 ST driver
ve 1 CubeMX syscalls kaynağı da kullanıcı kararıyla kapsam dışıdır.
ST/CubeMX kaynakları korunmuştur. Kapsam içindeki uyarılar kapanmıştır. Uyarı bastırma
uygulanmadı. Son Release derleme komutlarında `-Wshadow` etkindir.
`-Wformat=2` ve `-Werror` etkin görünmediğinden bütün zorunlu seçenekler
altında warning-free sonucu
iddia edilmez. Fiziksel cihaz kabul testi yapılmadı.

### 9.4 Commit durumu

RNG/BSP, RFWU/lifetime ve ilk uyarı/JSON düzeltmeleri commit'lenmiştir:
`e5322f6`, `622f050`, `ad40b75`, `6a0caa0`, `f8a8032`, `e8bb9cc`.
IEC104/Modbus adres kontrolü, arıza tabanlarının korunması, web hata
görünümü, canlı register aralığı ve ek testler `1102a17` commit'inde
kaydedilmiştir. Bu rapor ve adres rehberi ayrı doküman commit'ine dahildir.

HTTP uzunlukları, GSM parser sınırları, IEC104/SPI kodlama düzeltmeleri,
ek Ceedling/entegrasyon testleri ve veri türü kuralları `bf7ac1a`
commit'inde kaydedilmiştir. CubeIDE uyarı ayarları `5db75b8` commit'indedir.

### 9.5 Anahtarların geçici Git takibi — 04.10.2026

Kullanıcı kararıyla `keys/private_key.pem`, `keys/aes_key.bin` ve
`keys/rfwu_key.bin` geliştirme döneminde ana repoda takip edilir.
Anahtarlar değiştirilmedi. Saha öncesinde yeni üretim anahtarları ve cihaz
karşılıkları hazırlanıp doğrulanmalıdır. Ayrıntı `keys/README.md` içindedir.
Git takibi güvenli yedekleme ve saha kabulü maddelerini kapatmaz.
ARM CI/Docker ve README/CHANGELOG/kılavuz işleri kullanıcı kararıyla ertelendi.

/*** end of report ***/
