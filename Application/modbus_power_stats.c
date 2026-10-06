/*
 * modbus_power_stats.c
 *
 *  Created on: Jul 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SCP power summary registers, with explicit per-field quality.
 */

#include "modbus_power_stats.h"
#include "power_board_scp.h"
#include "stm32u3xx_hal.h"

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t *value)
{
    power_board_snapshot_t power;

    if ((NULL == value) || (MODBUS_PWR_STATS_ADDR_BASE > reg_addr) ||
        (MODBUS_PWR_STATS_ADDR_BASE + MODBUS_PWR_STATS_REG_COUNT <= reg_addr))
    {
        return false;
    }
    (void)power_board_get_snapshot(HAL_GetTick(), &power);
    const rf_scp_power_summary_t *data = &power.summary;
    const uint16_t registers[MODBUS_PWR_STATS_REG_COUNT] =
    {
        power.has_summary ? 1U : 0U, power.valid_fields, data->seq,
        data->flags, data->flags2, power.telemetry_age_sec,
        data->source, data->charge_phase, data->session,
        data->pv_mv, data->dc_mv, (uint16_t)data->input_ma,
        data->system_mv, data->battery_mv, (uint16_t)data->battery_ma,
        (uint16_t)data->input_power_10mw,
        (uint16_t)data->battery_power_10mw,
        (uint16_t)data->system_power_10mw, (uint16_t)data->soc_tenths,
        data->soh_percent, (uint16_t)(int16_t)data->battery_temperature,
        (uint16_t)(int16_t)data->board_temperature, data->capacity_ah,
        data->charge_rate_permille, data->last_gasp_count,
        (uint16_t)(power.active_alarms >> 16U),
        (uint16_t)(power.active_alarms & 0xFFFFU), data->soc_flags,
        power.alarm_seq, power.suppressed,
        power.has_last_gasp ? 1U : 0U, power.last_gasp_cancelled ? 1U : 0U,
        power.last_gasp_session, power.last_gasp_cause,
        (uint16_t)(power.received_age_ms >> 16U),
        (uint16_t)(power.received_age_ms & 0xFFFFU), 1U, 1U
    };

    *value = registers[(size_t)reg_addr - MODBUS_PWR_STATS_ADDR_BASE];
    return true;
}

/*** end of file ***/
