# RF Hub (MH) Simulatoru - tools/rf-hil

## HIL koşusunda imaj ve profil kapısı

Runner, kaynak header varsayılanını DUT profili olarak kullanmaz.
`--firmware-image` kurulu uygulamanın EFW dosyasını, `--transport-profile`
beklenen derleme profilini belirtmelidir (0=USART3, 1=UART4/RS-485 bench).
Güncel firmware'in `rf status` çıktısı kurulu boot metadata ve gerçek
derleme profilini bildirir. Koşu öncesi/sonrası eşleşme yoksa BLOCKED
olur. Bu komut firmware yüklemez:

```text
python test/system/rf_hil/run_hil.py --rf-port COM10 --console COM16 --firmware-image <kurulu_imaj.efw> --transport-profile 1
```

Seçilmiş ertelenmiş vaka, eksik kimlik veya boş seçim başarıyla çıkmamalıdır.
Eski koşuların metadata'sı geriye dönük değiştirilmemelidir.

BOLATeX Teslim4 R1 SCP arayuzunun (`doc/BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/`)
RF hub (MH) tarafini bir PC uzerinde birebir simule eden Python araci.
Modemin (DUT) USART3 RF hattiyla gercek MH varmis gibi konusur; tum
isteklere spec'e gore yanit verir, proaktif bildirim uretir, hat seviyesi
ve davranis seviyesi hata enjeksiyonu yapar.

## Kablolama

**Bench profili (profile=1, onerilen):** SCP UART4/RS-485'e yonelir;
COM10 (USB-RS485) Modbus konnektorune takilir. Gercek MH ayni anda
takili kalabilir — aktif RF-SCP yolunu yuklenen imajin profili
belirler: profile-1 firmware USART3 RF dagitimini derlemeden cikarir
(gercek hub yok sayilir), profile-0'da SCP USART3'ten gercek MH'ye
gider. Suitsu degistirmek kablolamayi degil, imaji degistirmeyi gerektirir.

**Eki yontem (USART3'e dogrudan erisim):** Modemin RF hatti
(USART3, 230400 8N1) bir **USB-TTL 3.3 V** ceviriciye baglanir
(RS-485 cevirici dogrudan USART3'e baglanmaz):

```
STM32_USART3_TX (PC4)  ->  cevirici RX
STM32_USART3_RX (PC5)  <-  cevirici TX
GND                    <-> cevirici GND
```

## Kullanim

Gunluk "sanal hub" modu (acilis/web testleri icin surekli acik birakilir):

```
python tools/rf-hil/rf_hub_sim.py COM10
```

Senaryolu modu (HIL harness ya da elle):

```
python tools/rf-hil/rf_hub_sim.py COM10 --scenario scenarios/normal.json \
    --control 127.0.0.1:7788 --trace /tmp/trace.jsonl
```

Kontrol kanali (canli mudahale):

```
python tools/rf-hil/sim_cmd.py 7788 status
python tools/rf-hil/sim_cmd.py 7788 inject_trip line=1 phase=2
python tools/rf-hil/sim_cmd.py 7788 add_events count=3 code=1 line=1
python tools/rf-hil/sim_cmd.py 7788 fault action=drop_next count=1
python tools/rf-hil/sim_cmd.py 7788 reboot
```

Tum eylemler: `sim/control.py` icindeki `dispatch_action` (status, boot,
reboot, ping, add_events, inject_trip, inject_discovery, inject_anomaly,
log_bell, pwr_summary, pwr_alarm, son_nefes, set_knob, set_telemetry,
fault, faults_clear, mark_bad_slot, clear_bad_slots, set_degraded,
busy_for, preload_inventory, live_value).

Alarm/dizi testlerinde `add_events boot_counter=<0..65535>` ile AY
acilis kimligi secilebilir; varsayilan 7'dir. Bir MH simulator surecinin
yeniden baslamasi, ayni AY'nin uptime'inin sifirlandigini kanitlamaz.
Ayri AY acilislarini test ederken bu kimlik acikca degistirilmelidir.

`set_knob name=log_bell_enabled value=false`, 0x47 bildirimlerini
susturur; olaylari halkadan silmez. Periyodik HEAD okumasi bu kayitlari
almaya devam edebilir. Merkezi host testleri bu ayrimi dogrular.

## Bilesenler

| Dosya | Islev |
|---|---|
| `rf_hub_sim.py` | CLI; seri port + parser + tick dongusu |
| `sim/scp_codec.py` | COBS + CRC-16/CCITT-FALSE + libscp-esbdesi parser |
| `sim/cp56.py` | CP56Time2a yardimcilari |
| `sim/hub_model.py` | MH davranis modeli (tum komutlar, halka, grup, PWRB) |
| `sim/faults.py` | Hat seviyesi hata enjeksiyonu (gecikme/dusurme/bozma/bolme/metin) |
| `sim/scenario.py` | JSON zaman cizelgesi yorumlayici |
| `sim/control.py` | TCP kontrol kanali + eylem dagiticisi |
| `sim/trace.py` | JSONL iz (harness oracle'i) |
| `sim_cmd.py` | Kontrol kanali istemcisi |
| `scenarios/` | Hazir senaryolar (normal.json) |

## Dogrulama

Donanimsiz self-test (codec CSV golden'lari, cfg_crc, halka, grup,
idempotency, PWRB GEN makinesi, fault motoru):

```
python test/system/rf_hil/test_sim_selftest.py
```

HIL kosusu (canli cihaz + konsol):

```
python test/system/rf_hil/run_hil.py --rf-port COM10 --console COM16
```

## Notlar

- `tools/rf-hub-sim/` (eski C araci) guncel protokol icin referans
  DEGILDIR; bu arac onun yerine gecti.
- Protokol referansi 09.10.2026 SCP arayuzu R2 ve BQ-17-20 yanitidir.
  R1 CSV'leri yalniz tarihsel wire test girdileridir. R2 modelinde 0x48,
  head'de kesilen RANGE, EUI ozeti, LIVE boot sayaci, 45 B E1, E7 b4,
  pre-COMMIT ABORT ve korunmus grup kimligi sinanir. Planlanan firmware ozellikleri bugunku
  davranis yerine uygulanmaz. Ayrintili kontrol:
  [RF-SCP yanıt kontrolü](../../doc/RF_SCP_BOLATEX_YANIT_KONTROLU.md).
