/*
 * modbus_bms_stats.h
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */

#ifndef MODBUS_BMS_STATS_H_
#define MODBUS_BMS_STATS_H_

#include <stdint.h>
#include <stdbool.h>

/* First logical Modbus address of the read-only BMS telemetry block (pack
 * voltage/current, SoC/SoH, per-cell voltages, temperatures, work state, MOS
 * and fault bitfields...). Lives in the holding-register space so it is
 * reachable via FC03. Mirrors modbus_power_stats / modbus_system_stats; the
 * block length is derived from the layout struct in the .c, see
 * modbus_bms_stats_read().
 *
 * Wire (PDU) address = MODBUS_BMS_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE
 * (49300 - 40000 = 9300): that is the address a master (e.g. Modbus Poll) must
 * use, exactly like the 49200/9200 split of the power-stats block. */
#define MODBUS_BMS_STATS_ADDR_BASE   49300U

bool modbus_bms_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_BMS_STATS_H_ */
