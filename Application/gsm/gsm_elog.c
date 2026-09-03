/*
 * gsm_elog.c
 *
 *  Created on: 3 Eyl 2026
 *      Author: fatih
 */
#include "gsm_elog.h"
#include "elog.h"
#include <string.h>

/* ── Flash event logging (elog) ─────────────────────────────────── */

void gsm_elog_modem_event(elog_code_t code)
{
	elog_add(code, ELOG_LEVEL_WARN, NULL, 0);
}

void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg,
                                  uint8_t arg_len)
{
	uint8_t info[16] = {0};

	memcpy(info, arg,
	       arg_len < sizeof(info) ? arg_len : sizeof(info));

	elog_add(code, ELOG_LEVEL_WARN, info, sizeof(info));
}

void gsm_elog_modem_error(elog_code_t code, const char *info,
                         uint8_t info_len)
{
	elog_add(code, ELOG_LEVEL_ERROR, info, info_len);
}

