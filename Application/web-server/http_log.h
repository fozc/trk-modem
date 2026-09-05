/*
 * http_log.h
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * HTTP alt sisteminin seviyeli konsol log makrolari. Seviye anahtari
 * gsm_log seviyesidir (konsol verbosity'unun ana denetimi):
 *   OFF     - hicbir HTTP cikti
 *   NORMAL  - yalniz hata ve uyari
 *   VERBOSE - tum cikti
 *
 * "gsm log <off|on|verbose>" komutu hem GSM hem HTTP ciktisini
 * birlikte kontrol eder.
 */

#ifndef WEB_SERVER_HTTP_LOG_H_
#define WEB_SERVER_HTTP_LOG_H_

#include "console_logger.h"
#include "gsm_log.h"

/* -- Seviyeli HTTP log makrolari (gsm_log ile ayni desen) --------- */

/** @brief Hata - >= NORMAL basilir (kirmizi, tarih saatli). */
#define HTTP_LOG_ERR(...)                                                \
    do {                                                                 \
        if (gsm_log_get_level() >= GSM_LOG_NORMAL)                       \
        {                                                                \
            CSLOG_ERR(__VA_ARGS__);                                      \
        }                                                                \
    } while (0)

/** @brief Uyari - >= NORMAL basilir (sari, tarih saatli). */
#define HTTP_LOG_WRN(...)                                                \
    do {                                                                 \
        if (gsm_log_get_level() >= GSM_LOG_NORMAL)                       \
        {                                                                \
            CSLOG_WARN(__VA_ARGS__);                                     \
        }                                                                \
    } while (0)

/** @brief Bilgi - yalniz VERBOSE'ta basilir (tarih saatli). */
#define HTTP_LOG_INF(...)                                                \
    do {                                                                 \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                      \
        {                                                                \
            CSLOG(__VA_ARGS__);                                          \
        }                                                                \
    } while (0)

/** @brief Renkli bilgi (yesil/mavi banner vb.) - yalniz VERBOSE'ta. */
#define HTTP_LOG_INF_C(color, ...)                                       \
    do {                                                                 \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                      \
        {                                                                \
            CCSLOG(color, __VA_ARGS__);                                  \
        }                                                                \
    } while (0)

/** @brief Devam satiri (tarih saat YOK) - yalniz VERBOSE'ta. */
#define HTTP_LOG_NODT(...)                                               \
    do {                                                                 \
        if (gsm_log_get_level() >= GSM_LOG_VERBOSE)                      \
        {                                                                \
            CSLOG_NODT(__VA_ARGS__);                                     \
        }                                                                \
    } while (0)

#endif /* WEB_SERVER_HTTP_LOG_H_ */
