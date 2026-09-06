/*
 * gsm_log.h
 *
 *  Created on: Jan 10, 2026
 *      Author: fatih
 */

#ifndef GSM_GSM_LOG_H_
#define GSM_GSM_LOG_H_

#include <stdint.h>
#include "console_logger_config.h"

/* ── Console log level control ──────────────────────────────────── */

typedef enum
{
    GSM_LOG_OFF     = 0,   /**< No GSM console output              */
    GSM_LOG_NORMAL  = 1,   /**< Warnings and errors only            */
    GSM_LOG_VERBOSE = 2    /**< Full trace: AT traffic, state, info */
} gsm_log_level_t;

/** @brief Set console log level (persisted via the central console_logger table). */
void gsm_log_set_level(gsm_log_level_t level);

/** @brief Get current console log level. */
gsm_log_level_t gsm_log_get_level(void);

/*
 * GSM_LOG_* makro ailesi kaldirildi: gsm dosyalari artik dosya
 * basinda #define CSLOG_MODULE LOG_MOD_GSM ile CSLOG ailesini
 * kullanir (seviye eslemesi console_logger_config.h'te).
 */

#endif /* GSM_GSM_LOG_H_ */
