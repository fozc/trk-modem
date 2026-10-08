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
    /* R1 5.2 ek alanlari: geccerlilik power_valid_fields ile tasınır. */
    uint16_t dc_voltage;        // DC giris gerilimi (mV)
    int16_t input_current;      // giris akimi (mA)
    int16_t input_power_10mw;   // giris gucu (10 mW birim)
    int16_t battery_power_10mw; // aku gucu (10 mW, + sarj)
    uint8_t power_source;       // 0 aku/1 PV/2 DC/3 belirsiz/4 giris var/255 bilinmiyor
    uint8_t telemetry_age;      // son gecerli telemetriden gecen sure (s)
    uint8_t board_flags;        // 0xE1 durum bayraklari (R1 5.2)
    uint8_t board_flags2;       // 0xE1 durum2 bayraklari (R1 5.2)
    uint32_t alarm_mask;        // etkin seviye alarmlarinin bit maskesi
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
