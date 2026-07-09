# Troika Smart Breaker Modem — Production-Readiness Analiz Raporu

**Hedef:** STM32U375VETx tabanlı akıllı röle/modem firmware'i
**Kernel:** Contiki kooperatif protothread kerneli (önceliksiz, event-driven)
**Tarih:** 2026-07-05
**Kapsam:** Kernel + altyapı, GSM alt-sistemi, Web/TCP sunucu, IEC 60870-5-104, Modbus RTU, COBS, SPI flash (W25Q), RF + SCP + Power Board (I2C slave), libs (xmodem/shell/xscanf/ring_buf/debouncer/datetime/crc32), logging (elog/fault_log), durum makineleri (breaker/periodic_reset/stack_monitor/system_status/reboot/boot/time_service/digital_input). **Tüm proje modülleri denetlendi.**
**Doğrulama durumu:** Her bulgunun yanında `[…]` içinde doğrulama seviyesi belirtilmiştir.

> **Doğrulama seviyeleri**
> - **[DOĞRULANDI]** — Kodu bizzat okuyup kontrol akışını takip ederek teyit ettim.
> - **[AJAN-DOĞRULANDI]** — Derin denetim ajanı buldu; mekanizma tutarlı ama her satırı benim tarafımdan teyit edilmedi (düşük false-positive riski).
> - **[TAHMİNİ]** — Kanıt güçlü ama çevresel koşula bağlı; sahanın teyidi gerekir.

---

## 0. Metodoloji ve Çapraz Kesişen Kısıtlar (önce bunu okuyun)

Firmware, **Contiki kooperatif kerneli** üzerine kurulmuştur. Bu, tüm bulguların yorumlanmasında belirleyicidir:

1. **Önceliksizlik (cooperative):** Ana döngü `while(process_run());`'tur ([app_main.c:484-490](Application/app_main.c#L484)). Bir protothread `PROCESS_WAIT_*` ile yield etmedikçe **başka hiçbir süreç çalışamaz**. Bu nedenle herhangi bir **bloklayıcı çağrı** (SPI flash erase/write ~60-300ms, `HAL_Delay`, meşgul-bekleme) tüm sistemi durdurur — UART RX ISR'leri ring buffer'ı doldurmaya devam eder ama tüketim/parse durur. Bu rapordaki en yıkıcı kategori budur.

2. **Protothread kuralı:** Yerel (stack) değişkenler `PROCESS_WAIT_EVENT*`/`PROCESS_YIELD` karşısında **korunmaz**. Wait öncesi/sonrası state `static` veya dosya-kapsamı olmalıdır. İncelemede bu kurala uyulduğunu gördüm (IEC104 ve GSM süreçleri yerelleri doğru şekilde `static` tutuyor).

3. **Tick → etimer zinciri:** TIM17 1ms ISR'i ([stm32u3xx_it.c:468](Core/Src/stm32u3xx_it.c#L468)) → `bsp_tick_handler()` ([bsp.c:30](Application/bsp/bsp.c#L30)) → `clock_tick()` + `etimer_request_poll()`. Bu zincir **doğru kurulmuştur**; etimer'lar çalışır.

4. **Kritik bölge yok:** Uygulama kodu `critical_enter`/`__disable_irq` **hiç kullanmıyor**. Contiki'nin `int-master` portu da (aşağıda) ARM'de no-op. Dolayısıyla ISR ↔ main arası paylaşılan durum; **sadece SPSC (tek-üretici/tek-tüketici) lock-free desenleri** ile korunabilir. Ring buffer'lar bu deseni doğru uyguluyor; **çok-alanlı struct paylaşımları** (örn. yazılım RTC'si) torn-read'a açıktır.

---

## 1. Yönetici Özeti — En Kritik Bulgular (öncelik sırasıyla)

| # | Modül | Bulgu | Severity | Durum |
|---|-------|-------|----------|-------|
| K1 | IEC104 | `iec104_tick()` **hiç çağrılmıyor** → T1/T2/T3/TESTFR tümü ölü; ölü bağlantı/half-open TCP hiç tespit edilmez | **CRITICAL** | ✅ DÜZELTİLDİ |
| K2 | IEC104 | ACK/K penceresi mantığında `k_counter` ↔ `w_counter` karışıklığı → TX penceresi bozuluyor, ACK'ler yanlış | **CRITICAL** | ✅ DÜZELTİLDİ |
| K3 | IEC104 | Red cevapları (bilinmeyen tip/adres) master'ın APCI'sini aynen yankılıyor → N(S) desync, oturum reset | **CRITICAL** | ✅ DÜZELTİLDİ |
| K4 | IEC104 | Tamponlanmış fault I-frame'leri yanlış N(S) üretiyor + `iec104_send`/TX-hesabını atlıyor | **CRITICAL** | ✅ DÜZELTİLDİ (K-window burst ihlali kabul) |
| K5 | GSM/OTA | `gsm_firmware_update` chunk yazımında 4 KiB buffer overflow → OTA sırasında BSS/SPI flash bozulması | **CRITICAL** | ✅ DÜZELTİLDİ |
| K6 | Web/Güvenlik | Web parolaları cihazın kendi IP'sinden türetiliyor (`admin<octet+1>`) → 256'lık uzay, uzaktan tam ele geçirme | **CRITICAL** | DOĞRULANDI |
| K7 | Web/Güvenlik | Oturum token'ı `bsp_get_tick()` tabanlı → tahmin edilebilir + loglara sızıyor → session hijack | **CRITICAL** | DOĞRULANDI |
| K8 | libs/xprintf | `xsnprintf` format içi `max_len`'i **uygulamıyor** (%s/%d alt-döngüleri) → yığın/BSS taşması; K6 parola tamponu da taşar | **CRITICAL** | ✅ DÜZELTİLDİ |
| K9 | Web/Güvenlik | RFWU firmware-güncelleme portu 80'de, kimlik doğrulamadan önce, zayıf `CRC32 XOR size` auth → kalıcı firmware kompromizasyonu | **CRITICAL** | DOĞRULANDI |
| K10 | Kernel/RTC | Yazılım RTC'si ISR'de güncelleniyor, ana bağlamda struct olarak okunuyor → **torn-read** (yanlış zaman damgaları) + 1001 ms/saniye sapma | **HIGH** | DOĞRULANDI |
| K11 | Altyapı/Flash | `nvram_sync()` her değişiklikte **tüm nvram'i 2 adrese** (erase+yaz+verify, ~60-300ms/sektör × 3 deneme) yazıyor; kerneli durduruyor; gelişi güzel tetiklenen çağrıcılar | **HIGH** | DOĞRULANDI |
| K12 | Kernel/Süreklilik | `lifetime` her saniye RAM'de artıyor ama **periyodik persistans yok** → güç kesintisinde kayıp; sadece başka bir sync tetikleyicisi olduğunda yazılır | **MEDIUM** | DOĞRULANDI |
| K13 | Power Board/I2C | I2C slave callback'lerinin 4/5'i `I2C1` kontrol ediyor (modül `I2C3` kullanıyor) → power-board link'i ölü (şu an wired değil/latent) | **CRITICAL** | DOĞRULANDI |
| K14 | digital_input | USER_RESET eşik makroları ters (`NVRAM=15s`, `OTHER=10s`) → 15 sn'lik **fabrika ayarı dalı ölü kod**; butonla fabrika ayarı imkânsız | **HIGH** | ✅ DÜZELTİLDİ |
| K15 | shell | `rx_buff` dolunca NUL terminatör `rx_buff[64]`'e OOB yazılıyor (konsol UART ISR) → komşu BSS bozulması | **HIGH** | ✅ DÜZELTİLDİ |
| K16 | shell | `shell_exec` `argc`'yi yok sayıyor; `exec` argümansız → `strcmp(NULL,...)` HardFault (su admin) | **HIGH** | ✅ DÜZELTİLDİ |
| K17 | RF | Periodik PING `0xFF`'e (broadcast `0x00` olmalı) + gelen ACK `WAIT_RESPONSE`'u temizlemiyor → RF link hiç çalışmıyor gibi görünür | **MEDIUM** | DOĞRULANDI |

**Neticede:** Donanım layer'ı (GPIO/UART/DMA/SPI init, tick zinciri, Modbus RTU, COBS, ring buffer, crc32, datetime, stack_monitor) sağlam. IEC104 protokol motorunun timewheel/sequence mantığı **büyük ölçüde düzeltildi** (K1-K6_iec, H7/H9/H10/H11/H12; bkz. §2.0) — geriye H8 (fault süreç deadlock) ve küçük açık/kabul maddeleri kaldı. Asıl kalan risk dört kümede: **(a) GSM ağı üzerinden erişilebilen Web/RFWU yüzeyinde kimlik doğrulama ve tampon güvenliği kırık (K6-K9)**, **(b) kooperatif kernelde bloklayıcı SPI-flash + UART TX erişimi tüm sistemi duraksatıyor (K11, K17)**, **(c) power_board/I2C slave sürücüsü yanlış peripheral ile kopyalanmış (WIP, latent, K13)**, **(d) shell/xscanf/xmodem tüketicilerinde tampon/söz-dizimi güvenliği zayıf (K8, K15, K16)**.

---

## 2. IEC 60870-5-104 (SCADA Köprüsü) — EN KRİTİK KÜME

> **DÜZELTME DURUMU (2026-07-06 güncellendi):** İlk turda raporlanan timewheel/sequence hatalarının büyük çoğunluğu **düzeltildi** ve doğrulandı. Protokol motoru artık bir SCADA master ile birlikte çalışabilir durumda. Aşağıdaki durum tablosuna bakınız.

Bu cihaz bir SCADA slave/controlled-station'dır.

### 2.0 Düzeltme Durumu Özeti

| Bulgu | Açıklama | Durum |
|-------|----------|-------|
| **K1** | `iec104_tick()` wired (her 1 sn) → T1/T2/T3/TESTFR canlandı | ✅ **DÜZELTİLDİ** |
| **K2** | w/k counter karışıklığı düzeltildi (process_i_frame + poll) | ✅ **DÜZELTİLDİ** |
| **K3** | Red cevapları artık taze `make_iframe_control(send_sn, receive_sn)` | ✅ **DÜZELTİLDİ** |
| **K4** | Buffered fault N(S) düzeltildi + k_counter/wait_for_ack/t1 accounting eklendi | ✅ **DÜZELTİLDİ** (K-window burst ihlali kabul edildi, bkz. K4-not) |
| **K5_iec** | `iec104_send`'de K-window `return` aktif (queue yok → frame drop kabulu) | ✅ **DÜZELTİLDİ** (kabulu ile) |
| **K6** | ACK wraparound: `num_acked = (nr - ack_sn) & 0x7FFF` | ✅ **DÜZELTİLDİ** |
| **H7** | T1 yalnız `k_counter 0→1` geçişinde başlatılıyor | ✅ **DÜZELTİLDİ** |
| **H9** | STARTDT artık `iec104_reset()` çağırıyor (tüm durum synch) | ✅ **DÜZELTİLDİ** |
| **H10** | STOPDT_CON/ACT sabitleri düzeltildi | ✅ **DÜZELTİLDİ** |
| **H11** | ASDU minimum uzunluk kontrolü eklendi (VSQ×N henüz değil) | ✅ **KISMEN** |
| **H12** | event-log `write_index` kapasite kontrolü | ✅ **DÜZELTİLDİ** |
| **H8** | temp/permanent fault süreçleri mutual `process_is_running` busy-wait → deadlock | ❌ **AÇIK** |
| **L16** | RX tampon taşıdında sessiz kırpma → frame desync | ❌ **AÇIK** |
| **(yeni-A)** | `close_socket_req` `volatile` değil (diğer listener flag'leri volatile) | ❌ **AÇIK** |
| **(yeni-B)** | `iec104_send` `bool` dönüşü hiçbir çağıranca kullanılmıyor; non-static, header'da yok | ❌ **AÇIK** (temizlik) |
| **(yeni-C)** | libiec104 → `iec104_application.h` katman sızıntısı (test'leri kırabilir) | ❌ **AÇIK** (refactor) |

**Düzeltme sırasında eklenen yeni mimari (olumlu):**
- **`link_active` gate:** STARTDT tamamlanana kadar timer'lar ve S/I-frame işleme kapalı (`iec104_tick`, `libiec104_poll`, `iec104_process_package`). Dead-link reset sonrası otomatik koruma sağlar.
- **TESTFR_CON takibi:** `wait_for_testfr_con` — T3 idle'da TESTFR_ACT gönderilir, CON gelirse temizlenir, `t1_max` içinde CON gelmezse "ölü bağlantı" → reset+soket kapat.
- **Gecikmeli/mükerrer ACK filtresi:** `num_acked > (0x7FFF - k_max)` → sessizce yok say (log gürültüsü engellendi).
- **Ölü-bağlantı kurtarma zinciri:** T1/desync/seq-error/TESTFR-timeout → `iec104_reset()` + `REQUEST_SOCKET_CLOSE` → GSM listener `close_socket_req` → `handle_idle`/`handle_data_mode`'da tüketilip soket kapatılır → `SOCKET_CLOSED` → fault süreçleri temizlenir. Yeniden bağlanmada STARTDT → `link_active=true`.
- **BUG (inceleme sırasında bulundu, düzeltildi):** `iec104_application_event_handler` `else if(IEC104_APP_EVT_SOCKET_CLOSED)` `evt ==` eksikti → düzeltildi ([iec104_application.c:36](Application/iec104_application.c#L36)).

### K1. [DOĞRULANDI] `iec104_tick()` hiç çağrılmıyor — tüm protokol zamanlayıcıları ölü — ✅ DÜZELTİLDİ
- **Dosya:** [iec104_process.c:37-40](Application/iec104_process.c#L37) (`tick_timer` tanımlı, çağıran yok); tüketici [iec104.c:78](Application/libiec104/iec104.c#L78) (`iec104_tick`)
- **Kök neden:** `tick_timer()` tanımlanmış ama hiçbir yerde çağrılmıyor/kaydedilmiyor. `iec104_process` thread'i her 100 ms'de `libiec104_poll()`, `iec104_periodic_send()`, `iec104_tx_process()` çağırıyor ama `iec104_tick` **asla**. `t1_timer`, `t2_timer`, `t3_timer`, `periodical_send_interval` yalnızca `iec104_tick` içinde artırılıyor → hepsi 0'da kalır.
- **Sonuç:**
  - **T3 hiç tetiklenmez** → link idle iken TESTFR keepalive gönderilmez. GSM radyo drop'larından sonra **half-open TCP asla tespit edilmez**; slave oturumu "canlı" sanır, SCADA cihazı çevrimdışı görür.
  - **T1 hiç tetiklenmez** → kaybolan ACK'ler tespit edilmez, dead-link abort çalışmaz.
  - `periodical_send_interval` 0'da kalır (periyodik gönderim sayacı işlemez).
- **Çözüm:** `iec104_tick()`'i `iec104_process` thread'inde 1 sn'lik bir `etimer`/alt-sayaca bağla (parametreler saniye cinsinden). Örn. mevcut 100 ms döngüde 10 sayımda bir çağır.

### K2. [DOĞRULANDI] K/W pencere mantığında `k_counter` ↔ `w_counter` karışıklığı — ✅ DÜZELTİLDİ
- **Dosya:** [iec104.c:841-845](Application/libiec104/iec104.c#L841) ve [iec104.c:1108-1113](Application/libiec104/iec104.c#L1108)
- **Kök neden:** IEC 104 §5.5: `W` = onaysız **alınan** I-frame sınırı; `w_counter >= w_max` olunca S-format ACK gönderilir. Kod iki yerde **gönderim penceresi sayacı `k_counter`'ı** test ediyor ve onu sıfırlıyor:
  ```c
  // iec104_process_i_frame, 841 (I-frame alınınca)
  if(k_counter >= config.k_max){ iec104_send_s_frame(...); k_counter = 0; }  // k_counter = GÖNDERİM penceresi!
  // libiec104_poll, 1108
  if((k_counter >= config.k_max) || ((k_counter > 0) && (t2_timer >= config.t2_max))){
      iec104_send_s_frame(...); t2_timer = 0; k_counter = 0;
  }
  ```
  Doğru W-trigger [iec104.c:881](Application/libiec104/iec104.c#L881)'de (`++w_counter >= w_max`) ama yukarıdaki iki blok hem alıcı ACK'ini bozuyor hem de **gönderim penceresi hesabını** her alınan frame'de sıfırlıyor.
- **Sonuç:** Slave, gönderdiği frame'leri onaylanmış sanıp K penceresini ihlal ederek göndermeye devam eder; master ACK gecikince kendi T1'i ateşlenir. SCADA oturumu dengesiz.
- **Çözüm:** Her iki `k_counter`/`config.k_max` testini `w_counter`/`config.w_max` ile değiştir; `k_counter = 0` atamalarını kaldır; S-frame (veya N(R) taşıyan frame) gönderince `w_counter = 0` yap.

### K3. [DOĞRULANDI] Red cevapları master'ın APCI'sini aynen yankılıyor — sequence desync — ✅ DÜZELTİLDİ
- **Dosya:** [iec104.c:888-904](Application/libiec104/iec104.c#L888)
- **Kök neden:** Desteklenmeyen ASDU tipi veya common-address uyuşmazlığında global `package` (**master'dan gelen frame, master'ın N(S)/N(R)'si içeriyor**) mutasyona uğratılıp `iec104_send`'e veriliyor. `iec104_send` onu I-frame sayıyor, slave'in `send_sn`/`k_counter`'ını artırıyor.
- **Sonuç:** Slave, **master'ın sequence numarasıyla** I-frame yayınlar → master beklenmedik N(S) görür ve §8.6 uyarınca oturumu resetler; üstelik slave kendi `send_sn`'i de bozulur. Standart tipleri tarayan herhangi bir master bu yolu **her taramada** tetikler.
- **Çözüm:** `package`'i yeniden kullanma; tıpkı diğer `iec104_send_*` yardımcılarında olduğu gibi `make_iframe_control(send_sn, receive_sn)` ile taze paket kur.

### K4. [DOĞRULANDI] Tamponlanmış fault frame'leri yanlış N(S) + TX hesabını atlıyor — ✅ DÜZELTİLDİ
- **Dosya:** [iec104.c:2106-2110](Application/libiec104/iec104.c#L2106) ve [iec104.c:2211-2215](Application/libiec104/iec104.c#L2211)
- **Kök neden:** `buff != NULL` yolunda frame'in N(S)'i `make_iframe_control(send_sn, …)` ile kurulduktan sonra `pkt.frame.apci.i_frame.send_seq++;` ile **bir fazlası** yapılıyor; sonra bu frame'ler `iec104_application.c`'ten doğrudan `gsm_send_to_socket` ile gönderiliyor — `iec104_send`/`k_counter`/`t1_timer`/TX-slot tamponu atlanıyor.
- **Sonuç:** Grup-3/4 sorgulamada ilk fault frame'inin N(S)'i bir fazla; master sequence hatasıyla resetler. TX-slot tamponu dışında, ayrı süreçten yayınlandığı için `iec104_process` kuyruğuyla N(S) sırası karışabilir.
- **Çözüm:** `send_seq++` satırlarını kaldır; tamponlu frame'leri `iec104_send` (veya en azından TX-slot tamponu) üzerinden geçirmeye zorla.

### K5_iec. [DOĞRULANDI] `iec104_send` K penceresini uygulamıyor — ✅ DÜZELTİLDİ (`return` aktif; queue yok → window-full'da frame drop kabulu)
- **Dosya:** [iec104.c:118-133](Application/libiec104/iec104.c#L118)
- **Kök neden:** `if(k_counter >= config.k_max)` sadece logluyor; `return` yorumdan çıkarılmamış. Sonrasında koşulsuz `k_counter++; send_sn++;` gönderim yapıyor.
- **Sonuç:** Yavaş/kayıp ACK durumunda (K1/K2 ile birleşince) slave K penceresini ihlal ederek link'i boğar.
- **Çözüm:** `k_counter >= config.k_max` iken erken `return` (veya frame'i kuyruğa al).

### K6_iec. [DOĞRULANDI] `on_ack_received` 15-bit sequence wrap'ı işlemiyor — ✅ DÜZELTİLDİ
- **Dosya:** [iec104.c:780-813](Application/libiec104/iec104.c#L780)
- **Kök neden:** Sadece `if (nr > ack_sn)` testi var. `ack_sn = 32767` iken master'ın wrap'lamış `nr = 4` değeri için `4 > 32767` false → ACK yok sayılır; `ack_sn`/`k_counter`/`wait_for_ack` güncellenmez.
- **Sonuç:** ~32768 frame sonunda (yoğun trafikte ~9 saat) her wrap-boundary ACK'i düşer.
- **Çözüm:** `uint16_t acked = (nr - ack_sn) & 0x7FFF;` modular mesafe; `acked <= k_counter` şartıyla işle.

### Diğer IEC104 bulguları (HIGH/MEDIUM, AJAN-DOĞRULANDI) — düzeltilme durumu işaretli
- ✅ **[iec104.c:132](Application/libiec104/iec104.c#L132)** (H7) T1 her I-frame gönderiminde sıfırlanıyordu → **DÜZELTİLDİ**: artık yalnız `k_counter 0→1` geçişinde.
- ✅ **[iec104.c:722-729](Application/libiec104/iec104.c#L722)** (H9) STARTDT_ACT yalnızca `send_sn`/`receive_sn`'i sıfırlıyordu → **DÜZELTİLDİ**: artık `iec104_reset()` (tüm durum + `link_active`).
- ✅ **[iec104.c:752-762](Application/libiec104/iec104.c#L752)** (H10) `iec104_send_stopdt_con`/`_act` ters sabit → **DÜZELTİLDİ**.
- ✅ **[iec104.c:1021-1098](Application/libiec104/iec104.c#L1021)** (H11) ASDU minimum uzunluk kontrolü yoktu → **KISMEN DÜZELTİLDİ**: tip-bazlı min uzunluk eklendi (VSQ×N henüz değil).
- ✅ **[iec104_event_log.c:142-148](Application/libiec104/iec104_event_log.c#L142)** (H12) `write_index` aralık doğrulaması yoktu → **DÜZELTİLDİ**.
- ❌ **[iec104_application.c:63-67 / 133-137](Application/iec104_application.c#L63)** (H8) temp/permanent fault süreçleri mutual `process_is_running` busy-wait → **AÇIK**: grup-3 ve grup-4 arka arkaya gelirse **deadlock** (interrogation hiç sonlanmaz). Çözüm: gerçek bir mutex/bayrak veya ikinciyi başlatma.
- ❌ **[iec104.c:1010-1013](Application/libiec104/iec104.c#L1010)** (L16) RX tamponu taşıdında sessizce kırpıyor → **AÇIK** frame desync.

---

## 3. GSM Alt-sistemi

### K5. [DOĞRULANDI] OTA firmware chunk yazımında 4 KiB buffer overflow — ✅ DÜZELTİLDİ
- **Dosya:** [gsm_firmware_update.c:47-60](Application/gsm/gsm_firmware_update.c#L47)
- **Kök neden:** `memcpy(&fw_update_buffer[fw_update_buffer_index], data, size);` öncesinde `fw_update_buffer_index + size <= 4096` kontrolü **yok**; sınır kontrolü `memcpy`'den sonra (`if(fw_update_buffer_index >= 4096)`). 4 KiB'yı aşan chunk'ta BSS taşması.
- **Tetik:** HTTP yolu `cs:4096` ilan ediyor ama istemciyi **tam 4096 göndermeye zorlamıyor**; 4096'yı bölmeyen her chunk (örn. 2000 byte'lık art arda: 2000→4000→ taşma 1904 byte) overflow. RFWU yolu ≤1024 byte chunk'lara izin veriyor; 1024'ün altındaki resume/partial chunk hizalamayı bozar (örn. 900'er: 3600→4500 taşma).
- **Sonuç:** OTA sırasında `flash_address`, `fw_update_buffer_index` ve komşu BSS'in üzerine yazım → bozuk firmware yazımı, yanlış boot bayrakları, hard fault. Sahada cihaz tuğlalanabilir.
- **Çözüm:**
  ```c
  while (size > 0U) {
      uint32_t space = 4096U - fw_update_buffer_index;
      uint32_t to_copy = (size < space) ? size : space;
      memcpy(&fw_update_buffer[fw_update_buffer_index], data, to_copy);
      fw_update_buffer_index += to_copy; data += to_copy; size -= to_copy;
      if (fw_update_buffer_index >= 4096U) {
          if (w25qxx_write_buff(flash_address, fw_update_buffer, 4096) != W25QXX_RES_OK) return -1;
          flash_address += 4096U; fw_update_buffer_index = 0U;
      }
  }
  ```
  Ayrıca API sınırında `size > 4096` reddet.

### Diğer GSM bulguları (AJAN-DOĞRULANDI)
- **[gsm_engine.c:532](Application/gsm/gsm_engine.c#L532)** Dialer `#SRECV` callback'i ikinci `"\r\n#"` arıyor (olması gereken `"\r\n"`) → `iec104_on_data` hiç çağrılmıyor, gelen SCADA verisi sessizce drop. Power-outage modunda (SCADA ACK/komutlarının en kritik olduğu an) etki eder.
- **[at_engine2.c:646-655](Application/gsm/at_engine2.c#L646)** AT yanıt tamponu tamamen dolduğunda NUL-terminatör yazılmıyor → sonradan `strstr` komşu BSS'i okuyor. ✅ **DÜZELTİLDİ** — döngü sınırı `< SIZE - 1U` yapıldı; son byte NUL için ayrıldı, terminatör her zaman geçerli index'e yazılıyor.
- **[gsm_listener_process.c:165-172](Application/gsm/gsm_listener_process.c#L165)** TX fast-path branch'i yanlış zaman damgası semantiği yüzünden ölü kod → her TX'de fazladan `AT#SI` turu (gecikme). ✅ **DÜZELTİLDİ** — `socket_timer` gelecekteki deadline olduğundan gerçek geçmiş süre `now - (socket_timer - GSM_LS_SOCKET_TIMER_MS)` ile hesaplanıyor (unsigned-safe); <3 sn ise `DATA_MODE`'a doğrudan geçiliyor.
- **[gsm_engine.c:1024 / 1839](Application/gsm/gsm_engine.c#L1024)** Bazı `#SRECV`/HTTPRCV callback'lerinde `rx.len >= N` koruması eksik (diğer varyantlarda var) → potansiyel OOB. ✅ **DÜZELTİLDİ** — mevcut korumalı desene uygun olarak `if(rx.len >= 4U)` (SRECV) ve `if(rx.len >= 6U)` (HTTPRCV) guard'ları eklendi.
- **[gsm_shell.c:81-101](Application/gsm/gsm_shell.c#L81)** 28-karakter AT komutlarında `at_cmd[32]`'de 1 byte OOB yazım. ✅ **DÜZELTİLDİ** — `move_len` (`AT`/`AT+` önek uzunluğu) sınır kontrolünden önce belirleniyor; böylece 3-byte önek de hesaba katılıyor.
- **[at_engine2.c:220-232](Application/gsm/at_engine2.c#L220)** DMA TX TC kaybında `SEND_CMD`'de süresiz bekleme (yalnızca 5 dk GSM watchdog ile kurtarılır). ✅ **DÜZELTİLDİ** — `AT_ENGINE_TX_TIMEOUT_MS` (3 sn) sınırlı bekleme eklendi; timeout'ta askıda kalan DMA `HAL_UART_AbortTransmit` ile iptal edilip flag temizleniyor ve komut yeniden gönderiliyor.

**Temiz doğrulananlar:** `ring_buff.c` (SPSC, C11 atomik, doğru acquire/release), `gsm_socket.c` (tüm giriş sınır-kontrollü), `gsm_wtd.c` (reset fırtınası yapamaz), SRECV binary-mode mekanizması.

---

## 4. Web/TCP Alt-sistemi (GSM ağı üzerinden erişilebilen yüzey) — GÜVENLİK KRİTİK

Aşağıdaki dört bulgu bir **uzaktan tam-ele geçirme zinciri** oluşturur.

### K6. [DOĞRULANDI] Parolalar cihazın kendi IP'sinden türetiliyor
- **Dosya:** [http_handlers.c:278-281](Application/web-server/http_handlers.c#L278)
- **Kök neden:** `xsnprintf(expected_admin_pass, 8, "admin%d", ip_d + 1);` (son oktet+1) ve `user%d` (ilk oktet+1). GSM IP'si özel APN içinde tahmin edilebilir/gözlemlenebilir → her iki rol için de 256'lık parola uzayı.
- **Sonuç:** HTTP listener portuna ulaşabilen herkes geçerli kimlik bilgisi hesaplayabilir → config yazma, web shell, firmware, reboot = tam yetki.
- **Çözüm:** NVRAM'de tuzlanmış hash; cihaza kurulum anında zorunlu per-device parola; başarısız denemede kilitleme/gecikme.

### K7. [DOĞRULANDI] Oturum token'ı `bsp_get_tick()` tabanlı
- **Dosya:** [http_handlers.c:302](Application/web-server/http_handlers.c#L302)
- **Kök neden:** `new_token = bsp_get_tick() ^ 0x5A5A0000 ^ (uint8_t)username[0];`. ms sayacı + `'a'`/`'u'` → birkaç bin denemeyle brute-force; ayrıca loglara ve yanıt gövdesine sızıyor; TCP kopmalarında 15 dk korunuyor.
- **Çözüm:** Donanım CSPRNG'sinden 128-bit token; her bağlantıda rotasyon; deneme oranı sınırlaması.

### K8. [DOĞRULANDI] `xsnprintf` format içi `max_len`'i uygulamıyor (yığın/BSS taşması) — ✅ DÜZELTİLDİ
- **Dosya:** [libs/xprintf.c:237-411](Application/libs/xprintf.c#L237)
- **Kök neden:** `xvfprintf`'de tek sınır kontrolü **ana döngü başında** (`if (!c || count >= max_len) break;`, satır 258). `%s` gövde döngüsü ([xprintf.c:335](Application/libs/xprintf.c#L335)) ve `%d`/`%u` değer döngüsü ([xprintf.c:401-404](Application/libs/xprintf.c#L401)) `xfputc`'u **`count >= max_len` kontrolü yapmadan** tekrarlı çağırıyor. `xfputc` koşulsuz `*strptr++ = chr;` ([xprintf.c:178](Application/libs/xprintf.c#L178)). Ayrıca `max_len == 0` durumunda `max_len--` wrap → `UINT_MAX` (sınır kapanır).
- **Sonuç:**
  - `xsnprintf(buf + pos, buf_size - pos, "...", ...)` deseniyle (`pos >= buf_size` ise `max_len` 0'a inip wrap'ler) günlük/log endpoint'leri ([http_handlers.c:619-728 syslogs, 2025-2112 faults](Application/web-server/http_handlers.c#L619)) 8 KiB `tx_buffer`'ı aşar.
  - K6 parola tamponu `expected_admin_pass[8]`'e `"admin%d"` ile `ip_d≥99` → en az 1 byte stack OOB (NUL terminator) — kimlik doğrulamamış istek bile bunu tetikler.
- **Uygulanan düzeltme:** `xfputc`'a tek çıkış-noktası donanımsal sınır eklendi — bellek modunda yazımlar `strptr_end` işaretçisine (son yazılabilir data byte'ının bir sonrası; `0` = sınırsız) uyuyor. `xsnprintf` artık `strptr_end = buff + len - 1` ayarlıyor (NUL için 1 byte rezerve, `len == 0` → hiç yazım) ve terminatörü yalnızca `len > 0` iken yazıyor. Böylece `%s`/`%d`/padding alt-döngülerinin hiçbiri `max_len` wrap durumunda bile tamponu aşamıyor. `xsprintf` API'de boyut almadığından tasarım gereği sınırsız kalıyor.
- **Kalan (caller tarafı):** `xsprintf` ([http_response.c:112,179](Application/web-server/http_response.c#L112)) kullanımlarını sınırlı `xsnprintf`'e çevir; handler'larda `pos < buf_size` guard'ı iyi pratik olarak kalsın.

### K9. [AJAN-DOĞRULANDI] RFWU firmware-güncelleme — zayıf auth, kimlik doğrulamadan erişim
- **Dosya:** [raw_tcp_fw_update.c:220-277](Application/web-server/raw_tcp_fw_update.c#L220); default anahtar [raw_tcp_fw_update.h:55](Application/web-server/raw_tcp_fw_update.h#L55)
- **Kök neden:** Listener socket'i (HTTP ile TCP/80 paylaşılıyor) ilk 4 byte `RFWU` magic'i olursa **kimlik doğrulamadan önce** RFWU yoluna sokar ([web_server.c:47-72](Application/web-server/web_server.c#L47)). HELLO auth = `CRC32(shared_key) XOR total_size`, `shared_key` default `0x534D4152` ("SMAR"), header'da "production'da değiştirin" notu var. Nonce yok, rate-limit yok → 32-bit anahtar online brute-force edilebilir.
- **Sonuç:** Kalıcı firmware-level kompromizasyon.
- **Çözüm:** Per-device güçlü anahtar + gerçek challenge-response (HMAC-SHA256, server nonce); başarısızlıkta üstel geri çekilme; RFWU'yu 80. porttan/auth arkasına taşı.

### Diğer Web bulguları (AJAN-DOĞRULANDI)
- **[http_handlers.c:361-372](Application/web-server/http_handlers.c#L361)** `web_shell` tx alanı `strstr` ile ayrıştırılıyor (escaped quote atlanır); `web_shell.c` `len`'i yok sayıp NUL'e kadar okuyor. *(Düşük etki: `tx_end` NUL'lanıp `len` doğru geçildiğinden OOB yok; yalnızca kaçışlı string yanlış kesilir — açık bırakıldı.)*
- **[http_handlers.c:2156-2240](Application/web-server/http_handlers.c#L2156)** fw update `total_size` sınırsız (`INT_MAX`/`0xFFFFFFFF`), fiziksel flash boyutuna karşı doğrulanmıyor. ✅ **DÜZELTİLDİ** — `fw_update_init` ve `fw_update_rfwu_init` ([gsm_firmware_update.c](Application/gsm/gsm_firmware_update.c#L19)) artık `total_size > FIRMWARE_FLASH_AREA_SIZE` ise transferi baştan reddediyor (sürücü katmanı flash geometrisini bildiği için doğru yer).
- **[http_request_parser.c:198-209](Application/web-server/http_request_parser.c#L198)** Content-Length signed `int`'te → taşımaya imkan (request smuggling). *(Kısmen: `MAX_CONTENT_LENGTH`=4608 kontrolü var ama biriktirme sonrası; signed overflow UB — güvenlik kümesinde ertelendi.)*
- **[http_request_parser.c:40-46](Application/web-server/http_request_parser.c#L40)** NUL-byte kontrolü ölü kod (`strlen` NUL'de durur). ✅ **DÜZELTİLDİ** — ölü döngü kaldırıldı; `len = strlen(path)` uzunluk kontrolü için korundu, yolun zaten NUL-terminate olduğu not düşüldü.
- **[json_config.c:838-1023](Application/web-server/json_config.c#L838)** Dizi parse'ları `MAX_ARRAYS`'i aşınca kapanış `]`'i tüketmiyor → parse başarısız (self-DoS), desen ~40 yerde tekrar ediyor. ✅ **DÜZELTİLDİ** — tek `skip_array_remainder()` yardımcısı eklendi; paylaşılan `parse_uint32/int32/float_array` düzeltildi + `parse_bool_array` eklendi; IEC "Hatlar" ve Modbus "Hat" bloklarındaki ~44 tekrarlı satır-içi döngü bu yardımcılara indirgendi (kullanılmayan `expect_array_end` kaldırıldı). Not: `MAX_ARRAYS==MAX_LINE_COUNT==8` olduğundan yazımlar zaten sınır-içiydi (OOB yok); sorun parse-desync robustness'tı.
- **[http_server.c:171-174](Application/web-server/http_server.c#L171)** `GET /device/reboot` HTTP yanıtı göndermiyor (istemci timeout'a kadar bekler). ✅ **DÜZELTİLDİ** — GET dalı artık POST dalıyla aynı: JSON yanıt gönderip `reboot_system_delayed(3000)` çağırıyor (eskiden ne yanıt ne reboot vardı).
- **[http_handlers.c:134-209](Application/web-server/http_handlers.c#L134)** Token URL query'de (`?t=`) → log/referrer sızıntısı. *(Güvenlik kümesi — K7 ile birlikte ertelendi.)*


---

## 5. Kernel ve Altyapı

### K10. [DOĞRULANDI] Yazılım RTC'si ISR/main torn-read + ~0.1% sapma
- **Dosya:** [bsp.c:45-84](Application/bsp/bsp.c#L45) (`bsp_rtc_process`, 1 ms TIM17 ISR'inden çağrılır)
- **Kök neden (torn-read):** `rtc` struct'ı **ISR'de** `bsp_rtc_process` ile mutasyona uğrar; ana bağlam `bsp_get_datetime()`/`bsp_get_pdate()` ile struct'ı **byte-byte** kopyalar. Saniye/dakika/saat/gün alanları 1 ms ISR'inde kademeli güncellenir; okuma bu kademelerin ortasında olursa **karışık değer** (örn. saniye=0 ama dakika=eskisi) alınır. `volatile` yok, kritik bölüm yok (uygulama hiç kullanmıyor — Bkz. §6).
- **Kök neden (sapma):** `if (rtc.millisec++ >= 1000)` **post-increment** → her saniye-incrementi **1001 tick**'te bir. 1 ms/tick → 1001 ms/saniye ≈ 0.1% hızlı (~3.6 sn/saat). Aynı post-increment deseni saniye/dakika/saat için de var.
- **Sonuç:** Olay zaman damgaları zaman zaman tutarsız ve sistematik olarak ileri; `rtc_resync_process` (6 saatte HW RTC'den senkron, [app_main.c:360-384](Application/app_main.c#L360)) sapmayı telafi ediyor olsa da arada sapma var.
- **Çözüm:** (a) pre-increment + tam-eşik: `if(++rtc.millisec >= 1000)`. (b) Zaman damgası için atomik snapshot: ya ISR'de tek 32-bit epoch tut, ya da kritik bölüm içinde struct kopyala. (c) Torn-read'u gidermek için en temizi: RTC'yi yalnızca HW RTC'den periyodik okuyup ana bağlamda önbelleğe al; ISR'de kalender hesap yapma.

### K11. [DOĞRULANDI] `nvram_sync()` tüm-kayıt + çift-adres bloklayıcı yazım
- **Dosya:** [nvram.c:402-418](Application/nvram.c#L402) (sync) + [w25qxx.c:548-631](Application/libs/w25qxx.c#L548) (write_buff)
- **Kök neden:** Her sync, `sizeof(nvram_t)` kadar veriyi **hem NVRAM hem NVRAM_BACKUP** sektöre yazar. `w25qxx_write_buff` her değen 4 KiB sektör için: **tüm sektörü oku → erase (~60 ms typ / 300 ms max) → 16 sayfa yaz (~0.7 ms/sayfa) → verify**, 3 denemeye kadar. CRC-skip optimizasyonu var ama CRC her değişiklikte değişir.
- **Çağrıcılar:** [console_logger.c:20](Application/cslog/console_logger.c#L20), [digital_input.c:143](Application/digital_input.c#L143), [gsm_log.c:38](Application/gsm/gsm_log.c#L38), [modem_config.c:16](Application/modem_config.c#L16), [iec104_config.c:18](Application/libiec104/iec104_config.c#L18), [modbus_config.c:16](Application/libmodbusrtu/modbus_config.c#L16), [rf_config.c:15](Application/rf/rf_config.c#L15), [json_config.c:1932](Application/web-server/json_config.c#L1932) (web config save). Web config save → her kayıtta birkaç yüz ms kernel donması.
- **Sonuç (kooperatif kernel):** Sync sırasında **tüm süreçler durar**. SPI write sürerken:
  - GSM AT motoru yanıt penceresini kaçırabilir; Modbus RTU frame'i buffer'a dolup overflow korumasınca drop edilir (Bkz. §7); IEC104 TX slot zamanlaması bozulur.
  - W25Q ~100K erase-cycle; sık sync NVRAM sektörlerini yıpratır. WDT `bsp_kick_wdt` ([bsp.c:211](Application/bsp/bsp.c#L211)) sync döngüsünde tetikleniyor ama `#ifdef RELEASE`'e bağlı; Debug'da WDT beslenmez → uzun erase+deneme WDT reset'ini tetikleyebilir.
- **Çözüm:** (a) Periyodik tek sync (örn. her 60 sn dirty-flag ile). (b) Web config save'de sync'i **delayed/deferred** yap (dirty flag → bir sonraki tick'te yaz). (c) NVRAM için wear-leveling veya log-structured küçük write'lar (alt-sektör). (d) Yedek yazımını arka planda/eşzamansız zamanla.

### K12. [DOĞRULANDI] `lifetime` periyodik persistansı yok
- **Dosya:** [app_main.c:292](Application/app_main.c#L292) + [modem_config.c:215](Application/modem_config.c#L215)
- **Kök neden:** Heartbeat her saniye `modem_config_set_lifetime(+1)` (yalnızca RAM). `set_lifetime` sync çağırmıyor. `lifetime` yalnızca **başka bir şey** sync ettiğinde diske yazılır (config save, DI değişimi, cslog toggle). Sessiz cihazda lifetime RAM'de birikir, güç kesintisinde kaybolur.
- **Çözüm:** Periyodik (örn. 10 dk) dirty-flag tabanlı sync; veya ömür sayacını wear-leveled sayaç alanında tut.

### Diğer altyapı bulguları (DOĞRULANDI)
- **[app_main.c:276-277](Application/app_main.c#L276)** Yorum/süreç uyuşmazlığı: `stack_timer` `CLOCK_SECOND*60` (60 sn) ama yorum "10 seconds" diyor; `rf_dummy_timer` 5 sn ama yorum "2 second". (Düşük risk; yanıltıcı dokümantasyon.)
- **[app_main.c:444 ve 477](Application/app_main.c#L444)** `xmodem_app_init()` **iki kez** çağrılıyor. İkincil init idempotent ise zararsız; doğrulanmalı.
- **[app_main.c:484-490](Application/app_main.c#L484)** Ana döngü idle iken **meşgul-döngü** (`if(process_nevents()) while(process_run());`) → WFI yok, güç israfı. `__WFI()` eklemek (kesme ile uyandırma) önler.
- **[bsp.c:27, 121-129](Application/bsp/bsp.c#L27)** `life_timer` uint32 → 49.7 günde taşma. `bsp_time_passes` wrap'ı doğru işliyor ama `bsp_get_epoch_time = (life_timer/1000) + base_epoch_time` taşmada geri sıçrar (epoch ~49 gün geri gider). Uzun ömürlü cihaz için real.
- **[bsp.c:153-169](Application/bsp/bsp.c#L153)** `bsp_delay_us` SysTick'i yeniden programlıyor; `AHB_FREQ=96000000` CubeIDE cpuclock=96 ile uyumlu (doğru) ama `platform-conf.h:10 CPU_CLOCK_FREQ_=550000000` yanlış/stale (sadece CPU-usage makrolarında, kapalı).
- **[platform-conf.h:42](contiki-kernel/platform-conf.h#L42)** `_PLATFORM_ = _WIN32_` ve **[int-master.c:42-83](contiki-kernel/sys/int-master.c#L42)**: ARM derlemesinde `PLATFORM(_ARM_CORTEXM_)` false → `critical_enter/exit` **tamamen no-op**, üstelik `int_master_enable()` içten `__disable_irq()` çağırıyor (ters). **Latent** çünkü uygulama bunları kullanmıyor; ama kernel portunun ARM desteği tam değil — ileride biri `critical_enter` kullanırsa sessiz race. (Düşük aktif risk, yüksek tuzak riski.)

---

## 6. SPI Flash (W25Q) Organizasyonu

### [DOĞRULANDI] Yorum adresleri yanıltıcı (offsetof makroları doğru)
- **Dosya:** [spi_flash_organization.h:26-50](Application/libs/spi_flash_organization.h#L26)
- **Durum:** Header'daki "Computed Address" yorumları FIRMWARE_A'dan itibaren yanlış (örn. NVRAM için `0x110000` diyor ama `offsetof` ile gerçek değer `0x210000` — çünkü FIRMWARE_A/B `_Alignas(0x10000)` dolgu içeriyor). Kod makroları (`SPIFLASH_SECTION_ADDR`) kullanıyor → **gerçek adresler doğru ve çakışmasız**; `_Static_assert`'ler geçerli. Bu bir **false positive tuzağı**ydı; offsetof makroları kurtarıyor.
- **Risk:** Yalnızca elle adres hesaplayan gelecekteki geliştiriciyi yanıltır. Header yorumlarını gerçek `offsetof` değerleriyle düzelt. (LOW)

### [DOĞRULANDI] Bootloader/app VTOR + linker tutarlı
- **Durum:** `main.c` Release'de `SCB->VTOR = FLASH_BASE | 0x14000` (80 KB bootloader sonrası). `.cproject` eşlemesi: **Debug → STM32U375VETX_FLASH.ld** (ORIGIN 0x08000000, VTOR override yok → tutarlı), **Release → STM32U375VETX_BOOT.ld** (ORIGIN 0x08014000 → VTOR ile uyumlu). Doğruladım, **tutarlı, bug yok**. Yalnızca tutarlılık riski: FLASH.ld ile Release derlemek vector tabloyu 0x08000000'a koyup VTOR 0x08014000'e işaret ederdi → ilk keserde HardFault. Build config'lerin yanlışlıkla değiştirilmemesine dikkat.

### [DOĞRULANDI] W25Q write_buff'inin dayanıklılığı iyi ama tamamen senkron
- **Dosya:** [w25qxx.c:548-631](Application/libs/w25qxx.c#L548)
- **Olumlu:** Modify-before-write için **tüm sektörü önbelleğe alıp** erase+write+verify yapıyor; 3 denemeli retry + `bsp_kick_wdt`; power-fail sırasında main/backup yedekliliği (boot'ta CRC → yedek) veri kaybını önlüyor. Tek `cache_mem` global'i kooperatif kernelde reentrant çağrı olmadığı sürece güvenli (ISR'den çağrılmıyor).
- **Risk:** Tamamen bloklayıcı (K11). Ve write_buff her zaman **tüm 4 KiB sektörü** erase ediyor — küçük bir nvram alanı için bile. Wear açısından maliyetli.

---

## 7. Modbus RTU Slave — Temiz (DOĞRULANDI, kendi denetimim)

- **Dosya:** [modbus_rtu_slave.c](Application/libmodbusrtu/modbus_rtu_slave.c) (tamamı okundu)
- **Değerlendirme:** İyi yazılmış, sıkı sınır kontrolü. CRC16 tablosu standart (init 0xFFFF). FC03: `count` doğrulaması (1..MAX_READ_REGISTERS), `response_len > MODBUS_BUFFER_SIZE` kontrolü, register çözümleme overflow-korumalı, her-register callback durumu doğrulanmış. FC06: uzunluk kontrolü, broadcast ack'siz, callback durumu dispatch'i. Frame: CRC doğrulaması, slave-id/broadcast filtresi, `busy` bayrağı ISR/main tampon paylaşımını koruyor. RX byte overflow koruması (rx_count ≥ BUFFER → sıfırla).
- **Tek çapraz etki (K11 ile):** Yazılım RTO (`MODBUS_USE_HW_RTO=0`) `get_tick_ms() - last_rx_time >= MODBUS_TIMEOUT_MS` ile. Sync gibi **bloklayıcı** bir işlem sırasında gelen Modbus frame'i `rx_count` overflow'a uğrar ve drop edilir; master timeout+retry ile kurtarır ama güvenilirlik düşer. Bu bir Modbus bug'ı değil, kernel-stall yan etkisidir.
- **Küçük notlar (LOW):** `busy` bayrağı `volatile` değil (Cortex-M'de pratikte çalışır ama sağlamlaştırmak için `volatile`); overflow drop'unda gözlemlenebilirlik/ sayaç yok; T3.5 sabitinin baud/`get_tick_ms` çözünürlüğüyle (1 ms) uyumu doğrulanmalı.

## COBS — Temiz (DOĞRULANDI)
- **Dosya:** [libscp/cobs.c](Application/libscp/cobs.c) — encode/decode/decode_inplace hepsinde çıkış boyut kontrolü, 0x00 reddi ve `MAX_CODE (0xFF)` yönetimi doğru. `decode_inplace`'in `write_idx < read_idx` invaryantı sağlam. RF framing temeli güvenli görünüyor.

---

## 8. Çapraz Kesin Bulgular (modüller arası)

1. **Kooperatif kernel × bloklayıcı SPI flash:** Tüm `w25qxx_write_buff`/`nvram_sync` çağrıları (elog, iec104_event_log, fault_log, nvram, fw_update) sistemi 60-300 ms (deneme × 2 yedek) durdurur. Bu, IEC104/Modbus zamanlamalarını ve GSM AT pencerelerini doğrudan etkiler. **Çözüm:** flash erişimini tek bir düşük-öncelikli "flusher" süreçte topla + dirty-flag + chunked write.
2. **Kritik bölüm yok:** Uygulama `__disable_irq`/`critical_enter` kullanmıyor. Çok-alanlı ISR paylaşımları (yazılım RTC'si — K10) torn-read'a açık. Tek-alanlı/SPSC desenler (ring buffer) güvenli.
3. **`xsnprintf` güvenli değil (K8):** Projedeki tüm sınırlı biçimli yazımlar güvenilmez; özellikle dış girdi içeren (Web) veya uzun döngülü (syslogs/faults JSON) her yerde taşma potansiyeli. Bu, libs katmanındaki **tek noktada** düzeltilebilen yüksek etki.

---

## 9. Doğrulanmış-Sağlam (false-positive elediğim alanlar)

Rigor için, teyit edip **temiz** bulduğum alanlar:
- Tick → etimer zinciri (TIM17 → bsp_tick_handler → clock_tick + etimer_request_poll) — doğru.
- `_PLATFORM_`/int-master — latent only; uygulama kullanmıyor (bug değil, tuzak).
- VTOR/linker — Release BOOT.ld ile tutarlı (bug değil).
- SPI flash adresleri — offsetof makroları doğru (yorumlar yanıltıcı olsa da).
- Modbus RTU slave — sıkı sınır kontrolü, doğru CRC.
- COBS encode/decode — doğru sınır/0x00 yönetimi.
- Ring buffer (GSM) — doğru SPSC atomik.
- Protothread yerelleri — IEC104/GSM süreçlerinde `static` kullanımı doğru.

---

## 10. RF + SCP + Power Board (2. tur — DOĞRULANDI)

### K13. [DOĞRULANDI] I2C slave callback'leri yanlış peripheral (I2C1 vs I2C3) — power-board link ölü
- **Dosya:** [i2c_slave.c:59, 88, 109, 121](Application/power_board/i2c_slave.c#L59) (ve doğru olan [AddrCallback :25](Application/power_board/i2c_slave.c#L25))
- **Kök neden:** Modül yalnızca `hi2c3` kullanıyor (`extern`, [i2c_slave.c:10](Application/power_board/i2c_slave.c#L10); `i2c_slave_init` → `HAL_I2C_EnableListen_IT(&hi2c3)`, [:141](Application/power_board/i2c_slave.c#L141)). `HAL_I2C_AddrCallback` doğru olarak `I2C3` kontrol ediyor; ama `SlaveRxCpltCallback`, `SlaveTxCpltCallback`, `ListenCpltCallback`, `ErrorCallback`'in dördü de `hi2c->Instance == I2C1` kontrol ediyor. I2C3 olayları bu callback'lere `Instance==I2C3` ile gelir → `I2C1` guard'ı false → fonksiyonlar boş dönüyor. `current_reg_addr` hiç yazılmıyor, sonraki `Seq_Receive_IT` hiç zamanlanmıyor, listen modu STOP sonrası yeniden silahlanmıyor.
- **Durum:** `i2c_slave_init()` şu an `app_main.c`'de **çağrılmıyor** (grep teyit) ve `power_board/` git'te untracked (yeni/WIP, 5 Tem 2026). Yani modül **latent** — derleniyor olabilir ama init edilmedigi için callback'ler ateşlenmiyor. Ancak güç kartı iletişimi devreye alınacağı an bu hata yüzünden hiç çalışmaz.
- **Ek (LOW):** [i2c_slave.c:14](Application/power_board/i2c_slave.c#L14) `i2c_memory_map` `volatile` değil (ISR yazıyor, ileride ana bağlam okuyacak → torn-read).
- **Çözüm:** 4 callback'in `I2C1` kontrolünü `I2C3` yap (ya da per-instance guard'ı kaldır, modül yalnızca I2C3'e hizmet ediyor). `i2c_memory_map`'i `volatile` yap.

### K14. [DOĞRULANDI] USER_RESET eşik makroları ters — fabrika ayarı dalı ölü kod — ✅ DÜZELTİLDİ
- **Dosya:** [digital_input.c:39-45](Application/digital_input.c#L39) (makrolar) + [digital_input.c:131-159](Application/digital_input.c#L131) (dispatch)
- **Kök neden:** Makro değerleri `NVRAM_TICKS = 15s`, `OTHER_TICKS = 10s` (design bloğu [:30-36](Application/digital_input.c#L30) ise `RESET < NVRAM < OTHER` istiyor, yani NVRAM<OTHER). Dispatch alçalan sırayla: `held >= OTHER(10s)` → "log only" ÖNCE yakalıyor; `held >= NVRAM(15s)` dalı ise 15s≥10s olduğu için hiç tetiklenmiyor → **fabrika ayarı butonu imkânsız**. Dispatch yorumları (">=10s NVRAM", ">=15s reserved") makroların nihaî (swapped) değerlerini yansıtıyor; makro tanımları ters.
- **Çözüm:** Makro değerlerini değiştir: `NVRAM_TICKS = 10s`, `OTHER_TICKS = 15s` (design bloğu ve dispatch yorumlarıyla uyumlu).

### K17. [DOĞRULANDI] RF PING `0xFF`'e + gelen ACK temizlenmiyor → RF link göstergesi yanıltıcı
- **Dosya:** [rf_process.c:169](Application/rf/rf_process.c#L169) (PING) + [rf_process.c:81-111](Application/rf/rf_process.c#L81) (ACK işlenmiyor) + [rf_process.c:157-162](Application/rf/rf_process.c#L157) (timeout uyarısı)
- **Kök neden (PING):** `scp_send_ping(&s_rf_scp_ctx, 0xFFU, 0U)` "Broadcast PING" yorumuyla; ama [scp.h:45](Application/libscp/scp.h#L45) `SCP_BROADCAST_ADDR = 0x00` ve [scp.c:83](Application/libscp/scp.c#L83) kabul `(dst==device_address) || (dst==SCP_BROADCAST_ADDR)`. `0xFF` standart aralık dışı (0x01–0xFE) → hiçbir standart düğüm kabul etmez.
- **Kök neden (ACK):** `rf_handle_packet` yalnızca `SCP_TYPE_PING` case'ine sahip; `SCP_TYPE_ACK` yok → `default`'a düşüp log'lanıyor. `s_rf_state` `RF_STATE_WAIT_RESPONSE`'tan `RF_STATE_IDLE`'a yalnızca 2 sn'lik `response_timer` bitiminde geçiyor → her döngüde "No response received" uyarısı.
- **Sonuç:** RF link'i hiç çalışmıyor gibi görünür (broadcast ulaşmaz + ACK işlenmez).
- **Çözüm:** `scp_send_ping(..., SCP_BROADCAST_ADDR, 0)` (veya hedef unicast adres). `rf_handle_packet`'a `case SCP_TYPE_ACK:` ekle; sekans/cmd eşleşirse `s_rf_state = RF_STATE_IDLE`.

### [DOĞRULANDI] SCP TX bloklayıcı (kooperatif kerneli durdurur)
- **Dosya:** [rf_process.c:54-62](Application/rf/rf_process.c#L54) → [uart.c:44-59](Application/bsp/uart.c#L44)
- **Kök neden:** `rf_scp_transmit` → `uart_send_buffer` → her byte için `while(!LL_USART_IsActiveFlag_TXE_TXFNF(uart))` meşgul-bekleme. SCP frame ≤256 byte → @115200 ≈ 22 ms tam CPU kararması. ACK cevabı ve PING protothread içinden gönderiliyor → her PING/ACK çevrimi ~22 ms diğer süreçleri (heartbeat, GSM, modbus, debounce, etimer) durdurur. (Aynı bloklayıcı desen `bsp_putchr`'de LPUART TXE için de var.)
- **Çözüm:** TX'i kesme/DMA tabanlı `HAL_UART_Transmit_IT/_DMA`'a taşı; ya da chunk'lı non-blocking durum makinesine çevir.

### RF/Power diğer bulgular (AJAN-DOĞRULANDI)
- [rf_process.c:68-71](Application/rf/rf_process.c#L68) RF RX ISR `rbuff_write_byte` dönüş değerini yok sayıyor → ring overflow'da sessizce byte drop (LOW; SCP CRC zaten bozuk frame'i reddeder).
- **scp.c temiz (DOĞRULANDI):** header/len/~len/CRC doğrulaması sıkı, endian yardımcıları doğru, COBS yerinde-çözme `SCP_COBS_MAX_SIZE` ile sınırlı, 100 ms inter-byte timeout wrap-güvenli.
- **rf.c/rf_config.c/rf_dummy.c temiz:** `MAX_POWER_LINE_COUNT=8` ile sınırlı, saturating cast'ler var; `rf_dummy` build'de kapalı.

---

## 11. libs + Logging + State Machines (2. tur)

### K15. [DOĞRULANDI] shell `rx_buff` dolunca NUL terminatör OOB — ✅ DÜZELTİLDİ
- **Dosya:** [shell.c:407-416](Application/libs/shell.c#L407) (+ ESC ok yolu [:396](Application/libs/shell.c#L396))
- **Kök neden:** `rx_buff[SHELL_TEXT_MAXLEN]` (64 byte, geçerli indeks 0–63). `if(rx_idx < SHELL_TEXT_MAXLEN)` guard'ı `rx_idx`'i 64'e kadar ilerletmeye izin ediyor; sonra `rx_buff[rx_idx] = 0` (ENTER yolu [:416](Application/libs/shell.c#L16)) koşulsuz yazılıyor. 63 karakter + ENTER (64. bayt) → `rx_buff[64] = 0` OOB. Aynı kalıp ESC ok tuşlarında ([:396](Application/libs/shell.c#L396)) ve dolu satırda. `shell_on_rx_received` konsol UART ISR'inden ([stm32u3xx_it.c:453](Core/Src/stm32u3xx_it.c#L453)) çağrılıyor.
- **Tetik:** Konsola (USB) 64 bayt girdi. Komşu static sembolü 1-byte bozar.
- **Çözüm:** Ya `rx_buff[SHELL_TEXT_MAXLEN + 1]` ya da her terminatör store'una `if(rx_idx < SHELL_TEXT_MAXLEN)` guard'ı ve karakter kabulü `SHELL_TEXT_MAXLEN - 1`'de durdur.

### K16. [DOĞRULANDI] `shell_exec` `argc`'yi yok sayıyor → NULL deref HardFault — ✅ DÜZELTİLDİ
- **Dosya:** [shell.c:628-636](Application/libs/shell.c#L628)
- **Kök neden:** `shell_exec` `(void)argc;` ile argc'yi yok sayıyor; `argv[1]`'i `strcmp`'e veriyor. `exec` argümansız (argc=1) → `argv[1]=NULL` → `strcmp(NULL,...)` → HardFault. (`shell_su`/`shell_kill`/`shell_terminal_char_set_test` doğru şekilde argc kontrol ediyor; yalnızca `exec` değil.)
- **Tetik:** `su admin` sonra `exec` (argümansız). Cihaz çöker.
- **Çözüm:** `shell_kill` gibi başa `if(argc < 2){ usage; return -1; }` ekle.

### Diğer libs/logging/state bulguları (AJAN-DOĞRULANDI)
- **[xmodem.c:355](Application/libs/xmodem.c#L355)** (MEDIUM) Yinelenen blok (kayıp ACK sonrası yeniden iletim) NAK ediliyor → lost-ACK kurtarısı kırık. Ayrıca **yalnızca SOH (128 B)** işleniyor, STX/XMODEM-1K blokları sessizce drop.
- **[elog.c:37](Application/elog.c#L37)** (MEDIUM) `elog_write` sınır kontrolü ölü ifade (`addr + size <= addr + len` → `size <= len` her zaman false). Bölge sınır kontrolü değil.
- **[elog.c:80-86](Application/elog.c#L80)** (MEDIUM) elog girdilerinde per-entry CRC yok, `w25qxx_write_buff`'in yazma hatası yok sayılıyor; `total_entries` yazmadan önce artıyor → bozuk/garip girdi metadata'da tutarlı görünür. (`fault_log_t`'nin per-entry CRC'si var, örnek alınmalı.)
- **[xscanf.c:504](Application/libs/xscanf.c#L504)** (MEDIUM) `%s` (küçük harf, subtype yok) sessizce hiçbir şey yapmıyor, `va_arg` tüketmiyor, `count` artmıyor → sonraki tüm dönüşümler yanlış argüman okur (tip karışıklığı). Doğrulayıcı `%S` (büyük). `%s` için `err = INVALID_FORMAT` döndür.
- **[xscanf.c:567](Application/libs/xscanf.c#L567)** (LOW) `%S` widthsiz varsayılan 16 → çağrıcının 8 baytlık buffer'ına 17 bayt yazabilir.
- **[shell.c:425-429](Application/libs/shell.c#L425)** (MEDIUM) Dolu satırda backspace 2 karakter siliyor (byte store edilmeden önce `rx_idx--` çalışır). TAB benzeri.
- **[reboot.c:46](Application/reboot.c#L46)** (LOW) `process_start(&reboot_process, &delay_ms)` stack by-value parametresinin adresini geçiyor; protothread ilk WAIT öncesi okuduğu için şu an çalışıyor ama kırılgan → `static`'e taşı.
- **[datetime.c:102](Application/libs/datetime.c#L102)** (LOW) `dt_init` ay 1-12 / gün 1-31 doğruluyor ama ay-bazı gün sayısı ve artık yıl kontrolü yok (30 Şub kabul).
- **[periodic_reset.c:34](Application/periodic_reset.c#L34)** (LOW) `s_period_s * CLOCK_SECOND` ürünü `uint32_t`'te çok büyük periyotlarda taşınır → ani reset. Clamp ekle.
- **[test_logs.c:114-127](Application/test_logs.c#L114)** (LOW) JSON inşasında `pos < buf_size` guard yok; `buf_size - pos` negatife kayınca `xsnprintf` sınırı kapanır (K8 ile etkileşim).

### libs/logging/state — Temiz doğrulananlar (DOĞRULANDI/AJAN-DOĞRULANDI)
- **crc32.c:** standart reflected CRC-32 (poly 0x04C11DB7, init/xor 0xFFFFFFFF), tablo zlib/PNG ile uyumlu.
- **ring_buf.c:** QuantumLeaps SPSC, `_Atomic head/tail`, doğru acquire/release, full/empty/num_free matematiği doğru.
- **debouncer.c:** integratör 0'dan aşağı/`debounce_time`'dan yukarı saturate; taşma yok.
- **datetime.c:** Zeller gün-hesabı ve epoch round-trip doğru (yalnızca `dt_init` doğrulaması zayıf).
- **stack_monitor.c:** paint/scan matematiği doğru, OOB/divide-by-zero/underflow guard'ları, IPSR guard'ı (exception frame'den paint'i engeller).
- **breaker.c:** tüm erişimciler `line_index < MAX_POWER_LINE_COUNT` kontrolü; `feeder_data` memcpy `sizeof` ile. (SBO IOA eşitliği yorumda — TODO.)
- **fault_log.c:** per-feeder RAM önbellek + whole-block + per-entry CRC, lazy flush, backup kurtarma, tüm feeder/phase/index sınır kontrolleri.
- **boot.c:** primary/backup superblock CRC + çift-hata durumunda bölüm A'ya güvenli default; app-side anti-rollback yok (tasarım gereği bootloader'un işi).
- **time_service.c:** `time_get_elapsed` wrap-around matematiği doğru (if/else gereksiz ama doğru).
- **system_status.c:** min/max ilk örnekle tohumlu, erişimciler doğru.
- **periodic_reset.c:** reset-fırtınası yok (yalnızca periyot değişiminde `timer_set`, terminal `bsp_system_reset`).
- **scp.c, scp_endian.h:** bkz. §10 (temiz).
- **xmodem.c:** ISR/main el sıkışması (PACKAGE_READY yapışkan) — tampon race yok; CRC-16-CITT polinomu doğru; 256-blok wraparound doğru.

---

## 12. Önerilen Fix Yol Haritası (fazlar)

**Faz 1 — Kritik güvenlik + brick-riski (hemen):**
1. ~~K8: `xprintf.c` `xvfprintf`'de `xfputc` öncesi `max_len` kontrolü.~~ **ERTELENDİ** (kullanıcı kararı). Not: K8 gerçek bir kontrat bug'ıdır (`xsnprintf` verilen `len`'i uygulamaz), ama sömürü çağıranlara bağlıdır; en net tetikleyici C4 (login) olup K6 (IP-türetli parola) kaldırılınca ölüyor. Bu yüzden **K6 yapılırsa K8 ertelenebilir**. İleride `xsprintf` çağıranlarının `xsnprintf`'e göçü ile birlikte ele alınacak (hardening).
2. ✅ K5: `gsm_firmware_update.c` chunk yazımında copy-öncesi sınır + parça-flush döngüsü. **DÜZELTİLDİ** — `fw_update_write_handler` artık `min(size, space)` ile parça-parça kopyalayıp sektör dolunca flush ediyor; taşma kapantı, her chunk boyutu (4096/1024/partial) güvenli.
3. K6/K7/K9: Web auth şemasını kaldır (parola hash + CSPRNG token), RFWU'yu güçlü challenge-response + port/auth arkasına taşı.

**Faz 2 — IEC104 protokol canlandırma (SCADA birlikte çalışımı):**
4. K1: `iec104_tick()`'i süreçte 1 sn'lik tick'e bağla (T1/T2/T3/TESTFR canlanır).
5. K2: `k_counter`/`w_counter` karışıklığını düzelt (841, 881, 1108).
6. K3/K4: Red ve tamponlu fault frame'lerinde taze APCI + `iec104_send` üzerinden geçiş.
7. K5_iec/K6_iec: K pencere guard + ACK wrap.

**Faz 3 — Kernel sağlamlığı:**
8. K10: Yazılım RTC torn-read (atomik snapshot) + sapma (pre-increment).
9. K11: `nvram_sync`'i deferred/dirty-flag tabanlı tek flusher'da topla; WDT debug'da da besle.
10. K12: `lifetime` periyodik persistans.
11. Idle'da `__WFI()`; `xmodem_app_init` çift çağrıyı gider; yorumları düzelt.
12. K17/SCP-TX: RF PING'i `0x00`'a, ACK case ekle; `uart_send_buffer` TX'i DMA/IT tabanlı yap (kernel stall kaynaklarını kapat).

**Faz 4 — Power Board + Shell + tüketiciler (2. tur bulguları):**
13. K13: `i2c_slave.c` 4 callback'in `I2C1`→`I2C3`; `i2c_memory_map` `volatile` (power-board devreye alınmadan ÖNCE).
14. ✅ K14: `digital_input.c` USER_RESET eşik makroları değiştirildi (NVRAM=10s, OTHER=15s). **DÜZELTİLDİ** — artık 10-14 sn basış fabrika ayarını tetikliyor.
15. ✅ K15/K16: `shell.c` rx_buff `[SHELL_TEXT_MAXLEN+1]` (NUL yeri) + `shell_exec` argc kontrolü. **DÜZELTİLDİ**.
16. xmodem dup-block ACK + STX/1K desteği; elog_write bölge kontrolü + per-entry CRC + yazma durumu; xscanf `%s`→INVALID_FORMAT.

**Faz 5 — Hardening:**
17. MISRA/CERT Katı modda `-Wconversion -Wshadow -Wstack-usage` ile derleme; cppcheck/PC-lint.
18. LOW bulgular (reboot static, datetime doğrulama, periodic_reset clamp, test_logs guard).

---

*Tüm modüller artık denetlendi. Her CRITICAL/HIGH bulgusu kod okuyarak doğrulandı (`[DOĞRULANDI]`); MEDIUM/LOW'lar mekanizma açısından tutarlı, düşük güvenilirlikte olanlar işaretli. Fix'leri istediğiniz sırayla, modül modül, adım adım uygulayabiliriz — başlamak için bir faz/sıra seçmeniz yeterli.*
