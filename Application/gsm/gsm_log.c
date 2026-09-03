/*
 * gsm_log.c
 *
 *  Created on: Jan 10, 2026
 *      Author: fatih
 */
#include "gsm_log.h"
#include "elog.h"
#include "nvram.h"
#include "time_service.h"
#include <string.h>

/* ── Console log level ──────────────────────────────────────────── */

static gsm_log_level_t s_level = GSM_LOG_VERBOSE;

void gsm_log_init(void)
{
    uint8_t stored = nvram_get_gsm_log_level();

    if (stored > (uint8_t)GSM_LOG_VERBOSE)
    {
        stored = (uint8_t)GSM_LOG_VERBOSE;
    }

    s_level = (gsm_log_level_t)stored;
}

void gsm_log_set_level(gsm_log_level_t level)
{
    if (level > GSM_LOG_VERBOSE)
    {
        level = GSM_LOG_VERBOSE;
    }

    s_level = level;
    nvram_set_gsm_log_level((uint8_t)level);
    nvram_sync(false);
}

gsm_log_level_t gsm_log_get_level(void)
{
    return s_level;
}
