/*
 * rf_discovery.c
 *
 *  Created on: 20 Ağu 2026
 *      Author: fatih
 */
#include "rf_discovery.h"
#include "console_logger.h"
#include "rf_types.h"
#include <string.h>


#define RF_DISCOVERY_LIST_SIZE 16U

static uint8_t list[RF_DISCOVERY_LIST_SIZE][RF_EUI64_LEN];
static uint8_t list_count = 0;

void rf_discovery_reset(void)
{
	list_count = 0;
	memset(list, 0, sizeof(list));
}

bool rf_discovery_add(const uint8_t *eui64){	if (list_count >= RF_DISCOVERY_LIST_SIZE){		return false;	}	// Check for duplicates	for (uint8_t i = 0; i < list_count; i++)	{		if (memcmp(list[i], eui64, RF_EUI64_LEN) == 0)		{			return false; // Duplicate found		}	}	memcpy(list[list_count], eui64, RF_EUI64_LEN);	list_count++;	return true;}

uint8_t rf_discovery_get_count(void){	return list_count;}

bool rf_discovery_get_device(uint8_t index, uint8_t *eui64_out){	if (index >= list_count){		return false;	}

	if(eui64_out == NULL){
		return false;
	}	memcpy(eui64_out, list[index], RF_EUI64_LEN);	return 0; // Success}
