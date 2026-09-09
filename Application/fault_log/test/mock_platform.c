/*
 * mock_platform.c
 *
 * Host-test doubles for the platform services fault_log.c reaches into.
 * Each feeder slot (primary + backup) is a 4096-byte RAM image; writes
 * can be forced to fail per copy and are counted so tests can prove
 * which copy a sync actually touched.
 */
#include "mock_platform.h"

#include <string.h>

#include "fault_log.h"
#include "shell.h"
#include "w25qxx.h"

#define SLOT_SIZE 4096U

static uint8_t  img_primary[MAX_POWER_LINE_COUNT][SLOT_SIZE];
static uint8_t  img_backup[MAX_POWER_LINE_COUNT][SLOT_SIZE];
static uint32_t writes_primary_cnt[MAX_POWER_LINE_COUNT];
static uint32_t writes_backup_cnt[MAX_POWER_LINE_COUNT];
static int      fail_primary_cnt[MAX_POWER_LINE_COUNT];
static int      fail_backup_cnt[MAX_POWER_LINE_COUNT];
static int      fail_primary_dest[MAX_POWER_LINE_COUNT];
static int      fail_backup_dest[MAX_POWER_LINE_COUNT];
static int      fail_erase_primary_cnt[MAX_POWER_LINE_COUNT];
static int      fail_erase_backup_cnt[MAX_POWER_LINE_COUNT];
static int      torn_budget_primary[MAX_POWER_LINE_COUNT];
static int      torn_budget_backup[MAX_POWER_LINE_COUNT];

void mock_reset(void)
{
    memset(img_primary, 0xFF, sizeof(img_primary));
    memset(img_backup, 0xFF, sizeof(img_backup));
    memset(writes_primary_cnt, 0, sizeof(writes_primary_cnt));
    memset(writes_backup_cnt, 0, sizeof(writes_backup_cnt));
    memset(fail_primary_cnt, 0, sizeof(fail_primary_cnt));
    memset(fail_backup_cnt, 0, sizeof(fail_backup_cnt));
    memset(fail_primary_dest, 0, sizeof(fail_primary_dest));
    memset(fail_backup_dest, 0, sizeof(fail_backup_dest));
    memset(fail_erase_primary_cnt, 0, sizeof(fail_erase_primary_cnt));
    memset(fail_erase_backup_cnt, 0, sizeof(fail_erase_backup_cnt));
    memset(torn_budget_primary, 0xFF, sizeof(torn_budget_primary));
    memset(torn_budget_backup, 0xFF, sizeof(torn_budget_backup));
}

void mock_fail_primary(uint8_t feeder, int n)
{
    fail_primary_cnt[feeder] = n;
}

void mock_fail_backup(uint8_t feeder, int n)
{
    fail_backup_cnt[feeder] = n;
}

void mock_fail_primary_destructive(uint8_t feeder, int n)
{
    fail_primary_dest[feeder] = n;
}

void mock_fail_backup_destructive(uint8_t feeder, int n)
{
    fail_backup_dest[feeder] = n;
}

void mock_fail_erase(uint8_t feeder, bool backup, int n)
{
    if (backup)
    {
        fail_erase_backup_cnt[feeder] = n;
    }
    else
    {
        fail_erase_primary_cnt[feeder] = n;
    }
}

void mock_torn_program(uint8_t feeder, bool backup, int pages)
{
    if (backup)
    {
        torn_budget_backup[feeder] = pages;
    }
    else
    {
        torn_budget_primary[feeder] = pages;
    }
}

uint32_t mock_writes_primary(uint8_t feeder)
{
    return writes_primary_cnt[feeder];
}

uint32_t mock_writes_backup(uint8_t feeder)
{
    return writes_backup_cnt[feeder];
}

/* Direct image access: tests construct flash states the module itself
 * must never produce (e.g. a backup holding the newer image). */
uint8_t *mock_slot_image_rw(bool backup, uint8_t feeder)
{
    return backup ? &img_backup[feeder][0] : &img_primary[feeder][0];
}

/* Resolve an address into (image, offset); NULL when out of the mapped
 * fault log areas. */
static uint8_t *mock_resolve(uint32_t addr, uint32_t *feeder, uint32_t *off)
{
    uint32_t base;
    uint32_t idx;

    if ((addr >= FAULT_LOG_ADDRESS)
        && (addr < (FAULT_LOG_ADDRESS + MAX_POWER_LINE_COUNT * SLOT_SIZE)))
    {
        base = FAULT_LOG_ADDRESS;
    }
    else if ((addr >= FAULT_LOG_BACKUP_ADDRESS)
        && (addr < (FAULT_LOG_BACKUP_ADDRESS + MAX_POWER_LINE_COUNT * SLOT_SIZE)))
    {
        base = FAULT_LOG_BACKUP_ADDRESS;
    }
    else
    {
        return NULL;
    }

    idx = (addr - base) / SLOT_SIZE;
    *feeder = idx;
    *off = (addr - base) % SLOT_SIZE;

    if (idx >= (uint32_t)MAX_POWER_LINE_COUNT)
    {
        return NULL;
    }
    return (base == FAULT_LOG_ADDRESS) ? &img_primary[idx][0]
                                       : &img_backup[idx][0];
}

void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len)
{
    uint32_t feeder = 0U;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &feeder, &off);

    if ((image == NULL) || ((off + len) > SLOT_SIZE))
    {
        memset(buff, 0xFF, len);
        return;
    }
    memcpy(buff, &image[off], len);
}

/* Apply the write-failure knobs to a mutating raw op on the given slot.
 * Returns true when the op must fail; the destructive knob wipes the
 * whole slot image first (the real driver erases before programming). */
static bool raw_write_knob_fails(uint32_t feeder, bool is_primary)
{
    if (is_primary)
    {
        if (fail_primary_dest[feeder] > 0)
        {
            fail_primary_dest[feeder]--;
            memset(&img_primary[feeder][0], 0xFF, SLOT_SIZE);
            return true;
        }
        if (fail_primary_cnt[feeder] > 0)
        {
            fail_primary_cnt[feeder]--;
            return true;
        }
    }
    else
    {
        if (fail_backup_dest[feeder] > 0)
        {
            fail_backup_dest[feeder]--;
            memset(&img_backup[feeder][0], 0xFF, SLOT_SIZE);
            return true;
        }
        if (fail_backup_cnt[feeder] > 0)
        {
            fail_backup_cnt[feeder]--;
            return true;
        }
    }
    return false;
}

/* NOR program semantics: bits can only go 1 -> 0. */
static void nor_program(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++)
    {
        dst[i] &= src[i];
    }
}

int w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t buff_len)
{
    uint32_t feeder = 0U;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &feeder, &off);
    bool is_primary = (addr < FAULT_LOG_BACKUP_ADDRESS);

    if (image == NULL)
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    if (is_primary)
    {
        writes_primary_cnt[feeder]++;
    }
    else
    {
        writes_backup_cnt[feeder]++;
    }

    if (raw_write_knob_fails(feeder, is_primary))
    {
        return W25QXX_RES_WRITE_FAIL;
    }

    if ((off + buff_len) > SLOT_SIZE)
    {
        return W25QXX_RES_INVALID_PARAM;
    }
    memcpy(&image[off], buff, buff_len);
    return W25QXX_RES_OK;
}

/* Sector erase used by the flash->flash repair copy path. Honours the
 * write-failure knobs (destructive wipes, plain rejects) plus a dedicated
 * non-destructive erase-failure knob. */
int w25qxx_erase_sector(uint32_t addr)
{
    uint32_t feeder = 0U;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &feeder, &off);
    bool is_primary = (addr < FAULT_LOG_BACKUP_ADDRESS);

    if ((image == NULL) || (off != 0U))
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    if (raw_write_knob_fails(feeder, is_primary))
    {
        return W25QXX_RES_WRITE_FAIL;
    }

    if (is_primary)
    {
        if (fail_erase_primary_cnt[feeder] > 0)
        {
            fail_erase_primary_cnt[feeder]--;
            return W25QXX_RES_WRITE_FAIL;
        }
    }
    else
    {
        if (fail_erase_backup_cnt[feeder] > 0)
        {
            fail_erase_backup_cnt[feeder]--;
            return W25QXX_RES_WRITE_FAIL;
        }
    }

    memset(image, 0xFF, SLOT_SIZE);
    return W25QXX_RES_OK;
}

/* Page program used by the flash->flash repair copy path. In addition to
 * the write-failure knobs, mock_torn_program() arms a one-shot torn write:
 * the first `pages` page programs succeed, the NEXT one programs only half
 * of its bytes and then fails (power cut mid-page); the knob disarms
 * itself after firing. */
int w25qxx_page_write(uint32_t addr, const void *buff, uint32_t len)
{
    uint32_t feeder = 0U;
    uint32_t off = 0U;
    uint8_t *image = mock_resolve(addr, &feeder, &off);
    bool is_primary = (addr < FAULT_LOG_BACKUP_ADDRESS);
    int *torn_budget = is_primary ? &torn_budget_primary[feeder]
                                  : &torn_budget_backup[feeder];

    if ((image == NULL) || ((off + len) > SLOT_SIZE))
    {
        return W25QXX_RES_INVALID_PARAM;
    }

    if (raw_write_knob_fails(feeder, is_primary))
    {
        return W25QXX_RES_WRITE_FAIL;
    }

    if (*torn_budget >= 0)
    {
        if (*torn_budget == 0)
        {
            uint32_t half = len / 2U;

            nor_program(&image[off], buff, half);
            *torn_budget = -1;   /* fired - disarm */
            return W25QXX_RES_WRITE_FAIL;
        }
        (*torn_budget)--;
    }

    nor_program(&image[off], buff, len);
    return W25QXX_RES_OK;
}

/* ---------------------------------------------------------------- */
/* Silenced platform services                                       */
/* ---------------------------------------------------------------- */

int shell_register_command(const shell_cmd_t *cmd)
{
    (void)cmd;
    return 0;
}

/* cp56time2a.c reaches the host build without any CSLOG macro visible
 * (the firmware gets it via a different include chain); its implicit
 * calls land on this silenced double. */
#undef CSLOG
void CSLOG(const char *fmt, ...)
{
    (void)fmt;
}

/* Minimal decimal parse for the shell argument path (utils.c double). */
int xstrtoi(const char *str)
{
    int value = 0;
    bool neg = false;

    if (NULL == str)
    {
        return 0;
    }
    if ('-' == *str)
    {
        neg = true;
        str++;
    }
    while ((*str >= '0') && (*str <= '9'))
    {
        value = (value * 10) + (*str - '0');
        str++;
    }
    return neg ? -value : value;
}

/* Fixed wall clock for cp56time2a_now(). */
bsp_rtc_t bsp_get_datetime(void)
{
    static bsp_rtc_t fixed = {
        .year = 26, .month = 9, .day = 9,
        .hour = 8, .minute = 33, .second = 50, .millisec = 0
    };
    return fixed;
}

unsigned int xcprintf(const char *color, const char *fmt, ...)
{
    (void)color;
    (void)fmt;
    return 0U;
}
