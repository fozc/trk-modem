/*
 * iec104_config.h
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */

#ifndef LIBIEC104_IEC104_CONFIG_H_
#define LIBIEC104_IEC104_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"


#define TEMPORARY_FAULT_COUNT              15U
#define TEMPORARY_FAULT_FIELDS_PER_RECORD   4U
#define TEMPORARY_FAULT_FIELD_AKIM          0U
#define TEMPORARY_FAULT_FIELD_SURE          1U
#define TEMPORARY_FAULT_FIELD_ENERJI        2U
#define TEMPORARY_FAULT_FIELD_NOMINAL_AKIM  3U

#define PERMANENT_FAULT_COUNT              15U
#define PERMANENT_FAULT_FIELDS_PER_RECORD   4U
#define PERMANENT_FAULT_FIELD_AKIM          0U
#define PERMANENT_FAULT_FIELD_SURE          1U
#define PERMANENT_FAULT_FIELD_ENERJI        2U
#define PERMANENT_FAULT_FIELD_NOMINAL_AKIM  3U

// Basic Functions
int iec104_config_sync(void);

// Global Config Access
const iec104_config_t* iec104_config_get(void);
void iec104_config_set(const iec104_config_t* config);

// Global Config Field Getters/Setters

// scada_ip_address
uint32_t iec104_config_get_scada_ip(void);
void iec104_config_set_scada_ip(uint32_t ip);

// scada_port
uint16_t iec104_config_get_scada_port(void);
void iec104_config_set_scada_port(uint16_t port);

// periodical_send_interval
uint32_t iec104_config_get_periodical_send_interval(void);
void iec104_config_set_periodical_send_interval(uint32_t interval);

// t0_max
uint16_t iec104_config_get_t0_max(void);
void iec104_config_set_t0_max(uint16_t timeout);

// t1_max
uint16_t iec104_config_get_t1_max(void);
void iec104_config_set_t1_max(uint16_t timeout);

// t2_max
uint16_t iec104_config_get_t2_max(void);
void iec104_config_set_t2_max(uint16_t timeout);

// t3_max
uint16_t iec104_config_get_t3_max(void);
void iec104_config_set_t3_max(uint16_t timeout);

// k_max
uint8_t iec104_config_get_k_max(void);
void iec104_config_set_k_max(uint8_t k);

// w_max
uint8_t iec104_config_get_w_max(void);
void iec104_config_set_w_max(uint8_t w);

// sbo_execute_timeout
uint16_t iec104_config_get_sbo_execute_timeout(void);
void iec104_config_set_sbo_execute_timeout(uint16_t timeout);

// is_sbo_active
uint8_t iec104_config_get_is_sbo_active(void);
void iec104_config_set_is_sbo_active(uint8_t active);

// originator_address
uint8_t iec104_config_get_originator_address(void);
void iec104_config_set_originator_address(uint8_t addr);

// common_address
uint16_t iec104_config_get_common_address(void);
void iec104_config_set_common_address(uint16_t addr);

// ioa_aku_uyarisi
ioa_3byte_t iec104_config_get_ioa_aku_uyarisi(void);
void iec104_config_set_ioa_aku_uyarisi(ioa_3byte_t ioa);

// ioa_modem_reset
ioa_3byte_t iec104_config_get_ioa_modem_reset(void);
void iec104_config_set_ioa_modem_reset(ioa_3byte_t ioa);

// Line Config Access
bool iec104_set_line_config(uint32_t line_index, const iec104_line_config_t* line_config);
const iec104_line_config_t* iec104_get_line_config(uint32_t line_index);
bool iec104_is_line_in_use(uint32_t line_index);



// Temporary Fault IOA Access
ioa_3byte_t iec104_get_feeder_temporary_fault_base_ioa(uint32_t feeder_id, uint8_t phase);
ioa_3byte_t iec104_get_feeder_temporary_fault_ariza_akimi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_temporary_fault_ariza_suresi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_temporary_fault_enerji_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_temporary_fault_nominal_akim_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);

// Permanent Fault IOA Access
ioa_3byte_t iec104_get_feeder_permanent_fault_base_ioa(uint32_t feeder_id, uint8_t phase);
ioa_3byte_t iec104_get_feeder_permanent_fault_ariza_akimi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_permanent_fault_ariza_suresi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_permanent_fault_enerji_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);
ioa_3byte_t iec104_get_feeder_permanent_fault_nominal_akim_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index);



#endif /* LIBIEC104_IEC104_CONFIG_H_ */
