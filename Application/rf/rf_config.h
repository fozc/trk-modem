/*
 * rf_config.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */

#ifndef RF_RF_CONFIG_H_
#define RF_RF_CONFIG_H_

#include "rf_types.h"
#include "types.h"

int rf_config_sync(void);
const rf_config_t* rf_config_get(power_line_id_t line_id);
rf_config_t* rf_config_get_mutable(power_line_id_t line_id);
bool rf_config_set(power_line_id_t line_id, const rf_config_t* config);

#endif /* RF_RF_CONFIG_H_ */
