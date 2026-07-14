/*
 * modbus_system_stats.c
 *
 *  Created on: 11 Tem 2026
 *      Author: fatih
 */
#include "modbus_system_stats.h"
#include "bsp.h"
#include "rtc.h"
#include "adc.h"
#include "reset_source.h"
#include <stddef.h>
#include "digital_input.h"
/*
 * System statistics register block (holding space, base
 * MODBUS_SYS_STATS_ADDR_BASE == 49000).
 *
 * modbus_sys_stats_map_t is a pure LAYOUT descriptor: it is never instantiated,
 * we only borrow its member offsets (via offsetof) so each register address
 * drops out of the field order automatically. Change a field's type/size, or
 * insert/remove a field, and every address after it shifts by itself — no hand
 * renumbering of the switch below.
 *
 *   uint16_t member     -> 1 register  (UINT16)
 *   uint16_t member[2]  -> 2 registers (UINT32 / FLOAT32; big-endian, high word
 *                          first = ABCD, per MODBUS_REGISTER_MAP.md §4.1)
 *
 * Every member is 2-byte aligned, so the struct carries NO padding: the range
 * is dense and contiguous, which lets a master bulk-read the whole block in a
 * single FC03 window without an exception (0x02). Block bounds (see below) are
 * base-relative and sizeof()-driven, so they track the struct without naming any
 * field — reordering or renaming members can never desynchronise them.
 */
typedef struct {
	uint16_t rtc_sec;        /* rtc time, seconds            */
	uint16_t rtc_min;        /* rtc time, minutes            */
	uint16_t rtc_hour;       /* rtc time, hours              */
	uint16_t rtc_day;        /* rtc time, days               */
	uint16_t rtc_month;      /* rtc time, month              */
	uint16_t rtc_year;       /* rtc time, year               */
	uint16_t rtc_unix[2];    /* rtc time, unix timestamp     */
	uint16_t uptime_sec;     /* uptime, seconds              */
	uint16_t reset_reason;   /* reset source, raw CSR bits   */
	uint16_t mcu_temp;       /* die temperature, celsius     */
	uint16_t v5v;            /* 5V rail, millivolts          */
	uint16_t v3v3;           /* 3V3 rail, millivolts         */
	uint16_t v3v8;           /* 3V8 rail, millivolts         */
	uint16_t uptime_raw[2];  /* uptime, full UINT32 (ABCD)   */
	uint16_t dinput_states;  /* digital input states, bitmask */
} modbus_sys_stats_map_t;

/* Logical Modbus address of a layout member. For a 2-register member this
 * resolves to its first (high) word; the low word is at +1. Constant
 * expression, so it is usable as a switch case label. */
#define SYS_REG(member) \
	((uint16_t)(MODBUS_SYS_STATS_ADDR_BASE + \
	            offsetof(modbus_sys_stats_map_t, member) / sizeof(uint16_t)))

/* Register count the layout occupies. Tracks the struct automatically: add or
 * remove a field and this follows, with no edit here. */
#define SYS_STATS_REG_COUNT   (sizeof(modbus_sys_stats_map_t) / sizeof(uint16_t))

/* Block bounds — deliberately free of member names so reordering or renaming
 * fields can never desynchronise them:
 *   FIRST is always the base: the first struct member lives at offset 0 (C
 *        standard — no padding before the first member), so its address is
 *        always MODBUS_SYS_STATS_ADDR_BASE.
 *   LAST  is the base plus the sizeof()-driven register count, so it tracks the
 *        struct size without naming any field. */
#define SYS_STATS_ADDR_FIRST   ((uint16_t)MODBUS_SYS_STATS_ADDR_BASE)
#define SYS_STATS_ADDR_LAST    \
	((uint16_t)(MODBUS_SYS_STATS_ADDR_BASE + SYS_STATS_REG_COUNT - 1U))

/* High / low 16-bit words of a 32-bit value (ABCD word order). */
static uint16_t sys_stats_high_word(uint32_t value)
{
	return (uint16_t)(value >> 16);
}

static uint16_t sys_stats_low_word(uint32_t value)
{
	return (uint16_t)(value & 0xFFFFU);
}

bool modbus_system_stats_read(uint16_t reg_addr, uint16_t* value)
{
	/* Outside the block: not our register, let the caller try other handlers. */
	if ((reg_addr < SYS_STATS_ADDR_FIRST) || (reg_addr > SYS_STATS_ADDR_LAST)) {
		return false;
	}

	switch (reg_addr)
	{
		case SYS_REG(rtc_sec):
			*value = (uint16_t)(rtc_get_second() & 0xFFFFU);
			return true;
		case SYS_REG(rtc_min):
			*value = (uint16_t)(rtc_get_minute() & 0xFFFFU);
			return true;
		case SYS_REG(rtc_hour):
			*value = (uint16_t)(rtc_get_hour() & 0xFFFFU);
			return true;
		case SYS_REG(rtc_day):
			*value = (uint16_t)(rtc_get_day() & 0xFFFFU);
			return true;
		case SYS_REG(rtc_month):
			*value = (uint16_t)(rtc_get_month() & 0xFFFFU);
			return true;
		case SYS_REG(rtc_year):
			*value = (uint16_t)((2000 + rtc_get_year()) & 0xFFFFU);
			return true;
		case SYS_REG(uptime_sec):
			*value = (uint16_t)(bsp_get_tick() / 60000 & 0xFFFFU); // dakika cinsinden
			return true;
		case SYS_REG(rtc_unix):
			/* UINT32 high word (ABCD). */
			*value = sys_stats_high_word(rtc_get_unix_epoch());
			return true;
		case SYS_REG(rtc_unix) + 1U:
			/* UINT32 low word. */
			*value = sys_stats_low_word(rtc_get_unix_epoch());
			return true;
		case SYS_REG(reset_reason):
			*value = reset_source_get_raw();
			return true;
		case SYS_REG(mcu_temp):
			*value = adc_get_mcu_temp_c();
			return true;
		case SYS_REG(v5v):
			*value = adc_get_voltage_mv(ADC_CH_5V);
			return true;
		case SYS_REG(v3v3):
			*value = adc_get_voltage_mv(ADC_CH_3V3);
			return true;
		case SYS_REG(v3v8):
			*value = adc_get_voltage_mv(ADC_CH_3V8);
			return true;
		case SYS_REG(uptime_raw):
			/* UINT32 high word (ABCD). */
			*value = sys_stats_high_word(bsp_get_tick());
			return true;
		case SYS_REG(uptime_raw) + 1U:
			/* UINT32 low word. */
			*value = sys_stats_low_word(bsp_get_tick());
			return true;
		case SYS_REG(dinput_states):
				*value = digital_input_get_all() & 0x00FFU;
			return true;
		default:
			/* Dense layout: only reachable if a member loses its case. Return 0
			 * so a bulk read of the block never raises exception 0x02. */
			*value = 0U;
			return true;
	}
}
