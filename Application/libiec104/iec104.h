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

/* Kutuphanenin uygulama katmanina bildirdigi olaylar. SOCKET_CLOSED'i
 * kutuphane uretmez; tasiyici tarafindan uygulamaya bildirilir. */
typedef enum
{
    IEC104_EVT_SEND_TEMP_FAULTS = 1,
    IEC104_EVT_SEND_PERM_FAULTS = 2,
    IEC104_EVT_SOCKET_CLOSED = 3,
    IEC104_EVT_REQUEST_SOCKET_CLOSE = 4,
} iec104_event_t;

typedef struct
{
    int (*send)(const uint8_t *data, uint16_t length);
    void (*on_event)(iec104_event_t evt);

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
bool iec104_send_s_frame(uint16_t receive_seq);

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
 

/* value: SPI (0/1) veya DPI (0-3). quality: standart SIQ/DIQ kalite bayti -
 * BL(0x10), SB(0x20), NT(0x40), IV(0x80). */
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


bool iec104_is_link_active(void);

/* Bir fider/faz icin arıza kayitlarinin yayim ilerlemesi. Cagiran sifirlanmis
 * bir kopya ile baslar ve yayim bitene kadar ayni kopyayla geri gelir. */
typedef struct
{
    uint8_t step;       /* sirasi gelen alan (0..3) */
    uint8_t obj_index;  /* o alanda kalinan nesne indisi */
} iec104_fault_emit_state_t;

/* Kareleri tek TX huniye (iec104_send) verir. Kuyruk veya k-penceresi dolarsa
 * false doner; cagiran beklemeli ve ayni state ile yeniden cagirmalidir. */
bool iec104_emit_feeder_temporary_faults(uint8_t feeder_id, phase_id_t phase,
    cause_of_transmission_t cause, iec104_fault_emit_state_t *state);
bool iec104_emit_feeder_permanent_faults(uint8_t feeder_id, phase_id_t phase,
    cause_of_transmission_t cause, iec104_fault_emit_state_t *state);

#endif /* IEC104_H_ */
