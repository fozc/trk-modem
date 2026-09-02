/*
 * board_status.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */
#include "system_status.h"
#include "bsp.h"
#include "gsm_info.h"
#include "adc.h"
#include "digital_input.h"
#include "power_board.h"
#include "relay.h"

static system_status_t g_system_status = {0};


const system_status_t* system_status_get(void)
{
	system_status_update();
    return &g_system_status;
}

void system_status_init(void)
{
	g_system_status.tdie_temp_min = INT8_MAX;
	g_system_status.tdie_temp_max = INT8_MIN;
	g_system_status.temp_min = INT8_MAX;
	g_system_status.temp_max = INT8_MIN;
}

void system_status_update_tdie()
{
	g_system_status.tdie_temp = (int8_t)adc_get_mcu_temp_c();

	if (g_system_status.tdie_temp < g_system_status.tdie_temp_min)
	{
		g_system_status.tdie_temp_min = g_system_status.tdie_temp;
	}

	if (g_system_status.tdie_temp > g_system_status.tdie_temp_max)
	{
		g_system_status.tdie_temp_max = g_system_status.tdie_temp;
	}
}

void system_status_update(void)
{
	power_board_telemetry_t telemetry = {0};

    power_board_get_telemetry(&telemetry);


    /* Debounced digital inputs (active-LOW → logical 1). */
    g_system_status.din[0] = digital_input_get(DIN_CH_1);
    g_system_status.din[1] = digital_input_get(DIN_CH_2);
    g_system_status.din[2] = digital_input_get(DIN_CH_3);
    g_system_status.din[3] = digital_input_get(DIN_CH_4);

    /* Debounced DIP switches (active-LOW: ON = 1). */
    g_system_status.dip_sw[0] = dip_switch_get(DIP_SW_1);
    g_system_status.dip_sw[1] = dip_switch_get(DIP_SW_2);

    g_system_status.rly[0] = relay_is_on(RELAY_CH_1);
    g_system_status.rly[1] = relay_is_on(RELAY_CH_2);

    /* Supply voltages (mV) from the ADC module (no V19 channel exists). */
    g_system_status.v3v3 = adc_get_voltage_mv(ADC_CH_3V3);
    g_system_status.v3v8 = adc_get_voltage_mv(ADC_CH_3V8);
    g_system_status.v5v  = adc_get_voltage_mv(ADC_CH_5V);

    /* PB'de dogrudan PV akimi alani yok; panel akimi bilinmiyor (0). */
    g_system_status.panel_current = 0;
    g_system_status.panel_voltage = telemetry.vpv_mv;  // mV
    g_system_status.battery_voltage = telemetry.vbat_mv;  // mV
    g_system_status.battery_current = telemetry.ibat_ma;  // mA
    g_system_status.battery_capacity        = telemetry.batt_cap_ah;  // Ah
    /* x10 alanlari ham (raw) tutulur; donusum gosterimde yapilir. */
    g_system_status.battery_charge_x10  = telemetry.soc_x10;
    g_system_status.battery_temp_x10    = telemetry.batt_temp_x10;
    g_system_status.battery_soc_x10     = telemetry.soc_x10;
    g_system_status.battery_soh_x10     = telemetry.soh_x10;
    g_system_status.charge_state    = telemetry.chg_stat;  // 0: Idle, 1: Charging, 2: Discharging

    g_system_status.gsm_signal = gsm_info_get_signal_quality();
    g_system_status.gsm_rat = get_network_generation();


    g_system_status.ambient_temp = 0;

    /* Board temperature from the PB telemetry; -128 = sensor error. */
    if (telemetry.board_temp_c != POWER_BOARD_BOARD_TEMP_ERROR)
    {
        g_system_status.temp = telemetry.board_temp_c;
        if (g_system_status.temp < g_system_status.temp_min)
        {
            g_system_status.temp_min = g_system_status.temp;
        }
        if (g_system_status.temp > g_system_status.temp_max)
        {
            g_system_status.temp_max = g_system_status.temp;
        }
    }
    system_status_update_tdie();

    /* Heater state from the PB telemetry (0x2A). */
    g_system_status.heater_state = telemetry.heater_state;
    //TODO: Isıtıcı PWM duty cycle'dan güç hesaplayın
    g_system_status.heater_power = 0;  // PWM_DutyCycle * MAX_HEATER_POWER / 100;
}



