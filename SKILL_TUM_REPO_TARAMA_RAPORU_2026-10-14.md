# Tam Repo Skill Tarama Raporu — 2026-10-14

İlk 12 bölüm başlangıçtaki statik taramanın tarihsel kaydıdır. Sonraki
değerlendirme, düzeltmeler, yürütülen kanıtlar ve commit kaydı §13 içindedir.

## Amaç

`.github/skills/` altindaki 8 denetim/test skill'ini kullanarak tum repoyu bir
taramadan gecirmek ve sonuclari tek dosyada toplamak.

## Kapsam

- Uretim: `Application/` (122 .c dosyasi; rf, gsm, web-server, libscp,
  libiec104, libmodbusrtu, nvram, elog, fault_log, bms, power_board, app_ipc,
  libefw, libs, bsp), `Core/Src` (yalniz USER CODE + ISR baglantilari).
- Kapsam DISI (mevcut kullanici kararlari): `Drivers/` (HAL vendor),
  `contiki-kernel`, CubeMX uretim bolmleri, BSL kardes repo ("bootlodaer").
- Test tarafinin incelenmesi: `test/` agaci (merkezi Ceedling + integration +
  system/target paketleri).

## Yontem ve kanit modeli

- 7 denetim skill'i paralel dilimlerde uygulandi (runtime-safety,
  concurrency/ISR, durum makinesi, bootloader/guvenlik, genel defo avi, test
  kapsami, fonksiyonel envanter); 8.'si (ceedling-test-generator) PLAN
  kipinde, kapsam incelemesinin ciktilariyla calistirildi. Her dilim once
  kendi `SKILL.md` + ilgili `references/*.md` dosyalarini okudu.
- **Kanit tavana E1'dir** (kod tabani izi): hicbir build/test/harness
  kosturulmadi. Bu yuzden skill kurallari geregi **hicbir bulgu "Confirmed
  defect" olamaz** — tum adaylar `Candidate` / `Unverified risk` /
  `Requirement ambiguity` / `Improvement` sinifindadir. "Sifir teyitli defo"
  sonucu, kodun temiz oldugunu DEGIL, yurutulmus kanit toplanmadigini gosterir.
- Skill'lerin kapsam kapisi ("tum repo" gecersiz kapsamdir, 5-8 aday oner ve
  dur) kullanici yonlendirmesiyle devre disi birakildi: tum repo, skill
  ozelliklerine gore modul dilimlerine bolundu. Bu sapma bilincli kayittir.
- **Spot-dogrulama:** derleyici (ana oturum), en kritik 7 bulguyu kaynakta
  yeniden kontrol etti (Bolum 5). Kalan bulgular ajan ciktisi olarak kalir ve
  boyle etiketlenmistir.
- Uretim koduna HIC dokunulmadi; test kosturulmadi; commit yapilmadi.
- Satir numaralari 14.10 calisma agacinin durumunu yansitir (Application/
  kodu commit edilmis durust durumda; degisen dosyalar yalniz
  `.github/copilot-instructions.md`, `.settings/language.settings.xml`).

## Ozet

| Skill | Taranan yuzey | Sonuc |
|---|---|---|
| embedded-c-runtime-safety-auditor | parser/serilestirme (json_config, rf_scp_codec, xscanf, modbus, iec104, elog, fault_log) | 3 dusuk siddetli aday (RSA-1..3) |
| concurrency-isr-auditor | ISR/main kesisimi, power_panic, RF/GSM RX, RTC, shell | 3 aday (CON-1..3), 1'i orta-yuksek |
| state-machine-auditor | rf_inventory, libscp/rf_comm, gsm, http fw-aktarimi, xmodem | 3 aday (STM-1..3) |
| bootloader-security-auditor | boot, xmodem, /fw_*, RFWU, libefw, nvram arbitraj, app_ipc | 1 aday (BSL-1); imaj/guc-kesintisi yollari saglam cikti |
| proof-driven-bug-hunter | http_handlers, modbus stats, rf apply/staging, bms/power_board kalintilari | 1 Candidate (PBH-1, dikkat cekici) + 1 Improvement; 11 suphe kanitla CURUTULDU |
| firmware-test-coverage-reviewer | test/ x uretim matrisi | 5 bosluk (COV-1..5) |
| device-functional-test-designer | tum urun (MODE: inventory) | ~90 islevlik envanter + 9 karanlik yuzey |
| ceedling-test-generator | COV-1/COV-2 icin PLAN kipi | 9 test tasarimi (CTG-1..9), uretilmedi |

En dikkat cekenler: **PBH-1** (govdesiz `POST /s` → `strstr(NULL)` →
HardFault riski), **CON-1** (shell RX satir tamponu iki baglamdan korumasiz),
**CON-2** (yazilim RTC yazicilari kritik bolgesiz), **STM-2** (web fw
aktariminda timeout sabitleri olu kod — yarida kalan aktarim surece `active`
gorunur).

Bilinen acik madde guncellemesi: **SCP R1 aralik uyusmazligi KAPANDI**
(json_config.c artik R1 araliklarini dogruluyor — Bolum 6).

---

## 1. embedded-c-runtime-safety-auditor (RSA)

Genel izlenim: taranan yuzeyde tamsayi tasima kontrolleri, dizi artik
drain'leri, staging atomikligi, wire-layout `_Static_assert`'leri ve APDU
minimum uzunluk dogrulamasi guclu. 0 teyitli hata, 3 dusuk siddetli aday.

### RSA-1 | IEC hata-mesaji bicimlendiricide dizi boyu ile `17` sabiti bagi yalniz insan disiplinine bagli | Improvement | E1 | SPOT-DOGRULANDI

- Yer: `Application/web-server/json_config.c:940-942`
  (`format_iec_address_field`'da `% 17U`), `:903-922` (`iec_address_keys[]`,
  tam 17 baslatci), `:1029` (`line_id = 2U + (line * 17U)`),
  `:1018-1019` (kapasite formulu `2 + MAX_ARRAYS*17`).
- Iddia: dizinin eleman sayisini ifade eden `sizeof`/`_Static_assert` yok;
  bugun 17==17 sinir ici, ama listeden anahtar cikarilirsa `iec_address_keys[16+]`
  okumasi const isaretciler dizisinin disina tasabilir.
- Erisilebilirlik: web POST `/w?IEC104Config` → `parse_iec_config` → hata
  yollari (`json_config.c:959, 981`). Bugunku tetik yalniz gelecekteki
  duzen degisikligi.
- Oneri: tek satirlik `_Static_assert(sizeof(arr)/sizeof(arr[0]) == 17U)`.

### RSA-2 | `parse_signed`'da INT_MIN uretimi implementation-defined + UB zinciri | Unverified risk | E1

- Yer: `Application/libs/xscanf.c:147, 157`.
- Iddia: `"-2147483648"` girisi `%s32`/`%d` ile verildiginde
  `(int32_t)2147483648U` donusumu implementation-defined, ardindan
  `-(INT_MIN)` isaretli tasma olarak UB. Gomulu test paketi bu davranisi
  varsayiyor (`xscanf.c:840-842`), host testleri kapsamiyor.
- Erisilebilirlik: uretimdeki tek tuketici `gsm_engine.c` — 12 cagrinin
  tumu `%u8/%u32` (isaretsiz). **Tetik yuzeyi bugun kapali**; risk public
  API sozlesmesine bagli.
- Oneri: UBSan host harness + hedef derleme assembly kaniti (E2/E3).

### RSA-3 | Shell test komutunda `atoi` aralik disi UB'si | Improvement | E1

- Yer: `Application/libiec104/iec104_event_log.c:680` (`(uint16_t)atoi(argv[2])`);
  dogru ornek ayni dosyada: `:419-437` (`xstrtoi` + aralik kontrolu).
- Iddia: `iec104evtlog test 99999999999` girisinde `atoi` davranisi UB;
  pratikte 65535'e akar ve en fazla 65535 flash log kaydi uretir.
- Erisilebilirlik: seri konsol/web terminali shell komutu (operator yuzeyi).
- Oneri: `evtlog_parse_seq` oruntusunun ayni fonksiyona uygulanmasi.

**Taranan yuzeyler (ozet):** json_config.c (tam, 2474), rf_json.c (tam),
rf_scp_codec.c (tam), libscp/scp.c + cobs.c (tam), xscanf.c (tam),
modbus_rtu_slave.c (tam), iec104.c hedefli bolgeler, iec104_event_log.c (tam),
elog.c (tam), fault_log.c (tam). **Derin okunmayanlar:** iec104.c
94-593 ve 1500-2756, xprintf.c format motoru, modbus_process.c callback
yuzeyi, spi_flash_log.c, iec104_replay.c.

**Dogrulanan capraz kilitler (bulgu degil):** `xsnprintf` donus degeri
`len-1` ile sinirli → `pos += xsnprintf(buf+pos, size-pos, ...)` deseni
sarmaz; `rf_feeder_config_t` packed 96B ve ofsetleri `_Static_assert`'li;
SCP/COBS/Modbus sabit geometri kapali (253=7+244+2 vb.).

---

## 2. concurrency-isr-auditor (CON)

Baglam modeli: tum kullanilan IRQ'lar (EXTI15, USART1/3, UART4/5, LPUART1,
GPDMA1 Ch0/1/2, TIM17) **oncelik 0** — ISR'lar birbirini preempt edemez; tek
cakisma ekseni ISR ↔ ana dongu (Contiki kooperatif). Application'dan
`process_post` cagrisi yok; ISR'lar yalniz `process_poll` (bayrak kalibi)
kullaniyor. `critical_enter`'in no-op oldugu kaynakta teyit edildi
(`contiki-kernel/sys/int-master.c:55-65`); etkiledigi etkin yol bugun yok.

### CON-1 | Shell RX satir tamponu iki baglamdan korumasiz yaziliyor (LPUART1 ISR ↔ web terminali ana baglam) | Candidate (orta-yuksek) | E1 | SPOT-DOGRULANDI

- Yer: ISR girisi `Core/Src/stm32u3xx_it.c:538` → `Application/libs/shell.c:375-480`;
  ana baglam girisi `Application/web-server/web_shell.c:138,140` (bayt
  dongusu + `shell_process()`). Paylasilan durum: `shell.c:65-70` —
  `rx_idx` (RMW: `rx_buff[rx_idx++]`), `rx_state`, `rx_buff`, `rx_ready`,
  `rx_special_ready`, `special_chr`. **Hicbiri volatile/atomik degil, kritik
  bolge yok.** (Ayrica `web_shell` ana baglamda `shell_set_putchar` +
  `xdev_out` globallerini gevistiriyor — ISR ara kesmesi halinde cikis
  yonlendirmesi de karisabilir.)
- Iddia: iki uretici ayni satir tamponunda `rx_idx` uzerinde oku-artir-yaz
  yapar; ISR bayti, ana baglamin `rx_idx`'i yazmacda tuttugu anda gelirse
  ayni slota iki yazi (bayt kaybi) veya satir karismasi olur; `rx_ready`
  el sikismasi tek tuketimli oldugundan diger terminalin satiri dusebilir.
- Tetik: web terminalinden komut gonderilirken konsoldan bayt gelmesi — iki
  canli kullanicinin eszamanli kullanimi. Yanlis komut calisma riski, tek
  paylasilan `session_level` nedeniyle yetki acidan da anlamli.
- Onerilen E2: host harness'te iki baglam arasiyla `shell_on_rx_received`
  cagrisi; duzeltim yonu (uygulanmadi): web bayt dongusunu yalniz LPUART1
  IRQ'sunu hedefleyen kisa kritik bolgeyle serilestirmek veya web girisini
  ISR-ureticisiyle ayni tek-uretici kuyruguna yonlendirmek.

### CON-2 | Yazilim RTC cok-alanli yazimi ana baglamda kritik bolgesiz; TIM17 ISR ayni yapiyi kaskadla guncelliyor | Candidate (orta) | E1 | SPOT-DOGRULANDI

- Yer: ISR yazicisi `Core/Src/stm32u3xx_it.c:553-559` (TIM17) →
  `Application/bsp/bsp.c:64-103` (`bsp_rtc_process` kaskad); ana baglam
  yazicilari `bsp.c:105-118` (`bsp_set_rtc_milisec`, `bsp_set_rtc` — duz
  atamalar, korumasiz; SPOT ciktisinda teyit). Cagiran zincir:
  `rtc_sync` ← `gsm_engine.c:1381, 2774` (NITZ/CCLK) ve `iec104.c:745`;
  6 saatlik anti-drift `app_main.c:188`. Okuyucu `rtc_snapshot` PRIMASK'la
  korunuyor (`bsp.c:120-132`) — **yalniz yazici tarafi acik**.
- Iddia: `bsp_set_rtc`'nin coklu alan atamalari TIM17 kesmesiyle bolundugunde
  ISR kaskadi yari-guncel alanlar uzerinde kosar; atlan/cift saniye-dakika
  gecisi kalici olur. Dusuk olasilik, mimari olarak hep acik, periyodik
  resync'te tekrarlanan maruziyet.
- Etki: yazilim RTC kayar → elog damgalari, IEC104 CP56Time2a etiketleri, RF
  TIME_SYNC govdesi etkilenir.
- Onerilen duzeltim yonu (uygulanmadi): yaziciyi da `rtc_snapshot` ile ayni
  PRIMASK kalibina almak (proje politikasi §13: cok-alanli islem → kisa
  kritik bolge).

### CON-3 | power_panic_init, EXTI15 ISR yazicisiyla acilis penceresinde carpisabiliyor | Candidate (dusuk) | E1

- Yer: EXTI15 NVIC `main.c:1150-1151` icinde acilir; `power_panic_init`
  (`power_panic.c:61-69`) cok sonra `app_main.c:228`'de kosar. Okuma (satir
  65) ile bayrak/ayna yazimi (67-68) arasinda kenar gelirse ISR bayragi
  uzerine yazilir; hat LOW kalip yeni kenar gelmezse panik bolumu bir sonraki
  kenar ciftine kadar loglanmaz.
- Etki: yalniz acilis-zamani panik olayinin log/ayna kacmasi. Kayitli
  tasarimin "boot-dusuk hat loglanabilsin" amacinin dinamik kenar icin
  saglanamamasi olarak da okunur.

**Taranan yuzeyler (ozet):** tum USER CODE ISR bloklari, power_panic.c,
app_main.c, bsp.c, rtc.c, at_engine2.c + ring_buff sozlesmesi, gsm URC
cagri grafigi, rf_comm.c (tam), modbus_process.c ISR/TX-DMA yolu, shell.c,
web_shell.c, bms_reader ISR isi, xmodem.c/xmodem_process.c, digital_input.c,
contiki-kernel baglam dosyalari. **Derin okunmayanlar:** gsm_engine.c URC
bayrak icleri (cagri grafigiyle ana baglamda kostugu kanitlandi), rf/ diger
moduller (ISR erisim yuzeyi yok), BMS govdesi (bilincli kapali).

**Bilinen maddelerin guncel durumu (ajan teyidi):**
1. SRECV payload URC false-dispatch korumasi (2026-10-05 fix) **hala yerinde**
   — bolge kisiti `at_engine2.c:335-340`, arm-once `:746-754`, sifirlama
   noktalari `:274-275, 884-885, 954-955, 969-971`.
2. Guc panik sekli kayitli karara uygun (volatile-only, ISR tek yazici,
   panic_logged latch, LL ayna ISR giriste/cikista, seviye-korumali
   rise-close) — tek sapma init yazici tarafi → CON-3.
3. `critical_enter` no-op'un canli etkisi bugun sifir (Application'da bu API
   cagrisi yok; gercek dislama noktalari CMSIS PRIMASK'i dogru kullaniyor).

**Unverified riskler (aday degil):** modbus RX sahiplik el sikismasinda
1-bayt kaybi penceresi (master kural ihlali + CRC yakalar); xmodem
`pckg.timer` cift-yazici (en kotu 10 ms sapma, kendini duzeltir);
`shell_on_rx_received` ISR icinde tab-tamamlama isi (sinirli, yarais degil).

---

## 3. state-machine-auditor (STM)

Cikarilan makineler (ozet): rf_inventory (IDLE/LOADING/.../DRAINING;
DRAINING'in genel timeout'u yok — tasarim karari gibi,Requirement ambiguity
olarak kayitli), libscp/rf_comm tek-aktif-komut makinesi (kusur yok),
gsm sorgu/URC/toparlanma makinesi (kusur bulunamadi), http oturum + fw
aktarim makinesi, xmodem bayrak tabanli aktarim.

### STM-1 | BOOT kesmesinden sonra replay "envanter yuklendi" oynatiyor ama kesilen 0x06 INVENTORY_UPDATE islemi yeniden oynatilmiyor | Candidate + Requirement ambiguity | E1

- Yer: `Application/rf/rf_inventory.c:961-989` (`rf_inventory_update` —
  yalniz RAM: `pending_update`), `:788-818` (`rf_inventory_reset` islemi
  dusurur), `:492-508` (`begin_upload` replay'i = journal 0x06 **silmeleri**
  + 0x04/0x05), `:265` ("envanter yuklendi" logu),
  `Application/rf/rf_comm.c:644-666` (BOOT → reset + RESTARTED bildirimi).
- Iddia: 0x06 **silme** varyanti 1f1a0dc ile NVRAM journal'ina alindi ve
  BOOT sonrasi yeniden oynatiliyor; 0x06 **guncelleme/atama** varyanti
  yalniz RAM'de — BOOT kesintisinde duser, replay 0x04/0x05 oynatir ve
  basari logu basiar. 14.10 cihaz gozleminin ("replay 0x06 yeniden
  oynamadi") guncel kaynak karsiligi budur.
- **Dikkat — erisilebilirlik:** `rf_inventory_update`'in Application/Core
  genelinde **uretim cagirani yok** (yalniz tanim + test mock'lari). Uretim
  akisi (web → `rf_apply_save`) yalniz silme varyantini kullaniyor ve o
  yorumda korumali. Yani bugun latent bir asimetri; API kullanimia girerse
  kesilen atama sessizce kaybolur.
- Beklenen davranisin resmi gereksinim kaynagi yok → **kullanici karari
  gerekli**: update islemi de journal'a mi alinir, yoksa "RESTARTED alan
  cagiran yeniden ister" sozlesmesi mi gecerli?
- Onerilen E2: mevcut `test/rf/test_rf_error_retry_scenario.c` altyapisiyla
  event-sequence harness (ikinci oturumda 0x06 gonderilmediginin assert'i).

### STM-2 | HTTP firmware-update aktarim makinesinde timeout/abort yolu yok — sabitler ve zaman damgasi alanlari olu kod | Candidate | E1 | SPOT-DOGRULANDI

- Yer: `Application/web-server/http_handlers.c:1805-1806`
  (`FW_UPDATE_CHUNK_TIMEOUT_MS` 30 sn, `FW_UPDATE_TOTAL_TIMEOUT_MS` 10 dk —
  tanimli, HICBIR yerde referans yok; SPOT grep teyidi: yalniz 2070-2071'de
  0'lama), `:2076` (`in_progress=true`), cikis yalniz 2203/2275/2280.
- Iddia: istemci yarida kopunca `fw_state.in_progress` surece `true` kalir;
  `handle_get_fw_status` surece `{"active":true}` bildirir; resume algilamasi
  bayat oturumu ayni dosya sanip kaldigi yerden devam der.
- Tetik: admin oturumuyla `/fw_start` → birkaci `/fw_chunk` → istemci kaybolur.
- Guvenli taraf: apply yine imza dogrulamali.
- Onerilen E2: fake fw_callbacks + kontrollu tick ile toplam timeout asimi
  sonrasi `active:true` donusunun kanitlanmasi.

### STM-3 | XMODEM overflow iptalinden sonra gelen COMPLETE olayi `s_download_complete` set etmiyor — mod 10 dk askida | Candidate | E1 | SPOT-DOGRULANDI

- Yer: `Application/xmodem_process.c:117-122` (overflow'lu COMPLETE: yalniz
  log + index sifirlama; SPOT ciktisinda teyit) ile karsilastir `:124-133`
  (verisiz EOT ikisini de set eder) ve `:148-149` (normal yol).
- Iddia: boyut asiminda 4x CAN gonderilir; gonderici CAN'i gormezden gelip
  EOT ile bitirirse COMPLETE isleyici overflow dalinda kalir →
  `s_download_complete` hic true olmaz → watchdog yalniz 10 dk inactivity
  timeout'uyla modu kapatir. Bu surede mod aktif gorunur, cslog kapali
  kalir, kutuphane her sn 'C' gondermeye devam eder.
- Onerilen duzeltim yonu (uygulanmadi): overflow dalina `download_complete=
  true; success=false` — verisiz-EOT dalıyla tutarlilik (CTG-1 B fasi).

**Taranan yuzeyler:** rf_inventory.c, rf_comm.c, rf_apply.c (tam), libscp/
scp.c (tam), gsm_process.c (tam) + gsm_engine durum yuzeyleri,
http_handlers.c oturum/fw blogu, xmodem_process.c (tam), rf_events.c,
ilgili testler, git gecmisi (1f1a0dc). **Derin okunmayanlar:**
gsm_engine.c AT kurulum switch'i ve ~40 yanit parser'i (satir satir),
rf_group.c govdesi, iec104 link durum makinesi.

---

## 4. bootloader-security-auditor (BSL)

Uygulama tarafi yuzeylerde dogrulanmamis yurutme, imaj siniri hatasi, guce
kesintisiyle tuglalama veya arbitraj tutarsizligi KANITLANAMADI. Guc
kesintisi noktalari olumlu: xmodem kalici "devam ediyor" durumu yok; RFWU
flash yazimindan SONRA NVRAM ilerlemesi; IPC yalim tam indirme + baslik
dogrulamasi sonrasi yazilir; HTTP `fw_state` RAM'de → reboot sonrasi apply
gecemez; yarim IPC sektor → magic/CRC duser → yok sayilir.

### BSL-1 | Web admin oturum token'i availability-grade RNG'den uretiliyor (secure RNG degil) | Unverified risk | E1 | SPOT-DOGRULANDI

- Yer: `Application/web-server/http_session_token.h:30-52` (token 4 kelimeyi
  `bsp_random_word` ile okur), `Application/bsp/bsp_random.c:92-110`
  (donanim RNG basarisizsa xorshift32 fallback dondurur ve **basarili**
  gorunur — SPOT ciktisinda teyit: `return generate_fallback_word(value)`
  `true`), fallback tohumu `tick + tx + rx` (`:68-77`), HRNG hic
  calismadiysa sabit accumulator (`:22`). Karsi dogru desen ayni agacta:
  `raw_tcp_fw_update.c:278-291` — RFWU nonce `bsp_random_secure_word` ister,
  basarisizsa fail-closed.
- Iddia: admin oturumunu tasiyan 128-bit token, HW RNG kullanilamaz oldugunda
  dusuk entropili deterministik fallback'ten uretilebilir. A09 kararindaki
  "availability fallback ile secure random ayrimini koru" ilkesiyle uyumsuz.
  HW RNG ariza sikligi hedefte DOGRULANMADI (varsayim).
- Etki: token tahmin edilirse admin oturumu (fw upload/apply, web shell).
  Son durak BSL ECDSA'dir (kardes repo) — imaj ozgunlugu asilamaz; asil risk
  cihaz yonetiminin devrilmesi. Login kilidi sifre denemelerini sinirlar ama
  token brut-force'una takilmaz.
- Oneri: token uretimini `bsp_random_secure_word` + fail-closed (503) yapmak
  en kucuk degisiklik; RFWU deseni referans. **Karar kullaniciya ait.**

**Belgeli karar baglami (yeni bulgu degil):** web sifresi IP turetmesi
`http_handlers.c:522-538` (admin=son oktet+1, user=ilk oktet+1; 12 B buffer
yeter). W-02 teyidi: 401 statuleri, 3 hata baglanti kopma (W-01), login
kilidi, 15 dk TTL, her istekte token dogrulamasi — kapi saglam, **anahtar
malzemesi zayif** (admin ~256 aday; filo geneli user sifresi egilimi
VARSAYIM). Gelistirme anahtarlari bilincli olarak Git'te (karar).

**Boot/guncelleme akis modeli (guven kararlarinin konumlari):** BSL
(kardes repo, ECDSA + superblok) → `boot_init` `boot.c:67-80` (magic/format/
CRC32) → indirme bolmu tutarliligi `boot.c:142-160` → fw approve
`app_main.c:132-163` (BL-21 kimlik bagi `app_ipc.c:173-197`) → uc kaynak
(xmodem superuser `xmodem_process.c:404`; HTTP `/fw_*` admin
`http_server.c:285-290` + boyut tavanlari `gsm_firmware_update.c:22-44`;
RFWU HMAC-SHA256 + secure nonce `raw_tcp_fw_update.c:298-340`) → IPC
erase/write/verify `app_ipc.c:78-99` → NVRAM cift kopya + sequence arbitraj
`nvram.c:459-753` → w25qxx oku-degistir-yaz + verify + 3 deneme
`w25qxx.c:660-720`.

**Not (olu kod tuzagi, E0):** xmodem'de `XMODEM_USE_CACHE` kapali; acilisa
SPI 0x000000 (BSL superblok bolgesi) yazdigindan etkinlestirilmemeli.

---

## 5. proof-driven-bug-hunter (PBH)

### PBH-1 | Govdesiz `POST /s` (fw_start) `strstr(NULL, ...)` cagirir — HardFault/reset riski | Candidate | E1 | SPOT-DOGRULANDI

- Yer: `Application/web-server/http_handlers.c:1996` — `handle_fw_start`
  govdesinin ILK kullanimi `strstr(json_body, "\"s\"")`, NULL guard yok.
  Ayni dosyadaki 6 kardes POST isleyicinin TAMAMI `!json_body` guard'ina
  sahip (satir 497, 589, 735, 1165, 1418, 1642 — SPOT grep teyidi);
  `handle_fw_start` tek istisna.
- Kanit zinciri: `http_request_parser.c:301-309` — `body` YALNIZCA
  `content_length > 0` iken kurulur → govdesiz/`Content-Length: 0` istekte
  `body == NULL`; `http_server.c:346-347` — `/fw_start` veya `/s` rotasi
  `handle_fw_start(request->body)` cagirir. Test kapsami yok (`test/` agacinda
  `handle_fw_start` calistiran test yok).
- Tetik: admin kimlik dogrulamis istemciden TEK govdesiz POST (`curl -X POST`
  bile `Content-Length: 0` gonderir). Onem: Medium — erisim admin oturumu
  gerektiriyor, etki cokus/reset (veri bozulmasi degil), tetik trivial.
- Onerilen E2: `.temp_files/prove_PBH1_fw_start_null_body.c` — gercek
  `http_handlers.c` host derlemesi + `handle_fw_start(NULL)`; EXPECTED: 400
  reddi, ACTUAL (defo varsa): cokus. E3: cihazda `curl -X POST` + gecerli
  token, bootlog'da HardFault kaydi.

### PBH-2 | Iki `*_stats_read` modulunde NULL guard eksikligi (kardes modullerle tutarsizlik) | Improvement | E1

- Yer: `Application/modbus_bms_stats.c:141-160`,
  `Application/modbus_system_stats.c:89-96`. Tek cagiran
  `fc03_read_callback` (`modbus_process.c:417-466`) fiilen NULL gecmez →
  bug degil, sozlesme tutarlishigi iyilestirmesi.

### Kanitla curutulen supheler (rapor disi birakilanlar — negatif sonuclar degerli)

| Suphe | Curutme kaniti |
|---|---|
| `pos += xsnprintf(...)` alt tasmasi | `xprintf.c:523-525` donus `len-1` sinirli |
| `/discovery/rf`, `/config/rf` JSON tamponu (8192) tasirmasi | ~620 B / ~4 KB < 8192 |
| `RF_APPLY_SAVE_*` sonrasi eksik `rf_store_stage_abort()` | `rf_config.c:237,240` sync kendisi commit/abort ediyor |
| `rf_inventory_refresh_epoch` senkron callback yarisi | callback yalniz `finish_command` uzerinden surec baglaminda |
| `period * CLOCK_SECOND` sarmasi | 2.592e9 < 2^32; setter ust kisici `modem_config.c:268-278` |
| `/faults` JSON tasmasi | `FAULT_LOG_COUNT` 15 → ~5 KB |
| `handle_fw_chunk` `received*100` tasmasi | toplam boyut `FIRMWARE_FLASH_AREA_SIZE` sinirli |
| bms calisma-zamani kusurlari | `app_main.c:340` yorumlu → erisilemez |
| `i2c_slave.c` | bastan sona `#if 0` |
| syslogs offset/limit negatif sarmasi | ust sinir kiskiclari `http_handlers.c:831, 846-848` |
| ETag/If-None-Match ayristirma hatalari | `http_response.c:293-343` zayif dogrulayici/muhafazakar 200 dogru |
| power_board raw ofsetleri, `store_alarm` info[16] | en yuksek indeks 38/45, 15/16 → dizi ici |

Proje yasaklari (heap/errno/recursion/VLA) taranan dosyalarda ihlal
edilmedi; `utils.c:354-383` `#if 0` blogu korunmus.

**Taranan yuzeyler:** http_response.c, http_handlers.c (tam), http_server.c,
http_request_parser.c, rf_apply/rf_inventory/rf_config/rf_group_web/rf_json,
rf_faults fix komsulugu (4963524/43538a5 diffleri), time_service,
periodic_reset, stack_monitor, utils, modem_config, system_status, 5 modbus
stats, bms/, power_board/ (i2c_slave #if 0), 1f1a0dc nvram/types diffleri.

---

## 6. firmware-test-coverage-reviewer (COV)

Modul x test-seviyesi matrisi (ozet): rf/ en yogun kapsanan alan (11 host
test dosyasi + sim + HIL + real_mh); gsm cogunlukla testli; web-server
testli + integration; libiec104 genis; nvram integration'li; **elog.c,
xmodem_process.c, app_boot_log.c host testi hic yok**; gsm_periodical
derleniyor ama davranis assert'i yok; breaker.c test yok (aktif kod ince,
SBO derleme disi).

### COV-1 | XMODEM guncelleme zincirinin host regresyonu yok — TEST TASARIM ADAYI 1 | Oncelik: Yuksek
- Uretim: `Application/xmodem_process.c` + `Application/libs/xmodem.c`.
  Eksik senaryolar: chunk tasmasi → 4xCAN; verisiz EOT reddi (O9.5);
  erase/write hatasi → FAIL; gecersiz imaj basligi → `app_ipc_request_update`
  CAGRILMAMASI (BL-21); 1 dk/10 dk timeout; 4096 B tampon siniri; cslog
  kapat/geri ac + superuser kapis; overflow-COMPLETE davranisi (STM-3).
  Kacabilecek defo: kirik imaj icin bootloader'a install istegi, yanlis
  "basarili" durumu, mod kilitlenmesi. Altyapi hazir: mock_w25qxx, app_ipc
  fake'i, host Contiki wrapper'i.

### COV-2 | elog.c kalic olay gunlugu host testi hic yok — TEST TASARIM ADAYI 2 | Oncelik: Yuksek
- Uretim: `Application/elog.c` (1008 satir). Eksik: halka sarmasi + seq
  devretmesi; read_recent skip/count; clear basari/basarisizlik; yarim
  kayit toleransi; packer bayt yerlesimleri. Kacabilecek defo: denetim izi
  kaybi/bozuk sira, clear sonrasi sizma. Desen hazir:
  `test/support/iec104_event_log_fixture.h` + `test/rf/test_rf_event_log.c`.

### COV-3 | gsm_periodical.c davranis assert'i yok | Oncelik: Orta-yuksek
- Adim siralamasi, AT hata/timeout sonrasi durum, GPRS reconnect +
  `gsm_reset_process_old` karar esigi, tick sarmasi. Kacabilecek defo:
  haberlesme sessiz kalmasi / gereksiz surec reseti.

### COV-4 | app_boot_log.c acilis gunlugu okuyucu testi yok | Oncelik: Orta
- Bozuk kayit atlatma, read_last sinirlama, bos alan, en yeni N kayit.
  Kardes `app_ipc.c` testli ve olculmus; okuyucu bosta.

### COV-5 | Gercek MH'ye karsi tam yapilandirma kabul otomatik degil (APPLIED/bitmap/CRC) | Oncelik: Orta-yuksek | Seviye: Cihaz
- rf_real_mh m1-m7 gozlem temelli; kesin basari sonraki yapilandirmanin
  APPLIED + uye bitmap + cfg_CRC dogrulamasidir (mevcut degil). 14.10
  S→T gozlemi tam da bu sinif boslugun ornegi.

---

## 7. device-functional-test-designer — fonksiyonel envanter (MODE: inventory)

~90 cihaz-gozlemlenebilir islev, 10 grupta (tam liste ajan ciktisindan
rapora uyarlandi; tumu E1):

- **Shell (27 komut):** cerceve (reset/help/su/whoami/exit/info/term/ps/
  exec/kill/killall) + `breaker`, `relay`, `rf` (disc/status/inv/time/epoch/
  live/cfg-*), `rf-bridge`, `pwrboard`, `gsm at/status/log`, `boot stay/
  update/status`, `bootlog`, `rstsrc`, `elog info/dump/clear`,
  `fltlog dump/test/clear`, `iec104elog`, `iec104evtlog`, `cslog`, `log`,
  `xmodem`. Not: `test evt`/`test trip` AY'nin komutlari, modem degil.
- **Web (~30 uc nokta):** auth login/logout + rol/kilit modeli; durum
  (`/status/board`); ayarlar (device/iec104/modbus/rf + apply/abort);
  izleme (`/monitor/rf/*`, `/status/rf-group`, `/discovery/rf`, `/syslogs`,
  `/faults`); `/serial` web terminal; `/device/reboot`; fw zinciri
  (`/fw_start|/s`, `/fw_chunk|/c`, `/fw_finish|/f`, `/fw_apply`,
  `/fw_status`, `/fw_reboot|/rb`); `/echo`.
- **LED/dugme:** modem RGB1 8 mod; sebekе RGB2 6 mod; dinleyici LED2/3;
  USER_RESET_SW esikleri 3 sn reset / 10 sn fabrika / 15 sn rezerve;
  DIN1-4 + DIP1-2 debounce.
- **Modbus RTU:** 40000 hat olcum, 49000 sistem, 49200 guc karti, 49300 BMS,
  49400 GSM, 49500 RF kalite; FC06 yalniz reset komutu; tanimsiz adres 0x02.
- **IEC104:** baglanti yasam dongusu; genel sorgulama grup 3/4; saat senkronu
  C_CS; izlenen noktalar (M_ME_TF_1, M_SP_TB_1 x4); spontan + replay; C_RP;
  SBO devre disi.
- **GSM:** CSQ/RAT, web+IEC104 TCP dinleyiciler, NTP → rtc_sync, SIM/APN,
  EWDT kurtarma yolu.
- **SCP/RF:** 0x01-0x07 + 0x20-0x2A config ailesi; olay akisi 0x40→0x42→0x48;
  hub bildirimleri 0x10-0x14/0x21/0x47; guc karti 0xE1-E8.
- **Alarm/ durum:** ERR_NODT kirmizi duyuru (yukselen/dusen kenar), guc
  karti 0xE3 maskesi → ELOG, BMS durum/bitmaskeleri.
- **Kalicilik:** NVRAM v4 (cihaz/iec104/modbus/rf/breaker), SPI flash
  gunlukleri (elog, fltlog, iec104 evtlog/elog, RF olay, bootlog), RTC.
- **Baslatma/kurtarma:** boot sirasi, power panic PE15, EWDT, periyodik
  reset, reset nedeni kaydi, fw approve, 3 guncelleme kaynagi.

**Karanlik/gozlemlenemeyen yuzeyler (9):**
1. `aku_uyarisi` Modbus kaydi sabit 23 (TODO, `modbus_process.c:428-431`).
2. C_SC_NA_1 SBO komut yolu devre disi/yarim (`iec104.c:24` yorum;
   `breaker.c:179-218` IOA eslesmesi TODO).
3. BMS okuyucu baslatilmamis (bilincli karar) → 49300 canli dogrulanamaz.
4. RF dummy uretecisi inaktif (bilincli).
5. `iec104.c:837` TODO: TRIP_NOTIFY bekleyen tampon temizligi tanimsiz.
6. Iletisim kopmasi icin ayri elog kod blogu bos (60..69).
7. 15 sn dugme rezerve.
8. `OVP_PV` pin tanimi PE15 — PWR_PANIC ile ayni pin, olcu tanim.
9. SPA sayfa basliklari gzip icinde; sayfa adlari API uclarindan cikarim
   (dogrulanmamis varsayim).

**Tam checklist icin onerilen oncelik gruplari:** (1) RF olay zinciri
(0x40→0x42→0x48) + alarm duyurusu — `test evt` 137 kaydiyla izlenebilir,
`test trip` YASAK kisiti bu grupta zorunlu; (2) web auth/rol + ayar
kaliciligi; (3) baslatma/kurtarma (power panic, EWDT, periodic reset, reset
nedenleri, 3/10 sn dugme).

---

## 8. ceedling-test-generator — PLAN kipi (CTG)

MODE: plan — hicbir dosya uretilmedi, test kosulmadi. Ortam: Ceedling 1.0.1,
tek `test/project.yml`, strict-ordering CMock, `NO_CONSOLE_LOG`/`NO_SHELL_LOG`
define'lari; odakli komut `ruby -S ceedling test:<dosya>`; host CLOCK_SECOND
farki (32) nedeniyle zamanlama testleri `xmodem_poll`/tick ciftiyle surulir.

Onerilen paketler: `test/application/test_xmodem_update_scenario.c`
(CTG-1..5) ve `test/application/test_elog_ring.c` + `test_elog_packers.c`
(CTG-6..9).

| ID | Test | Kilitlenen davranis |
|---|---|---|
| CTG-1 | Boyut tasmasi: 4xCAN + sessiz dusurme (+STM-3 gerilimi, A/B fazi) | tasma aninda tam 4 CAN; sonrasi sessiz; B fasi = kullanici karari |
| CTG-2 | Verisiz EOT reddi → FAIL zinciri (O9.5) | elog START→FAIL, mod kapanir, request_update cagrilamaz |
| CTG-3 | Erase/write hatasi + gecersiz baslik → request_update YOK + basari carkasi | BL-21 risk kontrolu |
| CTG-4 | 4096 B tampon siniri + EOT kisa kuyruk | adres/uzunluk serisi bayt-bayt |
| CTG-5 | 1 dk/10 dk timeout + superuser kapis + cslog geri acma | mod kilitleme onleme |
| CTG-6 | Halka sarmasi + seq 65534→0 devretmesi | entry_id ve okuma sirasi devri kesistiginde |
| CTG-7 | read_recent skip/count + yarim kayit toleransi | sayfalama cekirdek API |
| CTG-8 | elog_clear basari/kismi hata/yeniden tarama | veri kaybi sozlesmesi |
| CTG-9 | Packer bayt yerlesimleri + uzun veri kesme | elog.h duzen belgesinin tek dogrulayicisi |

**Uygulama onerisi:** CTG-9 → CTG-6/7/8 → CTG-2/3/4/5 → CTG-1 (A fazi; B
fazi karara bagli). `project.yml` degisikligi ongorulmuyor.

---

## 9. Spot-dogrulama kayitlari (ana oturum, kanit kurali)

Asagidaki bulgular rapor hazirligi sirasinda kaynakta YENIDEN kontrol edildi
ve dogrulandi:

| Bulgu | Dogrulama |
|---|---|
| PBH-1 | `sed` 1990-2000: guard yok; kardes guard grep: 497/589/735/1165/1418/1642; parser 301-309 body kosullu; route 346-347 |
| STM-2 | sabitler yalniz 1805-1806 tanim + 2070-2071 sifirlama; baska referans yok |
| STM-3 | 117-122 overflow dali `break` — set yok; 124-133 karsit ornek |
| CON-1 | `shell.c:65-70` statics; cagriranlar: `stm32u3xx_it.c:538` + `web_shell.c:138,140` |
| CON-2 | `bsp_set_rtc` duz atamalar (105-118); TIM17 handler `bsp_tick_handler` cagrisi 553-559; okuyucu PRIMASK'li |
| BSL-1 | `bsp_random_word` fallback'ten `true` donduruyor (92-110) |
| RSA-1 | `% 17U` json_config.c:940; `line * 17U` :1029; dizi :903 |

Kalan bulgular (RSA-2, RSA-3, CON-3, STM-1, PBH-2, COV-*, envanter kalemleri)
ajan ciktisidir; dosya:satır referanslari ajanlarin okumalarindandir ve
kullanilmadan once tekrar dogrulanmalidir.

## 10. Bilinen acik maddelerin guncel durumu (capraz kontrol)

| Madde | Durum (14.10) |
|---|---|
| SCP R1 aralik uyusmazligi (json_config eski R2 araliklarinda) | **KAPANDI** — `json_config.c:1593` Ia 5.0-240.0, `:1625` di_dt 1.0-2200.0; capraz alan `:1928-1938`; codec tablo `rf_scp_codec.c:160-161` (ajan kaniti, E1) |
| SRECV URC false-dispatch fix'i | Yerinde, gerileme yok (bolge kisiti + arm-once + sifirlama noktalari teyitli) |
| Guc panik tasarimi | Kayitli karara uygun; tek sapma init penceresi → CON-3 |
| replay 0x06 yeniden oynatilmiyor (14.10 gozlemi) | Guncel kaynak karsiligi STM-1: update varyanti RAM-only, uretim cagirani yok |
| iec104 periodic_update stub | Hala stub (`iec104.c:2751-2756`) — urun karari bekliyor |
| iec104_report_fault_event cagiransiz | Hala cagiransiz (`iec104_replay.c:227`) — kaldirma/baglama karari bekliyor |
| Gercek MH otomatik testi | rf_real_mh m1-m7 var (gozlem temelli); APPLIED/bitmap/CRC kabul otomasyonu yok → COV-5 |
| `test evt`/`test trip` modem komutu degil | Teyit: AY komutlari (HW rehberi) |

## 11. Sinirlamalar

- Kanit tavani E1: hicbir build/test/harness kosturulmadi. Tum bulgular
  aday; E2/E3 kanit (host harness, cihaz testi) uygulanmadi. Bu bir tam
  kabul degil, statik tam taramadir.
- Ajan bazli dilim taramasi: her dilim kendi oncelik sirasina gore en degerli
  0-3/0-5 adayi secti (skill kurallari); "her satir tarandi" iddia EDILMEZ.
  Dilimlerin "derin okunmayan" listeleri bolimlerde kayitli.
- Satir numaralari 14.10 calisma agacina aittir.
- Hedef-donanim davranisi (HRNG ariza sikligi, TIM17 carpisma olasiligi,
  HardFault gerceklesmesi) yalniz varsayim olarak isaretlendi, kesinlestirilmedi.
- Uretim kodu, test kodu ve yapilandirma DEGISTIRILMEDI; commit yapilmadi.

## 12. Kullanici karari bekleyenler / onerilen siradaki adimlar

1. **PBH-1** (fw_start NULL govde): tek satirlik `!json_body` guard + Ceedling
   regresyonu — en yuksek getirili duzeltim adayi. Fix icin acik istek bekler.
2. **STM-3 karari**: overflow'lu COMPLETE aninda moddan ciksin mi (CTG-1 B
   fazi) yoksa mevcut davranis mi kabul edilir?
3. **STM-1 karari**: rf_inventory update islemi journal'a alinsin mi, yoksa
   "RESTARTED alan cagiran yeniden ister" sozlesmesi mi?
4. **CON-1/CON-2**: duzeltim onerileri (kisa hedefli kritik bolge / PRIMASK
   kalibi) kanitlanmis defo degil aday duzeyinde — E2 harness kararlar.
5. **BSL-1 karari**: web oturum token'i secure RNG + fail-closed olsun mu?
6. **CTG uretimi**: onaylanirsa `generate-and-verify` kipinde (oneri sirasi
   Bolum 8).
7. COV-3/COV-4 testleri ve COV-5 cihaz-duzeyi kabul otomasyonu ayri is
   kalemleri.

## 13. Değerlendirme ve düzeltme — 14.10.2026

**Sürüm:** 1.2 · **Tarih:** 2026-10-14

**Amaç:** Yukarıdaki tarihsel E1 taramasından sonra yapılan doğrulamayı,
kullanıcı kararlarını ve uygulanan düzeltmeleri kaydetmek.

| Bulgu | Güncel durum | Değişiklik ve kanıt |
|---|---|---|
| PBH-1 | Düzeltildi | Gerçek request parser ve handler ile gövdesiz POST eski kaynakta çöktü. NULL gövde artık 400 ile reddediliyor; devam eden aktarım korunuyor. |
| CON-2 | Düzeltildi | Modem RTC yazımı TIM17 IRQ'sunu dışlıyor. Takvim ve milisaniye `bsp_set_datetime` ile birlikte yükleniyor. Önceki kapalı IRQ durumu ve bekleyen tick korunuyor. Eski setter host sınırında hiç IRQ kapatmazken yeni setter bir kez kapatıyor. |
| CON-3 | Düzeltildi | Açılışta GPIO örnekleme, bayrak ve ayna yazımı EXTI15 korumasında. Önceki IRQ durumu geri yükleniyor; pending (bekleyen) kesme temizlenmiyor. GPIO okuması sonrası enjekte edilen düşen kenar eski kaynakta aynayı yanlış bırakıyor, yeni kaynakta korunup bir kez loglanıyor. |
| STM-3 | Düzeltildi | Overflow sonrası COMPLETE başarısız tamamlanma olarak işaretleniyor. Watchdog modu kapatıyor ve önceki console log durumunu geri yüklüyor; update isteği gönderilmiyor. Eski kaynakta mod açık kalıyor. |
| RSA-2 | Düzeltildi | Negatif değer `abs_val - 1` üzerinden oluşturuluyor; INT32_MIN için signed overflow oluşmuyor. Gerçek eski xscanf, GCC `-O0 -ftrapv` altında exit 3; düzeltilen kaynak exit 0. Minimum, komşu değer, maksimum ve underflow regresyonları mevcut. |
| RSA-3 | Düzeltildi | `atoi` yerine mevcut sınırlı sayısal ayrıştırma ortak helper olarak kullanılıyor. Negatif, boş, metin ve 65535 üstü count reddediliyor. Geçerli 0..65535 count ve 0..65534 dump sequence ayrımı korunuyor. |
| RSA-1 | Bakım iyileştirmesi uygulandı | IEC adres anahtarlarının 17 eleman olması `_Static_assert` ile korunuyor. Bugünkü sınır aşımı olarak sınıflandırılmıyor. |
| CON-1 | Kabul edilen risk | Kullanıcı 14.10.2026 tarihinde mevcut davranışı kabul etti; shell paylaşımı değiştirilmedi. |
| BSL-1 | Mevcut davranış korundu | Kullanıcı RNG arızasında web erişimini kaybetme riskini belirtti. Availability fallback ve login davranışı değiştirilmedi; token güvenliği sınırlaması devam ediyor. |
| STM-1 | Üretim düzeltmesi yapılmadı | Update API'sinin üretim çağıranı yok. Journal/NVRAM veya restart sözleşmesi değiştirilmedi. Bu bulgu fiziksel 0x06 gözleminin kök nedeni sayılmıyor. |
| STM-2 | Davranış kararı açık | Timeout alanları kullanılmıyor; mevcut resume davranışı korunuyor. Kaynakta duran 30 saniye/10 dakika sabitleri yeni gereksinim kabul edilmedi. |
| PBH-2 | Mevcut çağrı yolunda hata değil | NULL guard veya API sözleşmesi değişikliği yapılmadı. |

### Bootloader karşılıkları

Kardeş proje `troika-smart-breaker-modem-bootlodaer` içinde RTC setter'ları
ve `rtc_sync_soft_from_hw` düzeltildi. SysTick yazıcısı kısa PRIMASK
korumasıyla dışlanıyor; önceki maske korunuyor. Takvim ve sıfır milisaniye
tek işlemde yükleniyor. `tool/host_tests/rtc_write.py` gerçek BSP kaynağını
derliyor ve mevcut `run.py` paketine bağlı. Eski setter'ın maske kontrolü
başarısız, yeni kaynak başarılı.

Bootloader XMODEM hata yolu `rx_session_fail` üzerinden transport'u iptal
edip durumu temizliyor; STM-3 karşılığı bulunmadı. HTTP, xscanf, IEC104 ve
power-panic modüllerinin bu projede karşılığı yok.

### Doğrulama

- Başlangıçta seçili mevcut modem testleri: 125/125 PASS.
- Son merkezi komut: `ruby test/run_all.rb unit`; 1374/1374 PASS,
  0 failure/ignored. Bu değişiklikte 20 yeni Unity regresyonu eklendi.
  Son tekrar koşusunda 472 kaynak/konfigürasyon hash'i aynı kaldı;
  koşu sırasında değişen dosya yok.
- Bootloader: `python tool/host_tests/run.py --gcc
  D:/Qt/Tools/mingw1120_64/bin/gcc.exe`; `ALL HOST REGRESSIONS PASSED`.
- Her iki proje GCC 14.3.rel1 ile `make -C Release -j8 main-build`
  derlemesini geçti. Modem text/data/BSS: 329604/508/159832 B;
  bootloader: 64184/28/28832 B. Kaynak boyutu farkı tek bu düzeltmelere
  atfedilmedi; çalışma sırasında kapsam dışı `rf_comm.c` değişikliği görüldü.
- Eski kaynaklarla tekrar derlenen regresyonlar: CON-3 exit 1,
  STM-3 exit 2, RSA-3 exit 1. Bunlar beklenen hata kanıtlarıdır;
  düzeltilen kaynakların merkezi testleri geçmiştir.
- Kanıt kayıtları `.temp_files/prove_PBH1.log`, `prove_CON2.log`,
  `prove_CON3.log`, `prove_STM3.log`, `prove_RSA2.log`, `prove_RSA3.log`,
  `verify_RSA2.log`, `verify-all-unit.log`, `boot-host-all.log`,
  `modem-release.log`, `boot-release.log` içindedir. Son kaynak manifesti
  `.temp_files/final-source-manifest.json`; üretim diff'leri
  `modem-production.diff` ve `boot-production.diff` içindedir.

**Sınırlar:** IRQ/GPIO/tick testleri host sınır modeli kullanır; gerçek
kesme gecikmesi veya fiziksel güç kesintisi kabulü değildir. XMODEM testleri
gerçek application callback/watchdog kodunu çalıştırır; transport, Flash ve
reset sınırları mock'tur. Bootloader monolitik BSP'nin host derlemesinde
mevcut hedefe özel pointer-to-32-bit cast uyarıları görünür; bu kodlar
RTC testinde çalıştırılmaz. Host ve artımlı Release sonucu bütün projenin
temiz warning-free kabulü değildir. Düzeltme doğrulaması sırasında cihaz
yüklemesi/reset ve commit/push yapılmadı. Sonraki commit kaydı aşağıdadır;
push yapılmadı. COV-2..5 kapsam işleri bu düzeltmelerle kapanmış sayılmıyor.

### Bakım değerlendirmesi

Düzeltmeler mevcut callback, watchdog, GPIO ve sayısal ayrıştırma yollarını
kullanıyor. Yeni kalıcı veri, heap, bağımlılık, retry veya process eklenmedi.
Birlikte RTC yükleme girişi ve EXTI15'e özel iki BSP IRQ fonksiyonu,
paylaşımın donanım erişimini BSP içinde tutmak için eklendi. NVRAM,
protokol, linker ve imza doğrulama davranışı değiştirilmedi.

### Commit kaydı — 14.10.2026

Kullanıcının açık commit isteği üzerine düzeltmeler anlamlı gruplar halinde
kaydedildi. Kod ve ilgili regresyon testleri aynı commit içinde tutuldu.

| Repo | Commit | Kapsam |
|---|---|---|
| Modem | `c882316` | RTC/power-panic IRQ koruması ve regresyonları |
| Modem | `53a2a65` | Gövdesiz firmware isteği, IEC alan eşlemesi ve HTTP regresyonu |
| Modem | `111120f` | INT32_MIN ve event-log count ayrıştırması, sınır regresyonları |
| Modem | `be67b4c` | XMODEM overflow sonrası kapanış ve watchdog senaryoları |
| Bootloader | `9befcdf` | RTC yazımı, merkezi host regresyonu ve doğrulama dokümanı |

Bu rapor ve modem test rehberi ayrı dokümantasyon grubudur. Önceden mevcut
IDE ayarları, `.github/skills/`, diğer raporlar ve `.temp_files/` kanıt
dosyaları bu commitlere alınmadı. Üretim raporu mevcut `.gitignore`
kuralıyla yerel kalıyor; düzeltme ve commit kayıtları orada da güncellendi.

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-10-14 | 1.1 | §13: kullanıcı kararları, yedi modem düzeltmesi, bootloader RTC karşılığı ve yürütülen kanıtlar |
| 2026-10-14 | 1.2 | §13: anlamlı commit grupları ve yerel kalan kayıtların kapsamı |

/*** end of file ***/
