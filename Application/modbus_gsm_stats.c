/*
 * modbus_gsm_stats.c
 *
 *  Created on: Sep 16, 2026
 *      Author: fatih
 *
 * GSM-status register block (holding space, base
 * MODBUS_GSM_STATS_ADDR_BASE == 49400): modem state/signal/RAT, socket
 * states and the last Modbus exception. Mirrors modbus_bms_stats.
 */

#include "modbus_gsm_stats.h"
#include "modbus_config.h"
#include "gsm_engine.h"
#include "gsm_info.h"
#include "gsm_types.h"
#include <stddef.h>

/*
 * modbus_gsm_stats_map_t is a pure LAYOUT descriptor - the same trick as
 * modbus_power_stats / modbus_bms_stats: it is never instantiated, we only
 * borrow its member offsets (via offsetof) so each register address drops
 * out of the field order automatically. Change a field's type/size, or
 * insert/remove a field, and every address AFTER it shifts by itself; add
 * new fields only at the tail so already-documented addresses stay stable.
 *
 *   uint16_t member      -> 1 register (UINT16)
 *   uint16_t member[N]   -> N contiguous registers
 *
 * Every member is a uint16_t (or an array of them), so the struct carries NO
 * padding: the range is dense and contiguous, which lets a master bulk-read
 * the whole block in a single FC03 window without an exception (0x02).
 */
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
} modbus_gsm_stats_map_t;

/* Logical Modbus address of a layout member. Constant expression, so it is
 * usable as a switch case label; for an array member it yields the address
 * of the first element. */
#define GSM_REG(member) \
    ((uint16_t)(MODBUS_GSM_STATS_ADDR_BASE + \
                offsetof(modbus_gsm_stats_map_t, member) / sizeof(uint16_t)))

/* Register count the layout occupies. Add or remove a field and this
 * follows, with no edit here. */
#define GSM_STATS_REG_COUNT  (sizeof(modbus_gsm_stats_map_t) / sizeof(uint16_t))

/* Block bounds - deliberately free of member names so reordering or
 * renaming fields can never desynchronise them. */
#define GSM_STATS_ADDR_FIRST  ((uint16_t)MODBUS_GSM_STATS_ADDR_BASE)
#define GSM_STATS_ADDR_LAST \
    ((uint16_t)(MODBUS_GSM_STATS_ADDR_BASE + GSM_STATS_REG_COUNT - 1U))

bool modbus_gsm_stats_read(uint16_t reg_addr, uint16_t* value)
{
    /* Outside the block: not our register, let the caller try other
     * handlers. */
    if ((reg_addr < GSM_STATS_ADDR_FIRST) || (reg_addr > GSM_STATS_ADDR_LAST))
    {
        return false;
    }

    if (NULL == value)
    {
        return false;
    }

    const uint32_t error_time = modbus_config_get_last_error_time();

    switch (reg_addr)
    {
        case GSM_REG(gsm_state):
            *value = (uint16_t)gsm_get_main_state();
            return true;
        case GSM_REG(gsm_signal_csq):
            *value = (uint16_t)gsm_info_get_signal_quality();
            return true;
        case GSM_REG(gsm_rat):
            *value = (uint16_t)get_network_generation();
            return true;
        case GSM_REG(mb_last_error_code):
            *value = (uint16_t)modbus_config_get_last_error_code();
            return true;
        case GSM_REG(mb_last_error_time_hi):
            *value = (uint16_t)(error_time >> 16);
            return true;
        case GSM_REG(mb_last_error_time_lo):
            *value = (uint16_t)(error_time & 0xFFFFU);
            return true;
        case GSM_REG(socket_state_web):
            *value = (uint16_t)gsm_get_socket_state((uint8_t)LISTENER_SOCKET);
            return true;
        case GSM_REG(socket_state_iec104):
            *value = (uint16_t)gsm_get_socket_state((uint8_t)IEC104_LISTENER_SOCKET);
            return true;
        case GSM_REG(socket_state_dialer):
            *value = (uint16_t)gsm_get_socket_state((uint8_t)DIALER_SOCKET);
            return true;
        default:
            /* Dense layout: only reachable if a member loses its case.
             * Return 0 so a bulk read of the block never raises exception
             * 0x02. */
            *value = 0U;
            return true;
    }
}

/*** end of file ***/
