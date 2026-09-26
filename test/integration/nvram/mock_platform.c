/*
 * mock_platform.c
 *
 * Host-test double for the w25qxx driver as used by the two-slot NVRAM
 * core. Two independent 4 KB sectors, real NOR semantics and phase-level
 * fault injection at the erase / program / verify boundaries.
 */
#include "mock_platform.h"

#include <string.h>

#include "nvram.h"
#include "w25qxx.h"

#define SLOT_SIZE 4096U

static uint8_t  slot_img[2][SLOT_SIZE];
static uint32_t write_buff_cnt[2];
static uint32_t erase_cnt[2];
static uint32_t page_cnt[2];
static mock_fail_stage_t fail_stage[2];
static bool     fail_erase_once[2];
static int      fail_page_at[2];

void mock_reset(void)
{
    memset(slot_img, 0xFF, sizeof(slot_img));
    memset(write_buff_cnt, 0, sizeof(write_buff_cnt));
    memset(erase_cnt, 0, sizeof(erase_cnt));
    memset(page_cnt, 0, sizeof(page_cnt));
    fail_stage[0] = MOCK_FAIL_NONE;
    fail_stage[1] = MOCK_FAIL_NONE;
    fail_erase_once[0] = false;
    fail_erase_once[1] = false;
    fail_page_at[0] = -1;
    fail_page_at[1] = -1;
}

void mock_fail_write(uint32_t slot, mock_fail_stage_t stage)
{
    fail_stage[slot] = stage;
}

void mock_fail_erase(uint32_t slot)
{
    fail_erase_once[slot] = true;
}

void mock_fail_page(uint32_t slot, int page)
{
    fail_page_at[slot] = page;
}

uint32_t mock_write_buff_calls(uint32_t slot)
{
    return write_buff_cnt[slot];
}

uint32_t mock_erase_calls(uint32_t slot)
{
    return erase_cnt[slot];
}

uint32_t mock_page_writes(uint32_t slot)
{
    return page_cnt[slot];
}

uint8_t *mock_slot_image_rw(uint32_t slot)
{
    return &slot_img[slot][0];
}

/* Resolve an address into (image, offset); NULL when outside both slots. */
static uint8_t *mock_resolve(uint32_t addr, uint32_t *off)
{
    if ((addr >= NVRAM_ADDRESS)
        && (addr < (NVRAM_ADDRESS + SLOT_SIZE)))
    {
        *off = addr - NVRAM_ADDRESS;
        return &slot_img[MOCK_SLOT_A][0];
    }
    if ((addr >= NVRAM_BACKUP_ADDRESS)
        && (addr < (NVRAM_BACKUP_ADDRESS + SLOT_SIZE)))
    {
        *off = addr - NVRAM_BACKUP_ADDRESS;
        return &slot_img[MOCK_SLOT_B][0];
    }
    return NULL;
}

static uint32_t mock_slot_of(uint32_t addr)
{
    return (addr < NVRAM_BACKUP_ADDRESS) ? MOCK_SLOT_A : MOCK_SLOT_B;
}

void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len)
{
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &off);

    if ((image == NULL) || ((off + len) > SLOT_SIZE))
    {
        memset(buff, 0xFF, len);
        return;
    }
    memcpy(buff, &image[off], len);
}

/* Sector erase: address must be a slot base; sets the slot to 0xFF. */
int w25qxx_erase_sector(uint32_t addr)
{
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &off);
    uint32_t slot;

    if ((image == NULL) || (off != 0U))
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    slot = mock_slot_of(addr);
    erase_cnt[slot]++;

    if (fail_erase_once[slot])
    {
        fail_erase_once[slot] = false;
        return W25QXX_RES_WRITE_FAIL;
    }

    memset(image, 0xFF, SLOT_SIZE);
    return W25QXX_RES_OK;
}

/* Page program: NOR AND semantics, must stay within one 256 B page. */
int w25qxx_page_write(uint32_t addr, const void *buff, uint32_t len)
{
    const uint8_t *src = buff;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &off);
    uint32_t slot;

    if ((image == NULL) || ((off + len) > SLOT_SIZE)
        || (((off % 256U) + len) > 256U))
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    slot = mock_slot_of(addr);

    if ((fail_page_at[slot] >= 0)
        && ((int)(off / 256U) == fail_page_at[slot]))
    {
        fail_page_at[slot] = -1;   /* one-shot: destination stays torn */
        return W25QXX_RES_WRITE_FAIL;
    }

    for (uint32_t i = 0U; i < len; i++)
    {
        image[off + i] &= src[i];
    }
    page_cnt[slot]++;
    return W25QXX_RES_OK;
}

/* Whole-image write: erase -> program -> verify, with one-shot injection
 * at each phase boundary (the dangerous partial states of the real
 * driver: untouched / erased-but-unwritten / written-but-unverified). */
int w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t len)
{
    const uint8_t *src = buff;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &off);
    uint32_t slot;
    mock_fail_stage_t stage;

    if ((image == NULL) || (off != 0U) || (len > SLOT_SIZE))
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    slot = mock_slot_of(addr);
    write_buff_cnt[slot]++;
    stage = fail_stage[slot];
    fail_stage[slot] = MOCK_FAIL_NONE;

    if (stage == MOCK_FAIL_BEFORE_ERASE)
    {
        return W25QXX_RES_WRITE_FAIL;
    }

    memset(image, 0xFF, SLOT_SIZE);
    erase_cnt[slot]++;

    if (stage == MOCK_FAIL_AFTER_ERASE)
    {
        return W25QXX_RES_WRITE_FAIL;
    }

    for (uint32_t i = 0U; i < len; i++)
    {
        image[i] &= src[i];
    }

    if (stage == MOCK_FAIL_AFTER_PROGRAM)
    {
        return W25QXX_RES_WRITE_FAIL;
    }

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
