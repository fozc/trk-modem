# RF HUB Simulatoru (host, Win32)

Modem_RF_Hub'un PC uzerinde birebir simulasyonu. RTU (STM32) tarafini
gercek UART uzerinden konusturur; tum gelen/giden cerceveleri hex +
yorum olarak loglar, klavyeden proaktif bildirim ve hata enjeksiyonu
yapmanizi saglar.

Referanslar: `doc/mailden/Fatih_Paketi_20260821_R1.md`,
`doc/mailden/scp_komut_kullanim_tablosu_R0.md`,
`doc/mailden/0x40_0x44_yanit_duzeni_R1.md`.

## Derleme

MinGW gcc ile (git-bash icinde):

```
cd tools/rf-hub-sim
make            # rf_hub_sim.exe  (canli UART modu)
make test       # hub_selftest.exe - seri port gerekmez, 72 test
```

## Kablolama

RTU'nun USART3 hatti (230400 8N1) bir USB-uart ceviriciye baglanir:

```
STM32 USART3_TX (PC4)  ->  cevirici RX
STM32 USART3_RX (PC5)  <-  cevirici TX
GND                   <->  cevirici GND
```

## Calistirma

```
./rf_hub_sim.exe COM7                  # 230400, R0 modu, acilista BOOT_NOTIFY
./rf_hub_sim.exe COM7 --r1             # 0x40 yaniti 10 B (tail'li, R1)
./rf_hub_sim.exe COM7 --baud 115200    # RTU tarafindaki baudi eslestirin
./rf_hub_sim.exe COM7 --noboot         # acilis bildirimi gonderme
```

Program acilista 300 ms bekleyip BOOT_NOTIFY gonderir; RTU'nun
devreye alma zincirini (TIME_SYNC -> envanter) tetikler.

## Klavye komutlari

| Tus | Islev |
|---|---|
| `b` | BOOT_NOTIFY gonder |
| `d` | DISCOVERY_REPORT gonder (atanmamis yeni EUI) |
| `t` | TRIP_NOTIFY gonder (envanterdeki 1. cihaz icin) |
| `v` | LIVE_DATA gonder |
| `n` | ANOMALY_REPORT gonder |
| `l` | 3 olay kaydi ekle + LOG_AVAILABLE zili (0x47) |
| `e` | Bir sonraki 0x20 istegini ERROR 0x05 ile yanitla ("taze yok") |
| `x` | Bir sonraki istege hic yanit verme (RTU timeout/retry provasi) |
| `f` | Bir sonraki COMMIT'i FAILED yap (sebep RANGE) |
| `1` | 0x40 modunu R0 <-> R1 degistir (8 B / 10 B tail) |
| `s` | Durum ozeti (envanter / grup / halka) |
| `q` | Cikis |

## Log cikti bicimi

- `[RX <-] xx xx ...` - gelen cercevenin ham baytlari
- `[HUB <ms>] ...`    - cercevenin yorumu (istek/yanit/aciklama)
- `[TX ->] xx xx ...` - giden cercevenin ham baytlari
- `[RAW <ms>] ...`    - bayt geldi ama gecerli cerceve cozulemedi
  (bozuk CRC/format; gercek cihaz bunu sessizce atar)
- `PROACTIVE ...`     - hub'un kendiliginden gonderdigi bildirim

## Simulasyon davranis ozeti

- GET_STATUS: uptime (sim saatinden), fw="SIM-R1", cycles (APPLIED sayisi)
- Envanter: INVENTORY_SET ile cihaz eklenir; yeni cihazin config'i
  spec R2 default bloguyla olusturulur ve "taze" isaretlenir
- CFG_READ_ALL: envanterde olmayan/taze olmayan cihaz -> ERROR 0x05
- CFG yazma: 0x22 STAGED -> 0x24 DELIVERED -> ~800 ms sonra APPLIED
  (0x21 bildirimi + cfg_crc). Maskeli alanlar (zone/fider/phase @0-2,
  RF ailesi @57-66) yazimdan etkilenmez - RMW testi icin korumali
- SET tekrari: ayni SEQ + araya istek sokulmadiysa son yanit aynen
  tekrarlanir (R1 2.3d sartli idempotentlik)
- Olay halkasi: 100 yuva, head/wrap/tail, 0x46 haric indeks kurali,
  geriye gitme / head asma ERROR 0x02
- 0x40: R0 8 B / R1 10 B (tail @8-9) - `--r1` ya da `1` tusu
- 0x44: tek cercevede en fazla 4 kayit (244 B limitinden)

## Bilinen basitlestirmeler

- 0x44 cok-cerceveli yanit desteklenmez (count>4 -> ERROR 0x02).
  RTU 4'erli bloklar halinde istemelidir.
- 0x46 wrap gecislerinde mutlak karsilastirma yapilmaz (test
  senaryolarinda wrap asilmaz).
- 0x22 bilinmeyen EUI'yu reddeder (ERROR 0x02). Gercek hub komutu
  kabul edip uygulama asamasinda NO_INV ile FAILED yapabilir.
- EPOCH kesif penceresi 30 s yerine 5 s simule edilir (test hizi).
- CP56 yil alani 2000+ varsayimiyla cozulur.
