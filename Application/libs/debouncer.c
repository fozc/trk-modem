/*
 * debouncer.c
 *
 *  Created on: Aug 8, 2024
 *      Author: fatih
 */
#include "debouncer.h"


int debouncer(debouncer_t *db, uint8_t pin_state)
{
	uint8_t output = db->stable_state;

	if (0U == pin_state)
	{
		if (db->integrator > 0U)
		{
			db->integrator--;
		}
	}
	else
	{
		if (db->integrator < UINT8_MAX)
		{
			db->integrator++;
		}
	}

	if (0U == db->integrator)
	{
		output = 0U;
	}
	else
	{
		if (db->integrator >= db->debounce_time)
		{
			db->integrator = db->debounce_time;
			output = 1U;
		}
	}

	if (output != db->stable_state)
	{
		db->stable_state = output;
		db->flag = 1U;

		return 1;
	}

	return 0;
}

