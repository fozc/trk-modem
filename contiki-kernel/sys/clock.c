/*
 * clock.c
 *
 *  Created on: Aug 26, 2022
 *      Author: fatih.ozcan
 */
#include "clock.h"


static volatile uint32_t system_tick_timer = 0;

void clock_tick(void)
{
	++system_tick_timer;
}

void clock_init(void)
{
	system_tick_timer = 0;
}

clock_time_t clock_seconds(void)
{
	return system_tick_timer / CLOCK_CONF_SECOND;
}

clock_time_t clock_time(void)
{
	return system_tick_timer;
}

void clock_set_seconds(uint32_t sec)
{
	system_tick_timer = sec * CLOCK_CONF_SECOND;
}

void clock_wait(clock_time_t msec)
{
	clock_time_t i = system_time();
	while(system_time() - i <= msec);
}

void clock_delay_usec(uint32_t dt)
{
	//TODO: write a usec delay
}
