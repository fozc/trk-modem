/*
 * runtime_stats.c
 *
 * Created on: Oct 24, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Fifteen-minute snapshots in the four-sector SPI flash log.
 */
#include "runtime_stats.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "w25qxx.h"
#include "spi.h"
#include "rtc.h"
#include "stack_monitor.h"
#include "bsp.h"
#include "console_logger.h"
#include <stddef.h>
#include <string.h>

#define STATS_PAYLOAD_SIZE 64U
#define STATS_VERSION 2U
#define STATS_CLOCK_VALID 1U
#define STATS_SOURCE_SHIFT 1U
#define STATS_SOURCE_MASK 0x0EU
#define STATS_FLAGS_MASK (STATS_CLOCK_VALID | STATS_SOURCE_MASK)

_Static_assert(RTC_SOURCE_WEB <= 7U, "RTC source must fit three bits");
_Static_assert(RUNTIME_COUNTER_COUNT == 13U, "stats field count");
_Static_assert(12U + (4U * RUNTIME_COUNTER_COUNT) == STATS_PAYLOAD_SIZE,
               "stats wire size");
_Static_assert(STATS_PAYLOAD_SIZE + LOG_ENTRY_OVERHEAD == 68U,
               "stats entry size");
_Static_assert(RUNTIME_STATS_SIZE / LOG_SECTOR_SIZE == 4U,
               "stats must use four sectors");

_Static_assert(RUNTIME_STATS_CAPACITY == (RUNTIME_STATS_SIZE / LOG_SECTOR_SIZE) *
               (LOG_SECTOR_SIZE / (STATS_PAYLOAD_SIZE + LOG_ENTRY_OVERHEAD)), "stats capacity");

static log_ctx_t stats_log;
static runtime_stats_t stats;
static uint32_t last_tick_ms;
static uint32_t save_seconds;
static bool storage_ready;

static int flash_read(uint32_t address, void *buffer, size_t length)
{
    (void)memset(buffer, 0xFF, length);
    w25qxx_read_buff(address, buffer, (uint32_t)length);
    return (spi_get_transfer_status() == SPI_TRANSFER_OK) ? 0 : -1;
}

static int flash_program(uint32_t address, const void *buffer, size_t length)
{
    return w25qxx_page_write(address, buffer, (uint32_t)length);
}

static int flash_erase(uint32_t address)
{
    return w25qxx_erase_sector(address);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        out[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint32_t get_u32(const uint8_t *data)
{
    uint32_t value = 0U;
    for (size_t index = 0U; index < 4U; index++)
    {
        value |= (uint32_t)data[index] << (index * 8U);
    }
    return value;
}

static void encode(const runtime_stats_t *record, uint8_t *data)
{
    data[0] = STATS_VERSION;
    /* Payload: version[0], flags/source[1], stack peak LE16[2..3],
     * checkpoint epoch[4..7], thirteen LE32 counters[8..59], last RTC epoch[60..63].
     * The log service adds sequence LE16 before it and CRC16 after it. */
    data[2] = (uint8_t)record->stack_peak_bytes;
    data[1] = (uint8_t)(((uint32_t)record->rtc_last_sync.source << STATS_SOURCE_SHIFT) |
                        (record->clock_valid ? STATS_CLOCK_VALID : 0U));
    data[3] = (uint8_t)(record->stack_peak_bytes >> 8U);
    put_u32(data + 4U, record->timestamp);
    put_u32(data + 60U, record->rtc_last_sync.timestamp);
    for (size_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
    {
        put_u32(data + 8U + (index * 4U), record->counters[index]);
    }
}

typedef struct
{
    uint32_t skip;
    runtime_stats_t *out;
    bool valid;
    uint32_t sequence;
} read_context_t;

static void read_record(const void *payload, uint32_t length,
                        uint32_t seq, void *context)
{
    read_context_t *reader = context;
    const uint8_t *data = payload;
    if (reader->skip != 0U)
    {
        reader->skip--;
        return;
    }
    if ((length != STATS_PAYLOAD_SIZE) || (data[0] != STATS_VERSION) ||
        ((data[1] & ~STATS_FLAGS_MASK) != 0U) ||
        ((data[1] >> STATS_SOURCE_SHIFT) > (uint32_t)RTC_SOURCE_WEB))
    {
        return;
    }
    *reader->out = (runtime_stats_t){0};
    reader->out->timestamp = get_u32(data + 4U);
    reader->out->stack_peak_bytes = (uint32_t)data[2] | ((uint32_t)data[3] << 8U);
    reader->out->rtc_last_sync.source = (rtc_source_t)(data[1] >> STATS_SOURCE_SHIFT);
    reader->out->rtc_last_sync.timestamp = get_u32(data + 60U);
    reader->out->clock_valid = (data[1] & STATS_CLOCK_VALID) != 0U;
    for (size_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
    {
        reader->out->counters[index] = get_u32(data + 8U + (index * 4U));
    }
    reader->sequence = seq;
    reader->valid = true;
}

bool runtime_stats_read(uint32_t skip, runtime_stats_t *out)
{
    log_page_ctx_t page = {0};
    read_context_t reader = {.skip = skip, .out = out, .valid = false};
    if (!storage_ready || (out == NULL) || (skip >= RUNTIME_STATS_CAPACITY))
    {
        return false;
    }
    runtime_stats_t record;
    reader.out = &record;
    if ((log_read_last(&stats_log, skip + 1U, read_record, &reader,
                       &page) != LOG_OK) || !reader.valid)
    {
        return false;
    }
    *out = record;
    return true;
}

void runtime_stats_snapshot(runtime_stats_t *out)
{
    if (out != NULL)
    {
        *out = stats;
        out->clock_valid = rtc_hw_is_valid();
        out->timestamp = out->clock_valid ? rtc_get_unix_epoch() : 0U;
        out->rtc_last_sync = rtc_get_last_sync();
        out->stack_peak_bytes = stack_monitor_get_used();
    }
}

bool runtime_stats_save(void)
{
    uint8_t payload[STATS_PAYLOAD_SIZE];
    runtime_stats_t record;
    runtime_stats_t verified;
    if (!storage_ready)
    {
        return false;
    }
    runtime_stats_snapshot(&record);
    if ((record.stack_peak_bytes > UINT16_MAX) ||
        ((uint32_t)record.rtc_last_sync.source > (uint32_t)RTC_SOURCE_WEB))
    {
        return false;
    }
    encode(&record, payload);
    const uint32_t sequence = log_get_next_seq(&stats_log);
    log_page_ctx_t page = {0};
    read_context_t reader = {.skip = 0U, .out = &verified, .valid = false};
    if ((log_write(&stats_log, payload) != LOG_OK) ||
        (log_read_last(&stats_log, 1U, read_record, &reader,
                       &page) != LOG_OK) ||
        !reader.valid || (reader.sequence != sequence))
    {
        return false;
    }
    uint8_t readback[STATS_PAYLOAD_SIZE];
    encode(&verified, readback);
    return memcmp(payload, readback, sizeof(payload)) == 0;
}

bool runtime_stats_init(void)
{
    const log_config_t config =
    {
        .base_addr = RUNTIME_STATS_ADDR,
        .sector_count = RUNTIME_STATS_SIZE / LOG_SECTOR_SIZE,
        .payload_size = STATS_PAYLOAD_SIZE,
        .ops = {.read = flash_read, .program = flash_program,
                .erase_sector = flash_erase}
    };
    log_page_ctx_t page = {0};
    read_context_t reader = {.skip = 0U, .out = &stats, .valid = false};
    (void)memset(&stats, 0, sizeof(stats));
    last_tick_ms = bsp_get_tick();
    save_seconds = 0U;
    storage_ready = false;
    if (log_init(&stats_log, &config) != LOG_OK)
    {
        return false;
    }
    if (log_read_last(&stats_log, 1U, read_record, &reader, &page) != LOG_OK)
    {
        return false;
    }
    /* A CRC-valid but unsupported payload must never be overwritten. */
    if ((page.page_count != 0U) && !reader.valid)
    {
        return false;
    }
    storage_ready = true;
    stats.counters[RUNTIME_BOOTS]++;
    return true;
}

void runtime_stats_add(runtime_counter_t counter, uint32_t amount)
{
    if ((uint32_t)counter < (uint32_t)RUNTIME_COUNTER_COUNT)
    {
        stats.counters[counter] += amount;
    }
}

uint32_t runtime_stats_get(runtime_counter_t counter)
{
    return ((uint32_t)counter < (uint32_t)RUNTIME_COUNTER_COUNT)
               ? stats.counters[counter] : 0U;
}

void runtime_stats_set(runtime_counter_t counter, uint32_t value)
{
    if ((uint32_t)counter < (uint32_t)RUNTIME_COUNTER_COUNT)
    {
        stats.counters[counter] = value;
    }
}

void runtime_stats_tick(bool iec_active)
{
    const uint32_t elapsed = (bsp_get_tick() - last_tick_ms) / 1000U;
    last_tick_ms += elapsed * 1000U;
    stats.counters[RUNTIME_LIFETIME] += elapsed;
    if (iec_active)
    {
        stats.counters[RUNTIME_IEC_ACTIVE_SECONDS] += elapsed;
    }
    save_seconds += elapsed;
    if (save_seconds >= RUNTIME_STATS_INTERVAL_SECONDS)
    {
        save_seconds = 0U;
        if (!runtime_stats_save())
        {
            CSLOG_ERR("Runtime stats save failed; next attempt in 15 min\r\n");
        }
    }
}

/*** end of file ***/
