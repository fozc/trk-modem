# Örnek SCP akışları ve telemetri

Kayıtlar 04.10.2026 ve 05.10.2026 sistem testlerinden alınmıştır. Çerçeve baytları değiştirilmemiştir (CRC geçerli). Dosyalar ayrıştırıcı sınaması ve akışların incelenmesi içindir.

## Dosyalar

### Senaryolar

| Dosya | İçerik |
|---|---|
| AY_01_acilis_envanter.csv | Boot notify, Time sync, Inventory set / end / update ve yanıtları |
| AY_02_kesif.csv | Discovery report |
| AY_03_zaman_esitleme.csv | Time sync ve yanıtı |
| AY_04_canli_veri.csv | Dört ayırıcıdan 60 s Live data |
| AY_05a_acma_bildirimi.csv | Trip notify |
| AY_05b_olay_kaydi_cekme.csv | Log available notify, event log head read, range read, consume ve yanıtları |
| AY_06_yapilandirma_yazma.csv | Üç Config write, Config commit, Config status notify |
| AY_06b_yapilandirma_geri_alma.csv | Önceki yapılandırmaya dönüş, aynı akış |
| AY_07_epoch_yenileme.csv | Epoch refresh ve yanıtı |
| AY_08_anomali.csv | Anomaly report |
| PWRB_01_ozet.csv | 10 dakikalık PWR summary notify |
| PWRB_02_alarm.csv | PWR alarm notify; alarmların başlangıcı ve bitişi |
| PWRB_03_ayar_yazma.csv | PWR config 2 okuma, yazma, yeniden okuma (iki yazma döngüsü) |
| PWRB_04_komut.csv | PWR command, yanıtı ve iki PWR command result bildirimi |

AY_05a ve AY_05b aynı olaya ait değildir. Her senaryoda yalnız o akışa ait komutların çerçeveleri bulunur. Aradaki başka komutların çerçeveleri çıkarılmış, kalan çerçevelerin sırası korunmuştur.

### Telemetri ve olay çizelgesi

| Dosya | İçerik |
|---|---|
| tlm_pwrb_0410.csv | Güç kartı: gerilim (mV), akım (mA), güç (W), SoC (%), SoH (%), sıcaklık (°C) |
| tlm_ay_live_0410.csv | Ayırıcı başına Irms (A), açma kondansatörü gerilimi (V), hasat gerilimi (V), MCU sıcaklığı (°C) |
| olay_zaman_cizelgesi_0410.csv | Trip, Boot, Discovery, Config ve PWR alarm olaylarının zaman çizelgesi |

## Biçim

- UTF-8, ayraç `;`, ondalık ayırıcı nokta.
- Zaman ISO 8601 biçimindedir, milisaniye çözünürlüklü ve yerel saattir (+03:00).
- `yon`: `MH→RTU` = modemden RTU'ya, `RTU→MH` = RTU'dan modeme.

Senaryo sütunları:

| Sütun | Anlamı |
|---|---|
| t_s | Senaryo başından geçen süre (s) |
| zaman | Kayıt zamanı |
| yon | Çerçeve yönü |
| cmd | Komut kodu (0xNN) |
| komut | Komut adı |
| tip | SCP TYPE (GET, SET, ACK) |
| seq | SCP SEQ |
| len | DATA uzunluğu (bayt) |
| veri_hex | DATA alanı |
| mantiksal_hex | COBS öncesi çerçeve: DST, SRC, TYPE, CMD, SEQ, LEN, ~LEN, DATA, CRC (LE) |
| hat_hex | Hatta giden bayt dizisi (COBS, 0x00 sınırlayıcıları dahil) |
| anlam | Komut adı ve rolü |

## Notlar

PWRB_04_komut.csv'deki komut örneği, uygulanmaması için bilerek geçersiz parametreyle (0x00) gönderilmiştir; sonuç 0x02 beklenen yanıttır.

AY_05b'de Log available notify bildirimine verilen ACK, kayıtta bildirimden 1–2 ms önce görünür. Kayıt zamanı ölçüm noktasından kaynaklanır; hatta önce bildirim, sonra ACK gider.

---

*Bu belge BOLATeX tarafından hazırlanmıştır. Yalnız hazırlanış amacı doğrultusunda kullanılabilir; üçüncü kişi ve kurumlarla paylaşılamaz.*
