/*
 * modem_types.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */

#ifndef MODEM_TYPES_H_
#define MODEM_TYPES_H_

#include "gsm_types.h"

#define MAX_POWER_LINE_COUNT 8
#define MAX_FEEDER_COUNT MAX_POWER_LINE_COUNT

typedef enum
{
	FEEDER_1 = 0,
	FEEDER_2,
	FEEDER_3,
	FEEDER_4,
	FEEDER_5,
	FEEDER_6,
	FEEDER_7,
	FEEDER_8,
	FEEDER_MAX
}feeder_id_t;

typedef enum
{
	POWER_LINE_1 = 0,
	POWER_LINE_2,
	POWER_LINE_3,
	POWER_LINE_4,
	POWER_LINE_5,
	POWER_LINE_6,
	POWER_LINE_7,
	POWER_LINE_8,
	POWER_LINE_MAX
} power_line_id_t;

typedef enum
{
	PHASE_L1 = 0, // Phase R/A/L1
	PHASE_L2 = 1, // Phase S/B/L2
	PHASE_L3 = 2, // Phase T/C/L3
	PHASE_MAX = 3,
    PHASE_ALL = 0xFF
} phase_id_t;

typedef enum
{
	BREAKER_STATE_UNDEFINED = 0,
	BREAKER_STATE_OPEN = 1,
	BREAKER_STATE_CLOSED = 2,
	BREAKER_STATE_UNKNOWN = 3
} breaker_state_enum_t;


#define MAX_APN_LEN 16
#define MAX_APN_USER_NAME_LEN 16
#define MAX_APN_PASSWORD_LEN 16
#define MAX_NTP_SERVER_LEN 64

typedef struct
{
    char mcc[5];
    char mnc[5];
    char lac[5];
    char ci[5];
}__attribute__((packed)) coordinates_t;

typedef struct
{
    uint32_t serial_number;                 // RO Cihaz seri numarasi, 0-99999999
    uint16_t web_interface_port;            // RW Web arayuzu portu,   1-65535
    uint16_t sim_card_pin;                  // RW SIM kart PIN kodu,    0000-9999
    apn_t	 apn;                                // RW SIM kart APN bilgileri (apn, username, password)
    char     ntp_server[MAX_NTP_SERVER_LEN];		 // RW NTP server adresi
    uint16_t ntp_server_port;			      // RW NTP server portu
    uint32_t time;                            // RW	 Anlik modem Epoch zamani
    int32_t  time_zone;                       // RW Saat dilimi offseti saat cinsinden, +/-12
    coordinates_t coordinates;                //RO Cihaz koordinat bilgileri
    uint32_t production_date;                 // RO Uretim tarihi epoch olarak
    uint32_t lifetime;                        // RO Cihazin toplam calisma suresi saniye cinsinden
    uint32_t periodic_modem_reset_period;     // RW Periyodik modem reset periyodu saniye cinsinden
    uint32_t commissioning_time;              // RO Devreye alma zamani (epoch)
    uint8_t  rf_firmware_version[4];          // RO RF firmware versiyonu semantic version
    uint32_t web_session_counter;                 // RO Modem oturum sayaci
    uint32_t iec_session_counter;                 // RO Modem oturum sayaci
	char    imei[16];			/* Modemdeki GSM modulun IMEI numarasi */
	char    phone_num[12];       /* Simkart'a ait tel. no*/
}__attribute__((packed)) modem_config_t;


typedef struct
{
    uint8_t  din[4];         // 4 adet dijital giris durumu
    uint8_t  rly[2];         // 2 adet role cikis durumu
    uint16_t vbat;           // Batarya Voltaji (mV)
    uint16_t v19;            // 19V Besleme Voltaji (mV)
    uint16_t v3v3;           // 3.3V Besleme Voltaji (mV)
    uint16_t v3v8;           // 3.8V Besleme Voltaji (mV)
    uint16_t v5v;            // 5V Besleme Voltaji (mV)
    uint8_t  charge_state;   // Batarya Sarj Durumu
    int8_t   temp;           // Sicaklik Degeri (°C)
    int8_t   temp_max;       // Maksimum Sicaklik Degeri (°C)
    int8_t   temp_min;       // Minimum Sicaklik Degeri (°C)
    int16_t  panel_current;  // Panel Akimi (mA)
    uint16_t panel_voltage;  // Panel Voltaji (mV)
    uint16_t battery_voltage;// Batarya Voltaji (mV)
    int8_t   tdie_temp;      // MCU Sicaklik Degeri (°C)
    int8_t   tdie_temp_max;  // MCU Sicaklik Maks Degeri (°C)
    int8_t   tdie_temp_min;  // MCU Sicaklik Min Degeri (°C)
    uint8_t  charge_percent; // Batarya Sarj Orani (%)
    uint16_t capacity;       // Batarya Kapasitesi (mAh)
    int8_t   gsm_signal;     // GSM Sinyal Gucü (dBm)
    uint8_t  gsm_rat;        // GSM RAT (Radio Access Technology)
    int16_t  battery_current;// Batarya Akimi (mA, + sarj, - desarj)
    int8_t   battery_temp;   // Batarya Sicakligi (°C)
    uint8_t  battery_soc;    // Batarya SOC - State of Charge (%)
    uint8_t  battery_soh;    // Batarya SOH - State of Health (%)
    int8_t   ambient_temp;   // Ortam Sicakligi (°C)
    uint8_t  heater_state;   // Isitici Durumu (0=Off, 1=On)
    uint16_t heater_power;   // Isitici Gucu (mW)
} board_status_t;


#endif /* MODEM_TYPES_H_ */
