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
- Y5.11 C_DC/C_SC komutları: derleme anahtarı `C_SC_NA_1_ENABLED` her iki konfigürasyonda da tanımsız (`.cproject:46-50, 153-157`) → komut zinciri ölü kod; 45/46 liste girişleri kullanıcı kararıyla yorum satırına alındı; olumsuz yanıt 59/59 protokol testiyle doğrulandı (bölüm 9.14).

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
| Y2.3 heap rezervi | **AKTİF HATA DOĞRULANMADI / KORUNDU** | 04.10.2026: üç linker betiğinde 512 byte minimum heap rezervi vardır. Mevcut Release ELF sembol tablosunda `_sbrk` ve allocation fonksiyonları yoktur; MAP dosyasında ilgili kodlar discarded input sections (çıkarılan bölümler) içindedir. Uygulamada allocation çağrısı bulunmadı. Kullanıcı kararıyla linker ve ST/CubeMX kaynakları korunmuştur. |
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
| BMS | `bms_reader_init()` yorumda (`app_main.c:331`) ama UART5 RX kesmesi açık (`:332`) — reader etkin değil; Modbus BMS bloğu güncellenmiyor | Altyapı kararı ertelendi (§9.31). Y8.4 parser ve Y8.5–Y8.7 reader düzeltmeleri yapıldı (§9.32–§9.33); fiziksel BMS kabulü ve etkinleştirme ayrı kalemdir. |
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
| N1 | ORTA — DÜZELTİLDİ | 04.10.2026: Web etiketi Sinyal seviyesi (CSQ) olarak değiştirildi; ham CSQ artık dBm diye gösterilmez. 0–31 birimsiz, 99 ve geçersiz/eksik değerler Bilinmiyor/Unknown gösterilir. HTTP ve Modbus ham değerleri korunur. | Gerçek renderBoard fonksiyonunu kaynak ve gömülü gzip HTML üzerinde çalıştıran web_navigation testleri; iki dil, 32 geçerli değer ve sekiz geçersiz/eksik girdi |
| N2 | ORTA | RFWU `shared_key` NVRAM alanı hiç ilklendirilmiyor; kod 0 göründe gömülü default'a düşürüp default'u NVRAM'e kalıcı yazıyor — "cihaza özgü anahtar" hedefiyle çelişen davranış | `types.h:196`, `raw_tcp_fw_update.c:242-243, 270` |
| N3 | ORTA | HTTP yanıt gövdesi dökümü `#if 1` ile derlemede; default log seviyesi VERBOSE olduğundan üretimde konsol trafiği ve gecikme üretir | `http_response.c:126-132`, `nvram.c:255` |
| N4 | DÜŞÜK — DÜZELTİLDİ | Firmware sürüm endpoint'i varsayılan sürümü VERSION_MAJOR/MINOR/PATCH üzerinden üretir. Sabit 1.0.0 kaldırıldı; mevcut setter override davranışı korunur. | Gerçek handler, gerçek initializer ve setter ile entegrasyon testi; varsayılan/override/NULL ve buffer sınırları |
| N5 | DÜŞÜK | SSENDEXT/si_all_zero koruması `#if 0`'da ama besleyen veri canlı tutuluyor — bilinçli erteleme; karar ve tarih belgelenmeli | `gsm_listener_process.c:257-280` |
| N6 | DÜŞÜK — TEMİZLENDİ / SINIRLI KAPSAM | 04.10.2026: test/unit altındaki sekiz boş dizin, boş test/unit kökü ve boş test/integration/nvram/-p dizini kullanıcı talebiyle kaldırıldı. Bunlar Git tarafından takip edilmiyordu ve test runner tarafından kullanılmıyordu. Entegrasyon build dizinlerinin topluca silinmesi yapılmadı. | Silme öncesi boşluk, repo içi yol ve Git takibi kontrolü; kaynak veya test dosyası silinmedi |
| N7 | DÜŞÜK | `rf_dummy.h:14` "RF_DUMMY_ENABLE tanımlı değilse no-op" diyor; böyle bir makro yok — başlık kodu/yorum uyumsuzluğu (runtime `initialized` bayrağı fiilen inert) | `rf_dummy.c:328, 340` |

---

## 6. Bu Turda Doğrulanamayanlar (Eylül raporunda açık kalan)

Aşağıdaki maddeler Eylül raporunda açık görünüyor ve bu tur HEAD'de kanıt okunmadı; kapanış/idame kararı ayrı bir tur gerektirir:

- Y2.1 `int_master_enable` ARM dalı (latent tuzak) — derlenme koşulu değişmedi mi, kontrol edilmedi.
- Y6b.1 baud değişimi erteleme; Y6b.5 RS-485 echo koruması (donanıma bağlı).
- Y9.3 sıra sarması bulgusu mevcut kod ve 04.10.2026 sınır testleriyle kapatıldı (bölüm 9.11). O9.7 değerlendirmesi bölüm 9.12 içindedir; kullanıcı bootloader ölçümlerinin uygun olduğunu bildirmiştir. Bu madde için kod değişikliği yapılmaz.
- Y-Y4 shell komut tablosu değerlendirmesi bölüm 9.13 içindedir; mevcut kapasite 32'dir. T-T1 mevcut sıkı Ceedling derlemesinde tekrarlanmadı (bölüm 9.14).
- Y5.11 mevcut derleme için kapatıldı; iki aktif destek girişi yorum satırındadır, test sonucu bölüm 9.14 içindedir.
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

### 9.6 Heap bulgusunun değerlendirilmesi — 04.10.2026

Mevcut Release ELF ve MAP dosyaları incelendi. `_sbrk`, `_sbrk_r` ve
allocation (dinamik bellek ayırma) fonksiyonları nihai imajda bulunmuyor.
Kaynakta `_sbrk` bulunması aktif heap kullanımını kanıtlamaz. Minimum heap
rezervi 512 byte olarak kalır; rezervi sıfırlamak heap yasağını tek başına
uygulamaz. Kullanıcı öneriyi kabul etti; linker ve ST/CubeMX kaynakları
korundu. Bu madde mevcut imaj için doğrulanmış üretim engeli sayılmaz.
Sonuç gelecekteki derlemeler için otomatik güvence veya donanım testi değildir.

### 9.7 Web GSM sinyal birimi — 04.10.2026

Gerçek veri yolu `gsm_csq_cb()` → `gsm_info_get_signal_quality()` →
`system_status` → HTTP `GsmSig` → `renderBoard()` olarak doğrulandı.
Ham CSQ değerinin dBm diye gösterilmesi düzeltildi. Backend, Modbus ve
modem sorguları değiştirilmedi. Testler mevcut web_navigation entegrasyon
paketine eklendi; kaynak ve yeniden üretilen gömülü HTML üzerinde geçti.
Bu değişiklik C uygulama mantığını değiştirmez; ayrı Ceedling testi eklenmedi.
Fiziksel cihaz testi yapılmadı.

Telit LE9x0 AT Commands Reference Guide, 80407ST10116A Rev.12,
02.07.2015, sayfa 109, +CSQ bölümü dönüşüm tablosunu verir:
1–30 için `dBm = -113 + 2 × CSQ`; 0 için ≤ -113 dBm,
31 için ≥ -51 dBm, 99 için bilinmiyor. Bu gösterge 2 dB adımlıdır;
LTE RSRP ölçümü olarak adlandırılmamalıdır.
[Telit belgesi, dağıtıcı kopyası](https://www.shoshin.co.jp/c/mt/documents/publications/manuals/telit_le910_at_commands_reference_guide_r12.pdf).
Kullanıcının sağladığı yerel `TC_LE910R1_AT_Commands_Reference_Guide_r8.pdf`
ile modele özgü içerik de doğrulandı: 80690ST11099A Rev.8, 29.04.2026,
sayfa 246–250, AT+CSQ. Sayfa 246 aynı 3GPP RSSI tablosunu verir.
Dolayısıyla 1–30 için dönüşüm LE910R1 rehberiyle de doğrulanmıştır.
Sayfa 247 ayrıca TDSCDMA için ayrı bir kodlama listeler; 0–31 formülü
bu ayrı kodlamaya uygulanmaz. Sayfa 249'daki LTE RSRQ tablosu cevapta
ikinci alan olan `<sq>` içindir; mevcut parser ilk alan `<rssi>` değerini
kullanır. Bu turda web gösterimi ham CSQ olarak kalır; dBm dönüşümü uygulanmadı.

### 9.8 LE910R1 2G/4G ayrı sinyal alanları — 04.10.2026

Yerel Telit Rev.8 rehberi, AT+CESQ, sayfa 277–281 ve gerçek
`gsm_cesq_cb()` incelendi. Yazılım 2G RXLEV, 3G RSCP ve 4G RSRP/RSRQ
alanlarını ayrı saklar. Web `GsmSig` alanı bunları kullanmaz; ortak CSQ'yu
kullanır. `gsm_info_get_cesq_report()` vardır, fakat kaynak aramasında
aktif çağıranı bulunmadı.

Rehberdeki ara değer aralıklarının alt sınırları:
2G RXLEV 1–62 için `raw - 111` dBm; 4G RSRP 1–96 için
`raw - 141` dBm; 4G RSRQ 1–33 için `raw * 0.5 - 20` dB.
Bu değerler ölçüm aralıklarıdır; kesin tek nokta değildir.
RXLEV 0: < -110, 63: ≥ -48 dBm; RSRP 0: < -140, 97: ≥ -44 dBm;
RSRQ 0: < -19.5, 34: ≥ -3 dB. RXLEV 99 ve LTE alanları 255 bilinmiyor
veya ilgili hücre teknolojisinin aktif olmadığını belirtir.

Mevcut CESQ rapor fonksiyonu ara değerlerde alt sınırı hesaplar; uç
kodları da aynı doğrusal hesapla tek sayı olarak sunar ve bilinmiyor
kodları dışındaki geçersiz aralıkları reddetmez. Dolayısıyla eski rapordaki
CESQ dönüşümünün tamamen doğru olduğu ifadesi sınır durumları için geçerli
değildir. Fonksiyon web'de kullanılmadığından bu, mevcut web'de ulaşılan
ayrı bir hata olarak sunulmaz. Ayrı alanları web'e bağlamak ve sınır
kodlarını açık göstermek henüz uygulanmamıştır.

### 9.9 Web ayrı sinyal ölçümleri ve teknoloji — 04.10.2026

Kullanıcı onayıyla board status HTTP yanıtına `GsmRxlev`, `GsmRscp`,
`GsmRsrp`, `GsmRsrq`, `GsmCREG`, `GsmCGREG`, `GsmCEREG` eklendi.
Mevcut `GsmSig` ve `GsmRAT` sözleşmeleri korundu. Yeni modem sorgusu,
NVRAM alanı veya arka plan süreci eklenmedi; mevcut getter'lar kullanılır.

Web 2G RSSI, 3G RSCP, 4G RSRP ve 4G RSRQ alanlarını ayrı gösterir.
Ortak CSQ birimsiz kalır. Dönüşümler Telit Rev.8 AT+CESQ tablosuna göre
web'de yapılır; ara değerlerde ölçüm aralığı, uç kodlarda < veya ≥ işareti
kullanılır. Eksik, bilinmiyor veya geçersiz ölçüm Bilinmiyor/Unknown olur.
Önceki kullanılmayan `gsm_info_get_cesq_report()` değiştirilmedi; web bu
fonksiyonun uç değer yorumuna ve sinyalden teknoloji seçimine dayanmaz.

Teknoloji mevcut RAT bilgisinden 2G/GSM, 3G/UMTS veya 4G/LTE olarak
gösterilir. 4G için CEREG; 2G/3G için CREG/CGREG kayıt bilgileri kullanılır.
Registered veya roaming durumunda Şebekeye kayıtlı, kayıt yok/arama/ret
bilgisinde Şebekeye kayıtlı değil, bilinmeyen bilgide Bilinmiyor gösterilir.
Bu ifade şebeke kaydını anlatır; internet veya SCADA bağlantısı garantisi
vermez. Teknoloji mevcut ölçümlerden tahmin edilmez.

Değerlendirme eşikleri projedeki mevcut CESQ yorumundan alınmıştır:

| Ölçüm | Çok iyi | İyi | Orta | Zayıf | Çok zayıf |
|---|---|---|---|---|---|
| 2G RSSI / 3G RSCP | ≥ -60 dBm | ≥ -75 dBm | ≥ -85 dBm | ≥ -95 dBm | Daha düşük |
| 4G RSRP | ≥ -80 dBm | ≥ -90 dBm | ≥ -100 dBm | ≥ -110 dBm | Daha düşük |
| 4G RSRQ | ≥ -10 dB | ≥ -15 dB | ≥ -18 dB | ≥ -20 dB | Daha düşük |

Tablo en yüksek karşılanan seviyeye göre değerlendirilir. Ara kodlarda
aralığın alt sınırı kullanılır; alt uç kodu Çok zayıf olarak gösterilir.
Bu eşikler Telit'in üretim kabul sınırları değildir; yardımcı yorumdur.
Ekranda değerlendirmenin yol gösterici olduğu ve değerlerin son modem
sorgusundan geldiği belirtilir. Aktif olmayan teknolojinin ölçümü bulunmayabilir.

Doğrulama: kaynak ve gömülü gzip HTML üzerinde iki dil, 2G/3G/4G kayıt,
roaming/arama/ret/bilinmiyor, dönüşüm uçları ve seviye eşikleri test edildi.
Gerçek GSM callback'lerini kullanan Ceedling paketinde 25/25 test geçti;
ayrı CESQ alanları, eksik yanıtta kayıt yapılmaması ve kayıt durumları
kapsanır. Gerçek board HTTP handler'ı mevcut entegrasyon altyapısında
çalıştırılıp JSON alanları, uzunluğu ve buffer sınırları doğrulandı.
9/9 entegrasyon paketi ve ARM Release derlemesi geçti. Fiziksel cihaz
üzerinde test yapılmadı; commit yapılmadı.

### 9.10 Firmware sürüm endpoint'i — 04.10.2026

Sabit `1.0.0` varsayılanı kaldırıldı. `fw_version` override (özel değer)
yoksa `handle_get_fw_version()` sürümü `VERSION_MAJOR`, `VERSION_MINOR`,
`VERSION_PATCH` üzerinden üretir. Mevcut `fw_update_set_version_info()`
davranışı ve JSON alanları korunur; NULL parametre mevcut değeri değiştirmez.
Bu değişiklik sürüm numarasını artırmaz; mevcut 1.0.1 doğru gösterilir.

Mevcut web_auth entegrasyon harness (test düzeneği), üretim kaynağındaki
initializer, gerçek handler ve setter'ı doğrudan kullanır. Beklenen
varsayılan version.h'dan alınır; override ve NULL senaryoları, küçük buffer
uzunlukları ve canary (sınır işareti) kontrolleri geçmiştir.
Tam Ceedling çalışmasında 381/381 test geçti; sürüm endpoint'inin gerçek
handler senaryoları mevcut web_auth entegrasyon paketindedir.
ARM Release derlemesi geçti. Fiziksel cihaz testi ve commit yapılmadı.

### 9.11 Log sıra sarması — Y9.3 kapatıldı, 04.10.2026

Mevcut `log_read_last()` sıfır sıra değerini boş log göstergesi saymaz.
Üretim kodu değiştirilmeden iki sınır senaryosu eklendi. Her senaryo gerçek
`log_write()` ile 0–65534 arası 65.535 kayıt yazar; sıra alanı elle değiştirilmez.
RAM tabanlı NOR modeli yalnız 1→0 programlama, sektör silme ve alan sonu
canary kontrollerini uygular.

- `next_seq = 0` iken son kayıtlar 65534, 65533, 65532 sırasıyla okunur.
- Sonraki 0 ve 1 kayıtları yazılınca newest-first (yeniden eskiye) okuma
  ve sayfa devamı 1, 0, 65534, 65533 sırasını korur.
- Yeniden başlangıç taraması sarmanın hemen ardından next_seq=0,
  iki yeni kayıt sonrasında next_seq=2 değerini bulur.
- Kronolojik okuma 65534, 0, 1 sırasını ve payload içeriklerini korur.

Testler mevcut `test/integration/libs/test_spi_flash_log.c` düzeneğine ve
aynı düzeneği tekrar kullanan `test/libs/test_spi_flash_log_sequence_wrap.c`
Ceedling dosyasına eklendi. Entegrasyonda 152 kontrol, Ceedling'de iki yeni
senaryo geçti. Eski `next_seq == 0` boş-log kontrolünü yalnız geçici test
kopyasına ekleyen mutation (hata ekleme) çalışması başarısız oldu; yeni
testlerin eski hatayı yakaladığı doğrulandı. Loglar
`test/build/production-audit-2026-10-03/log-sequence-*.log` içindedir.
Fiziksel flash/güç kesintisi testi ve commit yapılmadı.

### 9.12 Flash silme ve harici watchdog — O9.7, 04.10.2026

Kullanıcı karttaki EWDT modelini TPS3828-33DBVR olarak bildirdi.
TI SLVS165O Rev.O switching characteristics (anahtarlama özellikleri)
tablosunda TPS3823/4/8 watchdog timeout minimum 0.9 s, tipik 1.6 s,
maksimum 2.5 s olarak verilir. Bu tablonun koşulu TA=25°C'dir; tüm sıcaklık
aralığı için aynı minimumun garanti edildiği iddia edilmez.
Kaynak: https://www.ti.com/lit/ds/symlink/tps3828.pdf, sayfa 8 ve bölüm 7.3.4.

Kart yazılımında tanımlı AT25SF321B için Renesas DS-AT25SF321B-179 Rev.I
sayfa 55, 4 KB silme süresi tipik 55 ms, maksimum 250 ms verir.
32 KB için maksimum 450 ms, 64 KB için 700 ms, tüm çip için 30 s'dir.
Kaynak: https://www.renesas.com/en/document/dst/at25sf321b-datasheet?language=en.
Bu değerler farklı silme büyüklükleri için ayrı değerlendirilmelidir.

Log halkası `w25qxx_erase_sector()` üzerinden 4 KB siler. Bu fonksiyonda
silme öncesi `bsp_kick_wdt()` zaten vardır. Dolayısıyla eski rapordaki
silme yolunda hiç kick olmadığı iddiası mevcut kaynak için geçerli değildir.
Bekleme döngüsünde kick yoktur; 300000 değeri zaman değil döngü sayısıdır.

TI Rev.O TPS3828 WDI timer'ının falling edge (düşen kenar) ile beslendiğini
belirtir. `bsp_kick_wdt()` GPIO toggle yapar; tek çağrı her zaman besleyen
kenarı üretmez, ardışık iki çağrı bir düşen kenar üretir. Birbirini izleyen
4 KB silmelerde flash'ın kendi silme sürelerinin toplamı iki silme için
en fazla 500 ms'dir; SPI erişimi, aradaki işler ve önceki kenardan geçen
süre buna dahil değildir. Ana süreç de GPIO'yu düzenli toggle eder.

Mevcut normal 4 KB log silme yolunun watchdog resetine neden olduğu
kanıtlanmadı; bu madde için periyodik kick veya yeni recovery katmanı
uygulanmadı. SPI/flash arızası, blok/çip silme ve diğer uzun işlemler bu
sonuçla kapatılmaz. Tam cihaz kabulünde WDI düşen kenarları arasındaki
süre ve reset davranışı donanımda ölçülmelidir. Bu turda fiziksel ölçüm ve
commit yapılmadı; yalnız değerlendirme güncellendi.

Kullanıcı 04.10.2026 tarihinde bootloader tarafında ölçüm yapıldığını ve
sonucun uygun olduğunu bildirmiştir. Ölçüm kaydı bu turda incelenmemiştir;
bootloader sonucu uygulamadaki tüm uzun işlem yollarının ölçümü sayılmaz.
Mevcut 4 KB log silme bulgusu için ek kod değişikliği yapılmadan ilerlenir.

### 9.13 Shell komut tablosu kapasitesi — Y-Y4, 04.10.2026

Eylül bulgusu 24 slot üzerinden yazılmıştır. Mevcut
`Application/libs/shell.c:43` kapasiteyi 32 olarak tanımlar.
`shell_register_command()` tablo doluysa kayıt öncesinde kontrol yapar,
SHELL_LOG ile hata bildirir ve -1 döner (satır 158–175). Aynı isimle
tekrar kayıt da -2 ile reddedilir. Tablo sınırı aşılmaz.

Application kaynaklarında sabit isimli 27 kayıt noktası bulunmuştur;
bunların tamamı etkin veya başlangıçta çağrılmış kabul edilmemelidir.
Mevcut incelemede kapasite yüzünden bir üretim komutunun kaybolduğu
kanıtlanmamıştır. Çağıranlar dönüş değerini çoğunlukla kontrol etmez;
gelecekte kapasite aşılırsa yeni komut eklenmez ve hata yalnız terminal
çıktısında görünür. Bu kalan bakım riski mevcut taşma hatası değildir.

Kapasite veya shell mimarisi değiştirilmeden
`test/libs/test_shell_command_registration.c` içine dört Ceedling testi
eklendi. Testler gerçek `shell.c` dosyasını derler; kayıt ve silme
fonksiyonları mock değildir. Her test öncesinde özel komut tablosu
sıfırlanır; BSP bağımlılığı CMock ile ayrılır.

- 32 kayıt sonrası 33. komut reddedilir; mevcut 32 komutun handler'ı
  çalışmaya devam eder ve reddedilen komut çalıştırılamaz.
- Aynı isimle tekrar kayıt -2 döner; ilk handler korunur ve slot tüketilmez.
- Dolu tablonun ortasındaki komut silinir; kalan komutlar çalışır ve
  boşalan kapasiteye yeni bir komut eklenir.
- Son komut silinip aynı isimle yeniden eklenir; ikinci silme reddedilir
  ve yeniden dolan tablo kapasitesini aşan kayıt yine reddedilir.

`ceedling test:test_shell_command_registration` sonucu 4/4 geçti.
Host GCC'nin varsayılan signed char davranışı Cortex-M33 GCC 14.3.rel1
ile farklıdır; hedef derleyicide `__CHAR_UNSIGNED__ = 1` doğrulandı.
Yalnız bu test için `-funsigned-char` eklendi; uyarı kontrolleri korunur.
Contiki zamanlaması, UART/web eşzamanlılığı ve terminal hata metni bu
testlerin kapsamında değildir. Firmware kodu ve kapasite değiştirilmedi.
Test çıktısı `test/build/production-audit-2026-10-03/shell-command-ceedling.log`
içindedir; commit yapılmadı.

### 9.14 IEC104 desteklenmeyen kontrol komutları — Y5.11, 04.10.2026

Önceki T-T1 host uyarıları mevcut sıkı Ceedling protokol derlemesinde
tekrarlanmadı; değişiklik öncesi mevcut 57 senaryo geçti.

C_SC_NA_1 (45, single command) ve C_DC_NA_1 (46, double command) aktif
destek listesinde bulunmasına rağmen mevcut derlemede handler'ları yoktu.
Geçerli komutlar destek kontrolünü geçip yalnız log basan default dalına
ulaşıyordu. Gerçek RX/TX yoluna eklenen iki yeni Ceedling testi değişiklik
öncesinde beklenen tek yanıt yerine sıfır yanıt gördü ve başarısız oldu.
Bu durumun gerçek komut yanıtını etkilediği testle doğrulandı.

Kullanıcının kararıyla iki liste girişi yorum satırına alındı. Böylece
mevcut desteklenmeyen tip yanıtı (UkTypeId, P/N=1) gönderilir. Handler'lar,
tip tanımları ve kapalı kod korunur; SBO veya fiziksel çıkış kontrolü
etkinleştirilmez. Gelecekte komut etkinleştirilirken liste girişi ve
handler birlikte değerlendirilmelidir.

`test/iec104/test_iec104_protocol_scenario.c` içindeki iki yeni senaryo
cevap türünü, olumsuz teyit bitini, originator adresini, bağlantı sıra
numaralarını, CA/IOA ve komut verisinin korunmasını doğrular. Bağlantı
aktif kalır. Değişiklik sonrası `ceedling test:test_iec104_protocol_scenario`
sonucu 59/59 geçti. Önce/sonra çıktıları
`test/build/production-audit-2026-10-03/iec104-command-before.log` ve
`iec104-command-after.log` içindedir. Bu değişiklik için cihaz testi ve
ARM derlemesi yapılmadı; değişiklik henüz commit edilmedi.

### 9.15 Önceki çalışmaların commit kaydı — 04.10.2026

- `054842a`: web GSM sinyal/şebeke gösterimi, firmware sürüm kaynağı ve testler.
- `4f43331`: log sıra sarması ve shell kayıt sınırı testleri ile rapor.

Commit öncesinde tam Ceedling koşumu 387/387, entegrasyon 9/9 paket geçti.
Çıktılar `test/build/production-audit-2026-10-03/pre-commit-*.log` içindedir.
Bu sayılar bölüm 9.14'teki iki yeni senaryoyu içermez. Push yapılmadı.

### 9.16 Ertelenen maddeler ve I2C kurtarma incelemesi — 04.10.2026

Kullanıcı dialer O4.4 maddesini, altyapısı değişecek PowerBoard kalıcılığını
ve shell yardım kontrolü O10.8 maddesini ertelemiştir. Shell yardım
incelemesinde eklenen iki geçici test geri alınmıştır; önceki dört shell
kayıt testi korunmuştur. UART5/BMS RX kapatma değişikliği de geri
alınmıştır; kullanıcı hattın mevcut haliyle kalmasını istemiştir.
RF_SIMULATOR seçeneği açıldığında RF, UART5/BMS hattını kullanabilir;
mevcut kaynakta bu seçenek kapalıdır.

Sıradaki O8.11 maddesi için `HAL_I2C_ErrorCallback()` incelendi.
BERR/ARLO/OVR hatalarında DeInit/Init ve filtre ayarı ISR içinde yapılır;
normal read sonu NACK (AF) tek başına bu yeniden başlatmayı tetiklemez.
Bu davranış kaynakta doğrulanmıştır; ISR içinde olması tek başına
kilitlenme veya süre aşımı kanıtı değildir.

Yerel STM32U3 HAL 1.4.0 kaynaklarında I2C Init/DeInit ve filtre ayarı
fonksiyonlarında HAL_Delay veya tick ile timeout bekleyen döngü yoktur.
I2C3 MSP yolunda PCLK3 seçilir, GPIO ve NVIC yeniden ayarlanır; seçilen
RCC I2C3 dalı clock mux ayarıdır. Bu inceleme donanımda ISR süresi ölçümü
sayılmaz. Mevcut hata kurtarma yolunun cihazda kesme gecikmesine veya
kilitlenmeye neden olduğu doğrulanmamıştır. Bu yüzden yalnız genel
ISR kuralına dayanarak yeni süreç, bayrak veya retry katmanı önerilmez.
Kullanıcı mevcut kurtarma yolunun korunmasını onaylamıştır. İhtiyaç oluşursa I2C hata anında ISR süresi ve kurtarma sonucu ölçülmelidir.
Bu madde için kod değişikliği veya donanım testi yapılmamıştır.

### 9.17 SCP kaynak adresi doğrulaması — O6b.7, 04.10.2026

`doc/SCP_Arayuz_Paketi_R1_yeniden_yazim.md` bölüm 3.3 ve 3.7, 0x00
adresinin yalnız DST broadcast için geçerli olduğunu ve SRC=0x00'ın
reddedilmesini belirtir. Mevcut `scp.c` çözümcüsü COBS, hedef, uzunluk ve
CRC kontrolü yapar; SRC için bu kontrol yoktur. CRC'si ve uzunluğu doğru,
hedefi cihaz olan SRC=0x00 paketi callback'e iletilebilir. RF callback de
ayrı bir SRC reddi yapmaz. PING yanıtında hedef gelen SRC'den kopyalandığı
için böyle bir paket broadcast hedefli ACK üretme yoluna ulaşabilir.

Kullanıcı onayıyla çözümcüye CRC doğrulamasından sonra SRC=0x00 reddi
eklendi. DST=0x00 geçerli broadcast davranışı korunur. Yeni durum,
retry veya kurtarma katmanı eklenmedi.

`test/scp/test_scp.c` içine iki Ceedling senaryosu eklendi. Gerçek
`scp_send()` CRC ve COBS üretir; gerçek `scp_process_byte()` ile bu
paketler alınır. Unicast ve broadcast hedefler için sıfır kaynaklı
paketler packet-ready durumuna ulaşmaz ve `scp_get_packet()` NULL döner.
Ardından reset veya packet_done çağırmadan gönderilen geçerli paket
alınır; adres, tip, komut, sıra ve payload doğrulanır.

Düzeltme öncesi iki yeni test başarısız oldu (10 geçti / 2 kaldı);
düzeltme sonrası `ceedling test:test_scp` sonucu 12/12 geçti.
RF hub simülatörü entegrasyon paketi de 74/74 geçti; bu paket gerçek RF
cihazı testi değildir. Çıktılar
`test/build/production-audit-2026-10-03/scp-source-before.log`,
`scp-source-after.log` ve `scp-source-rf-hub.log` içindedir.
Bu madde mevcut RX yolu için kapatıldı. ARM derlemesi, cihaz testi ve
commit bu düzeltme için yapılmadı.

### 9.18 Modbus reset komutunun yanıt sırası — O6b.6, 04.10.2026

`Application/modbus_process.c` içindeki FC06 yazma callback'i geçerli
modem-reset adresi ve tetik değerinde doğrudan `bsp_system_reset()` çağırır.
Bu fonksiyon NVIC_SystemReset yapar ve dönmez. Modbus çekirdeği ise FC06
echo yanıtını yazma callback'i MODBUS_REG_OK döndükten sonra gönderir.
Dolayısıyla başarılı reset komutunun yanıtı gönderilmeden cihaz resetlenir.
Bu sıra mevcut aktif çağrı yolunda doğrulanmıştır; fiziksel ölçüm yapılmadı.

Kullanıcı mevcut web reset altyapısının kullanılmasını ve 1 saniyelik
gecikme verilmesini seçmiştir. Callback doğrudan reset yerine
`reboot_system_delayed(1000U)` çağırıp MODBUS_REG_OK döner. Normal FC06
yanıtı çekirdeğin mevcut echo yolundan gönderilir; broadcast isteğinde
reset planlanır ancak protokol gereği yanıt gönderilmez. Yeni TX-complete
bekleme durumu, retry, reset süreci veya NVRAM flush politikası eklenmez.
Mevcut `CLOCK_CONF_SECOND=1000` ile gecikme bir saniyedir.

`test/application/test_modbus_reset_scenario.c` gerçek uygulama
callback'ini ve gerçek RTU çekirdeğini kullanır. Yalnız reset planlayıcı,
config ve BSP mock'tur; test taşıması yanıt byte'larını yakalar. DMA ve
Contiki zamanlayıcısının fiziksel davranışı bu senaryolarda çalıştırılmaz.
LTO yalnız bu test için kullanılarak ilgisiz donanım/süreç yolları çıkarılır;
zorunlu uyarı seçenekleri korunur.

- Doğru unicast reset: planlayıcı 1000 ms alır ve istek byte'ları echo edilir.
- Broadcast reset: aynı gecikme planlanır, yanıt gönderilmez.
- Yanlış tetik değeri: 0x03 exception, reset planlanmaz.
- Yanlış adres: 0x02 exception, reset planlanmaz.

Düzeltme öncesi unicast/broadcast testleri beklenmeyen doğrudan BSP reset
çağrısını yakaladı. Düzeltme sonrası dört Ceedling testi geçti.
ARM Release incremental derlemesi başarılıdır; değişen kaynaklarda yeni
uyarı görülmedi. Çıktılar
`test/build/production-audit-2026-10-03/modbus-reset-before.log`,
`modbus-reset-after.log` ve `modbus-reset-release.log` içindedir.
Fiziksel UART iletimi veya reset zamanı ölçülmedi; bir saniye gecikme
iletim başarısını donanımsal olarak garanti eden bir kontrol değildir.
Bu düzeltme henüz commit edilmedi.

### 9.19 Modbus istek uzunluğu hatasının yanıtı — O6b.8, 04.10.2026

Gerçek RTU alım yolu en az dört byte, doğru CRC ve uygun cihaz adresi
kontrollerinden sonra FC03/FC06 işleyicilerine ulaşır. Bu işleyiciler
sekiz byte olmayan isteği yanıt vermeden bırakıyordu. Böyle bir paket
register callback'ini çalıştırmıyordu; sorun hatanın istemciye
bildirilmemesiydi. Yeni testler düzeltme öncesinde bu davranışı doğruladı:
beş testin üçü geçti, iki unicast exception testi başarısız oldu.

Kullanıcı onayıyla mevcut uzunluk kontrollerine mevcut exception gönderme
fonksiyonu eklendi. Cihaza yöneltilmiş, CRC'si doğru fakat uzunluğu hatalı
FC03/FC06 isteğine 0x03 (Illegal Data Value) yanıtı verilir. Broadcast,
başka cihaz adresi ve yanlış CRC durumlarında yanıt gönderilmez.
Dört byte'tan kısa paketler mevcut çerçeve kontrolünde reddedilir.
Yeni durum, retry veya alım mekanizması eklenmedi.

[Modbus Application Protocol V1.1b3, bölüm 7](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
0x03 kodunu istek yapısı ve ima edilen uzunluk hataları için tanımlar.

`test/libs/test_modbus_request_length.c` Ceedling altyapısına eklendi.
Testler gerçek `libmodbusrtu_modbus_rx_byte()` ve
`libmodbusrtu_modbus_process()` yolunu kullanır. Tick ve register callback'leri
test karşılıklarıdır; UART donanımı çalıştırılmaz. FC03 ve FC06 için 4, 7
ve 9 byte uzunlukları, exception byte'ları ve CRC, register callback'lerinin
çalışmaması, broadcast/başka adres/yanlış CRC sessizliği doğrulanır.
Her hatalı paketten sonra reset yapılmadan geçerli istek gönderilir ve
başarılı yanıtı kontrol edilir.

- Uzunluk senaryoları: 5/5 Ceedling testi geçti.
- Mevcut Modbus reset senaryoları: 4/4 Ceedling testi geçti.
- ARM Release incremental derlemesi başarılı; yeni uyarı görülmedi.

Çıktılar `test/build/production-audit-2026-10-03/modbus-length-before.log`,
`modbus-length-after.log`, `modbus-length-reset-regression.log` ve
`modbus-length-release.log` içindedir. Bu madde mevcut yazılım yolu için
kapatıldı. Fiziksel Modbus hattında test ve commit henüz yapılmadı.

### 9.20 Modbus çerçeveleme açıklamasının kanıtı — O6b.9, 04.10.2026

**Amaç:** Başlıktaki byte sayısıyla çerçeveleme açıklaması mevcut
uygulamayla karşılaştırılmıştır. Üretim kodu değiştirilmemiştir.

**Kullanım yeri:** `Application/libmodbusrtu/modbus_rtu_slave.h` içindeki
function code açıklaması ve `libmodbusrtu_modbus_process()` API açıklaması.
Bu yorumlar bilinen function code için uzunluğun paketi tamamlayabileceğini
söyler. Çekirdeğin gerçek kararı ise `modbus_rtu_slave.c` içindeki
`libmodbusrtu_modbus_process()` fonksiyonunda verilir: yazılım modunda
son RX byte'ından itibaren geçen süre `MODBUS_TIMEOUT_MS` değerine
ulaşmalıdır. Mevcut değer 10 ms, varsayılan `MODBUS_USE_HW_RTO` değeri
0'dır; `.cproject` içinde bunu değiştiren bir tanım bulunmamıştır.
Donanım modundaki ayrı yol `frame_ready` bayrağını kullanır.

**Doğrulama:** `test/libs/test_modbus_request_length.c` içine üç Ceedling
testi eklenmiştir. Gerçek RX ve process fonksiyonları çalıştırılmış;
yalnız tick, register callback'leri ve TX taşıması test karşılığıdır.

- Geçerli sekiz byte FC03 ve FC06 isteği hemen veya 9 ms sessizlikte
  işlenmez. Tam 10 ms sessizlikte callback çalışır ve yanıt gönderilir.
- Desteklenmeyen FC01 ve FC0F için de aynı bekleme gerçekleşir. Sonrasında
  beş byte 0x01 (Illegal Function) exception yanıtı, adresi ve CRC'si
  doğrulanır. Register callback'leri çalışmaz. FC0F gövdesinin geçerliliği
  test edilmez; mevcut çekirdek bu function code'u zaten desteklemez.
- Sekiz byte'tan 9 ms sonra ek bir byte alınması sessizlik süresini yeniden
  başlatır. İlk paketin başlangıcından 10 ms geçtiğinde hâlâ beklenir;
  son byte'tan 10 ms sonra çerçeve işlenir. Bozuk CRC nedeniyle yanıt
  gönderilmez; sonraki geçerli istek reset yapılmadan başarıyla işlenir.

Üç yeni test ve mevcut beş uzunluk testi birlikte 8/8 geçmiştir.
Çıktı: `test/build/production-audit-2026-10-03/modbus-framing-evidence.log`.
Bu sonuç byte sayısıyla erken tamamlama iddiasının mevcut yazılım modu
için yanlış olduğunu kanıtlar. Donanım RTO modu, UART kesme zamanlaması
ve fiziksel hattaki süreler bu testte doğrulanmamıştır.

**Öneri:** Yalnız ilgili başlık yorumları gerçek davranışa göre
düzeltilmelidir. Çalışma mantığını veya timeout değerini değiştiren bir
çözüm bu bulgu için gerekli değildir. Yorum düzeltmesi henüz yapılmamıştır.

### 9.21 PowerBoard toplu okuma örneklerinin kanıtı — O6b.10, 04.10.2026

**Amaç:** Register başına snapshot (anlık kopya) alma ve tek FC03
response (yanıt) içinde farklı örneklerin birleşmesi incelenmiştir.
Üretim kodu değiştirilmemiştir.

**Kullanım yeri:** Gerçek çağrı zinciri şöyledir:
`modbus_process_fc03()` her register için `fc03_read_callback()` çağırır;
PowerBoard aralığında `modbus_power_stats_read()` her defasında
`power_board_get_telemetry()` çağırır. Getter, `i2c_slave_snapshot()` ile
96 byte alır ve `power_board_decode_telemetry()` ile yeniden çözer.
`Application/power_board/i2c_slave.c:261-269` kopya sırasında kesmeleri
kapatır ve önceki PRIMASK değerini geri yükler. Aynı dosyadaki RX callback,
I2C verisini kesme bağlamında register map'e yazar. Bu yüzden kooperatif
Contiki modeli PowerBoard verisinin register okumaları arasında sabit
kalacağını garanti etmez. Her kopya sırasında kesmeler kapalıdır; bu
koruma bütün FC03 yanıtını kapsamaz. Kopya koruması, devam eden bir I2C
transferinin bütünüyle tamamlandığını da garanti etmez.

BMS tarafı farklıdır: `Application/bms/bms_reader.c:24-30` içindeki
getter yapıyı kopyalar fakat kesmeleri kapatmaz. Rapordaki BMS için
79 kez kesme kapatma iddiası mevcut kod için geçerli değildir. BMS
çözülmüş verisi kooperatif süreçte güncellenir; RX kesmesi yalnız ham
buffer'ı doldurur. BMS veri tutarsızlığı bu incelemede gösterilmemiştir.

**Doğrulama:** `test/application/test_modbus_power_snapshot_scenario.c`
Ceedling altyapısına eklenmiştir. Gerçek RTU çekirdeği, uygulamanın
`fc03_read_callback()` fonksiyonu, PowerBoard register eşlemesi,
telemetri getter'ı ve decoder çalıştırılmıştır. I2C snapshot kaynağı,
config ve ilgisiz kaynaklar test karşılığıdır. LTO yalnız bu testte
ilgisiz donanım/süreç yollarını çıkarır. Shell log argümanları derlemede
korunur; zorunlu uyarı seçenekleri kapatılmamıştır.

- 49200..49231 aralığının tek FC03 isteğinde okunması, 32 snapshot
  çağrısı oluşturmuştur. Yanıtın FC03 yapısı, uzunluğu ve CRC'si
  doğrulanmıştır. Sabit örneğin sequence=7 ve VBAT=24000 değerleri
  beklenen register'larda görülmüştür.
- 49200..49210 isteğinde üçüncü snapshot'tan sonra test kaynağı,
  sequence=7/VBAT=24000 örneğinden sequence=8/VBAT=28000 örneğine
  geçirilmiştir. İki örnek de doğru XSUM ile hazırlanmıştır. Tek
  yanıt eski sequence=7 ile yeni VBAT=28000 değerini birlikte
  taşımıştır. 11 register için 11 snapshot çağrısı oluşmuştur.
  Son örneğin sequence=8 ve VBAT=28000 olduğu gerçek decoder ile
  ayrıca doğrulanmıştır.

İki kanıt testi 2/2 geçmiştir. Bu testler mevcut kusurlu davranışı
belgeler; düzeltme yapıldığında beklentiler aynı örnek ve istek başına
tek snapshot davranışına göre değiştirilmelidir. Çıktı:
`test/build/production-audit-2026-10-03/modbus-power-snapshot-evidence.log`.
Fiziksel I2C kesmesi çalıştırılmamıştır. Veri değişimi kontrollü olarak
snapshot çağrıları arasına yerleştirilmiştir; sahadaki oluşma sıklığı,
kesme kapatma süresi veya performans kaybı ölçülmemiştir.

**Öneri:** PowerBoard verisi her FC03 isteğinde yalnız bir kez alınmalı;
o isteğin ilgili register'ları aynı kopyadan üretilmelidir. Uzun bir
kesme kapatma bölgesi, retry veya yeni arka plan süreci eklenmemelidir.
**Durum: Ertelendi.** Kullanıcı 04.10.2026 tarihinde bu bulgunun
şimdilik mevcut haliyle bırakılmasını seçmiştir. Üretim kodu
değiştirilmemiştir; iki Ceedling kanıt testi repoda korunmuştur.
PowerBoard için aynı yanıtta farklı örneklerin birleşmesi riski açıktır.
BMS optimizasyonu bu kanıtla zorunlu hale gelmemiştir.

### 9.22 Modbus cihaz adresi doğrulaması — D6b.12, 04.10.2026

**Amaç:** Web ve sunucu cihaz adresi kontrollerinin aynı aralığı kabul
etmesi ve register rehberindeki mevcut davranışın açıklanması.

**Kanıt:** Web arayüzü zaten 1..247 aralığını doğrulamaktadır. Sunucudaki
`validate_modbus_device_id()` ise 0 için yalnız uyarı verip true dönüyordu.
HTTP kayıt yolu gerçek `parse_modbus_config()` ardından
`set_modbus_config()` çağırır. RTU başlangıç fonksiyonu geçersiz cihaz
adresini 23 ile değiştirir; çalışma sırasındaki adres güncelleme API'si
geçersiz adresi yok sayar. Dolayısıyla 0 kaydedilmesi kayıtlı adres ile
çalışan adresin farklı olmasına yol açabilir. Broadcast paketlerinin
hedef adresi 0 olması, cihazın kendi adresinin 0 yapılmasını gerektirmez.

**Değişiklik:** Kullanıcı onayıyla mevcut doğrulama 0 ve 247 üzerini
reddedecek şekilde düzeltildi. Aynı doğrulama doğrudan setter'a da
konuldu; geçersiz adres config okumaya/yazmaya, sync veya çalışma ayarı
bildirimine ulaşmadan reddedilir. JSON parse hatası mevcut HTTP 400
cevabı üzerinden döner. Web ve RTU broadcast davranışı değiştirilmedi.

Başlangıç `active_baud=115200` ile CubeMX `huart4.Init.BaudRate=115200`
mevcut kodda aynıdır. Bu konuda çalışma hatası gösterilmedi; baud rate
başlatma veya HAL/CubeMX kaynakları değiştirilmedi.
Pasif fiderin sıfır dönmesi rehberin 5.1.1 bölümünde zaten yazılıdır.
Tek başına sıfırın veri yok anlamına gelmediği ve ayrılmış offset 27..99
adreslerinin de sıfır ile normal FC03 yanıtı verdiği açıklanmıştır.
Adres rehberi sürümü 1.8 olmuştur.

**Doğrulama:** `test/web_server/test_json_config_bounds.c` içine dört
Ceedling testi eklenmiştir. Gerçek JSON parser ve setter kullanılmıştır;
config depolama ve çalışma ayarı bildirimi mock'tur. 0/248/255 reddi,
1/247 kabulü, geçersiz setter çağrısında depolama API'lerine ulaşılmaması
ve 247'nin değişmeden depolama katmanına verilmesi doğrulanmıştır.
Mevcut test ayrıca adres 1'in depolama yolunu kapsamaktadır.

- Düzeltme öncesi: 24 geçti, iki yeni test başarısız oldu.
- Düzeltme sonrası: JSON sınır paketi 26/26 geçti.
- Entegrasyon: 9/9 paket geçti.
- ARM Release incremental derlemesi başarılı; yeni uyarı görülmedi.

Çıktılar `test/build/production-audit-2026-10-03/modbus-device-id-before.log`,
`modbus-device-id-after.log`, `modbus-device-id-integration.log` ve
`modbus-device-id-release.log` içindedir. Fiziksel cihaz testi ve commit
henüz yapılmadı. Cihaz adresi doğrulama eksikliği kapatılmıştır.

### 9.23 RF geçici ERROR yanıtının kanıtı — Y7.2, 04.10.2026

**Amaç:** NOT_AVAILABLE (veri henüz hazır değil, 0x05) yanıtında mevcut
retry haklarının kullanılıp kullanılmadığı ve envanter etkisi incelenmiştir.
Üretim kodu değiştirilmemiştir.

**Kanıt:** `rf_comm.c` içindeki `scp_on_response()` CMD/SEQ eşleşmesini
kontrol eder. ERROR tipinde gövdedeki hata kodunu ayırmadan SCP_CMD_ERR
sonucunu seçer; busy durumunu temizleyip done callback'ini çağırır.
`rf_inventory.c` içindeki `on_set_done()` bu sonuçta skipped_count
artırıp sonraki faza geçer. END için ACK alınırsa atlanan cihaz olsa
bile `loaded=true` yapılır. Mevcut timeout yolu ise kalan retry hakkını
azaltıp aynı paketi ve SEQ değerini yeniden gönderir.

**Doğrulama:** `test/rf/test_rf_error_retry_scenario.c` Ceedling'e
eklenmiştir. Gerçek komut gönderme/yanıt/timeout fonksiyonları ve
inventory sequencer çalıştırılmıştır. TX paketleri gerçek SCP/COBS/CRC
ile üretilmiş ve ikinci bir gerçek SCP parser ile çözümlenmiştir.
RX yanıt nesnesi doğrudan `scp_on_response()` girişine verilmiştir;
RX UART, RX dispatch ve fiziksel hub çalıştırılmamıştır. Tick ve feeder
store test karşılığıdır. Test başlangıcında modülün private RAM durumu
sıfırlanır; üretim reset veya API değişikliği eklenmemiştir.

- GET_STATUS için iki retry hakkıyla gönderilen isteğe eşleşen ERROR
  0x05 verildiğinde done bir kez SCP_CMD_ERR almıştır. Haklar hâlâ 2'dir,
  komut serbesttir; ileride timeout kontrolü yeni gönderim yapmamıştır.
- Yanıt gelmeyen kontrol senaryosunda 499 ms'de gönderim yoktur;
  500 ms'de aynı paket/SEQ yeniden gönderilir, hak 1'e iner.
  Sonraki ACK komutu başarıyla bitirir.
- Tek aktif cihazın INVENTORY_SET isteğine ERROR 0x05 verildiğinde
  skipped_count=1, sent_count=0 olmuştur. Bir sonraki continue çağrısı
  cihazı yeniden göndermek yerine INVENTORY_END göndermiştir.
  END ACK sonrası loaded=true ve active=false doğrulanmıştır.

Üç kanıt testi 3/3 geçmiştir. Testler mevcut sorunlu ERROR davranışını
belgeler; düzeltmede beklentiler retry davranışına göre değiştirilmelidir.
Çıktı: `test/build/production-audit-2026-10-03/rf-error-retry-evidence.log`.
Sahada 0x05 oluşma sıklığı ve fiziksel RF davranışı ölçülmemiştir.

**Öneri:** 0x05 için mevcut sınırlı timeout/retry yolu kullanılmalı;
aynı SEQ korunmalı ve kalıcı hata sonucu ancak haklar bitince verilmelidir.
Yeni kuyruk, süreç veya sınırsız retry eklenmemelidir. 0x03 BUSY ayrı
incelenmelidir: güncel SCP belgesi config işlemleri için 0x28/0x21
ile durum izlemeyi tarif eder; tüm komutlara aynı retry kuralı uygulanması
bu kanıtla gerekçelendirilmemiştir.

**Durum: Ertelendi.** Kullanıcı 04.10.2026 tarihinde bu bulgunun not
alınıp şimdilik geçilmesini seçmiştir. Üretim kodu değiştirilmemiştir;
üç Ceedling kanıt testi repoda korunmuştur. NOT_AVAILABLE yanıtında
retry yapılmaması ve envanterde cihaz atlanması riski açıktır.

### 9.24 NOT_AVAILABLE için iki retry — Y7.2, 04.10.2026

**Amaç:** Kullanıcı önceki erteleme kararından sonra 0x05 NOT_AVAILABLE
için devam edilmesini ve iki retry (ek deneme) kullanılmasını seçmiştir.
9.23 bölümündeki erteleme, bu hata kodu bakımından bu bölümle güncellenir.

**Değişiklik:** `rf_comm.c` içindeki gerçek ERROR yanıt yoluna küçük bir
kontrol eklenmiştir. CMD/SEQ eşleşmesinden sonra en az bir byte gövde,
ilk byte=0x05 ve kalan retry hakkı varsa mevcut sayaç azaltılır. Aynı
`last_req` paketi, aynı SEQ ile yeniden gönderilir ve mevcut timeout
süresiyle deadline yeniden kurulur. Done callback çağrılmaz; komut
busy kalır. Hak kalmadığında sonraki 0x05 mevcut SCP_CMD_ERR yolu ile
komutu bitirir. Varsayılan ve envanter komutları zaten iki ek retry ile
başlatılır; böylece toplam gönderim sınırı üçtür. Çağıranın verdiği
retry sayısı değiştirilmemiştir.

Timeout ve 0x05 aynı sayacı tüketir; iki ayrı retry bütçesi yoktur.
Son gönderime yanıt gelmezse mevcut SCP_CMD_TIMEOUT sonucu korunur.
BUSY, kalıcı ERROR ve boş ERROR gövdesi eski davranışla komutu bitirir.
Yeni süreç, kuyruk, durum alanı veya sınırsız deneme eklenmemiştir.
INVENTORY_SET ve INVENTORY_END mevcut komut yolundan bu davranışı alır;
retry sırasında envanter sonraki adıma geçmez. O7.5 kapsamındaki bütün
envanteri baştan otomatik başlatma ayrı bir konu olarak kalır.

**Doğrulama:** `test/rf/test_rf_error_retry_scenario.c` kanıt testleri
beklenen yeni davranışa uyarlanmıştır. Gerçek RF komut mekanizması,
envanter sequencer, TX SCP/COBS/CRC üretimi ve karşı parser çalışır.
Yanıt nesnesi doğrudan response girişine verilir; fiziksel UART/RF,
RX dispatch ve gerçek Contiki zamanlayıcısı çalıştırılmaz.

- Arka arkaya 0x05: aynı paket/SEQ ile iki retry, üçüncü hata sonrası
  yalnız bir done callback ve ERR; daha sonra ek gönderim yoktur.
- Timeout: mevcut aynı paket/SEQ ile retry ve sonraki ACK korunur.
- Envanter SET: 0x05 sonrası cihaz atlanmaz; retry ACK sonrası END'e geçer.
- Envanter END: iki 0x05 sırasında active kalır; üçüncü gönderimin ACK'i
  loaded durumuna geçirir.
- BUSY ve üç kalıcı hata: retry yapmadan ERR ile biter.
- Boş ERROR gövdesi: data[0] değeri 0x05 olsa da retry yapılmaz.
- 0x05 ve timeout birlikte: aynı iki hak tüketilir; ek hak oluşmaz.

Yeni beklentiler eski kodda üç testi başarısız yapmıştır (3 geçti / 3
başarısız). Düzeltme ve END testi sonrası 7/7 Ceedling testi geçmiştir.
RF hub simülatörü 74/74 geçmiştir. ARM Release incremental derlemesi
başarılıdır; yeni uyarı görülmemiştir. Çıktılar
`test/build/production-audit-2026-10-03/rf-not-available-before.log`,
`rf-not-available-after.log`, `rf-not-available-hub-sim.log` ve
`rf-not-available-release.log` içindedir. Akış belgesinin 5.3 bölümüne
mevcut uygulama notu eklenmiştir. Fiziksel cihaz testi ve commit henüz
yapılmamıştır. Y7.2'nin 0x05 kısmı kapatılmış; BUSY/durum sorgulama
politikası ve belgede tarif edilen link durumu bu değişikliğe alınmamıştır.

### 9.25 RF UART gönderiminin beklemesi — O7.6, 04.10.2026

**Kanıt:** Normal RF yolu `rf_comm_transmit()` üzerinden
`uart_send_buffer(UART_3, ...)` çağırır. Byte gönderimi UART TXE/TXFNF
bayrağını bekler; bu sırada kooperatif süreç ilerlemez, kesmeler açık
kalır. Mevcut USART3 ayarı 230400 8N1 ve FIFO kapalıdır. 256 byte için
11,1 ms ve mevcut 24 byte envanter paketi için yaklaşık 1 ms değerleri
hat süresi hesabıdır; fonksiyonun gerçek süresi ölçülmemiştir. TXE,
son byte'ın hat üzerindeki iletiminin tamamlandığı anlamına gelmez.

**Durum: Ertelendi.** Kullanıcı bu değerlendirmeyi kabul edip sonraki
bulguya geçilmesini seçmiştir. Mevcut kullanımda zamanlama ihlali veya
veri kaybı gösterilmediğinden DMA/TX kuyruğu eklenmemiştir. Önce fiziksel
cihazda gönderim süresi ve süreçlere etkisi ölçülmelidir. Üretim kodu
ve donanım ayarları değiştirilmemiştir.

### 9.26 Sanal RF keşif komutunun yetkisi — O7.7, 04.10.2026

`rf_shell.c` içindeki `rf disc` sanal EUI üretip gerçek keşif listesine
`rf_discovery_add()` ile ekler. Ana `rf` komutu SHELL_LVL_USER seviyesinde
kayıtlıdır; disc için ayrı SUPER_USER kontrolü yoktur. Yardım metni test
EUI/sanal cihaz olduğunu söyler; keşif listesi web tarafından da okunur.

**Durum: Ertelendi.** Kullanıcı 04.10.2026 tarihinde bu maddenin
geçilmesini seçmiştir. Test işlevi ve mevcut yetki korunmuştur;
üretim kodu değiştirilmemiştir.

### 9.27 RF response fonksiyon bildirimi — O7.8, 04.10.2026

**Kanıt:** `Application/rf/rf_comm.h` yalnız `scp_on_reply()` bildirirken
`rf_comm.c` içinde tanımlanan ve RX dispatch tarafından çağrılan fonksiyon
`scp_on_response()` adındadır. Eski bildirim için bir tanım yoktur.
Mevcut iç çağrı çalışır; header'daki eski adı kullanan dış çağrı link
hatasına yol açar.

**Değişiklik:** Kullanıcı onayıyla header bildirimi `scp_on_response()`
olarak düzeltildi. Callback bağlamı ve dosya açıklamasındaki ad da
uyumlu hale getirildi. Header author alanı repo kuralına göre güncellendi.
Fonksiyon gövdesi, imza parametreleri ve protokol davranışı değiştirilmedi.

**Doğrulama:** `ceedling test:test_rf_error_retry_scenario` 7/7 geçti.
`Application/rf` altında `scp_on_reply` ve `on_reply` kalıntısı bulunmadı.
Çıktı: `test/build/production-audit-2026-10-03/rf-response-header.log`.
Yeni davranış testi gerektiren bir lojik değişiklik yapılmadı.
Bu madde kapatıldı; commit henüz yapılmadı.

### 9.28 RF PING yanıtında ortak builder — O7.9, 04.10.2026

**Kanıt:** `rf_comm.c` içindeki `reply_ping()` ile `rf_scp.c` içindeki
`rf_scp_build_ping_reply()` aynı alan atamalarını ve broadcast sessizliği
kuralını ayrı uyguluyordu. Unicast için boş ACK, ters adresler ve gelen
CMD/SEQ değerleri iki yerde de aynıydı. Çalışma hatası gösterilmemiştir;
bulgu aynı kuralın tekrarlanması ve testli builder'ın bu yolda
kullanılmamasıdır.

**Değişiklik:** Kullanıcı onayıyla `reply_ping()` mevcut builder'ı
çağırır; builder false dönerse göndermez, true dönerse mevcut
`scp_send()` ile ACK gönderir. Tekrarlanan alan atamaları kaldırılmıştır.
Yeni durum, abstraction (soyutlama) veya protokol davranışı eklenmemiştir.

**Doğrulama:** RF senaryo testine iki üretim PING yolu testi eklenmiştir.
Gerçek `reply_ping()`, builder ve SCP/COBS/CRC gönderimi çalışır; ikinci
SCP parser gönderilen paketi çözer. Unicast için hedef/kaynak, ACK tipi,
CMD, SEQ ve boş gövde kontrol edilir. Broadcast için TX çağrısı yoktur.
Komut mekanizmasının bu yanıttan sonra serbest kalması da doğrulanır.
UART taşıması ve fiziksel hub bu testte çalıştırılmaz.

- RF komut/PING senaryoları: 9/9 Ceedling testi geçti.
- RF codec paketi: 4/4 Ceedling testi geçti.
- ARM Release incremental derlemesi başarılı; yeni uyarı görülmedi.

Çıktılar `test/build/production-audit-2026-10-03/rf-ping-path.log`,
`rf-ping-codec.log` ve `rf-ping-release.log` içindedir.
Bu madde kapatıldı; commit henüz yapılmadı.

### 9.29 RF telemetri ve eski akış belgesi — O7.10 / D7.11, 04.10.2026

O7.10 kapsamında gerçek RF telemetrisini veri modeline bağlayan handler'lar
henüz kurulmamıştır. Dummy üreticileri koruma kararı geçerlidir.
D7.11 kapsamında `doc/RF_SCP_Akis.md` hâlâ artık bulunmayan rf_process.c
ve eski FSM/link tasarımını anlatır; 5.3 bölümündeki güncel retry notu
belgenin geri kalanını güncel hale getirmez.

**Durum: Ertelendi.** Kullanıcı bu iki maddenin şimdilik geçilmesini
seçmiştir. Yeni telemetri handler'ı veya belge düzenlemesi yapılmamıştır.

### 9.30 COBS code byte sınır kontrolü — D7.12, 04.10.2026

**Kanıt:** Genel COBS encoder'a 254 sıfır olmayan byte ardından 0x00
verildiğinde 0xFF run (blok) code indeksini 255'e taşır. Çıkış kapasitesi
255 ise sonraki sıfır byte yolu, kapasiteyi kontrol etmeden bu indekse
code=1 yazar. Fonksiyon sonra false döner; ancak sınır dışı yazma
zaten gerçekleşmiştir. Mevcut SCP logical paket sınırı 253 byte'tır;
bu özel tetik mevcut SCP gönderim yolunda erişilebilir değildir.
Genel COBS API'sindeki kusur host testinde doğrulanmıştır.

**Değişiklik:** Kullanıcı onayıyla sıfır byte yolunda code byte yazılmadan
önce `code_idx >= output_size` kontrolü eklenmiştir. Yetersiz kapasitede
false döner. Kütüphane header author bilgisi repo kuralına uyarlanmıştır.
Yeni algoritma, durum veya protokol değişikliği eklenmemiştir.

**Doğrulama:** `test/scp/test_cobs.c` içine iki Ceedling testi eklenmiştir.
255 byte giriş için 255 byte çıkış kapasitesi verilen testte, buffer'ın
önü ve arkasındaki canary (koruyucu byte) kontrol edilmiştir. Eski kod
arka canary'yi 0x5A'dan 0x01'e değiştirmiştir. Diğer test aynı girişi
257 byte kapasiteyle kodlar; iki canary korunur ve gerçek decoder
ile bütün giriş byte'ları yeniden elde edilir.

- Düzeltme öncesi COBS: 13 geçti, bir canary testi başarısız oldu.
- Düzeltme sonrası COBS: 14/14 geçti.
- SCP: 12/12 geçti.
- RF komut/PING senaryoları: 9/9 geçti.
- ARM Release incremental derlemesi başarılı; yeni uyarı görülmedi.

Çıktılar `test/build/production-audit-2026-10-03/cobs-run-before.log`,
`cobs-run-after.log`, `cobs-run-scp.log`, `cobs-run-rf.log` ve
`cobs-run-release.log` içindedir. Bu madde kapatıldı; fiziksel cihaz testi
ve commit henüz yapılmadı.

### 9.31 PowerBoard ve BMS altyapısının değişmesi — 04.10.2026

**Durum: Ertelendi.** Kullanıcı PowerBoard ve BMS altyapısının değişeceğini
belirterek bu alt sistemlere özgü kalan bulguların geçilmesini istemiştir.
BMS raw payload CRC eksikliği (Y8.4), alım çerçeveleme/ortak durum/bayatlık
konuları (Y8.5–Y8.7) bu kapsamda düzeltilmemiştir. PowerBoard kalıcılığı
ve snapshot bulguları için önceki erteleme kararları da korunur.
BSP'nin RTC, UART ve benzeri ortak servisleri bu ertelemenin kapsamında
sayılmamıştır. Mevcut riskler yeni altyapı devreye alınmadan önce yeniden
incelenmelidir; bu karar üretime hazır oldukları anlamına gelmez.

### 9.32 BMS full-map yanıtının doğrulanması — Y8.4, 04.10.2026

**Amaç:** Kullanıcı BMS altyapısı değişecek olsa da bu parser kusurunun
şimdi düzeltilmesini seçmiştir. 9.31 bölümündeki erteleme Y8.4 için
bu bölümle kaldırılır; diğer BMS/PowerBoard ertelemeleri korunur.

**Kanıt:** Reader UART buffer'ını doğrudan BMS_ParseFullMapResponse'a
verir. Başlığı sıyıran bir çağrı veya başka raw payload tüketicisi
repoda bulunmamıştır. Eski parser, beklenen adres/fonksiyon başlığı
olmadığında en az 254 byte veriyi CRC kontrolü yapmadan çözüp geçerli
işaretliyordu. Geçerli başlık yolunda da tam uzunluk ve byte count
zorunlu değildi. Yeni testlerde raw, eksik/uzun, yanlış adres/fonksiyon
ve yanlış byte count senaryoları eski davranışı göstermiştir.
BMS reader init mevcut durumda kapalıdır; kusur bu yapılandırmada
aktif reader yolundan tüketilmez.

**Değişiklik:** Mevcut 259 byte tam yanıt, adres 0x51, fonksiyon 0x03,
byte count 254 ve doğru CRC zorunlu tutulmuştur. Kontroller decode'dan
önce yapılır. CRC'siz raw payload fallback kaldırılmıştır; #if 0
blokları kaldırılmamıştır. Doğru yanıtın veri çözümleme mantığı ve SOH
politikası değiştirilmemiştir. Reddedilen paket hedef snapshot'ı
bozmaz; önceki verinin bayatlatılması Y8.7'nin ayrı konusudur.

Host derlemesindeki üç integer-promotion uyarısı için CRC XOR sonucuna
ve birleştirilen iki CRC byte'ına, 16-bit aralıkları kanıtlanarak açık
dönüşüm uygulanmıştır. CRC algoritması ve byte sırası aynıdır;
uyarı seçenekleri kapatılmamıştır. SOH CRC byte birleştirmesi de bu
kapsamdadır ve ayrı regresyon senaryosuyla doğrulanmıştır.

**Doğrulama:** `test/libs/test_bms_full_map.c` Ceedling'e eklenmiştir.
Gerçek BMS parser ve CRC çalışır; test fixture bağımsız bitwise CRC
üretir. Geçerli paketin ilk hücre değeri 3300 mV ve ayrı SOH verisinin
korunması kontrol edilmiştir. Raw 254 byte, 0/2/258 byte kısa paket,
CRC'si yeniden hesaplanmış 260 byte uzun paket, yanlış adres/fonksiyon,
yanlış byte count ve bozuk CRC reddedilir. Her hata sonrası bütün
hedef yapının önceki örnekle aynı kaldığı kontrol edilir. SOH için
geçerli paket çözümleme ve bozuk CRC reddi de kapsanmıştır.

- Parser düzeltmesi öncesi: 2 geçti / 5 başarısız.
- Düzeltme ve SOH regresyon testi sonrası: 8/8 Ceedling testi geçti.
- ARM Release incremental derlemesi başarılı; yeni uyarı görülmedi.

Çıktılar `test/build/production-audit-2026-10-03/bms-full-map-before.log`,
`bms-full-map-after.log` ve `bms-full-map-release.log` içindedir.
BMS reader etkinleştirilmemiştir; reader çerçeveleme, ISR paylaşımı
ve bayatlık bulguları bu değişiklikle kapatılmış sayılmaz. Fiziksel
BMS testi ve commit henüz yapılmadı. Y8.4 parser kusuru kapatılmıştır.

### 9.33 BMS alım, ISR paylaşımı ve veri bayatlığı — Y8.5–Y8.7, 04.10.2026

**Amaç:** Kullanıcı kalan üç BMS bulgusunun da incelenip düzeltilmesini
istemiştir. §9.31'deki erteleme bu bulgular için kaldırılmıştır.
PowerBoard ertelemeleri ve BMS etkinleştirme kararı korunmuştur.

**Kanıt:** UART5 ISR (kesme işleyicisi) reader buffer'ına byte eklerken
process aynı buffer'ı çözüp sayacı sıfırlıyordu. Sayaç normal integer
olarak iki bağlamda paylaşılıyordu. Eski process her saniye, yanıt henüz
tamamlanmamış olsa da buffer'ı siliyordu. Full-map ve SOH geçerlilik
bayraklarında yaş sınırı yoktu. Kullanılan parser yalnızca verilen
paketi doğrular; reader'ın alım süresi, ortak buffer ve veri yaşı için
başka HAL veya kütüphane kurtarması bulunmamıştır. Reader init mevcut
yapılandırmada kapalı olduğundan bu kusurlar etkin reader yolunda
tüketilmez; testlerde gerçek reader etkinleştirilmiştir.

**Değişiklik:** Mevcut saniyelik, dönüşümlü full-map/SOH sorgulama
korunmuştur. Gönderilen isteğe göre 259 veya 7 byte yanıt beklenir.
ISR bu uzunluklara ulaşıldığında mevcut process'i poll ile uyandırır.
Eksik yanıt bir sonraki isteğe kadar korunur; mevcut bir saniyelik
yanıt penceresi dolunca bırakılır. İşleme anında beklenen uzunluğu
aşan veri ve buffer taşması reddedilir. Taşma sayacı başa sarmaz;
yeni istek alımı yeniden başlatır. Yeni process, kuyruk veya retry
(yeniden deneme) katmanı eklenmemiştir.

Kullanıcı master rolünü ve yalnızca ilgili RX kesmesinin kapatılmasını
hatırlatınca ilk atomic + PRIMASK çözümü sadeleştirilmiştir. RX buffer'ını
yazan tek yol UART5 ISR'dir. Main bağlamında sayacın okunması,
paket kopyalama ve sayacı temizleme yalnızca UART5 IRQ kapalıyken
yapılır. Önceki IRQ açık/kapalı durumu korunur. Global kesmeler
kapatılmaz; USART alıcısı ve pending (bekleyen) IRQ temizlenmez.
Sayaç `volatile uint32_t` olarak tutulur. Koruma volatile özelliğinden
değil, ortak duruma erişirken UART5 ISR'nin dışlanmasından gelir.
Ek atomic işleme ihtiyaç kalmamıştır. Yerel CMSIS `NVIC_DisableIRQ`
DSB/ISB, `NVIC_EnableIRQ` compiler barrier (derleyici bariyeri) içerir.
Decode, log ve UART gönderimi kritik bölümün dışındadır.

Full-map ve SOH için son başarılı yanıt zamanı ayrı tutulur. Her biri
5000 ms boyunca yenilenmezse kendi geçerlilik bayrağı temizlenir.
Son ölçüm değerleri korunur. Bozuk CRC zamanı yenilemez; yeni geçerli
yanıt ilgili bayrağı yeniden açar. Beş saniye, mevcut iki saniyelik
sorgu çevrimine göre seçilen yazılım politikasıdır; BMS donanımının
zorunlu yanıt süresi olarak kabul edilmemiştir. Tick sarması unsigned
zaman farkı ile ele alınmıştır.

**Doğrulama:** `test/libs/test_bms_reader_scenario.c` Ceedling'e
eklenmiştir. Gerçek reader, protothread ve BMS parser çalışır; UART,
tick, timer ve kesme maskesi host ortamında taklit edilir. İlk üç
test eski kodda başarısız olmuştur. Son durumda 11/11 reader testi
geçmiştir. Kapsam: parçalı full-map/SOH, yanlış yanıt türü, poll ile
erken işleme, sonraki istekte eksik yanıtın bırakılması, taşma sonrası
toparlanma, CRC hatasında zamanın yenilenmemesi, bağımsız bayatlama,
tick sarması ve önceden kapalı UART5 IRQ durumunun hem snapshot hem
yeni istek hazırlığında korunmasıdır. Kritik
bölümden çıkarken byte ekleyen senaryo, kopyalama sonrası yeni byte'ın
silinmediğini doğrular. Bu host senaryosu fiziksel kesme gecikmesini
ölçmez.

Sadeleştirme sonrası Ceedling genel paketi 439/439 geçmiştir.
ARM Release son derlemesi başarılıdır; yeni uyarı görülmemiştir.
Test çıktıları `test/build/production-audit-2026-10-03/` altındaki
`bms-reader-before.log`, `bms-reader-after.log`,
`bms-reader-all-final.log`, `bms-reader-uart-irq.log` ve
`bms-reader-uart-irq-release.log` içindedir.
Y8.5–Y8.7 yazılım düzeltmeleri tamamlanmıştır. BMS etkinleştirilmemiş,
fiziksel BMS/UART testi ve commit yapılmamıştır.

### 9.34 Skill ve ajan kurallarının kararlarla uyumu — 04.10.2026

Kullanıcı kararıyla mevcut oturum kararları kalıcı yönergelerle
karşılaştırılmıştır. AGENTS.md/CLAUDE.md, .github C/C++ yönergeleri ve
engineering-guidelines skill'leri uyumlu hale getirilmiştir. Atomic
seçiminden önce erişim kanıtı; mevcut HAL/driver toparlanmasının
incelenmesi; en küçük yeterli çözüm; gerçek davranışı sınayan ve repoda
tutulan regresyon testleri; mevcut yetki ve ertelemelerin korunması
kuralları eklenmiş veya netleştirilmiştir.

İsim önekleri, zorunlu Fatih Ozcan author bilgisi, #if 0 koruması ve
merkezi Ceedling test girişlerindeki çelişkiler giderilmiştir.
Projeye özgü dummy, lifetime, RNG API ayrımı, geliştirme anahtarları,
vendor/Contiki kapsamı ve kapalı BMS kararları
`engineering-guidelines/architecture.md` A08–A11'de kaynaklarıyla
tutulur. NVRAM boyut/ofset kaydı mevcut types.h assert'lerine
uyarlanmıştır; üretim kodu veya NVRAM yerleşimi değiştirilmemiştir.
Donanım skill'inde yalnız build/host testi ile cihaza müdahale yetkisi
ve host/derleme/fiziksel kanıt sınırları ayrılmıştır.

Bu güncelleme yönerge/belge değişikliğidir. Yeni firmware davranışı,
cihaza yükleme, ertelemelerin kaldırılması veya commit içermez.

### 9.35 RTC aralık doğrulamasının güncel incelemesi — O8.8, 04.10.2026

**Kanıt:** GSM'nin iki saat kaynağı ve IEC104 saat komutu rtc_sync()
üzerinden geçer. Mevcut rtc_is_valid() ay 1–12, takvim günü, saat
0–23 ve dakika/saniye 0–59 kontrollerini dizi erişiminden önce yapar.
2026-01-01 kaynak zamanı tabanı da korunur. Eski rapordaki dış kaynaklı
geçersiz ayın bu girişten days_lookup dizisini taşırması mevcut kodda
tekrarlanacak bir açık olarak kabul edilmemiştir. rtc_set() için repo
üretim kaynaklarında çağıran bulunmamıştır. bsp_set_rtc() ayrıca
rtc_load_sw() tarafından kullanılır; donanımdan resync yolu dış kaynak
zaman tabanından bilinçli olarak geçmez.

**Kalanlar:** rtc_is_valid() yılın 0–99 aralığını ve millisec <1000
sınırını kontrol etmez. Yerel HAL yıl sınırını yalnız assert_param ile
kontrol eder; USE_FULL_ASSERT mevcut konfigürasyonda kapalıdır.
IEC104 saat handler'ı yalnız iv_bit alanını kontrol eder; mevcut
cp56time2a_is_valid() fonksiyonunu çağırmaz. Yıl 100, 7-bit wire
alanına sığar; decoder bu alanı doğrudan RTC yapısına taşır. Dolayısıyla
bu değerin rtc_sync() üzerinden HAL'a ulaşabilmesi kaynak kodunda
izlenmiştir. Fiziksel RTC'nin bu değerdeki sonucu ölçülmemiştir.
Millisec sınırı genel RTC API'sindeki eksikliktir; mevcut GSM girişleri
0 üretir, CP56 decoder ise modulo 1000 kullanır. Aynı dış kaynak
ulaşılabilirliği bu alan için iddia edilmemiştir.

**Test hazırlığı:** Kullanıcının isteğiyle test/bsp/test_rtc_sync.c
merkezi Ceedling altyapısına eklenmiştir. On iki senaryo gerçek rtc.c
ve datetime.c kaynaklarını kullanır; BSP depolama ve HAL sınırları
CMock ile taklit edilir. Geçerli saat/epoch/marker, NULL, ay ve gün
sınırları, şubat/artık yıl, saat/dakika/saniye sınırları, kaynak zaman
tabanı, yıl 99/100 ve millisec 1000 kapsam içindedir. Red senaryoları
hiçbir yazma yapılmamasını ve önceki snapshot/epoch/marker değerlerinin
korunmasını ister. Yıl 100 ve millisec 1000 beklentileri mevcut kodda
karşılanmaz; bunlar düzeltme bekleyen regresyon senaryolarıdır.
Host-only RTC HAL bildirimleri mevcut test/support/main.h girişine
bağlanmıştır; firmware header'ları değiştirilmemiştir.

**04.10.2026 test devamı:** Kullanıcı kurulu Ceedling ile testlerin
çalıştırılmasını istemiştir. Ruby 3.4.9 ve Ceedling kuruludur. Normal
sandbox içindeki Ruby başlatması 0xC0000022 hatası vermiş; sandbox dışı
onaylı test çalıştırması başarılı biçimde başlamıştır. Yeniden kurulum
veya test altyapısı değişikliği gerekmemiştir.

- RTC paketi: 12 test, 10 geçti / 2 başarısız.
- Merkezi Ceedling paketi: 451 test, 449 geçti / 2 başarısız.
- İki başarısız senaryo: yıl 100 ve millisec 1000 için beklenen
  reddetme yerine altı yazma çağrısı gerçekleşmiştir. Bu sonuç gerçek
  rtc_sync() ve datetime.c koduyla doğrulanmıştır; HAL/BSP taklittir.
- Diğer 439 test geçmiştir; ortak RTC host header desteği nedeniyle
  ek bir test başarısızlığı görülmemiştir.

Tam çıktı test/build/production-audit-2026-10-03/rtc-all-before.log
içindedir. Testler atlanmamış, beklentiler mevcut hatalı davranışa göre
gevşetilmemiştir. Merkezi test paketi şu anda bu iki regresyon nedeniyle
başarısızdır. Sınır düzeltmesi beklemektedir; hedef ARM derlemesi ve
fiziksel RTC testi bu devam turunda yapılmamıştır. Epoch fixture
değerleri .NET ile bağımsız kontrol edilmiş; git diff --check geçmiştir.
Üretim kodu, IEC104 yanıt politikası, fiziksel cihaz ve commit bu turda
değiştirilmemiştir. O8.8 henüz kapatılmamıştır.

### 9.36 RTC yıl ve milisaniye sınır düzeltmesi — O8.8, 04.10.2026

**Amaç:** Kullanıcı §9.35'teki iki sınır eksikliğinin düzeltilmesini
onaylamıştır. Mevcut rtc_is_valid() kontrolüne year >99 ve millisec
>999 koşulları eklenmiştir. Diğer doğrulamalar ve 2026 kaynak zamanı
tabanı korunmuştur. Geçersiz istek mevcut reddetme yolundan döner;
yazılım saati, donanım saati, epoch ve geçerlilik işareti değiştirilmez.
Yeni API, durum alanı veya zaman politikası eklenmemiştir. Kaynak header
author bilgisi repo kuralına uyarlanmıştır.

**Doğrulama:** Merkezi test/bsp/test_rtc_sync.c senaryoları korunmuştur.
Düzeltme öncesi yıl 100 ve millisec 1000 testlerinde altı yazma çağrısı
oluşurken düzeltme sonrası hiçbir yazma yapılmaz; önceki snapshot,
epoch ve marker korunur. Yıl 99 ve millisec 999 içeren geçerli saat
senaryoları da geçer. Gerçek rtc.c/datetime.c çalışır; HAL ve BSP
depolama sınırları CMock ile taklit edilir.

- RTC paketi: 12/12 Ceedling testi geçti.
- Merkezi Ceedling paketi: 451/451 testi geçti, atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release paketinin raw-byte equality ve ECDSA self-check'i geçti.
- git diff --check geçti.

Çıktılar test/build/production-audit-2026-10-03/ altında
rtc-sync-after.log, rtc-all-after.log ve rtc-sync-release.log içindedir.
Önceki başarısız genel test çıktısı rtc-all-before.log olarak korunur.

RTC dış kaynak girişindeki bu iki sınır eksikliği kapatılmıştır.
IEC104 handler'ının yalnız iv_bit'e göre olumlu yanıt vermesi ayrı açık
konudur: RTC'nin isteği reddetmesi protokol yanıtını kendiliğinden
olumsuza çevirmez. Bu değişiklik IEC104 yanıt politikasını değiştirmez.
Donanımdan resync ve doğrudan BSP setter yolları bu düzeltmeye alınmamıştır.
Fiziksel RTC testi, cihaza yükleme ve commit yapılmamıştır.
### 9.37 Düzeltmeler ve yönergelerin commit kaydı — 04.10.2026

Kullanıcı onayıyla birikmiş değişiklikler ayrı commit'lerde kaydedildi:

- `809748f`: protokol sınırları, Modbus reset/adres, RF retry/PING,
  BMS parser/reader ve RTC sınır düzeltmeleri; ilgili Ceedling testleri
  ve protokol rehberi güncellemeleri birlikte kaydedildi.
- `b3e1f1e`: skill/ajan kuralları ve bağlı engineering-guidelines paketi;
  kullanıcı kararları, erişim kanıtı, koruma seçimi ve test yönlendirmeleri.

Commit öncesi son kayıt: 451/451 Ceedling testi, başarılı ARM Release
derlemesi ve raw-byte equality/ECDSA paket self-check. Skill paketinin
yapı, bağlantı ve UTF-8/LF kontrolleri geçti; staged diff kontrolü temiz.
Bu kayıt önceki bölümlerin o an için yazılmış "commit yapılmadı"
notlarını günceller; erteleme ve fiziksel test sınırlarını kaldırmaz.
Fiziksel cihaz testi, cihaza yükleme ve push yapılmadı.


### 9.38 Mikro-saniye gecikmesinin sınırları — O8.9, 04.10.2026

**Kanıt:** bsp_delay_us() için repo C/H kaynaklarında çağıran
bulunmamıştır. Mevcut 96 MHz saat ve 24-bit SysTick reload alanında
tek parça sınırı 174762 us'dir. Eski kod sıfır istekte LOAD=0xFFFFFFFF,
174763 us istekte LOAD=0x0100001F yazıyordu. Yaklaşık 175 ms ve
sınır üstündeki kısa bekleme değerleri register hesabıdır; fiziksel
süre ölçümü değildir. HAL zaman tabanı TIM17 kullanır; SysTick_Handler
boştur. Mevcut akışta HAL tick kaybı veya saha arızası gösterilmemiştir.

**Değişiklik:** Kullanıcı sıfır girdinin hemen dönmesini ve üst sınırı
aşan sürelerin parçalanmasını onaylamıştır. Sıfır istekte hiçbir
SysTick erişimi yapılmaz. Diğer istekler en fazla 174762 us parçalarla
işlenir; her parçanın ticks/us çarpımı reload alanına sığar. Süre
sessizce kısaltılmaz. Her parça sonrası timer kapatılıp VAL sıfırlanır.
Mevcut busy-wait (bekleme döngüsü) korunur; yeni IRQ, queue veya süreç
eklenmez. Tick/us oranı ve parça sınırı adlandırılmış sabitlerden
türetilmiştir. Static assert'ler tam sayı frekans oranını, en küçük
istekte sıfır olmayan geçerli reload'u ve pozitif parça sınırını
kontrol eder. IRQ kapatma veya watchdog davranışı değiştirilmemiştir.

**Doğrulama:** test/bsp/test_bsp_delay_us.c merkezi Ceedling'e eklenmiştir.
Gerçek bsp.c fonksiyonu çalışır. Host-only SysTick register modeli her
parça tamamlandığında COUNTFLAG üretir; bütün LOAD değerlerinin sınıra
sığmasını, toplam programlanan tick sayısını ve çıkışta CTRL/VAL
sıfırlanmasını kontrol eder. Sıfır, 1 us, 123 us, tek parça sınırı,
sınır+1, iki tam parça, bir saniye ve UINT32_MAX senaryoları vardır.
Sıfır istekte önceki register değerlerinin korunması da sınanır.
LTO yalnız bu test için BSP'nin ilgisiz donanım yollarını dışarıda
bırakır; uyarı seçenekleri kapatılmaz. Test üretim gecikme algoritmasını
taklit etmez; gerçek register zamanlamasını veya fiziksel süreyi ölçmez.

- Düzeltme öncesi: 8 test, 3 geçti / 5 başarısız.
- Düzeltme sonrası: 8/8 gecikme testi geçti.
- Merkezi Ceedling paketi: 459/459 geçti; atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release paketinin raw-byte equality ve ECDSA self-check'i geçti.
- git diff --check geçti.

Çıktılar test/build/production-audit-2026-10-03/ altındaki
delay-us-before.log, delay-us-after.log, delay-us-all.log ve
delay-us-release.log içindedir. O8.9 yazılım sınır kusurları kapatılmıştır.
Fiziksel süre ölçümü, cihaza yükleme ve commit yapılmamıştır.

**Sıradaki madde:** O8.10 PowerBoard işaret uyuşmazlığı §9.31 kapsamındaki
erteleme nedeniyle atlanır. O8.11 I2C kurtarmasını koruma kararı §9.16'da
bulunur. Sonraki incelenecek ortak servis bulgusu O8.12'dir:
uart_send_byte() mevcut durumda önce yazar, ardından TXE/TXFNF bekler.
Gerçek çağrı bağlamları ve mevcut UART/HAL koruması incelenmeden yeni
senkronizasyon veya üretim davranışı eklenmemelidir.
### 9.39 UART yazma oncesi hazirlik kontrolu — O8.12, 04.10.2026

**Kanıt:** Yerel STM32 LL sürücüsünde LL_USART_TransmitData8() yalnız
TDR'ye yazar; TXE/TXFNF beklemez. uart_send_byte(), RS-485 gönderimi,
legacy UART/LPUART yolları ve BMS reader'ın yerel gönderim döngüsü önce
yazıyor, ardından hazır bayrağını bekliyordu. İlk byte göndericinin
hazır olmadığı anda yazılabiliyordu. Generic buffer yolu GSM ve RF,
RS-485 yolu Modbus tarafından kullanılır. Legacy göndericiler için
uygulama çağrısı bulunmamıştır. BMS reader devre dışı kalmaya devam eder.
Sahada byte kaybı veya eşzamanlı yazan bağlam gösterilmemiştir.

**Değişiklik:** Bütün bu byte yazımlarından önce TXE/TXFNF beklenir.
Mevcut yazma sonrası bekleme korunur; fonksiyon dönüşündeki hazır olma
koşulu değişmez. RS-485 son TC beklemesini tamamladıktan sonra DE'yi
bırakır. BMS'nin OE/RE kontrolü korunmuştur. Yeni IRQ maskesi, mutex,
queue, timeout veya retry eklenmemiştir. Bu kontrol, farklı bağlamların
aynı UART'a eşzamanlı yazması için karşılıklı dışlama sağlamaz.

**Doğrulama:** test/bsp/test_uart_tx_scenario.c merkezi Ceedling'e
8 senaryo ekler ve gerçek uart.c çalıştırılır. Host LL modeli ilk
byte öncesinde ve ardışık byte'lar arasında göndericiyi meşgul tutar;
TDR yazımında hazır olmayı zorunlu kılar. Generic byte/buffer, hazır
giriş, geçersiz port, bütün legacy portlar, LPUART ve RS-485'in iki
DE polaritesi kapsanır. RS-485 testleri son TC tamamlanmadan DE'nin
bırakılmasını reddeder. Mevcut 11 BMS reader senaryosu aynı hazır olma
kontrolüyle güçlendirilmiştir ve gerçek yerel gönderim kodunu çalıştırır.
LTO yalnız UART host testi için ilgisiz donanım yollarını dışarıda
bırakır; uyarı kontrolleri korunur.

- Düzeltme öncesi UART: 8 test, 2 geçti / 6 başarısız.
- Düzeltme öncesi BMS reader: 11 test, 11 başarısız.
- Düzeltme sonrası UART: 8/8; BMS reader: 11/11 geçti.
- Merkezi Ceedling paketi: 467/467 geçti; atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release paketinin raw-byte equality ve ECDSA self-check'i geçti.
- git diff --check geçti.

Çıktılar test/build/production-audit-2026-10-03/ altında
uart-tx-before.log, uart-tx-bms-before.log, uart-tx-after.log,
uart-tx-bms-after.log, uart-tx-all.log ve uart-tx-release.log içindedir.
O8.12'nin yazmadan önce hazır olma kontrolü eksikliği kapatılmıştır.
Fiziksel UART testi, cihaza yükleme ve commit yapılmamıştır.

**Sıradaki bulgu:** D8.13 eski yorumlar. led_driver.h donanım açıklaması
active-high derken led_driver.c pin tablosunda sekiz kanalın tamamı
active-low'dur. BMS reader'ın eski 0x51 yorumları önceki çalışmada
0x81 olarak düzeltilmiştir; güncel frame ile tutarlıdır. PowerBoard
prot_ver alanındaki 0x06 yorumu güncel 0x09 sabitiyle uyuşmaz; bu modül
§9.31 ertelemesi kapsamındadır. Bu bölüm D8.13 için tespittir;
yorum düzeltmesi henüz yapılmamıştır.
### 9.40 Commit ve LED yorumlarının düzeltilmesi — D8.13, 05.10.2026

Kullanıcı onayıyla O8.9 ve O8.12 düzeltmeleri, ilgili merkezi Ceedling
testleri ve rapor kayıtları 78158e6 commit'ine alınmıştır. Commit öncesi
son doğrulama 467/467 Ceedling testi, başarılı ARM Release derlemesi ve
raw-byte equality/ECDSA paket self-check'tir. Bu kayıt §9.38 ve §9.39'un
önceki "commit yapılmadı" notlarını günceller. Push yapılmamıştır.

**Kanıt ve değişiklik:** led_driver.c pin tablosunun sekiz girdisi de
active_low=1 kullanır. gpio_defs.h LED3'ü GPIO_B/PIN_1 olarak tanımlar;
Web kanalı pin tablosunda LED3'e bağlıdır. led_driver.h açıklamasındaki
active-high ifadesi active-low olarak ve Web satırındaki LED1/PE7
ifadesi LED3/PB1 olarak düzeltilmiştir. Anlamsız tekrar kaldırılmış ve
dosya başlığına zorunlu yazar bilgisi eklenmiştir. Üretim mantığı,
API ve donanım eşlemesi değiştirilmemiştir.

**Doğrulama:** Yorumlar gerçek pin tablosu ve GPIO sabitleriyle
karşılaştırılmıştır; git diff --check geçmiştir. Yalnız açıklama değiştiği
için yeni test eklenmemiş ve önceki test/derleme tekrar çalıştırılmamıştır.
LED yorum uyuşmazlıkları kapatılmıştır. BMS'nin 0x81 açıklamaları güncel
kodla tutarlıdır. PowerBoard yorum uyuşmazlığı §9.31 ertelemesi kapsamında
açık kalır. LED yorum ve bu rapor değişikliği henüz commit edilmemiştir.
### 9.41 SPI bekleme sınırı ve ADC DMA görünürlüğü — D8.15, 05.10.2026

**Kullanıcı kararı:** D8.14 ölü kod maddesi geçilmiştir. D8.15'in GPIO
kısmı kapsam dışında bırakılmış; SPI ve ADC düzeltmeleri onaylanmıştır.
GPIO port/pin/mode davranışı değiştirilmemiştir.

**SPI kanıtı:** spi_send_byte() TXP ve RXP bayraklarını sınırsız bekliyordu.
Yerel LL fonksiyonları yalnız register bayrağını okur; timeout sağlamaz.
Repo çağrıları w25qxx.c içindedir. SPI2 master, full-duplex, 8-bit ve
DIV32 olarak başlatılır. Veri gelmeyen bir flash cevabı ile SPI register
bayrağının hiç gelmemesi aynı durum değildir; sahada arıza gösterilmemiştir.

**SPI değişikliği:** Her bayrak beklemesi HAL tick üzerinden 10 ms ile
sınırlıdır; tick durmuşken de en fazla 100000 başarısız polling yapılır.
Polling sınırı fiziksel süre değildir; hangisi önce dolarsa bekleme biter.
Tick hesabı unsigned farkla sarmayı destekler. Timeout'ta CS bırakılır,
SPI_TRANSFER_TIMEOUT kayıtlı kalır; sonraki byte/CS-low çağrıları transfer
başlatmaz. Otomatik retry veya çevre birimi reseti eklenmemiştir. Mevcut
w25qxx_reset() sınırında hata açıkça temizlenir; bu temizleme tek başına
SPI donanımının toparlandığını garanti etmez. Byte API'si korunur; hatada
0xFF döner. 0xFF geçerli veri de olabileceği için durum ayrıca sorgulanır.

Flash init, WEL kontrolü, program/erase sonu busy beklemesi ve verify,
SPI hatasını W25QXX_RES_TIMEOUT olarak iletir. Verify, beklenen veri 0xFF
olsa bile transport hatasını başarı saymaz. Eski byte/void flash okuma
API'leri durum dönüşü sunmaz; okuma tüketicilerinin tamamına hata aktarımı
bu değişiklikle kapanmış sayılmaz. Kayıtlı hata yeni flash yazımlarının
başarıyla tamamlanmasını engeller. Veri bütünlüğü ve fiziksel SPI recovery
ayrı kabul konularıdır.

**ADC kanıtı ve değişikliği:** main.c ADC1'i circular DMA olarak başlatır;
MSP kaynağı halfword kaynak/hedef ve artan hedef adresi tanımlar. Yerel
HAL, tampon pointer'ını DMA hedef adresi olarak kullanır. Sürekli yazılan
32-byte aligned dört uint16_t örnekli tampon volatile yapılmıştır.
HAL'ın uint32_t* imzasına dönüşüm yalnız adres teslim sınırında kalır.
CPU okuma yolları volatile niteliğini korur; sıcaklık hesabı örneği bir
kez yerel değişkene alır. Dört kanal için tutarlı toplu snapshot veya
cache coherency güvencesi eklenmemiştir; fiziksel ölçüm testi yapılmamıştır.

**Doğrulama:** Merkezi Ceedling'e test/bsp/test_spi_timeout.c (8 test) ve
test/bsp/test_adc_dma.c (4 test) eklenmiştir. Gerçek spi.c/adc.c çalışır;
LL/HAL sınırları fake ile ayrılır. SPI testleri hazır/gecikmeli bayrak,
TX/RX timeout, tick sarması/durması, CS bırakma, kayıtlı hata ve geçerli
0xFF verisini kapsar. ADC testi gerçek tampon türünü _Generic/static
assert ile kontrol eder; HAL'a verilen hedefe değişen örnekler yazar,
kanal eşlemesi, dönüşüm, geçersiz kanal ve sıcaklık örneğini doğrular.
Sıcaklık fake'i yalnız örneğin LL sınırına aktarımını test eder; STM32
kalibrasyon formülünün doğrulaması değildir. Mevcut flash senaryolarına
beş transport hatası testi eklenmiştir. ARM objdump, üç ADC getter'da
DMA örneği için LDRH okumasını ve sıcaklıkta tek örnek yüklemesini gösterir.

- SPI: 8/8; ADC: 4/4; flash senaryoları: 15/15 geçti.
- Tüm merkezi Ceedling paketi: 484/484 geçti; atlanan test yok.
- ARM Release incremental derlemesi ve raw-byte equality/ECDSA self-check geçti.
- ST stm32u3xx_ll_adc.h:2767'de bir sign-conversion uyarısı vardır;
  önceden kapsam dışı bırakılan ST kaynağı değiştirilmemiştir.
- git diff --check geçti. Fiziksel cihaz testi, yükleme ve commit yapılmadı.

Loglar test/build/production-audit-2026-10-03/ altında
spi-timeout-after.log, adc-dma-after.log, spi-flash-timeout-after.log,
bsp-spi-adc-all.log ve bsp-spi-adc-release.log içindedir.
D8.15 SPI sınırsız bekleme ve ADC okuma görünürlüğü kusurları kapatılmıştır;
GPIO maddesi kullanıcı kararıyla kapsam dışındadır.
### 9.42 SPI CS makroları ve hata ihtimalinin sınırı — 05.10.2026

Kullanıcı isteğiyle SPI_FLASH_CS_HIGH()/SPI_FLASH_CS_LOW() makroları
geri konmuş ve spi_cs_high()/spi_cs_low() yeniden bu makroları kullanmıştır.
Timeout ve durum kontrolü korunur. SPI Ceedling paketi tekrar 8/8 geçmiştir;
git diff --check temizdir. Yalnız makro yönlendirmesi değiştiği için tam
paket ve ARM derlemesi tekrar çalıştırılmamıştır.

Yerel LL kaynakta TXP gönderme FIFO'sunda paket yeri, RXP alma FIFO'sunda
paket verisi gösterir. main.c SPI2'yi enable/start ederek master ve
full-duplex kullanır; repo içinde SPI2'yi sonradan disable/suspend eden
uygulama çağrısı bulunmamıştır. Güncel normal akışta bayrağın takıldığı
somut bir yol veya saha arızası gösterilmemiştir. Timeout olağan flash
cevabının gecikme kontrolü değil, tamamlanmayan çevre birimi aktarımında
sonsuz beklemeyi önleyen savunmadır. Flash'ın ayrılması/yanıt vermemesi
tek başına master RX bayrağının gelmemesini göstermez: SPI master saati
üretir, MISO'dan geçersiz veri örnekleyebilir. Kimlik/veri kontrolü ayrı
konudur. Kaynak: ST AN5543 SPI master/clock ve FIFO açıklamaları.
https://www.st.com/resource/en/application_note/an5543-guidelines-for-enhanced-spi-communication-on-stm32-mcus-and-mpus-stmicroelectronics.pdf

Hata oluşma sıklığı ölçülmemiştir; bir olasılık yüzdesi verilmez. Çevre
biriminin clock/enable/transfer koşulları kaybolursa bayrak ilerlemeyebilir;
bu koşulun mevcut uygulamada oluştuğu kanıtlanmamıştır. Kayıtlı timeout
sonrası transferler açık reset/hata temizleme sınırına kadar engellenir;
otomatik recovery yoktur. Fiziksel cihaz testi ve commit yapılmamıştır.
### 9.43 Tekrarlayan olayların log üretimi — O9.8 incelemesi, 05.10.2026

**Sonuç:** Ortak rate limiter bulunmaması tek başına log flood kusuru
kanıtı değildir. İncelenen PowerBoard/GSM üreticilerinde mevcut tekrar
kontrolleri vardır. Bu akışların log halkasını dakikalar içinde doldurduğu
senaryo veya saha kaydı gösterilmemiştir. Genel baskılama önerilmez;
gerçek alarm/reset geçişlerini saklayabilir. Üretim kodu değiştirilmemiştir.

- Web login: elog.c:973–997 logged_once bayrağı kullanır; tick==0 sentinel
  kusuru zaten düzeltilmiştir. İlk giriş hatası hemen, sonraki kayıt
  unsigned tick farkı 60000 ms'yi aşınca yazılır. Tick sarması bu fark
  hesabında desteklenir. Bu incelemede ayrı web log sınır testi koşulmamıştır.
- PowerBoard: power_board.c:492–520 yalnız geçerli telemetride VBAT_LOW
  alarm_latch bitinin değişimini kaydeder. İlk örnek yalnız başlangıç
  durumunu kurar; aynı bit durumuyla tekrarlanan PUSH/REC paketleri kayıt
  üretmez. Bitin gerçekten 0/1 arasında gidip gelmesi yeni kayıt üretir;
  böyle hızlı değişim saha verisiyle doğrulanmamıştır. Yeni PowerBoard
  altyapısı için önceki erteleme kararı korunur.
- GSM watchdog: gsm_wtd.c busy yolunda 5 dakika, liveness yolunda 10 dakika
  eşik kullanır. Her karardan sonra ilgili zaman damgası yenilenir.
  Liveness iki restart sonrası hard reset ister; busy yolunda iki soft
  recovery sonrası hard reset vardır. Hızlı poll çağrısı tek başına
  eşik dolmadan tekrar kayıt üretmez. Bunlar genel tüm GSM olaylarının
  minimum kayıt aralığı değildir; her üreticinin kendi koşuludur.
- GSM init: gsm_init.c cold_boot_count ile en fazla iki cold boot dener;
  tükenince bir INIT_EXHAUSTED kaydı yazıp reset bekler. SIM değişimi
  gsm_engine.c:1584–1589 saklanan numara ile farklılık olduğunda kaydedilir;
  aynı numaranın tekrar okunması kayıt üretmez. gsm_elog_modem_error()
  için repo C kaynaklarında üretim çağıranı bulunmamıştır.

**Kapasite:** ELOG alanı 8192 byte, iki 4096-byte sektör; kayıt 24-byte
payload + 4-byte seq/CRC'dir. Her sektör 146, halka en fazla 292 kayıt
barındırır. Sektör silinerek yenilendiği için tutulan kayıt sayısı halka
geçişlerinde değişebilir. Yüksek olay hızı eski kayıtların ömrünü kısaltır;
bu kapasite hesabı gerçek yüksek olay hızı gösterildiği anlamına gelmez.

**Doğrulama:** Mevcut gerçek gsm_wtd.c kullanan merkezi Ceedling paketi
4/4 geçti; log: test/build/production-audit-2026-10-03/elog-flood-gsm-review.log.
Testler erken liveness kararını, iki restart sonrası reseti, ping ile
sayacın yenilenmesini ve busy yolunun liveness'tan ayrılmasını kapsar.
Sık poll altında bütün log türlerinin toplam hızını, PowerBoard geçişlerini
ve fiziksel flash aşınmasını ölçmez. Yeni test/kod ve commit eklenmemiştir.

**Önerilen aksiyon:** O9.8'i web sentinel kısmı düzeltilmiş, genel flood
iddiası mevcut üreticilerde kanıtlanmamış olarak değerlendirmek; yeni
rate limiter eklememek. İleride gerçek tekrar sorunu görülürse olay
kodu/payload/zaman izinden gereksiz tekrar ile gerçek durum geçişini ayırıp
yalnız ilgili üretici için politika belirlemek. Regresyon kapsamı
istenirse merkezi Ceedling'e web tick=0/sarma ve GSM aynı tick'te sık poll
senaryoları eklenebilir. PowerBoard geçiş testleri yeni altyapı kapsamında
ele alınmalıdır. Fiziksel cihaz testi ve Release derlemesi yapılmamıştır.
### 9.44 Arıza kaynağı ertelemesi ve RTC okuma incelemesi — D9.10, 05.10.2026

**O9.9 kararı:** Kullanıcı gerçek arıza kaynağının daha sonra ekleneceğini
belirtmiştir. iec104_report_fault_event() kayıt zinciri korunur; yeni
üretici eklenmez. Bu madde ertelenmiştir, kapatılmış değildir.

**D9.10 güncel kanıt:** dt_init() gün/ay uzunluğunu ve artık yılı zaten
kontrol eder; eski Şubat 31 bulgusu mevcut kaynakta geçerli değildir.
ENABLE_64BIT_TIME kapalıdır; üretim 32-bit epoch API'sini kullanır.
Kullanılmayan 64-bit konfigürasyon farkı mevcut derlemede hata değildir.

Log yolları elog.c:82–94 ve iec104_elog.c:69–81, rtc_now() üzerinden
bsp_get_datetime()/rtc_snapshot() yazılım RTC snapshot'ını kullanır.
Snapshot PRIMASK korunarak kısa IRQ kritik bölgesinde alınır; parçalı
okuma için mevcut koruma vardır. Yazılım başlangıç takvimi 01.01.2026'dır.
Dış kaynak rtc_sync() alan/takvim ve 2026 epoch tabanı kontrollerinden
geçer. Normal dış kaynak yolundan geçersiz ay/tarih kabulü gösterilmemiştir.

Açık kalan yol rtc_resync_sw_from_hw()'dir: rtc_hw_read() çıktısı takvim
kontrolü olmadan yazılım RTC'ye ve epoch'a yüklenir. Boot ve periyodik
resync çağıranları yalnız RTC_HW_VALID_MAGIC marker'ını kontrol eder.
Marker geçmişte saatin ayarlandığını gösterir; güncel alan doğrulaması
yapmaz. Yerel HAL_RTC_GetTime()/HAL_RTC_GetDate() register okur, BCD'yi
binary'ye çevirir ve her zaman HAL_OK döner; geçersiz takvim alanını
hata dönüşüyle bildirmez. GetTime ardından GetDate sırası doğrudur.
Bu nedenle bu sürüm için HAL_ERROR okuma senaryosunu gerçek hata yolu
olarak sunmak doğru değildir.

Donanım takvimi marker korunmuşken geçersiz değer içerirse resync bunu
kabul eder; iki log dönüştürücüsü ayrıca doğrulamadığı için hatalı epoch
üretilebilir. Geçersiz ay yazılım takviminde gün değişimi lookup'ını da
etkileyebilir. Bu koşulun sahada oluştuğu, donanım register'ının bozulduğu
veya marker'ın yanlış kaldığı gösterilmemiştir. Kanıtlanan nokta bu girişte
kontrol eksikliğidir; fiziksel hata olasılığına yüzde verilmez.

**Önerilen en küçük aksiyon:** Donanımdan yazılıma yükleme sınırında
okunan takvim/millisec alanlarını doğrulamak; geçersiz okumada mevcut
yazılım saati ve epoch'u korumak. GetDate ile shadow unlock sırası korunmalı,
otomatik reset/retry veya uydurma timestamp eklenmemelidir. Dış kaynak
2026 taban politikası ile kalıcı donanım takviminin takvim geçerliliği
ayrılmalıdır; boot resync'in mevcut yaş kabulü sessizce değiştirilmemelidir.
Alt-saniye hesabında SubSeconds/SecondFraction tutarlılığı da sınanmalıdır.
Merkezi Ceedling'de gerçek resync/boot yolları için geçersiz ay, ayın gün
sınırı, saat alanı ve alt-saniye girdisinde önceki snapshot/epoch'un
korunması; geçerli donanım saati ve markersız boot'un korunması test edilmelidir.

**Doğrulama:** Mevcut rtc_sync paketi 12/12, datetime paketi 9/9 geçti.
Bu testler donanımdan resync doğrulama eksikliğini kapsamaz. Loglar
 test/build/production-audit-2026-10-03/rtc-read-review.log ve
 datetime-read-review.log içindedir. Üretim kodu, yeni test ve commit
 eklenmemiştir; Release veya fiziksel cihaz testi yapılmamıştır.
### 9.45 Önceki düzeltmelerin commit kapsamı — 05.10.2026

Kullanıcı önceki tamamlanmış değişikliklerin commit edilmesini onaylamıştır.
Bu kayıtla aynı commit'in kapsamı LED polarite/pin yorumları, SPI bekleme
sınırı ve durum sorgusu, flash timeout aktarımı, ADC DMA volatile tamponu,
merkezi Ceedling testleri ve §9.40–9.44 inceleme/karar kayıtlarıdır.
SPI_FLASH_CS_HIGH()/LOW() makroları kullanıcı isteğiyle korunmuştur.
Bu commit §9.40–9.42'nin önceki "commit yapılmadı" notlarını günceller.

Son tam merkezi Ceedling sonucu 484/484'tür. Makro geri dönüşünden sonra
SPI paketi tekrar 8/8 geçmiştir. ARM Release ve raw-byte equality/ECDSA
self-check başarılıdır; kapsam dışı ST ADC başlığındaki sign-conversion
uyarısı sürer. İnceleme sırasında GSM watchdog 4/4, RTC sync 12/12 ve
datetime 9/9 geçmiştir. git diff --check temizdir. Loglar önceki bölümlerde
belirtilmiştir; bu commit isteği nedeniyle testler tekrar çalıştırılmamıştır.

D9.10 için önerilen donanımdan resync doğrulaması henüz uygulanmamıştır;
bu commit açık bulguyu kapatmaz. Erteleme kararları ve eski byte/void flash
okuma API'lerinin hata aktarımı sınırı korunur. Fiziksel cihaz testi,
cihaza yükleme ve push yapılmamıştır. Önceden izlenmeyen diğer belgeler
bu commit'in kapsamında değildir.
### 9.46 Donanımdan RTC resync doğrulaması — D9.10, 05.10.2026

Kullanıcı, geçersiz donanım takviminin mevcut sistem saatine yüklenmesini
engelleyen kontrolü onaylamıştır. Önceki §9.44 incelemesinde açık kalan
bu giriş kontrolü uygulanmıştır; sahada donanım bozulması gösterildiği
iddia edilmez.

**Değişiklik:** rtc_resync_sw_from_hw() donanım okumalarından sonra mevcut
rtc_is_valid() takvim/alan kontrolünü kullanır. Geçersiz yıl/ay/gün/saat
ve millisec değerinde yazılım RTC ve epoch setter'ları çağrılmaz; önceki
snapshot, epoch ve backup marker korunur. Reddetme CSLOG_WARN ile bildirilir.
Yazılım saatinin normal tick ilerlemesi değiştirilmemiştir. GetTime ardından
GetDate sırası korunur. rtc_hw_read() alt-saniye çıkarma hesabından önce
SubSeconds <= SecondFraction koşulunu kontrol eder; tutarsız okumada çıkış
nesnesine dokunmaz. Resync'in sıfırlanmış yerel nesnesi takvim kontrolünden
geçemediği için böyle okuma da yüklenmez. Mevcut millisecond formülü ve
SecondFraction=0/SubSeconds=0 davranışı korunur; yeni hesap eklenmemiştir.

Donanım resync'e dış kaynakların 2026 epoch tabanı eklenmemiştir. Geçerli
2025 donanım tarihi ve 2000 artık gününün yüklenmesi korunur. Ortak
rtc_is_valid() artık yıl hesabı desteklenen 2000–2099 aralığı için
iki haneli yılın 4'e bölünmesine göre düzeltilmiştir; 00, 2000 yılıdır
ve 400'e de bölünür. Bu düzeltme dış kaynakların 2026 taban reddini
kaldırmaz. HAL okuma hata dönüşü taklit edilmemiştir: yerel HAL'ın
GetTime/GetDate fonksiyonları her zaman HAL_OK döner. Otomatik
reset/retry, marker temizleme veya uydurma log timestamp eklenmemiştir.

**Doğrulama:** Mevcut test/bsp/test_rtc_sync.c merkezi Ceedling paketine
12 senaryo eklenmiştir. Gerçek rtc.c/datetime.c çalışır; BSP depolaması
ve HAL register sınırı CMock callback ile ayrılır. Callback'ler her
okumada GetTime -> GetDate sırasını ve beklenen okuma sayısını kontrol eder.
Geçersiz ay/gün/saat/yıl, alt-saniye tutarsızlığı, valid marker ile bozuk
boot takvimi ve geçersiz okumadan sonra geçerli resync sınanır. Snapshot,
epoch ve marker'ın korunması; geçerli takvim/ms yüklenmesi, markersız boot,
2025 donanım saati ve 29.02.2000 senaryoları kapsanır.

- Düzeltme öncesi: 24 test, 17 geçti / 7 başarısız; geçersiz girişte
  setter çağrıları eski kodun eksikliğini göstermiştir.
- Düzeltme sonrası RTC paketi: 24/24 geçti.
- Tüm merkezi Ceedling paketi: 496/496 geçti; atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release raw-byte equality ve ECDSA self-check geçti.
- git diff --check geçti.

Loglar test/build/production-audit-2026-10-03/ altında rtc-resync-before.log,
rtc-resync-after.log, rtc-resync-all.log ve rtc-resync-release.log içindedir.
D9.10 donanımdan resync girişindeki takvim doğrulama eksikliği kapatılmıştır.
Doğrudan BSP setter'ları veya HAL yazma hata politikası bu değişikliğin
kapsamında değildir. Log timestamp formatı ve saat kurulmamışken kullanılan
başlangıç tarihinin politikası değiştirilmemiştir. Fiziksel cihaz testi,
cihaza yükleme ve commit yapılmamıştır.
### 9.47 Flash silme öncesi WEL kontrolü — D9.12, 05.10.2026

**Kanıt ve karar:** Kullanıcı silme öncesi Write Enable sonucunun
kontrolünü onaylamıştır. Karttaki AT25SF321B'nin üretici belgesi WEL=0
iken erase komutlarının yürütülmediğini belirtir. Mevcut ortak 4/32/64 KB
silme ve chip erase yolları Write Enable gönderiyor, fakat bu bitin
set olduğunu okumadan erase komutunu gönderiyordu. Sonraki BUSY=0 sonucu
tek başına silmenin kabul edildiğini göstermez. Bu koşulun cihazda
oluştuğu gösterilmemiştir; doğrulanan nokta önkoşul kontrolü eksikliğidir.
Kaynak: https://www.renesas.com/en/document/dst/at25sf321b-datasheet?r=1608806

**Değişiklik:** w25qxx_erase_() ve w25qxx_erase_chip(), mevcut Write Enable
ve delay adımından sonra Status Register 1 okur. SPI durum sorgusu önce
değerlendirilir: hata varsa W25QXX_RES_TIMEOUT dönülür. SPI sağlıklı,
WEL=0 ise W25QXX_RES_WEL_NOT_SET dönülür. Her iki durumda erase opcode'u
ve adresi gönderilmez. WEL=1 ise mevcut opcode/adres/CS ve son BUSY
bekleme akışı korunur. Yeni hata kodu, API, retry veya reset eklenmemiştir.
WEL=1 yalnız silme iznidir; koruma bitleri veya diğer koşullar nedeniyle
silmenin başarıyla tamamlandığını tek başına garanti etmez. Erase sonrası
veri doğrulaması bu değişikliğin kapsamında değildir.

**Doğrulama:** test/libs/test_w25qxx_address_encoding.c merkezi Ceedling
paketine altı senaryo eklenmiştir; gerçek w25qxx.c çalışır, SPI sınırı
CMock ile ayrılır. Dört silme girişinde WEL=0 için yalnız Write Enable
ve status okuması beklenir; fazladan erase komutu testi başarısız kılar.
Transport timeout, hem WEL set görünen 0xFF hem WEL sıfır görünen 0x00
okumalarında izin değerlendirmesinden önce reddedilir. Geçerli sektör/
blok adres byte sıraları korunur; chip erase'te WEL=1 sonrası C7 komutu
ve BUSY -> idle polling sınanır.

- Düzeltme öncesi flash paketi: 21 test, 14 geçti / 7 başarısız.
- Düzeltme sonrası flash paketi: 21/21 geçti.
- Tüm merkezi Ceedling paketi: 502/502 geçti; atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release raw-byte equality ve ECDSA self-check geçti.
- git diff --check geçti.

Loglar test/build/production-audit-2026-10-03/ altında
flash-erase-wel-before.log, flash-erase-wel-after.log,
flash-erase-wel-all.log ve flash-erase-wel-release.log içindedir.
D9.12'nin silme öncesi WEL kontrolü kısmı kapatılmıştır. Aynı maddede
geçen JEDEC dört-byte okuması bu değişiklikte değiştirilmemiştir ve ayrı
protokol incelemesi gerektirir. Fiziksel cihaz testi, cihaza yükleme ve
commit yapılmamıştır.
### 9.48 RTC ve flash WEL düzeltmelerinin commit kapsamı — 05.10.2026

Kullanıcı tamamlanan değişikliklerin commit edilmesini onaylamıştır.
Bu kayıtla aynı commit'in kapsamı D9.10 donanımdan RTC resync doğrulaması,
D9.12 silme öncesi WEL kontrolü, ilgili merkezi Ceedling testleri ve
§9.46–9.47 rapor kayıtlarıdır. Bu commit önceki iki bölümün
"commit yapılmadı" notlarını günceller.

Son tam Ceedling sonucu 502/502, RTC paketi 24/24 ve flash paketi 21/21'dir.
ARM Release incremental derlemesi ve raw-byte equality/ECDSA paket
self-check başarılıdır; son derleme logunda warning/error yoktur.
git diff --check temizdir. Commit isteği nedeniyle testler yeniden
çalıştırılmamıştır. JEDEC dört-byte okuması ayrı inceleme olarak kalır.
Fiziksel cihaz testi, cihaza yükleme ve push yapılmamıştır. Önceden
izlenmeyen diğer belgeler bu commit'in kapsamında değildir.
### 9.49 JEDEC kimliğinin üç byte okunması — D9.12, 05.10.2026

**Kanıt:** Karttaki AT25SF321B için 9Fh Read Manufacturer and Device ID
komutu bir manufacturer ve iki device ID byte'ı tanımlar: 1F-87-01.
Kaynak: Renesas datasheet §12.1.
https://www.renesas.com/document/dst/at25sf321b-datasheet
Mevcut w25qxx_read_jedecid() dört byte okuyordu. Repo C kaynaklarında
tek çağıran w25qxx_init() kimlik eşlemesini ilk üç byte ile yapar; dördüncü
byte'ın mevcut uygulamada yanlış parça seçimine neden olduğu gösterilmemiştir.
Bu madde düşük öncelikli protokol tutarlılığı düzeltmesidir.

**Değişiklik:** Kullanıcı onayıyla JEDEC tamponu üç byte yapılmıştır.
Okuma döngüsü size_t indeks ve sizeof(tampon) kullanır; uint32_t dönüşe
yalnız bu üç byte aynı sırayla yerleştirilir. Üst byte sıfır kalır. API,
9Fh komutu, CS makroları, parça tablosu ve init kimlik kontrolü korunur.

**Doğrulama:** Merkezi test/libs/test_w25qxx_address_encoding.c paketinde
eski dört-byte beklentisi üç-byte beklentisine dönüştürülmüş ve iki
senaryo eklenmiştir. Gerçek w25qxx.c çalışır; CMock SPI sınırında tam üç
read bekler, dördüncü read'i reddeder. Winbond örneği EF-40-16,
kart parçası 1F-87-01 ve üçüncü byte'ında yüksek bitler bulunan EF-40-F0
girdilerinde byte sırası ve sıfır üst byte doğrulanır. Son girdi yalnız
bit paketleme sınamasıdır; desteklenen flash parçası olduğu iddia edilmez.

- Düzeltme öncesi flash paketi: 23 test, 20 geçti / 3 başarısız;
  üç JEDEC testi fazla okuma nedeniyle başarısızdır.
- Düzeltme sonrası flash paketi: 23/23 geçti.
- Tüm merkezi Ceedling paketi: 504/504 geçti; atlanan test yok.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release raw-byte equality ve ECDSA self-check geçti.
- git diff --check geçti.

Loglar test/build/production-audit-2026-10-03/ altında
jedec-three-byte-before.log, jedec-three-byte-after.log,
jedec-three-byte-all.log ve jedec-three-byte-release.log içindedir.
D9.12 WEL kısmı §9.47'de, kalan JEDEC okuma kısmı bu bölümde kapatılmıştır.
Fiziksel flash testi, cihaza yükleme ve commit yapılmamıştır.
### 9.50 IEC104 soket verisinin AT logunda gösterilmesi — 05.10.2026

**Kullanıcı isteği:** AT#SRECV=3,1024 yanıtındaki IEC104 verisinin konsol
logunda gösterilmesi istenmiştir. Önceki payload omitted etiketi uyarı
veya veri atılması değil, bütün SRECV yanıtları için ortak log gizlemesiydi.
SRING:3,6 bir soket veri bildirimidir; altı byte ASDU içermeyen IEC104
kontrol çerçevesi olabilir. Kullanıcının gerçek frame byte'ları görülmediği
için U/S türü veya kontrol komutu kesin olarak belirlenmemiştir.

**Değişiklik:** at_engine2.c log_response(), komutun AT#SRECV=3, önekiyle
başladığını cmd_len sınırı içinde kontrol eder. Sondaki virgül 30/31 gibi
başka socket ID'lerinin eşleşmesini engeller. IEC104 listener socket 3
VERBOSE seviyesinde AT yanıt tamponunun tamamını iki haneli hex byte'larla
gösterir. Böylece binary 00/04 gibi byte'lar nokta olarak kaybolmaz.
AT header ve modem sonlandırıcısı da hex gösterime dahildir; bytes alanı
halen response_buffer_len, yani bütün AT yanıtının boyutudur. IEC104
çerçevesi başlık içinden ayrı boyutta raporlanmamıştır. HTTP ve diğer
SRECV yollarının tam/kısmi/hatalı yanıt gizlemesi korunmuştur. Bilinmeyen
soket payload marker'ı gizlenmeye devam eder. Normal modem cevaplarının
metin/escaped CR-LF gösterimi ve VERBOSE kapısı korunur. Soket okuma,
IEC104 parser veya veri teslim akışları değiştirilmemiştir.

**Doğrulama:** test/gsm/test_at_socket_log.c merkezi Ceedling'e eklenmiştir.
Gerçek at_engine2.c ve log_response() derlenip çalıştırılır; CMock BSP/GSM
log seviyesi sınırlarını, capture fake konsol çıktılarını ayırır. LTO yalnız
bu testte ilgisiz AT/hardware yollarını dışarıda bırakır; sıkı warning
seçenekleri kapatılmamıştır. Host main shim yalnız UART DMA imzalarını
sağlar; gerçek DMA çalıştırıldığı iddia edilmez. Sekiz test IEC104 binary
ve kısmi timeout görünürlüğü, 29-byte AT zarfında altı-byte kontrol frame'i,
HTTP secret gizlemesi, socket 30 ayrımı, bilinmeyen marker, normal CSQ
metni ve VERBOSE kapısını kapsar. Web auth entegrasyonundaki mevcut
üretim-fonksiyonu çıkarım testi de IEC104 görünürlüğü/socket ayrımıyla
güçlendirilmiştir.

- Düzeltme öncesi ilk yedi log testi: 5 geçti / 2 başarısız.
- Son AT soket log paketi: 8/8 geçti.
- Tüm merkezi Ceedling paketi: 512/512 geçti; atlanan test yok.
- Web auth entegrasyonu: tam/kısmi socket secrets gizli, IEC104 hex görünür,
  socket 30 gizli ve mevcut HTTP/kimlik kontrolleri geçti.
- ARM Release incremental derlemesi başarılı; logda warning/error yok.
- Release raw-byte equality ve ECDSA self-check geçti.
- git diff --check geçti.

Loglar test/build/production-audit-2026-10-03/ altında
iec104-socket-log-before.log, iec104-socket-log-after.log,
iec104-socket-log-all.log, iec104-socket-log-web-auth.log ve
iec104-socket-log-release.log içindedir. Kullanıcının gönderdiği gerçek
trafik cihazda tekrar yakalanmamıştır. Fiziksel cihaz testi, firmware
 yükleme ve commit yapılmamıştır. Değişiklik yeni firmware yüklendiğinde
 mevcut VERBOSE seviyesinde görünür olacaktır.
### 9.51 Formatter/ISR yeniden giriş incelemesi — O10.13, 05.10.2026

**Ara kararlar:** Kullanıcı O10.6 xsprintf çağrı değişikliklerini şimdilik
geçmiştir. Y10.5 xscanf açıklaması maddesi ve O10.10 history maddesinde
kod değişikliği yapılmadan sonraki maddelere geçilmiştir. O10.12 için
repo üretim kaynaklarında %llb çağrısı bulunmamıştır; kullanıcı bu maddeyi
geçmiştir. Bu karar tüm 64-bit veri kullanımının bulunmadığı anlamına gelmez.
D9.13 incelemesinde eski flash haritası test kopyası bulunmamış, Stored
Entries gerçek kayıt sayımıyla eşleşmiş ve fault akımı gösterimindeki
açık double cast doğrulanmıştır. Saklanan fault_current alanı ve amper
getter'ı float'tır; variadic formatter double bekler. Bu cast veri
modelini veya kayıt boyutunu değiştirmemiştir.

**O10.13 sonuç:** xsprintf/xsnprintf ortak strptr/strptr_end kullanır;
iç içe iki tampon biçimleme çağrısına genel yeniden giriş güvencesi verilemez.
Ancak incelenen üretim akışında normal ISR'nin bu formatter'ları çağırdığı
bir yol doğrulanmamıştır. UART1 RX ring'e, TX tamamlanması bayrağa; UART3
RF RX ring'e, UART4 Modbus RX tampon/bayrağa, UART5 BMS RX tamponuna yazar.
LPUART RX shell/XMODEM durumuna aktarılır. Timer tick atomik uptime,
Contiki timer/clock ve yazılım RTC'yi günceller. EXTI panic ISR yalnız
GPIO/flag işidir; panic logu ana bağlamda yazılır. I2C callback ve yerel
MSP re-init yollarında formatter çağrısı bulunmamıştır. ISR process_poll()
fonksiyonu yalnız poll bayraklarını yazar; süreç callback'lerini anında
çalıştırmaz. XMODEM byte ISR, event bitlerini kurar; olay işleme poll'dadır.

CSLOG/CCSLOG, rtc_print_now() ve xprintf/xcprintf ile seçili çıktı
callback'ine doğrudan yazar; timestamp için xsprintf çağırmaz. SHELL_LOG,
xfprintf(shell_putchr, ...) kullanır. Normal bsp_putchr ve web capture
callback'leri yeniden formatlama yapmaz. Formatter'ın sayı tamponu,
va_list ve döngü durumu yereldir. Non-NULL çıktı callback'ine yazmak
strptr/strptr_end'i değiştirmez. Dolayısıyla her ISR konsol çıktısının
ana tamponu bozacağı şeklindeki genel iddia doğru değildir; iç içe
tampon formatter'ı veya çıktı hedefi değişimi ayrıca kanıtlanmalıdır.
Contiki süreçlerinin normal kooperatif akışı bu incelemede paralel RTOS
thread yürütmesi olarak kabul edilmemiştir. Genel mutex, atomic pointer
veya bütün biçimleme boyunca IRQ kapatma önerilmez.

**Ayrı fault bağlamı:** HardFault_Handler_C() önce fault trace'i TAMP backup
register'larına yazar, sonra konsol enable bayrağını açıp dump basar.
web_shell default_rx_handler() ise komut yürütmesi boyunca xfunc_output'u
web_shell_putchar'a yönlendirir. Bu aralıkta HardFault oluşursa handler
çıktı hedefini değiştirmediği için dump UART yerine web capture'a gidebilir.
Bu koşullu hedef seçimi kaynakta görünür; sahada gerçekleştiği gösterilmemiştir.
Fault handler geri dönmez; TAMP trace, dump'tan önce saklandığı için
bu yönlendirme tek başına backup kanıtının kaybolduğunu göstermez.
Bu, normal ISR'nin strptr yarışından ayrı bir teşhis çıktısı konusudur.

**Önerilen aksiyon:** Normal formatter yollarını korumak. İstenirse public
sözleşmede tampon formatter'larının aynı anda/iç içe çağrılamadığını belirtmek.
Fault dump için hedef garanti edilecekse HardFault dump başlamadan mevcut
BSP konsol çıktı callback'ini açıkça seçmek ve web hedefi aktifken konsol
hedefine dönüldüğünü test etmek. Bu turda yönlendirme veya API değişikliği
yapılmamıştır. Shell oturum/ISR tasarımı önceki erteleme kapsamındadır.

**Doğrulama:** Mevcut test/libs/test_xsnprintf_format_scenario.c merkezi
Ceedling paketi 18/18 geçti. Buradaki interleaved_calls testi ardışık
tamamlanmış çağrıları sınar; ISR preemption veya iç içe formatter testidir
şeklinde sunulmaz. Log: test/build/production-audit-2026-10-03/
formatter-isr-review.log. Genel yeniden giriş güvenliği veya fiziksel
kesme davranışı kanıtlanmamıştır. Yeni test/kod, Release derlemesi,
cihaz testi ve commit yapılmamıştır.
### 9.52 Shell/API ve karakter genişliği düzeltmeleri — D10.15, 05.10.2026

Kullanıcı güncel incelemede belirtilen dört tutarsızlığın düzeltilmesini
onaylamıştır. %c bir karakter formatıdır; %5c beş sütuna sağdan, %-5c
soldan yerleştirir. Aktarılan char, variadic sınırda int olarak okunur.

**İmza ve C++ bağlantısı:** LPUART ISR'deki shell_on_rx_received(uint8_t)
yerel extern kaldırılmış, USER CODE Includes alanına shell.h eklenmiştir.
Ortak gerçek int imzası kullanılır; alınan byte'ın 0–255 değeri korunur.
Core dosyasındaki değişiklikler Header/Includes/LPUART USER CODE alanlarıyla
sınırlıdır; bu alanlar çıkarıldıktan sonra önceki dosyayla metin eşitliği
kontrol edilmiştir. ST lisans başlığı korunmuştur. xprintf.h içinde xcprintf
ve xprintf_set_color bildirimleri mevcut extern "C" ve XF_USE_OUTPUT bloğuna
alınmıştır. Gerçek formatter C olarak derlenmiş; iki fonksiyonu çağıran
C++20 programı bu nesneyle linklenip çalışmıştır.

**Renk:** SHELL_CLOG renk argümanını bir kez değerlendirir; NULL değilse
shell_putchr yolu üzerinden ANSI renk dizisi, metin ve renk reseti gönderir.
Global xfunc_output'a yönlenmez. Web terminalinin metin capture'ında ANSI
CSI dizileri süzülür; UART renkli kalırken web'de [31m/[0m gibi parçalar
ve gereksiz truncation oluşmaz. Escape durumu her request capture başında
sıfırlanır. Metnin mevcut JSON quote/backslash/newline kaçışı korunur.
Bu uyum renk argümanının uygulanmasının web yanıtına etkisini karşılar;
yeni history/TAB özelliği veya shell oturum mimarisi eklenmemiştir.

**Karakter genişliği:** xvfprintf() %c kolunda mevcut width/left-align
bilgisine göre boşluk ekler. %c/%1c davranışı ve int argüman tüketimi
korunur. Dinamik pozitif/negatif genişlik desteklenir. xsnprintf tampon
sınırı ve truncation-clamped dönüş sözleşmesi korunur; padding için yeni
heap veya geçici tampon eklenmemiştir. Ertelenen xsprintf çağrı dönüşümü
ve 64-bit binary tampon genişletmesi bu değişiklikte yapılmamıştır.

**Doğrulama:** test/libs/test_xsnprintf_format_scenario.c içine üç senaryo
 eklenmiştir: snprintf ile statik/dinamik karakter hizalama karşılaştırması,
 dar tampondaki canary/sınır ve NUL karakterinin padding/dönüş sayısı.
 test/libs/test_shell_color_output.c merkezi Ceedling'e beş senaryo ekler:
 gerçek SHELL_CLOG makrosu/xprintf.c/web_shell.c çalışır, shell writer sınırı
 CMock callback ile ayrılır. UART renk/reset, NULL renk, web CSI süzme ile
 JSON kaçışları, capture'lar arası durum sıfırlama ve renk dizilerinin text
 kapasitesini tüketmemesi kapsanır.

- Karakter testleri düzeltme öncesi 21 test: 18 geçti / 3 başarısız.
- Düzeltme sonrası formatter paketi 21/21; shell renk paketi 5/5 geçti.
- Tüm merkezi Ceedling paketi 520/520 geçti; atlanan test yok.
- Tam paket sonrasında web renk testinin JSON kaçış kontrolü güçlendirilmiş,
  ilgili paket tekrar 5/5 geçmiştir; üretim kodu bu adımda değişmemiştir.
- C++ header/C formatter link ve çalıştırma kontrolü geçti.
- ARM Release incremental ve raw-byte equality/ECDSA self-check başarılı;
  logda warning/error yok. git diff --check geçti.

Loglar test/build/production-audit-2026-10-03/ altında
character-width-before.log, character-width-after.log, shell-color-after.log,
api-formatter-all.log ve api-formatter-release.log içindedir. C++ kontrol
kaynak/nesne/programı xprintf-linkage-review adıyla aynı build alanındadır.
Bu dört tutarsızlık kapatılmıştır. D10.15'in eski rapordaki diğer NULL,
switch/default ve parser-bound maddeleri ayrı inceleme gerektirir.
Fiziksel UART/web cihaz testi, cihaza yükleme ve commit yapılmamıştır.
### 9.53 JEDEC, IEC104 log ve API/formatter commit kapsamı — 05.10.2026

Kullanıcı tamamlanan değişikliklerin commit edilmesini onaylamıştır.
Bu kayıtla aynı commit'in kapsamı §9.49 üç-byte JEDEC okuması, §9.50
IEC104 socket 3 hex logu, §9.52 dört API/formatter düzeltmesi, ilgili
merkezi Ceedling/web auth testleri ve §9.51 inceleme/erteleme kayıtlarıdır.
Bu commit §9.49, §9.50 ve §9.52'nin önceki "commit yapılmadı" notlarını
günceller; kalan D10.15 maddelerini kapatmaz.

Son tam merkezi Ceedling sonucu 520/520'dir. Formatter 21/21, shell renk
paketi 5/5, AT soket logu 8/8 ve flash paketi 23/23 geçmiştir. Son web renk
paketi ayrıca JSON kaçış kontrolüyle 5/5 geçmiştir. Web auth entegrasyonu,
C++ header/C formatter bağlantı kontrolü, ARM Release incremental derlemesi
ve raw-byte equality/ECDSA self-check başarılıdır. Son Release logunda
warning/error yoktur; git diff --check temizdir. Commit isteği nedeniyle
testler tekrar çalıştırılmamıştır. CubeMX değişikliklerinin yalnız
USER CODE alanlarında kaldığı kontrol edilmiştir. Fiziksel cihaz testi,
cihaza yükleme ve push yapılmamıştır. Önceden izlenmeyen diğer belgeler
commit kapsamına alınmamıştır.
/*** end of report ***/

## 10. RF-SCP uygulama kanıtları

### 10.8. RF-SCP R1 örnek doğrulama — 05.10.2026

Kullanıcı yeni görevde BOLATeX R1 davranışlarının RTU tarafına eklenmesini
istemiştir. MH modem üzerindeki RF hub'dır; PWRB bildirimleri de RTU'ya
bu SCP arayüzünden gelecektir. Önce servis/shell, sonra ürün bağlantıları
sırası kabul edilmiştir. Olay, başarılı gönderim veya başarılı kalıcı
kayıt sonrasında tüketilebilir; merkez ACK'i ayrıca beklenmeyecektir.

İlk adımda 14 senaryo CSV'sindeki 175 çerçeve takip edilen fixture'a
dönüştürülmüştür. Python standart kütüphanesiyle çerçeve ve olay iç
CRC'leri denetlenmiş; fixture güncellik kontrolü geçmiştir. Gerçek
`scp.c`/`cobs.c` üzerinde yeni 6/6 Ceedling testi geçmiştir: örnek
decode/encode, her parçalama noktası, konsol/arka arkaya alım, bozuk
CRC ve yarım çerçeve sonrası toparlanma. Bu döngülerin 175 vektörü
ayrı Unity testleri olarak sayılmamıştır.

Üretim firmware kaynağı bu adımda değiştirilmemiştir. Y7.2/O7.5 hata
politikası, O7.10 canlı veri üreticisi ve G-01 gibi mevcut bulguların
kapanışı bu test sonucu ile değiştirilmemiştir. R1 işleyicileri ve
PWRB tüketici geçişi henüz uygulanmamıştır. Merkezi bütün testler,
integration, ARM derlemesi ve cihaz kabulü bu adımda çalıştırılmamıştır.

Kanıt: `test/scp/test_scp_capture_scenario.c`,
`test/fixtures/rf_scp_vectors.h`,
`test/scripts/generate_rf_scp_vectors.py --check` ve ilgili Ceedling
sonucu. Ayrıntılı sıra `doc/RF_SCP_MODEM_UYGULAMA_PLANI.md` içindedir.

### 10.9. RF-SCP R1 codec ve alan doğrulaması — 05.10.2026

Yeni `Application/rf/rf_scp_codec.c/.h`, R1 istek haritasını doğrular ve
ACK/ERROR/SET payload'larını çözer. Mevcut rf_scp API'leri korunmuştur.
Powerboard ailesi dahil R1 codec'leri eklenmiş; yeni kaynak henüz
rf_comm dispatch veya ürün tüketicilerine bağlanmamıştır.

`test/rf/test_rf_scp_codec.c` içinde 18/18 test geçmiştir. İlgili RF
config/retry/JSON, eski codec, SCP/COBS ve örnek paket testleri birlikte
71/71 geçmiştir. Tüm istek TYPE/uzunluk haritası, örnek gelen paketler,
hatalı girişte önceki çıktının korunması, ayırıcı 22 alan sınırları,
NaN/Inf, iç olay CRC'si, 32 bit süre ve ham PWRB endian alanları sınanmıştır.

GCC 14.3.rel1 Cortex-M33 hard-float `-Os` ile yeni modül bağımsız object
olarak sıkı uyarı seçeneklerinde derlenmiştir: text 3292 B, data/bss 0 B.
Yerel stack frame ölçümleri decode 272 B, event decode 160 B,
build_request/config validate 16 B'dır. Harici semboller memcpy, memset
ve mevcut rf_scp_decode_status ile sınırlıdır. Bu ölçüm toplam çağrı
zincirinin stack bütçesi veya tam firmware boyutu/kabulü değildir.
Object/stack kanıtı `build/rf-scp-r1/rf_scp_r1.o/.su` altındadır.

O7.10/G-01 gerçek veri üreticisi ve Y7.2/O7.5 işlem hata politikası
bu adımda kapanmış sayılmaz. İstek timeout/retry/SEQ ve dispatch bağlantısı
sonraki adımdadır. NVRAM, Flash yerleşimi, mevcut dummy/BMS kararları
korunmuştur. Tam merkezi test, integration, CubeIDE link ve cihaz
kabulü bu adımda yapılmamıştır; commit veya yükleme yoktur.

### 10.10. RF-SCP istek mekanizması ve adlandırma — 05.10.2026

Kullanıcı kaynak/API adlarında belge sürümü kullanılmamasını istemiştir.
Üretim codec'i `rf_scp_codec.c/.h`, girişleri rf_scp_build_packet,
rf_scp_decode_message, rf_scp_decode_event ve rf_scp_validate_config
olarak adlandırılmıştır. Test/fixture/generator ve plan dosyası da aynı
tercihe göre adlandırılmıştır. Tarihsel object ölçüm yolu §10.9'da
gerçek eski artifact adıyla kalır; kaynak R1 sürüm atfı korunur.

Mevcut tek aktif istek altyapısına codec doğrulaması bağlanmıştır.
Yanıtta MH/RTU adresi, CMD/SEQ, TYPE ve payload; CFG2 GET/SET'te özel
ACK uzunluğu kontrol edilir. Hatalı paket ve boş ERROR bekleyen isteği
tamamlamaz. Timeout aynı paket/SEQ kullanır; BUSY ve CFG2/ham telemetri
NOT_AVAILABLE sonrası mevcut timeout aralığında yeni SEQ ile girişim
yapılır. Bütün denemeler aynı bütçededir. GEN uyuşmazlığı ve CFG_READ_ALL
NOT_AVAILABLE otomatik tekrarlanmaz. Bu, §9.24'teki önceki kullanıcı
kararını yeni R1 uygulama yetkisiyle günceller.

Yeni scp_send_request doküman başlangıç timeout/retry değerlerini seçer.
Eski API korunur. Bridge açıkken istek/PING/retry durur; eski budget
harcanmaz. Bildirimler aktif istek sırasında doğrulanıp yönlendirilir
ve ACK almaz. Henüz eklenmemiş durum/ölçüm tüketicileri tamamlanmış
sayılmaz; BOOT/envanter özel politikaları sonraki adımdadır.

RF command senaryoları 25/25, ilgili sekiz RF/SCP test dosyası 87/87
geçmiştir. RX senaryolarında gerçek ISR girişi, ring buffer, parser ve
dispatch çalışır; UART/tick/bridge dış sınırları taklit edilir.
Kaynak/SEQ/payload reddi, kayıp/geç ACK, bildirim araya girmesi,
GEN/ERROR biçimi, retry tükenmesi ve sayaç sarma doğrulanmıştır.

CubeIDE managed Release build yeni codec'i kaynak/objects listesine
dahil etmiş ve tamamlanmıştır: 0 hata, daha önce kapsam dışında bırakılan
8 Contiki + 5 ST + 1 Core uyarısı. rf_comm/rf_inventory/rf_scp_codec
ayrıca GCC 14.3.rel1 Cortex-M33/C11 ve bütün sıkı uyarı seçenekleriyle
`-Werror` altında derlenmiştir. ELF text/data/bss 300308/524/125144 B'dır;
bu toplam mevcut diğer kullanıcı değişikliklerini de içerir, bu görevin
boyut farkı olarak sunulmaz. Mevcut 4096 B stack yerleşimi korunmuştur.
Toplam çağrı zinciri/ISR payı ve fiziksel kabul ölçümü yapılmamıştır.

Kanıtlar `build/scp-release-build.log`, `build/scp-strict-compile.log`,
`build/scp-target/` ve ilgili Ceedling sonuçlarıdır. Donanım yükleme,
reset, NVRAM değişikliği ve commit yapılmamıştır. Y7.2 tablosu güncel
kanıta bağlanmış; O7.5 açılış/envanter adımı nedeniyle açık kalmıştır.

### 10.11. RF açılış, saat, envanter ve keşif — 05.10.2026

Kullanıcı kararına göre geçersiz saatte TIME_SYNC atlanır, envanter
yüklenir; saat geçerli olunca eşitlenir. Yeni ACK envanteri yeniden
başlatmaz. Geçerli saatle başlangıçta TIME_SYNC önce gönderilir;
saatlik yenileme yüklenmiş envanteri korur. Geçersiz RTC/takvim değeri
wire (hat) paketine yazılmaz. Mevcut yerel RTC kaynağı kullanılır;
NTP'nin mevcut varsayılan timezone (saat dilimi) değeri +3'tür.

BOOT eski pending isteği SCP_CMD_RESTARTED ile sonlandırır, envanter
imlecini temizler ve yeni SEQ ile yükler. Tek baytlık BOOT tekrar/restart
ayrımı sağlamadığı için her geçerli BOOT resync talebi olarak ele alınır.
Major uyuşmazlığı envanter/yazmayı durdurur; eski ACK yeni isteği bitirmez.
Kısmi/boş/hatalı ve tam envanter durumları ayrı izlenir. Nonempty PARTIAL
MH'de loaded olabilir; bu tam başarı olarak gösterilmez. Hatalı/bölgesi
farklı girdilerde sonsuz bekleme yoktur. Local (yerel) 10 s upload gap
sınırı izlenir; bu fiziksel UART/MH işlem sürelerinin ölçümü değildir.

Discovery EUI/signed RSSI tutulur; yinelenen rapor sinyali yeniler,
dolu kuyruk yeni cihaz için eski girdiyi silmez. INVENTORY_UPDATE ve
silme servisi ACK öncesinde discovery girdisini kaldırmaz; NVRAM'i
kendisi değiştirmez. Yeni rf time ve rf epoch <1..4> shell komutları
vardır; epoch ACK yalnız broadcast kuyruğudur ve yaklaşık 30 s bekleme
bildirilir. Otomatik MH kart değişimi çıkarımı yapılmaz.

Haberleşme/açılış paketi 46/46; dokuz ilgili RF/SCP/CP56Time2a test
dosyası toplam 122/122 geçmiştir. Gerçek timer/CP56/RX/ring/dispatch
yolları çalışır; donanım ve NVRAM erişim sınırları taklit edilir.
Kanıt: `test/build/scp-startup-tests.log` ve ilgili Ceedling sonuçları.

Beş RF modülü GCC 14.3.rel1/Cortex-M33/C11 ve bütün sıkı uyarılarla
-Werror altında derlenmiştir. Release main-build/link geçmiştir;
text/data/bss 302136/524/125200 B'dır. Kanıtlar
`build/scp-startup-strict-compile.log` ve
`build/scp-startup-release-build.log` içindedir. Bu total (toplam)
diğer mevcut kullanıcı değişikliklerini de içerir. 14 mevcut kapsam dışı
Contiki/ST/Core uyarısını kapatma veya susturma işlemi yapılmamıştır.

O7.5 güncel host kanıtına bağlanmıştır. O7.10/G-01 ölçüm/arıza üreticileri,
Powerboard SCP kaynağına tüketici geçişi, web Save ve kalıcı kayıt
bağlantısı sonraki adımlardadır. NVRAM/linker düzeni, dummy ve kapalı BMS
kararları korunmuştur. Cihaz yükleme/reset, fiziksel kabul ve commit yoktur.

### 10.12. Kanonik RF canlı veri ve monitor — 05.10.2026

Kullanıcı sahada cihaz olmadığını ve eski RF modeline geriye dönük
uyumluluk gerekmediğini bildirdi. rf_monitor_t/DEVICEID32 ve unsigned
RSSI/int8 sıcaklık gibi eski monitor alanları kaldırılmıştır. RF modeli
EUI-64, float ölçüm, int16 sıcaklık, signed RSSI ve R1 alanlarını korur.
Monitor endpoint/tablosu bu modele geçirilmiştir; eski field adapter'ı
yoktur. Sentetik RF örneği gerçek envanter/cache'i değiştirmez.

Web Save desired store'u MH ACK'inden önce değiştirebildiğinden,
kaynak baytı yalnız fider/faz taşıyan LIVE için yanlış EUI etiketi
ulaşılabilir durumdur. Yeni ACK envanter aynası bu karışmayı önler;
atama/silme/taşınma yalnız geçerli eşleşen ACK ile güncellenir.
Eski ölçüm yeni cihaza bağlanmaz. Mevcut NVRAM düzeni bu adımda değişmez.

30 s LIVE tazeliği, SEQ boşluğunun kayıp sayılmaması, ayrı numeric
quality (sayısal geçerlilik), TRIP anlık akımının arıza akımından ayrılması
ve iki anomali yolu uygulanmıştır. AY uptime sıfırlaması Trip_Failed
alarmını latched tutar; MH BOOT alarmı silmez. Operatör onayı yalnız
live flag sıfırken kapanır. Bu durum RAM'dedir; RTU güç kesintisi sonrası
kalıcı geçmiş garantisi değildir. Event 101/105 bağlantısı olay adımındadır.

RF senaryo paketi 64/64, dokuz ilgili dosya 140/140 geçmiştir. Yeni JSON
yanıtı tam 12 faz/en büyük değerlerle mevcut HTTP 8192 B tamponunda
test edilmiş; küçük tampon taşması ve yarım yanıt engellenmiştir.
Kaynak/gömülü web sayfasında Türkçe/İngilizce yeni alan gösterimleri
geçmiştir. Kanıt `test/build/scp-live-tests.log` ve web_navigation paketidir.

Sekiz RF/monitor modülü GCC 14.3.rel1/Cortex-M33/C11 sıkı uyarılar ve
-Werror altında derlenmiştir. Release main-build/link tamamlanmıştır:
text/data/bss 305168/524/126048 B; toplam diğer kullanıcı değişikliklerini
de içerir. Loglar `build/scp-live-strict-compile.log` ve
`build/scp-live-final-build.log` altındadır. JSON builder yerel stack
frame'i 400 B'dır; çağrı zinciri/ISR toplamı fiziksel kabulde ölçülmelidir.

O7.10 güncel kanıta bağlanmış; G-01 merkez/fiziksel kabul kapısı henüz
kapanmamıştır. Diğer modüllerin dummy kararları ve kapalı BMS durumu
korunmuştur. Source RC ve model handler'ları actual (gerçek) üretim
kodudur; hardware (donanım) sınırları host'ta taklit edilir. Cihaz
yükleme/reset, linker değişikliği, NVRAM migration veya commit yoktur.

### 10.13. RF tam olay günlüğü ve örnek iletişim — 05.10.2026

R1 §4.7 olay kaydı 60 B ve tüm olay türlerini içerir. Mevcut 18 B
fault_log_t ile tam kaydın saklanması mümkün değildir. Kullanıcı mevcut
kalıcı/geçici arıza alanlarının korunmasını ve ayrı 8 KB alanı onayladı.
Yeni bölge 0x232000–0x233FFF adreslerindedir. Önceki Flash alanları ve
NVRAM adres/schema düzeni değişmedi; adresler Ceedling'de doğrulanır.

60 B ham paket + 4 B spi_flash_log ek bilgisi = 64 B entry. İki 4096 B
sektörde toplam 128 kayıt sığar. Sektör silinirken diğer 64 kayıt korunur;
yazım sonrasında 65–128 kayıt tutulur. Bu sınırlı geçmiş alanıdır.
Kütüphanenin yeniden açılış, yarım yazım ve ertelenmiş erase davranışları
kullanılır. Yeni kuyruk, heap veya retry katmanı eklenmedi.

rf_event_log_append yalnız iç CRC/uzunluk, başarılı yazma ve geri
okumada sıra numarası/tüm bayt eşleşmesi sonrasında true döner. Sessiz
CRC kaybında önceki aynı içerikte kayıt başarı kanıtı sayılmaz. Sürücü
okuma API'si void olduğu için doğrudan okuma hata kodu yoktur; boş Flash
ile genel okuma hatası ayrımının sürücü sınırı korunur. Fiziksel enerji
kesintisi dayanımı host testiyle tamamlanmış sayılmaz.

AY_05b örneğindeki HEAD/RANGE/CONSUME yanıtları değişmeden gerçek UART
ring/SCP parser/rf_comm yolunda sınanır. Bütün 60 B alanlar sabit
beklenen değerlerle doğrulanır. CSV'nin 0x47 ACK'i R1'e aykırıdır;
R1 gereğince ACK üretilmez. Test istek/yanıt bağımlılıklarını mantıksal
sırayla yürütür; CSV timestamp sırası uygulama kuralı sayılmaz.

Yeni kalıcı kayıt paketinde 11/11; on bir ilgili RF/SCP/CP56/SPI-log
dosyasında 157/157 test geçmiştir. Fixture kontrolü 175 frame'i doğrular.
Kanıt `test/build/scp-event-tests.log`. Dokuz RF/monitor modülü sıkı
uyarılar/-Werror ile Cortex-M33/C11 altında geçmiştir. CubeIDE Release
derleme/link sıfır uyarıyla bitmiştir: text/data/bss 305348/524/126096 B.
Bu derleme değişen kaynakları kapsar; önceki kapsam dışı vendor/Contiki
uyarı kararı sürer. Loglar `build/scp-event-strict-compile.log` ve
`build/scp-event-release-build.log`; toplam diğer kullanıcı değişikliklerini
de içerir. Cihaz yükleme/reset veya commit yapılmadı.

Otomatik olay çekme → kalıcı yazma → CONSUME bağlantısı henüz yoktur.
Yeni günlük başlangıçta açılır; kayıt API'si bir sonraki servis parçasında
bağlanacaktır. 101/105 alarm bağlantısı, merkez aktarımı, FRAM degraded
akışı ve Powerboard kaynak geçişi açık olduğundan G-01 tamamlandı sayılmaz.

### 10.15. Otomatik RF olay tüketimi ve arıza listesi — 05.10.2026

Kullanıcı olay 1/7'nin kalıcı, olay 3'ün geçici arıza listesine
yönlendirilmesini ve mevcut sürenin uint32_t olmasını onayladı.
Enum açıklamaları bu kararı taşır. fault_log_t 18 B'den 20 B'ye,
fider görüntüsü 1852 B'ye çıktı; mevcut 4096 B ana/yedek sektörlerine
sığar. FAULT_LOG_SCHEMA_VERSION 2 oldu. NVRAM ve Flash alan adresleri
değişmedi. Sahada cihaz olmadığı için eski görüntü migration'ı yoktur.
IEC104 günlük entry boyutu 24 B, kapasitesi sektör başına 170;
sekiz sektörde en çok 1360, sektör silinirken 1190 kayıttır.

Ulaşılabilir kaynak-zaman kaybı doğrulandı: fault_log_add_log mevcut
timestamp'i cp56time2a_now ile değiştiriyordu. Yeni fault_log_append
kaynak zamanını değiştirmeden mevcut private temporary/permanent
girişlerini kullanır; çağıran sync sonucunu kontrol eder. RF aktarımı
Fider_ID−1 yerine zone/Fider_ID ile store indeksini bulur. Clock quality
IV bitine aktarılır; RMS amper mevcut x10 helper'ı üzerinden saklanır.
R1 yük-akımı-var biti eski Below biti için terslenir. Line_ID=0,
eşleşmeyen kaynak veya temsil edilemeyen ölçüm için liste kaydı
uydurulmaz; ham 60 B paket korunur.

rf_events mevcut tek bekleyen SCP komutunu ve tek dört kayıtlık tamponu
kullanır. Envanter sonrasında/bildirimde/60 s kontrolde HEAD çekilir;
tail'den RANGE okunur. Ham yazma ve gereken arıza listesi/sync başarılı
olmadan CONSUME gönderilmez. Sync yeniden denemesi aynı kaydı RAM'e
ikinci kez eklemez; raw yazma da tekrar edilmez. Restart veya kayıp
CONSUME sonrası yeniden çekme kopya üretebilir; kalıcı tekilleştirme
garantisi yoktur. Kalıcı kayıt kullanıcı kararıyla teslim koşuludur;
merkez ACK'i beklenmez.

Yerel depolama hatası nedeniyle beklenmişse CONSUME öncesi HEAD tekrar
çekilir. Tail/total farkıyla eski yuvanın ezilmesi denetlenir; indeks
sarma nedeniyle eski değerine dönse de total değişimi görülür. Eski
yuvadan sonra yanlış yeni kayıtlar tüketilmez, güncel HEAD ile yeniden
başlanır. Normal başarılı örnek akışa ek sorgu eklenmez. Bu kontrol SCP
yanıtı ile sonraki istek arasını atomik yapmaz; fiziksel zamanlama
ve MH ring davranışı kabulde doğrulanmalıdır.

Kısa batch, 99→0 sarma ve geçerli önekin tüketilmesi uygulanır. Bozuk
iç CRC tüketilmez; yalnız MH ERROR 0x06 verdiğinde bir yuva atlanır.
Consume ERROR 0x02 sonrasında yeni HEAD alınır. Ortak retry bütçesi
sonrasında BUSY devam ederse FRAM tanısı çekilir; degraded=1 olay
akışını BOOT'a kadar durdurur. head=tail kullanıcı cevabına göre boş
kabul edilir; aynı head için pending=100 bildirimi dolu halkayı ayırır.
Yaklaşık free_slots kesin tüketme kararı için kullanılmaz. 60 s RTU
poll/yerel tekrar aralığı plan tercihidir; MH fiziksel süresi değildir.

15 ilgili Ceedling dosyasında 246/246 test geçti. Olay servisi 23 testi
kapsar. Merkezi fault_log paketi mevcut
NOR/çift kopya fixture'ındaki 53 kontrolü ve kaynak-zaman/UINT32_MAX
Flash yeniden açılış testini gerçek üretim koduyla çalıştırır. Servis
taşıma/kayıt API sınırlarını taklit eder; kayıt algoritmaları ayrı NOR
testlerinde sınanır. Eski fixture conversion/unused istisnaları yalnız
bu pakettedir; yeni RF servisinin sıkı uyarı kapsamı korunur. Önceki
UART/SCP örnek testleri yeni servise bildirim aktarımını da doğrular.
14 CSV/175 frame kontrolü geçmiştir; örnek kaynaklar değiştirilmedi.
Kanıtlar `test/build/scp-event-service-tests.log` ve
`test/build/scp-event-final-tests.log`.

On RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçmiştir.
CubeIDE yeni kaynağı managed build listesine almış, Release link
307860/524/126568 B text/data/bss ile tamamlanmıştır. Kanıtlar
`build/scp-event-service-strict.log`, `build/scp-event-service-release.log`
ve `build/scp-event-service-final-build.log`. Toplam diğer kullanıcı
değişikliklerini de içerir; fiziksel Flash/UART süreleri kanıtlanmaz.

101/105 alarm bağlantısı, yeni olayların IEC104 replay/spontane üretici
bağlantısı, Powerboard tüketici geçişi ve saha kabulü henüz tamamlanmadı;
G-01 kapatılmaz. IEC104 süre ölçümü mevcut float wire tipidir: büyük
değerlerde her milisaniyenin tam gösterimi garanti edilmez, kalıcı
uint32_t ve ham kayıtta tam değer korunur. Cihaz yükleme/reset veya
commit yapılmamıştır.

### 10.16. RF olaylarının IEC104 aktarım bağlantısı — 05.10.2026

RF-SCP'nin önceki kapsamı kullanıcı isteğiyle a635d61 commit'ine alındı.
Sonraki çalışma mevcut IEC104 producer'ın kaynak zamanı yerine yeni
zaman oluşturduğu ve sync hatalarını yok saydığı girişe doğrudan gitmez.
Kaynak-zaman/32-bit-süre taşıyan hazır 1/7/3 kaydı mevcut event_log_add,
emit_evtlog_record, mark_sent ve sync API'lerine aktarılır. Hat kapalı
veya anlık gönderim reddedilmişse unsent kayıt replay sürecinde kalır.

IEC104 günlük ekleme veya NVRAM sync başarısızsa MH tüketimi bekler.
Sync tekrarında ham/list/IEC104 kaydı ikinci kez eklenmez; spontane
gönderim de yeniden denenmez. Bir hazır fault kaydı batch işlenirken
tutulur; config değişimi yeniden denemede kaynak eşlemesini değiştirmez.
Yeni public API/process/kuyruk yoktur. Reboot sırasında kopya mümkün
olmaya devam eder; kalıcı tekilleştirme yapılmadı. 101/105'in geç gelen
kaydında LIVE açılış kimliğini taşımadığı için alarm kararı kullanıcıya
sorulmuş; bu cevap gelmeden alarm kodu değiştirilmemiştir.

27 olay servisi testi dahil 15 ilgili dosyada 250/250 test geçti.
Dört yeni senaryo online gönderim/mark_sent, gönderim reddi/replay,
IEC104 yazma ve NVRAM sync hatasıdır. Servis sınırları taklit edilir;
gerçek frame üretimi mevcut IEC104 protokol paketi kapsamındadır.
On RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti.
Mevcut IEC104 Init makrosunun HAL struct alanıyla çakışması include
sırasıyla giderildi; vendor/macro tanımı değiştirilmedi. Release link
307972/524/126584 B text/data/bss; toplam diğer kullanıcı değişikliklerini
de içerir. Kanıtlar `test/build/scp-replay-tests.log`,
`build/scp-replay-strict.log`, `build/scp-replay-release.log`.

Bu sonraki parça henüz commit edilmedi. 101/105, Powerboard ve grup ayarı,
fiziksel UART/Flash/SCADA kabulü açık olduğundan G-01 kapanmadı. Cihaza
yükleme veya reset yapılmadı.

### 10.17. BOLATeX soruları ve grup bloğu hazırlığı — 05.10.2026

Kullanıcı protokol belirsizliklerini ayrı belgede toplamayı ve geç
101/105'i BOLATeX cevabıyla netleştirmeyi istedi. Dokuz konu
doc/BOLATEXE_SORULACAKLAR.md BQ-01–09 altında kaynak/kanıt/soru/mevcut
uygulama/cevap alanlarıyla tutulur. Geç alarm, halka dolu/boş ayrımı,
overwrite/CONSUME, örnek ACK/zaman sırası, reset sayaçları, olay 122
işlem kimliği, epoch tamamlanması, E8 biçimi ve değişen envanterde
eski kayıt kimliği kapsanır. RTU tercihleri vendor cevabı sayılmaz.
101/105 için geçici politika uygulanmadı; açık madde düzeltme sayılmaz.
Belge BOLATeX'e gönderilmedi.

Grup hazırlık API'si device/default Fider_ID'yi bırakıyor ve blok CRC'si
hesaplıyordu; R1 §4.10 hedef fider ve CRC/reserved sıfırlama ister.
API üretimde henüz çağrılmadığından ulaşılabilir saha hatası iddiası
yoktur. Yaklaşan grup servisine hazırlık olarak Fider_ID RAM'den alınır,
aynı writable veriler korunur, CRC/reserved sıfırlanır. Mevcut codec
doğrulaması kullanılır; başarısızlıkta çıktı, alias dahil, korunur.
API imzası ve RAM/NVRAM düzeni değişmedi; CFG_READ_ALL sorgusu eklenmedi.

Ayar paketi 10/10; 15 ilgili Ceedling dosyası 253/253 geçti. Altı gerçek
AY_06/AY_06b WRITE bloğu, invalid fider/NaN/Inf/null ve alias sınırları
sınandı. Yeni codec bağımlılığı RF JSON ve NVRAM integration build'inde
tanımlandı; NVRAM paketi 433/433 kontrol geçti. On bir RF/monitor modülü
Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti. Release link başarılı:
307972/524/126584 B text/data/bss; toplam diğer kullanıcı değişikliklerini
de içerir. Helper'ın aktif çağıranı henüz olmadığı için link'te atılabilir.
Kanıtlar `test/build/scp-group-block-regression.log`,
`build/scp-group-nvram-integration.log`, `build/scp-group-block-strict.log`
ve `build/scp-group-block-release.log`.

WRITE×3/COMMIT/STATUS, APPLIED/desired kalıcılık, Powerboard ve fiziksel
kabul henüz tamamlanmadı; G-01 kapanmadı. Yeni commit veya cihaz
yükleme/reset yapılmadı.

### 10.18. Arıza sınıflama sorusu ve grup durum sorgusu — 05.10.2026

Kullanıcı geçici/kalıcı arızaların tanımını BOLATeX'e sormayı istedi.
doc/BOLATEXE_SORULACAKLAR.md BQ-10 tüm event_trigger eşlemesini,
algılama/sonuç ayrımını, çift sayımı, 100/101/105'i ve sayaç artışını sorar.
Mevcut 1/7 kalıcı, 3 geçici RTU kabulü korunmuş; enum üstüne BQ-10 teyit
notu eklenmiştir. BQ-11, WRITE'ta group_id bulunmazken COMMIT öncesi
ABORT kimliğini sorar. Açık sorular ertelenmiş olup düzeltme sayılmaz;
belge henüz BOLATeX'e gönderilmemiştir.

Bağımsız normal sorgu rf cfg-status <0..255> üzerinden eklendi. Mevcut
tek bekleyen komut/codec/UART kullanılır. Callback MH state/bitmap/reason/
cfg_crc/attempts gösterir; yerel APPLIED veya NVRAM durumu güncellenmez.
Group_id eşleşmesi kontrol edilir; busy çağrı bekleyen isteğin kimliğini
değiştirmez. Yanıt SHELL_LOG kanalına gider. Yeni kuyruk/process/API yoktur.

Haberleşme paketi 69/69; 15 ilgili Ceedling dosyası 256/256 geçti.
Üç yeni test örnek APPLIED gövdesinin gerçek UART/SCP sorgu yanıtı olarak
alınması, invalid argüman/busy ve ID 0/255 sınırıdır. On bir RF/monitor
modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti. Release link
308704/524/126584 B text/data/bss; toplam diğer kullanıcı değişikliklerini
de içerir. Kanıtlar test/build/scp-group-status-regression.log,
build/scp-group-status-strict.log ve build/scp-group-status-release.log.

WRITE×3/COMMIT sıralayıcısı, ABORT temizliği, eski 122 eşlemesi, kalıcılık
ürün kararı ve saha kabulü halen açıktır. G-01 kapanmaz; yeni commit veya
cihaz yükleme/reset yapılmadı.

### 10.19. Normal RF grup uygulaması ve doğrulama — 06.10.2026

Mevcut rf_comm WRITE×3/COMMIT sıralayıcısı içermiyor ve 0x21 bildirimi
işlenmiyordu. rf_group tek RAM snapshot'ı ve mevcut tek bekleyen komutla
bağlandı. Üç farklı/nonzero EUI, ACK envanteri, zone/fider/faz ve 22 alan
doğrulanır. Önce kimlik sorgulanır; R1'e göre eski APPLIED kimliğinin
COMMIT'i yeni uygulama başlatmadığından kullanımda kimlik WRITE öncesi
reddedilir. Üç aynı 96 B blok ve COMMIT ilerler. NVRAM/Save/API store
işlemi değiştirilmedi; yeni process/retry katmanı yoktur.

COMMIT ACK WAITING'dir. APPLIED ancak bitmap=7 ve expected writable CRC
ile doğrulanır. Farklı CRC/eksik bitmap yerel MISMATCH'tır; ham MH raporu
korunur. Member bitmap kabul edilen EUI sırasına göre gösterilir. Kısmi
başarısızlık otomatik yeniden uygulamaz. Notify kaybı için 5 s STATUS_GET
poll'u kullanılır; MH'nin yaklaşık 60/120 s limitleri yerel sahte FAILED
sonucuna dönüştürülmez. Komut timeout'ı UNCERTAIN olur; geç kesin bildirim
sonucu kurtarabilir. Erken terminal notify geç ACK ile ezilmez, gecikmiş
progress terminal sonucu geri almaz. USER_ABORT=10 CANCELLED bilgisidir.

Group ID ve üyeler işlem boyunca sabittir; desired store değişimi bloğu
karıştırmaz. ACK envanteri WRITE/COMMIT öncesi yeniden kontrol edilir.
EPOCH_REFRESH ACK'inden sonra ilgili fidere 30 s yerel bekleme eklenir;
diğer fiderler bağımsızdır. Bu RF tamamlanma kanıtı değildir, BQ-07 açıktır.
BOOT RESTARTED sonucudur. WRITE yapılmamış read-only probe güvenle iptal
ve tekrar edilebilir; gereksiz MH reseti gerekmez. WRITE sonucu belirsizse
yeni iş kilitlidir; BQ-11 cevaplanmadan bilinmeyen ABORT gönderilmez.
Kimliği bilinen COMMIT operatörle ABORT edilebilir; ERROR iptal sayılmaz.

Shell: rf cfg-apply <store line 1..7> <fresh group_id>, rf cfg-state,
rf cfg-status <group_id>, rf cfg-abort. Yanıtlar SHELL_LOG'tadır. Bu
komutlarla gerçek cihazda uygulama/iptal yapılmadı. K5 desired/APPLIED
NVRAM politikası, BQ-06 eski 122 eşlemesi ve BQ-11 temizliği ayrı açık
kalemlerdir; normal akışın tamamlanması bunların çözümü sayılmaz.

Grup paketi 23/23, haberleşme 73/73; 16 ilgili Ceedling dosyasında
283/283 geçti. Gerçek config/store/CRC çalışır; NVRAM ve transport/ACK
envanter sınırları taklit edilir. AY_06'nın EUI/ayar/0x096D CRC ve üç
durum gövdesi örneklerden alınır. Unmodified wire bildiriminin gerçek
RX/ring/parser/dispatch'ten servise ulaşması ve ACK üretmemesi de sınandı.
Erken/geç notify/ACK, wrong CRC/bitmap/group/type, PARTIAL, abort/red,
probe iptali/timeout, fider 4, timer wrap ve epoch beklemesi kapsandı.
Kanıt test/build/scp-group-service-regression.log içindedir.

On iki RF/monitor modülü Cortex-M33/C11 sıkı uyarılar/-Werror ile geçti.
CubeIDE yeni kaynağı managed build'e ekledi. Son Release link
311704/524/126768 B text/data/bss; diğer kullanıcı değişikliklerini de
içerir. Loglar build/scp-group-service-strict.log,
build/scp-group-service-release.log, build/scp-group-service-final-build.log.
Yeni commit veya cihaz yükleme/reset yapılmadı. BOLATeX soruları,
Powerboard tüketici geçişi ve fiziksel kabul açık; G-01 kapanmadı.

### 10.21. SCP Powerboard tüketicileri — 06.10.2026

Mevcut `app_main` eski I²C Powerboard process'ini başlatıyordu;
`system_status` ve `modbus_power_stats` bu kaynağı okuyordu. Kullanıcının
MH üzerinden SCP kararıyla E1/E3 için tek `power_board_scp` modeli eklendi.
Gerçek RF dispatch modele bağlıdır. Sistem/web/Modbus/shell aynı modeli
okur; eski process artık başlatılmaz. Kaynak dosyaları, GPIO power-panic
ve kapalı BMS reader korunmuştur.

Kullanıcı 49200 tabanında 38 register yeni SCP haritasını onayladı.
Birim/kalite tanımları Modbus haritası §7.2'de, model/alarm/günlük sınırları
[uygulama planındaki Powerboard bölümünde](doc/RF_SCP_MODEM_UYGULAMA_PLANI.md#powerboard-tüketici-geçişi--06102026)
kaydedildi. Modbus Poll dosyası da güncellendi. 30 s yerel eskime RTU
tercihidir; eski ham değer kalite bitleriyle korunur, web geçersiz değeri
boş gösterir. SCP'de olmayan ısıtıcı/panel akımı/ortam sıcaklığı uydurulmaz.
E1 mask telafisi ve E3 32 bit alarm/RESET/son nefes oturumu olay günlüğüne
bağlandı; ham bildirim sayısı benzersiz kesinti sayısı değildir.

20 ilgili Ceedling dosyasında 319/319 geçti. Gerçek RX wire örnekleri,
model, JSON ve FC03 yanıtı sınandı; hardware/elog sınırları taklit edildi.
14 CSV/175 frame fixture kontrolü, web navigation kaynak/gömülü ve web_auth
paketleri geçti. 16 Cortex-M33 modülü sıkı C11 uyarılar/-Werror ile geçti;
Release link başarılıdır: text/data/bss 307400/504/126792 B; eşzamanlı
diğer değişiklikler de dahildir. Kanıt `test/build/scp-power-regression.log`,
`build/scp-power-strict.log`, `build/scp-power-final-build.log`,
`build/scp-power-web-auth.log`. Bu seçili test sonucu tam suite geçişi olarak yorumlanmaz.

E5–E8 ayar/sonuç/ham telemetri bağlantıları, BQ-08 ve K9 adres seçimi
henüz tamamlanmadı. Fiziksel UART/RF/enerji kesintisi kabulü yapılmadı;
G-01 kapatılmadı. Bu adımda commit veya cihaz yükleme/reset yapılmadı.

### 10.22. SCP Powerboard ayar ve komut servisi — 06.10.2026

Mevcut codec E5–E8'i doğruluyordu; ürün kontrol tüketicisi yoktu.
Eski Powerboard shell ayarı I²C STATBLK gölgesine yüzde C-oranı/tcal
ile yazıyordu. Yeni `power_board_control` mevcut SCP request/retry
mekanizmasını kullanır. E5 GET/GEN ve yalnız müşteri maskeli SET,
20–32 s sonra yankı/E1 doğrulaması, E6 kabul ile E7 sonucu ayrımı,
iptal akıbeti ve E8 ham teşhis shell'e bağlandı. Yeni NVRAM alanı yoktur;
kalıcı CFG2 MH'dedir. Normal kapasite/C-oranı uygulanmadan, ret veya
belirsiz yazımdan sonra akü değişti komutu gönderilmez.

Ayarsız değer doğrulaması, E7 FF bekleme/bitiş ayrımı ve ayarsız periyot
b7 koruması BQ-12–14'e eklendi. Kullanıcı ayarsız kapasite/C-oranı için
ayrı yankı kabulü sonucunu; ayarsız periyodu cevap gelene kadar kapatmayı
onayladı. Otomatik yeni 05 gönderimi veya belgesiz üst timeout eklenmedi.
Ayrıntılı durum/shell kuralları uygulama planındaki Powerboard kontrol
bölümündedir. E8 ölçüm/karar kaynağı yapılmadı; BQ-08 açık kalır.

Yeni paket 22/22, 21 ilgili Ceedling dosyası 343/343 geçti.
Üretim servis/codec ve gerçek RF RX dispatch çalıştırıldı; transport/E1
snapshot sınırı taklit edildi. PWRB_03/04 örnekleri kullanıldı; E8 örneği
bulunmadığından 96/95 bayt sentetik sınır testleri eklendi. İlk yazım,
mask/GEN, yankı retleri, stale E1, birlikte kapasite/oran, belirsiz SET,
erken sonuç, iptal sonrası geç sonuç, BOOT ve tick wrap kapsandı.

18 Cortex-M33/C11 modülü sıkı uyarılar/-Werror ile geçti; CubeIDE yeni
kaynakları managed build'e aldı, Release link başarılıdır. Kanıt
`test/build/scp-power-control-regression.log`,
`build/scp-power-control-strict.log`, `build/scp-power-control-release.log`,
`build/scp-power-control-final-build.log`. Son link text/data/bss
311164/504/126952 B; eşzamanlı diğer değişiklikler de dahildir. Web navigation
kaynak/gömülü ve web_auth entegrasyonu geçti. Seçili sonuç tam suite geçişi
olarak yorumlanmaz; fiziksel güç kartı uygulaması/enerji kesintisi kabulü
sınanmadı. BOLATeX soruları/K5/K9 ve G-01 açık. Bu adımda commit veya
cihaz yükleme/reset yapılmadı.

### 10.23. CESQ düzeltmesinin COPS LED tüketicisine uygulanması — 06.10.2026

Kullanıcı CESQ range/report/LED sınıflandırıcısını düzeltmiştir. İncelemede
COPS callback'inin raw değeri yalnız 10 ile kıyaslayıp LED'e doğrudan
normal mod yazdığı ikinci üretim yolu bulundu. Callback init ve periodic
COPS sorgularında erişilebilirdir. Kullanıcı bu yolun da düzeltilmesini onayladı.

Üretim gsm_engine.c içinde ikinci LED switch'i kaldırıldı; mevcut
metadata setter'dan sonra gsm_signal_led_update çağrılır. Ortak 4G/3G/2G
validity, fallback ve strength eşikleri kullanılır. RAT metadata ve COPS
mode dönüşleri korunur; eksik RAT/ERROR/timeout davranışına dokunulmaz.
Yeni state/queue/API eklenmedi; kullanıcının CESQ düzeltmesi korunur.

Gerçek COPS callback'i ve gerçek classifier ile beş Unity testi eklendi.
Output hook yalnız donanım sınırını ayırır. Düzeltme öncesi 33 testte üç
başarısızlık; düzeltme sonrası ve direct LED write'ı reddeden son koşuda
33/33 görüldü. Tam Ceedling 875/875, 9 integration paketi geçti. Tam JUnit
saklandı ve test_inventory complete/zero failure verdi. ARM Release değişen
kaynağı yeniden derledi/link etti; uyarı/hata yok. Fiziksel LED/cihaz, clean
Debug, paket self-check veya yükleme bu adımda yapılmadı. Commit yoktur.

Kanıt kökü test/build/production-audit-2026-10-03/: cops-led-before-2026-10-06.log,
cops-led-all-host-2026-10-06.log/.xml, cops-led-final-2026-10-06.log,
cops-led-inventory-2026-10-06.md ve cops-led-release-2026-10-06.log.
§10.20 ve modül kapsam raporundaki altı eski CESQ regresyonu artık kapalıdır;
eski başarısız sayılar yalnız ilk tespit koşusunu anlatır. Eşzamanlı başka
modül değişiklikleri bu düzeltmenin katkısı olarak sayılmaz.

### 10.24. RF web Kaydet/Uygula ve doğrulanmış durum — 06.10.2026

Powerboard değişiklikleri 179205b commit'ine alındı; diğer kullanıcı
çalışmaları korunmuştur. Ardından mevcut RF Kaydet yolunun NVRAM'e hemen
sync ettiği ve grup servisinin ayrı APPLIED RAM sonucunu tuttuğu doğrulandı.
Kullanıcı K5'te bu ayrımı ve PARTIAL/FAILED'de otomatik tekrar yapmamayı
onayladı. Mevcut kayıt akışı değiştirilmeden web Uygula/durum/iptal yolları
mevcut RF grup servisine ve HTTP admin kontrolüne bağlandı.

Kaydet uygulama başlatmaz; POST started=true uygulanmış sonucu değildir.
GET ayar gönderemez. Bir tabanlı store satırı ve MatchesDesired ile hedef
fider/bölge/EUI/writable CRC eşleşmesi görünürdür. Eski APPLIED, yeni
istenen ayarın APPLIED sonucu diye gösterilmez. Web önbelleği Kaydet/oku
sonrasında temizlenir; eski ayar nesnesine ait geç GET cevabı atılır.
Reset/partial sonrası otomatik RF gönderimi eklenmedi. NVRAM/SCP wire
biçimi değişmedi; yalnız son işlem RAM'de izlenir.

32 grup/adaptör/yetki testi ve 24 ilgili dosyada 378/378 geçti.
Gerçek RF grup/CSV APPLIED → JSON/eşleşme, router auth/admin/GET ve gerçek
handler yanıtları sınandı. Navigation kaynak/gömülü ve web_auth geçti.
Transport/NVRAM/HTTP sınırları taklit edilir; algoritma taklit edilmez.
19 modül sıkı Cortex-M33/C11 ile ve Release link geçti; text/data/bss
314144/504/126952 B, eşzamanlı diğer değişiklikleri de içerir.
Kanıt `test/build/rf-group-web-regression.log`, `build/rf-group-web-auth.log`,
`build/rf-group-web-strict.log`, `build/rf-group-web-release.log`,
`build/rf-group-web-final-build.log`. Seçili sonuç tam suite geçişi değildir.

K5 ürün kararı kapandı; K9 adresleri, BOLATeX soruları ve G-01 açık kalır.
Yeni EUI/atama Kaydet'i MH envanter ACK'i sayılmaz; Uygula eşleşmeyen üyeyle
başlamaz. Fiziksel RF/AY/enerji kesintisi kabulü, cihaz yükleme/reset veya
bu yeni web adımının commit'i yapılmadı. Ayrıntılar uygulama planı 0.17'de.

### 10.25. Modbus canlı akım/RF kaynağı — 06.10.2026

RF web adımı ba578b9 commit'ine alındı. Sonraki kaynak incelemesi,
iec104_process_init → generate_dummy_test_data → breaker faz verisi ve
Modbus fixed/configurable reader yollarını doğruladı. Kullanıcı mevcut
akım adreslerinde eksik/eski/geçersiz veriye NaN, RF durumuna ayrı gösterim
kararını verdi. İki alan gerçek rf_get_phase_data kaynağına bağlandı.
Dummy üretici diğer alanlar için korunur; yeni okuyucular ona dönmez.

Cooperative FC03 işleminde RF alanları tek HAL zaman örneği kullanır;
30 s sınırında FLOAT32'nin iki word'ü farklı tazelik kararına düşmez.
Library API, adres ve NVRAM değişikliği yoktur. Pasif/rezerve satırın 0
kuralları korunur. Modbus haritası §5.2.1 tek veri sözleşmesidir.

Gerçek RF cache/FC03 ile fixed/configured 5'er yeni test, Powerboard 6
regresyonuyla 16/16 geçti. 30 seçili dosya 411/411 geçti; çalışma ağacındaki
başka Modbus testlerini de içerir, tam suite sonucu değildir. Envanter/
store/hardware sınırı taklit edilir; RF/Modbus algoritması gerçek kodudur.
Yanlış dummy yerine gerçek akım, NaN/yaş, ölçüm geçersizken RF'nin güncel
kalması, MH restart ve iki word sınırı test edildi. 20 modül sıkı C11/ARM
ve Release link geçti; text/data/bss 314224/504/126952 B, diğer eşzamanlı
çalışmaları da içerir. Kanıt test/build/modbus-rf-focused.log,
test/build/modbus-rf-regression.log, build/modbus-rf-strict.log,
build/modbus-rf-release.log.

IEC104 canlı veri timestamp tercihi soruldu; henüz ikinci cevap yoktur.
IEC104 okuyucuları, enerji/nominal/yük ve kalan alarm eşlemeleri bu adımda
bağlanmadı. Geçici/kalıcı arıza kaydı aktarımı önceki kapsamıyla korunur.
K9/BOLATeX ve G-01 açık; fiziksel RF/IRQ/cihaz kabulü, yükleme/reset ve
bu yeni Modbus adımının commit'i yapılmadı.

### 10.26. IEC104 canlı veri RTU alım zamanı — 06.10.2026

Kullanıcı LIVE_DATA için şimdilik RTU alım saatini onayladı ve BOLATeX'e
sorulmasını istedi. RF dispatch geçerli paketi işlerken RTC'yi bir kez
saklar; RTC geçersizse CP56 IV=1'dir. IEC104 akım/RF okuyucuları gerçek
RF cache ve saklanan zamanı kullanır. Geç sorgu/RTC düzeltmesi eski örneği
yeniden zamanlandırmaz. Ölçüm/RTC kalitesi ve RF tazeliği ayrıdır.
Olay kaydının kaynak zamanı, adres ve NVRAM düzeni değişmez. BQ-15 soru
listesine eklendi; üretici cevabı veya gönderilmiş mesaj sayılmadı.

Gerçek model/kayıtlı okuyucular için 5, gerçek RX/COBS/codec için 2 yeni
test; 31 seçili dosyada 418/418 geçti. Eksik/eski/geçersiz ölçüm, iyi RF
ile bozuk akım ayrımı, geç okuma, yanlış parametre, geçersiz RTC, sonradan
RTC düzeltmesi ve hatalı pakette önceki zamanın korunması sınandı.
Clock/envanter/store/hardware sınırları taklit edilir; algoritma gerçek
koddur. 21 modül sıkı C11/ARM ve Release link geçti: text/data/bss
314472/504/127048 B. RF alım metadata RAM farkı +96 B; diğer eşzamanlı
çalışmalar da linke dahildir. Init/HAL macro çakışması include sırasıyla
çözüldü; HAL/vendor kaynakları değiştirilmedi.
Kanıt test/build/iec104-rf-live-regression.log,
build/iec104-rf-live-strict.log ve build/iec104-rf-live-release.log.

Enerji/nominal-yük/kalan alarm eşlemeleri tamamlanmadı; dummy producer
korunur. BQ-15/K9/G-01 açık. Host sonucu fiziksel ölçüm/RF gecikmesi kabulü
değildir. Bu adımda commit, cihaz yükleme/reset veya dışarı mesaj gönderimi
olmadı. Ayrıntılar uygulama planı 0.19'da kaydedildi.

### 10.27. SCP enerji/yük ve Modbus kalite bloğu — 06.10.2026

**Kanıt:** `f352bed` sonrasında IEC104 enerji/nominal okuyucuları ve
sabit/yapılandırılabilir Modbus göstergeleri breaker verisini okuyordu.
R1 LIVE_DATA `Status_Flags` bit 0 enerji, bit 1 yük akımı bilgisidir.
R1 olayındaki `nominal_current_status` da yük var anlamındadır; eski
"nominal altı" yorumu kullanılmamalıdır.

**Karar ve değişiklik:** kullanıcı 49500–49520 ayrı kalite bloğunu ve
eski uyumluluk yerine dokümanın esas alınmasını onayladı. Enerji/yük
üretim okuyucuları gerçek RF cache kaynağına bağlandı. RMS'ten veya
koruma nominal ayarından bu bitler hesaplanmaz. Nominal gösterge
alanları `yuk_akimi_varyok`, JSON ve ekran karşılıkları yük akımı oldu.
Adresler, NVRAM alan sırası ve boyutları korundu; koruma nominal ayarı
değişmedi. Eski JSON anahtarlarına takma ad eklenmedi.

Kalite bit 0 akım, bit 1 enerji, bit 2 yük geçerliliğidir. Taze geçerli
RMS=7, taze geçersiz RMS=6, eksik/eski/etkin olmayan satır=0 olur.
Değerin yok biti ile geçerliliği ayrıdır. Mevcut Boolean son biti
korur; örnek yoksa 0 olur. İstemci kalite=0 değerini kullanmamalıdır.
IEC104 eski örnekte IV/NT, eksik örnekte IV ve alım zamanı IV kullanır.
FC06 kalite yazmasını reddeder. Web ve sunucu yeni blokla çakışan
adresleri, FLOAT32 ikinci register'ı dahil, kaydetmeden reddeder.

**Doğrulama:** gerçek model/FC03/IEC104 okuyucuları ve donanım sınırı
mock'ları ile 428/428 seçili Ceedling testi geçti. Son IEC104 fonksiyon
adı düzenlemesinden sonra 72/72 alt küme tekrar geçti. Kaynak/gömülü
web navigation, gerçek HTTP handler paketi ve NVRAM integration
433/433 kontrolü geçti. 22 modül sıkı ARM/C11 ile derlendi; Release
0 hata ve 0 uyarıyla tamamlandı. NVRAM 2476 B, CRC offset 2472 B
assert'ları hedef derlemesinde korundu.

**Kanıt yolları:** `test/build/rf-energy-load-regression.log`,
`test/build/rf-energy-load-iec-final.log`,
`build/rf-energy-load-strict.log`, `build/rf-energy-load-nvram.log`,
`build/rf-energy-load-web-auth.log`,
`build/rf-energy-load-release-final.log`.

RF web monitoründe enerji/yük iki ayrı satırdır; eski/eksik/geçersiz
Flags için — gösterilir. Kaynak/gömülü sayfa iki dilde sınandı.
Bit anlamları dokümanda açık olduğundan yeni BOLATeX sorusu oluşturulmadı;
Modbus/web kalite tercihi soru belgesindeki RTU kararlarına işlendi.

**Sınırlar:** farklı FC03 istekleri arasında yeni LIVE gelebilir;
ayrı değer/kalite yanıtları atomik görüntü değildir. Fiziksel RF/seri
port kabulü veya cihaz yüklemesi yapılmadı. Kalan arıza/alarm eşlemeleri
ve BOLATeX soruları bu adımla kapatılmaz. Dummy producer silinmedi.

### 10.28. Anlık arıza alanları ve olay yük biti — 06.10.2026

**Kanıt:** `rf_events.c:build_fault()` olay ofset 13 değerini ters
çeviriyordu. R1 tablo §4.9 bu alanı LIVE bit 1 ile aynı yük var
anlamında tanımlar. Yakalanmış paket ve yük var/yok fixture girişleriyle
27 testin 17'si düzeltme öncesi başarısız oldu. IEC104 genel sorgusu ve
Modbus anlık arıza alanları ayrıca eski breaker verisini okuyordu.

**Kullanıcı kararı:** anlık arıza akımı/süresi/tipi kaldırılır; yalnız
geçici/kalıcı arıza listeleri kalır. Web/JSON ayarları ve IEC104 canlı
okuyucu/üreticileri kaldırıldı. Modbus offset 0–5/12–17 reserved sıfırdır;
yapılandırılabilir harita bu eski alanları eşlemez. Kalan adresler ve
NVRAM byte yerleşimi korunur; eski yuvalar reserved adlandırıldı.
Modbus canlı tüketicisi artık breaker snapshot'ına ihtiyaç duymaz.
Dummy producer silinmedi; Modbus liste blokları bu adımda eklenmedi.

**Düzeltme:** olay yük biti liste ve replay kaydına doğrudan yazılır.
Fault log schema 3'tür; eski ters bit anlamlı schema 2 görüntüsü
reddedilir. 20 B kayıt/32 bit süre/fider boyutu ve Flash adresleri
değişmedi. Eski geliştirme Flash/replay görüntülerini taşıma kapsamda
değildir; bunların yeni kayıt biçimiyle uyumlu olduğu iddia edilmez.

**Onaylanan gönderim:** spontane/replay eski canlı IOA'lar yerine
geçici/kalıcı listenin ilk kayıt IOA'larını kullanacak şekilde hazırlandı.
İki ölçüm ve iki durum nesnesi olay zamanını taşır. Kullanıcı
06.10.2026 tarihinde bu hazırlanan paketin commitlenmesini ve devam
edilmesini istedi; gönderim biçimi onaylanan paketin parçasıdır.
Bu adım K9'un bütün alarm eşlemelerini tamamlamaz.

**Doğrulama:** 177/177 ilgili ve 432/432 geniş seçili Ceedling testi
geçti. Gerçek GI isteği, liste IOA/tel verisi, kaldırılan Modbus alanları,
JSON adres/range koruması ve eski kayıt schema'sı sınandı. Web kaynak/
gömülü sayfa, HTTP handler, NVRAM 307 kontrol ve NOR arıza saklama
53 kontrol geçti. 22 modül sıkı ARM/C11 ve Release 0 hata/0 uyarıyla
geçti; fiziksel RF/Flash/SCADA kabulü yapılmadı.

**Kanıt:** `test/build/rf-event-load-before.log`,
`test/build/rf-fault-lists-only.log`,
`test/build/rf-fault-lists-regression.log`,
`build/rf-fault-lists-only-strict.log`, `build/rf-fault-lists-release.log`,
`build/rf-fault-lists-nvram.log`, `build/rf-fault-lists-storage.log`,
`build/rf-fault-lists-web-auth.log`. Modbus haritası 1.10, plan 0.21 ve
BOLATeX soru belgesindeki RTU karar tablosu güncellendi. Yeni bir
üretici belirsizliği oluşmadı; açık BOLATeX soruları kapatılmadı.

### 10.29. RF operatör onayı kanalı — 06.10.2026

**Kanıt:** RF monitorü `TripFailedAlarm` ve `TripFailureLatched`
durumlarını yayımlar. Onay mevcut `rf alarm-ack` terminal komutuyla
`rf_ack_trip_failure()` servisine gider. Servis atanmış EUI/LIVE,
Trip_Failed=0 ve latch=1 koşullarını kullanır; yalnız RTU RAM durumunu
değiştirir. R1 §4.5 operatör onayı ister, ancak onayın kaynağını ve
MH/AY'ye iletim gereksinimini tanımlamaz.

**Kullanıcı kararı:** yeni web düğmesi/yazma API'si eklenmeden onayın
hangi taraftan gelmesi gerektiği BOLATeX'e sorulacaktır. BQ-16 bu
kapsamla eklendi. Mevcut servis/terminal değiştirilmedi; yeni UI/API
eklenmedi. BQ-01'deki geç olay bağlantısı ayrıca açık kalır. Bu açık
konu bir tamamlanmış entegrasyon olarak kapatılmamalıdır.

**Doğrulama:** mevcut servisin olumlu onayı ve ret koşulları gerçek
SCP RX fixture'ıyla Ceedling'e eklendi. Restart latch'inin doğru
kimlik/0 flag ile kapanması, aktif hata/atanmamış kaynağın reddi,
tekrar onayın reddi ve EUI/örnek zaman sayacının korunması sınandı.
`test/build/rf-alarm-ack-service.log`: 79/79 geçti. Firmware mantığı
değişmedi; yeniden hedef derlemesi veya fiziksel test yapılmadı.

### 10.30. Arıza günlüklerinde yük biti çıktısı — 06.10.2026

**Kanıt:** `fault_log.c:print_log()` 1 değerini Below, 0 değerini Normal
olarak gösteriyordu. `iec104_event_log.c:visit_dump()` aynı biti ters
gösteriyordu. R1 §4.9 olay ofset 13 ve önceki kullanıcı kararıyla bu
bit doğrudan yük var/yok bilgisidir. İki public dump girişinin gerçek
shell/NOR testleri düzeltme öncesi başarısız oldu.

**Düzeltme:** iki shell çıktısı `Load=1/0` doğrudan anlamını kullanır;
enerji durumu ayrı kalır. Kayıt/Flash/NVRAM biçimi, SCP veya alarm onay
davranışı değiştirilmedi. BQ-16 ve diğer üretici cevapları beklenir.

**Doğrulama:** gerçek fault/event log ve xprintf üretim girişleri;
Flash ve terminal sınırları test çiftidir. Yük/enerji bitleri bağımsız
değerlerle sınandı. Kayıt/SCP RX/olay/IEC104 tel seçiminde 172/172 geçti.
14 CSV'deki 175 çerçeve güncel fixture ile eşleşti. Release 0 hata ve
0 uyarıyla tamamlandı; fiziksel cihaz kabulü yapılmadı.

**Kanıt:** `test/build/rf-load-shell-before.log`,
`test/build/rf-load-shell-regression.log`, `build/rf-load-shell-release.log`;
fixture için `python test/scripts/generate_rf_scp_vectors.py --check`.
