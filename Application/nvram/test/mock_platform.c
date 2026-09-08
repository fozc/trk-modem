/*
 * mock_platform.c
 *
 * Host-test doubles for the platform services nvram.c reaches into.
 * The two NVRAM flash slots live as RAM images; writes can be forced
 * to fail per slot and are counted so tests can prove whether a sync
 * actually touched the flash.
 */
#include "mock_platform.h"

#include <string.h>

#include "nvram.h"
#include "w25qxx.h"

/* ---------------------------------------------------------------- */
/* w25qxx: RAM slot images + failure injection                       */
/* ---------------------------------------------------------------- */

static uint8_t slot_main[sizeof(nvram_t)];
static uint8_t slot_backup[sizeof(nvram_t)];
static uint32_t writes_main;
static uint32_t writes_backup;
static int fail_main;
static int fail_backup;

void mock_reset(void)
{
    memset(slot_main, 0xFF, sizeof(slot_main));
    memset(slot_backup, 0xFF, sizeof(slot_backup));
    writes_main = 0U;
    writes_backup = 0U;
    fail_main = 0;
    fail_backup = 0;
}

void mock_fail_main(int n)
{
    fail_main = n;
}

void mock_fail_backup(int n)
{
    fail_backup = n;
}

uint32_t mock_writes_main(void)
{
    return writes_main;
}

uint32_t mock_writes_backup(void)
{
    return writes_backup;
}

const uint8_t *mock_slot_image(bool backup)
{
    return backup ? slot_backup : slot_main;
}

void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len)
{
    const uint8_t *image = NULL;
    uint32_t max_len = 0U;

    if (addr == NVRAM_ADDRESS)
    {
        image = slot_main;
        max_len = (uint32_t)sizeof(slot_main);
    }
    else if (addr == NVRAM_BACKUP_ADDRESS)
    {
        image = slot_backup;
        max_len = (uint32_t)sizeof(slot_backup);
    }

    if ((image == NULL) || (len > max_len))
    {
        memset(buff, 0xFF, len);
        return;
    }
    memcpy(buff, image, len);
}

int w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t buff_len)
{
    uint8_t *image = NULL;

    if (addr == NVRAM_ADDRESS)
    {
        writes_main++;
        if (fail_main > 0)
        {
            fail_main--;
            return W25QXX_RES_WRITE_FAIL;
        }
        image = slot_main;
    }
    else if (addr == NVRAM_BACKUP_ADDRESS)
    {
        writes_backup++;
        if (fail_backup > 0)
        {
            fail_backup--;
            return W25QXX_RES_WRITE_FAIL;
        }
        image = slot_backup;
    }
    else
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    if (buff_len > (uint32_t)sizeof(slot_main))
    {
        return W25QXX_RES_INVALID_PARAM;
    }
    memcpy(image, buff, buff_len);
    return W25QXX_RES_OK;
}

/* ---------------------------------------------------------------- */
/* Silenced platform services                                       */
/* ---------------------------------------------------------------- */

void console_logger_init(void)
{
}

void elog_log_nvram_recovery(uint8_t action, uint32_t stored_crc,
                             uint32_t calc_crc)
{
    (void)action;
    (void)stored_crc;
    (void)calc_crc;
}

unsigned int xcprintf(const char *color, const char *fmt, ...)
{
    (void)color;
    (void)fmt;
    return 0U;
}
