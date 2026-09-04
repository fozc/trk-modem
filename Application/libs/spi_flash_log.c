/**
 * @file spi_flash_log.c
 * @brief SPI NOR Flash Loglama Sisteminin Çekirdek Uygulaması (çok-bağlamlı).
 */

#include "spi_flash_log.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Statik Doğrulamalar (R1 üst sınırı derleme zamanında da tutarlı olmalı)   */
/* -------------------------------------------------------------------------- */

_Static_assert(LOG_MAX_ENTRY_SIZE == FLASH_PAGE_SIZE, "max entry tam bir sayfa olmali");
_Static_assert(LOG_SECTOR_SIZE % FLASH_PAGE_SIZE == 0, "sektor, sayfanin tam kati olmali");

/* Entry içi alan offsetleri (payload_size bağlama göre değiştiği için türetilir):
 *   seq     : [0 .. LOG_SEQ_SIZE)
 *   payload : [LOG_SEQ_SIZE .. LOG_SEQ_SIZE + payload_size)
 *   crc32   : [LOG_SEQ_SIZE + payload_size .. entry_size)                     */

typedef enum {
    ENTRY_EMPTY,
    ENTRY_CORRUPT,
    ENTRY_VALID
} entry_state_t;

/* -------------------------------------------------------------------------- */
/* İç Yardımcı Fonksiyonlar                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief CRC-16/CCITT-FALSE (poli 0x1021, init 0xFFFF, yansımasız).
 *        Doğrulama vektörü: "123456789" -> 0x29B1.
 */
static uint16_t crc16_calc(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/**
 * @brief Standart IEEE 802.3 Ethernet CRC-32 hesaplama fonksiyonu.
 *        (Genel amaçlı yardımcı; entry bütünlüğü crc16_calc ile korunur.)
 */
static uint32_t crc32_calc(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/**
 * @brief Programlama cagrisini sayfa sinirlarinda boler.
 *
 * NOR page-program komutu sayfa (FLASH_PAGE_SIZE) icinde kalrmak zorundadir;
 * entry 256'nin kati olmak zorunda olmadigindan (R1 kaldirildi), bir entry
 * iki sayfaya tasabilir. Bu yardimci, verilen araligi sayfa sinirinda
 * parcalara ayirip her parcadan ops.program ister.
 *
 * Iki fazli yazma disiplini korunur: cagiran once seq+payload'i, sonra CRC'yi
 * programlar; CRC baytlari entry'nin sonunda oldugundan her zaman son parca
 * icinde kalir -> guc kesintisinde torn algilama bozulmaz.
 */
static int program_split(const log_ctx_t *ctx, uint32_t addr, const uint8_t *buf, uint32_t len) {
    while (len > 0u) {
        uint32_t page_rem = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);
        uint32_t chunk = (len < page_rem) ? len : page_rem;
        if (ctx->ops.program(addr, buf, chunk) != 0) {
            return -1;
        }
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return 0;
}

/* ---- Wrap destekli seq aritmetiği ----------------------------------------
 * seq 16-bittir; 0xFFFE'den sonra 0'a sarar (0xFFFF EMPTY olarak ayrıdır).
 * Modüler karşılaştırma, ring'in toplam kapasitesi LOG_SEQ_HALFSPACE (32768)
 * altında kaldığı sürece doğru sırayı verir — log_init bunu doğrular. */

static uint16_t seq_next(uint32_t seq) {
    uint32_t n = (seq + 1u) & 0xFFFFu;
    return (uint16_t)((n == ENTRY_SEQ_EMPTY) ? 0u : n);
}

static bool seq_newer(uint32_t a, uint32_t b) {
    return (int16_t)((uint16_t)a - (uint16_t)b) > 0;
}

/**
 * @brief Ham entry tamponundaki seq alanını okur (hizalama-güvenli, 16-bit).
 */
static uint16_t entry_get_seq(const uint8_t *buf) {
    uint16_t seq;
    memcpy(&seq, buf, LOG_SEQ_SIZE);
    return seq;
}

/**
 * @brief Ham entry tamponundaki crc16 alanını okur (hizalama-güvenli).
 */
static uint16_t entry_get_crc(const uint8_t *buf, uint32_t payload_size) {
    uint16_t crc;
    memcpy(&crc, buf + LOG_SEQ_SIZE + payload_size, LOG_CRC_SIZE);
    return crc;
}

/**
 * @brief Bir log kaydının durumunu kontrol eder.
 *
 * CRC yalnızca seq + payload üzerinden (ilk LOG_SEQ_SIZE + payload_size bayt)
 * hesaplanır; kendi alanını kapsamaz.
 */
static entry_state_t entry_check(const log_ctx_t *ctx, const uint8_t *buf) {
    if (entry_get_seq(buf) == ENTRY_SEQ_EMPTY) {
        return ENTRY_EMPTY;
    }
    uint16_t calc = crc16_calc(buf, LOG_SEQ_SIZE + ctx->payload_size);
    return (calc == entry_get_crc(buf, ctx->payload_size)) ? ENTRY_VALID : ENTRY_CORRUPT;
}

/**
 * @brief Bir sektörü tarayarak yazının gerçek sonunu bulur; torn (CORRUPT)
 *        delikleri ATLAYARAK son geçerli entry ve ilk boş slot bilgisini çıkarır.
 *
 * Kurallar:
 *   - ENTRY_EMPTY (seq=0xFFFFFFFF) : yazılmamış bölgenin kesin sonu -> DUR.
 *   - ENTRY_CORRUPT (CRC tutmuyor) : kesinti artığı delik      -> ATLA, devam et.
 *   - ENTRY_VALID                  : son geçerli offset/seq'i güncelle.
 *
 * "Boş slotta dur" kuralı güvenlidir: yazımlar daima head'den itibaren ardışık
 * gider ve head her zaman ilk boş slota konur; dolayısıyla bir boş slottan
 * sonra geçerli kayıt yazılmış olamaz (yarım erase bu değişmezi bozabilir,
 * ancak açılıştaki koşulsuz erase onu iyileştirir).
 */
static log_status_t scan_sector(const log_ctx_t *ctx,
                                 uint32_t sector_idx,
                                 uint32_t *out_first_empty_off,
                                 uint32_t *out_last_valid_off,
                                 uint32_t *out_last_valid_seq,
                                 bool     *out_sector_full) {
    uint32_t base = ctx->base_addr + sector_idx * LOG_SECTOR_SIZE;
    int32_t  last_valid = -1;
    uint32_t last_seq   = 0;
    uint8_t  buf[LOG_MAX_ENTRY_SIZE];
    bool     hit_empty  = false;
    uint32_t off        = 0;

    for (; off + ctx->entry_size <= LOG_SECTOR_SIZE; off += ctx->entry_size) {
        if (ctx->ops.read(base + off, buf, ctx->entry_size) != 0) {
            return LOG_ERR_FLASH_READ;
        }
        entry_state_t st = entry_check(ctx, buf);
        if (st == ENTRY_EMPTY) {
            hit_empty = true;
            break;               /* Yazılmamış bölge: kesin son. */
        }
        if (st == ENTRY_CORRUPT) {
            continue;            /* Torn delik: atla, sonrasına bak. */
        }
        last_valid = (int32_t)off;
        last_seq   = entry_get_seq(buf);
    }

    *out_first_empty_off = hit_empty ? off : LOG_SECTOR_SIZE;
    *out_last_valid_off  = (last_valid < 0) ? UINT32_MAX : (uint32_t)last_valid;
    *out_last_valid_seq  = last_seq;
    *out_sector_full     = !hit_empty;   /* Yazılabilir boş slot kalmadı */
    return LOG_OK;
}


/* -------------------------------------------------------------------------- */
/* Genel Arayüz Fonksiyonları                                                */
/* -------------------------------------------------------------------------- */

log_status_t log_init(log_ctx_t *ctx, const log_config_t *cfg) {
    if (ctx == NULL || cfg == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    if (cfg->ops.read == NULL || cfg->ops.program == NULL || cfg->ops.erase_sector == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    /* Dairesel yapı için en az LOG_MIN_SECTOR_COUNT sektör gerekir: yazma head'i
     * her zaman bir sektörü boş bırakır, geri kalanlar sağlam veriyi tutar. */
    if (cfg->sector_count < LOG_MIN_SECTOR_COUNT) {
        return LOG_ERR_INVALID_PARAM;
    }
    if (cfg->payload_size == 0u || cfg->payload_size > LOG_MAX_PAYLOAD_SIZE) {
        return LOG_ERR_INVALID_PARAM;
    }
    /* base_addr sektöre hizalı olmalı (erase granülaritesi). */
    if ((cfg->base_addr % LOG_SECTOR_SIZE) != 0u) {
        return LOG_ERR_INVALID_PARAM;
    }

    uint32_t entry_size = LOG_ENTRY_OVERHEAD + cfg->payload_size;
    /* R1 kuralı: entry hiçbir zaman iki flash sayfası arasında bölünmemeli.
     * FLASH_PAGE_SIZE, LOG_SECTOR_SIZE'ın böleni olduğundan entry_size sayfaya
     * tam sığıyorsa sektöre de tam sığar. */
    /* Modüler seq karşılaştırmalarının geçerliliği: ring'in toplam kapasitesi
     * yarım uzaydan (LOG_SEQ_HALFSPACE) küçük olmalı — aksi halde açılıştaki
     * "en yeni sektör" seçimi sarma bölgelerinde yanlış olabilir. */
    if (cfg->sector_count * (LOG_SECTOR_SIZE / entry_size) >= LOG_SEQ_HALFSPACE) {
        return LOG_ERR_INVALID_PARAM;
    }

    ctx->base_addr          = cfg->base_addr;
    ctx->sector_count       = cfg->sector_count;
    ctx->payload_size       = cfg->payload_size;
    ctx->entry_size         = entry_size;
    ctx->entries_per_sector = LOG_SECTOR_SIZE / entry_size;
    ctx->ops                = cfg->ops;
    ctx->initialized        = false;

    bool     found_any       = false;
    uint32_t best_sector     = 0;
    uint32_t best_first_empty = 0;
    uint32_t best_seq        = 0;
    bool     best_full       = false;
    uint32_t sec0_first_empty = LOG_SECTOR_SIZE;
    bool     sec0_full       = false;

    /* Sektörleri tarayarak en yüksek seq'e (en güncel) sahip sektörü bul.
     * Tarama torn (CORRUPT) delikleri atladığından, deliklerin ardındaki
     * geçerli kayıtlar da bulunur ve sector "dolu" ancak yazılabilir boş
     * slot kalmadığında kabul edilir. */
    for (uint32_t s = 0; s < ctx->sector_count; s++) {
        uint32_t first_empty, off, seq; bool full;
        log_status_t st = scan_sector(ctx, s, &first_empty, &off, &seq, &full);
        if (st != LOG_OK) {
            return st;
        }
        if (s == 0u) {
            sec0_first_empty = first_empty;
            sec0_full        = full;
        }
        if (off == UINT32_MAX) {
            continue;   /* Bu sektörde hiç geçerli entry yok */
        }
        if (!found_any || seq_newer(seq, best_seq)) {
            found_any        = true;
            best_sector      = s;
            best_first_empty = first_empty;
            best_seq         = seq;
            best_full        = full;
        }
    }

    if (!found_any) {
        /* Hiç geçerli kayıt yok (bakır flash ya da ilk yazımlarda kesinti
         * sonucu yalnızca delikler var): sektör 0'ın ilk BOŞ slotından başla.
         * Corrupt bir slota asla yazılmaz (NOR 1->0 AND bozulması riski). */
        if (sec0_full) {
            /* Patolojik: sektör 0'ın tamamı delik -> sonraki sektörü sil ve
             * oradan devam (idempotent erase sector 0'ı bir sonraki turda
             * temizleyecektir). */
            if (ctx->ops.erase_sector(ctx->base_addr + LOG_SECTOR_SIZE) != 0) {
                return LOG_ERR_FLASH_ERASE;
            }
            ctx->write_sector_index = 1u;
            ctx->write_offset       = 0u;
        } else {
            ctx->write_sector_index = 0u;
            ctx->write_offset       = sec0_first_empty;
        }
        ctx->next_seq    = 0u;
        ctx->initialized = true;
        return LOG_OK;
    }

    if (best_full) {
        /* Aktif sektörde yazılabilir boş slot kalmadı (tamamen dolu ya da
         * sonunda delikler birikti): koşulsuz bir sonraki sektörü sil ve
         * oradan devam et (idempotent erase — erase sırasında kesinti otomatik
         * çözülür; sector 0'daki patolojik delik birikimi de tur sonunda
         * bu yolla temizlenir). */
        uint32_t next = (best_sector + 1u) % ctx->sector_count;
        if (ctx->ops.erase_sector(ctx->base_addr + next * LOG_SECTOR_SIZE) != 0) {
            return LOG_ERR_FLASH_ERASE;
        }
        ctx->write_sector_index = next;
        ctx->write_offset       = 0;
        ctx->next_seq           = seq_next(best_seq);
    } else {
        /* Head: son geçerli kayıttan sonraki İLK BOŞ slot. Aradaki torn
         * slotlar atlanır: corrupt slota yeniden yazmak NOR'un 1->0 kısıtı
         * yüzünden eski ve yeni verinin AND'ine yol açar (içerik değişkense
         * kalıcı CRC hatası + açılış başına donma). Atlanan delikler sektör
         * erase'ine kadar 16-32 B yer feda eder; seq numaraları delikten
         * sonraki kayıtla kesintisiz sürer. */
        ctx->write_sector_index = best_sector;
        ctx->write_offset       = best_first_empty;
        ctx->next_seq           = seq_next(best_seq);
    }

    ctx->initialized = true;
    return LOG_OK;
}

log_status_t log_write(log_ctx_t *ctx, const void *payload) {
    if (ctx == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return LOG_ERR_NOT_INITIALIZED;
    }
    if (payload == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    /* Seq taşma sınırı yoktur: seq_next(), ENTRY_SEQ_EMPTY'yi atlayarak sarar. */

    uint8_t  buf[LOG_MAX_ENTRY_SIZE];
    uint16_t seq = (uint16_t)ctx->next_seq;
    uint32_t crc_off = LOG_SEQ_SIZE + ctx->payload_size;

    memcpy(buf, &seq, LOG_SEQ_SIZE);
    memcpy(buf + LOG_SEQ_SIZE, payload, ctx->payload_size);
    uint16_t crc = crc16_calc(buf, crc_off);
    memcpy(buf + crc_off, &crc, LOG_CRC_SIZE);

    uint32_t addr = ctx->base_addr
                  + ctx->write_sector_index * LOG_SECTOR_SIZE
                  + ctx->write_offset;

    /* 1. program komutu: seq + payload (sayfa sinirinda bolunebilir) */
    if (program_split(ctx, addr, buf, crc_off) != 0) {
        /* Torn slot: offset ilerlet ki ayni adrese yeniden yazilmasin.
         * NOR 1->0 AND kisiratmasi: farkli payload ayni slota yazilirsa
         * iki degerin AND'i kalir, CRC sonsuza dek tutmaz (O9.6). */
        ctx->write_offset += ctx->entry_size;
        return LOG_ERR_FLASH_PROGRAM;
    }
    /* 2. program komutu: crc (en son) */
    if (program_split(ctx, addr + crc_off, buf + crc_off, LOG_CRC_SIZE) != 0) {
        ctx->write_offset += ctx->entry_size;
        return LOG_ERR_FLASH_PROGRAM;
    }

    ctx->next_seq = seq_next(ctx->next_seq);   /* wrap: 0xFFFE -> 0 */
    ctx->write_offset += ctx->entry_size;

    /* Sektör sınırına ulaşıldıysa sonraki sektörü koşulsuz sil (lazy erase). */
    if (ctx->write_offset + ctx->entry_size > LOG_SECTOR_SIZE) {
        uint32_t next = (ctx->write_sector_index + 1u) % ctx->sector_count;
        if (ctx->ops.erase_sector(ctx->base_addr + next * LOG_SECTOR_SIZE) != 0) {
            return LOG_ERR_FLASH_ERASE;
        }
        ctx->write_sector_index = next;
        ctx->write_offset       = 0;
    }

    return LOG_OK;
}

log_status_t log_read_all(log_ctx_t *ctx, log_visit_fn visit, void *user_ctx) {
    if (ctx == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return LOG_ERR_NOT_INITIALIZED;
    }
    if (visit == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }

    uint32_t next_after_active = (ctx->write_sector_index + 1u) % ctx->sector_count;
    uint32_t probe_first_empty = 0, probe_off = 0, probe_seq = 0;
    bool probe_full = false;

    log_status_t st = scan_sector(ctx, next_after_active,
                                  &probe_first_empty, &probe_off, &probe_seq, &probe_full);
    if (st != LOG_OK) {
        return st;
    }

    bool wrapped = (probe_off != UINT32_MAX);
    uint32_t start_sector = wrapped ? next_after_active : 0;
    uint32_t s = start_sector;
    uint8_t  buf[LOG_MAX_ENTRY_SIZE];

    for (;;) {
        uint32_t base  = ctx->base_addr + s * LOG_SECTOR_SIZE;
        uint32_t limit = (s == ctx->write_sector_index)
                       ? ctx->write_offset
                       : ctx->entries_per_sector * ctx->entry_size;

        for (uint32_t off = 0; off < limit; off += ctx->entry_size) {
            if (ctx->ops.read(base + off, buf, ctx->entry_size) != 0) {
                return LOG_ERR_FLASH_READ;
            }
            if (entry_check(ctx, buf) == ENTRY_VALID) {
                visit(buf + LOG_SEQ_SIZE, ctx->payload_size, entry_get_seq(buf), user_ctx);
            }
        }
        if (s == ctx->write_sector_index) {
            break;
        }
        s = (s + 1u) % ctx->sector_count;
    }

    return LOG_OK;
}

log_status_t log_read_last(log_ctx_t *ctx, uint32_t count,
                           log_visit_fn visit, void *user_ctx,
                           log_page_ctx_t *page) {
    if (ctx == NULL || page == NULL) {
        return LOG_ERR_INVALID_PARAM;
    }
    if (!ctx->initialized) {
        return LOG_ERR_NOT_INITIALIZED;
    }
    if (visit == NULL || count == 0u) {
        return LOG_ERR_INVALID_PARAM;
    }

    /* Sonuç alanlarını sıfırla (imleç korunur) */
    page->page_first_seq  = UINT32_MAX;
    page->page_last_seq   = UINT32_MAX;
    page->page_count      = 0u;
    page->skipped_corrupt = 0u;
    page->has_more        = false;

    if (page->_state == 2u) {
        return LOG_OK;                    /* Yineleme çoktan bitti */
    }
    if (page->_state == 0u) {
        /* "Log boş" için next_seq sınanmaz: seq 0xFFFE'den sonra 0'a sardığı
         * için bu değer boş logu değil sarmayı da gösterebilir. Boş log zaten
         * aşağıdaki geri tarama ilk ENTRY_EMPTY slotta durarak saptanır. */
        page->_state  = 1u;
        /* Yazma head'inin hemen gerisinden (yazılmış son slottan) başla */
        page->_sector = ctx->write_sector_index;
        page->_offset = ctx->write_offset;
    }

    uint8_t  buf[LOG_MAX_ENTRY_SIZE];
    uint32_t eps      = ctx->entries_per_sector;
    uint32_t examined = 0u;
    uint32_t lap_cap  = ctx->sector_count * eps;   /* tam bir halka: güvenlik sınıri */

    while (page->page_count < count) {
        if (examined++ >= lap_cap) {
            /* Tüm halka tarandı (tümü torn gibi patolojik durum) */
            page->_state = 2u;
            break;
        }
        /* Bir slot geriye */
        if (page->_offset == 0u) {
            page->_sector = (page->_sector + ctx->sector_count - 1u) % ctx->sector_count;
            page->_offset = (eps - 1u) * ctx->entry_size;
        } else {
            page->_offset -= ctx->entry_size;
        }

        uint32_t addr = ctx->base_addr
                      + page->_sector * LOG_SECTOR_SIZE
                      + page->_offset;
        if (ctx->ops.read(addr, buf, ctx->entry_size) != 0) {
            return LOG_ERR_FLASH_READ;
        }

        entry_state_t st = entry_check(ctx, buf);
        if (st == ENTRY_EMPTY) {
            /* Yazılmamış bölgeye ulaşıldı: logun başı */
            page->_state = 2u;
            break;
        }
        if (st == ENTRY_CORRUPT) {
            /* Torn delik: okundu, CRC tutmadı -> atla ve say */
            page->skipped_corrupt++;
            continue;
        }

        uint32_t seq = entry_get_seq(buf);
        if (page->page_count == 0u) {
            page->page_last_seq = seq;    /* ilk ziyaret = parçanın en yenisi */
        }
        page->page_first_seq = seq;       /* son ziyaret = parçanın en eskisi  */
        page->page_count++;
        visit(buf + LOG_SEQ_SIZE, ctx->payload_size, seq, user_ctx);
    }

    /* Parça dolduysa daha eski kayıt OLABİLİR (bir sonraki çağrı kesinleştirir);
     * veri sonuna gelindiyse yukarıda _state=2 ve has_more=false kaldı. */
    if (page->_state != 2u) {
        page->has_more = true;
    }
    return LOG_OK;
}

/* -------------------------------------------------------------------------- */
/* Tanılama ve Yardımcı Fonksiyonlar                                          */
/* -------------------------------------------------------------------------- */

bool log_is_initialized(const log_ctx_t *ctx) {
    return (ctx != NULL) && ctx->initialized;
}

uint32_t log_get_write_sector_index(const log_ctx_t *ctx) {
    return (ctx != NULL) ? ctx->write_sector_index : 0u;
}

uint32_t log_get_write_offset(const log_ctx_t *ctx) {
    return (ctx != NULL) ? ctx->write_offset : 0u;
}

uint32_t log_get_next_seq(const log_ctx_t *ctx) {
    return (ctx != NULL) ? ctx->next_seq : 0u;
}

uint16_t log_calculate_crc16(const void *data, size_t len) {
    if (data == NULL || len == 0u) return 0u;
    return crc16_calc(data, len);
}

uint32_t log_calculate_crc32(const void *data, size_t len) {
    if (data == NULL || len == 0) return 0;
    return crc32_calc(data, len);
}

