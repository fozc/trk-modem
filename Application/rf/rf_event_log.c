/*
 * rf_event_log.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Store RF event packets using the existing SPI flash log ring.
 */

#include "rf_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "w25qxx.h"
#include <string.h>

#define RF_EVENT_ENTRY_SIZE (LOG_ENTRY_OVERHEAD + RF_SCP_EVENT_SIZE)
#define RF_EVENT_SECTORS (RF_EVENT_LOG_SIZE / LOG_SECTOR_SIZE)
#define RF_EVENT_CAPACITY \
    (RF_EVENT_SECTORS * (LOG_SECTOR_SIZE / RF_EVENT_ENTRY_SIZE))

_Static_assert(sizeof(rf_event_record_t) == 60U,
               "RF event payload must preserve all 60 bytes");
_Static_assert(RF_EVENT_ENTRY_SIZE == 64U,
               "RF log entries must occupy 64 bytes");
_Static_assert(RF_EVENT_SECTORS == LOG_MIN_SECTOR_COUNT,
               "RF event log must contain two sectors");
_Static_assert(RF_EVENT_CAPACITY == 128U,
               "RF event log must hold at most 128 entries");
_Static_assert(0U == (RF_EVENT_LOG_ADDR % LOG_SECTOR_SIZE),
               "RF event log must be sector aligned");

/* All accesses are synchronous in cooperative process context. */
static log_ctx_t event_log;

static int flash_read(uint32_t address, void *buffer, size_t length)
{
    /* The driver read API has no status. Clear stale data first; entry
     * CRC and exact append readback detect incomplete/corrupt transfers.
     */
    (void)memset(buffer, 0xFF, length);
    w25qxx_read_buff(address, buffer, (uint32_t)length);
    return 0;
}

static int flash_program(uint32_t address, const void *buffer,
                         size_t length)
{
    return w25qxx_page_write(address, buffer, (uint32_t)length);
}

static int flash_erase(uint32_t address)
{
    return w25qxx_erase_sector(address);
}

bool rf_event_log_init(void)
{
    const log_config_t config =
    {
        .base_addr = RF_EVENT_LOG_ADDR,
        .sector_count = RF_EVENT_SECTORS,
        .payload_size = RF_SCP_EVENT_SIZE,
        .ops =
        {
            .read = flash_read,
            .program = flash_program,
            .erase_sector = flash_erase
        }
    };

    return LOG_OK == log_init(&event_log, &config);
}

typedef struct
{
    const uint8_t *data;
    uint32_t seq;
    bool matched;
} readback_t;

static void verify_record(const void *payload, uint32_t length,
                          uint32_t seq, void *context)
{
    readback_t *readback = context;

    readback->matched = (RF_SCP_EVENT_SIZE == length) &&
                        (readback->seq == seq) &&
                        (0 == memcmp(payload, readback->data, length));
}

bool rf_event_log_append(const uint8_t *data, size_t length)
{
    rf_scp_event_t event;
    log_page_ctx_t page = {0};
    readback_t readback =
    {
        .data = data,
        .seq = log_get_next_seq(&event_log),
        .matched = false
    };

    if ((RF_CMD_OK != rf_scp_decode_event(data, length, &event)) ||
        (LOG_OK != log_write(&event_log, data)))
    {
        return false;
    }
    return (LOG_OK == log_read_last(&event_log, 1U, verify_record,
                                    &readback, &page)) &&
           (1U == page.page_count) && readback.matched;
}

typedef struct
{
    size_t skip;
    size_t count;
    rf_event_record_t *records;
} read_context_t;

static void copy_record(const void *payload, uint32_t length,
                        uint32_t seq, void *context)
{
    read_context_t *read = context;

    (void)seq;
    if (0U < read->skip)
    {
        read->skip--;
    }
    else if (RF_SCP_EVENT_SIZE == length)
    {
        (void)memcpy(read->records[read->count].data, payload, length);
        read->count++;
    }
    else
    {
        /* log_init fixes the payload length for this context. */
    }
}

bool rf_event_log_read_recent(size_t skip, size_t count,
                              rf_event_record_t *records,
                              size_t *out_count)
{
    log_page_ctx_t page = {0};
    read_context_t read = {.skip = skip, .count = 0U, .records = records};

    if (NULL == out_count)
    {
        return false;
    }
    *out_count = 0U;
    if ((NULL == records) || (0U == count) ||
        (RF_EVENT_CAPACITY < count) ||
        (RF_EVENT_CAPACITY < skip) ||
        (RF_EVENT_CAPACITY < skip + count))
    {
        return false;
    }
    if (LOG_OK != log_read_last(&event_log, (uint32_t)(skip + count),
                                copy_record, &read, &page))
    {
        return false;
    }
    *out_count = read.count;
    return true;
}

/*** end of file ***/
