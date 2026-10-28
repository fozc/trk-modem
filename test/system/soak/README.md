# Soak Harness — Birleşik Uzun Koşu + Fonksiyon Doğrulama + Anomali/kanıt

Amaç: modemın tüm yüzeylerini (konsol, web, Modbus RTU, IEC104/SCADA,
RF/MH, güç kartı, AY) tek harness altında **periyodik + rastgele**
çalıştırmak; yalnızca "canlı mı" bakmadan **çıktıların doğruluğunu**
denetlemek (SCADA'ya doğru formatta periyodik/spontane veri, Modbus
harita tutarlılığı, çapraz yüzey tutarlılığı, uçtan uca olay zinciri);
tüm kayıtları rotasyonlu tutmak ve beklenmedik durum anında kanıt paketi
toplamak. Sonradan insan/ajan `anomalies.jsonl` + `evidence/` +
`summary.md` üzerinden soruşturur.

Faz durumu:

- **Faz 1 (hazır)**: supervisor + konsol yakalama/rotasyon + hafif
  web/Modbus/IEC104 probları + anomali işaretleri + kanıt pili.
- **Faz 2 (hazır)**: c104 sürekli master (`--iec104-mode master|auto`):
  her spontane/periyodik APDU'nun tip/IOA/CA/OA/CP56 doğrulaması
  (süit `cases_periodic.validate_frame` kurallarıyla), periyodik yayın
  kadansı ve saat sapması denetimi, zamanlanmış GI + saat senkronu
  turları, tam APDU kanıtı (`iec104-apdu.jsonl`); Modbus sabit harita
  turu (`modbus_map_check`: blok sabitleri `Application/modbus_map.h`'den
  okunur, yapisal + role/fider çapraz yüzey kontrolleri); web endpoint
  şema denetimi (kanitli alan kümeleri).
- **Faz 3 (hazır)**: AY `test evt` stimülasyonu (bütçe + `test trip`
  hard-guard), uçtan uca olay zinciri doğrulayıcı (AY → /faults →
  fltlog → IEC104 spontane → Modbus sayaç; halka halka süre ölçümü,
  kopuk halka = `chain_break`), RF sağlık geciş probu, `summarize.py`.

## Kullanım

```text
# bench, üretim imajı, 24 saat, her şey açık:
python test/system/soak/run_soak.py auto --console COM18 --rs485 COM10 --hours 24

# AY zincirleriyle (portlar bench düzenine göre):
python test/system/soak/run_soak.py auto --ay COM15,COM16 --ay-mapping "{\"COM15\":1}" --hours 24

# kısa kabul penceresi (integration sarmalayıcı üzerinden):
SOAK_DEVICE=1 SOAK_HOURS=1 make -C test/integration/soak run

# cihazsız self-test (merkezi koşuda her zaman önce bu çalışar):
python test/system/soak/selftest.py

# koşu özeti (kapanışta otomatik de yazılır):
python test/system/soak/summarize.py test/build/soak/<timestamp>
```

Varsayılanlar `doc/HW_TEST_VE_DEBUG_REHBERI.md` v1.3 düzenidir: konsol
COM18, RS-485 COM10. AY **varsayılan kapalıdır** (`--ay` ile açık verilir).
Host `auto` ise `gsm status` çıktısından IP çözülür. IEC104 kipi `auto`:
c104 kuruluysa (gerektiğinde `test/system/iec104_master/.venv` python'ı
ile kendini yeniden başlatır) sürekli master, değilse Faz 1 TESTFR
probu. Tüm parametreler için `--help`.

## Çıktı düzeni (`test/build/soak/<timestamp>/`)

| Dosya | İçerik |
|---|---|
| `manifest.json` | git kısa hash + dirty sayısı, sürümler, portlar, tohum, kip, `rf status` ham çıktısı |
| `serial.log` (+`.1`..`.5`) | ham konsol akışı, 10 MB × 5 rotasyonlu |
| `ay/<port>.log` | AY konsolları ham akış (AY açıkken) |
| `commands.jsonl` | harness'in çalıştırdığı her konsol komut + çıktısı |
| `web.jsonl` | her HTTP işlemi: endpoint, durum, ms, sha16, JSON/şema sonucu; TCP drop eylemleri |
| `modbus.jsonl` | her FC03 işlemi + tam harita turları (çözümlü fider verileri) |
| `iec104.jsonl` | TESTFR ölçümleri / periyodik çevrim, GI, saat senkron, fault_event kayıtları |
| `iec104-apdu.jsonl` | master kipinde tüm APDU'lar ham+çözümlü (JsonlEvidence) |
| `rf.jsonl` | fider başına RF durum anlıkleri (Online/FsmError/...) |
| `chains.jsonl` | her olay zinciri turu: halka halka ok + süre |
| `health.jsonl` | periyodik konsol sağlık turu + `stats history` ham blokları |
| `health_history.csv` | 15 dk'lık sayaç satırları (trend) |
| `anomalies.jsonl` | tüm bulgular: sınıf, şiddet, kaynak, detay |
| `evidence/<seq>-<sınıf>/` | anomali anı kanıt paketi |
| `metrics.json` | atomik sayaçlar + görev başına tur/hata + `state` |
| `summary.md` | kapanışta otomatik özet (anomali tablosu, zincir halkaları, kadans) |

Kanıt paketi: `console.txt` (komut pili: `rstsrc`, `elog dump 20`,
`bootlog dump`, `stats`, `rf status`, `pwrboard show`, `gsm status`,
`ps`, `info`), `recent_console.txt`, `metrics.json` kopyası, ilgili
JSONL'lerin son 60 satırı.

## Anomali sınıfları

Konsol işaretleri (kaynak: `gsm_soak.py`/`gsm_disconnect_test.py` yasak
listeleri, `power_panic.c`, `app_main.c`): `hardfault`, `gsm_restart`,
`ewdt_wait`, `power_panic`, `hardfault_previous_run`,
`send_data_timeout`, `gsm_fixture_image`.

elog (`/syslogs` artımlı): `elog_hardfault`, `elog_reset_abnormal`,
`elog_uart_recovery`, `elog_nvram_recovered`, `elog_time_lost`.

Türetilmiş: `boot_detected`, `web_failures`, `web_bad_json`,
`web_schema`, `modbus_failures`, `iec104_failures`, `harness_error`
(50 ardışık → düzgün durdurma).

Faz 2 fonksiyonel: `iec104_format` (tip/IOA/CA/CP56 ihlali),
`iec104_periodic_stalled`, `iec104_time_skew`, `iec104_stack_restart`
(bağlıyken M_EI), `iec104_gi_failed`/`iec104_gi_mismatch`/
`iec104_gi_missing`/`iec104_gi_extra`, `iec104_clocksync_failed`,
`iec104_master_disconnected`/`iec104_master_down`, `iec104_unexpected`,
`modbus_map_quality`, `modbus_map_value`, `modbus_block_read`,
`modbus_web_relay_mismatch`.

Faz 3: `chain_break` (olay bir tüketici yüzeyine ulaşmadı),
`rf_offline`, `rf_fsm_error`, `rf_uptime_stalled`, `rf_trip_alarm`,
`rf_store_degraded`, `modbus_rf_state_mismatch`, `ay_stim_disabled`.

## Güvenlik sınırları

- Profil **gözlem + pasif kaos**: config yazması yok, FW işlemi yok,
  `elog clear`/`fltlog clear` yok, cihaz reboot yok (`GET /device/reboot`
  poll setine bilinçli girmez).
- TCP drop kaosu yeni bağlantı üzerinden, ortalama 1/10 dk.
- AY: yalnız `test evt`/`test evt sim`; `test trip` komut onay
  katmanıyla yasak; AY başına günlük olay bütçesi (varsayılan 20) ve
  min 180 s bekleme; RED/dergi reddinde o AY durur.
- Modbus: yalnız FC03; 50001 modem-reset yazmacı okunur, yazılmaz.
- Yaz-geri-al mutasyonları ve ağır kaos (FW kesinti, reboot) ayrı onayla
  gelecek profillerdir.

## Canlı kabulde doğrulanacak varsayımlar

1. `/syslogs?offset=N` → "ilk N kaydı atla" yorumu (kayıtlar
   `offset`/`t` alanlarını taşır; ilerlemezse görünür).
2. `stats` çıktısındaki `boots` sayacı regex'i (ham metin `health.jsonl`'de).
3. AY portlarının gerçek düzeni (`--ay` açık verilirken teyit).
4. `/monitor/rf/N` JSON anahtarları (L1/L2/L3 + Online...): uymazsa
   RF geçiş denetimleri sessizce boş kalır (yanlış pozitif üretmez).
5. Konsol `relay status` çıktısı iddialı parse edilmez; ham metin
   `modbus.jsonl` map_check kaydında saklanır.

## Yeniden kullanılan bileşenler

`tools/hwtest/serial_io.py` (konsol oturumu), `test/system/iec104_master/`
(`oracle.py` WebDevice + parola türetme, `master104.py`, `apdu.py`,
`cases*` doğrulama kuralları, `evidence.py`), `test/integration/
modbus_device/run_tests.py` (RTU istemcisi), `gsm_disconnect_test.py`
(IecProbe, TCP drop), `console_io.py` (`parse_gsm_ip`),
`Application/modbus_map.h` (harita sabitleri, çalışma anında okunur).
Yeni yazılan yalnız birleştirici katmandır.
