/*
 * time_service.h
 *
 *  Created on: 14 Eyl 2025
 *      Author: fatih
 */

#ifndef TIME_SERVICE_H_
#define TIME_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>

uint32_t get_system_uptime(void);
void time_service_tick(void);
uint32_t time_get_elapsed(uint32_t start_time);
bool time_has_elapsed(uint32_t start_time, uint32_t duration);

#ifdef TEST
void time_service_reset(void);
void time_service_set_ticks(uint32_t ticks);
#endif



#endif /* TIME_SERVICE_H_ */
