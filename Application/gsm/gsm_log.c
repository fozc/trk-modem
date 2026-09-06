/*
 * gsm_log.c
 *
 *  Created on: Jan 10, 2026
 *      Author: fatih
 *
 * GSM log seviyesi - ince delegasyon katmani: durum ve NVRAM eslemesi
 * console_logger'in merkez tablosunda yasrar. Fonksiyon API'si
 * korunur (at_engine2 kontrol akisi, gsm_shell); seviye sabitleri
 * log_lvl_t ile birebir ortusur (0/1/2).
 */

#include "gsm_log.h"
#include "console_logger.h"

void gsm_log_set_level(gsm_log_level_t level)
{
    console_logger_set_level(LOG_MOD_GSM, (log_lvl_t)level);
}

gsm_log_level_t gsm_log_get_level(void)
{
    return (gsm_log_level_t)console_logger_get_level(LOG_MOD_GSM);
}

/*** end of file ***/
