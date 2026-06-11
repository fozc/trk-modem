/*
 * board_status.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */

#ifndef SYSTEM_STATUS_H_
#define SYSTEM_STATUS_H_

#include <stdint.h>


typedef struct
{
    uint8_t din[4];  // 4 adet dijital giris durumu
    uint8_t rly[2];  // 2 adet role cikis durumu
    uint16_t vbat;   // Batarya Voltaji (mV)
    uint16_t v19;    // 19V Besleme Voltaji (mV)
    uint16_t v3v3;   // 3.3V Besleme Voltaji (mV)
    uint16_t v3v8;   // 3.8V Besleme Voltaji (mV)
    uint16_t v5v;    // 5V Besleme Voltaji (mV)
    uint8_t charge_state; // Batarya Sarj Durumu
    int8_t temp;      // Sicaklik Degeri (°C)
    int8_t temp_max;   // Maksimum Sicaklik Degeri (°C)
    int8_t temp_min;   // Minimum Sicaklik Degeri (°C)
    int16_t panel_current;  // Panel Akimi (mA)
    uint16_t panel_voltage;  // Panel Voltaji (mV)
    uint16_t battery_voltage;  // Batarya Voltaji (mV)
    int8_t tdie_temp;       // MCU Sicaklik Degeri (°C)
    int8_t tdie_temp_max;   // MCU Sicaklik Maks Degeri (°C)
    int8_t tdie_temp_min;   // MCU Sicaklik Min Degeri (°C)
    uint8_t tdie_seeded;    // MCU sicaklik min/max ilk ornek ile seed edildi mi
    uint8_t charge_percent; // Batarya Sarj Orani (%)
    uint16_t capacity;      // Batarya Kapasitesi (mAh)
    int8_t gsm_signal;       // GSM Sinyal Gucü (dBm)
    uint8_t gsm_rat;         // GSM RAT
    int16_t battery_current; // Batarya Akimi (mA)
    int8_t battery_temp;     // Batarya Sicakligi (°C)
    uint8_t battery_soc;     // Batarya SOC (%)
    uint8_t battery_soh;     // Batarya SOH (%)
    int8_t ambient_temp;     // Ortam Sicakligi (°C)
    uint8_t heater_state;    // Isitici Durumu
    uint16_t heater_power;   // Isitici Gucu (mW)
} system_status_t;

const system_status_t* system_status_get(void);
void system_status_update(void);

#endif /* SYSTEM_STATUS_H_ */
