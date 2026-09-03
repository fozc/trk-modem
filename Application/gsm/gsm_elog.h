/*
 * gsm_elog.h
 *
 *  Created on: 3 Eyl 2026
 *      Author: fatih
 */

#ifndef GSM_GSM_ELOG_H_
#define GSM_GSM_ELOG_H_

#include "elog_codes.h"

/* ── Flash event logging (elog) ─────────────────────────────────── */

void gsm_elog_modem_event(elog_code_t code);
void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg,
                                  uint8_t arg_len);
void gsm_elog_modem_error(elog_code_t code, const char *info,
                         uint8_t info_len);

#endif /* GSM_GSM_ELOG_H_ */
