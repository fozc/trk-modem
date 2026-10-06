/*
 * modbus_power_stats.h
 *
 *  Created on: Jul 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef MODBUS_POWER_STATS_H_
#define MODBUS_POWER_STATS_H_

#include <stdint.h>
#include <stdbool.h>

/* Logical holding addresses 49200..49237 (PDU 9200..9237).
 * Map format 1 uses SCP summary fields and quality; signed quantities
 * retain their two's-complement bits, including negative SOC.
 * 32-bit fields use high word first. See MODBUS_REGISTER_MAP.md.
 */
#define MODBUS_PWR_STATS_ADDR_BASE   49200U
#define MODBUS_PWR_STATS_REG_COUNT   38U

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_POWER_STATS_H_ */

/*** end of file ***/
