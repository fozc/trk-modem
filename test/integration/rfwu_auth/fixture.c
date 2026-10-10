/*
 * fixture.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only RFWU dependency double; no hardware access.
 */
#include "raw_tcp_fw_update.h"
#include "nvram.h"
#include "app_ipc.h"
#include "bsp.h"
#include "bsp_random.h"
#include <string.h>

static rfwu_nvram_t record;
static uint32_t init_count;
static uint32_t write_count;
static uint32_t reboot_count;
static uint8_t response[64];
static uint32_t response_len;
static uint32_t tick;
static uint32_t nonce_word;
static bool rng_failure;
static bool init_failure;
static bool write_failure;
static bool finish_failure;
static uint32_t finish_count;
static uint32_t written_bytes;
static uint32_t init_offset;

const rfwu_nvram_t *nvram_get_rfwu(void)
{
    return &record;
}

void nvram_set_rfwu(const rfwu_nvram_t *value)
{
    record = *value;
}

int app_ipc_read_download_image_id(uint32_t *crc, uint32_t *size)
{
    *crc = 0U;
    *size = 0U;
    return -1;
}

int app_ipc_request_update(uint32_t crc, uint32_t size, bool force)
{
    (void)crc;
    (void)size;
    (void)force;
    return APP_IPC_OK;
}

static int capture_response(const void *data, int len)
{
    if ((0 < len) && (sizeof(response) >= (size_t)len))
    {
        (void)memcpy(response, data, (size_t)len);
        response_len = (uint32_t)len;
    }
    return len;
}

static int fake_init(uint32_t size, uint32_t offset)
{
    (void)size;
    init_offset = offset;
    init_count++;
    return init_failure ? -1 : 0;
}

static int fake_write(const uint8_t *data, uint32_t size)
{
    (void)data;
    write_count++;
    if (write_failure)
    {
        return -1;
    }
    written_bytes += size;
    return 0;
}

static int fake_finish(uint32_t size)
{
    (void)size;
    finish_count++;
    return finish_failure ? -1 : 0;
}

static void fake_reboot(void)
{
    reboot_count++;
}

void fixture_reset(uint32_t key)
{
    static const rfwu_fw_ops_t ops =
    {
        .fw_init = fake_init,
        .fw_write = fake_write,
        .fw_finish = fake_finish,
        .fw_reboot = fake_reboot
    };

    (void)memset(&record, 0, sizeof(record));
    (void)memset(response, 0, sizeof(response));
    record.shared_key = key;
    response_len = 0U;
    tick = 0U;
    nonce_word = 0x12345678U;
    rng_failure = false;
    init_failure = false;
    write_failure = false;
    finish_failure = false;
    finish_count = 0U;
    written_bytes = 0U;
    init_offset = 0U;
    init_count = 0U;
    write_count = 0U;
    reboot_count = 0U;
    rfwu_init(capture_response);
    rfwu_register_fw_ops(&ops);
}

uint32_t fixture_value(uint32_t field)
{
    switch (field)
    {
        case 0U: return response[4];
        case 1U: return response[RFWU_HEADER_SIZE];
        case 2U: return init_count;
        case 3U: return write_count;
        case 4U: return reboot_count;
        case 5U: return record.total_size;
        case 6U: return record.file_hash;
        case 7U: return finish_count;
        case 8U: return written_bytes;
        case 9U: return init_offset;
        case 10U: return record.received_bytes;
        default: return 0U;
    }
}
uint32_t bsp_get_tick(void)
{
    return tick;
}

bool bsp_random_secure_word(uint32_t *value)
{
    *value = nonce_word++;
    return !rng_failure;
}

void fixture_advance_tick(uint32_t delta)
{
    tick += delta;
}

void fixture_rng_failure(bool failed)
{
    rng_failure = failed;
}

void fixture_progress(uint32_t received)
{
    record.received_bytes = received;
}

uint32_t fixture_response(uint8_t *data)
{
    (void)memcpy(data, response, sizeof(response));
    return response_len;
}

void fixture_fw_failure(uint8_t operation, bool failed)
{
    switch (operation)
    {
        case 1U: init_failure = failed; break;
        case 2U: write_failure = failed; break;
        case 3U: finish_failure = failed; break;
        default: break;
    }
}
/*** end of file ***/
