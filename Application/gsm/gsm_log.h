/*
 * gsm_log.h
 *
 *  Created on: Jan 10, 2026
 *      Author: fatih
 */

#ifndef GSM_GSM_LOG_H_
#define GSM_GSM_LOG_H_

#include <stdint.h>
#include "elog_codes.h"
#include "console_logger_config.h"

/* ── Flash event logging (elog) ─────────────────────────────────── */

void gsm_log_modem_event(elog_code_t code);
void gsm_log_modem_event_with_arg(elog_code_t code, const void *arg,
                                  uint8_t arg_len);
void gsm_log_modem_error(elog_code_t code, const char *info,
                         uint8_t info_len);

/* ── Console log level control ──────────────────────────────────── */

typedef enum
{
    GSM_LOG_OFF     = 0,   /**< No GSM console output              */
    GSM_LOG_NORMAL  = 1,   /**< Warnings and errors only            */
    GSM_LOG_VERBOSE = 2    /**< Full trace: AT traffic, state, info */
} gsm_log_level_t;

/** @brief Initialise GSM log level from NVRAM (call after nvram_init). */
void gsm_log_init(void);

/** @brief Set console log level (persisted to NVRAM). */
void gsm_log_set_level(gsm_log_level_t level);

/** @brief Get current console log level. */
gsm_log_level_t gsm_log_get_level(void);

/* ── Level-gated console log macros ─────────────────────────────── */

/** @brief Error — printed at >= NORMAL (red, with timestamp). */
#define GSM_LOG_ERR(...)                                              \
    do {                                                              \
        if (gsm_log_get_level() >= GSM_LOG_NORMAL)                    \
        {                                                             \
            CCSLOG(XCOLOR_RED, __VA_ARGS__);                          \
        }                                                             \
    } while (0)

/** @brief Warning — printed at >= NORMAL (yellow, with timestamp). */
#define GSM_LOG_WRN(...)                                              \
    do {                                                              \
        if (gsm_log_get_level() >= GSM_LOG_NORMAL)                    \
        {                                                             \
            CCSLOG(XCOLOR_YELLOW, __VA_ARGS__);                       \
        }                                                             \
    } while (0)

/** @brief Info — printed at VERBOSE only (default color, timestamp). */
#define GSM_LOG_INF(...)                                              \
    do {                                                              \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                   \
        {                                                             \
            CSLOG(__VA_ARGS__);                                       \
        }                                                             \
    } while (0)

/** @brief Colored info — printed at VERBOSE only. */
#define GSM_LOG_INF_C(color, ...)                                     \
    do {                                                              \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                   \
        {                                                             \
            CCSLOG(color, __VA_ARGS__);                               \
        }                                                             \
    } while (0)

/** @brief Continuation (no date/time) — printed at VERBOSE only. */
#define GSM_LOG_NODT(...)                                             \
    do {                                                              \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                   \
        {                                                             \
            CSLOG_NODT(__VA_ARGS__);                                  \
        }                                                             \
    } while (0)

#endif /* GSM_GSM_LOG_H_ */
