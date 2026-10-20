# Cihaz fault kabulü

**Amaç:** Gerçek Modbus timeout/toparlanma girişini ve HardFault sonrası
kalıcı kayıt zincirini güvenli bench üzerinde doğrulamak.

**Kullanım yeri:** Üretim profilindeki RTU, güvenli ayrılmış çıkışlar,
RTU konsolu ve RS485 adaptörü. Donanım yükleme/reset yetkisi alınmış
olmalıdır. Güncelleme [donanım rehberindeki](../../../doc/HW_TEST_VE_DEBUG_REHBERI.md)
BSL/XMODEM yolu üzerinden yapılmalıdır.

## Kurulum ve çalıştırma

1. Mevcut imaj, ayarlar ve log seviyeleri kaydedilmelidir.
2. ARM toolchain PATH'i tanımlandıktan sonra test paketi hazırlanmalıdır:

```powershell
python test/system/device_fault/build_fixture.py --outdir .temp_files/fault-build
```

Çıktı dizini yeni olmalıdır. Yardımcı, BSP/shell kayıt noktasını ve link
recipe'sini geçici değiştirir; başarıda ve hatada kaynakları `finally`
içinde eski baytlarına döndürür. EFW ve ELF test dizinine kopyalanır.
Paket üretim imajı değildir; `HWTEST FIXTURE ACTIVE` bildirimi taşır.

3. İmzalı `fixture.efw` BSL/XMODEM ile kurulmalı ve test çalıştırılmalıdır:

```powershell
python test/system/device_fault/run_faults.py --console COM18 --rs485 COM10 --outdir .temp_files/fault-run
```

4. Test bitince `make -C Release -B -j8 main-build`, ardından
`make -C Release -j8 all` çalıştırılmalı; yeni üretim EFW kurulmalıdır.
`main-build` tek başına imzalama adımının yerine kullanılmamalıdır.
Üretim ELF'inde `hw_fault_fixture_init`/`__wrap_HAL_*` simgelerinin ve
cihazda `hwtest` komutunun bulunmadığı doğrulanmalıdır. Özgün ayarlar ve
log seviyeleri geri yüklenmelidir. Mevcut kayıtlar topluca silinmemelidir.

## Ölçütler

- `timeout`: UART4 TX DMA tamamlanma IRQ'su engellenir. Gerçek Modbus
  process timer'ı TX timeout üretmeli; gerçek HAL abort sonrasında yeni
  FC03 isteği yanıtlanmalıdır.
- `recover`: Gerçek HAL abort'un yan etkileri uygulanır, dönüş değeri
  bir defa HAL_ERROR olarak enjekte edilir. Gerçek DeInit/Init toparlanma
  yolu çalışmalı, elog sonucu recovered olmalı ve yeni FC03 yanıtlanmalıdır.
- `fail`: Gerçek abort ve DeInit yan etkilerinden sonra hata dönüşleri
  enjekte edilir. Elog failed olmalı; sonraki FC03 yanıtlanmamalı ve
  otomatik MCU reseti olmamalıdır. `resume` yalnız fixture'ın maskelediği
  IRQ'yu açar; başarısız transport'u hazır göstermez.
- `hardfault`: `udf #0` gerçek exception üretmelidir. FORCED/UNDEFINSTR
  görülmeli; watchdog sonrasında cihaz açılmalı ve kalıcı elog içindeki
  PC/LR/CFSR/HFSR dump ile birebir eşleşmelidir.

HAL dönüş enjeksiyonu fiziksel DMA arızası değildir. Başarısızlıkta normal
moda dönüş HardFault/reset sonrasında olur; otomatik toparlanma garantisi
çıkarılmamalıdır. Bu fixture GSM UART1 arızasını enjekte etmez.

## Doğrulama ve kanıt

Host negatif verdict (sonuç kararı) testleri merkezi `fault_log` girişinde
çalışır: `python test/system/device_fault/test_fault_evidence.py`.
Eksik veya uyuşmayan kalıcı kayıt başarı sayılamaz. Ham konsol kaydı,
JSON sonuçları, EFW SHA256 ve kaynak kimliği birlikte saklanmalıdır.
20.10.2026 fiziksel koşu özeti test kapanış raporundadır.
