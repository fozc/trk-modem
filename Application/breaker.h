/*
 * breaker.h
 *
 *  Created on: 15 Agu 2025
 *      Author: fatih
 */

#ifndef BREAKER_H_
#define BREAKER_H_

#include "types.h"

void breaker_init(void);
uint8_t breaker_get_active_powerline_count(void);
bool breaker_is_ioa_valid(ioa_3byte_t ioa);
const power_line_t * breaker_get_power_line_by_idx(uint32_t idx);

bool breaker_set_feeder_data(uint32_t line_index, const feeder_data_t *feeder_data);
const feeder_data_t * breaker_get_feeder_data(uint32_t line_index);

#endif /* BREAKER_H_ */
