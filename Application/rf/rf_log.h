/*
 * rf_log.h
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * RF alt sisteminin seviyeli konsol log kontrolu - gsm_log ile ayni
 * model: OFF / NORMAL (yalniz hata+uyari) / VERBOSE (tam iz + ham
 * paket dokumu). Seviye NVRAM'de kalici; "rf log" shell komutu ile
 * degistirilir.
 */

/* TODO (merkezilestirme): ucuncu kopya (gsm_log.h, rf_log.h,
 * http_log.h) ayni seviye makro ailesini tasiyor. Dorduncu alt sistem
 * seviye istediginde (modbus / iec104 adaylari) tek tabloya gecelim:
 * Application/cslog/mod_log.c/h - log_mod_t enum (GSM/RF/HTTP/...),
 * uint8_t levels[LOG_MOD_COUNT], tek MOD_LOG_ERR/WRN/INF/NODT filtresi;
 * alt sistem basliklari tek satirlik delegasyona iner
 * (#define RF_LOG_ERR(...) MOD_LOG_ERR(LOG_MOD_RF, __VA_ARGS__)) -
 * cagri noktalari ve gsm_log fonksiyon API'si (at_engine2 kullaniyor)
 * degismez. Tek shell komutu: "log <mod> <off|on|verbose>", "log"
 * (liste), "log all <seviye>". HTTP'in gsm seviyesini takip etmesi
 * tabloda acik bir tercih haline gelir (kendi NVRAM alanini alabilir).
 * NVRAM eslemesi nvram_get/set_*_log_level uzerinden kalir. */

#ifndef RF_RF_LOG_H_
#define RF_RF_LOG_H_

#include <stdint.h>
#include "console_logger_config.h"

/* -- RF log seviye kontrolu -------------------------------------- */

typedef enum
{
    RF_LOG_OFF     = 0,   /**< RF konsol cikisi yok                  */
    RF_LOG_NORMAL  = 1,   /**< Yalniz hata ve uyari                  */
    RF_LOG_VERBOSE = 2    /**< Tam iz + ham paket dokumu (hex)       */
} rf_log_level_t;

/** @brief RF log seviyesini NVRAM'den yukle (nvram yuklemesinden sonra). */
void rf_log_init(void);

/** @brief RF log seviyesini ayarla (NVRAM'e yazilir). */
void rf_log_set_level(rf_log_level_t level);

/** @brief Gecerli RF log seviyesini dondur. */
rf_log_level_t rf_log_get_level(void);

/* -- Seviyeli log makrolari (gsm_log ile ayni desen) ------------- */

/** @brief Hata - >= NORMAL basilir (kirmizi, tarih saatli). */
#define RF_LOG_ERR(...)                                                  \
    do {                                                                 \
        if (rf_log_get_level() >= RF_LOG_NORMAL)                         \
        {                                                                \
            CSLOG_ERR(__VA_ARGS__);                                      \
        }                                                                \
    } while (0)

/** @brief Uyari - >= NORMAL basilir (sari, tarih saatli). */
#define RF_LOG_WRN(...)                                                  \
    do {                                                                 \
        if (rf_log_get_level() >= RF_LOG_NORMAL)                         \
        {                                                                \
            CSLOG_WARN(__VA_ARGS__);                                     \
        }                                                                \
    } while (0)

/** @brief Bilgi - yalniz VERBOSE'ta basilir (tarih saatli). */
#define RF_LOG_INF(...)                                                  \
    do {                                                                 \
        if (rf_log_get_level() >= RF_LOG_VERBOSE)                        \
        {                                                                \
            CSLOG(__VA_ARGS__);                                          \
        }                                                                \
    } while (0)

/** @brief Devam satiri (tarih saat YOK) - yalniz VERBOSE'ta. Ham
 *         paket hex dokumu gibi parcali ciktilar icin. */
#define RF_LOG_NODT(...)                                                 \
    do {                                                                 \
        if (rf_log_get_level() >= RF_LOG_VERBOSE)                        \
        {                                                                \
            CSLOG_NODT(__VA_ARGS__);                                     \
        }                                                                \
    } while (0)

#endif /* RF_RF_LOG_H_ */
