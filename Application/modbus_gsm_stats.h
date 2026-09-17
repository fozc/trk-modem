/*
 * modbus_gsm_stats.h
 *
 *  Created on: Sep 16, 2026
 *      Author: fatih
 *
 * Read-only GSM-status Modbus register block: GSM modem state, signal
 * quality (raw AT+CSQ), network generation (RAT), socket states (web /
 * IEC104 / dialer) and the last Modbus exception code/timestamp.
 */

#ifndef MODBUS_GSM_STATS_H_
#define MODBUS_GSM_STATS_H_

#include <stdint.h>
#include <stdbool.h>

/* First logical Modbus address of the read-only GSM-status block.
 * Lives in the holding-register space so it is reachable via FC03, exactly
 * like the 49000/49200/49300 stats blocks.
 *
 * Wire (PDU) address = MODBUS_GSM_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE
 * (49400 - 40000 = 9400): that is the address a master (e.g. Modbus Poll)
 * must use, the same 49000/9000 split of the system-stats block. */
#define MODBUS_GSM_STATS_ADDR_BASE   49400U

bool modbus_gsm_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_GSM_STATS_H_ */
