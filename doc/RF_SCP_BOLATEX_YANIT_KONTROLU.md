# RF-SCP BOLATeX yanıtlarının uygulama kontrolü

**Sürüm:** 0.3
**Tarih:** 07.10.2026
**Durum:** Altı kullanıcı kararı uygulandı; üreticiye bildirilecek sapmalar ve protokol sınırları ayrı tutuldu.

## Amaç

BOLATeX yanıtlarını mevcut firmware, gerçek çağrı yolları ve testlerle
karşılaştırmak. Yapılmış düzeltmeler ile kalan ürün kararlarını ayırmak.

## Kullanım yeri

RF-SCP entegrasyonunun devamında ve BOLATeX görüşmesinde kullanılır.
Kaynaklar [R0 yanıtı](BOLATeX_Yanit_RF-SCP_Sorulari_R0.md) ve
[Ek-1](BOLATeX_Yanit_RF-SCP_Sorulari_R0_Ek-1.md) dosyalarıdır.
Üreticinin dosyaları değiştirilmemiştir.

## Kurallar

Üretici yanıtının “BOLATeX tarafında planlanan” bölümleri mevcut firmware
özelliği olarak uygulanmamalıdır. Yeni sürüm bildirimi beklenmelidir.
Bu kontrol host testi ve hedef derlemedir; fiziksel RF veya I2C ölçümü değildir.

## Bulgular ve uygulama durumu

| Konu | Kanıt ve yapılan iş | Kalan iş |
|---|---|---|
| BQ-01, BQ-16 alarm | `rf_alarm.c` kalıcı alarm kuyruğu ve senkronlama sonrası otomatik RTU onayı verir. LIVE 1 süren alarmı açık tutar; 1→0 çözülmesi korunur. Son 128 olay kimliği/EUI RAM cache ile onaylanmış kopya yeniden açmaz. Manuel terminal onayı kaldırıldı. | Otomatik onay operatör onayının yerine geçen RTU ürün kararıdır; üreticiye bildirim taslağı hazırdır. Cache RTU restart ile sıfırlanır; BQ-19 kimlik sınırı açıktır. |
| BQ-02 halka | `rf_events.c` artık pending ipuçları tutmaz; HEAD verisi esas alınır. `rf_scp_codec.c` MH pending/left için 100'ü reddeder. AY LIVE log_pending ayrı alandır, onun sınırı değiştirilmedi. | Yok. |
| BQ-03 açılış | `rf_comm.c` → `rf_events_boot_protect` → `rf_events_process`: inventory IDLE iken koruma kendi ön koşuluna takılıyordu. Koruma artık inventory'den önce çalışır; boş halka dahil HEAD + no-op CONSUME başarıyla bitmeden inventory başlamaz. Hata mevcut 60 s poll ile yeniden denenir. | Taze HEAD ile CONSUME arasında atomik koruma mevcut protokolde yoktur. |
| BQ-03 tüketme | Her kayıt grubundan sonra taze HEAD ve kayıt sıra numarası kontrolü var. Tail kayıt grubunun içinde ilerlediyse yalnız kalan bölüm tüketilir. Kaydedilen son konuma ulaşmış/geçmiş tail için eski cursor gönderilmez. MH resetinde yeniden güncel tail alınır. | Koşullu tüketme MH tarafında planlanıyor; mevcut firmware'e varmış gibi uygulanmadı. |
| BQ-03 Ek-1 | RANGE ERROR 06 sonrasında HEAD okunur; start=head ise skip/consume yapılmaz. CONSUME ERROR 02 sonrasında HEAD yenilenir. HEAD'in ilk 10 baytından sonraki ek alanlar yok sayılır. | Yeni MH'nin halka taşması sonrası bir kez ERROR 02 davranışı fiziksel cihazda denenmedi. |
| BQ-04 örnek sıra | SET bildirimine ACK üretilmiyor. Testler CSV saat sırasını değil request/response neden sırasını kullanıyor. Mevcut `rf_scp_vectors` örnekleri korunuyor. | Yeni üretici örnek seti henüz gelmedi. |
| BQ-05 depo reseti | Ardışık HEAD total/konum karşılaştırması ve uyarı var. Reset görülürse kaydedilmiş eski grubun consume konumu bırakılır. | Normal sayaç taşmasıyla reset ayrımı BQ-18'de soruldu. |
| BQ-06 kısmi ayar | Olay 122 tek başına APPLIED kanıtı yapılmıyor. Mevcut grup sonucu ve CRC kontrolü korunuyor. Otomatik tekrar yok; Ek-1 bunu kabul ediyor. | FAILED sebep 6 için aynı ayar/yeni grup kimliği yönlendirmesi webde eklendi; otomatik ayar tekrarı yoktur. |
| BQ-07 epoch | `rf_inventory.c` 30 s bekliyordu; 90 s oldu. Başka fidere epoch gönderimi de mevcut bekleme dizisiyle engelleniyor. Shell mesajı güncellendi. | 08.10.2026: mevcut rf epoch N açık MH değişimi bakımını başlatır. Başarılı ACK sonrası ilk yerel yapılandırma FAILED/5 verirse tek EPOCH tekrar edilir; sonraki başarısızlıkta yeni otomatik tekrar yoktur. Rutin işlem ve BOOT bunu başlatmaz. Yeni davranışın fiziksel özel senaryo kabulü ayrı izlerle tamamlanmalıdır. |
| BQ-08 Powerboard | `app_main.c` yalnız `power_board_scp_init` çağırıyor. E8 96 bayt ham kalıyor; E1/E3 tüketiliyor. CubeMX I2C3 init'i USER CODE alanında atlanıyor. PD12/PD13 mevcut MSP'de doğrulandı; HAL GPIO DeInit analog/no-pull durumunu kuruyor. | Pinlerin fiziksel yüksek empedansı ve eski kartla açılış ölçülmedi. Eski kaynak dosyaları ve `#if 0` blokları silinmedi. |
| BQ-09 envanter | `rf_inventory_start/update` DRAINING ön kontrolünden geçer. Canlı AY log_pending=0 ve MH taze boş HEAD beklenir; total sınırı RAM'de tutulur. Eski kabul edilmiş RTU satırı drain sırasında kullanılır. 06 hata yanıtı eski binding'i değiştirmez. Manuel inv BOOT korumasını ve aktif yerel ayarı atlayamaz. | Tam 04/05 yükleme mevcut PARTIAL politikasını korur. Sınır sonrası eski etiketli kaydın kimlik belirsizliği R0 BQ-09 sınırıdır; fiziksel tekrar yapılmadı. |
| BQ-10 arıza | `rf_faults.c` dizi sınıflaması: 4/5→3 arada 6 yoksa ham olay; 6→3 kalıcı sonuç yoksa geçici. Faz listeleri korunur. Saat kalitesi 1 ve geçerli tarihte aynı fiderin 1 s içindeki açmaları tek sayılır. 100/101 önce gelirse sonraki 1/7 toplamı artırmaz; açma önce biliniyorsa 100/101 ek faz arızası yapılmaz. 117 ayrı atanmış-olmayan sayımdadır. | Sayaçlar bu RTU oturumunda işlenen kayıtlar içindir. Saat kalitesi 0/2, eski veya eksik dizi kesin toplam gibi gösterilmez. Sonradan gelen faz için geçmiş faz sonucu silinmez; BQ-20 kesin dizi kapanışı hâlâ açıktır. |
| BQ-11 grup | Sıfır ID ve yanlış fider/faz eşleşmesi reddedilir. RTU startup son job bilgisini sıfırlar; NVRAM alanı eklenmedi. Eski ID'nin peer raporu unsent yerel işin APPLIED sonucu yapılmaz. | Kullanıcı kalıcılığı şimdilik erteledi; bu R0 önerisinden ürün sapması olarak bildirilmelidir. Yarım WRITE temizliği ve BQ-17 restart keşfi ayrıca açıktır. |
| BQ-12 ayar | Kapasite service ve request codec'te 7–54 ile sınırlı. Doğrulama beklemesi 3P+6, alt sınırı 20 s; P=10 için 36 s. Gerçek kapasite için bekleme sonrasında alınmış E1 aranıyor. Şarj sınırlama verdict 2 reddetme sayılmıyor. Ayarsız C-oranı gerçek kapasitenin doğrulamasını atlamıyor. | Kapasite-bilinmiyor bakım uyarısı system_status/JSON/web yolunda tamamlandı. Yürürlükteki E1 C-oranının kontrol ekranında ayrı gösterimi açık kalır. Otomatik akü-değişti komutu eklenmedi. |
| BQ-13 komut | E7, ACK sonrası CMD 05 ile eşleniyor; SIRA bilgi. ACK öncesi cache başarı kanıtı değil. SET FF/yayin≥2/no-outcome bitleri yanıtsız bitiş olur. GET'te 5 dakikadan önce kesin bitiş sayılmaz. Cancel ACK sonrası tek GET yapılır; etkisiz iptal, izlenen akıbet ve iptal edildi sonuçları ayrıdır. | Gelecek E7 b4 kullanılmıyor. 5 dakika sonunda otomatik GET eklenmedi; mevcut operatör GET yolu kullanılır. |
| BQ-14 periyot | Müşteri service ve request codec 1–10 s kabul eder. GET b7 korunur; b6/FF reddedilir. | Üretici mevcut 1–10 s tercihini kabul etti; ayarsız seçenek açılmadı. |
| BQ-15 kalite | Ardışık aynı uptime, SEQ aynı olsa da ölçümü geçersiz yapar. IEC104 IV; eskimiş LIVE için NT; Modbus NaN ve ayrı kalite=0. RF online ayrı kalır. FSM hata bayrağı ölçümü tek başına geçersiz yapmaz. Web bakım uyarısı ve geçersiz/eski ölçümde — gösterir. Alım zamanı kullanımı yanıtla doğrulandı. | Gerçek AY'nin sabit uptime/FSM hatası gözlemlenirse kaydı BOLATeX'e iletmek gerekir; bu koşullar host testinde üretildi. |

## Sadeleştirme

Olay makinesinden `pending_hint`, `hint_head`, `needs_verification`
kaldırıldı. Tüketme kontrolü tek `verify_head` fonksiyonunda tutuluyor.
Powerboard'da gereksiz wrapper ve doğrulamadan sonra tekrar edilen
periyot kontrolü kaldırıldı. Grup ilerleme handler'ındaki erişilemeyen
terminal durum kontrolleri kaldırıldı.

Alarm için yeni durum alanı eklenmedi: `trip_failed=true`,
`trip_failure_latched=false` onaylanmış ama süren alarmı gösterir.
Cancel sonrası zorunlu GET için tek bekleme bayrağı kullanılır;
ek retry servisi veya arka plan sorgu döngüsü kurulmadı.

## PC simülatörünün kontrolü

`tools/rf-hil/sim/hub_model.py` eski consume konumunu bugünkü MH'de
olmayan bir korumayla reddediyordu. Bu koruma kaldırıldı. Güncel model
eski slot numarasını kabul eder; test, okunmamış 99 kaydın böyle
tüketilebildiğini gösterir. Bu, RTU'nun taze HEAD kontrolünü test etmek
için bilinçli olarak korunan mevcut MH davranışıdır.

İlk yeni WRITE önceki grup geçmişini siler; pre-COMMIT sorgusunda ID 0
boş durum döner. Restart pre-COMMIT üyeleri siler; çalışan COMMIT
FAILED/reason 8 ve ID 0 olur. Eski ID sorgusu ERROR 02 döner.

Powerboard başlangıcı E7 FF/yayin 1, yanıtsız bitişi FF/yayin 2 bildirir.
Bitmiş komuta iptal son sonucu değiştirmez ve nötr ACK verir. Bekleyen
komutta simülatörün seçilmiş deterministik iptal sonucu b2'dir; gerçek
firmware'deki b1 ve gecikmiş akıbet yolları Ceedling'de ayrıca sınanır.
Bu simülatör seçimi fiziksel güç kartının zamanlamasına kanıt değildir.

## Kullanıcı kararları

07.10.2026 yanıtlarıyla altı karar kapatılmıştır. Kararların ve BOLATeX'e
bildirilecek sınırların tek kaynağı
[uygulama raporu bölüm 7](../BOLATEX_YANIT_UYGULAMA_RAPORU_2026-10-07.md#7-bolatexe-bildirilecek-rtu-ürün-kararları--07102026)
olmalıdır. Yeni manuel web/SCADA onay komutu eklenmemiştir; onay kayıt
sonrasında otomatik yapılır.

Yeni IEC104 alarm noktaları web'den yapılandırılır. Kalıcı replay günlüğü
fault/alarm türüyle genişletildi; NVRAM schema 3, yerleşim boyutu aynı.
Fider sayımı `/monitor/rf/<satır>` yanıtında ve izleme ekranında görünür.
Toplu 21 faz yanıtı mevcut HTTP buffer sınırını korur. Yeni Modbus sayım
veya SCADA sayım adresi eklenmemiştir.

## Doğrulama

- Ceedling: RF/Powerboard/Modbus/IEC104 modülleri ve mevcut iletişim
  örnekleriyle çalışan gerçek production girişleri.
- Yeni regresyonlar: boş/IDLE açılış koruması, hata sonrası inventory'nin
  beklemesi, tail ilerlemesi, store reset, ERROR 06/no-record ve ERROR 02,
  99/100 sınırı, kapasite/C-oranı, 36 s ve tick wrap, eski E1,
  değişen E7 SIRA, ACK öncesi sonuç, cancel GET, 5 dakika GET ayrımı,
  olay-only onay, aynı açılışta otomatik kapanma ve sabit uptime kalitesi.
- Web navigation: kaynak HTML ve firmware'e gömülü gzip içerik.
- 22 production modülü: C11 Cortex-M33 ve sıkı warning seçenekleri.
- Release derlemesi. Cihaza yazma, reset ve fiziksel HIL tekrarı yapılmadı.

Son sayısal sonuçlar [üretim hazırlık raporunda](../URETIM_HAZIRLIK_RAPORU_2026-10.md)
tutulur. Açık madde, tamamlanmış düzeltme olarak sayılmamalıdır.

## Değişiklik geçmişi

| Tarih | Sürüm | Etkilenen bölüm |
|---|---|---|
| 07.10.2026 | 0.1 | İki üretici yanıtının kaynak/test karşılaştırması, düzeltmeler, refactor ve kalan kararlar |

| 07.10.2026 | 0.2 | Altı kullanıcı kararının uygulanması; otomatik onay, kalıcı karma replay, SCADA alarm IOA'ları, dizi/fider sayımı ve envanter ön kontrolü |

| 08.10.2026 | 0.3 | Analiz teyidi: tek EPOCH bakım tekrarı, FAILED/6 yönlendirmesi ve kapasite-bilinmiyor web uyarısı; fiziksel özel senaryo sınırı korundu |
