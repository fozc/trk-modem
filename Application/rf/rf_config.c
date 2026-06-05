/*
 * rf_config.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */
#include "rf_config.h"
#include "types.h"
#include "nvram.h"
 
#define breaker_config (nvram_get_breaker_rw())

int rf_config_sync(void)
{
	return nvram_sync(false);
}

bool rf_config_set(power_line_id_t line_id, const rf_config_t* config)
{
	if(line_id >= MAX_POWER_LINE_COUNT || config == NULL) {
		return false;
	}
	
	breaker_config->line[line_id].rf_config = *config;
	return true;
}

const rf_config_t* rf_config_get(power_line_id_t line_id)
{
	if(line_id >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}
	return &breaker_config->line[line_id].rf_config;
}

rf_config_t* rf_config_get_mutable(power_line_id_t line_id)
{
	if(line_id >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}
	return &breaker_config->line[line_id].rf_config;
}
