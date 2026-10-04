# Embedded C++ Alt Kümesi

Sürüm: 1.1.0 · Tarih: 2026-10-04.

## CP00. Amaç ve kapsam

Heap kullanmadan C++ firmware geliştirme. Dil/compiler seçimi proje
A10/A11'den alınır. Heap ve runtime sınırları G02'de tanımlıdır.

## CP01. Nesne ve kaynak ömrü

[ZORUNLU] RAII (kaynağın nesne ömrüyle yönetilmesi) destructor'ı
çağrıldığı bağlamda sınırlı süreli ve exception üretmeyen biçimde
tasarlanmalıdır. Kaynak sahibi sınıfta copy/move politikası açık
seçilmelidir; her sınıfın kopyalanması otomatik yasaklanmamalıdır.

[ZORUNLU] Callback, span ve string_view için gösterilen verinin
yaşam süresi belirlenmelidir. String_view null-terminated sayılmamalıdır.
C++20 span erişiminin otomatik bounds check yaptığı varsayılmamalıdır.

[ZORUNLU] Array/optional/variant nesnelerinin depolama yeri tanımlandıkları
yere bağlı olarak değerlendirilmelidir; daima stack üzerinde oldukları
söylenmemelidir. İçerilen türün dolaylı ayırmaları ayrıca incelenmelidir.
Optional erişimi has_value kontrolü sonrası yapılmalıdır.

## CP02. Başlatma ve runtime

[ZORUNLU] Global/static başlatma sırası, ISR'nin erişime başlayacağı an,
destructor ve runtime bağımlılıkları belirlenmelidir.
Constexpr/constinit uygun yerde tercih edilmelidir.

[ZORUNLU] -fno-threadsafe-statics yalnızca eşzamanlı ilk başlatma
olmadığı kanıtlandığında seçilmelidir. RTOS öncesi başlatma tek başına
yeterli olmayabilir; ISR erişimi de kapsanmalıdır.

[ZORUNLU] Placement new için D02'deki ömür/hizalama koşulları
uygulanmalıdır. Global operator new rastgele override edilmemelidir.
RTTI/exception kapalı ayarlarda standart kütüphane yollarının runtime
gereksinimleri hedef link çıktısında incelenmelidir.

## CP03. Atomikler ve C arayüzü

[ZORUNLU] Önce [atomic-isr.md S01](atomic-isr.md) erişim ve koruma
seçimi uygulanmalıdır. Atomic semantiği gereken C++20 erişimlerinde
std::atomic<T> ve açık memory order kullanılmalıdır.
S01–S05 sahiplik ve süre kuralları korunmalıdır.
ISR türü için std::atomic<T>::is_always_lock_free kontrol edilmelidir.
C _Atomic ile C++ std::atomic nesne yerleşimlerinin uyumu varsayılmamalıdır.

[ZORUNLU] C ABI sınırında extern "C" fonksiyonları kullanılmalı;
atomik nesnenin erişimi tek dildeki uygulama arayüzü üzerinden
yapılmalıdır. ISR wrapper'ın yönlendirdiği nesne, kesme açılmadan
hazır olmalı ve kesme kapatılmadan yok edilmemelidir.

## CP04. Hizalama, dil ve performans

[ZORUNLU] Nesneye uygulanan alignas ile türün alignof sonucu
karıştırılmamalıdır. Doğru tür örneği
[alignment.cpp.txt](../../../examples/alignment.cpp.txt) içindedir.
Örnekteki 32 değeri hedef DMA/cache gereksinimi değildir.

[ZORUNLU] Cast, aliasing ve lifetime kuralları açısından
gerekçelendirilmelidir. Reinterpret_cast doğrulama yerine geçmez.
Public C++ header'ları C derleyicisine zorla verilmemelidir.

[ÖNERİ] Virtual/CRTP, lookup table ve branch hint seçimi ölçüme
dayandırılmalıdır. Özel bir soyutlama varsayılan zorunluluk olmamalıdır.
Nodiscard hata sonucunun gözden kaçmasını azaltmak için kullanılabilir.

## CP05. Kaynaklar ve değişiklik

- [C++ alignof](https://eel.is/c++draft/expr.alignof)
- [C++ lock-free özellikleri](https://eel.is/c++draft/atomics.lockfree)

Bu çevrimiçi taslaklar güncellenebilir; hedef C++20 seçimi A10'dadır.

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Yaşam süresi, başlatma ve tür hizalama düzeltmeleri |
| 2026-10-04 | 1.1.0 | CP03: atomic işlemden önce erişim ve koruma seçimi |
