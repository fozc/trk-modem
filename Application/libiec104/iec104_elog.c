/**
 * @file  iec104_elog.c
 * @brief IEC-104 diagnostic event log (connection-level events only).
 *
 * Structural mirror of elog.c (envelope, append-only ring via
 * libs/spi_flash_log, shell dump/clear/info) with its own flash area,
 * event codes and decode table. Kept deliberately small: the event set
 * is connect/disconnect only.
 *
 * Reference layout: spi_flash_organization.h IEC104_LOG_* macros.
 */
#define CSLOG_MODULE LOG_MOD_IEC104
#include "iec104_elog.h"

#include <string.h>
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "w25qxx.h"
#include "rtc.h"
#include "datetime.h"
#include "bsp.h"
#include "shell.h"
#include "xprintf.h"
#include "utils.h"
#include "console_logger.h"

/* ======================================================================
 *  Ring storage context + flash ops
 * ====================================================================== */

static log_ctx_t iec104_elog_ctx;

static int iec104_elog_flash_read(uint32_t addr, void *buf, size_t len)
{
    w25qxx_read_buff(addr, buf, (uint32_t)len);
    return 0;
}

static int iec104_elog_flash_program(uint32_t addr, const void *buf, size_t len)
{
    return w25qxx_page_write(addr, buf, (uint32_t)len);
}

static int iec104_elog_flash_erase_sector(uint32_t sector_addr)
{
    return w25qxx_erase_sector(sector_addr);
}

static void iec104_elog_config_build(log_config_t *cfg)
{
    cfg->base_addr = IEC104_LOGAREA_ADDRESS;
    cfg->sector_count = IEC104_LOG_SECTOR_COUNT;
    cfg->payload_size = (uint32_t)sizeof(iec104_elog_entry_t);
    cfg->ops.read = iec104_elog_flash_read;
    cfg->ops.program = iec104_elog_flash_program;
    cfg->ops.erase_sector = iec104_elog_flash_erase_sector;
}

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

static uint32_t iec104_elog_timestamp_now(void)
{
    rtc_t dt = rtc_now();
    datetime_t dt2 = {
        .date.day = dt.day,
        .date.month = dt.month,
        .date.year = 2000U + (uint16_t)dt.year,
        .time.hour = dt.hour,
        .time.minute = dt.minute,
        .time.second = dt.second
    };
    return dt_conv_to_unix(&dt2);
}

static void iec104_elog_write_entry(const iec104_elog_entry_t *entry)
{
    if (!log_is_initialized(&iec104_elog_ctx))
    {
        return;
    }

    log_status_t st = log_write(&iec104_elog_ctx, entry);
    if (st != LOG_OK)
    {
        CSLOG_ERR("[IEC104ELOG] write failed (%d)\r\n", (int)st);
    }
}

static void iec104_elog_add(iec104_elog_code_t code, iec104_elog_level_t level,
                            const void *data, size_t data_len)
{
    iec104_elog_entry_t entry = {0};

    entry.timestamp = iec104_elog_timestamp_now();
    entry.entry_id = (uint16_t)(log_get_next_seq(&iec104_elog_ctx) & 0xFFFFU);
    entry.level = (uint8_t)level;
    entry.code = (uint8_t)code;
    if ((data != NULL) && (data_len > 0U))
    {
        size_t len = (data_len < sizeof(entry.info)) ? data_len : sizeof(entry.info);
        memcpy(entry.info, data, len);
    }

    iec104_elog_write_entry(&entry);
}

/* ======================================================================
 *  Semantic API
 * ====================================================================== */

/* One record per side per window; a flapping SCADA link must not flood
 * the ring. Applied symmetrically to connect and disconnect records. */
#define IEC104_ELOG_FLAP_MIN_INTERVAL_MS 60000UL

void iec104_elog_connected(uint32_t peer_ip)
{
    /* info: up=1(1) reason=1 client connected(1) peer_ip(4 BE) */
    static uint32_t last_log_tick = 0U;
    uint32_t now = bsp_get_tick();

    /* Connect tarafi da flap supresyonlu: 60 sn'de en fazla bir kayit. */
    if ((last_log_tick != 0U) &&
        ((now - last_log_tick) < IEC104_ELOG_FLAP_MIN_INTERVAL_MS))
    {
        return;
    }
    last_log_tick = now;

    uint8_t info[16] = {0};

    info[0] = 1U;
    info[1] = 1U;
    info[2] = (uint8_t)(peer_ip >> 24);
    info[3] = (uint8_t)(peer_ip >> 16);
    info[4] = (uint8_t)(peer_ip >> 8);
    info[5] = (uint8_t)peer_ip;
    iec104_elog_add(IEC104_ELOG_CONN, IEC104_ELOG_LEVEL_INFO, info, sizeof(info));
}

void iec104_elog_disconnected(iec104_elog_disc_reason_t reason)
{
    /* info: up=0(1) disc reason(1) reserved(2) */
    static uint32_t last_log_tick = 0U;
    uint32_t now = bsp_get_tick();

    if ((last_log_tick != 0U) &&
        ((now - last_log_tick) < IEC104_ELOG_FLAP_MIN_INTERVAL_MS))
    {
        return;
    }
    last_log_tick = now;

    uint8_t info[16] = {0};

    info[0] = 0U;
    info[1] = (uint8_t)reason;
    iec104_elog_add(IEC104_ELOG_DISC, IEC104_ELOG_LEVEL_WARN, info, sizeof(info));
}

/* ======================================================================
 *  Read / clear / counters
 * ====================================================================== */

typedef struct
{
    iec104_elog_entry_t *out;
    uint32_t capacity;
    uint32_t copied;
} iec104_elog_collect_ctx_t;

static void iec104_elog_discard_visitor(const void *payload, uint32_t payload_size,
                                        uint32_t seq, void *user_ctx)
{
    (void)payload;
    (void)payload_size;
    (void)seq;
    (void)user_ctx;
}

static void iec104_elog_collect_visitor(const void *payload, uint32_t payload_size,
                                        uint32_t seq, void *user_ctx)
{
    (void)payload_size;
    (void)seq;
    iec104_elog_collect_ctx_t *cc = (iec104_elog_collect_ctx_t *)user_ctx;
    if ((cc != NULL) && (cc->copied < cc->capacity))
    {
        memcpy(&cc->out[cc->copied], payload, sizeof(iec104_elog_entry_t));
        cc->copied++;
    }
}

int iec104_elog_read_recent(uint32_t skip_newest, uint32_t count,
                            iec104_elog_entry_t *entries, uint32_t *out_count)
{
    if (!log_is_initialized(&iec104_elog_ctx) || NULL == entries || NULL == out_count)
    {
        return 1;
    }

    log_page_ctx_t page = {0};

    /* Step 1: skip the newest skip_newest entries (single pass, no copy). */
    if (skip_newest > 0U)
    {
        if (log_read_last(&iec104_elog_ctx, skip_newest, iec104_elog_discard_visitor,
                          NULL, &page) != LOG_OK)
        {
            return 1;
        }
        if (page.page_count < skip_newest)
        {
            /* Fewer entries than requested to skip: window is empty. */
            *out_count = 0U;
            return 0;
        }
    }

    /* Step 2: collect the next count entries, newest -> oldest. */
    iec104_elog_collect_ctx_t cc = {0};
    cc.out = entries;
    cc.capacity = count;

    if (count > 0U)
    {
        if (log_read_last(&iec104_elog_ctx, count, iec104_elog_collect_visitor,
                          &cc, &page) != LOG_OK)
        {
            return 1;
        }
    }

    *out_count = cc.copied;
    return 0;
}

void iec104_elog_clear(void)
{
    if (!log_is_initialized(&iec104_elog_ctx))
    {
        return;
    }

    log_config_t cfg;

    iec104_elog_config_build(&cfg);

    for (uint32_t s = 0U; s < iec104_elog_ctx.sector_count; s++)
    {
        (void)iec104_elog_flash_erase_sector(
                iec104_elog_ctx.base_addr + (s * LOG_SECTOR_SIZE));
    }

    (void)log_init(&iec104_elog_ctx, &cfg);
    CSLOG("IEC104 log cleared. All entries removed.\r\n");
}

uint16_t iec104_elog_get_entry_count(void)
{
    if (!log_is_initialized(&iec104_elog_ctx))
    {
        return 0U;
    }
    return (uint16_t)log_get_next_seq(&iec104_elog_ctx);
}

uint16_t iec104_elog_get_max_entries(void)
{
    if (!log_is_initialized(&iec104_elog_ctx))
    {
        return 0U;
    }
    /* One sector is always kept erased for the circular invariant. */
    return (uint16_t)(iec104_elog_ctx.entries_per_sector *
                      (iec104_elog_ctx.sector_count - 1U));
}

/* ======================================================================
 *  Decode for display
 * ====================================================================== */

static uint32_t iec104_elog_rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

const char *iec104_elog_info_to_text(const iec104_elog_entry_t *entry)
{
    static char text[64];

    if (NULL == entry)
    {
        return "-";
    }

    const uint8_t *info = entry->info;

    switch ((iec104_elog_code_t)entry->code)
    {
        case IEC104_ELOG_CONN:
        {
            uint32_t ip = iec104_elog_rd_be32(&info[2]);
            xsnprintf(text, sizeof(text), "connected %u.%u.%u.%u",
                      (unsigned)((ip >> 24) & 0xFFU),
                      (unsigned)((ip >> 16) & 0xFFU),
                      (unsigned)((ip >> 8) & 0xFFU),
                      (unsigned)(ip & 0xFFU));
            break;
        }
        case IEC104_ELOG_DISC:
            switch ((iec104_elog_disc_reason_t)info[1])
            {
                case IEC104_ELOG_DISC_CLOSED_BY_REMOTE:
                    xsnprintf(text, sizeof(text), "disconnected (closed by remote)");
                    break;
                case IEC104_ELOG_DISC_NO_CARRIER:
                    xsnprintf(text, sizeof(text), "disconnected (no carrier)");
                    break;
                case IEC104_ELOG_DISC_CONN_TIMEOUT:
                    xsnprintf(text, sizeof(text), "disconnected (connection timeout)");
                    break;
                case IEC104_ELOG_DISC_LOCAL_CLOSE:
                    xsnprintf(text, sizeof(text), "disconnected (local close)");
                    break;
                case IEC104_ELOG_DISC_SOCKET_CLOSED:
                    xsnprintf(text, sizeof(text), "disconnected (socket closed)");
                    break;
                case IEC104_ELOG_DISC_UNKNOWN:
                default:
                    xsnprintf(text, sizeof(text), "disconnected (unknown)");
                    break;
            }
            break;
        default:
            xsnprintf(text, sizeof(text), "?");
            break;
    }
    return text;
}

const char *iec104_elog_ts_to_text(uint32_t unix_ts)
{
    static char buf[20];
    datetime_t dt;

    dt_conv_from_epoch((time32_t)unix_ts, &dt);
    xsnprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
              (unsigned)dt.date.year, (unsigned)dt.date.month,
              (unsigned)dt.date.day, (unsigned)dt.time.hour,
              (unsigned)dt.time.minute, (unsigned)dt.time.second);
    return buf;
}

/* ======================================================================
 *  Shell command: iec104elog dump [N|raw] | clear | info
 * ====================================================================== */

static void iec104_elog_print_entry(uint32_t seq, const iec104_elog_entry_t *e)
{
    const char *lvl = (e->level == (uint8_t)IEC104_ELOG_LEVEL_WARN) ? "WARN" : "INFO";

    SHELL_LOG("#%-6lu %s TS:%s %s\r\n",
              (unsigned long)seq, lvl,
              iec104_elog_ts_to_text(e->timestamp),
              iec104_elog_info_to_text(e));
}

static void iec104_elog_dump_visitor(const void *payload, uint32_t payload_size,
                                     uint32_t seq, void *user_ctx)
{
    (void)payload_size;
    (void)user_ctx;
    iec104_elog_print_entry(seq, (const iec104_elog_entry_t *)payload);
}

static void iec104_elog_shell_dump(int argc, char **argv)
{
    bool raw = false;
    uint32_t count = 0U;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "raw") == 0)
        {
            raw = true;
        }
        else
        {
            int parsed = xstrtoi(argv[i]);
            if (parsed > 0)
            {
                count = (uint32_t)parsed;
            }
        }
    }

    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("        IEC104 ELOG DUMP\r\n");
    SHELL_LOG("========================================\r\n\r\n");

    if (raw)
    {
        iec104_elog_entry_t chunk[8];
        uint32_t got = 0U;
        uint32_t skip = 0U;

        while (skip < count || count == 0U)
        {
            if ((iec104_elog_read_recent(skip, 8U, chunk, &got) != 0) || (got == 0U))
            {
                break;
            }
            for (uint32_t i = 0U; i < got; i++)
            {
                SHELL_LOG("LVL:%u CODE:%u INFO:%02X%02X%02X%02X%02X%02X\r\n",
                          (unsigned)chunk[i].level, (unsigned)chunk[i].code,
                          chunk[i].info[0], chunk[i].info[1], chunk[i].info[2],
                          chunk[i].info[3], chunk[i].info[4], chunk[i].info[5]);
            }
            skip += got;
            if (got < 8U)
            {
                break;
            }
        }
    }
    else if (count > 0U)
    {
        iec104_elog_entry_t chunk[8];
        uint32_t got = 0U;
        uint32_t skip = 0U;

        while (skip < count)
        {
            uint32_t want = ((count - skip) > 8U) ? 8U : (count - skip);

            if ((iec104_elog_read_recent(skip, want, chunk, &got) != 0) || (got == 0U))
            {
                break;
            }
            for (uint32_t i = 0U; i < got; i++)
            {
                /* display index is 1-based from the newest */
                iec104_elog_print_entry(skip + i + 1U, &chunk[i]);
            }
            skip += got;
            if (got < want)
            {
                break;
            }
        }
    }
    else
    {
        if (log_read_all(&iec104_elog_ctx, iec104_elog_dump_visitor, NULL) != LOG_OK)
        {
            SHELL_LOG("Dump failed: flash read error\r\n");
        }
    }

    SHELL_LOG("\r\n");
}

static int iec104_elog_shell_handler(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "clear") == 0)
    {
        iec104_elog_clear();
    }
    else if (argc > 1 && strcmp(argv[1], "info") == 0)
    {
        SHELL_LOG("\r\n");
        SHELL_LOG("========================================\r\n");
        SHELL_LOG("        IEC104 ELOG STATUS\r\n");
        SHELL_LOG("========================================\r\n");
        SHELL_LOG("Status           : INITIALIZED (append-only ring)\r\n");
        SHELL_LOG("Stored Entries   : %u / %u\r\n",
                  iec104_elog_get_entry_count(), iec104_elog_get_max_entries());
        SHELL_LOG("========================================\r\n\r\n");
    }
    else if (argc > 1 && strcmp(argv[1], "dump") == 0)
    {
        iec104_elog_shell_dump(argc, argv);
    }
    else
    {
        SHELL_LOG("Usage: iec104elog <command>\r\n"
                  "  dump [N|raw] - dump newest N entries (all if absent)\r\n"
                  "  clear        - remove all entries\r\n"
                  "  info         - ring status\r\n");
        return -1;
    }

    return 0;
}

/* ======================================================================
 *  Init
 * ====================================================================== */

void iec104_elog_init(void)
{
    log_config_t cfg;

    iec104_elog_config_build(&cfg);

    log_status_t st = log_init(&iec104_elog_ctx, &cfg);
    if (st != LOG_OK)
    {
        CSLOG_ERR("[IEC104ELOG] init failed (%d)\r\n", (int)st);
        return;
    }

    CSLOG("IEC104 elog system initialized.\r\n");
    CSLOG("Capacity: %u entries, total written: %u\r\n",
          iec104_elog_get_max_entries(), iec104_elog_get_entry_count());
}

void iec104_elog_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "iec104elog",
        .desc = "IEC104 diagnostic log\r\n"
                "iec104elog dump [N|raw] - dump entries\r\n"
                "iec104elog clear       - remove all entries\r\n"
                "iec104elog info        - ring status",
        .func = iec104_elog_shell_handler});
}

/*** end of file ***/
