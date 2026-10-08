# RF HIL Test Rehberi — Tekrarlanabilir Prosedür

| | |
|---|---|
| Belge | RF HIL test altyapısının kullanım, ön koşul ve tekrar koşma rehberi |
| Sürüm | 1.1 |
| Tarih | 08.10.2026 |
| Amaç | Bu belgeyi takip eden bir mühendis, aynı HIL testlerini herhangi bir zamanda tekrar koşabilir |
| Kullanım yeri | Bench kurulumu, test koşumu, doğrulama ve sorun giderme |
| Dayanak plan | `RF_HUB_SIM_HIL_TEST_PLANI_2026-10-06.md` v1.2 |
| Dayanak yanıt | `doc/BOLATeX_Yanit_RF-SCP_Sorulari_R0.md` (BQ-01…BQ-11) |

---

## 1. Bileşenler ve dosya yerleşimi

| Bileşen | Dizin | İşlev |
|---|---|---|
| Simülatör (MH rolü) | `tools/rf-hil/` | Python 3 + pyserial; Teslim4 R1 SCP arayüzünü oynatır |
| Harness | `test/system/rf_hil/` | Vaka kaydı, konsol sürücüsü, doğrulama, rapor |
| Donanımsız self-test | `test/system/rf_hil/test_sim_selftest.py` | Codec golden, model, fault motoru (port açmaz; 62 test) |
| İmaj kimliği | `test/system/rf_hil/identity.py` | EFW parse + DUT `RTU image:` satırı karşılaştırması |
| DUT yapılandırma | `test/system/rf_hil/configure_dut.py` | Web üzerinden RF store fider/EUI ayarı |
| Merkezi kapı | `test/integration/rf_hil/` | run_all.rb'ye bağlı integration paketi |
| Senaryo dosyaları | `tools/rf-hil/scenarios/` | Hazır JSON zaman çizelgeleri |

### Simülatör modülleri (`tools/rf-hil/sim/`)

| Dosya | İşlev |
|---|---|
| `scp_codec.py` | COBS + CRC-16/CCITT-FALSE + libscp eşdeğeri ayrıştırıcı |
| `cp56.py` | CP56Time2a yardımcıları |
| `hub_model.py` | MH davranış modeli (tüm komutlar, halka-99, grup, PWRB) |
| `faults.py` | Hat seviyesi hata enjeksiyonu (gecikme/düşürme/bozma/bölme/metin) |
| `scenario.py` | JSON zaman çizelgesi yorumlayıcı |
| `control.py` | TCP kontrol kanalı + eylem dağıtıcısı |
| `trace.py` | JSONL iz (oracle kaynağı) |

### Harness dosyaları (`test/system/rf_hil/`)

| Dosya | İşlev |
|---|---|
| `run_hil.py` | Orkestratör; imaj kimlik doğrulaması dahil |
| `cases.py` | Açılış/canlı/olay/grup/epoch/PWRB/hat vakaları (grup A-I) |
| `cases_v12.py` | v1.2 plan eklemeleri: e2/e3/g3/g4/d6/h2/h7 |
| `cases_bq.py` | BOLATeX R0 yanıtı: BQ-01 alarm, BQ-11 gid=0, h5, c4, b1 |
| `checks.py` | İz + konsol doğrulama kütüphanesi |
| `console.py` | Etkileşimli konsol adaptörü (serial_io.py ConsoleSession) |
| `identity.py` | EFW artifact parse + DUT imaj kimlik karşılaştırma |
| `test_hil_identity.py` | identity modülünün donanımsız testi |
| `configure_dut.py` | Web üzerinden DUT RF store yapılandırma |

---

## 2. Bench ön koşulları

### 2.1 Donanım bağlantısı

**UART4/RS-485 HIL profili** (`RF_SCP_OVER_MODBUS_PORT=1` derlemesi):

```
COM10 (USB-RS485)  <->  Modbus konnektörü (UART4)
COM16              <->  Konsol (LPUART1, 230400 8N1)
ST-Link            <->  SWD (sadece xmodem firmware yükleme için)
```

**Üretim profili** (`RF_SCP_OVER_MODBUS_PORT=0`): SCP, USART3 (PC4/PC5)
üzerinden gerçek MH'ye gider; HIL süiti bu profilde koşulmaz.

**Uyarılar:**
- Gerçek MH takılıysa HIL profili kullanılamaz (iki sürücü çarpışır).
- USB-TTL 3.3 V gereklidir; RS-485 çevirici doğrudan USART3'e bağlanmaz.

### 2.2 Firmware derleme ve yükleme

```bash
# Toolchain PATH
export PATH="/c/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:$PATH"

# HIL profili: Application/rf/rf_hil_transport.h içinde 1
# Üretim profili: 0
make -C Release -j8 all

# Yükleme (imzalı .efw, xmodem yolu — asla doğrudan ST-Link yazma)
printf 'su admin\nxmodem start\n' | python tools/hwtest/serial_io.py \
    capture COM16 230400 6 /tmp/xm0.log
python tools/hwtest/xmodem_send.py COM16 230400 \
    Release/<paket>.efw /tmp/xm1.log
```

### 2.3 DUT RF store yapılandırması (bir kez)

```bash
python test/system/rf_hil/configure_dut.py --host <cihaz_ip> --feeders 1,2 --zone 1
# Şifre IP'den türetilir: admin<son oktet+1>
```

### 2.4 İmaj kimlik doğrulaması

Süit, `rf status` çıktısındaki `RTU image: crc=... size=... git=... profile=...`
satırını, seçilen `.efw` dosyasının içeriğiyle karşılaştırır. Bu yüzden
`--firmware-image` argümanı **yüklenen dosyanın birebir aynısı** olmalıdır.

---

## 3. Test koşumu

### 3.1 Donanımsız self-test (port açmaz, her zaman koşulabilir)

```bash
python test/system/rf_hil/test_sim_selftest.py
# Beklenen: 62/62 OK
```

### 3.2 Merkezi host koşusu (Ceedling + integration)

```bash
cd test
ruby run_all.rb
# rf_hil integration paketi dahil; beklenen: tüm paketler PASS
```

### 3.3 HIL süiti (gerçek cihaz)

```bash
# Tam süit (32 vaka; soak vakaları DEFERRED döner)
python test/system/rf_hil/run_hil.py \
    --rf-port COM10 \
    --console COM16 \
    --firmware-image Release/<paket>.efw \
    --transport-profile 1

# Tek vaka
python test/system/rf_hil/run_hil.py --rf-port COM10 --console COM16 \
    --firmware-image ... --transport-profile 1 --case a1_boot_from_start

# Vaka listesi
python test/system/rf_hil/run_hil.py --list
```

**Argümanlar:**

| Arg | Zorunlu | Açıklama |
|---|---|---|
| `--rf-port` | Evet | Simülatörün konuşacağı COM portu |
| `--console` | Evet | DUT konsolu (COM16) |
| `--firmware-image` | Evet | Yüklenen .efw dosyası (kimlik doğrulama) |
| `--transport-profile` | Evet | 0=üretim(USART3) / 1=HIL(UART4) |
| `--case` | Hayır | Belirli vakalar (tekrarlanabilir) |

**Çıkış kodları:** 0=tüm PASS, 1=FAIL var, 2=ERROR/BLOCKED (altyapı).

### 3.4 Günlük sanal hub (test dışı)

```bash
python tools/rf-hil/rf_hub_sim.py COM10

# Canlı müdahale (başka terminalde)
python tools/rf-hil/sim_cmd.py 7788 status
python tools/rf-hil/sim_cmd.py 7788 inject_trip line=1 phase=2
python tools/rf-hil/sim_cmd.py 7788 add_events count=3 code=1 line=1
```

---

## 4. Vaka kataloğu

### Grup A — Açılış

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| a1_boot_from_start | BOOT→TIME_SYNC→BQ-03 koruması→envanter→END | 40 s |
| a2_hub_late | Geç hub, liveness timeout sonrası toparlanma | 45 s |
| a4_hub_reboot | Hub reseti: üçlü bildirim + yeniden yükleme | 70 s |

### Grup C — Canlı veri

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| c1_live_values | Canlı veri akışı + değerler | 25 s |
| c2_live_timeout | 30 s kesinti → offline | 55 s |
| c4_trip_failed_latch | Trip_Failed bayrak set/temizle döngüsü | 30 s |

### Grup D — Olay çekme

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| d1_trip_and_event_pull | TRIP + 0x47→0x40→0x44→0x46→left=0 | 35 s |
| d3_busy_retry_new_seq | ERROR 0x03'te yeni SEQ ile tekrar | 35 s |
| d4_bad_slot_skip | ERROR 0x06: atla + imleç ötesi | 35 s |
| d6_bad_record_crc | İç-CRC bozuk: prefix koruması | 35 s |

### Grup E — Konfig grubu

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| e1_cfg_apply_happy | 3×0x22+0x24 → APPLIED + cfg_crc | 30 s |
| e2_cfg_crc_mismatch | Kasıtlı CRC bozma → MISMATCH | 45 s |
| e3_cfg_abort | STAGED sonrası abort → FAILED 10 | 30 s |
| e4_drop_first_write | Düşen 0x22 → aynı SEQ tekrar | 40 s |
| e6_cfg_not_live | Canlılık yok → FAILED 1 | 30 s |

### Grup F — Epoch

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| f1_epoch_refresh | 0x2A ACK + pencere içi 0x03 | 20 s |

### Grup G — PWRB

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| g1_pwr_summary | 0xE1 özetleri + pwrboard show | 30 s |
| g2_pwr_alarm_edges | 0xE3 kenarları + maske | 40 s |
| g3_pwr_cfg2_flow | E5 GET→SET→yankı doğrulama | 60 s |
| g4_pwr_command | Kapasite+battery-replaced (E5+E6+E7) | 75 s |

### Grup H — Hat seviyesi

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| h1_corrupt_crc_reply | Bozuk CRC → sessiz atma + tekrar | 30 s |
| h2_split_short_gap | <100 ms bölünme → parse | 30 s |
| h3_split_frame_gap | >100 ms bölünme → atma + tekrar | 30 s |
| h4_text_noise | Çerçeve arası metin → yok sayma | 30 s |
| h5_late_reply | 700 ms gecikme → timeout + tekrar | 30 s |
| h6_unknown_proactive | Bilinmeyen CMD → sessiz | 20 s |
| h7_vendor_band_notify | 0xF5 → yok sayma | 20 s |

### Grup I — Kesinti

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| i1_silent_hub_recovery | Hub sessizliği + toparlanma | 65 s |

### Grup BQ — BOLATeX yanıtı

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| bq01_alarm_from_101 | BQ-01: 101 kaydı alarm açar; fault listesine girmez | 40 s |
| bq01_alarm_from_105 | BQ-01: 105 kaydı alarm açar (farklı fider) | 40 s |
| bq11_gid_zero_reject | BQ-11: group_id=0 → 0x22 gönderilmez | 15 s |

### Grup B — Keşif

| Vaka | Doğrulanan | Süre ~ |
|---|---|---|
| b1_discovery | 0x14 → rf disc kuyruğunda görünür | 25 s |

### Soak (ertelenmiş; ayrı koşum gerektirir)

| Vaka | Açıklama |
|---|---|
| d2_lost_bell_60s_poll | 0x47 kaybı → 60 s periyodik 0x40 |
| d5_wrap_during_read | Okuma sırasında halka sarması |

**Toplam: 32 koşulan + 4 soak/ertelenmiş = 36 kayıtlı vaka; koşulan takım ~20 dk.**

---

## 5. Rapor ve kanıt

Her koşu `test/build/hil-rf/<zaman_damgasi>/` altına üretir:

| Dosya | İçerik |
|---|---|
| `report.md` | Vaka bazında PASS/FAIL/ERROR/BLOCKED/DEFERRED + kanıt satırları |
| `manifest.json` | İmaj kimliği (CRC/git/profile), git commit, portlar, CSV hash'leri, snapshot |
| `<vaka>/sim_trace.jsonl` | Simülatör izi: her TX/RX çerçeve (cmd, seq, data hex, zaman) |
| `<vaka>/console.log` | DUT konsol çıktısı (ham) |
| `<vaka>/snapshots.json` | Vaka öncesi/sonrası simülatör durumu |

---

## 6. Simülatör ayrı kullanım

### 6.1 CLI

```bash
python tools/rf-hil/rf_hub_sim.py COM10
# Seçenekler: --baud 230400, --noboot, --scenario <json>,
#             --control 127.0.0.1:7788, --trace <jsonl>, --quiet
```

### 6.2 Kontrol eylemleri (TCP JSON-lines)

```bash
python tools/rf-hil/sim_cmd.py 7788 status
python tools/rf-hil/sim_cmd.py 7788 boot
python tools/rf-hil/sim_cmd.py 7788 reboot
python tools/rf-hil/sim_cmd.py 7788 inject_trip line=1 phase=2
python tools/rf-hil/sim_cmd.py 7788 inject_discovery line=9 phase=1
python tools/rf-hil/sim_cmd.py 7788 add_events count=3 code=1 line=1
python tools/rf-hil/sim_cmd.py 7788 log_bell
python tools/rf-hil/sim_cmd.py 7788 pwr_summary
python tools/rf-hil/sim_cmd.py 7788 pwr_alarm kod=4 level=true
python tools/rf-hil/sim_cmd.py 7788 fault action=drop_next count=1
python tools/rf-hil/sim_cmd.py 7788 fault action=corrupt_next kind=crc
python tools/rf-hil/sim_cmd.py 7788 fault action=split_next gap_ms=150
python tools/rf-hil/sim_cmd.py 7788 fault action=noise_before text="test"
python tools/rf-hil/sim_cmd.py 7788 set_knob name=live_period_s value=5
python tools/rf-hil/sim_cmd.py 7788 set_telemetry value='{"cap_ah": 30}'
python tools/rf-hil/sim_cmd.py 7788 mark_bad_slot index=0
python tools/rf-hil/sim_cmd.py 7788 corrupt_record_slot index=1
python tools/rf-hil/sim_cmd.py 7788 set_degraded value=true
python tools/rf-hil/sim_cmd.py 7788 busy_for ms=8000
python tools/rf-hil/sim_cmd.py 7788 preload_inventory lines='[1,2]' zone=1
python tools/rf-hil/sim_cmd.py 7788 raw_notify cmd=245 data_hex=0102
```

### 6.3 Senaryo JSON biçimi

```json
{
  "duration_s": 60,
  "setup": [
    {"do": "set_knob", "name": "live_period_s", "value": 5},
    {"do": "set_telemetry", "value": {"cap_ah": 30}}
  ],
  "steps": [
    {"at_s": 1.0, "do": "boot"},
    {"at_s": 20.0, "do": "inject_trip", "line": 1, "phase": 2},
    {"at_s": 25.0, "do": "add_events", "count": 3, "code": 1, "line": 1}
  ]
}
```

---

## 7. Web gerçek-veri yolları (canlı cihaz)

Cihaz şebekedeyken (`gsm status` → IP), admin oturumu ile:

```bash
python test/system/rf_hil/configure_dut.py --host <ip> --feeders 1,2
# (login + POST /config/rf dahil)
```

| Uç nokta | Yöntem | Doğrulanan |
|---|---|---|
| `/status/board` | GET | PWRB→MH→SCP→web güç kartı değerleri |
| `/monitor/rf/` | GET | RF faz verileri (HasData) |
| `/config/rf` | GET/POST | Store oku + Kaydet round-trip |
| `/config/rf/apply/<l>/<gid>` | POST | Uygula (grup kimliği 0 reddedilir) |
| `/config/rf/abort` | POST | Aktif grubu iptal |
| `/status/rf-group` | GET | Grup durumu JSON |
| `/discovery/rf` | GET | Keşif kuyruğu |
| `/faults?feeder=N` | GET | Arıza listesi (feeder zorunlu) |

---

## 8. Sorun giderme

| Belirti | Neden | Çözüm |
|---|---|---|
| `BLOCKED: control channel did not come up` | COM portu başka işlemde | Python süreçlerini kapat; COM10'u boşalt |
| `BLOCKED: IdentityError(...)` | .efw dosyası cihazdakinden farklı | Derle → yükle → aynı .efw'yi `--firmware-image`'a ver |
| Tüm vakalar BLOCKED, COM10 yok | Adaptör takılı değil | Aygıt Yöneticisi → Ports; USB'yi sök-tak |
| a2/geç hub düşer | Başlatma gecikmesi BOOT'u kaçırıyor | Vaka zaman penceresini genişlet (cases.py) |
| d4 düşer | BQ-03 boot-protect no-op 0x46[0] filtreye takılıyor | `pull_consumes` filtresini kullan (cases.py d4) |
| g3/g4 düşer | Kabuk gecikmesi | Deadline'ları 25 s yap; retry ekle |
| Üretim profilde HIL koşulmaz | USART3'e MH takılı | MH'yi çıkar veya HIL profiline geç |
| Profil uyuşmazlığı | Cihazda profile=0, süit 1 istiyor | HIL derle + yeniden yükle |
| Konsolda RF çerçevesi yok | NVRAM'de cslog master kapalı | `cslog on` gönder (harness otomatik yapar); NVRAM değişince sıfırlanabilir |
| `Envanter: BOS` | RF store boş (NVRAM sıfırlanmış) | `configure_dut.py` ile yeniden doldur |

---

## 9. Hızlı başlangıç kontrol listesi

```
□ pyserial kurulu: pip install pyserial
□ COM10 bağlı (USB-RS485) ve boş
□ COM16 bağlı (konsol) ve boş
□ HIL firmware yüklü (profile=1, xmodem yoluyla)
□ RF store yapılandırılmış (configure_dut.py)
□ Self-test geçiyor: test_sim_selftest.py → 62/62
□ Süit koş: run_hil.py --rf-port COM10 --console COM16 \
    --firmware-image <efw> --transport-profile 1
□ Rapor kontrol: test/build/hil-rf/<ts>/report.md → 32/32 PASS
□ Üretim imajına dön: profile=0 derle + xmodem
```

---

## Değişiklik geçmişi

| Tarih | Açıklama |
|---|---|
| 07.10.2026 | 1.0 — İlk sürüm: 26 vakalı HIL süiti |
| 08.10.2026 | 1.1 — BQ vakaları (bq01_alarm_101/105, bq11_gid_zero), h5/c4/b1 eklendi; self-test 62; kimlik doğrulama; cslog NVRAM anahtarı; RF store yeniden yapılandırma notu; 32/32 PASS |
