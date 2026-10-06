# Architecture — Smart Breaker Modem

Sürüm: 1.1.0 · Tarih: 2026-10-04 · Durum: mevcut proje profili; açık kararlar var.

## A00. Amaç ve durum

Mevcut yazılım kararları ve doğrulama yolları. Genel yöntemler skill'lerde
tutulur. Bu belge mevcut kodu değiştirmez ve yeni ürün davranışı
onaylamaz. Kaynak/kullanıcı beyanı, karar kabulü ve hedef ölçümü ayrıdır.
Depo kökü bu paketin bir üst dizinidir.

## A01. İşlev ve kabul ölçütleri

Proje tanımına göre akıllı kesici modem firmware'i; Modbus RTU,
IEC 60870-5-104, SCP, GSM/RF, power-board, BMS ve HTTP işlevleri bulunur.
Kaynak: [mevcut proje tanımı](../CLAUDE.md).
İşlevlere ait birimli kabul ölçütleri görev bazında çıkarılmalıdır.

| ID | Gereksinim | Kabul ölçütü / durum |
|---|---|---|
| A-REQ-01 | Haberleşme ve kesici kontrol davranışı | Protokol belgeleriyle ayrıntılandırılacak |
| A-REQ-02 | Veri kaybı/tepki süresi sınırları | AÇIK; kullanıcı/ürün gereksinimi gerekli |

## A02. Emniyet ve güvenlik

[AÇIK] Kesici çıkışlarının güvenli durumu, tespit+tepki sınırı,
reset/update sırasında izinli davranış, bağımsız koruma ve yeniden
başlatma koşulları ürün sorumlusuyla belirlenmelidir. H02/H03 fiziksel
bilgileri bu kararların girdisidir.

[AÇIK] Uygulanacak standart/sürüm/kapsam ve değerlendirme süreci
belirlenmelidir. Bu paket medikal/otomotiv uygunluk iddiası taşımaz.
Kimlik doğrulama, komut yetkilendirme, servis/debug erişimi, anahtar
saklama ve firmware doğrulama gereksinimleri ayrıca tamamlanmalıdır.

## A03. Modüller ve donanım erişimi

Mevcut proje kararı: Application -> Service/Driver -> HAL/HW.
Doğrudan register erişimi yalnızca Core veya BSP sınırında.
Drivers vendor kodudur. CubeMX üretilmiş kaynak değişikliği yalnızca
USER CODE BEGIN/END alanlarında yapılır; bu kural bütün el yazımı
kaynaklara uygulanmaz. IOC değişikliği ayrı konfigürasyon işlemidir.

| Alan | Sorumluluk |
|---|---|
| Core / Drivers | Başlatma, CubeMX ve HAL/CMSIS |
| contiki-kernel | Cooperative process/protothread |
| Application | Ürün modülleri ve protokol servisleri |
| Application/cslog | Arka plan log kanalı |
| Shell komutları | SHELL_LOG / SHELL_CLOG yanıt kanalı |

Mevcut log sözleşmesi: arka plan için CSLOG/xsprintf; komut yanıtında
SHELL_LOG/SHELL_CLOG. Bare printf/sprintf üretimde kullanılmaz.
Komut yanıtının CSLOG'a yazılması web terminalinde görünmeyebilir.

## A04. Çalışma ve paylaşım modeli

Kaynaklar: [platform-conf.h](../contiki-kernel/platform-conf.h),
[critical.h](../contiki-kernel/sys/critical.h),
[int-master.c](../contiki-kernel/sys/int-master.c).

KODDAN_OKUNDU: _PLATFORM_ değeri _WIN32_ seçilmiş. Bu seçimde
int_master_read_and_disable kesmeleri maskelemez; status_set da
maskeleme yapmaz. critical_enter/exit IRQ koruması olarak kullanılamaz.
S03 yöntemi veya S01 sahiplik tasarımı gerekir.

[AÇIK: A-04] _ARM_CORTEXM_ dalına geçmek tek satırlık güvenli düzeltme
sayılmamalıdır: int_master_enable içinde disable çağrısı ve
read_and_disable ARM dalında eksik dönüş görülüyor. Portun değiştirilmesi
ayrı kapsam, onay ve hedef testi gerektirir.

[AÇIK] Modül bazında ISR/process/DMA veri sahibi, öncelik, çağrı
bağlamı ve reentrancy tablosu henüz tamamlanmadı.

## A05. Build konfigürasyonu ve boot

| Hedef | Aktif linker seçimi | Flash origin |
|---|---|---|
| Debug | STM32U375VETX_FLASH.ld | 0x08000000 |
| Release | STM32U375VETX_BOOT.ld | 0x08014000 |

Kaynak: [.cproject](../.cproject).
[main.c](../Core/Src/main.c) içinde DEBUG tanımlı değilken VTOR,
FLASH_BASE | 0x14000 olarak atanıyor.
Bu kaynak uyumu bootloader handoff'un hedefte sınandığı anlamına gelmez.
Yeni bellek yerleşimi kararı G03 kapsamındadır.

Build sistemi STM32CubeIDE managed build; CMake zorunluluğu yoktur.
Mevcut compiler/kurulum ve build komutu A11'deki kaynaklardan alınır.
Headless CI/Docker işi kullanıcı kararıyla ertelidir.

## A06. Bellek ve kalıcı veri

Mevcut NVRAM ABI korunmalıdır. Kaynaklar:
[types.h](../Application/types.h), [nvram.c](../Application/nvram.c).

KODDAN_OKUNDU: NVRAM_SCHEMA_VERSION 2U; nvram_t için sizeof assert
2476U, crc ofseti 2472U. Header magic/version/length/sequence taşır.
Bunlar kaynak assert değerleridir; bu paket çalışmasında hedef
compiler ile yeniden ölçülmedi.

Mevcut değişiklik kuralı: alanlar crc öncesinde kuyruğa eklenir;
şema sürümü artırılır; hedef sizeof/offsetof kontrolleri güncellenir.
Saha uyumluluğu gerektiğinde migration ve host testi zorunludur.
Saha cihazı olmasa bile eski şemayı reddetme/default davranışı ve
güç kesintisi sonucu test kapsamına alınmalıdır.
CRC ve sequence ile iki kopya seçimi korunmalıdır.

[AÇIK] Saha cihazı durumu, migration/rollback politikası, RAM/stack/Flash
bütçesi, pool/kuyruk kapasitesi ve taşma davranışları tamamlanmalıdır.
Yeni serialization ilkesi mevcut NVRAM ABI'yi sessizce değiştirme izni
vermez.

## A07. Zaman, hata ve toparlanma

[AÇIK] Modül periyotları, deadline/jitter, toplam retry süresi, watchdog
sağlık koşulları, assert/fault çıkışı ve reset döngüsü sınırı
gereksinim ve ölçümle doldurulmalıdır.
Mevcut kodda sayı bulunması onun gereksinim olarak onaylandığını
göstermez.

## A08. Protokol ve update

Mevcut adres sözleşmeleri
[IEC104 rehberinde](../doc/IEC104_YAPILANDIRMA_VE_ADRES_REHBERI.md) ve
[Modbus haritasında](../Application/libmodbusrtu/MODBUS_REGISTER_MAP.md)
tutulur. Save öncesi web kontrolü ve backend parser/setter doğrulaması
birlikte korunmalıdır. Aktif fiderlerde bütün kategoriler, fazlar ve
fiderler karşılaştırılır; Modbus'ta alanın kullandığı bütün register
aralığı dikkate alınır. Sıfır/atanmamış adres sözleşmesi korunur.
Hata ilgili alanlarda anlamlı biçimde gösterilir. Web kontrolü backend
doğrulamasının yerine geçmez. Kapalı fiderleri kapsama alma kararı açık
kalmıştır; kapsam kendiliğinden genişletilmemelidir.
Kaynak: [üretim raporu §9.1](../URETIM_HAZIRLIK_RAPORU_2026-10.md).

RFWU v2 uygulanmıştır; cihaz başı anahtar kullanıcı kararıyla ertelidir.
Güvenli nonce yolundaki RNG hatası BSP erişilebilirlik fallback'i ile
aşılmamalıdır. Kaynak: üretim raporu §9.1 ve
[güncel üretim raporu §10.3](../URETIM_HAZIRLIK_RAPORU_2026-10.md#103-korunan-kararlar-ve-taşınan-kanıt-özeti).
Yeni protokol davranışı için D06/G03 uygulanır.

## A09. Kaynak ve değişiklik sınırı

Mevcut kullanıcı değişiklikleri korunur. G03 onay sınırı geçerlidir.
Üretilmiş/vendor kod yamaları gerekçe, sürüm, kaynak ve tekrar uygulama
yöntemiyle ayrı ele alınır. Bu paketin proje kararlarına kök AGENTS.md
üzerinden başvurulur; taşınabilir kurallar skill'lerde tutulur.

### A09.1. Korunacak kullanıcı kararları

| Karar | Kapsam ve kaynak |
|---|---|
| Dummy üreticiler korunur | Gerçek veri/altyapı henüz yoktur. Dummy kaldırılmaz; veri kaynağı bulgusu tamamlandı sayılmaz. Üretim raporu §9.3/§9.29. |
| #if 0 blokları korunur | Kullanıcı açıkça kaldırılmasını istemedikçe kod temizliği kapsamında silinmez. Kök AGENTS.md. |
| Contiki ve ST/CubeMX uyarı düzeltmeleri kapsam dışıdır | 8 Contiki, 5 ST driver, 1 Core tanısı kullanıcı kararıyla bırakılmıştır. Uyarı susturma veya vendor yamasıyla gizlenmez. [güncel warning kararı §10.3](../URETIM_HAZIRLIK_RAPORU_2026-10.md#103-korunan-kararlar-ve-taşınan-kanıt-özeti), kapsam dışındaki 14 uyarı. |
| Lifetime mevcut NVRAM alanında kalır | 25 saatte bir ve mevcut periyodik reset öncesinde sync vardır. Ayrı Flash alanı/migration veya her reset için flush yetkisi verilmemiştir. Üretim raporu §9.1. |
| Random altyapısı BSP'dedir | HTTP token doğrudan bsp_random_word kullanır; fallback bu API içindedir. HAL başlangıcı main/CubeMX'tedir. Erişilebilirlik fallback'i güvenli entropy sayılmaz. Üretim raporu §9.1. |
| Geliştirme anahtarları şimdilik Git'te kalır | Anahtarlar otomatik silinmez/taşınmaz veya geçmiş yeniden yazılmaz. Saha öncesi yeni üretim anahtarları hazırlanıp doğrulanmalıdır. [keys/README.md](../keys/README.md), üretim raporu §9.5. |
| RF modeli son dokümana göre kurulur | 05.10.2026 kullanıcı kararı: sahada cihaz yoktur; eski RF model/JSON/API uyumluluğu gerekmez. R1 EUI-64/signed RSSI/float modeline geçilmiş; sentetik RF örnekleri gerçek cache/envanterden ayrılmıştır. Güncel protokolün olay/alarm geçmişi yükümlülüğü sürer. Üretim raporu §10.12. |
| Tam RF olay günlüğü ayrı 8 KB alandadır | 05.10.2026 kullanıcı kararı: kalıcı/geçici arıza alanları korunur; 60 B paket için spi_flash_log kullanılır. Yeni bölge 0x232000–0x233FFF; 64 B entry ile en çok 128 kayıt, sektör silinirken diğer 64 kayıt korunur. NVRAM düzeni değişmez. Üretim raporu §10.13. |
| RF arızaları mevcut listelere de aktarılır | 05.10.2026 kullanıcı kararı: olay 1/7 kalıcı, 3 geçici; mevcut süre uint32_t olur. fault_log_t 20 B ve fider görüntüsü 1852 B; alan adresleri korunur, FAULT_LOG_SCHEMA_VERSION 2 olur. head=tail boş kabul edilir; pending=100 bildirimi dolu halkayı ayırır. Üretim raporu §10.15. |
| Mevcut reset/retry altyapısı kullanılır | Modbus reset yanıtı için mevcut 1000 ms gecikmeli reset korunur. Kullanıcının 05.10.2026 R1 uygulama kararı RF için önceki iki retry seçimini güncellemiştir: komut bazlı doküman varsayılanları, timeout'ta aynı SEQ, izinli ERROR sonrası yeni SEQ ve tek ortak bütçe kullanılır. Yeni bağımsız katman eklenmez. Üretim raporu §9.18/§10.10. |
| IEC104 canlı veri zamanı RTU alım saatidir | 06.10.2026 kullanıcı geçici kararı: LIVE_DATA kabulünde yerel RTC bir kez saklanır; geçersiz RTC zaman IV=1 olur. Sorgu eski örneği yeniden zamanlandırmaz. Bu ayırıcı ölçüm zamanı değildir; BQ-15 teyidi beklenir. Kaynak olay zamanı ve NVRAM düzeni değişmez. |
| Modbus RF akımı ve iletişimi ayrıdır | 06.10.2026 kullanıcı kararı: mevcut anlık akım adresleri SCP kaynağını kullanır; eksik/eski/geçersiz akım NaN olur. RF alanı yalnız canlı veri tazeliğini gösterir. Geçersiz ölçüm güncel RF bağlantısını 0 yapmaz. Aynı FC03 RF alanları tek zaman örneği kullanır; adres/NVRAM düzeni değişmez. |
| RF enerji/yük ve ayrı kalite bloğu | 06.10.2026 kullanıcı kararı: doküman esas alınır, eski nominal gösterge adları korunmaz. Enerji/yük LIVE bit 0/1 kaynağıdır. Mevcut değer adresleri 0/1; 49500–49520 salt okunur faz kalite bloğunda bit 0 akım, bit 1 enerji, bit 2 yük geçerliliğidir. Eksik/eski örnek kalite=0; son Boolean korunur, hiç örnek yoksa 0 olur. Web/sunucu adres çakışmasını reddeder. NVRAM boyutu/sırası değişmez. Koruma nominal akımı ayrı anlamını korur. Üretim raporu §10.27. |
| Anlık arıza özetleri kaldırılır | 06.10.2026 kullanıcı kararı: arıza akımı/süresi/tipi canlı göstergeleri kaldırılır, geçici/kalıcı listeler kalır. NVRAM yuvaları reserved olur; kalan adresler korunur. Olay yük biti doğrudan saklanır; eski ters anlamlı liste sürümü 2 reddedilir, yeni fault schema 3'tür. Spontane/replay ilk liste kaydı IOA'larından kendi olay zamanıyla gönderilir; hazırlanan paket 06.10.2026 kullanıcı commit talebiyle onaylandı. |
| RF operatör onay kanalı sorulur | 06.10.2026 kullanıcı kararı: onayın yerel RTU operatörü veya SCADA merkezinden gelmesi BOLATeX'e BQ-16 ile sorulur. Yeni web düğmesi/yazma API'si cevap bekler. Mevcut terminal `rf alarm-ack` ve servis davranışı değiştirilmez. |
| RF Kaydet/Uygula ayrıdır | 06.10.2026 kullanıcı K5 kararı: Kaydet istenen ayarı hemen NVRAM’e yazar; Uygula ayrı başlatılır. APPLIED RAM sonucudur, ayar store’unu değiştirmez. PARTIAL/FAILED/reset sonrası otomatik tekrar yoktur. Yeni ayarda eski APPLIED, MatchesDesired ile ayrılır. NVRAM düzeni değişmez. |
| Powerboard tüketicileri SCP kaynağını kullanır | 06.10.2026 kullanıcı kararı: 49200 tabanı korunarak 38 register yeni harita kurulur. Özet/alarm modeli system_status, web, Modbus ve shell kaynağıdır; eski I²C process başlatılmaz. Kaynaklar silinmez; GPIO power-panic ve kapalı BMS reader korunur. E5–E8 müşteri kontrolü shell ile bağlıdır; GEN/yankı/E1 ve komut sonucu ayrımı korunur. Ayarsız ayar/periyot ve E7 bitiş ayrımı BQ-12–14'tedir. Web/Modbus yazma arayüzü ayrıca ele alınır. [Uygulama planı](../doc/RF_SCP_MODEM_UYGULAMA_PLANI.md#powerboard-tüketici-geçişi--06102026). |
| PowerBoard ertelemeleri korunur | BMS Y8.4–Y8.7 yazılım düzeltmeleri tamamlandı; reader init hâlâ kapalıdır. Düzeltme etkinleştirme yetkisi değildir. Üretim raporu §9.31–§9.33. |

Diğer geçilen/ertelenen bulguların güncel kapsamı üretim raporundadır.
Eski erteleme ile sonraki açık kullanıcı kararı çelişirse son karar
esas alınmalıdır. Erteleme üretim kabulü veya düzeltme sayılmamalıdır.
ARM CI/Docker ve ertelenen dokümantasyon işleri kendi başına bu görevle
yeniden açılmış sayılmamalıdır.

## A10. Dil ve proje stili

Mevcut depo tercihi: C11 / Embedded C++20; varsayılan C.
Yeni kod: Allman braces, 4 boşluk, 80 sütun, LF, tab yok;
sabit karşılaştırmada Yoda; unsigned sabitlerde U;
sonsuz döngüde for (;;). g_/s_/p_ kullanılmaz.
Yeni C++ üye adlarında da açıklayıcı snake_case tercih edilir;
harici API ve mevcut sınıfların isimleri topluca değiştirilmez.

Fonksiyon hedefi en fazla 100 satır/5 parametre.
Bu sınırlar emniyet kanıtı değildir; mevcut kodu kapsam dışı yeniden
düzenlemek için gerekçe yapılmaz. Private fonksiyon static;
include sırası kendi header'ı, proje, HAL, standart.
Yeni kaynak sonunda /*** end of file ***/ bulunur.
Header'da gerçek dosya adı, oluşturma tarihi ve kısa sorumluluk
yazılır. Yeni veya düzenlenen C/C++ dosya header'ında
`Author: Fatih Ozcan`, hemen sonraki hizalı satırda
`fatihozcan@gmail.com` kullanılmalıdır. Bu projeye özgü author kararı
başka projeye taşınmamalıdır.
Paketin çalıştırılabilir örnekleri yalnızca host doğrulamasıdır.

Üretim hata API'sinde proje status_t sözleşmesi korunur; errno
kullanılmaz. Standart main ve harici API türleri istisnadır.
Yeni modülün sözleşmesi G03 kapsamında değerlendirilir.

Uyarı hedefi: -Wall -Wextra -Werror -Wshadow -Wconversion
-Wdouble-promotion -Wformat=2. Mevcut build'in bu hedefi sağladığı
henüz kanıtlanmadı. C++ hedefinde -fno-exceptions -fno-rtti;
-fno-threadsafe-statics koşulu CP02'de tanımlıdır.

## A11. Gerçek doğrulama yolları

Aşağıdaki merkezi girişler depo kökünden kullanılır. Ayrıntılı kurulum,
modül seçimi ve test kapsamı [test/README.md](../test/README.md) içindedir.

| Modül | Komut |
|---|---|
| Tüm host testleri | ruby test/run_all.rb |
| Ceedling birim/senaryo testleri | ruby test/run_all.rb unit |
| Mevcut integration paketleri | ruby test/run_all.rb integration |
| Tek modül/senaryo | test/ içinde ceedling test:test_bms_reader_scenario gibi gerçek target |
| ARM Release | Toolchain PATH ayarından sonra make -C Release -j8 main-build |

Testler merkezi test/<modül>/ altında tutulur. Donanım testi, yükleme
ve debug için [.agents troika-hwtest](../.agents/skills/troika-hwtest/SKILL.md)
ve onun yönlendirdiği rehber okunmalıdır. Yalnız firmware derleme
talebi, cihaza yükleme/reset yetkisi sayılmamalıdır.
Host test geçişi hedef warning-free veya fiziksel kabul kanıtı değildir.

| Kapı | Durum |
|---|---|
| Paket yapısı ve örnekler | [DOGRULAMA.md](DOGRULAMA.md) |
| ARM build ve araç | Release derlendi; GCC 14.3.rel1 yolu ve komut donanım rehberinde. Debug tam build kabulü ayrıca doğrulanmalıdır. |
| Statik analiz / seçili kurallar | AÇIK: araç/konfigürasyon/sapmalar |
| ISR koruma ve süre | İlgili modülde kaynak/assembly incelenir; fiziksel gecikme ölçümü ayrı kanıttır. BMS için üretim raporu §9.33. |
| Boot/update/Flash güç kesintisi | AÇIK: bench ve kabul ölçütü |

## A12. Açık konular ve sonraki işlem

| ID | Konu | Bloke ettiği iş / gereken bilgi |
|---|---|---|
| A-01 | Debug tam build ve hedef kabulü | Release/host sonucu bütün konfigürasyonlara genellenmez |
| A-02 | Güvenli çıkışlar/tepki süresi | Emniyet davranışı; H-02 gerekli |
| A-03 | ISR/DMA sahipliği ve kaynak bütçeleri | İlgili concurrency değişikliği |
| A-04 | Contiki ARM port dalı | Platform değiştirme; ayrı inceleme |
| A-05 | Saha/NVRAM migration ve update politikası | Kalıcı ABI/update değişikliği |

İlk soru, yürütülecek işe göre en kritik açık için seçilir.
Bu paketin oluşturulması bu kararların kabulü sayılmaz.

## A13. Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 2026-09-14 | 1.0.0 | Mevcut kaynaklarla ilk doldurma; açık kararların ayrılması |
| 2026-10-04 | 1.1.0 | A06/A08–A11: güncel ABI, kullanıcı kararları, author ve merkezi test girişleri |
