/**********************************************************************
 *
 * Filename:    mock_platform.c
 *
 * Description: Host-test doubles for the platform services libiec104
 *              reaches into. iec104.c includes breaker.h, fault_log.h,
 *              rtc.h and (through iec104_config.c) nvram.h; on the host
 *              those are replaced by the minimal fakes below.
 *
 * Notes:       Logging is silenced so the test output stays readable.
 *
 **********************************************************************/

#include "mock_platform.h"

#include <string.h>

#include "types.h"
#include "nvram.h"
#include "console_logger_config.h"
#include "breaker.h"
#include "fault_log.h"
#include "rtc.h"
#include "bsp.h"

/* ---------------------------------------------------------------- */
/* nvram                                                             */
/* ---------------------------------------------------------------- */

static breaker_t mock_breaker;
static iec104_config_t mock_iec104_config;

breaker_t *nvram_get_breaker_rw(void)
{
    return &mock_breaker;
}

iec104_config_t *nvram_get_iec104_config_rw(void)
{
    return &mock_iec104_config;
}

int nvram_sync(bool crc_no_check)
{
    (void)crc_no_check;
    return 0;
}

/* ---------------------------------------------------------------- */
/* breaker                                                           */
/* ---------------------------------------------------------------- */

const power_line_t *breaker_get_power_line_by_idx(uint32_t idx)
{
    if (idx >= (uint32_t)MAX_POWER_LINE_COUNT)
    {
        return NULL;
    }

    return &mock_breaker.line[idx];
}

uint8_t breaker_get_active_powerline_count(void)
{
    uint8_t count = 0U;

    for (uint8_t idx = 0U; idx < MAX_POWER_LINE_COUNT; idx++)
    {
        if (0U != mock_breaker.line[idx].iec104.in_use)
        {
            count++;
        }
    }

    return count;
}

void mock_breaker_set_line_in_use(uint8_t feeder_id, bool in_use)
{
    if (feeder_id < MAX_POWER_LINE_COUNT)
    {
        mock_breaker.line[feeder_id].iec104.in_use = (uint8_t)(in_use ? 1U : 0U);
    }
}

/* ---------------------------------------------------------------- */
/* fault_log                                                         */
/* ---------------------------------------------------------------- */

#define MOCK_FAULT_LOG_CAPACITY   15U
#define MOCK_FAULT_LOG_TYPE_COUNT  2U

static uint8_t     mock_fault_count[MAX_POWER_LINE_COUNT][PHASE_MAX][MOCK_FAULT_LOG_TYPE_COUNT];
static fault_log_t mock_fault_entry[MAX_POWER_LINE_COUNT][PHASE_MAX][MOCK_FAULT_LOG_TYPE_COUNT][MOCK_FAULT_LOG_CAPACITY];

void mock_fault_log_fill(uint8_t feeder_id, uint8_t phase_id,
                         fault_log_type_t type, uint8_t count)
{
    if ((feeder_id >= MAX_POWER_LINE_COUNT) || (phase_id >= PHASE_MAX))
    {
        return;
    }

    if (count > MOCK_FAULT_LOG_CAPACITY)
    {
        count = MOCK_FAULT_LOG_CAPACITY;
    }

    mock_fault_count[feeder_id][phase_id][type] = count;

    for (uint8_t n = 0U; n < count; n++)
    {
        fault_log_t *entry = &mock_fault_entry[feeder_id][phase_id][type][n];

        (void)memset(entry, 0, sizeof(*entry));
        entry->fault_current                = (float)n;
        entry->fault_duration_ms            = (uint16_t)(n + 1U);
        entry->info.feeder                  = feeder_id & 0x07U;
        entry->info.phase                   = phase_id & 0x03U;
        entry->info.nominal_current_status  = (uint8_t)(n & 0x01U);
        entry->info.power_status            = (uint8_t)((n + 1U) & 0x01U);
        entry->info.type                    = (uint8_t)type;
    }
}

uint8_t fault_log_get_temp_count(uint8_t feeder_id, uint8_t phase_id)
{
    if ((feeder_id >= MAX_POWER_LINE_COUNT) || (phase_id >= PHASE_MAX))
    {
        return 0U;
    }

    return mock_fault_count[feeder_id][phase_id][FAULT_LOG_TYPE_TEMPORARY];
}

uint8_t fault_log_get_perm_count(uint8_t feeder_id, uint8_t phase_id)
{
    if ((feeder_id >= MAX_POWER_LINE_COUNT) || (phase_id >= PHASE_MAX))
    {
        return 0U;
    }

    return mock_fault_count[feeder_id][phase_id][FAULT_LOG_TYPE_PERMANENT];
}

bool fault_log_read_nth(uint8_t feeder_id, uint8_t phase_id, fault_log_type_t type,
                        uint8_t n, fault_log_t *log)
{
    if ((feeder_id >= MAX_POWER_LINE_COUNT) || (phase_id >= PHASE_MAX) || (NULL == log))
    {
        return false;
    }

    if (n >= mock_fault_count[feeder_id][phase_id][type])
    {
        return false;
    }

    *log = mock_fault_entry[feeder_id][phase_id][type][n];

    return true;
}

/* ---------------------------------------------------------------- */
/* rtc / bsp                                                         */
/* ---------------------------------------------------------------- */

void rtc_sync(const rtc_t *dt)
{
    (void)dt;
}

void rtc_print_now(void)
{
}

/* Fixed wall clock so CP56Time2a stamps are reproducible. */
bsp_rtc_t bsp_get_datetime(void)
{
    bsp_rtc_t now;

    (void)memset(&now, 0, sizeof(now));
    now.year   = 26U;
    now.month  = 3U;
    now.day    = 11U;
    now.hour   = 12U;
    now.minute = 23U;
    now.second = 47U;

    return now;
}

/* ---------------------------------------------------------------- */
/* logging - silenced                                                */
/* ---------------------------------------------------------------- */

bool console_logger_is_enabled_flag = false;

/* Seviye kapi stub'u: testler log ciktisi dogrulamadigindan her seyi
 * reddeder - CSLOG ailesinin modullu makrolari bunu cagirir. */
bool console_logger_level_enabled(log_mod_t module, log_lvl_t min_level)
{
    (void)module;
    (void)min_level;
    return false;
}

unsigned int xprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0U;
}

unsigned int xcprintf(const char *color, const char *fmt, ...)
{
    (void)color;
    (void)fmt;
    return 0U;
}

/* ---------------------------------------------------------------- */

void mock_platform_reset(void)
{
    (void)memset(&mock_breaker, 0, sizeof(mock_breaker));
    (void)memset(&mock_iec104_config, 0, sizeof(mock_iec104_config));
    (void)memset(mock_fault_count, 0, sizeof(mock_fault_count));
    (void)memset(mock_fault_entry, 0, sizeof(mock_fault_entry));
}

/*** end of file ***/
