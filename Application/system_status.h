/*
 * system_status.h
 *
 *  Created on: Feb 1, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef SYSTEM_STATUS_H_
#define SYSTEM_STATUS_H_

#include <stdint.h>
#include <stdbool.h>


typedef struct
{
    uint8_t din[4];  // 4 adet dijital giris durumu
    uint8_t dip_sw[2]; // 2 adet DIP switch durumu
    uint8_t rly[2];  // 2 adet role cikis durumu
    uint16_t v19;    // 19V Besleme Voltaji (mV)
    uint16_t v3v3;   // 3.3V Besleme Voltaji (mV)
    uint16_t v3v8;   // 3.8V Besleme Voltaji (mV)
    uint16_t v5v;    // 5V Besleme Voltaji (mV)
    int16_t panel_current;  // Panel Akimi (mA)
    uint16_t panel_voltage;  // Panel Voltaji (mV)
    int16_t battery_current; // Batarya Akimi (mA)
    uint16_t battery_voltage;  // Batarya Voltaji (mV)
    uint16_t battery_capacity;      // Batarya Kapasitesi (Ah)
    /* x10 alanlari SCP'den donusturulerek (raw) tutulur: 995 = %99.5,
     * 245 = 24.5C. Donusum yalniz gosterim noktasinda yapilir. */
    int16_t battery_charge_x10; // Batarya Sarj Orani (x10 %, isaretli)
    int16_t battery_temp_x10;   // x10 C; validity in power_valid_fields.
    int16_t battery_soc_x10;    // Batarya SOC (x10 %, isaretli)
    uint16_t battery_soh_x10;   // Batarya SOH (x10 %)
    uint8_t charge_state; // Batarya Sarj Durumu
    int8_t gsm_signal;       // GSM Sinyal Gucu (dBm)
    uint8_t gsm_rat;         // GSM RAT
    int8_t ambient_temp;     // Ortam Sicakligi (degC)
    int8_t temp;      // Sicaklik Degeri (degC)
    int8_t temp_max;   // Maksimum Sicaklik Degeri (degC)
    int8_t temp_min;   // Minimum Sicaklik Degeri (degC)
    int8_t tdie_temp;       // MCU Sicaklik Degeri (degC)
    int8_t tdie_temp_min;   // MCU Sicaklik Min Degeri (degC)
    int8_t tdie_temp_max;   // MCU Sicaklik Maks Degeri (degC)
    uint8_t heater_state;    // Isitici Durumu
    uint16_t heater_power;   // Isitici Gucu (mW)
    uint16_t power_valid_fields;
    bool board_temp_history_valid;
    uint8_t charge_verdict;
    bool battery_capacity_unknown;
} system_status_t;

const system_status_t* system_status_get(void);
void system_status_update(void);
void system_status_init(void);
#endif /* SYSTEM_STATUS_H_ */

/*** end of file ***/
