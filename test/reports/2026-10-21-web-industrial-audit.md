# Web arayüzü — Endüstriyel kullanım ve AY silme incelemesi

**Sürüm:** 1.2 — düzeltmeler ve gerçek cihaz testleri  
**Tarih:** 21.10.2026  
**Amaç:** Web arayüzündeki hataları, işletme eksiklerini ve AY EUI silme akışının doğrulama sınırlarını belirlemek.  
**Kullanım yeri:** Düzeltme önceliği, servis işlemi ve cihaz kabulü hazırlığı.  
**Kapsam:** Kaynak HTML, gömülü HTML, HTTP yetkilendirme, RF parser/store, envanter, ayar uygulama ve ilgili merkezi testler.

## 1. Sonuç ve kanıt sınırı

Arayüz, yapılandırma ve servis aracı olarak kullanılabilecek bir temele sahiptir. İncelemede bulunan WEB-01–05 hataları, kullanıcının uygulama talimatıyla giderilmiştir. İlk bulgu ve reproduksiyon kayıtları aşağıda tarihsel kanıt olarak korunmuştur; uygulama bölüm 9'da, sonraki gerçek cihaz kabulü bölüm 10'dadır. Parola/HTTP modeli ve üç gerçek AY ile APPLIED kabulü açık sınırlar olarak kalır.

AY silmenin RTU ve MH envanter akışı protokoldeki yöntemle uyumludur. EUI'nin formdan boşaltılması, kalıcı hedefe yazılması, eski EUI için açık silme gönderilmesi, ACK beklenmesi ve kesilen işlemin tekrar yürütülmesi vardır. Host testleri bu yolları doğrulamıştır. Bu sonuç AY kartının kendi atamasının silindiğini, yayınını durdurduğunu veya koruma işlevinin devre dışı kaldığını kanıtlamaz. Bu sınır BQ-26 ile zaten izlenmektedir.

Kanıt seviyeleri:

- **E1:** Güncel kaynak ve ulaşılabilir çağrı yolu incelendi.
- **E2:** Gerçek üretim fonksiyonları host üzerinde çalıştırıldı; donanım/HTTP sınırı test karşılığıyla temsil edildi.
- **Yerel tarayıcı:** Gerçek gömülü sayfa, mevcut localhost preview sunucusunda sentetik verilerle incelendi. MH veya AY davranışı simüle edilmiş sayılmaz.
- **İlk incelemedeki canlı cihaz kapsamı:** Giriş sayfasına erişildi. Admin girişini otomatik onay denetimi reddetti; ilk incelemede kimlik doğrulanmış canlı cihaz testi, EUI kaydı, reset veya firmware yüklemesi yapılmadı. Sonraki açık kullanıcı talimatıyla yapılan fiziksel testler bölüm 10'da ayrı kaydedilmiştir.

İncelenen Git tabanı `a2901519704320a250d5e420c1f3f995a3e2dfd0` ve mevcut çalışma ağacıdır. Önceden bulunan değişiklikler korunmuştur. İlk inceleme üretim kodunu değiştirmedi; sonraki uygulama kaynak/gömülü web sayfasını değiştirdi. Commit/push yapılmadı. İlk incelemedeki yükleme sınırı, sonraki kullanıcı talimatıyla bölüm 10'daki yükleme/test kaydıyla güncellenmiştir. Bulgu bölümlerindeki satır numaraları ilk inceleme sürümüne aittir.

## 2. Testle yeniden üretilen hatalar

P1, veri gizliliğini veya ayar doğruluğunu etkileyen yüksek önceliktir. P2, işletme ve servis güvenilirliği sorunudur. Bu dereceler saha arıza olasılığı ölçümü değildir.

| Kimlik | Öncelik | Bulgu | Kanıt |
|---|---|---|---|
| WEB-01 | P1 | Önceki admin verisi logout/401 sonrasında kalır; aynı sayfada user girişiyle yeniden gösterilebilir | E2; kaynak ve gömülü sayfa |
| WEB-02 | P1 | Sayısal doğrulama ile gönderilen tam sayı farklı olabilir | E2; kaynak ve gömülü sayfa |
| WEB-03 | P2 | Request timeout, response body tamamlanmadan kaldırılır | E2; kaynak ve gömülü sayfa |
| WEB-04 | P2 | FAILED bitmap'i yararlı üye bilgisi taşırken arayüz ayrıntı bulunmadığını söyler | E2 + protokol ve üretim WRITE sırası |
| WEB-05 | P2 | Modbus menüsü backend'in reddettiği 230400 baud seçeneğini sunar | E2; UI ve gerçek C parser ayrı çalıştırıldı |

### WEB-01 — Oturum değişiminde önceki kullanıcının verileri korunuyor

**Tetikleyici:** Admin Cihaz yönetimi sayfasını okur; çıkış yapar veya isteği 401 alır; aynı uygulama sayfasında user hesabıyla giriş yapılır ve Cihaz yönetimi açılır.

**Beklenen:** Önceki oturuma ait yapılandırma, gizli alanlar ve günlükler yeni oturuma taşınmamalıdır.

**Gerçek:** `doLogout()` token/role alanlarını temizler; `api()` 401 yolunda da token/role sıfırlanır. `pageCache` temizlenmez. `buildMenu()` yalnız firmware menüsünü role göre gizler. `renderPage()` önceki `pageCache.device` içeriğini, yeni bir GET yapmadan kullanır. Host denemesinde sentetik APN parolası yeni user ekranının HTML'inde kaldı.

**Etki:** Aynı tarayıcı sayfasındaki kullanıcı değişiminde gizli ayar sızıntısı ve eski ölçümlerin yeni oturumda görünmesi. Backend POST yetkilerinin aşıldığı iddia edilmez.

**Kaynak:** `web-page/index.html:1261`, `1277–1278`, `1756–1759`.

**En küçük öneri:** Logout, yetki kaybı ve kullanıcı değişiminde ortak bir yerel oturum temizleme işlemiyle cache, günlükler, form/terminal içeriği ve sayfa durumlarını temizlemek. Rolün izin vermediği işlemleri arayüzde de kapatmak. Yeni bir oturum/cache katmanı gerekmiyor.

### WEB-02 — Doğrulanan sayı ile gönderilen sayı farklı

**Tetikleyici:** Modbus Cihaz ID alanına `37.9` veya `1e2` girilip Kaydet seçilir.

**Beklenen:** Tam sayı olmayan ID reddedilmelidir. Bilimsel gösterim kabul ediliyorsa sayısal anlamı korunmalıdır; kabul edilmiyorsa açıkça reddedilmelidir.

**Gerçek:** `validation()` `Number(value)` ile aralık denetler; `getInt()` `parseInt(value,10)` kullanır. `37.9` değeri 37, `1e2` değeri 1 olarak POST edilir. İkincisi doğrulamada 100'dür. Kaydet yolu native form submission kullanmadığından input'un integer step niteliği bunu engellemez.

**Etki:** Kullanıcının girdiğinden farklı, fakat backend açısından geçerli bir ayarın kaydedilmesi. ID değişikliği haberleşme hedefini değiştirebilir. Aynı yardımcıyı kullanan diğer integer alanlar ayrıca taranmalıdır; bu raporda somut reproduksiyon Modbus ID'dir.

**Kaynak:** `web-page/index.html:1305`, `1309`, `1728`, `1747`.

**En küçük öneri:** Integer alanlarda mevcut `validationEl()` yaklaşımını yeniden kullanmak; doğrulama ve extraction aynı `Number()` sonucu üzerinden yapılmalıdır. Yalnız integer kontrolü eklemek `1e2`/`parseInt` uyuşmazlığını tek başına çözmez.

### WEB-03 — Yanıt gövdesi takılırsa timeout koruması kalmıyor

**Tetikleyici:** HTTP response headers gelir, fakat JSON body tamamlanmaz.

**Beklenen:** İstek bütçesi gövdenin okunmasını da kapsamalıdır.

**Gerçek:** `api()` `await fetch()` sonrasında timer'ı kaldırıp `await r.json()` veya hata yolunda `await r.text()` çağırır. Fetch headers geldiğinde tamamlanabilir; body beklerken artık AbortController timer'ı yoktur. Host denemesinde body Promise'i beklerken etkin 30000 ms timer sayısı sıfırdı.

**Etki:** Tarayıcı/transport kendi hatasını üretinceye kadar uygulamanın 30 saniyelik süresi aşılabilir. `showLoad()` kullanan işlemde tüm düğmelerin beklemede kalması mümkündür. Sonsuz gerçek cihaz takılması bu incelemede fiziksel olarak ölçülmedi.

**Kaynak:** `web-page/index.html:1219–1220`, `1261–1271`.

**En küçük öneri:** Mevcut timer, body tüketimi tamamlandıktan sonra `finally` içinde temizlenmelidir. Ek retry, bağlantı katmanı veya cihaz reseti gerekmez.

### WEB-04 — Sorunlu grup üyesi bilgisi operatöre açıklanmıyor

**Tetikleyici:** Kendi yürütülen grubu için üç WRITE kabulünden sonra `FAILED`, reason=1, bitmap=0x04 raporu alınır.

**Beklenen:** Bitmap'in gösterdiği üçüncü kabul edilmiş üye açıklanmalıdır. Üye-faz eşlemesi kanıtlıysa ilgili EUI/faz gösterilmelidir.

**Gerçek:** Teknik ayrıntılarda ham bitmap vardır; normal hata açıklaması "Bu rapor sorunlu faz cihazını belirtmiyor" der. `buildRfFailure()` bitmap'i yorumlamaz. Yerel preview'da aynı yanıltıcı metin görüldü.

**Protokol ayrımı:** SCP R2 §4.9 bitmap biti, kabul edilen farklı EUI'li WRITE sırasına aittir; genel olarak doğrudan faz biti değildir. Bu RTU'nun kendi yeni grubunda üyeler L1/L2/L3 olarak dondurulur ve `writes_acked` sırasıyla gönderilir. Bilinmeyen/başka bir gruba ait rapora aynı eşleme uygulanmamalıdır.

**Kaynak:** `web-page/index.html:1406–1423`, `1464`; `Application/rf/rf_group.c:159–161`, `422`; üretici R2 belgesi `§4.9`, satır 1648.

**En küçük öneri:** Bilinen grup için mevcut frozen üyelerden kabul sırasını EUI/faza bağlamak; eşleme bilinmiyorsa en azından "Sorunlu kabul edilmiş üye: 3" demek. Ham bitmap'i her durumda faz maskesi kabul etmek yanlış olur. Üye bilgisinin JSON'a eklenmesi gerekirse API kararı ayrıca onaylanmalıdır.

### WEB-05 — Modbus baud seçenekleri uzlaşmıyor

**Tetikleyici:** Web menüsünden 230400 baud seçilip Kaydet seçilir.

**Gerçek:** `renderModbus()` 230400 seçeneğini üretir. Gerçek `parse_modbus_config()` bu değeri reddeder; 115200 kontrol isteğini kabul eder. Backend ayrıca 4800/14400/28800 kabul eder, fakat bunlar normal menüde yoktur. Menüde olmayan mevcut değer için genel select helper geçici seçenek gösterebilir; bu durum açık destek listesi yerine geçmez.

**Etki:** Arayüzün geçerli seçenek olarak sunduğu ayar kaydedilemez. Cihazın hatalı baud'a geçtiği iddia edilmez.

**Kaynak:** `web-page/index.html:1400–1403`; `Application/web-server/json_config.c:625–644`.

**En küçük öneri:** Menü mevcut onaylı backend profiliyle eşleştirilmelidir. Bu bulguyu kapatmak için sürücünün destek aralığını genişletmek gerekmez.

## 3. Endüstriyel kullanım için eksikler ve korunacak iyi özellikler

Bu bölümdeki maddeler beş doğrulanmış hatadan ayrıdır. Yeni ürün davranışı veya mimari onayı sayılmamalıdır.

| Konu | Gözlem / eksik | Öneri |
|---|---|---|
| Kimlik doğrulama | Parola IP'den türetilir; HTTP ve query token kullanılır. HTTP RNG fallback'i kriptografik entropy garantisi değildir. Mevcut ürün kararları korunmuştur. | Saha erişim modeli ve risk kabulü netleştirilmeli; kimlik/transport değişikliği ayrı kararla yapılmalıdır. |
| Role göre kullanım | User, yazma yetkisi olmayan sayfalarda Kaydet ve servis denetimlerini görebilir; backend bunları reddeder. | Salt okunur kontroller açıkça gösterilmeli, yetkisiz işlem düğmeleri kapatılmalıdır. |
| Cihaz kimliği | Üst başlık sayfa ve web sürümünü gösterir; hedef seri numarası, çalışan firmware ve aktif rol sürekli görünmez. | Birden fazla cihazla çalışan servis personeli için hedef kimliği ve rol görünür olmalıdır. |
| Veri tazeliği | Otomatik yenilemenin kapalı olduğu ve son okuma zamanı yazılıdır. Ancak okunan ONLINE/alarm ve veri yaşı ekran açık kaldıkça kendiliğinden değişmez. | Manuel model korunabilir; ekranın bir snapshot olduğu ve üzerinden geçen süre belirgin gösterilmelidir. Bu tek başına yeni polling gerekçesi değildir. |
| EUI silme anlamı | Temizle, sadece formu değiştirir. Kaydet daha sonra RTU kaydı ve MH işlemlerini başlatır. AY'nin kendi durumu bununla doğrulanmaz. | İşlem açıklaması bu üç katmanı ayırmalıdır. "AY güvenli olarak devre dışı" mesajı verilmemelidir. |
| Keşfedilen cihazlar | Silme ACK'inden sonra EUI, yeni RF yayını beklenmeden discovery listesine eklenir (`rf_inventory.c:1005–1009`). | Listede görünmek canlılık kanıtı gibi sunulmamalıdır; bilinen atanmamış cihaz ile yeni RF keşfi ayrılmalıdır. |
| Değişiklik kaydı | `elog_log_config_change()` kaynak/IP/alan/başarı saklar; hangi EUI/fazın hangi değerden değiştiğini tutmaz (`elog.c:802–835`). | Servis izlenebilirliği gerekiyorsa hedefli değişiklik özeti tasarlanmalıdır. Kalıcı format değişikliği ayrı karar gerektirir. |
| Tarayıcıdan ayrılma | Uygulama içi geçiş ve yeniden okuma dirty onayı vardır; source'da browser reload/kapatma için `beforeunload` handler yoktur. | Kaydedilmemiş ayarın yanlışlıkla kaybı için browser uyarısı değerlendirilebilir. |

Korunması gereken mevcut özellikler:

- RTU'ya kaydedilme, MH envanter kabulü ve üç üyeye koruma ayarı uygulanması ayrı gösterilir. ACK, APPLIED yerine sayılmaz.
- RF Kaydet öncesinde silinen/taşınan/eklenen EUI özeti vardır.
- Busy, belirsiz işlem ve pending envanter korumaları vardır.
- Dil değişiminde taslak korunur; yeniden okuma ve sayfa geçişi kayıp onayı ister.
- Ölçüm yokluğu/geçersizliği, ADC geçerliliği ve RF bakım bilgileri için mevcut gösterimler vardır.
- Source ve embedded sürümler aynı davranış testlerinden geçer.

## 4. AY EUI silme altyapısı nasıl çalışıyor?

1. **Form:** `clearRfEui()` seçilen faz alanını boşaltır ve dirty yapar. Bu adım HTTP yazması değildir.
2. **Onay:** `savePage('rf')` mevcut RF iş durumunu okur; busy ise durur. `rfChangeSummary()` eski EUI'nin hangi fider/fazdan çıkarılacağını onayda gösterir.
3. **HTTP/parser:** `POST /config/rf` admin gerektirir. Parser staging kopyasında çalışır. Boş EUI sekiz sıfır bayta dönüşür. Geçersiz istek staging'den çıkarılır; mevcut store korunur.
4. **Kalıcı hedef:** `rf_inventory_prepare_save()` eski kabul edilmiş binding'lerden kaldırılması gereken EUI'leri journal'a alır. Yeni hedef ve journal mevcut NVRAM save yoluyla saklanır. Başarısız save, RF işlemine başlanmış gibi gösterilmez. Yalnız primary kopya yazıldıysa backup uyarısı verilir ve RF dizisi başlatılmaz.
5. **Eski kayıtları koruma:** Envanter yüklenmişse canlı AY'nin bekleyen kayıtları, MH halkası, RTU alarm/kayıt durumları ve SCP uygunluğu beklenir. Sessiz AY için LIVE bekleme adımı atlanabilir; MH drain şartı kalır.
6. **Açık silme:** Eski EUI için `0x06 INVENTORY_UPDATE`, **fider=0**, eski zone/phase/EUI/channel ile gönderilir. Web'de aktif fider numarasını 0 yapmak bu işlemin kullanıcı arayüzü değildir.
7. **ACK/hata:** ACK'te kabul edilmiş binding kaldırılır; EUI atanmamış listesine eklenir. Silme başarısızsa akış ERROR olur; eski binding tutulur ve yeni upload/success varsayılmaz.
8. **Yeni envanter:** Kalan hedefler `0x04` ile, ardından `0x05 END` ile gönderilir. END kabulü ve journal temizliğinin kalıcı kaydı tamamlanmadan işlem kesin eşleşmiş sayılmaz.
9. **Kesinti:** Journal durduğu için RTU durum sıfırlamasında eski silme tekrar gönderilebilir. END'e kadar tek tek ACK'ler journal'ı silmez. Yarım işlem bitmeden farklı hedefle değiştirme engellenir; aynı kayıtlı hedefle retry mümkündür.
10. **Koruma ayarı:** EUI silindiği için üç fazı tamamlanmayan fider, koruma WRITE/COMMIT grubunda atlanır. Envanter silmesi başarılı olduğu halde "Faz üyeleri eksik; ayar uygulanmadı" görülebilir. Başka tam üyeli fiderlerin ayarları yine gönderilebilir.

Kaynaklar: `Application/web-server/http_handlers.c:1527–1601`, `Application/rf/rf_apply.c:68–128`, `Application/rf/rf_inventory.c:318–410`, `652–742`, `745–812`, `928–1015`; `Application/rf/rf_config.c:216–243`, `327–361`.

Üretici R2 belgesi `§4.2/§4.3` silme için `0x06/fider=0` yöntemini belirtir. Aynı belge `§4.7`, eski kartı devreden çıkarma → envanteri güncelleme → yeni kartı devreye alma sırasını açıklar. AY'nin kendi fiderini elle 0 yapmak, MH envanterinden silme yerine geçmez.

## 5. Operatör için uygulama adımları

### 5.1. Yalnız bir AY'nin atamasını kaldırma

1. Hedef cihaz kimliği, AY'nin gerçek EUI'si, RF satırı, **atanmış Fider No** ve L1/L2/L3 fazı doğrulanmalıdır. Satır numarası ile Fider No aynı varsayılmamalıdır.
2. Bakım koşulları sağlandıktan sonra admin ile **RF Yapılandırma → Cihazdan oku** seçilmelidir. Başlangıç eşlemeleri ve koruma ayarları kaydedilmelidir.
3. Doğru satırdaki ilgili fazın **Temizle** düğmesi seçilmelidir. Diğer iki EUI'ye dokunulmamalıdır. Tek fazı kaldırmak için bütün fiderin "RF yapılandırmasına dahil et" seçeneği kapatılmamalıdır.
4. **Kaydet** seçilmeli; onayda doğru EUI/fider/fazın çıkarılacağı kontrol edilmelidir. Kaydet, diğer etkin ve tam üyeli fiderlere de koruma ayarı gönderebildiğinden bu kapsam görülmelidir.
5. RF ayar aktarımı bölümünde **Durumu yenile** kullanılmalıdır. Otomatik yenileme yoktur. "RTU üzerinde kayıtlı" mesajı MH işleminin bittiği kabul edilmemelidir.
6. Envanter beklemesi/yüklemesi bitmeli; envanter hatası ve pending recovery kalmamalıdır. Kalan üyeler varsa READY, hiç kalmadıysa EMPTY beklenir. `InventoryPending=false` ve `InventoryRecoveryPending=false` kontrolü teknik kanıttır. MH doğrulaması burada ACK'lere dayalı kabul edilmiş envanterdir; bağımsız AY readback değildir.
7. Yeniden **Cihazdan oku** ile seçilen EUI'nin boş, diğer fazların aynı olduğu doğrulanmalıdır. Bu GET RTU hedef ayarını gösterir; tek başına MH silme kanıtı değildir.
8. RF İzleme yeniden okunmalı; kaldırılan EUI'nin eski konumun güncel verisi olarak sunulmadığı kontrol edilmelidir. Keşfedilen Cihazlar'da EUI'nin görünmesi otomatik yeniden atama değildir ve tek başına canlılık kanıtı değildir.

### 5.2. İşlem hata verirse

- Busy veya kayıt boşaltma sürüyorsa neden ve bekleme süreleri incelenmelidir. Körlemesine yeniden kaydetme/reset ile işlem atlanmaya çalışılmamalıdır.
- Envanter ERROR/pending ise önce **Cihazdan oku** ile kayıtlı hedef görülmeli; neden giderildikten sonra **aynı hedef** yeniden Kaydet ile denenmelidir. Pending işlem varken başka EUI eklemek/replacement yapmak engellenebilir.
- `backup_failed` uyarısı RF silmenin başladığı anlamına gelmez. Depolama sorunu giderilmeden başarılı sayılmamalıdır.
- "Faz üyeleri eksik; ayar uygulanmadı" ile envanter hatası ayrılmalıdır. Bir AY bilerek kaldırıldığında eksik üyeli fiderin koruma ayar grubunun atlanması beklenen davranıştır.
- Kartın kendisinin tanımsız duruma geçtiği veya RF yayınını durdurduğu ayrıca üretici prosedürü ve kart gözlemiyle doğrulanmalıdır. Bu konuda mevcut **BQ-26** kullanılmalıdır; yeni paralel soru listesi açılmamıştır.

### 5.3. Canlı cihazda hâlâ gereken kabul

Onaylı bakım düzeneğinde tek EUI kaldırma, ACK kaybı/ret, RTU kesintisinden recovery ve yeniden atama denenmelidir. Her adımda RTU hedef kaydı, SCP komut/ACK izi, MH sonucu, AY'nin kendi config/LIVE çıktısı ve diğer fazların korunması ayrı kaydedilmelidir. Eski kayıtların yeni AY'ye yanlış bağlanmadığı ve kayıtların topluca silinmediği doğrulanmalıdır. Bu incelemede gerçek karttan kalıcı EUI silinmemiştir.

## 6. Çalıştırılan testler

| Test | Sonuç / sınır |
|---|---|
| İlk RF/web C seçimi | 173/173 PASS |
| Gerçek RF comm/inventory/error/retry senaryosu | 130/130 PASS |
| Ek parser testleri | Seçilen fazı boşaltma/diğer fazları koruma/reload ve geçersiz EUI sonrası koruma; 11/11 PASS olan parser paketi içinde |
| Son birleşik RF/web C seçimi | **306/306 PASS**, fail/ignore=0; `.temp_files/web-audit-20261021/selected-ceedling.log` |
| `make -C test/integration/web_navigation run` | PASS; source+embedded navigation, i18n, loading, minifier, Modbus metadata |
| `make -C test/integration/web_auth run` | PASS; auth, formatter, RF save handler ve HTTP/GSM transport host kontrolleri |
| `node test/integration/web_navigation/test_industrial_audit.js` | 8 kontrol × source/embedded = 16; mevcut beş kusur nedeniyle 16 FAIL beklenir. Yeşil suite değildir. Varsayılan Makefile'a bağlanmadı. |
| Yerel tarayıcı | Gömülü v1.4.11; RF formu, EUI Temizle → dirty → yeniden okuma onayı; sentetik NOT_LIVE sonucu |

EUI için mevcut senaryolar tek tek kaynakta da okundu: `test_removed_device_is_explicitly_deleted_before_inventory_end`, `test_failed_device_delete_preserves_binding_and_stops_upload`, `test_phase_swap_deletes_old_assignments_before_loading_new_ones`, `test_saved_delete_survives_rtu_state_reset_and_clears_only_at_end`, `test_journal_clear_failure_keeps_pending_work_and_blocks_final_match`, `test_pending_inventory_allows_same_target_but_rejects_replacement`. Bunlar `test_rf_error_retry_scenario.c` içindedir. Journal testindeki reset, RAM durumunun sıfırlanmasıdır; fiziksel güç kesintisi kanıtı değildir. NVRAM fake ile reload, gerçek Flash programlama kanıtı değildir.

İlk sandbox denemesinde Ruby ve GCC assembler başlatılamadı. İzinli çalışma bağlamında testler yürütüldü. Yeni EUI parser testinin ilk taslağı, tarayıcının gönderdiği yedi eleman yerine tek eleman kullanıyordu ve başarısız oldu; gerçek web payload biçimine düzeltilip tekrar çalıştırıldı. Bu ilk fixture sonucu, web EUI silme hatası olarak raporlanmadı. Kısa dizilerin parser davranışı ayrı inceleme konusudur.

Ham reproduksiyon kaydı: `.temp_files/web-audit-20261021/industrial-reproductions.log`. Başarısız audit kontrolleri düzeltme sonrası aynı komutla yeşile dönmelidir. Firmware değiştirilmediği için yeni ARM build/cihaza yükleme yapılmadı; tam host suite koşulduğu iddia edilmez.

## 7. Önerilen düzeltme sırası

1. WEB-01 oturum temizliği ve WEB-02 sayısal dönüşüm düzeltilmelidir.
2. WEB-03 için timeout kapsamı gövde okumayı içermelidir.
3. WEB-05 menü/backend destek listesi uzlaştırılmalıdır.
4. WEB-04 için grup üye sırası doğru yorumlanarak hata açıklaması iyileştirilmelidir.
5. EUI silme açıklaması ve role/kimlik/tazelik sunumu geliştirilmelidir.
6. Bakım onayıyla gerçek AY silme kabulü ve BQ-26 netleştirmesi tamamlanmalıdır.

RTU silme akışını yeniden tasarlamak, ek silme kuyruğu, bağımsız retry, yeni reset veya yeni kalıcı alan eklemek için bu incelemede gerekçe bulunmadı. Mevcut staging/journal/ACK/drain mekanizmaları yeniden kullanılmalıdır.

## 8. Değişiklik geçmişi

| Tarih | Sürüm | Değişiklik |
|---|---|---|
| 21.10.2026 | 1.0 | Beş hata, host reproduksiyonları, endüstriyel kullanım eksikleri ve AY silme prosedürü |
| 21.10.2026 | 1.1 | WEB-01–05 kapanışı, rol/EUI/tazelik açıklamaları, 30 kalıcı kontrol ve Release doğrulaması |
| 21.10.2026 | 1.2 | Son web v1.4.13 imajının yüklenmesi, tam host testleri ve gerçek cihaz kabul kaydı |

## 9. Uygulama ve kapanış — 21.10.2026

Kullanıcının "önerilen düzeltmeleri uygula" talimatı sonrası web sürümü **1.4.12** oldu. Bölüm 2'deki ilk bulguların güncel durumu:

| Bulgu | Değişiklik | Güncel kanıt |
|---|---|---|
| WEB-01 | `clearSession()` logout/401 ve başarılı kullanıcı geçişinde cache, dirty/read/save durumları, günlükler ve eski ekranı temizler. Logout yerel temizliği server cevabından önce yapar. `api()` mevcut token'ı request başında alır; başka oturumun geç gelen body/401 yanıtını yeni oturuma uygulamaz. | Kaynak ve embedded regresyonları PASS; gecikmiş body/401 ayrıca sınandı |
| WEB-02 | Integer doğrulaması `Number.isInteger` kullanır; extraction aynı `Number()` anlamını korur. RF'nin kesirli ölçüm alanları açıkça float doğrulamasında kalır. | 37.9 reddi, 1e2=100 ve 0.3 float kabulü PASS |
| WEB-03 | Mevcut request timer yalnız `finally` içinde temizlenir; JSON ve hata body okumaları aynı bütçededir. | Bekleyen body'nin timer ile abort edilmesi, loading temizliği ve cache'in korunması PASS |
| WEB-04 | Geçerli FAILED bitmap'i, kabul edilen WRITE sırasına göre sorunlu üye numaraları olarak gösterilir. Faz kimliği olmadığı açıkça yazılır. Bilinmeyen bitmap için fallback açıklaması kalır. | Tek üye, seyrek bitmap 0x05 ve bilinmeyen bitmap; TR/EN PASS |
| WEB-05 | Menü mevcut parser'ın desteklediği 4800/9600/14400/19200/28800/38400/57600/115200 değerlerine uyar. 230400 sunulmaz. | Kaynak+embedded menü kontrolü PASS; C parser'ın önceki 230400 ret testi korunur |

Ek uygulamalar:

- User hesabı başlangıçta erişebildiği cihaz durumu ekranına açılır. Admin sayfaları menüden çıkarılır; RF/Modbus alanları ve EUI atama/Temizle kontrolleri salt okunur olur. Kaydet/abort denetimleri role göre kapalıdır. Loading kapanışı role bağlı kapatmayı kaldırmaz. Backend yetkileri değişmez.
- Üstte hedef host/port ve aktif hesap rolü gösterilir. Cihaz seri numarası/çalışan firmware için yeni bir backend endpoint eklenmedi; bu daha geniş kimlik ihtiyacı ayrı kalır.
- Son okuma bilgisinin yanında değerlerin snapshot olduğu ve veri yaşlarının otomatik değişmediği belirtilir. Yeni polling veya timer eklenmedi.
- Temizle/Kaydet/MH silme ve eksik üç fazlı fiderin atlanması açıklanır. AY yayını/korumasının durduğu iddia edilmez. Discovery listesi canlılık kanıtı gibi açıklanmaz.
- Browser reload/kapatma için yalnız dirty taslak varken `beforeunload` uyarısı eklenir.

**Test:** `test_industrial_audit.js` artık kalıcı regresyon olarak web_navigation Makefile'ına bağlıdır. 15 kontrol × source/embedded = **30/30 PASS**. Önceki 16 FAIL, düzeltme öncesi tarihsel reproduksiyondur. Normal navigation, firmware sayfası, loading, i18n, metadata ve minifier kontrolleri PASS; `web_auth` gerçek HTTP/transport host paketi PASS. Yerel tarayıcıda v1.4.12, hedef/rol başlığı, snapshot uyarısı, EUI açıklaması ve sorunlu üye 3 metni görüldü. Sentetik eksik preview alanları gerçek cihaz verisi gibi yorumlanmadı.

**ARM Release:** `make -C Release -j8 all` PASS; imzalı EFW self-check PASS (raw byte equality + ECDSA verify). Üretim binary 357704 B, CRC32 **EA7EAD29**. Paket: `Release/troika-smart-breaker-modem_v1.0.1_20261021_131023_a2901519.efw`. Önceden var olan kullanıcı C değişiklikleri build'e dahildir; paket yalnız bu UI değişikliğinin kimliği gibi sunulmaz. Vendor/Contiki warning-free iddiası yoktur.

**Kanıt dosyaları:** `.temp_files/web-audit-20261021/fixes-navigation.log`, `fixes-auth.log`, `fixes-release.log`. İlk 306/306 C test sonucu ilk incelemenin son birleşik koşusudur; frontend düzeltmesi sonrası tam C suite yeniden koşulduğu iddia edilmez.

**Bakım kontrolü:** Mevcut cache nesneleri, request token'ı, timer ve RF durumları kullanıldı; yeni kuyruk, kalıcı alan, reset/retry katmanı veya firmware API'si eklenmedi. AY silmenin RTU/MH protokol akışı değiştirilmedi. Gerçek kart kabulü, BQ-26, parola/transport modeli ve ayrıntılı kalıcı değişiklik kaydı açık kalır.

## 10. Sonraki gerçek cihaz kabulü — 21.10.2026

Kullanıcının firmware yükleme ve tam test talimatıyla son web v1.4.13
cihaza kuruldu. Uygulama CRC32 AE80B796, üretim profili 0; imzalı XMODEM,
FW_INSTALL_OK ve approved YES doğrulandı. v1.4.13 yalnız ek IEC104 yardım
metni düzeltmesini içerir: 0 periyodik gönderimi kapatır. Kaynak/gömülü
kalıcı audit kontrol sayısı 32'ye çıktı ve tamamı geçti.

Tam unit 1574/1574; 13 diğer entegrasyon paketi exit 0; gerçek RS485 ve
iki röle paketi 29/29; gerçek IEC104 13/13, skip/error yok. Gerçek MH'nin
kimlik, envanter ve iki AY canlı veri kontrolleri geçti. Son tarayıcı
matrisi 245 PASS, 0 FAIL ve araç maskelemesi nedeniyle iki
sonuçlandırılamayan hassas alan taslak kontrolüdür.

EUI silmenin gerçek MH ACK zinciri, resetten sonra RTU'da boş kalması,
geri atama ve canlı verinin dönmesi sınandı. Takip sorusu üzerine
discovery listesindeki Fider 1/L2 düğmesinden yeniden atama da
Kaydet/cihazdan geri okuma ile geçti. Silme ACK'i bilinen EUI'yi discovery
kuyruğuna ekler; bu sonuç yeni kablosuz keşif veya AY korumasının
kapatılması anlamına gelmez. BQ-26'ya kanıt eklendi; açık kalır.

Üçüncü AY test tanımı olduğundan fiziksel üç üyeli APPLIED kabulü açık
kalır. Yeni metadata/saat/ufak ekran ve tek HTTP istemcisi gözlemleri,
alan matrisi, test sınırları ve son ayar durumu
[cihaz kabul raporundadır](2026-10-21-web-device-acceptance.md).
