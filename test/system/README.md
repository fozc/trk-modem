# Donanım testleri ve HIL

## Amaç

RTU üzerinde çalışan testleri tek yerden bulmak, ayrı ayrı çalıştırmak
ve sonuç dosyalarını okumak. HIL (donanım döngüde) koşusunda RTU gerçektir.
RF HIL'de PC, MH'nin yerini alır. Gerçek MH ve AY ile yapılan ölçümler
ayrı testlerdir.

## 1. Ön koşullar

- Örnek komutlar depo kökünden çalıştırılmalıdır. Örneklerdeki
  `192.0.2.1`, `COM18` ve `COM10` yerine test düzeneğinin IP ve portları
  kullanılmalıdır. RTU IP'si konsoldaki `gsm status` çıktısından okunabilir.
- Seri port kullanan testler için seçilen Python ortamında `pyserial`
  kurulmalıdır. Portları görmek için `python -m serial.tools.list_ports -v`
  çalıştırılabilir. RTU konsolu ile RS485 adaptörünün aynı port olmadığı
  doğrulanmalı, portu açık tutan terminal kapatılmalıdır.
- IEC104 kabul süiti için [c104 kurulumu](iec104_master/README.md#kurulum-bir-kez)
  izlenmelidir. Tek istemcili IEC104 dinleyicisinde diğer master
  oturumu kapatılmalıdır.
- Röle, ayar yazma, reset, bağlantı kesme ve arıza testleri ayrılmış test
  düzeneğinde çalıştırılmalıdır. Koşuya başlamadan önce cihazın firmware
  kimliği ve değişebilecek başlangıç ayarları kaydedilmelidir. Test imajı
  kurulacaksa [firmware ve konsol rehberi](../../doc/HW_TEST_VE_DEBUG_REHBERI.md)
  kullanılmalıdır.

### Test seçimi

```powershell
python test/system/run_device.py --list
python test/system/run_device.py relay-din --help
python test/system/run_device.py rf-hil --list
```

Git Bash veya POSIX shell (komut kabuğu) ile aynı seçenekler kullanılabilir.

```sh
sh test/system/run_device.sh --list
sh test/system/run_device.sh relay-din --help
sh test/system/run_device.sh rf-hil --list
```

İlk komut test adlarını, diğer ikisi seçilen testin yardımını veya vaka
listesini gösterir. `run_device.sh`, Python girişine aynı argümanları
iletir ve onun çıkış kodunu döndürür. Yalnız `python3` komutu varsa
`PYTHON=python3 sh test/system/run_device.sh --list` kullanılabilir.
Test betikleri kendi yollarından da doğrudan çalıştırılabilir.

## 2. IEC104 röle ve dijital girişler

GI20 ve DIN bildirim koşuları Python standart kütüphanesiyle çalışır.
Röle koşusu RTU konsolu için ayrıca `pyserial` kullanır.

### Adımlar

1. IEC104 portunu, Common Address (CA) ve röle/DIN IOA'larını cihaz
   ayarlarından okuyun. Aşağıdaki örneklerde CA `1`, port `2404` kullanılır.
   Betiğin varsayılan IOA'ları röleler için `10002 10003`, DIN1–DIN4 için
   `10004 10005 10006 10007` değerleridir. Farklı ayarlar için kanal
   sırasıyla `--relay-ioas` ve `--din-ioas` verin.
2. Yalnız güncel girişleri okumak için GI20 (genel sorgu, QOI 20) çalıştırın.

   ```powershell
   python test/system/run_device.py relay-din --host 192.0.2.1 --port 2404 --ca 1
   ```

3. Röle kumandasını denemek için `--relay-test` ve RTU konsolunu ekleyin.
   Betik her iki röleyi sırayla değiştirir. Test bitince ilk durumların
   geri geldiğini konsoldan kontrol edin.

   ```powershell
   python test/system/run_device.py relay-din --host 192.0.2.1 --ca 1 --console COM18 --relay-test
   ```

   ```sh
   sh test/system/run_device.sh relay-din --host 192.0.2.1 --ca 1 --console COM18 --relay-test
   ```

4. Bağlantı açıkken DIN bildirimi için `--din-changes` kullanın. Betiğin
   ekranda istediği girişe sırayla istenen mantıksal değeri uygulayın.
   Her giriş önce ters duruma, sonra ilk durumuna alınır. Her hareket
   için varsayılan bekleme süresi 30 saniyedir.

   ```powershell
   python test/system/run_device.py relay-din --host 192.0.2.1 --ca 1 --din-changes --din-timeout 45
   ```

### Doğrulama

| Koşu | Başarı ölçütü |
|---|---|
| GI20 | Olumlu ACT_CON ve ACT_TERM arasındaki yanıtta dört DIN IOA'sı birer kez, geçerli kaliteyle ve Type 1 olarak gelir. Röle IOA'ları komut adresidir. |
| Röle | Type 45 SBO (Select Before Operate) akışında seçimsiz Execute reddedilir. Select röle durumunu değiştirmez. Geçerli Execute yalnız hedef röleyi değiştirir ve başlangıç durumu geri okunur. |
| DIN bildirimi | Her hareket doğru IOA ve değerle, Type 30/COT 3 olarak ve geçerli CP56Time2a damgasıyla gelir. Son GI20, başlangıç değerlerine döner. |

Başarılı kontroller `PASS`, hata `FAIL` olarak yazılır. Betik başarıda
`0`, yakalanan test hatasında `1`, geçersiz argümanda `2` döndürür.
Her koşunun ham ve çözülmüş IEC104 trafiği
`test/build/iec104-master/relay-din-<zaman>/apdu.jsonl` dosyasına yazılır.
Röle koşusunda aynı dizinde `console.log` bulunur.

Röle koşusundaki `relay status`, cihazın bildirdiği kumanda durumunu
gösterir. Fiziksel kontağı doğrulamak için onaylı test bağlantısında kontak
ölçümü veya röleye bağlı DIN geri bildirimi ayrıca gözlenmelidir.
`--din-changes` dört girişte de operatör adımı ister. Bu koşu GSM kesintisi
sırasında fiziksel DIN olaylarının daha sonra gönderilmesini sınamaz.
IEC104 süitindeki `i06_replay_reconnect` ve `i07_replay_retention` kayıt
gönderimini sentetik olaylarla sınar. Cihaz konsolu ve izinler için
[IEC104 kabul süitine](iec104_master/README.md#vaka-kataloğu) bakılmalıdır.

## 3. Protokol ve RF testleri

Aşağıdaki komutlar, `python test/system/run_device.py` sonuna eklenir.

| Test ve örnek seçenekler | Ne yapılır ve sonuç nerede bulunur? |
|---|---|
| `iec104 --host 192.0.2.1 --case i02_gi_station --require-device` | Seçili IEC104 kabul vakası. `test/build/iec104-master/` altında `manifest.json` ve `apdu.jsonl`. [Vaka ve ayar seçenekleri](iec104_master/README.md). |
| `modbus` | Gerçek RS485 üzerinde register haritası, röle yazma/okuma, broadcast, exception ve gecikme ölçümü. Aşağıdaki ortam değişkenleri kullanılır. Son satır `SONUC: ... PASS / ... FAIL / ... NOT`. |
| `web 192.0.2.1 --allow-config-writes` | Web oturumu ve API kabulü. Ayar yazma izinlidir. Sonuç terminalde PASS/FAIL olarak görünür. |
| `web-roundtrip 192.0.2.1` | Web ayarlarını değiştirir, geri okur ve başlangıç değerlerini geri yükler. RF ayarı ayrıca `--allow-rf-apply` ister. Sonuç terminalde görünür. |
| `rf-hil --rf-port COM10 --console COM18 --firmware-image $Efw --transport-profile 1` | Kurulu RTU, PC'deki MH simülatörüne bağlanır. `$Efw` cihazdaki imajın dosya yolu olmalıdır. `--list` ile vaka adları görülür, `--case` ile biri seçilir. `test/build/hil-rf/` altında `report.md`, `manifest.json` ve vaka izleri bulunur. [Kablolama ve profiller](../../tools/rf-hil/README.md). |
| `rf-real-mh --console COM18 --fw $MhFw --case m1_hub_fw --require-device` | Gerçek MH ile RTU kullanılır. `$MhFw` beklenen MH firmware kimliği olmalıdır. `test/build/rf-real-mh/` altında `report.md`, `results.json` ve `console.log`. [Vaka ve donanım koşulları](rf_real_mh/README.md). |

Modbus için ayrı RS485 portu ve cihazla aynı baud, slave adresi ve stop
bit ayarı seçilir. Test R1'i değiştirir ve normal akışta OFF bırakır.
R2 için ilk durumun geri geldiğini kontrol eder. Koşudan sonra iki
rölenin durumunu da kontrol edin.

```powershell
$env:MODBUS_RTU_PORT = 'COM10'
$env:MODBUS_RTU_BAUD = '115200'
$env:MODBUS_RTU_SLAVE = '23'
$env:MODBUS_RTU_STOPBITS = '1'
python test/system/run_device.py modbus
Remove-Item Env:MODBUS_RTU_PORT, Env:MODBUS_RTU_BAUD, Env:MODBUS_RTU_SLAVE, Env:MODBUS_RTU_STOPBITS
```

Git Bash veya POSIX shell örneği

```sh
MODBUS_RTU_PORT=COM10 MODBUS_RTU_BAUD=115200 MODBUS_RTU_SLAVE=23 MODBUS_RTU_STOPBITS=1 \
  sh test/system/run_device.sh modbus
```

## 4. İzleme, bağlantı ve arıza testleri

| Test ve örnek seçenekler | Cihaz etkisi ve kanıt |
|---|---|
| `monitor auto --console COM18 --rs485 COM10 --hours 1 --iec104-mode master` | Konsol, web, Modbus, RF ve IEC104 örneklerini toplar. Özet `test/build/device_monitor/<zaman>/summary.md`. [İzleme kılavuzu](device_monitor/README.md). |
| `gsm-soak 192.0.2.1 --console COM18 --hours 1 --outdir test/build/gsm-soak-01` | HTTP ve IEC104 TESTFR sürekliliğini izler. Yeni çıktı dizininde `metrics.json` ve `serial.log`. |
| `gsm-disconnect 192.0.2.1 --console COM18 --rounds 2 --log test/build/gsm-disconnect.log` | HTTP istemcisinin kesilmesinden sonra HTTP ve IEC104 çalışmasını kontrol eder. Sonuç terminalde, konsol izi `--log` dosyasında. |
| `http-drop 192.0.2.1/loading.html --phase after-response --min 0 --max 0 --count 1` | HTTP bağlantısını seçilen aşamada kapatır. Terminal çıktısı yanıtın ve kapatma işleminin ayrıntılarını verir. [Aşamaların kapsamı](device_monitor/README.md#tcp-bağlantısını-kapatma). |
| `gsm-resources 192.0.2.1 --console COM18 --log test/build/gsm-resources.log` | Gömülü gzip sayfalarını cihazdaki imajla eşleşen kaynaklarla karşılaştırır. Terminalde sayfa başına PASS, `--log` dosyasında konsol izi. |
| `gsm-firmware 192.0.2.1 $Efw --console COM18 --log test/build/gsm-fw.log` | İmzalı paketi test cihazına aktarır ve TCP kesintisinden sonra devamı doğrular. `--apply` eklenirse kurulum ve reset de yapılır. Terminal sonucu ve konsol izi birlikte kaydedilir. [Aktarım ve kurulum ayrıntıları](gsm_fault/README.md#üretime-dönüş). |
| `ntp-reset --console COM18 --timezone 3 --outdir test/build/ntp-reset-01` | RTU'yu resetler, NTP ve saat dilimi uygulamasını kontrol eder. Çıktı dizininde `result.json` ve `console.log`. |
| `gsm-fault 192.0.2.1 --console COM18 --case transport --log test/build/gsm-fault.log` | GSM test imajı gerektirir. Seçilen arızaya göre veri oturumu kesilebilir veya RTU resetlenebilir. Terminal sonucu ve konsol izi kaydedilir. [İmaj, vakalar ve üretime dönüş](gsm_fault/README.md). |
| `device-fault --console COM18 --rs485 COM10 --outdir test/build/device-fault-01` | Özel test imajıyla UART toparlanmasını ve HardFault kaydını sınar. Sonuç `results.json` dosyasında. [İmaj ve üretime dönüş](device_fault/README.md). |

## 5. Sonuçları değerlendirme

- `PASS` tamamlanan vakanın ölçütlerinin sağlandığını gösterir. Bir
  koşuda `SKIP`, `BLOCKED` veya `DEFERRED` varsa o vakalar doğrulanmış
  sayılmamalıdır. Kabul koşusunda desteklenen süitlerde
  `--require-device` kullanılmalıdır.
- İnceleme için çalıştırılan komut, firmware kimliği ve testin ürettiği
  dosyalar bir arada saklanmalıdır. Yalnız terminale yazan testlerde
  PowerShell'deki `Tee-Object -FilePath` ile çıktı kaydedilebilir.
- Cihaz erişimi olmadan `--list`, `--help` veya süitlerin self-test'leri
  kullanılabilir. Bunların sonucu fiziksel donanım ölçümü sayılmamalıdır.
  Ruby/Ceedling ve integration girişleri için [test altyapısı](../README.md)
  kullanılmalıdır.

### Elle yapılan kontroller

Firmware yükleme, seri konsol ve ST-Link işlemleri
[donanım ve debug rehberinde](../../doc/HW_TEST_VE_DEBUG_REHBERI.md).
JSON yapılandırma için cihazda uygulanacak adımlar
[JSON cihaz testinde](../integration/json/DEVICE_TEST.md).
Web üzerinden Modbus ayarlarını sınamak için hedefi komutta alan
`web-roundtrip` kullanılır.

## 6. Sık hatalar

| Belirti | Kontrol |
|---|---|
| Seri porta erişilemiyor | Port numarasını ve terminalin portu açık tutup tutmadığını kontrol edin. |
| GI20 timeout veya DIN IOA eksik | Cihaz IP'sini, TCP portunu, CA'yı, IOA'ları ve diğer IEC104 oturumlarını kontrol edin. |
| RF HIL `BLOCKED` veriyor | `--firmware-image` ile kurulu imajı ve `--transport-profile` ile profili eşleştirin. Benç profili 1'in RS485 hattı bu koşuda SCP içindir. |
| Modbus `SKIP` veriyor | Aynı PowerShell oturumunda `MODBUS_RTU_PORT` tanımını kontrol edin. |
