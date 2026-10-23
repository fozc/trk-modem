# JSON uçtan uca cihaz doğrulaması (tüm alanlar)

**Tarih:** 23.10.2026

Bu tarihsel koşunun sınırları ve betik hataları
[bağımsız incelemede](2026-10-23-json-device-e2e-review.md), sonraki
düzeltmeler [uygulama raporunda](2026-10-23-json-device-fixes.md) kayıtlıdır.
Sonuçlar yeni paketin fiziksel kabulü olarak kullanılmamalıdır.

## Amaç

Web sunucusunun HTTP endpoint'leri bağımsız JSON çekirdeğine
(`libraries/json`, `web_json.h`) taşındıktan sonra, gerçek cihaz üzerinde
web üzerinden **tüm alanların** doğru aktarıldığını ve parse edildiğini
uçtan uca doğrulamak.

## Kullanım yeri ve kurulum

- Cihaz konsolu COM18/230400 ile doğrulandı; web sunucusu GSM üzerinden
  erişildi (`WEB_DEVICE_HOST`, varsayılan 188.59.76.59).
- Kurulu imaj `ff219be7` + taşınmış çalışma ağacı derlemesidir:
  `v1.0.1_20261023_174215_ff219be7.efw`, ham 363 104 B, CRC `0xCC9D60CD`,
  paket self-check PASSED. Kurulum XMODEM/BSL yoluyla yapıldı; boot log
  seq **1050 FW_INSTALL_OK** ve açılışta `Firmware approved successfully!`.
- Host tabanı: merkezi Ceedling **1682/1682**, 0 fail/ignore. Bu koşu
  öncesi `test_http_iec_config_scenario.c` içindeki iki beklenti yeni
  davranışa güncellendi (aşağıda).
- Cihazda mevcut `web_device` süiti **21/21 PASS** (şemalar, StopBits
  çevrimi, ETag 304 yenidoğrulaması dahil).

## Yöntem

Yeni kalıcı betik `test/integration/web_device/run_roundtrip.py` tek
keep-alive oturumla (taşıma hatasında settle+retry ile yeniden bağlanır)
şu katmanları koşar:

1. **GET şema taraması** — board, device, iec104, modbus, rf, rf-group,
   monitor/rf, faults, syslogs, fw_status: yazarların ürettiği bütün
   anahtarlar ve dizi boyları.
2. **Birebir (verbatim) gövde round-trip** — dört config sayfasının GET
   gövdesi aynen POST edilir (RO alanlar atlanır kanıtıyla) ve yeni GET
   ile alan-alan eşitliği karşılaştırılır (Time/RunTime/LifeTime/SonHata*
   uçucu alanlar hariç).
3. **Alan bazlı aktarım kanıtı** — her yazılabilir alan geçerli başka bir
   değere değiştirilir, yeni GET'te doğrulanır ve geri yüklenir:
   device 16 alanın 10'u doğrudan; link kritik olanlar (WebArayuzuPortu,
   NtpServerPortu, SimKartPin, TimeZone, reset periyodu; iec104 Port,
   ScadaIPAdresi, PeriodicSend, T1, T3, CommonAddr; modbus üçü) geçersiz
   değerle 400 reddi ile kanıtlanır. Dize alanları aynı değerin
   `\uXXXX` escape'li kodlamasıyla parse yolu kanıtı yapar.
4. **IEC Hatlar** — nokta adresi (IOA) dizileri yalnız kullanımdaki
   hatlara yazılır (`set_iec_config` kullanım dışı hatta IOA bloğunu
   atlar). Tezgah cihazında bütün hatlar kullanım dışı olduğundan 15 IOA
   dizisi tek POST'ta hat 0'ı kısa süre kullanıma alıp boş adres
   penceresine taşınarak, doğrulanıp orijinaline geri yüklenip hat tekrar
   kullanım dışı bırakılarak kanıtlandı.
5. **RF** — değer mutasyonu bilinçli olarak yapılmaz (gerçek MH
   provizyon akışını tetikler); birebir round-trip + aralık reddi
   (ArtimliAkimEsigi 9999, HatFrekansi 55 → 400). Alan başına saklama
   semantiği host testlerinde (`test_rf_config_parse.c`) kalır.
6. **Yapısal** — bozuk JSON gövdesi dört endpoint'te de 400.
7. **Geri yükleme kanıtı** — koşu sonunda dört sayfa da başlangıç
   anlık görüntüsüyle alan-alan eşitlenir.

Betiğin kendi kontrol akışı sahte cihaz üzerinde iki dalda da
kuru-koşuldu (exception yok; yalnız reddi kontrolleri başarısız — sahte
aygıt reddetmez).

## Sonuç

**Son koşu: 79 kontrol PASS, 5 FAIL** (kanıt dosyasında). Bütün
şema, birebir round-trip, kaçış kodlaması, yapısal ve geri yükleme
kontrolleri geçti. IEC104'ün 15 skaler alanı, Hatlar tabanları, nokta
adresleri ve inUse aktarımı; Modbus'un üç ayarı; cihaz ayarlarının
parse edilen alanları kanıtlandı.

## Açık bulgu: beş cihaz ayarı kalıcılaşmıyor (migration öncesi)

`field_device_{WebIlkVeriZamanAsimi, WebBostaKalmaZamanAsimi,
IEC104IlkVeriZamanAsimi, IEC104BostaKalmaZamanAsimi,
DevreyeAlinmaZamani}_transfer` kırmızıdır: POST 200
`"Device configuration saved","success":true` döner, değer yeni GET'te
eski kalır (örnek: `WebBostaKalmaZamanAsimi` 5 gönderildi, 0 okundu).

Kök neden `Application/modem_config.c:31-46`: `modem_config_set` yalnız
dokuz alanı beyaz listeyle uygular (web portu, SIM PIN/APN üçlüsü, NTP
sunucu/port, saat dilimi, reset periyodu). Dört zaman aşımı alanı ve
`commissioning_time` JSON katmanında parse edilir ama veri modelinde
sessizce bırakılır. `modem_config_set_commissioning_time` var fakat
çağrılmaz; dört timeout için setter yoktur. Bu dosya taşınmadan
değişmedi (son değişiklik c47de38) — bulgu taşınma öncesidir ve daha
önce kapatılan O6.7 sınıfının ("GET yayınlıyor, POST sessizce
uygulamıyor") veri modeli katmanındaki eşdeğeridir. Çözüm kullanıcı
kararı bekler: alanları kalıcılaştırmak (setter ekleyip beyaz listeye
almak) veya GET/UI'dan kaldırmak.

## Tasarım notları (bulgu değil)

- **Kullanım dışı IEC hattının IOA dizileri yazılmaz** — tabanlar
  güncellenir, nokta adresleri eski değerde kalır. Cihazda tabanlar
  100 000.. / 200 000.. kuşağındayken nokta adresleri eski 1 000
  düzeninden (1030 vb.) geliyor; taban değişiminde kullanım dışı hattın
  IOA'ları bilinçli olarak bayat bırakılıyor.
- **Gövde sınırı** — `MAX_CONTENT_LENGTH` 4608 B
  (`http_request_parser.c:19`, 4 KB fw chunk desteği). 5 578 B'lık
  modbus GET gövdesinin aynen POST'u HTTP katmanında 400 verir; JSON
  parse'ı hiç çalışmaz. RegisterMap derleme zamanında sabit read-only
  metadata olduğu için POST edilmez; iç içe dizi taşıyan kompakt bir
  `RegisterMap` anahtarı bilinçli olarak atlanır (200) ve ayarlar
  değişmez. Betik gövdeleri 4 500 B altında parçalara böler.
- **Tek web slotu** — büyük gövdeli 400'ün ardından cihaz bağlantıyı
  bir süre reddedebilir (bilinen RST/slot takılması sınıfı); betik
  settle+retry ile yeniden bağlanır. Tarayıcı sekmesi test sırasında
  kapalı tutulmalıdır (README'ye not eklendi).

## Taşınmayla gelen iki test beklentisi güncellemesi

`test_http_iec_config_scenario.c` (yalnız test, üretim kodu değişmedi):

1. `handle_fw_start("")` — eski parser boş gövdeyi `size=0` sayıp 200
   JSON `"invalid size"` döndürüyordu; ortak çekirdek boş gövdeyi
   geçersiz JSON sayar ve handler 400 Bad Request gönderir (NULL gövde
   yoluyla aynı). Beklenti 400'e güncellendi.
2. Küçük tamponla modbus GET — endpoint'e özgü `"Modbus register map
   exceeds response buffer"` mesajı yerine ortak `http_json_send`
   yolu tek genel 500 mesajı verir; koruma aynıdır. Beklenti güncellendi.

Paket son haliyle 13/13; tam süit 1682/1682.

## Kanıt ve tekrar kullanım

- Son koşu çıktısı: `test/reports/2026-10-23-json-device-e2e.log`
  (PASS/FAIL satırları).
- Betik: `test/integration/web_device/run_roundtrip.py`
  (`python test/integration/web_device/run_roundtrip.py [host]`).
- Kurulum ve boot kanıtı: ham seri kayıtlar `/tmp` altında oturuma aittir;
  boot logda seq 1050 FW_INSTALL_OK kaydı kalıcıdır.

## Karar ve sınırlar

Yeni JSON çekirdeği cihaz üzerinde HTTP yoluyla tüm config sayfalarında
alan eksiksiz serialize/parse etmektedir; doğrulama, kaçış kodlaması ve
hata yolları uçtan uca çalışır. Kalan açık yukarıdaki beş alanlı
veri-modeli bulgusudur (taşınma dışı, kullanıcı kararı bekler). Bu koşu
RF provizyon kabulü, IEC104 master oturumu, Modbus RTU fiziksel hattı
veye röle kontak ölçümü değildir; host testleri hedef zamanlaması
kanıtı sayılmaz.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 23.10.2026 | Yeni imajın kurulması, web_device 21/21, run_roundtrip.py ile tüm-alan taraması, beş alanlı veri-modeli bulgusunun açılması |
