# Web kaynaklarını firmware'e gömme

Sürüm: 1.1
Tarih: 2026-10-11

## Amaç

HTML, CSS ve JavaScript kaynaklarını küçültüp gzip verisi olarak C dizisine
dönüştürmek. Kaynak okunabilir tutulur; küçültme yalnız build (derleme)
sırasında uygulanır.

## Ön koşul

- Python 3 ve Node.js 18 veya üzeri komut satırında bulunmalıdır.
- İlk kurulumda depo kökünden aşağıdaki komut çalıştırılmalıdır:

```text
npm ci --prefix web-page/tools --ignore-scripts --no-audit --no-fund
```

Terser 5.51.2, Acorn 8.19.0 ve geçişli bağımlılıklar package-lock.json
ile sabitlenmiştir.
node_modules Git'e eklenmez. Normal build sırasında paket indirilmez;
kurulum tamamlandıktan sonra küçültme çevrimdışı çalışır.

## Adımlar

Depo kökünden:

```text
python web-page/tools/html_to_c.py index.html -o Application/web-server/ --minify --gzip
python web-page/tools/html_to_c.py fw_update.html -o Application/web-server/ --minify --gzip
```

CubeIDE Debug/Release pre-build adımları aynı converter'ı çağırır.
Komutlar `&&` ile bağlıdır; ilk başarısızlıkta devam edilmez. `.cproject`
build ayarlarının kaynağıdır; eski üretilmiş makefile kullanılıyorsa IDE'de
build dosyaları yeniden oluşturulmalıdır.

## Kurallar

- Global/inline handler isimleri ve JSON property (alan) isimleri korunmalıdır.
- Kaynak I18N sözlüğü okunabilir kalmalıdır. Build çıktısında anahtarlar
  bir kez ve iki dilin metinleri ayraçlı tablo olarak saklanır; sayfa
  başlatılırken aynı I18N.en/tr sözlükleri yeniden oluşturulur.
- Kompakt tablo yalnız aynı anahtar sırasına sahip iki literal (sabit)
  metin sözlüğüne uygulanmalıdır. Eksik/farklı anahtar, getter, fonksiyon
  veya güvenli ayraç bulunamaması halinde özgün sözlük korunmalıdır.
- Minifier yalnız yerel değişken isimlerini kısaltmalı; unsafe (davranış
  varsayımı yapan) optimizasyonlar kapalı kalmalıdır.
- Eksik Node/Terser veya geçersiz JavaScript sessiz fallback ile
  geçilmemelidir. Dönüştürme başarısız olmalı, son geçerli C çıktısı korunmalıdır.
- Gzip mtime=0 ile üretilmelidir. Aynı içerik çıktısı yeniden yazılmamalıdır.
- İstatistikler UTF-8 byte sayısını göstermelidir; karakter sayısı byte
  sayısı olarak etiketlenmemelidir.
- Yeni minifier sürümü package/lock dosyaları ve ilgili testlerle birlikte
  güncellenmelidir.

## Doğrulama

```text
make -C test/integration/web_navigation run
```

Bu paket converter'ın gerçek girişini, literal (metin sabiti), regex ve
nested template (iç içe şablon) korunmasını, hatalı JavaScript reddini,
deterministik üretimi ve son geçerli çıktının korunmasını sınar.
Kaynak/gömülü sayfalar için navigation, TR/EN, RF kayıt, config validation
(doğrulama), login ve firmware update kontrolleri de çalışır.
Kompakt dil testleri boş metin, Unicode, ayraç çakışması, dinamik anahtar,
aynı tanımdaki diğer değişkenler, desteklenmeyen sözlük
ve iki sayfanın tam kaynak/gzip sözlük eşitliğini sınar.

UI sürümü web-page/VERSION içinde tutulmalıdır. Build sırasında tarih
eklemek yalnız açık `--version auto` seçeneğiyle yapılır.

## Değişiklik geçmişi

| Sürüm | Tarih | Değişiklik |
|---|---|---|
| 1.0 | 2026-10-11 | Sabitlenmiş parser minifier, kurulum ve doğrulama yolu |
| 1.1 | 2026-10-11 | Build çıktısında kompakt I18N ve kayıpsız sözlük testleri |
