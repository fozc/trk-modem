/*
 * elog.c
 *
 *  Created on: 14 Eyl 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Storage engine: spi_flash_log append-only ring (Application/libs/).
 * The 24-byte elog_entry_t is the log payload; the library wraps it with a
 * 16-bit wrapping sequence number and a 16-bit CRC on flash (28-byte entries;
 * entries may straddle flash pages, the library splits the programs). One
 * record costs at least two page programs (payload phase, then CRC), three
 * when the payload phase straddles a page boundary; a sector is erased only
 * when a new record needs its space (lazy erase: an idle full ring keeps its
 * oldest sector). On flash errors the library keeps the
 * write head inside the ring area, poisons a slot the driver never wrote
 * (so the boot scan cannot hide later records) and retries a failed sector
 * transition on the next write.
 */

#include "elog.h"
#include "version.h"
#include "boot.h"
#include <string.h>
#include "bsp.h"
#include "shell.h"
#include "xprintf.h"
#include "rtc.h"
#include "datetime.h"
#include "utils.h"
#include "w25qxx.h"
#include "spi_flash_organization.h"
#include "spi_flash_log.h"

/* Not: log kutuphanesinin sayfa sabiti LOG_FLASH_PAGE_SIZE'dir; HAL'in
 * FLASH_PAGE_SIZE'i (ic flash, 4 KB) ile ad cakismasi kaldirildi. */

/* ---- compile-time geometry checks ------------------------------------ */
_Static_assert(sizeof(elog_entry_t) == 24U, "elog payload must stay 24 bytes");
/* Wrap-around seq comparison stays valid only while the whole ring capacity
 * remains under LOG_SEQ_HALFSPACE; guard it at compile time (28 B entries,
 * ELOG_LOG_SECTOR_COUNT sectors). */
_Static_assert((ELOG_LOG_SECTOR_COUNT * (LOG_SECTOR_SIZE / (LOG_ENTRY_OVERHEAD + 24U)))
               < LOG_SEQ_HALFSPACE,
               "elog ring capacity must stay below LOG_SEQ_HALFSPACE");

static log_ctx_t elog_ctx;

/* ---- w25qxx adapter layer (signatures differ from log_flash_ops_t) --- */

/* w25qxx_read_buff is void-typed: the driver layer cannot distinguish a
 * failed SPI read, so this adapter has no error to propagate and always
 * reports success. TODO(w25qxx): add an int-returning read variant once
 * the SPI/HAL layer can detect transfer errors; until then a read error
 * surfaces only as a CRC/torn-slot rejection inside the log library. */
static int elog_flash_read(uint32_t addr, void *buf, size_t len)
{
    w25qxx_read_buff(addr, buf, (uint32_t)len);
    return 0;
}

static int elog_flash_program(uint32_t addr, const void *buf, size_t len)
{
    return w25qxx_page_write(addr, buf, (uint32_t)len);
}

static int elog_flash_erase_sector(uint32_t sector_addr)
{
    return w25qxx_erase_sector(sector_addr);
}

static void elog_config_build(log_config_t *cfg)
{
    cfg->base_addr = ELOG_LOGAREA_ADDRESS;
    cfg->sector_count = ELOG_LOG_SECTOR_COUNT;
    cfg->payload_size = (uint32_t)sizeof(elog_entry_t);
    cfg->ops.read = elog_flash_read;
    cfg->ops.program = elog_flash_program;
    cfg->ops.erase_sector = elog_flash_erase_sector;
}

static uint32_t elog_timestamp_now(void)
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
    return dt_conv_to_epoch(&dt2);
}

static void elog_write_entry(const elog_entry_t *entry)
{
    if (!log_is_initialized(&elog_ctx))
    {
        return;
    }

    log_status_t st = log_write(&elog_ctx, entry);
    if (st != LOG_OK)
    {
        CSLOG_ERR("[ELOG] write failed (%d)\r\n", (int)st);
    }
}

void elog_init(void)
{
    log_config_t cfg;

    elog_config_build(&cfg);

    log_status_t st = log_init(&elog_ctx, &cfg);
    if (st != LOG_OK)
    {
        CSLOG_ERR("[ELOG] init failed (%d)\r\n", (int)st);
        return;
    }

    CSLOG("Error log system initialized.\r\n");
    CSLOG("Capacity: %u entries, next seq: %u\r\n",
          elog_get_capacity(), elog_get_next_seq());
}

void elog_add(elog_code_t _code, elog_level_t level, const void *data, size_t data_len)
{
    elog_entry_t entry = {0};

    entry.timestamp = elog_timestamp_now();
    entry.entry_id = (uint16_t)(log_get_next_seq(&elog_ctx) & 0xFFFFU);
    entry.level = (uint8_t)level;
    entry.code = (uint8_t)_code;
    if ((data != NULL) && (data_len > 0U))
    {
        size_t len = (data_len < sizeof(entry.info)) ? data_len : sizeof(entry.info);
        memcpy(entry.info, data, len);
    }

    elog_write_entry(&entry);
}

void elog_add_entry(const elog_entry_t *entry)
{
    if (NULL == entry)
    {
        return;
    }

    /* entry_id is auto-assigned from the ring sequence number. */
    elog_entry_t copy = *entry;
    copy.entry_id = (uint16_t)(log_get_next_seq(&elog_ctx) & 0xFFFFU);
    elog_write_entry(&copy);
}

/* ---- reading ---------------------------------------------------------- */

typedef struct
{
    elog_entry_t *out;
    uint32_t capacity;
    uint32_t copied;
} elog_collect_ctx_t;

/* log_read_last delivers entries newest -> oldest. */
static void elog_collect_visitor(const void *payload, uint32_t payload_size,
                                 uint32_t seq, void *user_ctx)
{
    elog_collect_ctx_t *cc = (elog_collect_ctx_t *)user_ctx;

    (void)payload_size;
    (void)seq;

    if (cc->copied < cc->capacity)
    {
        cc->out[cc->copied] = *(const elog_entry_t *)payload;
        cc->copied++;
    }
}

/* Sink for entries the caller wants to page over without copying. */
static void elog_discard_visitor(const void *payload, uint32_t payload_size,
                                 uint32_t seq, void *user_ctx)
{
    (void)payload;
    (void)payload_size;
    (void)seq;
    (void)user_ctx;
}

static void elog_count_visitor(const void *payload, uint32_t payload_size,
                               uint32_t seq, void *user_ctx)
{
    uint32_t *count = (uint32_t *)user_ctx;

    (void)payload;
    (void)payload_size;
    (void)seq;

    (*count)++;
}

int elog_read_recent(uint32_t skip_newest, uint32_t count,
                     elog_entry_t *entries, uint32_t *out_count)
{
    if (!log_is_initialized(&elog_ctx) || NULL == entries || NULL == out_count)
    {
        return 1;
    }

    log_page_ctx_t page = {0};

    /* Step 1: skip the newest skip_newest entries (single pass, no copy). */
    if (skip_newest > 0U)
    {
        if (log_read_last(&elog_ctx, skip_newest, elog_discard_visitor, NULL, &page) != LOG_OK)
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
    elog_collect_ctx_t cc = {0};
    cc.out = entries;
    cc.capacity = count;

    if (count > 0U)
    {
        if (log_read_last(&elog_ctx, count, elog_collect_visitor, &cc, &page) != LOG_OK)
        {
            return 1;
        }
    }

    *out_count = cc.copied;
    return 0;
}

void elog_print(elog_level_t level, const char *message)
{
    const char *level_str = "";
    switch(level) {
        case ELOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case ELOG_LEVEL_INFO:  level_str = "INFO";  break;
        case ELOG_LEVEL_WARN:  level_str = "WARN";  break;
        case ELOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case ELOG_LEVEL_FATAL: level_str = "FATAL"; break;
        default: level_str = "UNKNOWN"; break;
    }
    CSLOG("[%s] %s\r\n", level_str, message);
}

bool elog_clear(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        SHELL_LOG("Error: elog not initialized\r\n");
        return false;
    }

    log_config_t cfg;

    elog_config_build(&cfg);

    uint32_t erase_failures = 0U;

    for (uint32_t s = 0U; s < elog_ctx.sector_count; s++)
    {
        if (elog_flash_erase_sector(elog_ctx.base_addr + (s * LOG_SECTOR_SIZE)) != 0)
        {
            erase_failures++;
            CSLOG_ERR("[ELOG] sector %u erase failed\r\n", (unsigned int)s);
        }
    }

    /* Re-scan regardless: log_init recovers the true head from whatever
     * the flash holds after the (possibly partial) erase pass. */
    log_status_t st = log_init(&elog_ctx, &cfg);

    if (erase_failures > 0U)
    {
        SHELL_LOG("Clear FAILED: %u sector(s) not erased, re-scan %s\r\n",
                  (unsigned int)erase_failures,
                  (st == LOG_OK) ? "recovered the ring" : "failed");
        return false;
    }

    if (st != LOG_OK)
    {
        SHELL_LOG("Clear FAILED: re-scan error %d, log left uninitialized\r\n",
                  (int)st);
        return false;
    }

    SHELL_LOG("Error log cleared. All entries removed.\r\n");
    return true;
}

uint16_t elog_get_next_seq(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        return 0U;
    }
    return (uint16_t)log_get_next_seq(&elog_ctx);
}

uint32_t elog_get_stored_count(void)
{
    uint32_t count = 0U;

    if (!log_is_initialized(&elog_ctx))
    {
        return 0U;
    }
    if (log_read_all(&elog_ctx, elog_count_visitor, &count) != LOG_OK)
    {
        return 0U;
    }
    return count;
}

uint16_t elog_get_capacity(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        return 0U;
    }
    /* Erase is deferred to the moment a new record needs the space, so a
     * full ring holds every sector's worth of records. */
    return (uint16_t)(elog_ctx.entries_per_sector * elog_ctx.sector_count);
}

/* ---- shell commands ---------------------------------------------------- */

/* ---- info payload decoding for display ------------------------------- */

static uint32_t elog_rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Copy printable bytes until NUL/16; "-" when nothing printable. */
static void elog_printable_or_dash(const uint8_t *info, char *dst, size_t dst_size)
{
    size_t pos = 0U;
    bool   any = false;

    for (size_t i = 0; (i < 16U) && (info[i] != 0U) && (pos + 1U < dst_size); i++)
    {
        if ((info[i] >= 32U) && (info[i] < 127U))
        {
            dst[pos++] = (char)info[i];
            any = true;
        }
    }
    dst[pos] = '\0';

    if (!any)
    {
        xsnprintf(dst, dst_size, "-");
    }
}

static const char *pb_batt_state_str(uint8_t state)
{
    switch (state)
    {
        case 0U: return "PRESENT";
        case 2U: return "ABSENT";
        case 3U: return "PROBING";
        default: return "?";
    }
}

static const char *bms_work_state_str(uint8_t state)
{
    switch (state)
    {
        case 0U: return "STATIONARY";
        case 1U: return "CHARGING";
        case 2U: return "DISCHARGING";
        default: return "?";
    }
}

const char *elog_info_to_text(const elog_entry_t *entry)
{
    static char text[112];

    if (NULL == entry)
    {
        return "-";
    }

    const uint8_t *info = entry->info;

    switch ((elog_code_t)entry->code)
    {
        case ELOG_SYSTEM_RESET_CAUSE:
        {
            uint32_t flags = elog_rd_be32(&info[0]);
            char causes[48];
            int  pos = 0;

            causes[0] = '\0';
            for (uint32_t bit = 1U; (bit != 0U) && (pos < (int)sizeof(causes) - 6); bit <<= 1)
            {
                if ((flags & bit) != 0U)
                {
                    const char *name = "?";
                    switch (bit)
                    {
                        case 0x01U: name = "POR";  break;
                        case 0x02U: name = "BOR";  break;
                        case 0x04U: name = "PIN";  break;
                        case 0x08U: name = "SFT";  break;
                        case 0x10U: name = "IWDG"; break;
                        case 0x20U: name = "WWDG"; break;
                        case 0x40U: name = "LPWR"; break;
                        case 0x80U: name = "OBL";  break;
                        default:    name = "?";    break;
                    }
                    pos += xsnprintf(&causes[pos], (size_t)(sizeof(causes) - pos),
                                     "%s%s", (pos > 0) ? "|" : "", name);
                }
            }
            if (pos == 0)
            {
                xsnprintf(causes, sizeof(causes), "UNKNOWN");
            }
            xsnprintf(text, sizeof(text), "%s raw=0x%08lX %s",
                      causes, (unsigned long)elog_rd_be32(&info[4]),
                      (info[8] != 0U) ? "ABNORMAL" : "normal");
            break;
        }

        case ELOG_SYSTEM_NVRAM_RECOVERED:
            xsnprintf(text, sizeof(text), "%s stored=0x%08lX calc=0x%08lX",
                      (info[0] == ELOG_NVRAM_RESTORED_FROM_BACKUP) ? "backup" : "defaults",
                      (unsigned long)elog_rd_be32(&info[1]),
                      (unsigned long)elog_rd_be32(&info[5]));
            break;

        case ELOG_SYSTEM_FW_UPDATE:
        {
            const char *src = "?";
            switch (info[0])
            {
                case ELOG_FW_SRC_XMODEM:   src = "uart-xmodem"; break;
                case ELOG_FW_SRC_RFWU:     src = "web";         break;
                case ELOG_FW_SRC_BOOT_CMD: src = "console";      break;
                default: break;
            }
            const char *res = "?";
            switch (info[1])
            {
                case ELOG_FW_RESULT_START:     res = "start";     break;
                case ELOG_FW_RESULT_OK:        res = "ok";        break;
                case ELOG_FW_RESULT_FAIL:      res = "fail";      break;
                case ELOG_FW_RESULT_AUTH_FAIL: res = "auth-fail"; break;
                default: break;
            }
            uint32_t size = elog_rd_be32(&info[2]);
            if (size != 0U)
            {
                xsnprintf(text, sizeof(text), "%s %s %luB v%u.%u.%u.%u",
                          src, res, (unsigned long)size,
                          info[6], info[7], info[8], info[9]);
            }
            else
            {
                xsnprintf(text, sizeof(text), "%s %s v%u.%u.%u.%u",
                          src, res,
                          info[6], info[7], info[8], info[9]);
            }
            break;
        }

        case ELOG_SYSTEM_FW_APPROVED:
            xsnprintf(text, sizeof(text), "approved v%u.%u.%u.%u %s",
                      info[0], info[1], info[2], info[3],
                      (info[4] != 0U) ? (const char *)&info[4] : "-");
            break;

        case ELOG_GSM_COLD_BOOT:
            xsnprintf(text, sizeof(text), "cold boot attempt %u", info[0]);
            break;

        case ELOG_GSM_WTD_LIVENESS:
            xsnprintf(text, sizeof(text), "silent %us, recovery %u",
                      (unsigned)((info[0] << 8) | info[1]), info[2]);
            break;

        case ELOG_SYSTEM_HARDFAULT:
            xsnprintf(text, sizeof(text), "pc=0x%08lX lr=0x%08lX cfsr=0x%08lX hfsr=0x%08lX",
                      (unsigned long)elog_rd_be32(&info[0]),
                      (unsigned long)elog_rd_be32(&info[4]),
                      (unsigned long)elog_rd_be32(&info[8]),
                      (unsigned long)elog_rd_be32(&info[12]));
            break;

        case ELOG_PWR_ALARM:
            xsnprintf(text, sizeof(text), "%s latch=0x%02X live=0x%02X sys=0x%02X bq=0x%02X/0x%02X",
                      (info[5] != 0U) ? "set" : "clear",
                      info[0], info[1], info[2], info[3], info[4]);
            break;

        case ELOG_BAT_STATE:
            if (info[1] == 0U)  /* state change */
            {
                if (info[0] == ELOG_BAT_SRC_POWER_BOARD)
                {
                    xsnprintf(text, sizeof(text), "pb batt %s->%s soc=%u%% soh=%u%%",
                              pb_batt_state_str(info[2]), pb_batt_state_str(info[3]),
                              info[4], info[5]);
                }
                else
                {
                    xsnprintf(text, sizeof(text), "bms work %s->%s soc=%u%% soh=%u%%",
                              bms_work_state_str(info[2]), bms_work_state_str(info[3]),
                              info[4], info[5]);
                }
            }
            else  /* SOC threshold */
            {
                xsnprintf(text, sizeof(text), "soc<=%u%% %s soc=%u%% soh=%u%%",
                          info[2], (info[3] != 0U) ? "SET" : "cleared",
                          info[4], info[5]);
            }
            break;

        case ELOG_WEB_LOGIN_FAIL:
            xsnprintf(text, sizeof(text), "fail x%u from %u.%u.%u.%u",
                      (unsigned)((info[4] << 8) | info[5]),
                      info[0], info[1], info[2], info[3]);
            break;

        case ELOG_CONFIG_DEVICE_CHANGED:
        case ELOG_CONFIG_IEC104_CHANGED:
        case ELOG_CONFIG_MODBUS_CHANGED:
        case ELOG_CONFIG_RF_CHANGED:
        {
            /* ip(4) source(1) area[10] flags(1); area is copied out so a
             * full-width label cannot run past the flag byte. */
            char area[11];
            memcpy(area, &info[5], 10U);
            area[10] = '\0';

            xsnprintf(text, sizeof(text), "%s via %s %u.%u.%u.%u %s",
                      area,
                      (info[4] == (uint8_t)ELOG_SOURCE_WEB) ? "web" : "serial",
                      info[0], info[1], info[2], info[3],
                      (info[15] == 0xFFU) ? "?" :
                      (info[15] != 0U) ? "ok" : "FAILED");
            break;
        }
        default:
            /* GSM events carry a phone number or diagnostics - printable
             * covers both. */
            elog_printable_or_dash(info, text, sizeof(text));
            break;
    }

    return text;
}

static const char* elog_level_to_string(elog_level_t level)
{
    switch(level) {
        case ELOG_LEVEL_DEBUG: return "DEBUG";
        case ELOG_LEVEL_INFO:  return "INFO ";
        case ELOG_LEVEL_WARN:  return "WARN ";
        case ELOG_LEVEL_ERROR: return "ERROR";
        case ELOG_LEVEL_FATAL: return "FATAL";
        default: return "?????";
    }
}

static void elog_shell_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!log_is_initialized(&elog_ctx)) {
        SHELL_LOG("Error Log: NOT INITIALIZED\r\n");
        return;
    }

    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("       ERROR LOG SYSTEM STATUS\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Status           : INITIALIZED (append-only ring)\r\n");
    SHELL_LOG("Next Seq         : %u (wraps at 65535)\r\n",
              elog_get_next_seq());
    SHELL_LOG("Stored Entries   : %u / %u\r\n",
              elog_get_stored_count(), elog_get_capacity());

    SHELL_LOG("\r\nMemory Addresses:\r\n");
    SHELL_LOG("  Log Area       : 0x%08X (%u sectors)\r\n",
              elog_ctx.base_addr, elog_ctx.sector_count);
    SHELL_LOG("  Entry Size     : %u bytes (payload %u)\r\n",
              elog_ctx.entry_size, elog_ctx.payload_size);

    SHELL_LOG("========================================\r\n");
    SHELL_LOG("\r\nCommands:\r\n");
    SHELL_LOG("  elog             - Show this information\r\n");
    SHELL_LOG("  elog dump [N]    - Dump entries (all or newest N, decoded)\r\n");
    SHELL_LOG("  elog dump raw [N]- Dump entries (raw info hex bytes)\r\n");
    SHELL_LOG("  elog clear       - Clear all log entries\r\n");
    SHELL_LOG("========================================\r\n\r\n");
}

static bool elog_dump_raw = false;

/* Print one entry in the active dump mode (decoded text or raw hex). */
static void elog_print_entry(uint32_t seq, const elog_entry_t *entry)
{
    datetime_t dt;
    dt_conv_from_epoch(entry->timestamp, &dt);

    char info_str[64];
    const char *info_out;

    if (elog_dump_raw)
    {
        int pos = 0;
        for (uint8_t j = 0; j < sizeof(entry->info) && pos < (int)sizeof(info_str) - 3; j++) {
            pos += xsnprintf(info_str + pos, sizeof(info_str) - pos, "%02X ", entry->info[j]);
        }
        if (pos > 0) info_str[pos - 1] = '\0'; /* Remove trailing space */
        info_out = info_str;
    }
    else
    {
        info_out = elog_info_to_text(entry);
    }

    SHELL_LOG("%-6u %-5s %04u-%02u-%02u %02u:%02u:%02u %-18s %s\r\n",
            seq,
            elog_level_to_string((elog_level_t)entry->level),
            dt.date.year, dt.date.month, dt.date.day,
            dt.time.hour, dt.time.minute, dt.time.second,
            elog_code_to_string((elog_code_t)entry->code),
            info_out);
}

static void elog_dump_visitor(const void *payload, uint32_t payload_size,
                              uint32_t seq, void *user_ctx)
{
    (void)payload_size;
    (void)user_ctx;

    elog_print_entry(seq, (const elog_entry_t *)payload);
}

static void elog_shell_dump(int argc, char **argv)
{
    if (!log_is_initialized(&elog_ctx)) {
        SHELL_LOG("Error: elog not initialized\r\n");
        return;
    }

    /* Args in any order: "raw" selects the hex view, a number selects how
     * many of the newest entries to show (0/absent = all). */
    uint32_t count = 0U;

    elog_dump_raw = false;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "raw") == 0)
        {
            elog_dump_raw = true;
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

    bool dump_all = (count == 0U);

    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("        ERROR LOG DUMP\r\n");
    SHELL_LOG("========================================\r\n\r\n");

    SHELL_LOG("%-6s %-5s %-19s %-18s %s\r\n",
              "Seq", "Level", "Timestamp", "Code", "Info");
    SHELL_LOG("------ ----- ------------------- ------------------ ----------------------------------------\r\n");

    if (dump_all)
    {
        if (log_read_all(&elog_ctx, elog_dump_visitor, NULL) != LOG_OK) {
            SHELL_LOG("Dump failed: flash read error\r\n");
        }
    }
    else
    {
        /* Newest count entries, chronological: the library positions the
         * cursor backwards once and visits forward (<= 2 x count slot
         * reads; no re-scanning from the head per chunk). Overshoot is
         * clamped to the stored count inside the library. */
        if (log_read_tail(&elog_ctx, count, elog_dump_visitor, NULL) != LOG_OK) {
            SHELL_LOG("Dump failed: flash read error\r\n");
        }
    }

    SHELL_LOG("\r\n========================================\r\n\r\n");
}

static void elog_shell_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!log_is_initialized(&elog_ctx)) {
        SHELL_LOG("Error: elog not initialized\r\n");
        return;
    }

    uint16_t count = elog_get_next_seq();

    if (elog_clear())
    {
        SHELL_LOG("\r\nCleared log (seq was at %u).\r\n", count);
    }
}

static int elog_shell_command(int argc, char **argv)
{
    if (argc < 2) {
        /* No arguments - show info */
        elog_shell_info(argc, argv);
        return 1;
    }

    /* Parse subcommand */
    if (strcmp(argv[1], "dump") == 0) {
        elog_shell_dump(argc, argv);
    }
    else if (strcmp(argv[1], "clear") == 0) {
        elog_shell_clear(argc, argv);
    }
    else if (strcmp(argv[1], "info") == 0) {
        elog_shell_info(argc, argv);
    }
    else {
        SHELL_LOG("Unknown elog command: %s\r\n", argv[1]);
        SHELL_LOG("Usage: elog [dump [raw]|clear|info]\r\n");
    }

    return 1;
}

void elog_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "elog",
        .desc = "Error log management\r\n"
                "\telog info          - ring status and counters\r\n"
                "\telog dump [n|raw]  - last n entries (0=all), raw: hex\r\n"
                "\telog clear         - remove all entries",
        .level = 0,
        .func = elog_shell_command
    });
}

void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area,
                            bool success)
{
    uint8_t info[16] = {0};

    info[0] = (uint8_t)((ip >> 24U) & 0xFFU);
    info[1] = (uint8_t)((ip >> 16U) & 0xFFU);
    info[2] = (uint8_t)((ip >>  8U) & 0xFFU);
    info[3] = (uint8_t)( ip         & 0xFFU);

    info[4] = (uint8_t)source;
    info[15] = success ? 1U : 0U;   /* 1 = committed, 0 = save failed */

    if (p_area != NULL)
    {
        size_t len = strlen(p_area);
        if (len > 10U)              /* [5..14]; [15] holds the flag */
        {
            len = 10U;
        }
        memcpy(&info[5], p_area, len);
    }

    elog_add(code, ELOG_LEVEL_INFO, info, sizeof(info));

    const char *src_str = (source == ELOG_SOURCE_WEB) ? "web" : "serial";
    CSLOG_WARN("[ELOG] Config %s: %s src:%s ip:%u.%u.%u.%u\r\n",
            success ? "changed" : "change FAILED",
            p_area ? p_area : "?", src_str,
            (unsigned int)info[0], (unsigned int)info[1],
            (unsigned int)info[2], (unsigned int)info[3]);
}

/* ====================================================================== */
/* Semantic logging API                                                   */
/*                                                                        */
/* Payload layouts, level policy and rate limiting for every event code  */
/* are defined HERE and nowhere else. Modules report plain event         */
/* parameters through the elog_log_* functions.                          */
/* ====================================================================== */

static void elog_pack_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)(value);
}

void elog_log_reset_cause(uint32_t flags, uint32_t raw_csr, bool abnormal)
{
    /* info: flags(4) raw_csr(4) abnormal(1) */
    uint8_t info[16] = {0};

    elog_pack_u32_be(&info[0], flags);
    elog_pack_u32_be(&info[4], raw_csr);
    info[8] = abnormal ? 1U : 0U;
    elog_add(ELOG_SYSTEM_RESET_CAUSE,
             abnormal ? ELOG_LEVEL_WARN : ELOG_LEVEL_INFO,
             info, sizeof(info));
}

void elog_log_hardfault(uint32_t pc, uint32_t lr, uint32_t cfsr, uint32_t hfsr)
{
    /* info: pc(4) lr(4) cfsr(4) hfsr(4) */
    uint8_t info[16] = {0};

    elog_pack_u32_be(&info[0], pc);
    elog_pack_u32_be(&info[4], lr);
    elog_pack_u32_be(&info[8], cfsr);
    elog_pack_u32_be(&info[12], hfsr);
    elog_add(ELOG_SYSTEM_HARDFAULT, ELOG_LEVEL_FATAL, info, sizeof(info));
}

void elog_log_nvram_recovery(uint8_t action, uint32_t stored_crc, uint32_t calc_crc)
{
    /* info: action(1) stored_crc(4) calc_crc(4) */
    uint8_t info[16] = {0};

    info[0] = action;
    elog_pack_u32_be(&info[1], stored_crc);
    elog_pack_u32_be(&info[5], calc_crc);
    elog_add(ELOG_SYSTEM_NVRAM_RECOVERED, ELOG_LEVEL_ERROR, info, sizeof(info));
}

void elog_log_fw_update(uint8_t source, uint8_t result, uint32_t size)
{
    /* info: source(1) result(1) size(4) */
    uint8_t info[16] = {0};

    info[0] = source;
    info[1] = result;
    elog_pack_u32_be(&info[2], size);
    info[6] = VERSION_MAJOR;
    info[7] = VERSION_MINOR;
    info[8] = VERSION_PATCH;
    info[9] = VERSION_EXTRA;
    elog_add(ELOG_SYSTEM_FW_UPDATE,
             (result == ELOG_FW_RESULT_FAIL) ? ELOG_LEVEL_ERROR
             : (result == ELOG_FW_RESULT_AUTH_FAIL) ? ELOG_LEVEL_WARN
             : ELOG_LEVEL_INFO,
             info, sizeof(info));
}

void elog_log_fw_approved(void)
{
    uint8_t info[16] = {0};

    info[0] = VERSION_MAJOR;
    info[1] = VERSION_MINOR;
    info[2] = VERSION_PATCH;
    info[3] = VERSION_EXTRA;

    /* Imaj kimligi boot superblock'tan: hangi commit onaylandi.
     * short_commit_hash NUL sonlu 7 karakter (boot.h garantisi). */
    const fw_info_t *fw_info = boot_get_installed_fw_info();
    if (fw_info != NULL)
    {
        (void)memcpy(&info[4], fw_info->short_commit_hash, 8U);
    }

    elog_add(ELOG_SYSTEM_FW_APPROVED, ELOG_LEVEL_INFO, info, sizeof(info));
}

void elog_log_power_alarm(const elog_power_alarm_t *alarm)
{
    /* info: latch(1) live(1) sys_fault(1) bq0(1) bq1(1) rising(1) */
    uint8_t info[16] = {0};

    if (NULL == alarm)
    {
        return;
    }

    info[0] = alarm->latch;
    info[1] = alarm->live;
    info[2] = alarm->sys_fault;
    info[3] = alarm->bq_fault0;
    info[4] = alarm->bq_fault1;
    info[5] = alarm->rising ? 1U : 0U;
    elog_add(ELOG_PWR_ALARM,
             alarm->rising ? ELOG_LEVEL_ERROR : ELOG_LEVEL_INFO,
             info, sizeof(info));
}

void elog_log_battery_state_change(uint8_t source, uint8_t old_state,
                                   uint8_t new_state, uint8_t soc, uint8_t soh)
{
    /* info: src(1) event=0(1) old(1) new(1) soc(1) soh(1).
     * Power board: state 0 = battery present, anything else is a fault. */
    uint8_t info[16] = {0};

    info[0] = source;
    info[1] = 0U;
    info[2] = old_state;
    info[3] = new_state;
    info[4] = soc;
    info[5] = soh;
    elog_add(ELOG_BAT_STATE,
             ((source == ELOG_BAT_SRC_POWER_BOARD) && (new_state != 0U))
                 ? ELOG_LEVEL_WARN
                 : ELOG_LEVEL_INFO,
             info, sizeof(info));
}

void elog_log_battery_soc_threshold(uint8_t threshold, bool set,
                                    uint8_t soc, uint8_t soh)
{
    /* info: src=BMS(1) event=1(1) threshold(1) set(1) soc(1) soh(1) */
    uint8_t info[16] = {0};

    info[0] = ELOG_BAT_SRC_BMS;
    info[1] = 1U;
    info[2] = threshold;
    info[3] = set ? 1U : 0U;
    info[4] = soc;
    info[5] = soh;
    elog_add(ELOG_BAT_STATE,
             (threshold <= 10U) ? ELOG_LEVEL_ERROR : ELOG_LEVEL_WARN,
             info, sizeof(info));
}

void elog_log_power_panic(void)
{
    elog_add(ELOG_PWR_PANIC, ELOG_LEVEL_ERROR, NULL, 0U);
}

void elog_log_web_login_fail(uint32_t client_ip)
{
    /* info: client_ip(4) burst_count(2). First failure logs at once, then
     * one record per 60 s window carrying the failures in the burst.
     * logged_once: tick==0 sentinel yerine ayrik bayrak — life_timer
     * 49,7 gunde bir tam 0'a denk gelirse eski sentinel o pencerede
     * korumayi devre disi birakiyordu (O9.8). */
    static uint32_t burst = 0U;
    static uint32_t last_log_tick = 0U;
    static bool logged_once = false;

    uint32_t now = bsp_get_tick();

    burst++;
    if (!logged_once || ((now - last_log_tick) > 60000UL))
    {
        uint8_t info[16] = {0};
        elog_pack_u32_be(&info[0], client_ip);
        info[4] = (uint8_t)(burst >> 8);
        info[5] = (uint8_t)(burst);
        elog_add(ELOG_WEB_LOGIN_FAIL, ELOG_LEVEL_WARN, info, sizeof(info));
        last_log_tick = now;
        logged_once = true;
        burst = 0U;
    }
}
