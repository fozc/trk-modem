/*
 * board_status.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */
#include "system_status.h"
#include "bsp.h"
#include "gsm_info.h"

static system_status_t g_system_status = {0};


const system_status_t* system_status_get(void)
{
	system_status_update();
    return &g_system_status;
}

void system_status_init(void)
{
	g_system_status.tdie_temp_min = -128;
	g_system_status.tdie_temp_max = +127;
}

void system_status_update_tdie()
{
	uint16_t get_tdie(void);

	g_system_status.tdie_temp = get_tdie();
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
    g_system_status.gsm_rat = gsm_info_get_access_technology();

    system_status_update_tdie();

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



