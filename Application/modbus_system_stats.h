/*
 * modbus_system_stats.h
 *
 *  Created on: Jul 11, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef MODBUS_SYSTEM_STATS_H_
#define MODBUS_SYSTEM_STATS_H_

#include <stdint.h>
#include <stdbool.h>
#include "modbus_map.h"

/* First logical Modbus address of the read-only system-statistics block
 * (uptime, RTC, reset reason, supply voltages, ...). Lives in the holding-
 * register space so it is reachable via FC03. The base and length are owned
 * by the fixed map (modbus_map.h); the layout struct in the header proves its
 * register count against that contract. */
#define MODBUS_SYS_STATS_ADDR_BASE   MODBUS_MAP_SYS_BASE

/* Register layout shared by the reader and RO web metadata. */
typedef struct {
	uint16_t rtc_sec;        /* rtc time, seconds            */
	uint16_t rtc_min;        /* rtc time, minutes            */
	uint16_t rtc_hour;       /* rtc time, hours              */
	uint16_t rtc_day;        /* rtc time, days               */
	uint16_t rtc_month;      /* rtc time, month              */
	uint16_t rtc_year;       /* rtc time, year               */
	uint16_t rtc_unix[2];    /* rtc time, unix timestamp     */
	uint16_t uptime_sec;     /* uptime, seconds              */
	uint16_t reset_reason;   /* decoded reset-cause bitmask  */
	uint16_t mcu_temp;       /* die temperature, celsius     */
	uint16_t v5v;            /* 5V rail, millivolts          */
	uint16_t v3v3;           /* 3V3 rail, millivolts         */
	uint16_t v3v8;           /* 3V8 rail, millivolts         */
	uint16_t uptime_raw[2];  /* uptime, full UINT32 (ABCD)   */
	uint16_t dinput_states;  /* digital input states, bitmask */
} modbus_sys_stats_map_t;

bool modbus_system_stats_read(uint16_t reg_addr, uint16_t* value);

/**
 * @brief Freeze time-sensitive sources for one FC03 response.
 *
 * UINT32 values (rtc_unix, uptime_raw) span two registers per value;
 * reading each word from the live getter can tear the pair across a
 * counter boundary. Call this once before reading any registers of a
 * single FC03 response; the UINT32 getters then use the cached values.
 * Cooperative-scheduler sources need no locking (MB-08).
 */
void modbus_system_stats_snapshot(void);

/**
 * @brief Mark the snapshot cache stale (call once per FC03 response).
 *
 * The next system-stats register read lazily takes a fresh snapshot.
 * This keeps the poll wrapper free of getter dependencies.
 */
void modbus_system_stats_invalidate_snapshot(void);

#endif /* MODBUS_SYSTEM_STATS_H_ */

/*** end of file ***/
