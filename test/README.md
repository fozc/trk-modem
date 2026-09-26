# Test Altyapısı

**Sürüm:** 1.0
**Tarih:** 2026-09-25

**Amaç:** Firmware modüllerinin host üzerinde hızlı ve tekrarlanabilir biçimde
doğrulanmasını sağlayan ortak test giriş noktasını açıklar.

**Kapsam:** Ceedling birim testleri, host integration testleri (birlikte çalışma
testleri) ve yalnızca hedef cihazda çalışan testlerin dizin düzenini kapsar.

## Ön koşul

- Ruby ve Ceedling 1.0.1 kurulu olmalıdır.
- Host C derleyicisi ve GNU Make komut satırında bulunmalıdır.
- Web arayüzü integration testi için Node.js bulunmalıdır.
- Coverage (kapsam) raporu için `gcov` ve `gcovr` bulunmalıdır.

## Dizin düzeni

| Dizin | İçerik |
|---|---|
| `unit/` | Unity ve CMock kullanan hızlı, yalıtılmış Ceedling testleri |
| `integration/` | Gerçek üretim modüllerini birlikte derleyen host senaryoları |
| `support/` | Birden fazla Ceedling testinin kullandığı ortak yardımcılar |
| `target/` | STM32 üzerinde çalıştırılan ve host koşusuna katılmayan testler |
| `system/` | Ağ veya çalışan cihaz gerektiren uçtan uca test araçları |
| `fixtures/` | Testlerde kullanılan sabit firmware ve veri örnekleri |
| `scripts/` | Platformdan bağımsız test yardımcıları |

## Adımlar

Tüm host testleri repo kökünden şu komutla çalıştırılır:

```text
ruby test/run_all.rb
```

Yalnız Ceedling testleri için:

```text
ruby test/run_all.rb unit
```

Yalnız integration testleri için:

```text
ruby test/run_all.rb integration
```

Coverage raporu için:

```text
ruby test/run_all.rb coverage
```

Üretilen test çıktıları şu komutla temizlenir:

```text
ruby test/run_all.rb clean
```

## Yeni test ekleme

Saf hesaplama, codec veya tek modüllü davranış `unit/<modül>/` altında Unity
testi olarak eklenmelidir. Donanım ve komşu modül çağrıları CMock ya da küçük
bir fake (davranışlı test çifti) ile ayrılmalıdır.

Gerçek Contiki çekirdeğini, kalıcı bellek görüntüsünü veya birden fazla üretim
modülünü birlikte kullanan senaryolar `integration/<modül>/` altında
tutulmalıdır. Her integration paketi `make run` hedefi sağlamalıdır.

Hedef MCU, interrupt veya fiziksel çevre birimi gerektiren testler `target/`
altında tutulmalıdır. Bu testler host test sonucu olarak gösterilmemelidir.

Ağdaki gerçek cihaza istek gönderen testler `system/` altında tutulmalıdır.
Bu testler hedef adresi açıkça verilmeden merkezi host koşusuna
eklenmemelidir.

## Doğrulama

Başarılı koşuda merkezi komut sıfır durum koduyla biter. Ceedling JUnit XML
çıktısı `test/build/ceedling/artifacts/test/` altında oluşturulur. Coverage
çıktıları `test/build/ceedling/artifacts/gcov/` altında oluşturulur.

## Sık hatalar

- `ceedling` bulunamazsa Ruby gem kurulum dizini `PATH` değişkenine
  eklenmelidir.
- Eski object dosyaları kuşkulu sonuç üretiyorsa ilgili integration paketinin
  `build/` dizini temizlenmelidir.
- `gcovr` bulunamazsa normal test koşusu kullanılabilir; yalnız coverage
  komutu etkilenir.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-25 | 1.0 | İlk merkezi Ceedling ve integration test yapısı |
