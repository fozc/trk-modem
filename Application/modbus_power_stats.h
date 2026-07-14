/*
 * modbus_power_stats.h
 *
 *  Created on: 14 Tem 2026
 *      Author: fatih
 */

#ifndef MODBUS_POWER_STATS_H_
#define MODBUS_POWER_STATS_H_

#include <stdint.h>
#include <stdbool.h>

/* First logical Modbus address of the read-only PowerBoard telemetry block
 * (battery/PV/DC voltages, charge & bus currents, temperatures, SoC/SoH, BQ
 * fault and alarm bitfields...). Lives in the holding-register space so it is
 * reachable via FC03. Mirrors modbus_system_stats; the block length is derived
 * from the layout struct in the .c, see modbus_power_stats_read().
 *
 * Wire (PDU) address = MODBUS_PWR_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE
 * (49200 - 40000 = 9200): that is the address a master (e.g. Modbus Poll) must
 * use, exactly like the 49000/9000 split of the system-stats block. */
#define MODBUS_PWR_STATS_ADDR_BASE   49200U

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_POWER_STATS_H_ */
