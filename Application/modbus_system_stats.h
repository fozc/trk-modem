/*
 * modbus_system_stats.h
 *
 *  Created on: 11 Tem 2026
 *      Author: fatih
 */

#ifndef MODBUS_SYSTEM_STATS_H_
#define MODBUS_SYSTEM_STATS_H_

#include <stdint.h>
#include <stdbool.h>
#include "modbus_map.h"

/* First logical Modbus address of the read-only system-statistics block
 * (uptime, RTC, reset reason, supply voltages, ...). Lives in the holding-
 * register space so it is reachable via FC03. The base and length are owned
 * by the fixed map (modbus_map.h); the layout struct in the .c proves its
 * register count against that contract. */
#define MODBUS_SYS_STATS_ADDR_BASE   MODBUS_MAP_SYS_BASE

bool modbus_system_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_SYSTEM_STATS_H_ */
