/*
 * rf.c
 *
 *  Created on: Nov 9, 2025
 *      Author: fatih
 */
#include "rf.h"
#include "nvram.h"
#include "bsp.h"

static rf_monitor_t rf_monitor[MAX_POWER_LINE_COUNT] = {0};


bool rf_set_monitor(uint32_t line_index, const rf_monitor_t* monitor)
{
	if(line_index >= MAX_POWER_LINE_COUNT || monitor == NULL){
		return false;
	}

	rf_monitor[line_index] = *monitor;
	return true;
}

const rf_monitor_t* rf_get_monitor(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT){
		return NULL;
	}

	return &rf_monitor[line_index];
}
 

