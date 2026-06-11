/*
 * time_service.c
 *
 *  Created on: 14 Eyl 2025
 *      Author: fatih
 */
#include "time_service.h"
#include <stdio.h>

static uint32_t system_ticks = 0; // in milliseconds


#ifdef TEST
void time_service_reset(void)
{
    system_ticks = 0;
}

void time_service_set_ticks(uint32_t ticks)
{
    system_ticks = ticks;
}
#endif


void time_service_tick(void)
{
    system_ticks++;
}

uint32_t get_system_uptime(void)
{
    return system_ticks;
}

uint32_t time_get_elapsed(uint32_t start_time)
{
    uint32_t current_time = system_ticks;
    if (current_time >= start_time) {
        return current_time - start_time;
    } else {
        // Handle wrap-around
        return (UINT32_MAX - start_time + 1) + current_time;
    }
}

bool time_has_elapsed(uint32_t start_time, uint32_t duration)
{
    return time_get_elapsed(start_time) >= duration;
}




