/*
 * debouncer.c
 *
 *  Created on: Aug 8, 2024
 *      Author: fatih
 */
#include "debouncer.h"


int debouncer(debouncer_t * db, uint8_t pin_state)
{
	uint32_t output    = db->stable_state;

	if(pin_state == 0) //Low
	{
		if(db->integrator > 0)
		{
			db->integrator--;
		}
	}
	else
	{
		db->integrator++;
	}

	if(db->integrator == 0)
	{
		output = 0; /* input state is LOW */
	}
	else
	{
		if(db->integrator >= db->debounce_time)
		{
			db->integrator = db->debounce_time;
			output = 1; /* input state is HIGH */
		}
	}

	if(output != db->stable_state)
	{
		db->stable_state = output;
		db->flag = 1;

		return 1;
	}

	return 0;
}

