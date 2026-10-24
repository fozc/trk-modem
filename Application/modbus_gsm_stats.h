/*
 * modbus_gsm_stats.h
 *
 *  Created on: Sep 16, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Read-only GSM-status Modbus register block: GSM modem state, signal
 * quality (raw AT+CSQ), network generation (RAT), socket states (web /
 * IEC104 / dialer) and the last Modbus exception code/timestamp.
 */

#ifndef MODBUS_GSM_STATS_H_
#define MODBUS_GSM_STATS_H_

#include <stdint.h>
#include <stdbool.h>
#include "modbus_map.h"

/* First logical Modbus address of the read-only GSM-status block.
 * Lives in the holding-register space so it is reachable via FC03, exactly
 * like the 49000/49200/49300 stats blocks.
 *
 * Wire (PDU) address = MODBUS_GSM_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE
 * (49400 - 40000 = 9400): that is the address a master (e.g. Modbus Poll)
 * must use, the same 49000/9000 split of the system-stats block. The base
 * and length are owned by the fixed map (modbus_map.h). */
#define MODBUS_GSM_STATS_ADDR_BASE   MODBUS_MAP_GSM_BASE

/* Register layout shared by the reader and RO web metadata. */
typedef struct
{
    uint16_t gsm_state;             /* gsm_states_t: 0=COMMON_INIT 1=MODULE_INIT
                                     * 2=SIM_ERROR 3=NORMAL 4=POWER_OUTAGE
                                     * 5=POWER_SAVING                     */
    uint16_t gsm_signal_csq;        /* raw AT+CSQ: 0..31, 99 = unknown   */
    uint16_t gsm_rat;               /* network_generation_t: 0/2/3/4     */
    uint16_t mb_last_error_code;    /* SonHataKodu: last exception 0..3  */
    uint16_t mb_last_error_time_hi; /* SonHataZamani UINT32 ABCD hi word */
    uint16_t mb_last_error_time_lo; /* SonHataZamani UINT32 ABCD lo word */
    uint16_t socket_state_web;      /* socket_state_t: web listener      */
    uint16_t socket_state_iec104;   /* socket_state_t: IEC104 listener   */
    uint16_t socket_state_dialer;   /* socket_state_t: HES dialer client */
    uint16_t sim_state;            /* gsm_sim_state_t; PUK required = 3 */
} modbus_gsm_stats_map_t;

bool modbus_gsm_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_GSM_STATS_H_ */

/*** end of file ***/
