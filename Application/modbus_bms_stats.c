/*
 * modbus_bms_stats.c
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */
#include "modbus_bms_stats.h"
#include "bms_reader.h"
#include "bms.h"
#include <stddef.h>

/*
 * BMS telemetry register block (holding space, base
 * MODBUS_BMS_STATS_ADDR_BASE == 49300).
 *
 * modbus_bms_stats_map_t is a pure LAYOUT descriptor — the same trick as
 * modbus_power_stats: it is never instantiated, we only borrow its member
 * offsets (via offsetof) so each register address drops out of the field order
 * automatically. Change a field's type/size, or insert/remove a field, and
 * every address after it shifts by itself — no hand renumbering of the switch
 * below.
 *
 *   uint16_t member          -> 1 register (UINT16)
 *   uint16_t member[N]        -> N contiguous registers
 *
 * Every member is a uint16_t (or an array of them), so the struct carries NO
 * padding: the range is dense and contiguous, which lets a master bulk-read the
 * whole block in a single FC03 window without an exception (0x02). Block bounds
 * (see below) are base-relative and sizeof()-driven, so they track the struct
 * without naming any field.
 *
 * The source bms_data_t mixes floats and signed integers; the fixed-point
 * scalings applied when populating a register are documented per field. Signed
 * fields (currents, temperatures) are reinterpreted as their two's-complement
 * uint16_t bit pattern so a master reads them back as signed 16-bit.
 */
typedef struct {
	uint16_t valid;              /* is_data_valid flag (0/1)                   */
	uint16_t soh_valid;          /* is_soh_valid flag (0/1)                    */
	uint16_t total_voltage_dv;   /* 0x38 pack voltage x10 (0.1 V)              */
	uint16_t current_da;         /* 0x39 pack current x10 (0.1 A, signed)      */
	uint16_t soc_x10;            /* 0x3A State-of-charge x10 (%)               */
	uint16_t soh_x10;            /* 0x117 State-of-health x10 (%)              */
	uint16_t life_heartbeat;     /* 0x3B LIFE heartbeat counter                */
	uint16_t active_cell_count;  /* 0x3C battery (cell) quantity               */
	uint16_t temp_sensor_count;  /* 0x3D temperature sensor quantity           */
	uint16_t max_cell_mv;        /* 0x3E highest cell voltage (mV)             */
	uint16_t max_cell_index;     /* 0x3F highest cell serial number            */
	uint16_t min_cell_mv;        /* 0x40 lowest cell voltage (mV)              */
	uint16_t min_cell_index;     /* 0x41 lowest cell serial number             */
	uint16_t cell_diff_mv;       /* 0x42 cell voltage delta (mV)               */
	uint16_t max_temp_c;         /* 0x43 max cell temperature (degC, signed)   */
	uint16_t max_temp_index;     /* 0x44 max temp sensor serial number         */
	uint16_t min_temp_c;         /* 0x45 min cell temperature (degC, signed)   */
	uint16_t min_temp_index;     /* 0x46 min temp sensor serial number         */
	uint16_t temp_diff_c;        /* 0x47 temp delta (degC, signed)             */
	uint16_t work_state;         /* 0x48 0 idle / 1 charge / 2 discharge       */
	uint16_t charger_status;     /* 0x49 0 none / 1 charger detected           */
	uint16_t load_status;        /* 0x4A 0 none / 1 load detected              */
	uint16_t remaining_cap_dah;  /* 0x4B remaining capacity x10 (0.1 Ah)       */
	uint16_t cycle_count;        /* 0x4C battery cycle times                   */
	uint16_t balance_state;      /* 0x4D 0 off / 1 passive / 2 active          */
	uint16_t mos_status_flags;   /* 0x52..0x56 MOS status bitmask              */
	uint16_t average_voltage_mv; /* 0x57 average cell voltage (mV)             */
	uint16_t power_w;            /* 0x58 power (W)                             */
	uint16_t energy_wh;          /* 0x59 energy (Wh)                           */
	uint16_t mos_temp_c;         /* 0x5A power MOS temperature (degC, signed)  */
	uint16_t ambient_temp_c;     /* 0x5B ambient temperature (degC, signed)    */
	uint16_t heating_temp_c;     /* 0x5C heating temperature (degC, signed)    */
	uint16_t heating_current_a;  /* 0x5D heating current (A)                   */
	uint16_t current_limit_state;/* 0x5F 1 limiting on / 0 off                 */
	uint16_t current_limit_da;   /* 0x60 current limit x10 (0.1 A, signed)     */
	uint16_t rtc_year;           /* 0x61 RTC absolute year (2000 + value)      */
	uint16_t rtc_month;          /* 0x61 RTC month                             */
	uint16_t rtc_day;            /* 0x62 RTC day                               */
	uint16_t rtc_hour;           /* 0x62 RTC hour                              */
	uint16_t rtc_minute;         /* 0x63 RTC minute                            */
	uint16_t rtc_second;         /* 0x63 RTC second                            */
	uint16_t remaining_charge_min;/* 0x64 remaining charge time (min)          */
	uint16_t dido_status;        /* 0x65 DI1..8 low byte / DO1..8 high byte    */
	uint16_t wake_source_flags;  /* 0x6B wake-up source bitmask                */
	uint16_t comm_interface_type;/* 0x7E 1 = RS485, 2 = UART                   */
	uint16_t cell_voltage_mv[BMS_MAX_CELL_COUNT];    /* 0x00..0x0F per-cell mV */
	uint16_t temperatures_c[BMS_MAX_TEMP_SENSORS];   /* 0x30..0x37 NTC (signed)*/
	uint16_t balance_position[3];/* 0x4F..0x51 per-cell balance bitmask        */
	uint16_t fault_codes[7];     /* 0x6D..0x73 fault/alarm code words          */
} modbus_bms_stats_map_t;

/* Logical Modbus address of a layout member. Constant expression, so it is
 * usable as a switch case label; for an array member it yields the address of
 * the first element. */
#define BMS_REG(member) \
	((uint16_t)(MODBUS_BMS_STATS_ADDR_BASE + \
	            offsetof(modbus_bms_stats_map_t, member) / sizeof(uint16_t)))

/* Register span of an array member. Tracks the layout automatically. */
#define BMS_ARR_LEN(member) \
	(sizeof(((modbus_bms_stats_map_t*)0)->member) / sizeof(uint16_t))

/* Register count the layout occupies. Add or remove a field and this follows,
 * with no edit here. */
#define BMS_STATS_REG_COUNT   (sizeof(modbus_bms_stats_map_t) / sizeof(uint16_t))

/* Block bounds — deliberately free of member names so reordering or renaming
 * fields can never desynchronise them. */
#define BMS_STATS_ADDR_FIRST   ((uint16_t)MODBUS_BMS_STATS_ADDR_BASE)
#define BMS_STATS_ADDR_LAST    \
	((uint16_t)(MODBUS_BMS_STATS_ADDR_BASE + BMS_STATS_REG_COUNT - 1U))

/* Round a physical float to an unsigned 0.1-unit register, saturating. */
static uint16_t bms_f_to_u16_x10(float value)
{
	if (value <= 0.0f) {
		return 0U;
	}
	float scaled = (value * 10.0f) + 0.5f;
	if (scaled > 65535.0f) {
		return 65535U;
	}
	return (uint16_t)scaled;
}

/* Round a physical float to a signed 0.1-unit register (two's-complement bit
 * pattern), saturating to the int16_t range. */
static uint16_t bms_f_to_i16_x10(float value)
{
	float scaled = value * 10.0f;
	scaled += (scaled >= 0.0f) ? 0.5f : -0.5f;
	if (scaled > 32767.0f) {
		scaled = 32767.0f;
	}
	if (scaled < -32768.0f) {
		scaled = -32768.0f;
	}
	return (uint16_t)(int16_t)scaled;
}

bool modbus_bms_stats_read(uint16_t reg_addr, uint16_t* value)
{
	/* Outside the block: not our register, let the caller try other handlers. */
	if ((reg_addr < BMS_STATS_ADDR_FIRST) || (reg_addr > BMS_STATS_ADDR_LAST)) {
		return false;
	}

	/* Tear-free snapshot of the latest decoded BMS record, taken once per call
	 * so every register of a bulk read sees the same sample. The `valid` and
	 * `soh_valid` registers expose parse status so a master can qualify the
	 * rest of the block. */
	bms_data_t d;
	bms_reader_get_data(&d);

	/* Array sub-blocks resolved by base-relative index. */
	const uint16_t cell_base  = BMS_REG(cell_voltage_mv);
	const uint16_t temp_base  = BMS_REG(temperatures_c);
	const uint16_t bal_base   = BMS_REG(balance_position);
	const uint16_t fault_base = BMS_REG(fault_codes);

	if ((reg_addr >= cell_base) &&
	    (reg_addr < (uint16_t)(cell_base + BMS_ARR_LEN(cell_voltage_mv)))) {
		*value = d.cell_voltage_mv[reg_addr - cell_base];
		return true;
	}
	if ((reg_addr >= temp_base) &&
	    (reg_addr < (uint16_t)(temp_base + BMS_ARR_LEN(temperatures_c)))) {
		*value = (uint16_t)d.temperatures_celsius[reg_addr - temp_base]; /* signed */
		return true;
	}
	if ((reg_addr >= bal_base) &&
	    (reg_addr < (uint16_t)(bal_base + BMS_ARR_LEN(balance_position)))) {
		*value = d.balance_position[reg_addr - bal_base];
		return true;
	}
	if ((reg_addr >= fault_base) &&
	    (reg_addr < (uint16_t)(fault_base + BMS_ARR_LEN(fault_codes)))) {
		*value = d.fault_codes[reg_addr - fault_base];
		return true;
	}

	switch (reg_addr)
	{
		case BMS_REG(valid):              *value = d.is_data_valid ? 1U : 0U;               return true;
		case BMS_REG(soh_valid):          *value = d.is_soh_valid ? 1U : 0U;                return true;
		case BMS_REG(total_voltage_dv):   *value = bms_f_to_u16_x10(d.total_voltage_v);     return true;
		case BMS_REG(current_da):         *value = bms_f_to_i16_x10(d.current_a);           return true; /* signed */
		case BMS_REG(soc_x10):            *value = bms_f_to_u16_x10(d.soc_percent);         return true;
		case BMS_REG(soh_x10):            *value = bms_f_to_u16_x10(d.soh_percent);         return true;
		case BMS_REG(life_heartbeat):     *value = d.life_heartbeat;                        return true;
		case BMS_REG(active_cell_count):  *value = (uint16_t)d.active_cell_count;           return true;
		case BMS_REG(temp_sensor_count):  *value = (uint16_t)d.temp_sensor_count;           return true;
		case BMS_REG(max_cell_mv):        *value = d.max_cell_mv;                           return true;
		case BMS_REG(max_cell_index):     *value = d.max_cell_index;                        return true;
		case BMS_REG(min_cell_mv):        *value = d.min_cell_mv;                           return true;
		case BMS_REG(min_cell_index):     *value = d.min_cell_index;                        return true;
		case BMS_REG(cell_diff_mv):       *value = d.cell_diff_mv;                          return true;
		case BMS_REG(max_temp_c):         *value = (uint16_t)d.max_temp_celsius;            return true; /* signed */
		case BMS_REG(max_temp_index):     *value = d.max_temp_index;                        return true;
		case BMS_REG(min_temp_c):         *value = (uint16_t)d.min_temp_celsius;            return true; /* signed */
		case BMS_REG(min_temp_index):     *value = d.min_temp_index;                        return true;
		case BMS_REG(temp_diff_c):        *value = (uint16_t)d.temp_diff_celsius;           return true; /* signed */
		case BMS_REG(work_state):         *value = (uint16_t)d.work_state;                  return true;
		case BMS_REG(charger_status):     *value = d.charger_status;                        return true;
		case BMS_REG(load_status):        *value = d.load_status;                           return true;
		case BMS_REG(remaining_cap_dah):  *value = bms_f_to_u16_x10(d.remaining_cap_ah);    return true;
		case BMS_REG(cycle_count):        *value = d.cycle_count;                           return true;
		case BMS_REG(balance_state):      *value = d.balance_state;                         return true;
		case BMS_REG(mos_status_flags):   *value = d.mos_status_flags;                      return true;
		case BMS_REG(average_voltage_mv): *value = d.average_voltage_mv;                    return true;
		case BMS_REG(power_w):            *value = d.power_w;                               return true;
		case BMS_REG(energy_wh):          *value = d.energy_wh;                             return true;
		case BMS_REG(mos_temp_c):         *value = (uint16_t)d.mos_temperature_celsius;     return true; /* signed */
		case BMS_REG(ambient_temp_c):     *value = (uint16_t)d.ambient_temperature_celsius; return true; /* signed */
		case BMS_REG(heating_temp_c):     *value = (uint16_t)d.heating_temperature_celsius; return true; /* signed */
		case BMS_REG(heating_current_a):  *value = d.heating_current_a;                     return true;
		case BMS_REG(current_limit_state):*value = d.current_limit_state;                   return true;
		case BMS_REG(current_limit_da):   *value = bms_f_to_i16_x10(d.current_limit_a);     return true; /* signed */
		case BMS_REG(rtc_year):           *value = (uint16_t)(2000U + (uint16_t)d.rtc.year);return true;
		case BMS_REG(rtc_month):          *value = (uint16_t)d.rtc.month;                   return true;
		case BMS_REG(rtc_day):            *value = (uint16_t)d.rtc.day;                     return true;
		case BMS_REG(rtc_hour):           *value = (uint16_t)d.rtc.hour;                    return true;
		case BMS_REG(rtc_minute):         *value = (uint16_t)d.rtc.minute;                  return true;
		case BMS_REG(rtc_second):         *value = (uint16_t)d.rtc.second;                  return true;
		case BMS_REG(remaining_charge_min):*value = d.remaining_charge_min;                 return true;
		case BMS_REG(dido_status):        *value = d.dido_status;                           return true;
		case BMS_REG(wake_source_flags):  *value = d.wake_source_flags;                     return true;
		case BMS_REG(comm_interface_type):*value = d.comm_interface_type;                   return true;
		default:
			/* Dense layout: only reachable if a member loses its case. Return 0
			 * so a bulk read of the block never raises exception 0x02. */
			*value = 0U;
			return true;
	}
}
