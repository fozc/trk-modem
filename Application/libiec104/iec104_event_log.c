/*
 * iec104_event_log.c
 *
 *  Created on: Mar 16, 2026
 *      Author: fatih
 *
 * IEC 104 ariza olaylarinin kalici gunlugu.
 *
 * Depolama spi_flash_log'a birakilmistir (append-only halka, iki asamali
 * yazim, torn-slot tespiti, ertelenmis erase). Bu modul yalnizca iki sey
 * ekler: kaydin CRC'sini hesaplamak ve "hangi kayitlar SCADA'ya gonderildi"
 * durumunu tutmak.
 *
 * Gonderim durumu bitmap degil, iki seq siniridir. Replay yeniden eskiye
 * gider ve hat acikken gelen yeni kayit aninda gonderilir; bu yuzden
 * gonderilmemis kayitlar her zaman tek parca bir seq araligi olusturur:
 *
 *   add(S)              -> aralik bossa low = S; her durumda high = S
 *   mark_sent(high)     -> high bir geri cekilir (aralik bosalabilir)
 *
 * Aralik NVRAM'de tutulur (iec104_evtlog_state_t). Kayit basina degil,
 * yalnizca hat kapaliyken kayit eklendiginde ve replay bitiminde senkronlanir;
 * nvram_sync() 8 KB'lik bolgeyi yeniden yazar.
 */

#define CSLOG_MODULE LOG_MOD_IEC104
#include "iec104_event_log.h"

#include <string.h>
#include <stdlib.h>

#include "nvram.h"
#include "crc32.h"
#include "cp56time2a.h"
#include "shell.h"
#include "w25qxx.h"
#include "spi_flash_organization.h"
#include "spi_flash_log.h"

/* Kapasite sabitleri (spi_flash_log sabitlerinden turetilmis hali; yalnizca
 * bu modulun ici icin - bkz. basliktaki NOT). */
#define IEC104_EVTLOG_ENTRY_SIZE   (LOG_ENTRY_OVERHEAD + sizeof(fault_log_t))
#define IEC104_EVTLOG_PER_SECTOR   (LOG_SECTOR_SIZE / IEC104_EVTLOG_ENTRY_SIZE)
#define IEC104_EVTLOG_MAX_ENTRIES  ((IEC104_EVTLOG_SECTOR_COUNT - 1U) * IEC104_EVTLOG_PER_SECTOR)

/* ────────────────────────────────────────────────────────── derleme kontrolleri */
_Static_assert(sizeof(fault_log_t) == 18U,
               "fault_log_t layout degisti - evtlog kapasitesi ve flash haritasi gecersiz");
_Static_assert(IEC104_EVTLOG_SECTOR_COUNT >= LOG_MIN_SECTOR_COUNT,
               "evtlog icin en az LOG_MIN_SECTOR_COUNT sektor gerekir");
_Static_assert(IEC104_EVTLOG_SECTOR_SIZE == LOG_SECTOR_SIZE,
               "evtlog sektor boyutu spi_flash_log ile uyusmuyor");

/* Seq 16-bit ve 0xFFFF "hic yazilmamis slot" icin ayrilmis. */
#define EVTLOG_SEQ_INVALID   0xFFFFU

/* ────────────────────────────────────────────────────────── modul durumu */
static log_ctx_t s_log;
static bool      s_initialized;

/* NVRAM'deki replay durumuna kisayol. */
#define s_state (nvram_get_iec104_evtlog_state())

/* log_read_last() imleci: replay tek gecisde ilerlesin diye cagrilar arasinda
 * korunur. Araya bir log_write() girerse imleç gecersizlesir (modul dokumani),
 * bu yuzden yazim sayaci ile karsilastirilir. */
static log_page_ctx_t s_cursor;
static uint32_t       s_cursor_gen;
static uint32_t       s_write_gen;

/* ────────────────────────────────────────────────────────── w25qxx adaptoru */

/* w25qxx_read_buff void doner: surucu katmani basarisiz SPI okumasini ayirt
 * edemiyor, bu yuzden adaptor daima basari bildirir (elog.c ile ayni durum).
 * Okuma hatasi log kutuphanesinde CRC/torn-slot reddi olarak gorunur. */
static int evtlog_flash_read(uint32_t addr, void *buf, size_t len)
{
    w25qxx_read_buff(addr, buf, (uint32_t)len);
    return 0;
}

/* Erase YAPMAYAN sayfa programlama olmali; w25qxx_write_buff kullanilamaz
 * (o oku-sil-yaz yapar ve log kutuphanesinin torn-slot garantisini bozar). */
static int evtlog_flash_program(uint32_t addr, const void *buf, size_t len)
{
    return w25qxx_page_write(addr, buf, (uint32_t)len);
}

static int evtlog_flash_erase_sector(uint32_t sector_addr)
{
    return w25qxx_erase_sector(sector_addr);
}

/* ────────────────────────────────────────────────────────── seq yardimcilari */

/* Modüler karsilastirma: a, b'den yeni ise > 0 (halka kapasitesi < 32768). */
static int16_t seq_diff(uint16_t a, uint16_t b)
{
    return (int16_t)((uint16_t)(a - b));
}

static uint16_t seq_prev(uint16_t seq)
{
    return (uint16_t)((0U == seq) ? (LOG_SEQ_MODULUS - 2U) : (seq - 1U));
}

/* ────────────────────────────────────────────────────────── kayit CRC'si */

static uint32_t record_calc_crc(const fault_log_t *r)
{
    crc32_t c = crc32_init();
    c = crc32_update(c, r, sizeof(fault_log_t) - sizeof(r->crc));
    return crc32_finalize(c);
}

/* ────────────────────────────────────────────────────────── unsent araligi */

static void unsent_clear(void)
{
    s_state->has_unsent = 0U;
    s_state->unsent_low = 0U;
    s_state->unsent_high = 0U;
}

static void unsent_extend(uint16_t seq)
{
    if (0U == s_state->has_unsent)
    {
        s_state->unsent_low = seq;
        s_state->has_unsent = 1U;
    }
    s_state->unsent_high = seq;
}

/* ────────────────────────────────────────────────────────── public API */

bool iec104_event_log_init(void)
{
    iec104_event_log_shell_init();

    const log_config_t cfg = {
        .base_addr    = IEC104_EVTLOG_ADDR,
        .sector_count = IEC104_EVTLOG_SECTOR_COUNT,
        .payload_size = sizeof(fault_log_t),
        .ops = {
            .read         = evtlog_flash_read,
            .program      = evtlog_flash_program,
            .erase_sector = evtlog_flash_erase_sector,
        },
    };

    const log_status_t rc = log_init(&s_log, &cfg);

    if (LOG_OK != rc)
    {
        CSLOG_ERR("evtlog: log_init basarisiz (%d)\r\n", (int)rc);
        return false;
    }

    s_initialized = true;
    s_write_gen   = 0U;
    s_cursor_gen  = 0U;
    (void)memset(&s_cursor, 0, sizeof(s_cursor));

    CSLOG("evtlog: hazir (kapasite=%u, unsent=%u)\r\n",
          (unsigned)IEC104_EVTLOG_MAX_ENTRIES,
          (unsigned)iec104_event_log_get_unsent_count());

    return true;
}

bool iec104_event_log_add(const fault_log_t *entry, uint16_t *seq_out)
{
    if (!s_initialized || (NULL == entry))
    {
        return false;
    }

    /* CRC'yi burada hesaplariz; cagiranin bunu bilmesi beklenmez. */
    fault_log_t record = *entry;
    record.crc = record_calc_crc(&record);

    if (LOG_OK != log_write(&s_log, &record))
    {
        CSLOG_ERR("evtlog: log_write basarisiz\r\n");
        return false;
    }

    s_write_gen++;

    const uint16_t seq = seq_prev((uint16_t)log_get_next_seq(&s_log));

    unsent_extend(seq);

    if (NULL != seq_out)
    {
        *seq_out = seq;
    }

    return true;
}

uint16_t iec104_event_log_get_unsent_count(void)
{
    if (!s_initialized || (0U == s_state->has_unsent))
    {
        return 0U;
    }

    uint16_t count = (uint16_t)((uint16_t)(s_state->unsent_high - s_state->unsent_low) + 1U);

    /* Bozuk NVRAM araligi 65535 uretebilir; replay dongusunun bosuna
     * donmesini engellemek icin fiziksel kapasiteyle sinirla. */
    if (count > IEC104_EVTLOG_MAX_ENTRIES)
    {
        count = (uint16_t)IEC104_EVTLOG_MAX_ENTRIES;
    }

    return count;
}

/* log_read_last() ziyaretcisi: bir kaydi disari tasir. */
typedef struct
{
    fault_log_t *out;
    uint16_t     seq;
    bool         valid;
} evtlog_visit_t;

static void visit_one(const void *payload, uint32_t payload_size, uint32_t seq, void *user_ctx)
{
    evtlog_visit_t *v = (evtlog_visit_t *)user_ctx;

    if (payload_size != sizeof(fault_log_t))
    {
        return;
    }

    (void)memcpy(v->out, payload, sizeof(fault_log_t));
    v->seq   = (uint16_t)seq;
    v->valid = true;
}

bool iec104_event_log_read_newest_unsent(fault_log_t *out, uint16_t *seq_out)
{
    if (!s_initialized || (NULL == out) || (0U == s_state->has_unsent))
    {
        return false;
    }

    /* Araya yazim girdiyse imleç gecersiz: bastan tara. */
    if (s_cursor_gen != s_write_gen)
    {
        (void)memset(&s_cursor, 0, sizeof(s_cursor));
        s_cursor_gen = s_write_gen;
    }

    /* Tarama tavani: NVRAM'deki aralik bozuksa (has_unsent=1 ama sinirlar
     * sacmaysa) log_read_last'in sonunda has_more=false demesine guvenmek
     * yerine fiziksel kapasite kadar taranip aralik temizlenir. */
    uint16_t scanned = 0U;

    while (scanned < (uint16_t)IEC104_EVTLOG_MAX_ENTRIES)
    {
        scanned++;

        evtlog_visit_t v = { .out = out, .seq = EVTLOG_SEQ_INVALID, .valid = false };

        if (LOG_OK != log_read_last(&s_log, 1U, visit_one, &v, &s_cursor))
        {
            return false;
        }

        if (!v.valid)
        {
            /* Parca bos gelebilir; daha eski kayit yoksa aralik tukendi. */
            if (!s_cursor.has_more)
            {
                unsent_clear();
                return false;
            }
            continue;
        }

        if (seq_diff(v.seq, s_state->unsent_high) > 0)
        {
            continue;   /* araligin ustunde: zaten gonderilmis */
        }

        if (seq_diff(v.seq, s_state->unsent_low) < 0)
        {
            /* Aralik halkadan dusmus: kalan gonderilmemis kayit yok. */
            unsent_clear();
            return false;
        }

        if (NULL != seq_out)
        {
            *seq_out = v.seq;
        }
        return true;
    }

    /* Tavan asildi: aralik guvenilmez. */
    unsent_clear();
    return false;
}

void iec104_event_log_mark_sent(uint16_t seq)
{
    if (!s_initialized || (0U == s_state->has_unsent))
    {
        return;
    }

    /* Yalnizca araligin ust ucu isaretlenebilir; replay hep oradan gonderir. */
    if (0 != seq_diff(seq, s_state->unsent_high))
    {
        return;
    }

    if (0 == seq_diff(s_state->unsent_high, s_state->unsent_low))
    {
        unsent_clear();
    }
    else
    {
        s_state->unsent_high = seq_prev(s_state->unsent_high);
    }
}

int iec104_event_log_sync(void)
{
    if (!s_initialized)
    {
        return -1;
    }

    return nvram_sync(false);
}

void iec104_event_log_clear(void)
{
    if (!s_initialized)
    {
        return;
    }

    for (uint32_t sector = 0U; sector < IEC104_EVTLOG_SECTOR_COUNT; sector++)
    {
        (void)evtlog_flash_erase_sector(IEC104_EVTLOG_ADDR + (sector * LOG_SECTOR_SIZE));
    }

    unsent_clear();
    (void)nvram_sync(false);

    s_initialized = false;
    (void)iec104_event_log_init();

    CSLOG("evtlog: temizlendi\r\n");
}

/* ────────────────────────────────────────────────────────── shell */

static void visit_dump(const void *payload, uint32_t payload_size, uint32_t seq, void *user_ctx)
{
    (void)user_ctx;

    if (payload_size != sizeof(fault_log_t))
    {
        return;
    }

    fault_log_t e;
    (void)memcpy(&e, payload, sizeof(e));

    const bool sent = (0U == s_state->has_unsent) ||
                      (seq_diff((uint16_t)seq, s_state->unsent_high) > 0) ||
                      (seq_diff((uint16_t)seq, s_state->unsent_low) < 0);

    SHELL_LOG("  seq=%5u sent=%u  F%u Ph%u %s  I=%.1fA  T=%ums  P=%s N=%s  %02u-%02u-%04u %02u:%02u:%02u\r\n",
              (unsigned)seq,
              sent ? 1U : 0U,
              (unsigned)(e.info.feeder + 1U),
              (unsigned)(e.info.phase + 1U),
              e.info.type ? "P" : "T",
              (double)fault_log_current_amps(&e),
              (unsigned)e.fault_duration_ms,
              e.info.power_status ? "1" : "0",
              e.info.nominal_current_status ? "0" : "1",
              (unsigned)e.tm.day,
              (unsigned)e.tm.month,
              (unsigned)(e.tm.year + 2000U),
              (unsigned)e.tm.hour,
              (unsigned)e.tm.minute,
              (unsigned)cp56time2a_get_second(&e.tm));
}
void iec104_event_log_dump(void)
{
    SHELL_LOG("\r\n=== iec104_event_log ==============================================\r\n");
    SHELL_LOG("  kayit boyutu : %u bayt (entry %u)\r\n",
              (unsigned)sizeof(fault_log_t), (unsigned)IEC104_EVTLOG_ENTRY_SIZE);
    SHELL_LOG("  kapasite     : %u  (%u sektor x %u)\r\n",
              (unsigned)IEC104_EVTLOG_MAX_ENTRIES,
              (unsigned)IEC104_EVTLOG_SECTOR_COUNT,
              (unsigned)IEC104_EVTLOG_PER_SECTOR);
    SHELL_LOG("  next_seq     : %lu\r\n", (unsigned long)log_get_next_seq(&s_log));
    SHELL_LOG("  unsent       : %u", (unsigned)iec104_event_log_get_unsent_count());

    if (0U != s_state->has_unsent)
    {
        SHELL_LOG("  [%u..%u]", (unsigned)s_state->unsent_low, (unsigned)s_state->unsent_high);
    }
    SHELL_LOG("\r\n  (yeniden eskiye)\r\n");
    SHELL_LOG("-------------------------------------------------------------------\r\n");

    log_page_ctx_t page = {0};

    do
    {
        (void)log_read_last(&s_log, 16U, visit_dump, NULL, &page);
    }
    while (page.has_more && (page.page_count > 0U));

    SHELL_LOG("===================================================================\r\n");
}

void iec104_event_log_test(uint16_t count)
{
    if (!s_initialized || (0U == count))
    {
        return;
    }

    for (uint16_t i = 0U; i < count; i++)
    {
        const uint32_t idx = log_get_next_seq(&s_log);

        fault_log_t e;
        (void)memset(&e, 0, sizeof(e));

        e.tm                          = cp56time2a_now();
        fault_log_set_current_amps(&e, 100.0f + (float)(idx % 900U) / 10.0f);
        e.fault_duration_ms           = (uint16_t)(100U + (idx % 50U) * 20U);
        e.info.feeder                 = (uint8_t)(idx % 8U);
        e.info.phase                  = (uint8_t)(idx % 3U);
        e.info.type                   = (uint8_t)(idx % 2U);
        e.info.nominal_current_status = (uint8_t)(((idx % 3U) == 0U) ? 1U : 0U);
        e.info.power_status           = (uint8_t)(idx % 2U);

        if (!iec104_event_log_add(&e, NULL))
        {
            SHELL_LOG("evtlog test: add basarisiz (i=%u)\r\n", (unsigned)i);
            break;
        }
    }

    (void)nvram_sync(false);

    SHELL_LOG("evtlog test: %u kayit eklendi - unsent=%u\r\n",
              (unsigned)count, (unsigned)iec104_event_log_get_unsent_count());
}

/* log_read_last ziyaretcisi: gecerli kayitlari sayar (status icin). */
static void visit_count(const void *payload, uint32_t payload_size, uint32_t seq, void *user_ctx)
{
    (void)payload;
    (void)seq;

    if (payload_size == sizeof(fault_log_t))
    {
        (*(uint32_t *)user_ctx)++;
    }
}

/* Ozet durum: kayit dökmeden saglik gorunumu (elog 'info' duzeni). */
static void evtlog_status(void)
{
    if (!s_initialized)
    {
        SHELL_LOG("evtlog: hazir degil (init edilmemis)\r\n");
        return;
    }

    uint32_t stored = 0U;
    log_page_ctx_t page = {0};

    do
    {
        (void)log_read_last(&s_log, 64U, visit_count, &stored, &page);
    }
    while (page.has_more && (page.page_count > 0U));

    SHELL_LOG("\r\n=== iec104evtlog status ==================================\r\n");
    SHELL_LOG("  durum        : hazir\r\n");
    SHELL_LOG("  kayit boyutu : %u bayt (entry %u)\r\n",
              (unsigned)sizeof(fault_log_t), (unsigned)IEC104_EVTLOG_ENTRY_SIZE);
    SHELL_LOG("  kapasite     : %u  (%u sektor x %u)\r\n",
              (unsigned)IEC104_EVTLOG_MAX_ENTRIES,
              (unsigned)IEC104_EVTLOG_SECTOR_COUNT,
              (unsigned)IEC104_EVTLOG_PER_SECTOR);
    SHELL_LOG("  next_seq     : %lu\r\n", (unsigned long)log_get_next_seq(&s_log));
    SHELL_LOG("  kayitli      : %lu\r\n", (unsigned long)stored);
    SHELL_LOG("  unsent       : %u",
              (unsigned)iec104_event_log_get_unsent_count());

    if (0U != s_state->has_unsent)
    {
        SHELL_LOG("  [%u..%u]",
                  (unsigned)s_state->unsent_low, (unsigned)s_state->unsent_high);
    }

    SHELL_LOG("\r\nKullanim: iec104evtlog [status|dump|test <N>|clear]\r\n");
}

static int shell_iec104evtlog(int argc, char *argv[])
{
    if ((argc <= 1) || (0 == strcmp(argv[1], "status")))
    {
        evtlog_status();
    }
    else if (0 == strcmp(argv[1], "dump"))
    {
        iec104_event_log_dump();
    }
    else if (0 == strcmp(argv[1], "test"))
    {
        iec104_event_log_test((argc > 2) ? (uint16_t)atoi(argv[2]) : 10U);
    }
    else if (0 == strcmp(argv[1], "clear"))
    {
        iec104_event_log_clear();
    }
    else
    {
        SHELL_LOG("Bilinmeyen alt komut: %s\r\n", argv[1]);
        SHELL_LOG("Kullanim: iec104evtlog [status|dump|test <N>|clear]\r\n");
    }

    return 0;
}

void iec104_event_log_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd  = "iec104evtlog",
        .desc = "IEC104 olay gunlugu: [status] | dump | test <N> | clear",
        .func = shell_iec104evtlog
    });
}

/*** end of file ***/
