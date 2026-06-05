/*
 * breaker.c
 *
 *  Created on: 15 Agu 2025
 *      Author: fatih
 */
#include <stdio.h>
#include "breaker.h"
#include "nvram.h"
#include "bsp.h"
 
#define breaker (nvram_get_breaker_rw())

static bool breaker_initialized = false;
static breaker_data_t breaker_data = {0};

static inline bool is_ioa_equal(ioa_3byte_t ioa1, ioa_3byte_t ioa2)
{
    return (ioa1.ioa_high == ioa2.ioa_high &&
            ioa1.ioa_low == ioa2.ioa_low &&
            ioa1.ioa_mid == ioa2.ioa_mid);
}

void breaker_init(void)
{
 
}

uint8_t breaker_get_active_powerline_count(void)
{
    uint8_t count = 0;
    for(size_t i = 0; i < MAX_POWER_LINE_COUNT; i++){
        if(breaker->line[i].iec104.in_use){
            count++;
        }
    }
    return count;
}

const power_line_t * breaker_get_power_line(uint32_t idx)
{
	if(idx >= MAX_POWER_LINE_COUNT){
		return NULL;
	}

	return &breaker->line[idx];
}

const power_line_t * breaker_get_power_line_by_idx(uint32_t line_index)
{
    if (line_index >= MAX_POWER_LINE_COUNT) {
        CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
        return NULL;
    }

    return &breaker->line[line_index];
}

bool breaker_set_feeder_data(uint32_t line_index, const feeder_data_t *feeder_data)
{
	if (line_index >= MAX_POWER_LINE_COUNT) {
		CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
		return false;
	}

	memcpy(&breaker_data.feeder[line_index], feeder_data, sizeof(feeder_data_t));
	return true;
}

const feeder_data_t * breaker_get_feeder_data(uint32_t line_index)
{
	if (line_index >= MAX_POWER_LINE_COUNT) {
		CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
		return NULL;
	}

	return &breaker_data.feeder[line_index];
}



#ifdef C_SC_NA_1_ENABLED
int breaker_set_power_line_sbo_state(ioa_3byte_t ioa, sbo_state_t state)
{
    if (!breaker_initialized) {
        return -1;
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /* && is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            breaker->line[i].sbo_state = state;
            //TODO: match the ioa
            return 0;
        }
    }

    return -2;
}

sbo_state_t breaker_get_power_line_sbo_state(ioa_3byte_t ioa)
{
    if (!breaker_initialized) {
        return (sbo_state_t){0};
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /*&& is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            return breaker->line[i].sbo_state;
            //TODO: match the ioa
        }
    }

    return (sbo_state_t){0};
}

bool breaker_is_ioa_valid(ioa_3byte_t ioa)
{
    if (!breaker_initialized) {
        return false;
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /*&& is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            return true;
        }
    }

    return false;
}
#endif
