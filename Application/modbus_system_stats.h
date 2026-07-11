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

/* First logical Modbus address of the read-only system-statistics block
 * (uptime, RTC, reset reason, supply voltages, ...). Lives in the holding-
 * register space so it is reachable via FC03. The block length is derived from
 * the layout struct in the .c; see modbus_system_stats_read(). */
#define MODBUS_SYS_STATS_ADDR_BASE   49000U

bool modbus_system_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_SYSTEM_STATS_H_ */
