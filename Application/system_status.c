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

    /* Supply voltages (mV) from the ADC module. */
    g_system_status.v19  = 19;
    g_system_status.v3v3 = adc_get_voltage_mv(ADC_CH_3V3);
    g_system_status.v3v8 = adc_get_voltage_mv(ADC_CH_3V8);
    g_system_status.v5v  = adc_get_voltage_mv(ADC_CH_5V);

    g_system_status.panel_current = 0;
    g_system_status.panel_voltage = 0;
    g_system_status.battery_voltage = telemetry.vbat_mv;  // mV
    g_system_status.battery_current = telemetry.ibat_ma;  // mA
    g_system_status.battery_capacity        = telemetry.batt_cap_ah;  // Ah
    g_system_status.battery_charge_percent  = telemetry.soc_x10 / 10;  // %
    g_system_status.battery_temp    = telemetry.batt_temp_x10 / 10;  // °C
    g_system_status.battery_soc     = telemetry.soc_x10 / 10;  // %
    g_system_status.battery_soh     = telemetry.soh_x10 / 10;  // %
    g_system_status.battery_temp    = telemetry.batt_temp_x10 / 10;  // °C
    g_system_status.charge_state    = telemetry.chg_stat;  // 0: Idle, 1: Charging, 2: Discharging

    g_system_status.gsm_signal = gsm_info_get_signal_quality();
    g_system_status.gsm_rat = get_network_generation();


    g_system_status.ambient_temp = 0;
    g_system_status.temp = 0;
    g_system_status.temp_max = 0;
    g_system_status.temp_min = 0;
    system_status_update_tdie();

    //TODO: Isıtıcı kontrol GPIO'sundan okuyun
    g_system_status.heater_state = 0;  // GPIO_ReadPin(HEATER_CTRL_GPIO_Port, HEATER_CTRL_Pin);
    //TODO: Isıtıcı PWM duty cycle'dan güç hesaplayın
    g_system_status.heater_power = 0;  // PWM_DutyCycle * MAX_HEATER_POWER / 100;
}



