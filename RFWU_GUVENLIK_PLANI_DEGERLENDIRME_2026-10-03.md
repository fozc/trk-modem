# RFWU güvenlik düzeltme planı değerlendirmesi

| Belge künyesi | Değer |
|---|---|
| Sürüm | 1.1 |
| Tarih | 03.10.2026 |
| İnceleme tabanı | `622f050` ve çalışma ağacındaki lifetime kayıt değişikliği |
| İncelenen plan | `RFWU_GUVENLIK_DUZELTME_PLANI_2026-10.md`, 0.1 |

## Amaç

Planın teyit edilmiş RFWU açığını kapatıp kapatmadığını, mevcut altyapıyla
uyumunu ve uygulama öncesi gerekli düzeltmeleri değerlendirmek.

## Kapsam

Gerçek RFWU parser, PC aracı, BSP random API, NVRAM layout/yükleme akışı ve
kardeş bootloader reposundaki HMAC kaynakları incelenmiştir. RFWU protokolü
bu değerlendirmede değiştirilmemiştir. Donanım testi yapılmamıştır.

## Sonuç

Nonce ile HMAC-SHA256 kullanma yönü doğrudur. Plan 0.1 haliyle uygulanmaya
hazır değildir. Özellikle 4 bayt HMAC, 4 bayt nonce, anahtar oluşturma/kayıt
akışı ve config kaybına yol açabilecek şema değişikliği düzeltilmelidir.
"Açığı tam kapatır" ifadesinin kapsamı daraltılmalıdır: HELLO doğrulaması
tek başına aktif ağ saldırganının açık TCP oturumundaki DATA, ABORT veya
REBOOT paketlerini değiştirmesini engellemez.

## Bulgular ve öneriler

| Öncelik | Plan maddesi | Kanıt ve gerekli düzeltme |
|---|---|---|
| Yüksek | HMAC çıktısının ilk 4 baytı | 128 bit anahtara rağmen etiket yalnız 32 bittir; tek bağımsız tahminin başarı olasılığı 2^-32'dir. 16 bayt etiket önerilir. RFC 2104 §5, çıktının hash boyutunun yarısından ve 80 bitten kısa olmamasını önerir; SHA-256 için yarısı 128 bittir. |
| Yüksek | 4 bayt rastgele nonce | Rastgele 32 bit alanda yaklaşık 77 bin üretimde herhangi iki nonce'un çakışma olasılığı %50'ye yaklaşır. Bu bir sahada ölçülmüş saldırı sıklığı değildir; birthday bound hesabıdır. Eski HELLO'nun yeni nonce ile eşleşmesi yeniden replay olanağı oluşturur. 16 bayt nonce, bağlantı kapanınca silme ve tek kullanım önerilir. |
| Yüksek | Nonce yaşam döngüsü eksik | Aynı bağlantıda aynı HELLO yeniden gönderilmesi, başarısız HELLO sonrası nonce kullanımı ve yeni CHALLENGE'ın eski yetkiye etkisi tanımlanmamıştır. HELLO denemesinde nonce tüketilmeli; tekrar HELLO için yeni challenge istenmelidir. Authenticated oturumda CHALLENGE kabulü/ret davranışı açıkça belirtilmelidir. |
| Yüksek | RNG anahtar üretimi | Güncel `bsp_random_word()` donanım hatasında noncryptographic fallback verir; dönüş değeri hangi kaynağın kullanıldığını bildirmez. Kalıcı gizli anahtar bu fallback ile üretilmemelidir. Güçlü RNG başarısızsa anahtar üretilmemeli; web erişimi için kabul edilmiş fallback davranışı ayrı kalmalıdır. |
| Yüksek | Anahtar kalıcılığı | Anahtar kullanıma açılmadan önce NVRAM sync ve kalıcı kaydın doğrulanması tamamlanmalıdır. Kayıt hatasında yalnız RAM'de kalan bir anahtarın operatöre kalıcıymış gibi sunulması önlenmelidir. |
| Yüksek | Anahtarın konsola otomatik basılması | Önceki hassas log temizliği ve henüz cihaz başına anahtar dağıtımı yapılamaması kararıyla uyumsuzdur. Yetkili anahtar teslimi/kurtarma kanalı ayrıca kararlaştırılmalıdır. Boot loguna otomatik anahtar basılması önerilmez. |
| Yüksek | `shared_key` alanını yerinde büyütmek | AGENTS.md yeni NVRAM alanlarının kuyruktan eklenmesini ister. Yerinde büyütme sonraki alanları kaydırır. Mevcut schema/length yükleme kontrolleri eski görüntüyü reddedip defaults'a geçebilir; config sıfırlanabilir. Anahtar alanı kuyruktan eklenmeli ve mevcut config değerlerini koruyan dönüşüm testi hazırlanmalıdır. Saha cihazı olmaması mevcut geliştirme config'inin kaybını otomatik olarak kabul edilebilir yapmaz. |
| Orta | Paket başına MAC yok | HELLO sertleştirmesi pasif yakalama/replay ve izinsiz yeni oturum açmayı ele alır. TCP üzerinde aktif müdahale yetkili oturumun DATA/ABORT/REBOOT akışını etkileyebilir. ECDSA kurulum bütünlüğü ayrı korumadır; bu işlemleri yetkilendirmez. HELLO ile sınırlı kapsam kabul edilecekse kalan risk açıkça yazılmalıdır. |
| Orta | `file_hash` dosyayı bağlar iddiası | PC aracı yalnız ilk en çok 1024 baytın CRC'sini kullanır. Bu alanı HMAC'e katmak alan değiştirmeyi engeller, bütün dosyanın kriptografik kimliğini sağlamaz. Aynı başlangıcı/boyutu olan farklı paketler ayrışmayabilir. Resume kapsamı doğru tarif edilmelidir. |
| Orta | Eski istemci geçişi | Eski CRC/XOR doğrulamasını kabul eden geçiş modu açığı açık bırakır. v1 HELLO reddi ve cihaz/PC aracının birlikte güncellenmesi önerilir. |
| Orta | Deneme kilidi | Hata sayacı TCP kopuşunda sıfırlanmamalıdır; yeni bağlantı ile kilit aşılamamalıdır. CHALLENGE yoğunluğu, süre dolumu ve kilit sırasında davranış test edilmelidir. Kilit, güçlü MAC'in yerine geçmez. |
| Düşük | Güncellik ve test kaynağı | "RNG uygulamada kullanılmıyor" ifadesi eskidir; BSP random modülü artık vardır. HMAC-SHA256 test vektörleri için RFC 4231 kullanılmalıdır. "Dakikalar içinde kırılır" ve "~10 satır" iddiaları ölçülmüş efor/performans kanıtı değildir; kesin değer olarak kullanılmamalıdır. |

## Mevcut altyapı

Kardeş repo `smart-breaker-bootloader/Application/libs/libsha256/` altında
`sha256.c/.h` ve `hmac_sha256.c/.h` gerçekten bulunmaktadır. HMAC kaynağı
incelenmiştir; stack üzerinde sabit tamponlar kullanır. Bu inceleme kripto
doğrulama testinin yerine geçmez. Taşınırsa RFC 4231 vektörleri, projenin
zorunlu uyarı bayrakları ve hedef derlemesiyle doğrulanmalıdır.

Gerçek `raw_tcp_fw_update.c` ve `efw_crc.c` ile mevcut 9 host kontrolü replay,
hash değiştirme ve ele geçirilmiş token'dan yeni boyut token'ı üretme açığını
kanıtlar. Bu kontrollerin başarılı olması mevcut protokolün güvenli olduğu
anlamına gelmez; düzeltme sonrası açık kabulünü bekleyen testler ret
beklentisine çevrilmelidir.

## Kabul ölçütleri

Planın mevcut T1–T7 kontrollerine aşağıdakiler eklenmelidir:

- Aynı bağlantıda ve yeniden bağlantıda replay reddedilmelidir.
- Yeni challenge eski nonce'u geçersiz kılmalıdır; nonce kullanımı tüketilmelidir.
- Size, file hash ve MAC baytı değişiklikleri reddedilmelidir.
- RNG fallback, anahtar üretiminde kabul edilmemelidir; kayıt hatasında
  anahtar kalıcı kabul edilmemelidir.
- Config korunarak eski schema dönüşümü ve tekrar boot sınanmalıdır.
- Auth kilidi reconnect ile aşılamamalıdır.
- Resume, bozuk/eksik paket ve gerçek cihaz güncellemesi doğrulanmalıdır.
- Paket MAC'i kapsam dışında kalacaksa aktif TCP müdahale riski kapanmış
  olarak raporlanmamalıdır.

## Kullanıcı kararları

Kullanıcı cihaz başına anahtar modelinin şimdilik uygulanmamasını istemiştir.
Plan 0.1 içindeki cihaz başına RNG anahtarı, ilk boot konsol çıktısı ve bu
anahtar için NVRAM değişikliği mevcut uygulama kapsamından çıkarılmıştır.
Bulgular tablosundaki bu maddeler plan 0.1'in değerlendirmesidir; yeni bir
cihaz başına anahtar modeli kurma talimatı değildir.

Sade alternatif olarak 16 baytlık tek ortak RFWU ürün anahtarı ile
challenge-response ve 16 bayt HMAC etiketi önerilir. Mevcut public SMAR/32 bit
anahtardan daha uzun bir değer türetmek yeni gizlilik sağlamaz; bağımsız güçlü
bir ürün anahtarı seçilmelidir. Anahtar gizli build/operatör girdisi olarak
saklanmalı, Git'e veya loglara yazılmamalıdır. Ürün anahtarı firmware'de
bulunacağından bir cihazdan çıkarılması tüm aynı anahtarlı cihazları etkiler;
cihaz başına izolasyon sağlandığı iddia edilmemelidir.

Bu alternatif ilk boot anahtar üretimi, cihaz başına anahtar teslimi ve
RFWU anahtarı için NVRAM şema değişikliği gerektirmez. Nonce'un tek kullanım,
bağlantı ömrü ve yeterli uzunluk kontrolleri korunmalıdır. Ortak anahtarın
seçimi, v1 reddi ve aktif ağ müdahalesi kapsamı uygulama öncesi açık karardır. Bu rapor bir protokol değişikliği
onayı değildir. Lifetime mevcut NVRAM alanında tutulacaktır; ayrı flash veya
eski lifetime değerini taşıyan bir işlem eklenmeyecektir.

## Kaynaklar

- [RFC 2104 §5: HMAC çıktı kısaltma](https://www.rfc-editor.org/rfc/rfc2104.html#section-5)
- [RFC 4231: HMAC-SHA256 test vektörleri](https://www.rfc-editor.org/rfc/rfc4231.html)
- Yerel kaynaklar: `Application/web-server/raw_tcp_fw_update.c/.h`,
  `tools/fw_update_tcp.py`, `Application/bsp/bsp_random.c/.h`,
  `Application/nvram.c`, `Application/types.h`, `AGENTS.md`.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 03.10.2026 | 1.0 | İlk kaynak karşılaştırması, güvenlik sınırları ve uygulama öncesi düzeltmeler |
| 03.10.2026 | 1.1 | Cihaz başına anahtar kullanıcı kararıyla kapsamdan çıkarıldı; ortak ürün anahtarı alternatifi ve sınırı belirtildi |
