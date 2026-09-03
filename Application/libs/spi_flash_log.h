/**
 * @file spi_flash_log.h
 * @brief SPI NOR Flash üzerinde çalışan, güç kesintisinden etkilenmeyen, çok-bağlamlı (multi-context) loglama kütüphanesi.
 *
 * Bu modül, dairesel (circular) sektörler üzerinde sabit boyutlu log kayıtlarını tutar.
 * Genel entry yapısı her zaman aynıdır (seq + payload + crc); ancak payload boyutu
 * her log alanı (context) için ayrı ayrı belirlenebilir. Böylece tek bir kütüphane,
 * farklı payload boyutlarına sahip birden çok bağımsız log alanını (ör. sistem-logları,
 * kullanıcı-logları) aynı mantıkla yönetir.
 *
 * Her log alanı bir @ref log_ctx_t bağlamıyla temsil edilir; taban adres, sektör sayısı,
 * payload boyutu ve donanım erişim fonksiyonları bu bağlamda tutulur. Bağlam nesnesini
 * çağıran tahsis eder (statik veya global); kütüphane dinamik bellek kullanmaz.
 */

#ifndef SPI_FLASH_LOG_H
#define SPI_FLASH_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Sabitler                                                                  */
/* -------------------------------------------------------------------------- */

#define FLASH_PAGE_SIZE          256u    /* Çipin sayfa boyutu: program çağrıları buna göre bölünür */
#define LOG_SECTOR_SIZE          4096u   /* Çipin erase (sektör) boyutu — şimdilik sabit    */
#define LOG_MIN_SECTOR_COUNT     2u      /* Dairesel yapı için gereken en az sektör sayısı  */
#define LOG_MAX_PAYLOAD_SIZE     252u    /* Payload üst sınırı: 4 + 252 = 256 = 1 sayfa */

#define LOG_SEQ_SIZE             2u      /* 16-bit seq — wrap (sarma) destekli              */
#define LOG_CRC_SIZE             2u      /* CRC-16/CCITT-FALSE                              */
#define LOG_ENTRY_OVERHEAD       (LOG_SEQ_SIZE + LOG_CRC_SIZE)             /* = 4         */
#define LOG_MAX_ENTRY_SIZE       (LOG_ENTRY_OVERHEAD + LOG_MAX_PAYLOAD_SIZE) /* = 256     */

#define ENTRY_SEQ_EMPTY          0xFFFFu /* Silinmiş (0xFF) seq alanı = boş entry göstergesi */
#define LOG_SEQ_MODULUS          65536u  /* Seq değer uzayı                                  */
#define LOG_SEQ_HALFSPACE        32768u  /* Modüler karşılaştırmanın geçerlilik sınırı: ring
                                          * kapasitesinin bu değerin ALTINDA kalması gerekir
                                          * (log_init bunu doğrular).                        */

/* -------------------------------------------------------------------------- */
/* Durum Kodları                                                             */
/* -------------------------------------------------------------------------- */

typedef enum {
    LOG_OK                  =  0,   /**< İşlem başarılı                                     */
    LOG_ERR_INVALID_PARAM   = -1,   /**< Geçersiz parametre / yapılandırma (NULL, boyut...) */
    LOG_ERR_FLASH_READ      = -2,   /**< Flash okuma hatası                                 */
    LOG_ERR_FLASH_PROGRAM   = -3,   /**< Flash programlama hatası                           */
    LOG_ERR_FLASH_ERASE     = -4,   /**< Flash sektör silme hatası                          */
    LOG_ERR_NOT_INITIALIZED = -5,   /**< Bağlam (context) henüz init edilmedi               */
    LOG_ERR_SEQ_OVERFLOW    = -6    /**< KULLANILMIYOR: seq 16-bit ve sarmalı; ayrılmıştır  */
} log_status_t;

/* -------------------------------------------------------------------------- */
/* Donanım Sürücü Arayüzü (Fonksiyon Pointerları)                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Flash bellekten okuma fonksiyonu tipi.
 * @param addr Okunacak flash adresi.
 * @param buf Okunan verinin yazılacağı ara bellek.
 * @param len Okunacak bayt sayısı.
 * @return 0 ise başarılı, aksi halde hata kodu.
 */
typedef int (*log_flash_read_fn)(uint32_t addr, void *buf, size_t len);

/**
 * @brief Flash belleğe programlama (yazma) fonksiyonu tipi.
 * @param addr Programlanacak flash adresi (1->0 değişimi).
 * @param buf Yazılacak veri kaynağı.
 * @param len Yazılacak bayt sayısı.
 * @return 0 ise başarılı, aksi halde hata kodu.
 */
typedef int (*log_flash_program_fn)(uint32_t addr, const void *buf, size_t len);

/**
 * @brief Flash bellekte bir sektörün tamamını silme (0xFF yapma) fonksiyonu tipi.
 * @param sector_addr Silinecek sektörün başlangıç adresi.
 * @return 0 ise başarılı, aksi halde hata kodu.
 */
typedef int (*log_flash_erase_sector_fn)(uint32_t sector_addr);

/**
 * @brief SPI Flash operasyonlarını barındıran sürücü yapısı.
 *
 * @note Tüm fonksiyonlar bloklayıcı (blocking) olmalıdır: çağrı, işlem
 *       tamamlanana (WIP biti temizlenene) kadar dönmemelidir. Sürücünüz
 *       asenkron ise, bu kütüphanenin iki aşamalı yazması (payload → CRC)
 *       ve sektör geçişlerinde sıra garantisini sağlamak için bir
 *       busy-wait sarmalayıcı (wrapper) sağlayın (doküman Bölüm 14).
 */
typedef struct {
    log_flash_read_fn         read;         /**< Flash okuma pointer'ı         */
    log_flash_program_fn      program;      /**< Flash programlama pointer'ı   */
    log_flash_erase_sector_fn erase_sector; /**< Flash sektör silme pointer'ı  */
} log_flash_ops_t;

/* -------------------------------------------------------------------------- */
/* Yapılandırma (Configuration)                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bir log alanı (context) oluşturmak için gereken yapılandırma bilgileri.
 *
 * Payload boyutu her bağlam için burada bir kez belirlenir; genel entry yapısı
 * (seq + payload + crc) tüm bağlamlar için aynıdır. Farklı log tipleri kendi
 * sabit payload struct'larını tanımlayıp @c sizeof değerini @ref payload_size
 * olarak verir.
 */
typedef struct {
    uint32_t        base_addr;     /**< Flash'taki log alanı başlangıç adresi (sektöre hizalı)  */
    uint32_t        sector_count;  /**< Bu alana ayrılan sektör sayısı (>= LOG_MIN_SECTOR_COUNT) */
    uint32_t        payload_size;  /**< Bu bağlamın payload boyutu (1..LOG_MAX_PAYLOAD_SIZE)     */
    log_flash_ops_t ops;           /**< Bu bağlama ait donanım erişim fonksiyonları              */
} log_config_t;

/* -------------------------------------------------------------------------- */
/* Log Bağlamı (Context)                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bir log alanının tüm durumunu (yapılandırma + çalışma zamanı) tutan bağlam.
 *
 * Bu nesne çağıran tarafından tahsis edilir (statik/global önerilir) ve
 * @ref log_init ile başlatılır. Alanlara doğrudan erişmek yerine tanılama
 * fonksiyonlarını (log_get_*) kullanın.
 */
typedef struct {
    /* --- Yapılandırma (init sonrası değişmez) --- */
    uint32_t        base_addr;
    uint32_t        sector_count;
    uint32_t        payload_size;
    uint32_t        entry_size;          /**< Türetilmiş: seq + payload + crc          */
    uint32_t        entries_per_sector;  /**< Türetilmiş: LOG_SECTOR_SIZE / entry_size */
    log_flash_ops_t ops;

    /* --- Çalışma zamanı durumu --- */
    uint32_t        write_sector_index;  /**< Şu an yazılan sektör: 0..sector_count-1  */
    uint32_t        write_offset;        /**< Sektör içindeki bir sonraki boş yer      */
    uint32_t        next_seq;            /**< Bir sonraki entry'ye atanacak seq        */
    bool            initialized;         /**< log_init başarıyla tamamlandı mı         */
} log_ctx_t;

/**
 * @brief Log okuma ziyareti (visitor) fonksiyonu tipi.
 * @param payload Log içerik verisine pointer (bağlamın payload_size boyutunda).
 * @param payload_size Payload boyutu (bayt).
 * @param seq Log kayıt sıra numarası.
 * @param user_ctx Kullanıcı bağlamı (visitor'a aktarılan opak pointer).
 */
typedef void (*log_visit_fn)(const void *payload, uint32_t payload_size, uint32_t seq, void *user_ctx);

/* -------------------------------------------------------------------------- */
/* Genel Fonksiyon Prototipleri                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bir log bağlamını yapılandırır, başlatır ve açılışta Flash kurtarma
 *        (Boot Recovery) taraması yapar.
 *
 * @param ctx Başlatılacak bağlam nesnesi (çağıran tahsis eder).
 * @param cfg Yapılandırma bilgileri (kopyalanır; çağrıdan sonra saklanması gerekmez).
 * @return LOG_OK ise başarılı, aksi halde ilgili hata kodu.
 *
 * @note Doğrulamalar (hepsi LOG_ERR_INVALID_PARAM döner):
 *       - ctx/cfg veya ops üyelerinden biri NULL
 *       - sector_count < LOG_MIN_SECTOR_COUNT
 *       - payload_size 0 veya > LOG_MAX_PAYLOAD_SIZE
 *       - base_addr sektöre hizalı değil (LOG_SECTOR_SIZE'ın katı değil)
 *       - ring kapasitesi (sektör_sayısı x sektör_başı_kayıt) >= LOG_SEQ_HALFSPACE
 * @note Kurtarma politikası: yazım sırasında yarıda kalan (CRC'siz/torn) slotlar
 *       ASLA üzerine yazılmaz; head son geçerli kayıttan sonraki ilk BOŞ slota
 *       konur. Torn slot, sektör erase edilene kadar atılan bir "delik" olarak
 *       kalır; seq numaraları delikten sonraki kayıtla kesintisiz sürer.
 *       (NOR 1->0 kısıtı: corrupt slota yeniden yazmak, içerik değişkense
 *       eski ve yeni verinin AND'ine yol açar.)
 */
log_status_t log_init(log_ctx_t *ctx, const log_config_t *cfg);

/**
 * @brief Verilen bağlama yeni bir log kaydı yazar (iki aşamalı: seq+payload ardından CRC).
 * @param ctx Başlatılmış log bağlamı.
 * @param payload Yazılacak, bağlamın payload_size boyutundaki veri.
 * @return LOG_OK ise başarılı, aksi halde ilgili hata kodu.
 *
 * @note Eşzamanlılık: Aynı bağlam birden fazla görev/ISR bağlamından
 *       çağrılıyorsa, çağıran tarafın bu çağrıyı bir mutex/kritik bölge ile
 *       koruması gerekir. Farklı bağlamlar birbirinden bağımsızdır.
 * @note Sektör dolduğu çağrıda, bu fonksiyon erase süresi kadar bloklayabilir.
 * @note Seq sarması: seq 16-bittir ve 65534'ten sonra 0'a SARARAK devam eder;
 *       0xFFFF yalnızca "hiç yazılmamış slot" göstergesi olarak ayrıdır ve hiçbir
 *       kayda atanmaz. Tükenebilir bir ömür sınırı YOKTUR. Seq'leri karşılaştıran
 *       çağıranlar modüler aritmetik kullanmalıdır: a, b'den yeni ise
 *       (int16_t)((uint16_t)a - (uint16_t)b) > 0 (ring kapasitesi < 32768 iken güvenli).
 */
log_status_t log_write(log_ctx_t *ctx, const void *payload);

/**
 * @brief Bağlamdaki tüm geçerli logları en eskiden en yeniye kronolojik sırayla okur.
 * @param ctx Başlatılmış log bağlamı.
 * @param visit Her geçerli log kaydı için çağrılacak callback fonksiyonu.
 * @param user_ctx Callback fonksiyonuna aktarılacak kullanıcı pointer'ı.
 * @return LOG_OK ise başarılı, aksi halde ilgili hata kodu.
 */
log_status_t log_read_all(log_ctx_t *ctx, log_visit_fn visit, void *user_ctx);

/* -------------------------------------------------------------------------- */
/* Parçalı (imleçli) Okuma — buffer küçük, log büyük senaryosu                */
/* -------------------------------------------------------------------------- */

/**
 * @brief log_read_last() çağrılarının sonucu ve yineleme durumu.
 *
 * KULLANIM (ör. 10 kayıtlık buffer'la tüm logu taramak):
 * @code
 * log_page_ctx_t page = {0};            // memset(0) = en yeniden başla
 * do {
 *     log_read_last(&ctx, 10, visit, buf, &page);
 *     // page.page_count kayıt ziyaret edildi (yeni -> eski sırayla)
 * } while (page.has_more && page.page_count > 0);
 * @endcode
 *
 * Sonuç alanları her çağrıda güncellenir; _ ile başlayan alanlar kütüphanenin
 * yineleme durumudur (opak) — değiştirmeyin, sonraki çağrıda aynı yapıyı geri verin.
 */
typedef struct {
    /* --- Sonuç (çağıran okur) --- */
    uint32_t page_first_seq;   /**< Bu parçadaki en ESKİ geçerli seq (parça boşsa UINT32_MAX) */
    uint32_t page_last_seq;    /**< Bu parçadaki en YENİ geçerli seq (parça boşsa UINT32_MAX)  */
    uint32_t page_count;       /**< Bu parçada ziyaret edilen geçerli kayıt sayısı            */
    uint32_t skipped_corrupt;  /**< Bu parçada okunup CRC'si elenen (torn) slot sayısı        */
    bool     has_more;         /**< Daha eski kayit olabilir mi (bkz. log_read_last notu)     */

    /* --- Yineleme durumu (OPAK) --- */
    uint8_t  _state;           /**< 0: başlanmadı, 1: sürüyor, 2: bitti */
    uint32_t _sector;          /**< Sonraki incelenecek sektör          */
    uint32_t _offset;          /**< Sonraki incelenecek sektör içi ofset */
} log_page_ctx_t;

/**
 * @brief En yeni kayıttan geriye doğru, parça parça (imleçli) okuma.
 *
 * Her çağrı en fazla @p count geçerli kaydı YENİ -> ESKİ sırayla ziyaret eder
 * ve fiziksel konumu @p page içine kaydeder; aynı yapı sonraki çağrıyla geri
 * verildiğinde kaldığı yerden devam eder. Böylece küçük bir buffer'la tüm log
 * (kayıt sayısından bağımsız) tek geçişte, parça başına yeniden tarama
 * yapılmadan taranabilir.
 *
 * Torn (yarıda kalmış, CRC'si tutmayan) slot okuma aralığına denk gelirse
 * durulmaz: slot okunur, CRC kontrol edilir, geçersizse atlanır ve
 * @c skipped_corrupt sayacı artar. Geçerli kayıtların seq aralığı torn-skip
 * politikası gereği boşluksuz olduğundan parçalar kesişmez ve kayıt kaçmaz.
 *
 * @param ctx        Başlatılmış log bağlamı.
 * @param count      Bu parçada en fazla ziyaret edilecek geçerli kayıt sayısı.
 * @param visit      Her geçerli kayıt için çağrılacak callback (yeni→eski).
 * @param user_ctx   Callback'e aktarılacak opak pointer.
 * @param page       memset(0) ile başlatılmış imleç; sonraki çağrıda geri verilir.
 * @return LOG_OK ise başarılı; parça sonu/boşluk hata değildir (page alanlarına bakın).
 *
 * @note has_more == true iken bile bir sonraki çağrı page_count == 0
 *       dönebilir (tam sınır isabetleri); örnek döngüdeki koşul bunu ele alır.
 * @note İmleç yalnızca aynı bağlamda ve okuma süresince yazım yapılmadığı
 *       varsayımıyla geçerlidir (tıpkı log_read_all gibi).
 * @note NULL/0 argümanları LOG_ERR_INVALID_PARAM döndürür.
 */
log_status_t log_read_last(log_ctx_t *ctx, uint32_t count,
                           log_visit_fn visit, void *user_ctx,
                           log_page_ctx_t *page);

/* -------------------------------------------------------------------------- */
/* Tanılama ve Durum Sorgulama Yardımcı Fonksiyonları                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bağlamın başlatılıp başlatılmadığını sorgular.
 */
bool log_is_initialized(const log_ctx_t *ctx);

/**
 * @brief Şu an aktif olan yazma sektör indeksini döner (0..sector_count-1).
 */
uint32_t log_get_write_sector_index(const log_ctx_t *ctx);

/**
 * @brief Sektör içindeki bir sonraki yazma offset'ini döner.
 */
uint32_t log_get_write_offset(const log_ctx_t *ctx);

/**
 * @brief Bir sonraki yazılacak log sıra numarasını (seq) döner.
 */
uint32_t log_get_next_seq(const log_ctx_t *ctx);

/**
 * @brief Verilen veri bloğu için CRC-16 (CCITT-FALSE: poli 0x1021, init 0xFFFF,
 *        yansımasız) değeri hesaplar. Doğrulama vektörü: "123456789" -> 0x29B1.
 */
uint16_t log_calculate_crc16(const void *data, size_t len);

/**
 * @brief Verilen veri bloğu için CRC-32 değeri hesaplar (durumsuz yardımcı;
 *        entry bütünlüğü artık CRC-16 ile korunur — bu yardımcı genel amaçlıdır).
 */
uint32_t log_calculate_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SPI_FLASH_LOG_H */
