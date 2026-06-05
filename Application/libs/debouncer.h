/*
 * debouncer.h
 *
 *  Created on: Aug 8, 2024
 *      Author: fatih
 */

#ifndef LIBS_DEBOUNCER_H_
#define LIBS_DEBOUNCER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
	uint8_t integrator;   /* Filter integrator */
	uint8_t debounce_time;
	uint8_t stable_state; /* Stable/Filtered  state */
	uint8_t flag;
}debouncer_t;



/**
 * @brief Debouncer Process
 * @details Call it periodically with suitable parameters.
 *
 * @return 1 : There is a change on button state, 0: No change
 */
int debouncer(debouncer_t * db, uint8_t pin_state);

#ifdef __cplusplus
}
#endif
#endif /* LIBS_DEBOUNCER_H_ */
