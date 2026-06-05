/*
 * json_config.h
 *
 *  Created on: 31 Eki 2025
 *      Author: fatih
 */

#ifndef JSON_CONFIG_H_
#define JSON_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"  /* for modem_config_t */

#define MAX_STRING_LEN 64
#define MAX_ARRAYS 8

#define MAX_LINE_COUNT 8

/* Board Status Structure */
 
/* IEC Config Structure */
typedef struct
 {
    bool in_use[MAX_LINE_COUNT];                // Hat kullaniliyor mu

    uint32_t ioa_r_ariza_akimi[MAX_LINE_COUNT];     
    uint32_t ioa_s_ariza_akimi[MAX_LINE_COUNT];    
    uint32_t ioa_t_ariza_akimi[MAX_LINE_COUNT];    
    uint32_t ioa_r_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t ioa_s_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t ioa_t_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t ioa_r_ariza_turu[MAX_LINE_COUNT];    
    uint32_t ioa_s_ariza_turu[MAX_LINE_COUNT];    
    uint32_t ioa_t_ariza_turu[MAX_LINE_COUNT];    
    uint32_t ioa_r_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_s_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_t_anlik_akim[MAX_LINE_COUNT];    
    uint32_t ioa_r_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_s_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_t_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_r_nominal_akim_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_s_nominal_akim_varyok[MAX_LINE_COUNT]; 
    uint32_t ioa_t_nominal_akim_varyok[MAX_LINE_COUNT]; 
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
    uint16_t originator_address;
    uint8_t common_address;
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

    uint32_t addr_r_ariza_akimi[MAX_LINE_COUNT];     
    uint32_t addr_s_ariza_akimi[MAX_LINE_COUNT];    
    uint32_t addr_t_ariza_akimi[MAX_LINE_COUNT];    
    uint32_t addr_r_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t addr_s_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t addr_t_ariza_suresi[MAX_LINE_COUNT];   
    uint32_t addr_r_ariza_turu[MAX_LINE_COUNT];    
    uint32_t addr_s_ariza_turu[MAX_LINE_COUNT];    
    uint32_t addr_t_ariza_turu[MAX_LINE_COUNT];    
    uint32_t addr_r_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_s_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_t_anlik_akim[MAX_LINE_COUNT];    
    uint32_t addr_r_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_s_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_t_enerji_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_r_nominal_akim_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_s_nominal_akim_varyok[MAX_LINE_COUNT]; 
    uint32_t addr_t_nominal_akim_varyok[MAX_LINE_COUNT]; 
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
    uint32_t r_device_id[MAX_LINE_COUNT];
    uint32_t s_device_id[MAX_LINE_COUNT];
    uint32_t t_device_id[MAX_LINE_COUNT];
    uint8_t mode[MAX_LINE_COUNT];
    float sistem_nominal_akimi[MAX_LINE_COUNT];
    float set_edilebilir_actirma_esik_akimi[MAX_LINE_COUNT];
    uint8_t set_edilebilir_acma_ariza_sayisi[MAX_LINE_COUNT];
    float artimli_akim_esigi[MAX_LINE_COUNT];
    uint16_t hat_kopuk_hat_bosta[MAX_LINE_COUNT];
    uint16_t olu_hat_akimi_dogrulama_suresi[MAX_LINE_COUNT];
    uint8_t yenilenme_sifirlama_suresi[MAX_LINE_COUNT];
    uint8_t hat_frekansi[MAX_LINE_COUNT];
} jayirici_rf_config_t;

/* RF Monitor Structure (read-only status) */
// 0.indexde, 1.hat için veriler, 1.indexde 2.hat için veriler vs.
typedef struct {
    uint8_t hat_id[MAX_LINE_COUNT];
    uint8_t zone_id[MAX_LINE_COUNT];
    uint32_t r_device_id[MAX_LINE_COUNT];
    uint32_t s_device_id[MAX_LINE_COUNT];
    uint32_t t_device_id[MAX_LINE_COUNT];
    uint8_t r_calisma_modu[MAX_LINE_COUNT];
    uint8_t s_calisma_modu[MAX_LINE_COUNT];
    uint8_t t_calisma_modu[MAX_LINE_COUNT];
    uint8_t r_hat_frekansi[MAX_LINE_COUNT];
    uint8_t s_hat_frekansi[MAX_LINE_COUNT];
    uint8_t t_hat_frekansi[MAX_LINE_COUNT];
    int8_t r_sistem_sicakligi[MAX_LINE_COUNT];
    int8_t s_sistem_sicakligi[MAX_LINE_COUNT];
    int8_t t_sistem_sicakligi[MAX_LINE_COUNT];
    uint8_t r_sistem_dc_gerilimi[MAX_LINE_COUNT];
    uint8_t s_sistem_dc_gerilimi[MAX_LINE_COUNT];
    uint8_t t_sistem_dc_gerilimi[MAX_LINE_COUNT];
    uint16_t r_v5vdc[MAX_LINE_COUNT];
    uint16_t s_v5vdc[MAX_LINE_COUNT];
    uint16_t t_v5vdc[MAX_LINE_COUNT];
    uint16_t r_v3v3dc[MAX_LINE_COUNT];
    uint16_t s_v3v3dc[MAX_LINE_COUNT];
    uint16_t t_v3v3dc[MAX_LINE_COUNT];
    uint8_t r_actirma_dc_gerilimi[MAX_LINE_COUNT];
    uint8_t s_actirma_dc_gerilimi[MAX_LINE_COUNT];
    uint8_t t_actirma_dc_gerilimi[MAX_LINE_COUNT];
    uint16_t r_faz_akimi[MAX_LINE_COUNT];
    uint16_t s_faz_akimi[MAX_LINE_COUNT];
    uint16_t t_faz_akimi[MAX_LINE_COUNT];
    uint16_t r_faz_hata_akimi[MAX_LINE_COUNT];
    uint16_t s_faz_hata_akimi[MAX_LINE_COUNT];
    uint16_t t_faz_hata_akimi[MAX_LINE_COUNT];
    uint8_t r_aktif_sifirlama_zamanlayici_durumu[MAX_LINE_COUNT];
    uint8_t s_aktif_sifirlama_zamanlayici_durumu[MAX_LINE_COUNT];
    uint8_t t_aktif_sifirlama_zamanlayici_durumu[MAX_LINE_COUNT];
    uint8_t r_aktif_ariza_sayaci[MAX_LINE_COUNT];
    uint8_t s_aktif_ariza_sayaci[MAX_LINE_COUNT];
    uint8_t t_aktif_ariza_sayaci[MAX_LINE_COUNT];
    uint16_t r_gecmis_acma_sayisi[MAX_LINE_COUNT];
    uint16_t s_gecmis_acma_sayisi[MAX_LINE_COUNT];
    uint16_t t_gecmis_acma_sayisi[MAX_LINE_COUNT];
    uint32_t r_last_tx[MAX_LINE_COUNT];
    uint32_t s_last_tx[MAX_LINE_COUNT];
    uint32_t t_last_tx[MAX_LINE_COUNT];
    int8_t r_rssi[MAX_LINE_COUNT];
    int8_t s_rssi[MAX_LINE_COUNT];
    int8_t t_rssi[MAX_LINE_COUNT];
    uint8_t r_lqi[MAX_LINE_COUNT];
    uint8_t s_lqi[MAX_LINE_COUNT];
    uint8_t t_lqi[MAX_LINE_COUNT];
} jayirici_rf_monitor_t;

/* Partial Parser Functions - Alt bölümleri parse et */
int parse_device_config(const char *json_str, modem_config_t *config);

int parse_iec_config(const char *json_str, jiec_config_t *iec);
int parse_modbus_config(const char *json_str, jmodbus_configs_t *modbus);
int parse_rf_config(const char *json_str, jayirici_rf_config_t *rf);

/* Global Config Access Functions */
const modem_config_t* get_device_config(void);
void set_device_config(const modem_config_t *config);

/* IEC104 and Modbus config setters - No getters needed, use config managers directly */
void set_iec_config(const jiec_config_t *config);
void set_modbus_config(const jmodbus_configs_t *config);

jayirici_rf_config_t* get_rf_config(void);
void set_rf_config(const jayirici_rf_config_t *config);

#endif /* JSON_CONFIG_H_ */
