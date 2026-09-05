/*
 * rf_log.c
 *
 *  Created on: Sep 05, 2026
 *      Author: fatih
 *
 * RF log seviyesi durumu - gsm_log ile ayni model, NVRAM destekli.
 */

#include "rf_log.h"
#include "nvram.h"

/* -- RF log seviyesi --------------------------------------------- */

static rf_log_level_t level = RF_LOG_NORMAL;

void rf_log_init(void)
{
    uint8_t stored = nvram_get_rf_log_level();

    if (stored > (uint8_t)RF_LOG_VERBOSE)
    {
        stored = (uint8_t)RF_LOG_VERBOSE;
    }

    level = (rf_log_level_t)stored;
}

void rf_log_set_level(rf_log_level_t new_level)
{
    if (new_level > RF_LOG_VERBOSE)
    {
        new_level = RF_LOG_VERBOSE;
    }

    level = new_level;
    nvram_set_rf_log_level((uint8_t)new_level);
    nvram_sync(false);
}

rf_log_level_t rf_log_get_level(void)
{
    return level;
}

/*** end of file ***/
