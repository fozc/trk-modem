/*
 * system_status.c
 *
 *  Created on: Feb 1, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "system_status.h"
#include "bsp.h"
#include "gsm_info.h"
#include "adc.h"
#include "digital_input.h"
#include "power_board_scp.h"
#include "stm32u3xx_hal.h"
#include "relay.h"

static system_status_t system_status = {0};


const system_status_t* system_status_get(void)
{
	system_status_update();
    return &system_status;
}

void system_status_init(void)
{
    system_status = (system_status_t){0};
	system_status.tdie_temp_min = INT8_MAX;
	system_status.tdie_temp_max = INT8_MIN;
	system_status.temp_min = INT8_MAX;
	system_status.temp_max = INT8_MIN;
}

void system_status_update_tdie()
{
	system_status.tdie_temp = (int8_t)adc_get_mcu_temp_c();

	if (system_status.tdie_temp < system_status.tdie_temp_min)
	{
		system_status.tdie_temp_min = system_status.tdie_temp;
	}

	if (system_status.tdie_temp > system_status.tdie_temp_max)
	{
		system_status.tdie_temp_max = system_status.tdie_temp;
	}
}

void system_status_update(void)
{
    power_board_snapshot_t power;

    (void)power_board_get_snapshot(HAL_GetTick(), &power);
    const rf_scp_power_summary_t *telemetry = &power.summary;
    system_status.power_valid_fields = power.valid_fields;
    /* BQ-12: default 7 Ah, unset capacity and verdict 3 need maintenance. */
    system_status.battery_capacity_unknown =
        (0U != (power.valid_fields & POWER_VALID_SUMMARY)) &&
        (7U == telemetry->capacity_ah) &&
        (0U == (telemetry->flags2 & 0x0CU)) &&
        (0x30U == (telemetry->flags2 & 0x70U));


    /* Debounced digital inputs (active-LOW -> logical 1). */
    system_status.din[0] = digital_input_get(DIN_CH_1);
    system_status.din[1] = digital_input_get(DIN_CH_2);
    system_status.din[2] = digital_input_get(DIN_CH_3);
    system_status.din[3] = digital_input_get(DIN_CH_4);

    /* Debounced DIP switches (active-LOW: ON = 1). */
    system_status.dip_sw[0] = dip_switch_get(DIP_SW_1);
    system_status.dip_sw[1] = dip_switch_get(DIP_SW_2);

    system_status.rly[0] = relay_is_on(RELAY_CH_1);
    system_status.rly[1] = relay_is_on(RELAY_CH_2);

    /* Supply voltages (mV) from the ADC module (no V19 channel exists). */
    system_status.v3v3 = adc_get_voltage_mv(ADC_CH_3V3);
    system_status.v3v8 = adc_get_voltage_mv(ADC_CH_3V8);
    system_status.v5v  = adc_get_voltage_mv(ADC_CH_5V);

    /* PB'de dogrudan PV akimi alani yok; panel akimi bilinmiyor (0). */
    system_status.panel_current = 0;
    system_status.panel_voltage = telemetry->pv_mv;  // mV
    system_status.dc_voltage = telemetry->dc_mv;     // mV
    system_status.input_current = telemetry->input_ma;   // mA
    system_status.input_power_10mw = telemetry->input_power_10mw;
    system_status.battery_power_10mw = telemetry->battery_power_10mw;
    system_status.power_source = telemetry->source;
    system_status.telemetry_age = power.telemetry_age_sec;
    system_status.board_flags = telemetry->flags;
    system_status.board_flags2 = telemetry->flags2;
    system_status.alarm_mask = power.active_alarms;
    system_status.battery_voltage = telemetry->battery_mv;  // mV
    system_status.battery_current = telemetry->battery_ma;  // mA
    system_status.battery_capacity        = telemetry->capacity_ah;  // Ah
    /* x10 alanlari ham (raw) tutulur; donusum gosterimde yapilir. */
    system_status.battery_charge_x10  = telemetry->soc_tenths;
    system_status.battery_temp_x10    = (int16_t)((int16_t)telemetry->battery_temperature * 10);
    system_status.battery_soc_x10     = telemetry->soc_tenths;
    system_status.battery_soh_x10     = (uint16_t)((uint16_t)telemetry->soh_percent * 10U);
    system_status.charge_state    = telemetry->charge_phase;
    system_status.charge_verdict = (telemetry->flags >> 6U) & 3U;  // R1 charge verdict: unknown/no/yes/conflict.

    system_status.gsm_signal =
        (int8_t)gsm_info_get_signal_quality();
    system_status.gsm_rat = get_network_generation();


    system_status.ambient_temp = 0;

    /* Board temperature from the PB telemetry; -128 = sensor error. */
    if (0U != (power.valid_fields & POWER_VALID_BOARD_TEMP))
    {
        system_status.temp = telemetry->board_temperature;
        system_status.board_temp_history_valid = true;
        if (system_status.temp < system_status.temp_min)
        {
            system_status.temp_min = system_status.temp;
        }
        if (system_status.temp > system_status.temp_max)
        {
            system_status.temp_max = system_status.temp;
        }
    }
    system_status_update_tdie();

    /* The SCP summary does not contain heater measurements. */
    system_status.heater_state = 0U;
    system_status.heater_power = 0U;
}

/*** end of file ***/
