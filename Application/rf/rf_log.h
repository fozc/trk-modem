/*
 * rf_log.h
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * RF alt sisteminin seviyeli konsol log kontrolu - OFF / NORMAL
 * (yalniz hata+uyari) / VERBOSE (tam iz + ham paket dokumu). Seviye
 * NVRAM'de kalici; "rf log" shell komutu ile degistirilir.
 *
 * Durum ve NVRAM eslemesi console_logger'in merkez tablosunda
 * yasrar. RF_LOG_* makro ailesi kaldirildi: rf dosyalari dosya
 * basinda #define CSLOG_MODULE LOG_MOD_RF tanimlayip CSLOG ailesini
 * kullanir (seviye eslemesi console_logger_config.h'te). Bu baslik
 * yalnizca "rf log" komutunun kullandigi seviye API'sini tasir;
 * sabitler log_lvl_t ile birebir ortusur (0/1/2).
 */

#ifndef RF_RF_LOG_H_
#define RF_RF_LOG_H_

#include <stdint.h>
#include "console_logger_config.h"

/* -- RF log seviye API'si (rf shell komutu kullanir) --------------- */

typedef enum
{
    RF_LOG_OFF     = 0,   /**< RF konsol cikisi yok                  */
    RF_LOG_NORMAL  = 1,   /**< Yalniz hata ve uyari                  */
    RF_LOG_VERBOSE = 2    /**< Tam iz + ham paket dokumu (hex)       */
} rf_log_level_t;

/** @brief RF log seviyesini ayarla (merkez tablo uzerinden NVRAM'e yazilir). */
void rf_log_set_level(rf_log_level_t level);

/** @brief Gecerli RF log seviyesini dondur. */
rf_log_level_t rf_log_get_level(void);

#endif /* RF_RF_LOG_H_ */
