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

static system_status_t g_system_status = {0};


const system_status_t* system_status_get(void)
{
	system_status_update();
    return &g_system_status;
}

void system_status_init(void)
{
	/* Seed sentinels so the first real sample initialises both bounds:
	 * min starts at the highest value, max at the lowest. */
	g_system_status.tdie_temp_min = +127;
	g_system_status.tdie_temp_max = -128;
	g_system_status.tdie_seeded   = 0U;
}

void system_status_update_tdie()
{
	g_system_status.tdie_temp = (int8_t)adc_get_mcu_temp_c();

	if(g_system_status.tdie_seeded == 0U){
		/* First valid sample seeds both bounds. */
		g_system_status.tdie_temp_min = g_system_status.tdie_temp;
		g_system_status.tdie_temp_max = g_system_status.tdie_temp;
		g_system_status.tdie_seeded   = 1U;
	}
	if(g_system_status.tdie_temp_min > g_system_status.tdie_temp){
		g_system_status.tdie_temp_min = g_system_status.tdie_temp;
	}
	if(g_system_status.tdie_temp_max < g_system_status.tdie_temp){
		g_system_status.tdie_temp_max = g_system_status.tdie_temp;
	}
}

void system_status_update(void)
{
	g_system_status.gsm_signal = gsm_info_get_signal_quality();
    g_system_status.gsm_rat = get_network_generation();

    system_status_update_tdie();

    /* Debounced digital inputs (active-LOW → logical 1). */
    g_system_status.din[0] = digital_input_get(DIN_CH_1);
    g_system_status.din[1] = digital_input_get(DIN_CH_2);
    g_system_status.din[2] = digital_input_get(DIN_CH_3);
    g_system_status.din[3] = digital_input_get(DIN_CH_4);

    /* Debounced DIP switches (active-LOW: ON = 1). */
    g_system_status.dip_sw[0] = dip_switch_get(DIP_SW_1);
    g_system_status.dip_sw[1] = dip_switch_get(DIP_SW_2);

    /* Supply voltages (mV) from the ADC module. */
    g_system_status.v3v3 = adc_get_voltage_mv(ADC_CH_3V3);
    g_system_status.v3v8 = adc_get_voltage_mv(ADC_CH_3V8);
    g_system_status.v5v  = adc_get_voltage_mv(ADC_CH_5V);

    //TODO: Batarya akımı sensöründen okuyun (ADC)
    g_system_status.battery_current = 0;  // + şarj, - deşarj
    
    //TODO: Batarya sıcaklık sensöründen okuyun
    g_system_status.battery_temp = 25;  // Varsayılan değer
    
    //TODO: BMS (Battery Management System) varsa oradan okuyun
    g_system_status.battery_soc = g_system_status.charge_percent;  // Mevcut charge_percent
    g_system_status.battery_soh = 100;  // Varsayılan %100 sağlıklı
    
    //TODO: Ortam sıcaklık sensöründen okuyun (varsa)
    g_system_status.ambient_temp = g_system_status.temp;  // Şimdilik mevcut temp
    
    //TODO: Isıtıcı kontrol GPIO'sundan okuyun
    g_system_status.heater_state = 0;  // GPIO_ReadPin(HEATER_CTRL_GPIO_Port, HEATER_CTRL_Pin);
    
    //TODO: Isıtıcı PWM duty cycle'dan güç hesaplayın
    g_system_status.heater_power = 0;  // PWM_DutyCycle * MAX_HEATER_POWER / 100;
}



