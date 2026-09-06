/*
 * rf_log.c
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * RF log seviyesi - ince delegasyon katmani: durum ve NVRAM eslemesi
 * console_logger'in merkez tablosunda yasrar. Fonksiyon API'si korunur
 * (rf_shell); seviye sabitleri log_lvl_t ile birebir ortusur (0/1/2).
 */

#include "rf_log.h"
#include "console_logger.h"

void rf_log_set_level(rf_log_level_t level)
{
    console_logger_set_level(LOG_MOD_RF, (log_lvl_t)level);
}

rf_log_level_t rf_log_get_level(void)
{
    return (rf_log_level_t)console_logger_get_level(LOG_MOD_RF);
}

/*** end of file ***/
