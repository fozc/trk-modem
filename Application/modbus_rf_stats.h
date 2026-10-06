/*
 * modbus_rf_stats.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Fixed read-only validity words for existing RF measurements.
 */
#ifndef MODBUS_RF_STATS_H
#define MODBUS_RF_STATS_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_RF_STATS_ADDR_BASE 49500U
#define MODBUS_RF_STATS_REG_COUNT 21U
#define MODBUS_RF_STATS_ADDR_LAST \
    (MODBUS_RF_STATS_ADDR_BASE + MODBUS_RF_STATS_REG_COUNT - 1U)

#define MODBUS_RF_CURRENT_VALID (1U << 0U)
#define MODBUS_RF_ENERGY_VALID  (1U << 1U)
#define MODBUS_RF_LOAD_VALID    (1U << 2U)

bool modbus_rf_stats_read(uint16_t reg_addr, uint32_t now_ms, uint16_t *value);

#endif /* MODBUS_RF_STATS_H */

/*** end of file ***/
