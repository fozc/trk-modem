/*
 * iec104_config.c
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */
#include "iec104_config.h"
#include "nvram.h"
#include "bsp.h"
#include "iec104_util.h"


#define iec104_config (nvram_get_iec104_config_rw())
#define breaker_config (nvram_get_breaker_rw())

int iec104_config_sync(void)
{
	return nvram_sync(false);
}

// ============================================
// Global Config Access
// ============================================

const iec104_config_t* iec104_config_get(void)
{
	return iec104_config;
}

void iec104_config_set(const iec104_config_t* config)
{
	if(config) {
		*iec104_config = *config;
	}
}

// ============================================
// Global Config Field Getters/Setters
// ============================================

// scada_ip_address
uint32_t iec104_config_get_scada_ip(void)
{
	return iec104_config->scada_ip_address;
}

void iec104_config_set_scada_ip(uint32_t ip)
{
	iec104_config->scada_ip_address = ip;
}

// scada_port
uint16_t iec104_config_get_scada_port(void)
{
	return iec104_config->scada_port;
}

void iec104_config_set_scada_port(uint16_t port)
{
	iec104_config->scada_port = port;
}

// periodical_send_interval
uint32_t iec104_config_get_periodical_send_interval(void)
{
	return iec104_config->periodical_send_interval;
}

void iec104_config_set_periodical_send_interval(uint32_t interval)
{
	iec104_config->periodical_send_interval = interval;
}

// t0_max
uint16_t iec104_config_get_t0_max(void)
{
	return iec104_config->t0_max;
}

void iec104_config_set_t0_max(uint16_t timeout)
{
	iec104_config->t0_max = timeout;
}

// t1_max
uint16_t iec104_config_get_t1_max(void)
{
	return iec104_config->t1_max;
}

void iec104_config_set_t1_max(uint16_t timeout)
{
	iec104_config->t1_max = timeout;
}

// t2_max
uint16_t iec104_config_get_t2_max(void)
{
	return iec104_config->t2_max;
}

void iec104_config_set_t2_max(uint16_t timeout)
{
	iec104_config->t2_max = timeout;
}

// t3_max
uint16_t iec104_config_get_t3_max(void)
{
	return iec104_config->t3_max;
}

void iec104_config_set_t3_max(uint16_t timeout)
{
	iec104_config->t3_max = timeout;
}

// k_max
uint8_t iec104_config_get_k_max(void)
{
	return iec104_config->k_max;
}

void iec104_config_set_k_max(uint8_t k)
{
	iec104_config->k_max = k;
}

// w_max
uint8_t iec104_config_get_w_max(void)
{
	return iec104_config->w_max;
}

void iec104_config_set_w_max(uint8_t w)
{
	iec104_config->w_max = w;
}

// sbo_execute_timeout
uint16_t iec104_config_get_sbo_execute_timeout(void)
{
	return iec104_config->sbo_execute_timeout;
}

void iec104_config_set_sbo_execute_timeout(uint16_t timeout)
{
	iec104_config->sbo_execute_timeout = timeout;
}

// is_sbo_active
uint8_t iec104_config_get_is_sbo_active(void)
{
	return iec104_config->is_sbo_active;
}

void iec104_config_set_is_sbo_active(uint8_t active)
{
	iec104_config->is_sbo_active = active;
}

// originator_address
uint8_t iec104_config_get_originator_address(void)
{
	return iec104_config->originator_address;
}

void iec104_config_set_originator_address(uint8_t addr)
{
	iec104_config->originator_address = addr;
}

// common_address
uint16_t iec104_config_get_common_address(void)
{
	return iec104_config->common_address;
}

void iec104_config_set_common_address(uint16_t addr)
{
	iec104_config->common_address = addr;
}

// ioa_aku_uyarisi
ioa_3byte_t iec104_config_get_ioa_aku_uyarisi(void)
{
	return iec104_config->ioa_aku_uyarisi;
}

void iec104_config_set_ioa_aku_uyarisi(ioa_3byte_t ioa)
{
	iec104_config->ioa_aku_uyarisi = ioa;
}

// ioa_modem_reset
ioa_3byte_t iec104_config_get_ioa_modem_reset(void)
{
	return iec104_config->ioa_modem_reset;
}

void iec104_config_set_ioa_modem_reset(ioa_3byte_t ioa)
{
	iec104_config->ioa_modem_reset = ioa;
}

// ============================================
// Line Config Access
// ============================================

bool iec104_set_line_config(uint32_t line_index, const iec104_line_config_t* line_config)
{
	if(line_index >= MAX_POWER_LINE_COUNT || line_config == NULL) {
		return false;
	}

	breaker_config->line[line_index].iec104 = *line_config;
	return true;
}

const iec104_line_config_t* iec104_get_line_config(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}

	return &breaker_config->line[line_index].iec104;
}

bool iec104_is_line_in_use(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return false;
	}

	return breaker_config->line[line_index].iec104.in_use != 0;
}



 
// IOAs per phase: 15 records * 4 fields = 60
#define TEMPORARY_FAULT_IOAS_PER_PHASE  (TEMPORARY_FAULT_COUNT * TEMPORARY_FAULT_FIELDS_PER_RECORD)

static ioa_3byte_t iec104_get_temporary_fault_field_ioa(
    uint32_t feeder_id,
    uint8_t  phase,
    uint8_t  fault_index,
    uint8_t  field_offset)
{
    if (feeder_id >= MAX_POWER_LINE_COUNT ||
        phase >= PHASE_MAX               ||
        fault_index >= TEMPORARY_FAULT_COUNT) {
        return (ioa_3byte_t){0};
    }

    uint32_t base_ioa = iec104_ioa_3byte_to_uint32(
        breaker_config->line[feeder_id].iec104.temporary_fault);

    uint32_t ioa = base_ioa
    		     + (feeder_id * TEMPORARY_FAULT_IOAS_PER_PHASE * PHASE_MAX) // Her bes faz icin 60 IOA ayrilmis, fazlar sirayla gelir
                 + (phase      * TEMPORARY_FAULT_IOAS_PER_PHASE)
                 + (fault_index * TEMPORARY_FAULT_FIELDS_PER_RECORD)
                 + field_offset;

    return iec104_make_ioa_3byte(ioa);
}

ioa_3byte_t iec104_get_feeder_temporary_fault_base_ioa(uint32_t feeder_id, uint8_t phase)
{
    if (feeder_id >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return (ioa_3byte_t){0};
    }

    uint32_t base_ioa = iec104_ioa_3byte_to_uint32(
        breaker_config->line[feeder_id].iec104.temporary_fault);

    uint32_t ioa = base_ioa + (phase * TEMPORARY_FAULT_IOAS_PER_PHASE);

    return iec104_make_ioa_3byte(ioa);
}

ioa_3byte_t iec104_get_feeder_temporary_fault_ariza_akimi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_temporary_fault_field_ioa(feeder_id, phase, fault_index, TEMPORARY_FAULT_FIELD_AKIM);
}

ioa_3byte_t iec104_get_feeder_temporary_fault_ariza_suresi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_temporary_fault_field_ioa(feeder_id, phase, fault_index, TEMPORARY_FAULT_FIELD_SURE);
}

ioa_3byte_t iec104_get_feeder_temporary_fault_enerji_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_temporary_fault_field_ioa(feeder_id, phase, fault_index, TEMPORARY_FAULT_FIELD_ENERJI);
}

ioa_3byte_t iec104_get_feeder_temporary_fault_nominal_akim_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_temporary_fault_field_ioa(feeder_id, phase, fault_index, TEMPORARY_FAULT_FIELD_NOMINAL_AKIM);
}
 
#define PERMANENT_FAULT_IOAS_PER_PHASE  (PERMANENT_FAULT_COUNT * PERMANENT_FAULT_FIELDS_PER_RECORD)

static ioa_3byte_t iec104_get_permanent_fault_field_ioa(
    uint32_t feeder_id,
    uint8_t  phase,
    uint8_t  fault_index,
    uint8_t  field_offset)
{
    if (feeder_id >= MAX_POWER_LINE_COUNT ||
        phase >= PHASE_MAX               ||
        fault_index >= PERMANENT_FAULT_COUNT) {
        return (ioa_3byte_t){0};
    }

    uint32_t base_ioa = iec104_ioa_3byte_to_uint32(
        breaker_config->line[feeder_id].iec104.permanent_fault);

    uint32_t ioa = base_ioa
    		     + (feeder_id * PERMANENT_FAULT_IOAS_PER_PHASE * PHASE_MAX) // Her bes faz icin 60 IOA ayrilmis, fazlar sirayla gelir
                 + (phase      * PERMANENT_FAULT_IOAS_PER_PHASE)
                 + (fault_index * PERMANENT_FAULT_FIELDS_PER_RECORD)
                 + field_offset;

    return iec104_make_ioa_3byte(ioa);
}

ioa_3byte_t iec104_get_feeder_permanent_fault_base_ioa(uint32_t feeder_id, uint8_t phase)
{
    if (feeder_id >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return (ioa_3byte_t){0};
    }

    uint32_t base_ioa = iec104_ioa_3byte_to_uint32(
        breaker_config->line[feeder_id].iec104.permanent_fault);

    uint32_t ioa = base_ioa + (phase * PERMANENT_FAULT_IOAS_PER_PHASE);

    return iec104_make_ioa_3byte(ioa);
}

ioa_3byte_t iec104_get_feeder_permanent_fault_ariza_akimi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_permanent_fault_field_ioa(feeder_id, phase, fault_index, PERMANENT_FAULT_FIELD_AKIM);
}

ioa_3byte_t iec104_get_feeder_permanent_fault_ariza_suresi_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_permanent_fault_field_ioa(feeder_id, phase, fault_index, PERMANENT_FAULT_FIELD_SURE);
}

ioa_3byte_t iec104_get_feeder_permanent_fault_enerji_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_permanent_fault_field_ioa(feeder_id, phase, fault_index, PERMANENT_FAULT_FIELD_ENERJI);
}

ioa_3byte_t iec104_get_feeder_permanent_fault_nominal_akim_varyok_ioa(uint32_t feeder_id, uint8_t phase, uint8_t fault_index)
{
    return iec104_get_permanent_fault_field_ioa(feeder_id, phase, fault_index, PERMANENT_FAULT_FIELD_NOMINAL_AKIM);
}
 
