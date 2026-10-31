# IEC104 tip ve callback örnekleri

## Amaç

Uygulama callback'lerinin kullanımını ve ileride eklenecek tiplerin yolunu
göstermek. Bu klasör firmware'e otomatik bağlanmaz; örnek ölçümler gerçek
RF/NVRAM verisi sayılmaz. Tip numarasının tanımlı olması o tipin uygulandığı
anlamına gelmez.

## Kullanım yeri

`iec104_example.c/.h` derlenebilir bir uygulama örneğidir. Tek `handle_request` callback'i
içinde grup seçimi, nokta okuma ve devam konumu yönetimini içerir. Type 1/13/30/31/36
ve encoded extension ile Type 7 örneği Ceedling testleriyle çalıştırılır.
Aynı callback'ler başka bir ürüne alınabilir; örnek IOA'lar ve veri kaynakları
ürünün sözleşmesine göre değiştirilmelidir.

## Callback bağlantısı

```c
iec104_example_data_t values = { 0 };
iec104_server_io_t io = {
    .context = &values,
    .write = application_tcp_write,
    .handle_request = iec104_example_handle_request
};
```

`application_tcp_write` projenin kendi transport (taşıma) callback'idir;
context burada `values` pointer'ıdır. Portun ek bağlamı gerekiyorsa ölçüm ve
transport bağlamı uygulamanın tek context struct'ında birleştirilmelidir.
Saat gibi ürün işlemleri aynı request callback'inde Type ID ile seçilir.
Resetin fiziksel yürütülmesi, pozitif cevaptan sonraki `request_reset`
bildirimindedir. ACK için `delivered` yalnız kalıcı teslim takibi gerekiyorsa
sağlanır. Boş başarılı fonksiyon eklenmemelidir. `handle_request = NULL`
veya UNSUPPORTED sonucu uygun negatif protokol cevabı oluşturur.

Akış:

```text
Istek -> application.handle_request(request, response)
      -> application nokta/IOA/deger secer
      -> iec104_response_add(response, point)
      -> kutuphane hazir parcayi gonderir
GI devam ediyorsa -> ayni callback, continuation=true
```

İlk çağrı listeyi başlatabilir veya ilk parçayı da hazırlayabilir. Sonraki
parça gerekiyorsa `response.more=true` verilir. Örnek, kendi
`data.query_index` alanını ilerletir; library liste dolaşmaz. Paket dolu veya
noktaların tipi farklıysa `iec104_response_add()` false verir; konum
ilerletilmeden mevcut parça tamamlanır. Son parçanın more alanı false olur.
Taşıma beklemesinde library aynı hazır byte'ları tekrar dener, callback'i
tekrar çalıştırmaz.

Örnek GI20, zamansız 100/101/105 IOA'larını seçer. GI21, uygulama profili
örneği olarak zamanlı 102/103/104 IOA'larını seçer. GI21'in zamanlı seçimi
standart uyumluluk iddiası değildir; gerçek projede SCADA profilinin izin
verdiği tipler seçilmelidir. `request.qualifier` QOI'yi taşır; aynı IOA'nın farklı
sorgularda farklı zaman gösterimiyle hazırlanması uygulamada seçilebilir.

## Periyodik gönderim örneği

`iec104_example_publish_periodic(server, data)` örneği yalnız seçilen
ölçümü COT=1 ile yayınlar. Application bu fonksiyonu kendi mevcut
zamanlayıcısından çağırır; false dönünce ilerlemeyi tamamlanmış saymaz.
Gerçek modem içeriği `iec104_modem.c` içindeki `periodic_live_group` ve
`send_periodic()` bölümünden seçilir.

## Reset SBO örneği

Application kendi `handle_request` fonksiyonunda Type 45 için reset IOA,
`request.single_command.state` ve `request.single_command.qualifier`
alanlarını doğrular. Library `reset_sbo=true` iken aynı bağlantı/OA/değer
ve deadline koşulunu yönetir. Örnek sıra `Type45/reset_ioa/SCO=0x81`, ardından
`Type45/reset_ioa/SCO=0x01` şeklindedir. Type 105'e S/E biti eklenmez.
Gerçek cihaz eylemi yalnız pozitif Execute ACT_CON/ACT_TERM zincirinden sonraki
`request_reset` callback'inde yapılmalıdır. Tam kurallar
[kütüphane rehberindedir](../README.md#reset-için-opsiyonel-sbo).

## Tip ekleme yolu

1. Aşağıdaki katalogdan tip ve ürünün IEC104 profili kontrol edilmelidir.
2. Mevcut setter yeterliyse uygulama yalnız IOA/veri/kalite/zaman sağlamalıdır.
3. Yeni monitor tipinin encoder'ı kütüphane tarafına eklenmelidir. Kısa bir
   prototip için `iec104_point_set_encoded()` kullanılabilir; bu fonksiyon
   yalnız sınır kontrolü ve kopyalama yapar, tipin anlamını doğrulamaz.
4. Komut/parametre için `handle_request` içinde Type ID ile gerçek
   doğrulama ve yan etki eklenmelidir. Okuma komutu Type 102 ayrıca COT=REQUEST ve cevap akışı
   gerektirir; GI callback'i tek başına Type 102 desteği değildir.
5. Mevcut ortak komut doğrulaması yalnız activation komutlarını yönetir.
   Değişken boyutlu dosya/güvenlik servisi için servis doğrulaması ve işlem
   akışı ayrıca uygulanmalıdır; bunlara sahte başarılı handler eklenmemelidir.
6. Tekli/çoklu tam-frame golden vektörleri ve hata yolları merkezi testlere
   eklenmelidir. Kalıcı verinin tüketimi yalnız protokol ACK'ine bağlanmalıdır.

## Kapasite ve değişken boyut

Varsayılan `IEC104_POINT_VALUE_MAX=12` korunur. Projede bütün kütüphane ve
uygulama derlemelerine aynı `-DIEC104_POINT_VALUE_MAX=N` verilerek 12–240
arasında seçilebilir. Bu, her noktanın ve stack'teki nokta dizilerinin
belleğini büyütür; ihtiyaç yokken 240 yapılmamalıdır. Büyük dosya parçaları
monitor noktası yerine mevcut ASDU builder ile hazırlanabilir. Builder
249 byte ASDU, tek SQ=0 nesnede en fazla 240 byte eleman taşır.
Tek ASDU'daki nesneler aynı Type ID ve aynı eleman boyutunda olmalıdır.

## Tam tip kataloğu

Aşağıdaki liste referans lib60870'nin ortak Type ID listesidir; 101'e özgü
ve güvenlik uzantısı tiplerini de içerir. Her tip her IEC104 profilinde
geçerli değildir. Özellikle CP24 tipleri için ürünün 104 profili ayrıca
kontrol edilmelidir. Rezerve numaralar listelenmez; 128–255 özel tip alanı
ürünler arası sözleşmeyle kullanılabilir.

**Durum:** `Hazır`, gerçek encoder/servis vardır demektir. `Örnek`, yalnız
bu klasörde test edilen extension encoder'ı vardır. `Eklenecek`, Type ID
adlandırılmıştır; gerçek encoder/servis henüz yoktur. Satırlar tamamlanmış
özellik gibi yorumlanmamalıdır.

| ID | Tip | Durum ve uygulama yolu |
|---|---|---|
| 1 | `M_SP_NA_1` | Hazır: `iec104_point_set_single()` |
| 2 | `M_SP_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 3 | `M_DP_NA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 4 | `M_DP_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 5 | `M_ST_NA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 6 | `M_ST_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 7 | `M_BO_NA_1` | Örnek: `iec104_example_set_bitstring()`; çekirdeğe kayıtlı komut değildir |
| 8 | `M_BO_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 9 | `M_ME_NA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 10 | `M_ME_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 11 | `M_ME_NB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 12 | `M_ME_TB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 13 | `M_ME_NC_1` | Hazır: `iec104_point_set_float()` |
| 14 | `M_ME_TC_1` | Eklenecek: tipli monitor encoder + handle_request |
| 15 | `M_IT_NA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 16 | `M_IT_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 17 | `M_EP_TA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 18 | `M_EP_TB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 19 | `M_EP_TC_1` | Eklenecek: tipli monitor encoder + handle_request |
| 20 | `M_PS_NA_1` | Eklenecek: tipli monitor encoder + handle_request |
| 21 | `M_ME_ND_1` | Eklenecek: tipli monitor encoder + handle_request |
| 30 | `M_SP_TB_1` | Hazır: `iec104_point_set_single()` |
| 31 | `M_DP_TB_1` | Hazır: `iec104_point_set_double()` |
| 32 | `M_ST_TB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 33 | `M_BO_TB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 34 | `M_ME_TD_1` | Eklenecek: tipli monitor encoder + handle_request |
| 35 | `M_ME_TE_1` | Eklenecek: tipli monitor encoder + handle_request |
| 36 | `M_ME_TF_1` | Hazır: `iec104_point_set_float()` |
| 37 | `M_IT_TB_1` | Eklenecek: tipli monitor encoder + handle_request |
| 38 | `M_EP_TD_1` | Eklenecek: tipli monitor encoder + handle_request |
| 39 | `M_EP_TE_1` | Eklenecek: tipli monitor encoder + handle_request |
| 40 | `M_EP_TF_1` | Eklenecek: tipli monitor encoder + handle_request |
| 41 | `S_IT_TC_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 45 | `C_SC_NA_1` | Hazır: SCO encoder/parser; yapılandırılmış reset IOA'sında opsiyonel SBO |
| 46 | `C_DC_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 47 | `C_RC_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 48 | `C_SE_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 49 | `C_SE_NB_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 50 | `C_SE_NC_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 51 | `C_BO_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 58 | `C_SC_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 59 | `C_DC_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 60 | `C_RC_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 61 | `C_SE_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 62 | `C_SE_TB_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 63 | `C_SE_TC_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 64 | `C_BO_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 70 | `M_EI_NA_1` | Hazır: server başlangıç bildirimi |
| 81 | `S_CH_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 82 | `S_RP_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 83 | `S_AR_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 84 | `S_KR_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 85 | `S_KS_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 86 | `S_KC_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 87 | `S_ER_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 90 | `S_US_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 91 | `S_UQ_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 92 | `S_UR_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 93 | `S_UK_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 94 | `S_UA_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 95 | `S_UC_NA_1` | Eklenecek: güvenlik servisi ve durum/anahtar yönetimi |
| 100 | `C_IC_NA_1` | Hazır: handle_request + application veri seçimi |
| 101 | `C_CI_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 102 | `C_RD_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 103 | `C_CS_NA_1` | Hazır: handle_request; uygulama RTC servisini sağlar |
| 104 | `C_TS_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 105 | `C_RP_NA_1` | Hazır: SBO kapalıyken genel reset QRP=1; açıkken negatif cevap |
| 106 | `C_CD_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 107 | `C_TS_TA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 110 | `P_ME_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 111 | `P_ME_NB_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 112 | `P_ME_NC_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 113 | `P_AC_NA_1` | Eklenecek: komut/parametre handler ve tipe uygun COT/IOA doğrulaması |
| 120 | `F_FR_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 121 | `F_SR_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 122 | `F_SC_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 123 | `F_LS_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 124 | `F_AF_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 125 | `F_SG_NA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 126 | `F_DR_TA_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |
| 127 | `F_SC_NB_1` | Eklenecek: dosya servisi, değişken boyut ve işlem akışı |

## Doğrulama ve referans

Repo kökünden `cd test` ve `ruby -S ceedling "test:pattern[iec104_stack]"`
çalıştırılır. `test_iec104_stack_examples.c` gerçek örnek callback'lerini
server/core ile çalıştırır; liste ve okuyucu yerine algoritma mock edilmez.

Type ID kaynağı: [lib60870 ortak API](https://github.com/mz-automation/lib60870/blob/master/lib60870-C/src/inc/api/iec60870_common.h).
Callback kullanım referansı: [resmî server örneği](https://github.com/mz-automation/lib60870/blob/master/lib60870-C/examples/cs104_server/simple_server.c).
Kontrol tarihi: 30.10.2026. Upstream uygulama kodu kopyalanmamıştır.

## Değişiklik geçmişi

| Tarih | Değişiklik |
|---|---|
| 30.10.2026 | Tek GI handler, IOA okuyucusu, derlenebilir örnekler ve tam Type ID kataloğu |
| 30.10.2026 | Örnek tek handle_request callback'ine sadeleştirildi |
