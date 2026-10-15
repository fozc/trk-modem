/*
 * modbus_power_stats.c
 *
 *  Created on: Jul 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SCP power summary registers, with explicit per-field quality.
 *
 * Layout follows the struct-bind pattern of the other stats blocks (see
 * modbus_map.h): modbus_pwr_stats_map_t is a pure layout descriptor, never
 * instantiated; each register address drops out of the member order
 * automatically and the dense uint16_t layout keeps the block bulk-readable
 * in a single FC03 window. The base address and block length are owned by
 * the fixed map.
 */

#include "modbus_power_stats.h"
#include "power_board_scp.h"
#include "stm32u3xx_hal.h"
#include <stddef.h>

typedef struct
{
    uint16_t has_summary;             /* SCP summary received: 0/1          */
    uint16_t valid_fields;            /* per-field validity bitmask         */
    uint16_t seq;                     /* E1 counter                         */
    uint16_t flags;                   /* E1 status byte, raw                */
    uint16_t flags2;                  /* E1 status2 byte, raw               */
    uint16_t telemetry_age_sec;       /* reported + local age, 255 saturated*/
    uint16_t source;                  /* 0 bat / 1 PV / 2 DC / 3 unknown    */
    uint16_t charge_phase;            /* 0 none..5 full                     */
    uint16_t session;                 /* PWRB session 1..255, 0 none        */
    uint16_t pv_mv;                   /* PV rail, mV                        */
    uint16_t dc_mv;                   /* DC rail, mV                        */
    uint16_t input_ma;                /* input current, INT16 mA            */
    uint16_t system_mv;               /* system rail, mV                    */
    uint16_t battery_mv;              /* battery rail, mV                   */
    uint16_t battery_ma;              /* battery current, INT16 mA          */
    uint16_t input_power_10mw;        /* INT16, 10 mW                       */
    uint16_t battery_power_10mw;      /* INT16, 10 mW, + charge             */
    uint16_t system_power_10mw;       /* INT16, 10 mW, - consumption        */
    uint16_t soc_tenths;              /* INT16, 0.1 %, negative kept        */
    uint16_t soh_percent;             /* UINT16, %                          */
    uint16_t battery_temperature;     /* INT16 degC, -128 invalid           */
    uint16_t board_temperature;       /* INT16 degC, -128 invalid           */
    uint16_t capacity_ah;             /* Ah, 7 + unknown source             */
    uint16_t charge_rate_permille;    /* per-mille of C                     */
    uint16_t last_gasp_count;         /* MH notification counter            */
    uint16_t active_alarms_hi;        /* current E1/E3 mask, high word      */
    uint16_t active_alarms_lo;        /* current mask, low word             */
    uint16_t soc_flags;               /* E1 soc_capa byte, raw              */
    uint16_t alarm_seq;               /* last E3 counter                    */
    uint16_t suppressed;              /* suppressed E3 edge count           */
    uint16_t has_last_gasp;           /* last gasp seen: 0/1                */
    uint16_t last_gasp_cancelled;     /* same-session cancel: 0/1           */
    uint16_t last_gasp_session;       /* last-gasp PWRB session             */
    uint16_t last_gasp_cause;         /* 1 sudden, 2 predicted              */
    uint16_t summary_age_ms_hi;       /* local age of summary, high word    */
    uint16_t summary_age_ms_lo;       /* low word, UINT32_MAX when none     */
    uint16_t supported_summary_version; /* supported E1 layout: 1          */
    uint16_t map_version;             /* this register layout: 1            */
} modbus_pwr_stats_map_t;

/* Logical Modbus address of a layout member; constant expression, usable as
 * a switch case label. */
#define PWR_REG(member) \
	((uint16_t)(MODBUS_PWR_STATS_ADDR_BASE + \
	            offsetof(modbus_pwr_stats_map_t, member) / sizeof(uint16_t)))

/* Register count the layout occupies. */
#define PWR_STATS_REG_COUNT  (sizeof(modbus_pwr_stats_map_t) / sizeof(uint16_t))

/* The fixed map owns the block extent; this layout must prove it. */
_Static_assert(PWR_STATS_REG_COUNT == MODBUS_MAP_PWR_REG_COUNT,
               "power-summary layout does not match the fixed map extent");

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t *value)
{
    if ((NULL == value) || (MODBUS_PWR_STATS_ADDR_BASE > reg_addr) ||
        ((uint16_t)(MODBUS_PWR_STATS_ADDR_BASE + PWR_STATS_REG_COUNT)
             <= reg_addr))
    {
        return false;
    }

    power_board_snapshot_t power;
    (void)power_board_get_snapshot(HAL_GetTick(), &power);
    const rf_scp_power_summary_t *data = &power.summary;

    switch (reg_addr)
    {
        case PWR_REG(has_summary):
            *value = power.has_summary ? 1U : 0U;
            return true;
        case PWR_REG(valid_fields):
            *value = power.valid_fields;
            return true;
        case PWR_REG(seq):
            *value = data->seq;
            return true;
        case PWR_REG(flags):
            *value = data->flags;
            return true;
        case PWR_REG(flags2):
            *value = data->flags2;
            return true;
        case PWR_REG(telemetry_age_sec):
            *value = power.telemetry_age_sec;
            return true;
        case PWR_REG(source):
            *value = data->source;
            return true;
        case PWR_REG(charge_phase):
            *value = data->charge_phase;
            return true;
        case PWR_REG(session):
            *value = data->session;
            return true;
        case PWR_REG(pv_mv):
            *value = data->pv_mv;
            return true;
        case PWR_REG(dc_mv):
            *value = data->dc_mv;
            return true;
        case PWR_REG(input_ma):
            *value = (uint16_t)data->input_ma;
            return true;
        case PWR_REG(system_mv):
            *value = data->system_mv;
            return true;
        case PWR_REG(battery_mv):
            *value = data->battery_mv;
            return true;
        case PWR_REG(battery_ma):
            *value = (uint16_t)data->battery_ma;
            return true;
        case PWR_REG(input_power_10mw):
            *value = (uint16_t)data->input_power_10mw;
            return true;
        case PWR_REG(battery_power_10mw):
            *value = (uint16_t)data->battery_power_10mw;
            return true;
        case PWR_REG(system_power_10mw):
            *value = (uint16_t)data->system_power_10mw;
            return true;
        case PWR_REG(soc_tenths):
            *value = (uint16_t)data->soc_tenths;
            return true;
        case PWR_REG(soh_percent):
            *value = data->soh_percent;
            return true;
        case PWR_REG(battery_temperature):
            *value = (uint16_t)(int16_t)data->battery_temperature;
            return true;
        case PWR_REG(board_temperature):
            *value = (uint16_t)(int16_t)data->board_temperature;
            return true;
        case PWR_REG(capacity_ah):
            *value = data->capacity_ah;
            return true;
        case PWR_REG(charge_rate_permille):
            *value = data->charge_rate_permille;
            return true;
        case PWR_REG(last_gasp_count):
            *value = data->last_gasp_count;
            return true;
        case PWR_REG(active_alarms_hi):
            *value = (uint16_t)(power.active_alarms >> 16U);
            return true;
        case PWR_REG(active_alarms_lo):
            *value = (uint16_t)(power.active_alarms & 0xFFFFU);
            return true;
        case PWR_REG(soc_flags):
            *value = data->soc_flags;
            return true;
        case PWR_REG(alarm_seq):
            *value = power.alarm_seq;
            return true;
        case PWR_REG(suppressed):
            *value = power.suppressed;
            return true;
        case PWR_REG(has_last_gasp):
            *value = power.has_last_gasp ? 1U : 0U;
            return true;
        case PWR_REG(last_gasp_cancelled):
            *value = power.last_gasp_cancelled ? 1U : 0U;
            return true;
        case PWR_REG(last_gasp_session):
            *value = power.last_gasp_session;
            return true;
        case PWR_REG(last_gasp_cause):
            *value = power.last_gasp_cause;
            return true;
        case PWR_REG(summary_age_ms_hi):
            *value = (uint16_t)(power.received_age_ms >> 16U);
            return true;
        case PWR_REG(summary_age_ms_lo):
            *value = (uint16_t)(power.received_age_ms & 0xFFFFU);
            return true;
        case PWR_REG(supported_summary_version):
            *value = 1U;
            return true;
        case PWR_REG(map_version):
            *value = 1U;
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
