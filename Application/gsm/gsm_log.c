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

/* ── Flash event logging (elog) ─────────────────────────────────── */

void gsm_log_modem_event(elog_code_t code)
{
	elog_add(code, ELOG_LEVEL_WARN, NULL, 0);
}

void gsm_log_modem_event_with_arg(elog_code_t code, const void *arg,
                                  uint8_t arg_len)
{
	uint8_t info[16] = {0};

	memcpy(info, arg,
	       arg_len < sizeof(info) ? arg_len : sizeof(info));

	elog_add(code, ELOG_LEVEL_WARN, info, sizeof(info));
}

void gsm_log_modem_error(elog_code_t code, const char *info,
                         uint8_t info_len)
{
	elog_add(code, ELOG_LEVEL_ERROR, info, info_len);
}
