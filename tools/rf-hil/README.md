# RF Hub (MH) Simulatoru - tools/rf-hil

BOLATeX Teslim4 R1 SCP arayuzunun (`doc/BOLATeX_Teslim4_R1_RTU_Arayuzu_MD/`)
RF hub (MH) tarafini bir PC uzerinde birebir simule eden Python araci.
Modemin (DUT) USART3 RF hattiyla gercek MH varmis gibi konusur; tum
isteklere spec'e gore yanit verir, proaktif bildirim uretir, hat seviyesi
ve davranis seviyesi hata enjeksiyonu yapar.

## Kablolama

Modemin RF hatti (USART3, 230400 8N1) bir **USB-TTL 3.3 V** ceviriciye
baglanir (RS-485 cevirici uygun DEGILDIR):

```
STM32_USART3_TX (PC4)  ->  cevirici RX
STM32_USART3_RX (PC5)  <-  cevirici TX
GND                    <-> cevirici GND
```

Gercek MH modulu hatta takiliyken simulator ayni hatta baglanmaz
(cakisir); once MH modulunu cikarin.

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
- Protokol referansi yalnizca Teslim4 R1 dokumani + ornek CSV'ler +
  firmware kaynagidir.
