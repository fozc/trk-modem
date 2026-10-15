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
#include "modbus_map.h"

/* Logical holding addresses 49200..49237 (PDU 9200..9237).
 * Map format 1 uses SCP summary fields and quality; signed quantities
 * retain their two's-complement bits, including negative SOC.
 * 32-bit fields use high word first. The base and length are owned by the
 * fixed map (modbus_map.h). See MODBUS_REGISTER_MAP.md.
 */
#define MODBUS_PWR_STATS_ADDR_BASE   MODBUS_MAP_PWR_BASE
#define MODBUS_PWR_STATS_REG_COUNT   MODBUS_MAP_PWR_REG_COUNT

/* Register layout shared by the reader and RO web metadata. */
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

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_POWER_STATS_H_ */

/*** end of file ***/
