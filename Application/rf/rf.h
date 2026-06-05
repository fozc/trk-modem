/*
 * rf.h
 *
 *  Created on: Nov 9, 2025
 *      Author: fatih
 */

#ifndef RF_H_
#define RF_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"




bool rf_set_monitor(uint32_t line_index, const rf_monitor_t* monitor);
const rf_monitor_t* rf_get_monitor(uint32_t line_index);

#endif /* RF_H_ */
