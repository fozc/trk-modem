# GSM arıza enjeksiyonu

**Amaç:** Normal GSM çağrı yollarındaki timeout ve kurtarma sınırlarını
cihaz üzerinde sınamak.

**Kullanım yeri:** Donanım testi yetkisi verilmiş bench cihazı. Üretim
firmware'iyle aynı anda kullanılmamalıdır.

## Kurulum

ARM toolchain PATH'e eklendikten sonra yeni bir çıktı diziniyle çalıştırılmalıdır:

```powershell
python test/system/device_fault/build_fixture.py --kind gsm --outdir test/build/gsm-fixture
```

Builder, mevcut kaynakları geçici değiştirir ve `finally` içinde eski
baytlarına döndürür. Aynı anda host testi veya başka derleme
çalıştırılmamalıdır. `manifest.json` içinde `sources_restored` doğrulanmalıdır.
İmzalı `fixture.efw`, [donanım rehberindeki](../../../doc/HW_TEST_VE_DEBUG_REHBERI.md)
BSL/XMODEM yoluyla kurulmalıdır.

## Çalıştırma

```powershell
python test/system/gsm_fault/run_tests.py 188.59.76.59 --case transport --log test/build/gsm-transport.log
```

Her vaka öncesinde ve sonrasında gerçek HTTP yanıtı ve IEC104 TESTFR
doğrulanmalıdır. `--case` seçenekleri:

- `transport`: RX sessizliği, geç gelen yanıt, HAL DMA başlatma dönüşünde
  `HAL_BUSY`, AT engine TX completion bildiriminin bir defa düşürülmesi.
- `busy`: Başarılı AT ilerlemesi olmadan iki soft recovery ve MCU reseti.
- `stall`: AT üretmeyen FREE süreçte iki modem yeniden başlatma isteği
  ve MCU reseti.
- `initfail`: Gerçek `CGDCONT` yanıtını ERROR yaparak init merdiveninin
  ENHRST, cold boot ve EWDT basamaklarını sınama. Kartta ayrı RESET pini
  bulunmadığından pin reset basamağı kullanılmamalıdır.
- `prompt` / `ack`: Gerçek modem prompt'unu veya payload ACK'ini bir defa
  düşürme. Toparlanma başarısızlığı test hatası sayılmalıdır.
- `pdp` / `detach`: Gerçek `SGACT=1,0` / `CGATT=0` sonrasında otomatik
  ağ dönüşünü sınama. Bu test RF kesilmesi değildir.
- `sim`: Başlangıç `SIMDET` modu okunur; `SIMDET=0` ile SIM yokluğu
  benzetilir ve özgün mod geri yüklenip AT yanıtıyla doğrulanır. Bu modem
  QSS=1 raporlayabilir; bu koşu QSS=0 çıkarma yolunu kanıtlamaz.
  Fiziksel SIM teması sınanmaz.

Watchdog vakaları zaman damgasını geri alır; üretim timeout değerleri
değişmez. Bu sonuç gerçek 5–10 dakikalık fiziksel bekleme değildir.
HAL dönüş ve RX enjeksiyonu fiziksel DMA, UART veya RF arızası değildir.
Yanlış PIN/PUK denenmemelidir.

## Üretime dönüş

Test sonunda `make -C Release -B -j8 main-build`, ardından
`make -C Release -j8 all` çalıştırılmalıdır. Yeni üretim EFW kurulmalı;
ELF içinde `gsm_fault_*` ve wrapper simgelerinin, konsolda `gsmtest`
komutunun bulunmadığı doğrulanmalıdır. `FW_INSTALL_OK`, `FW_APPROVED`,
HTTP ve IEC104 tekrar doğrulanmalıdır.

Normal imaj üzerinde uzun test başlatılabilir:

```powershell
python test/system/gsm_soak.py 188.59.76.59 --hours 24 --outdir test/build/gsm-soak
```

Bu test ayar veya firmware yazmaz. Her döngüde HTTP gövdesinin aynı
olduğunu ve kalıcı IEC104 bağlantısının TESTFR yanıtını denetler.
`metrics.json`, ilerlemeyi ve hataları içerir. Tam süre dolmadan uzun
test başarılı sayılmamalıdır. SIM'in fiziksel çıkarılması, RF zayıflaması
ve güç kesintisi ayrıca fiziksel test gerektirir.

Gerçek firmware aktarımı ayrıca çalıştırılabilir:

```powershell
python test/system/gsm_firmware_transfer.py 188.59.76.59 Release/normal.efw --log test/build/gsm-fw.log --apply
```

`normal.efw` yerine gerçekten üretilen imzalı paket verilmelidir.
Test, 4096 baytlık bir parçanın yarısında TCP RST uygular; alınan ofsetin
ilerlemediğini ve aynı dosyanın kaldığı ofsetten devam ettiğini doğrular.
Aktarım boyunca IEC104 TESTFR yanıtı denetlenir. `--apply`, BSL kurulumunu
başlatır; bu seçenek için yükleme/reset yetkisi bulunmalıdır. Sonrasında
onaylı açılış ve HTTP/IEC104 doğrulanır. Bu koşu fiziksel güç kesintisi
veya MCU resetinden sonra aktarım ofsetinin kalıcılığını kanıtlamaz.
BSL onayı için kontrollü ek reset uygulanır; `FW_APPROVED` sıra numarası
son `FW_INSTALL_OK` numarasından büyük olmalıdır. Eski onay kaydı ve
fixture'a geri dönüş, merkezi `fault_log` hedefindeki negatif verdict
(sonuç kararı) testlerinde reddedilir.

Büyük çıkış gövdeleri, derlenmiş kaynak dizileriyle karşılaştırılabilir:

```powershell
python test/system/gsm_http_resources_test.py 188.59.76.59 --log test/build/gsm-resources.log
```

Bu test için cihazdaki imajın gömülü HTML kaynakları çalışma diziniyle
aynı olmalıdır. Üç sayfanın tüm gzip baytları, uzunluk ve gzip CRC'si
doğrulanır; eş zamanlı IEC104 TESTFR bağlantısı korunur.
