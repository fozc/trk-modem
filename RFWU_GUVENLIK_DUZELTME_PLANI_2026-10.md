# RFWU Güvenlik Düzeltme Planı

| Belge künyesi | Değer |
|---|---|
| Sürüm | 0.3 — uygulandı; cihaz kabulü bekliyor |
| Tarih | 03.10.2026 |
| İnceleme tabanı | `622f050`; değişiklikler çalışma ağacında |
| İlgili değerlendirme | `RFWU_GUVENLIK_PLANI_DEGERLENDIRME_2026-10-03.md` |
| Durum | Host ve ARM Release doğrulandı; gerçek cihaz kabulü yapılmadı |

## Amaç

RFWU HELLO kimlik doğrulamasındaki CRC/XOR token türetme ve replay
(yakalanmış isteği yeniden kullanma) açığını, mevcut altyapıyı kullanarak
sade bir challenge-response (cihazın isteğine anahtarlı yanıt) ile kapatmak.

## Kapsam

Cihaz RFWU parser, BSP donanım RNG erişimi, mevcut SHA-256/HMAC kodu,
Python güncelleme aracı, ortak ürün anahtarı ve kalıcı testler kapsamdadır.
Cihaz başına anahtar dağıtımı, NVRAM schema/layout değişikliği, TLS ve
paket başına MAC bu işin kapsamı dışındadır. Lifetime mevcut NVRAM
alanında kalır; bu plan lifetime'ı başka bir alana taşımaz.

## Kullanım yeri

Firmware ve PC aracının birlikte güncellenmesinde uygulanacak iş listesidir.
Kullanıcı kararları: cihaz başına anahtar şimdilik kullanılmayacaktır;
RFWU düzeltmesi sade tutulacaktır; firmware ve PC aracı birlikte değişecektir.

## Teyit edilmiş sorun

Mevcut token `CRC32(key) XOR total_size` biçimindedir. Ele geçirilen
HELLO'dan `eski_token XOR eski_boyut XOR yeni_boyut` ile yeni token
üretilebilir. Gerçek parser host testinde replay, hash değiştirme ve
anahtar bilinmeden yeni boyut token'ı üretme kabul edilmiştir. Sadece
anahtarı değiştirmek veya sequence denetimi eklemek bu formülü düzeltmez.

## Tasarım kararları ve kurallar

### Ortak ürün anahtarı

- Tek ortak, bağımsız ve rastgele üretilmiş 16 bayt RFWU anahtarı
  kullanılmalıdır. Mevcut public SMAR/32 bit anahtar kullanılmamalıdır.
- Anahtar `keys/rfwu_key.bin` içinde Git dışında saklanmalıdır. Cihaz
  derlemesi için `Application/rfwu_product_key.h` üretilmelidir; bu dosya
  da Git dışında kalmalıdır. İki dosya aynı anahtardan üretilmelidir.
- `python tools/rfwu_key.py --create` ilk hazırlamada bir kez kullanılmalıdır.
  Var olan anahtar yeniden üretilmemelidir. Diğer makinelerde aynı ürün
  anahtarı geri yüklenmeli, `python tools/rfwu_key.py` çalıştırılmalıdır.
  Anahtar yoksa normal hazırlama sessizce farklı bir anahtar üretmemelidir.
- Anahtar konsola/loga basılmamalıdır. Anahtar dosyası operatör ve build
  ortamında erişim kontrollü olarak saklanmalı ve yedeklenmelidir.
- İlk boot anahtar üretimi veya NVRAM'e yeni anahtar alanı eklenmemelidir.
  Eski `shared_key` alanı yalnız layout uyumluluğu için korunmalı;
  RFWU v2 kimlik doğrulamasında kullanılmamalıdır.

Ortak anahtar bir cihazdan veya operatör ortamından çıkarılırsa aynı
anahtarlı bütün cihazlar etkilenir. Bu kullanıcı tarafından seçilen model
cihaz başına izolasyon sağlamaz.

### Protokol v2

| Paket | Kod | Payload |
|---|---|---|
| CHALLENGE | `0x07` | Boş |
| CHALLENGE response | `0x84` | 16 bayt nonce |
| HELLO | `0x01` | `total_size` 4 bayt LE + `file_hash` 4 bayt LE + 16 bayt HMAC etiketi |

- Etiket `HMAC-SHA256(key16, "RFWU2" || nonce16 || total_size_LE4 ||
  file_hash_LE4)` çıktısının ilk 16 baytı olmalıdır. Byte dizisi
  firmware ve PC tarafında aynı olmalıdır. MAC karşılaştırması bütün
  etiket baytlarını işlemelidir.
- Header, DATA/FINISH paket biçimi ve taşıma CRC'si korunmalıdır.
  CRC kimlik doğrulama yerine kullanılmamalıdır.
- Eski 12 bayt HELLO reddedilmelidir; zayıf v1 kabul eden geçiş modu
  eklenmemelidir. Firmware ile PC aracının birlikte güncellenmesi gerekir.
- Nonce yalnız aynı TCP bağlantısında, tek HELLO denemesi için ve
  60 saniye boyunca geçerli olmalıdır. Disconnect nonce'u silmelidir.
  Her yeni CHALLENGE önceki nonce ve bağlantı yetkisini geçersiz kılmalıdır.
- CHALLENGE için donanım RNG kullanılmalıdır. BSP'deki mevcut
  noncryptographic availability fallback challenge kaynağı olmamalıdır.
  RNG başarısızsa `RFWU_ERR_RNG (0x08)` dönmelidir; hiçbir HELLO yetkisi
  veya flash işlemi oluşmamalıdır. HTTP login fallback'i değişmemelidir.
- Beş hatalı HELLO sonrası 60 saniye kimlik doğrulama kilidi uygulanmalıdır.
  Hata sayacı/kilit disconnect ile sıfırlanmamalıdır. Sayaç farkları tick
  taşmasına dayanıklı hesaplanmalıdır.
- Doğru HELLO sonrası QUERY/REBOOT/ABORT mevcut yetki kapılarından
  geçmelidir. Resume, size ve file hash eşleşmesiyle korunmalıdır.

### Mevcut altyapının kullanımı

Kardeş bootloader reposundaki SHA-256/HMAC kaynakları uyarlanmalıdır;
aynı iş için yeni kriptografi algoritması tasarlanmamalıdır. Kaynaklar
`Application/libs/sha256.c/.h` ve `hmac_sha256.c/.h` altında tutulmalıdır.
Donanım RNG okuması mevcut BSP random modülünden sağlanmalıdır.
Ek process, arka plan işi veya yeni depolama katmanı kurulmayacaktır.

### PC aracı ve web arayüzü

`tools/fw_update_tcp.py` içindeki normal transfer, force restart,
QUERY için ilk yetkilendirme ve ABORT için ilk yetkilendirme yolları
önce CHALLENGE almalı, ardından v2 HELLO göndermelidir. Her HELLO için
yeni challenge alınmalıdır; token önceden hesaplanıp tekrar kullanılmamalıdır.
Anahtar alanı 32 hex karakter kabul etmeli, değeri maskelemeli ve yerel
`keys/rfwu_key.bin` varsa onu yüklemelidir. Anahtar/token loglanmamalıdır.

`web-page/fw_update.html` RFWU raw TCP kullanmaz. Admin oturumuyla
HTTP `/fw_*` uçlarını kullanır; `fetch()` çağrıları bunun kanıtıdır.
Bu RFWU işi web sayfasına ortak ürün anahtarı veya HMAC kodu eklemeyi
gerektirmez. Web sayfasına gizli ürün anahtarı konulmamalıdır. HTTP update
kimlik modeli ayrı bir güvenlik konusudur; RFWU düzeltildi diye onun
kapsamının kapandığı söylenmemelidir.

## Güvenlik sınırları

HELLO doğrulaması pasif yakalama/replay ve anahtar bilinmeden yeni RFWU
oturumu açmayı ele alır. Paket başına MAC/TLS olmadığından aktif TCP
müdahalesi yetkilendirilmiş oturumun DATA/ABORT/REBOOT akışını etkileyebilir.
Firmware ECDSA kurulum kontrolü ayrı korumadır; bu komutları yetkilendirmez.

File hash mevcut istemcide ilk en çok 1024 baytın CRC'sidir. HMAC bu alanın
değiştirilmesini engeller; bütün dosyanın kriptografik kimliği değildir.
Bu işte mevcut resume kimliği korunacaktır.

## İş listesi

| İş | Durum | Kabul ölçütü |
|---|---|---|
| SHA-256/HMAC kaynaklarını mevcut koddan uyarlama | Uygulandı; vektör/uyarı kontrolü geçti | RFC 4231 vektörleri ve zorunlu uyarı bayrakları geçer |
| Ortak gizli anahtar ve header hazırlama aracı | Uygulandı; yerel anahtar/header Git dışında | Gizli dosyalar Git dışındadır; var olan anahtar yeniden oluşturulmaz |
| BSP hardware-only RNG API | Uygulandı; host kontrolü geçti | HAL hatası fallback üretmeden başarısız olur; HTTP davranışı korunur |
| Parser challenge/HELLO v2, süre/tek kullanım ve kilit | Uygulandı; host kontrolü geçti | Aşağıdaki ret/kabul senaryoları geçer |
| PC transfer/QUERY/ABORT/force restart uyumu | Uygulandı; host kontrolü geçti | Gerçek PC yardımcıları cihaz parser'ıyla aynı HMAC üretir |
| Kalıcı testler ve merkezi runner | Tamamlandı | Eski açık testleri ret beklentisine çevrilir; tam host koşusu geçer |
| ARM Release ve paket öz denetimi | Geçti | Derleme/link, içerik eşitliği ve ECDSA öz denetimi geçer |
| Cihaz kabulü | Yapılmadı | Gerçek RNG, resume, güncelleme ve reboot uçtan uca sınanır |

## Kabul testleri

1. RFC 4231 HMAC-SHA256 vektörleri, boş ve blok sınırı SHA-256 girdileri
   standart sonuçlarla eşleşmelidir.
2. Eski XOR HELLO, challenge'sız HELLO, yanlış anahtar, değiştirilmiş
   size/hash/MAC ve bozuk paket CRC reddedilmelidir.
3. Doğru fragmented CHALLENGE/HELLO kabul edilmelidir.
4. Aynı bağlantı replay, disconnect sonrası replay, yeni challenge ile
   eski token, nonce süre dolumu ve nonce'un yeniden kullanımı reddedilmelidir.
5. RNG hatasında challenge/flash işlemi olmamalı; HTTP login fallback'i
   çalışmaya devam etmelidir.
6. Auth kilidi reconnect ile aşılamamalı; süre sonunda tekrar doğru akış
   kabul edilmelidir. Tick taşması sınanmalıdır.
7. Resume, duplicate DATA, gap/overflow ve flash hata davranışları korunmalıdır.
8. PC normal transfer, force restart, QUERY ve ABORT yollarında yeni
   challenge alınması sınanmalıdır.
9. Gizli anahtar/header Git tarafından yok sayılmalı; loglar anahtar/token
   içermemelidir. Config schema/layout aynı kalmalıdır.
10. Host başarısı cihaz kabulünün yerine geçmemelidir.

## Uygulama ve review sonucu

Yerel koşuda 302 Ceedling testi ve 9 integration paketi geçmiştir. RFWU
paketi 25 kontrol içerir. ARM Release derlemesi ve paket içerik/ECDSA
öz denetimi başarılıdır; mevcut legacy uyarılar warning-free üretim
şartının sağlandığı anlamına gelmez.

Review sırasında `bsp_tick()` yerine projenin gerçek `bsp_get_tick()` API'si
kullanılmış; test double aynı adla düzeltilmiştir. ctypes shared library
ABI'si Python ile eşleştirilmiştir; Linux CI gerçekten çalıştırılmamıştır.
PC GUI yeni transferde eski yetki bayrağını sıfırlar. Yeni SHA/HMAC
kaynakları için ignored yerel Release kaynak/nesne listeleri güncellenmiştir;
CubeIDE bu kaynakları Application/libs dizininden keşfeder.

Web güncelleme sayfası HTTP `/s`, `/d`, `/f` ve `/fw_apply` akışını kullanır;
bu işte web sayfasına ürün anahtarı/HMAC eklenmemiştir. Cihaza yükleme,
gerçek RNG hata enjeksiyonu ve bootloader ile uçtan uca kurulum yapılmadı.
Değişiklikler kullanıcı onayıyla commit kapsamına alınmıştır. Push yapılmamıştır.

Loglar: `test/build/production-audit-2026-10-03/rfwu-v2-host.log`,
`rfwu-v2-release.log` ve `rfwu-v2-abi.log`.

## Kaynaklar

- [RFC 2104 §5: HMAC çıktı uzunluğu](https://www.rfc-editor.org/rfc/rfc2104.html#section-5)
- [RFC 4231: HMAC-SHA256 test vektörleri](https://www.rfc-editor.org/rfc/rfc4231.html)
- `Application/web-server/raw_tcp_fw_update.c/.h`, `Application/bsp/bsp_random.c/.h`
- `tools/fw_update_tcp.py`, `web-page/fw_update.html`, `AGENTS.md`

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 03.10.2026 | 0.1 | İlk taslak; cihaz başına anahtar ve 32 bit etiket/nonce önerisi |
| 03.10.2026 | 0.2 | Kullanıcı kararlarıyla ortak anahtar, 128 bit etiket/nonce, sade uygulama, web kapsamı ve kabul testleri |
| 03.10.2026 | 0.3 | Uygulama, host/ARM doğrulaması, review düzeltmeleri ve cihaz kabul sınırı |
