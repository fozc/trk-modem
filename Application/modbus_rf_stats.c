/*
 * modbus_rf_stats.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Evaluate RF validity using the caller's FC03 time snapshot.
 */
#include "modbus_rf_stats.h"
#include "modbus_config.h"
#include "rf.h"
#include <stddef.h>

bool modbus_rf_stats_read(uint16_t reg_addr, uint32_t now_ms, uint16_t *value)
{
    if ((NULL == value) || (MODBUS_RF_STATS_ADDR_BASE > reg_addr) ||
        (MODBUS_RF_STATS_ADDR_LAST < reg_addr))
    {
        return false;
    }
    const size_t offset = (size_t)reg_addr - MODBUS_RF_STATS_ADDR_BASE;
    const size_t line = offset / 3U;
    const phase_id_t phase = (phase_id_t)(offset % 3U);
    rf_phase_data_t data;

    *value = 0U;
    if (modbus_is_line_in_use((uint32_t)line) &&
        rf_get_phase_data(line, phase, now_ms, &data) &&
        data.has_live && data.is_online)
    {
        *value = MODBUS_RF_ENERGY_VALID | MODBUS_RF_LOAD_VALID;
        if (data.current_valid)
        {
            *value |= MODBUS_RF_CURRENT_VALID;
        }
    }
    return true;
}

/*** end of file ***/
