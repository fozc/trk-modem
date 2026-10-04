# Geliştirme anahtarları

## Amaç

Bu klasör, geliştirme sırasında kullanılan firmware imzalama, şifreleme
ve RFWU anahtarlarını içerir.

## Kullanım yeri

| Dosya | Kullanım |
|---|---|
| `private_key.pem` | `tools/bin2efw.py` ile EFW paketinin ve firmware kimliğinin ECDSA imzası |
| `aes_key.bin` | Release paketleme sırasında firmware payload (veri içeriği) şifrelemesi |
| `rfwu_key.bin` | RFWU kimlik doğrulaması için ortak ürün anahtarı |
| `ecdsa_public_key.h` | İmzalama anahtarının public key (açık anahtar) karşılığı |
| `aes_product_key.h` | Cihaz tarafında kullanılan AES anahtarının header karşılığı |

CubeIDE Release post-build (derleme sonrası) adımı ilk iki dosyayı kullanır.
RFWU header dosyası temiz checkout (repo kopyası) sonrasında
`python tools/rfwu_key.py` ile mevcut anahtardan oluşturulur.
`Application/rfwu_product_key.h` Git dışında kalır.

## Geçici karar

**[KARAR: 2026-10-04]** Kullanıcı talebiyle mevcut üç anahtar dosyası ana
repoda takip edilir. Bu karar geliştirme dönemi içindir. Bu anahtarlar
üretim anahtarı olarak kabul edilmez. Dosyaların daha sonra silinmesi,
anahtarları Git geçmişinden veya mevcut klonlardan kaldırmaz.

## Saha öncesi kurallar

- Yeni ECDSA, AES ve RFWU üretim anahtarları oluşturulmalıdır.
- Bootloader ve uygulamadaki ilgili anahtar karşılıkları birlikte
  güncellenmelidir. Public key ve AES header dosyalarının yeni anahtarlarla
  eşleştiği doğrulanmalıdır.
- Yeni anahtarlarla EFW paketleme, bootloader doğrulaması ve RFWU
  kimlik doğrulaması cihaz üzerinde test edilmelidir.
- Üretim anahtarları Git dışında tutulmalı, şifreli yedeği alınmalı ve
  geri yükleme işlemi doğrulanmalıdır.
- Bu geçici anahtarların `.gitignore` istisnaları saha öncesinde
  kaldırılmalıdır. Eski anahtarlar üretim cihazlarında kullanılmamalıdır.

## Değişiklik kaydı

| Tarih | Değişiklik |
|---|---|
| 2026-10-04 | Geçici Git takibi kararı ve saha öncesi anahtar değiştirme adımları kaydedildi. |
