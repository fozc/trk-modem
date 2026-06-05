/*
 * iec104.h
 *
 *  Created on: 26 Tem 2025
 *      Author: fatih
 */

#ifndef IEC104_H_
#define IEC104_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iec104_types.h"
#include "iec104_data_types.h"
#include "cp56time2a.h"
#include "types.h"

#define SBO_SELECT_TIMEOUT 60 // seconds
#define PERIODIC_SEND_INTERVAL 60

// Paket baslangic karakteri
#define IEC104_START_BYTE 0x68

// U-Format komut tipleri
#define IEC104_STARTDT_ACT 0x01   // StartDT activation
#define IEC104_STARTDT_CON 0x02   // StartDT confirmation
#define IEC104_STOPDT_ACT  0x04   // StopDT activation
#define IEC104_STOPDT_CON  0x08   // StopDT confirmation
#define IEC104_TESTFR_ACT  0x10   // TestFR activation
#define IEC104_TESTFR_CON  0x20   // TestFR confirmation

typedef struct
{
    int (*send)(const uint8_t *data, uint16_t length);

    int (*get_ariza_akimi)(uint32_t power_line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp);
    int (*get_ariza_suresi)(uint32_t power_line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp);
    int (*get_anlik_akim)(uint32_t power_line_index, uint8_t phase, float *value, qds_t *quality, cp56time2a_t *timestamp);
    int (*get_ariza_kalicimi)(uint32_t power_line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp);
    int (*get_enerji_varyok)(uint32_t power_line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp);
    int (*get_nominal_akim_varyok)(uint32_t power_line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp);
    int (*get_rf_haberlesme_varyok)(uint32_t power_line_index, uint8_t phase, siq_t *value, cp56time2a_t *timestamp);

}iec104_io_t;

void iec104_tick(void);

void iec104_set_originator_address(uint8_t address);
void iec104_set_common_address(uint16_t address);

void iec104_init(const iec104_io_t *io_cfg, const iec104_config_t *iec104_config);
void iec104_data_received(const uint8_t *data, uint16_t length);
void libiec104_poll(void);
void iec104_send_s_frame(uint16_t receive_seq);

uint16_t iec104_get_receive_sn(void);
uint16_t iec104_get_send_sn(void);
void iec_set_receive_sn(uint16_t sn);
void iec_set_send_sn(uint16_t sn);

uint16_t iec104_get_w(void);
uint16_t iec104_get_k(void);
uint16_t iec104_get_acksn(void);

ioa_3byte_t iec104_make_ioa_3byte(uint32_t ioa);
bool iec104_ioa_3byte_equals(ioa_3byte_t ioa1, ioa_3byte_t ioa2);
uint32_t iec104_ioa_3byte_to_uint32(ioa_3byte_t ioa);
 

void iec104_send_M_SP_TB_1_spontan(ioa_3byte_t ioa, uint8_t value, uint8_t quality);
void iec104_send_M_DP_TB_1_spontan(ioa_3byte_t ioa, uint8_t value, uint8_t quality);
void iec104_send_M_ME_TF_1(cot_t cot, ioa_3byte_t ioa, float value, qds_t quality);
void iec104_send_C_SC_NA_1(cot_t cot, ioa_3byte_t ioa, sco_command_state_t scs, qualifier_of_command_t qu, se_bit_t se_bit);
void iec104_send_C_DC_NA_1(cot_t cot, ioa_3byte_t ioa, dco_command_state_t dcs, qualifier_of_command_t qu, se_bit_t se_bit);


void iec104_interrogation_send_m_sp_tb_1_objects(const ioa_3byte_t *ioas, const siq_t *states);
void iec104_interrogation_send_m_me_tf_1_objects(const ioa_3byte_t *ioas, const float *values, const qds_t *quality);
void iec104_interrogation_send_c_sc_na_1_object(ioa_3byte_t ioa, uint8_t state, qualifier_of_command_t qualifier, se_bit_t se_bit);



void iec104_send_currents(const breaker_t *breaker);
void iec104_send_breaker_states(const breaker_t *breaker);


void iec104_reset(void);


cp56time2a_t iec104_get_last_clock_sync_time(void);






void iec104_send_general_interrogation_con(iec104_qoi_t qoi, uint8_t is_negative);
void iec104_send_general_interrogation_term(iec104_qoi_t qoi);


void iec104_get_feeder_temporary_faults(uint8_t *buff, uint16_t *len, uint16_t max_len, uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause);
void iec104_get_feeder_permanent_faults(uint8_t *buff, uint16_t *len, uint16_t max_len, uint8_t feeder_id, phase_id_t phase, cause_of_transmission_t cause);

#endif /* IEC104_H_ */
