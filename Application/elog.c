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
 * entries may straddle flash pages, the library splits the programs). A sector
 * is erased only when the write head wraps to it, so a log_add costs one
 * 28-byte page program instead of the four sector erase+rewrites of the old
 * engine.
 */

#include "elog.h"
#include <string.h>
#include "bsp.h"
#include "shell.h"
#include "xprintf.h"
#include "rtc.h"
#include "datetime.h"
#include "w25qxx.h"
#include "spi_flash_organization.h"

/* The HAL header (stm32u3xx_hal_flash.h) defines FLASH_PAGE_SIZE for the
 * internal flash; the log library needs the SPI chip page size instead. The
 * library header must come after every HAL include so its definition wins
 * inside this module, and nothing here may use the HAL value afterwards. */
#undef FLASH_PAGE_SIZE
#include "spi_flash_log.h"

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
    CSLOG("Capacity: %u entries, total written: %u\r\n",
          elog_get_max_entries(), elog_get_entry_count());
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

void elog_clear(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        CSLOG("Error: elog not initialized\r\n");
        return;
    }

    log_config_t cfg;

    elog_config_build(&cfg);

    for (uint32_t s = 0U; s < elog_ctx.sector_count; s++)
    {
        (void)elog_flash_erase_sector(elog_ctx.base_addr + (s * LOG_SECTOR_SIZE));
    }

    (void)log_init(&elog_ctx, &cfg);
    CSLOG("Error log cleared. All entries removed.\r\n");
}

uint16_t elog_get_entry_count(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        return 0U;
    }
    return (uint16_t)log_get_next_seq(&elog_ctx);
}

uint16_t elog_get_max_entries(void)
{
    if (!log_is_initialized(&elog_ctx))
    {
        return 0U;
    }
    /* One sector is always kept erased for the circular invariant. */
    return (uint16_t)(elog_ctx.entries_per_sector * (elog_ctx.sector_count - 1U));
}

/* ---- shell commands ---------------------------------------------------- */

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

static uint32_t elog_stored_count(void)
{
    uint32_t count = 0U;

    if (log_read_all(&elog_ctx, elog_count_visitor, &count) != LOG_OK)
    {
        return 0U;
    }
    return count;
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
    SHELL_LOG("Next Seq         : %u (16-bit, wraps at 65535)\r\n", elog_get_entry_count());
    SHELL_LOG("Stored Entries   : %u / %u\r\n", elog_stored_count(), elog_get_max_entries());

    SHELL_LOG("\r\nMemory Addresses:\r\n");
    SHELL_LOG("  Log Area       : 0x%08X (%u sectors)\r\n",
              elog_ctx.base_addr, elog_ctx.sector_count);
    SHELL_LOG("  Entry Size     : %u bytes (payload %u)\r\n",
              elog_ctx.entry_size, elog_ctx.payload_size);

    SHELL_LOG("========================================\r\n");
    SHELL_LOG("\r\nCommands:\r\n");
    SHELL_LOG("  elog          - Show this information\r\n");
    SHELL_LOG("  elog dump     - Dump all log entries\r\n");
    SHELL_LOG("  elog clear    - Clear all log entries\r\n");
    SHELL_LOG("========================================\r\n\r\n");
}

static void elog_dump_visitor(const void *payload, uint32_t payload_size,
                              uint32_t seq, void *user_ctx)
{
    const elog_entry_t *entry = (const elog_entry_t *)payload;

    (void)payload_size;
    (void)user_ctx;

    datetime_t dt;
    dt_conv_from_epoch(entry->timestamp, &dt);

    char hex_str[64];
    int pos = 0;
    for (uint8_t j = 0; j < sizeof(entry->info) && pos < (int)sizeof(hex_str) - 3; j++) {
        pos += xsnprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X ", entry->info[j]);
    }
    if (pos > 0) hex_str[pos - 1] = '\0'; /* Remove trailing space */

    SHELL_LOG("%-6u %-5s %04u-%02u-%02u %02u:%02u:%02u %-24s %s\r\n",
            seq,
            elog_level_to_string((elog_level_t)entry->level),
            dt.date.year, dt.date.month, dt.date.day,
            dt.time.hour, dt.time.minute, dt.time.second,
            elog_code_to_string((elog_code_t)entry->code),
            hex_str);
}

static void elog_shell_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!log_is_initialized(&elog_ctx)) {
        SHELL_LOG("Error: elog not initialized\r\n");
        return;
    }

    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("        ERROR LOG DUMP\r\n");
    SHELL_LOG("========================================\r\n\r\n");

    SHELL_LOG("%-6s %-5s %-19s %-24s %s\r\n",
              "Seq", "Level", "Timestamp", "Code", "Info (Hex)");
    SHELL_LOG("------ ----- ------------------- ------------------------ ------------------------------------------------\r\n");

    if (log_read_all(&elog_ctx, elog_dump_visitor, NULL) != LOG_OK) {
        SHELL_LOG("Dump failed: flash read error\r\n");
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

    uint16_t count = elog_get_entry_count();
    elog_clear();
    SHELL_LOG("\r\nCleared log (seq was at %u).\r\n", count);
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
        SHELL_LOG("Usage: elog [dump|clear|info]\r\n");
    }

    return 1;
}

void elog_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "elog",
        .desc = "Error log management (dump/clear/info)",
        .level = 0,
        .func = elog_shell_command
    });
}

void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area)
{
    uint8_t info[16] = {0};

    info[0] = (uint8_t)((ip >> 24U) & 0xFFU);
    info[1] = (uint8_t)((ip >> 16U) & 0xFFU);
    info[2] = (uint8_t)((ip >>  8U) & 0xFFU);
    info[3] = (uint8_t)( ip         & 0xFFU);

    info[4] = (uint8_t)source;

    if (p_area != NULL)
    {
        size_t len = strlen(p_area);
        if (len > 11U)
        {
            len = 11U;
        }
        memcpy(&info[5], p_area, len);
    }

    elog_add(code, ELOG_LEVEL_INFO, info, sizeof(info));

    const char *src_str = (source == ELOG_SOURCE_WEB) ? "web" : "serial";
    CSLOG_WARN("[ELOG] Config changed: %s src:%s ip:%u.%u.%u.%u\r\n",
            p_area ? p_area : "?", src_str,
            (unsigned int)info[0], (unsigned int)info[1],
            (unsigned int)info[2], (unsigned int)info[3]);
}
