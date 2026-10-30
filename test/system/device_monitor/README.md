# Cihaz İzleme ve Uzun Süreli Doğrulama — device_monitor

**Sürüm:** 1.3

**Tarih:** 2026-10-30

## Amaç

Operatörün başlattığı uzun süreli koşuda konsol, web, Modbus RTU,
IEC104, RF ve güç kartı verilerini denetler. Beklenen sonuçlardan sapma
(anomaly) görüldüğünde ilgili kayıtları bir araya getirir. Koşu klasörü
sonradan insan veya agent incelemesine verilebilir. Eski adı `soak` idi.

## Kullanım yeri

Üretim imajı çalışan test cihazında saatlerce veya günlerce gözlem ve
fonksiyon doğrulaması için kullanılır. Beklenen değerler mevcut cihaz
ayarlarından ve yeniden kullanılan protokol testlerinin kurallarından
alınır; bütün ürün gereksinimlerinin doğrulandığı anlamına gelmez.

## Ön koşullar

- Python ortamında `pyserial` bulunmalıdır.
- Tam IEC104 denetimi için `c104` kurulmalıdır. Kurulum ve sürümler için
  [IEC104 rehberi](../iec104_master/README.md) kullanılmalıdır.
- Portlar ve hedef cihaz, [donanım rehberine](../../../doc/HW_TEST_VE_DEBUG_REHBERI.md)
  göre doğrulanmalıdır. Konsol için varsayılan COM18/230400, RS485 için
  COM10/115200 ve slave adresi 23'tür; bunlar her düzende geçerli değildir.
- Başka bir terminal aynı konsolu, başka bir master aynı IEC104 bağlantısını
  kullanmamalıdır.

## Kullanım

Komutlar depo kökünden çalıştırılır. Cihaza bağlanmadan altyapıyı kontrol etmek için:

```powershell
python test/system/device_monitor/selftest.py
python test/system/device_monitor/run_monitor.py --help
```

Örnek: 24 saatlik koşu, tam IEC104 denetimi ve otomatik IP bulma:

```powershell
python test/system/device_monitor/run_monitor.py auto --console COM18 --rs485 COM10 --hours 24 --iec104-mode master
```

`auto`, IP'yi konsoldaki `gsm status` çıktısından bulur. Yerine açık IP
verilebilir. `master` kipi gerekli c104 ortamı bulunamazsa hata verir.
Varsayılan `auto` kipi ise sürekli master yerine yalnız TESTFR bağlantı
probuna düşebilir; bu kip tam IEC104 veri doğrulaması sayılmamalıdır.
Mevcut `test/system/iec104_master/.venv` ortamı varsa program onu kullanabilir.

Örnek: belirli bir çıktı klasörüne, süre sınırı olmadan çalıştırma:

```powershell
python test/system/device_monitor/run_monitor.py auto --console COM18 --rs485 COM10 --hours 0 --outdir .temp_files/device-monitor-run-01
```

Çıktı klasörü önceden bulunmamalıdır. `Ctrl+C` ile düzgün kapatma yapılır;
programın kapanış özeti yazması beklenmelidir. Aynı rastgele görev dizisini
tekrar denemek için `--seed` kullanılabilir; ağ ve cihaz zamanlaması aynı
olacağı garanti edilmez. Bütün seçenekler `--help` çıktısındadır.

Merkezi integration girişi, varsayılan olarak yalnız cihazsız self-test çalıştırır:

```powershell
make -C test/integration/device_monitor run
```

Bu giriş üzerinden gerçek cihaz koşusu açıkça etkinleştirilir:

```powershell
$env:DEVICE_MONITOR_DEVICE = '1'
$env:DEVICE_MONITOR_HOURS = '24'
make -C test/integration/device_monitor run
Remove-Item Env:DEVICE_MONITOR_DEVICE, Env:DEVICE_MONITOR_HOURS
```

Doğrudan `run_monitor.py` kullanımı, kip ve portların komutta açıkça
belirtilmesini sağladığı için elle yapılan koşularda tercih edilebilir.

## TCP bağlantısını kapatma

Rutin `tcp_drop` görevi `/loading.html` yanıtını tamamen almalıdır.
`Content-Length` ile alınan gövde uzunluğu eşit ve HTTP durumu 200 olmalıdır.
Bundan sonra rastgele 0,2–2 saniye beklenmeli ve FIN/RST ile kapatılmalıdır.
Eksik yanıt, başarılı kapatma olarak kaydedilmemelidir. `web.jsonl` içindeki
`phase=after-response` ve `response_complete` alanları bu ayrımı gösterir.

Aktarım sırasında erken kapatma ayrı bir hata senaryosudur. Mevcut araçla
`python test/system/http_port_drop_test.py <IP>/loading.html --phase after-headers --min 0 --max 0 --count 1`
çalıştırılabilir. FIN için `--graceful` eklenmelidir; varsayılan RST'dir.
Bu senaryo bilinen modem takılmasını tetikleyebilir. Sonuç, normal aktarım
başarısı ile ayrı değerlendirilmelidir.

## Çıktı düzeni ve inceleme için paylaşım

Varsayılan çıktı `test/build/device_monitor/<zaman>/` altındadır.

| Dosya veya klasör | İçerik |
|---|---|
| `manifest.json` | Git kimliği/dirty sayısı, araç sürümleri, hedef, portlar ve seed |
| `summary.md` | Kapanış özeti, anomali sayıları ve ölçülen sonuçlar |
| `metrics.json` | Çalışma durumu, görev sayıları ve hata sayaçları |
| `anomalies.jsonl` | Beklenmeyen durumların zamanı, sınıfı, şiddeti ve açıklaması |
| `evidence/` | Anomali anındaki konsol çıktıları, sayaçlar ve yakın kayıtlar |
| `serial.log`, döndürülmüş kopyaları | Ham cihaz konsolu |
| `commands.jsonl`, `health.jsonl`, `health_history.csv` | Gönderilen konsol komutları ve sağlık eğilimleri |
| `web.jsonl`, `modbus.jsonl`, `rf.jsonl` | Arayüz okumaları ve doğrulama sonuçları |
| `iec104.jsonl`, `iec104-apdu.jsonl` | IEC104 turları; master kipinde ham ve çözülmüş APDU'lar |
| `ay/`, `chains.jsonl` | AY etkinleştirilmişse olay üretimi ve zincir kontrolleri |

İnceleme için koşu klasörünün tamamı ZIP olarak paylaşılmalıdır; yalnız
`summary.md` yeterli değildir. Sorunun yaklaşık saati, gözlenen davranış
ve çalıştırılan komut da belirtilmelidir. Ani kapanmada özet oluşmamışsa
ham dosyalar korunmalı; özet çevrimdışı yeniden üretilebilir:

```powershell
python test/system/device_monitor/summarize.py test/build/device_monitor/20261030-120000
Compress-Archive -LiteralPath test/build/device_monitor/20261030-120000 -DestinationPath .temp_files/device-monitor-20261030-120000.zip
```

Örnekteki zaman, gerçek koşu klasörüyle değiştirilmelidir. Paylaşılacak
kayıtlardaki IP, seri numarası ve diğer kuruma özel bilgiler kontrol edilmelidir.

## Kurallar ve sınırlar

- Modbus istekleri aynı seri portta sırayla çalışır; yanıt tamamlanmadan
  başka görev istek göndermez. Web oturumu ve TCP bağlantı kesme denemesi
  de ortak kilit kullanır; aynı anda ikinci web işlemi başlatılmaz.
- Çalışan cihazın yapılandırmasını kaydetme, firmware yükleme, günlük silme
  ve cihaz reseti bu altyapının mevcut görevleri arasında değildir.
- Varsayılan görevler tamamen pasif değildir: web giriş istekleri, IEC104
  GI (genel sorgu), saat eşitlemesi ve rastgele TCP bağlantı kesme denemeleri
  yapılır. Saat eşitlemesi cihaz saatini değiştirebilir.
- AY olay üretimi varsayılan kapalıdır. `--ay`, `--ay-mapping` ve ilgili
  seçenekler yalnız uygun test düzeneğinde verilmelidir. `test trip` yasaktır;
  mevcut koruma yalnız test olaylarına izin verir ve olay bütçesi uygular.
- Konsol ham kaydı 10 MB sınırında beş eski kopya tutar. JSONL kayıtları
  aynı rotasyona sahip değildir; uzun koşularda disk alanı izlenmelidir.
- `skip`, başarıdan ayrı değerlendirilmelidir. `passed` sonucu yalnız o
  koşuda etkin denetimlerin kritik hata bulmadığını gösterir; uyarılar ve
  atlanan denetimler ayrıca okunmalıdır. Beklenmeyen durumun cihazdan mı
  yoksa test istemcisinden mi kaynaklandığı kayıtlarla incelenmelidir.
- Host self-test geçişi, gerçek cihazın uzun süreli kabulü veya fiziksel
  açma/gerilim ölçümü olarak sunulmamalıdır.

## Yeniden kullanılan bileşenler

`tools/hwtest/serial_io.py`, `test/system/iec104_master/`,
`test/integration/modbus_device/run_tests.py`, mevcut web/bağlantı
probları ve `Application/modbus_map.h` kullanılır. `device_monitor`
bunları ortak zamanlama, kayıt ve anomali toplama altında çalıştırır.

## Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 2026-10-30 | 1.3 | Rutin TCP kesmeleri tam HTTP yanıtından sonraya alındı; erken FIN/RST senaryosu ayrı açıklandı |
| 2026-10-30 | 1.2 | Modbus işlemleri ve ortak web oturumu sıraya alındı; eşzamanlı erişim regresyonları eklendi |
| 2026-10-30 | 1.1 | `soak` adı `device_monitor` olarak değiştirildi; elle çalıştırma ve kayıt paylaşımı açıklandı |
