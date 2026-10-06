/*
 * json_config.h
 *
 *  Created on: 31 Eki 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef JSON_CONFIG_H_
#define JSON_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"  /* for modem_config_t */

#define MAX_STRING_LEN 64
#define MAX_ARRAYS MAX_POWER_LINE_COUNT

#define MAX_LINE_COUNT MAX_POWER_LINE_COUNT

/* Board Status Structure */
 
/* IEC Config Structure */
typedef struct
 {
    bool in_use[MAX_LINE_COUNT];                // Hat kullaniliyor mu
    uint32_t temporary_fault_base[MAX_LINE_COUNT];
    uint32_t permanent_fault_base[MAX_LINE_COUNT];

    uint32_t ioa_r_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_s_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_t_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_r_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_s_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_t_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_r_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t ioa_s_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t ioa_t_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t ioa_r_rfhab_varyok[MAX_LINE_COUNT];    
    uint32_t ioa_s_rfhab_varyok[MAX_LINE_COUNT];    
    uint32_t ioa_t_rfhab_varyok[MAX_LINE_COUNT];    

} jiec_line_config_t;

// Ana IEC Config yapisi
typedef struct 
{
    uint32_t periodical_send_interval;
    char scada_ip_address[MAX_STRING_LEN];
    uint16_t scada_port;
    uint8_t t0_timeout;
    uint8_t t1_timeout;
    uint8_t t2_timeout;
    uint8_t t3_timeout;
    uint8_t k_max;
    uint8_t w_max;
    uint8_t originator_address;
    uint16_t common_address;
    bool sbo_active;
    uint32_t sbo_timeout;
    uint32_t ioa_aku_uyarisi;
    uint32_t ioa_modem_reset;

    jiec_line_config_t line;
} jiec_config_t;

/* Modbus Config Structure */
typedef struct
 {
    bool in_use[MAX_LINE_COUNT];                // Hat kullaniliyor mu

    uint32_t addr_r_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_s_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_t_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_r_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_s_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_t_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_r_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t addr_s_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t addr_t_yuk_akimi_varyok[MAX_LINE_COUNT];
    uint32_t addr_r_rfhab_varyok[MAX_LINE_COUNT];    
    uint32_t addr_s_rfhab_varyok[MAX_LINE_COUNT];    
    uint32_t addr_t_rfhab_varyok[MAX_LINE_COUNT];  
} jmodbus_line_config_t;

typedef struct 
{
    uint8_t device_addr;                     // Modbus cihaz adresi 1-247
    uint8_t last_error_code;
    uint32_t baud_rate;
    uint32_t addr_aku_uyarisi;
    uint32_t addr_modem_reset;
    jmodbus_line_config_t line;
} jmodbus_configs_t;

/* RF Config Structure */
typedef struct
{
    bool in_use[MAX_LINE_COUNT];
    uint8_t hat_id[MAX_LINE_COUNT];
    uint8_t zone_id[MAX_LINE_COUNT];
    char r_eui64[MAX_LINE_COUNT][RF_EUI64_HEX_LEN];   /* 16-hex, "" = atanmamis */
    char s_eui64[MAX_LINE_COUNT][RF_EUI64_HEX_LEN];
    char t_eui64[MAX_LINE_COUNT][RF_EUI64_HEX_LEN];
    uint8_t mode[MAX_LINE_COUNT];
    float sistem_nominal_akimi[MAX_LINE_COUNT];
    float set_edilebilir_actirma_esik_akimi[MAX_LINE_COUNT];
    uint8_t set_edilebilir_acma_ariza_sayisi[MAX_LINE_COUNT];
    float artimli_akim_esigi[MAX_LINE_COUNT];
    float hat_kopuk_hat_bosta[MAX_LINE_COUNT];
    uint16_t olu_hat_akimi_dogrulama_suresi[MAX_LINE_COUNT];
    uint16_t yenilenme_sifirlama_suresi[MAX_LINE_COUNT];
    uint8_t hat_frekansi[MAX_LINE_COUNT];
    /* Spec R2 section 3 - ekranda olmayan (Faz 4) ama tasinan alanlar */
    float is_safety[MAX_LINE_COUNT];
    uint16_t threshold_ms[MAX_LINE_COUNT];
    uint16_t t_mem_dead_sec[MAX_LINE_COUNT];
    uint16_t inrush_timer_ms[MAX_LINE_COUNT];
    float inrush_multiplier[MAX_LINE_COUNT];
    uint16_t sync_trip_delay_ms[MAX_LINE_COUNT];
    uint16_t trip_pulse_duration_ms[MAX_LINE_COUNT];
    uint8_t trip_mode[MAX_LINE_COUNT];
    uint8_t inrush_100hz_ratio[MAX_LINE_COUNT];
    uint8_t clp_enabled[MAX_LINE_COUNT];
    float clp_multiplier[MAX_LINE_COUNT];
    uint16_t clp_duration_ms[MAX_LINE_COUNT];
    float vtrip_target[MAX_LINE_COUNT];
} jayirici_rf_config_t;



/* Partial Parser Functions - Alt bolumleri parse et */
int parse_device_config(const char *json_str, modem_config_t *config);

/* Last synchronous IEC parse/set address error; empty on other errors. */
const char *json_config_get_iec_address_error(void);
int parse_iec_config(const char *json_str, jiec_config_t *iec);
int parse_modbus_config(const char *json_str, jmodbus_configs_t *modbus);
int parse_rf_config(const char *json_str, jayirici_rf_config_t *rf);

/* Global Config Access Functions */
const modem_config_t* get_device_config(void);

/* Setters return 0 when the change was persisted to flash, -1 when the
 * NVRAM sync failed (the change stays in RAM and is retried on the next
 * save attempt). */
int set_device_config(const modem_config_t *config);

/* IEC104 and Modbus config setters - No getters needed, use config managers directly */
int set_iec_config(const jiec_config_t *config);
int set_modbus_config(const jmodbus_configs_t *config);

#endif /* JSON_CONFIG_H_ */
