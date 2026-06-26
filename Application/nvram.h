/*
 * nvram.h
 *
 *  Created on: 15 Agu 2025
 *      Author: fatih
 */

#ifndef NVRAM_H_
#define NVRAM_H_

#include <spi_flash_organization.h>
#include <stdint.h>
#include <stdbool.h>
#include "types.h"


#define DEVICE_DEFAULT_SERIAL_NUMBER 99999999

#define IEC104_DEFAULT_ORIGINATOR_ADDRESS 1
#define IEC104_DEFAULT_COMMON_ADDRESS 1
#define IEC104_DEFAULT_PERIODICAL_SEND_INTERVAL 60*15  // 15 minutes
#define IEC104_DEFAULT_T0_MAX 90
#define IEC104_DEFAULT_T1_MAX 45
#define IEC104_DEFAULT_T2_MAX 30    
#define IEC104_DEFAULT_T3_MAX 60 
#define IEC104_DEFAULT_K_MAX  64  // k > w olmalidir
#define IEC104_DEFAULT_W_MAX  24   // k > w olmalidir
#define IEC104_DEFAULT_SBO_STATE 0
#define IEC104_DEFAULT_SBO_EXECUTE_TIMEOUT 60

#define IEC_CONFIG_INIT(phaseR_m_sp_tb_ioa, phaseS_m_sp_tb_ioa, phaseT_m_sp_tb_ioa, \
                        phaseR_m_me_tf_1_ioa, phaseS_m_me_tf_1_ioa, phaseT_m_me_tf_1_ioa, \
                        c_dc_na_1_ioa) \
(iec104_line_config_t){ \
    .m_sp_tb_1_ioa = { \
        [PHASE_L1] = iec104_make_ioa_3byte(phaseR_m_sp_tb_ioa), \
        [PHASE_L2] = iec104_make_ioa_3byte(phaseS_m_sp_tb_ioa), \
        [PHASE_L3] = iec104_make_ioa_3byte(phaseT_m_sp_tb_ioa), \
    }, \
    .m_me_tf_1_ioa = { \
        [PHASE_L1] = iec104_make_ioa_3byte(phaseR_m_me_tf_1_ioa), \
        [PHASE_L2] = iec104_make_ioa_3byte(phaseS_m_me_tf_1_ioa), \
        [PHASE_L3] = iec104_make_ioa_3byte(phaseT_m_me_tf_1_ioa), \
    }, \
}



int nvram_init(void);
void nvram_dump(void);
int nvram_sync(bool crc_no_check);
void nvram_set_defaults(void);

void nvram_set_cslog_enabled(bool enabled);
bool nvram_is_cslog_enabled(void);

void    nvram_set_gsm_log_level(uint8_t level);
uint8_t nvram_get_gsm_log_level(void);




void nvram_set_is_sbo_active(bool is_active);
bool nvram_get_is_sbo_active(void);
void nvram_set_sbo_execute_timeout(uint16_t timeout);
uint16_t nvram_get_sbo_execute_timeout(void);

breaker_t* nvram_get_breaker(void);
int nvram_set_breaker(const breaker_t* breaker);
uint32_t nvram_get_device_serial_number(void);
void nvram_set_device_serial_number(uint32_t serial_number);

void nvram_set_modbus_device_addr(uint8_t slave_id);
uint8_t nvram_get_modbus_device_addr(void);

int nvram_set_m_sp_na_1_ioa(uint32_t line_index, uint8_t phase, uint32_t ioa);
ioa_3byte_t nvram_get_m_sp_na_1_ioa(uint32_t line_index, uint8_t phase);
int nvram_set_m_me_tf_1_ioa(uint32_t line_index, uint8_t phase, uint32_t ioa);
ioa_3byte_t nvram_get_m_me_tf_1_ioa(uint32_t line_index, uint8_t phase);

int nvram_set_ioa(uint8_t type_id, uint32_t line_index, uint8_t phase, uint32_t ioa);
int nvram_get_line_config(uint32_t line_index, iec104_line_config_t* config);
int nvram_set_line_config(uint32_t line_index, const iec104_line_config_t* config, bool sync);









//Modem Configuration
const modem_config_t *nvram_get_modem_config(void);
modem_config_t *nvram_get_modem_config_rw(void);
void nvram_set_modem_config(const modem_config_t *info);

breaker_t *nvram_get_breaker_rw(void);




//IEC104 Configuration
const iec104_config_t *nvram_get_iec104_config(void);
iec104_config_t *nvram_get_iec104_config_rw(void);
bool nvram_is_power_line_in_use(uint32_t line_index);
void nvram_set_iec104_config(const iec104_config_t *cfg);
const iec104_line_config_t* nvram_iec104_get_line_config(uint32_t line_index);
bool nvram_iec104_set_line_config(uint32_t line_index, const iec104_line_config_t* config);
 

// Modbus Configuration
const modbus_configs_t *nvram_get_modbus_config(void);
modbus_configs_t *nvram_get_modbus_config_rw(void);
void nvram_set_modbus_config(const modbus_configs_t *cfg);
const modbus_line_config_t* nvram_modbus_get_line_config(uint32_t line_index);
bool nvram_modbus_set_line_config(uint32_t line_index, const modbus_line_config_t* config);


//RF Ayirici Config
const rf_config_t* nvram_get_rf_config(uint32_t line_index);
bool nvram_set_rf_config(uint32_t line_index, const rf_config_t* config);

/* RFWU firmware-update session */
const rfwu_nvram_t *nvram_get_rfwu(void);
void                nvram_set_rfwu(const rfwu_nvram_t *p_rfwu);

#endif /* NVRAM_H_ */
