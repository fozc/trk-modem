/*
 * modbus_power_stats.c
 *
 *  Created on: 14 Tem 2026
 *      Author: fatih
 */
#include "modbus_power_stats.h"
#include "power_board.h"
#include <stddef.h>

/*
 * PowerBoard telemetry register block (holding space, base
 * MODBUS_PWR_STATS_ADDR_BASE == 49200).
 *
 * modbus_pwr_stats_map_t is a pure LAYOUT descriptor — the same trick as
 * modbus_system_stats: it is never instantiated, we only borrow its member
 * offsets (via offsetof) so each register address drops out of the field order
 * automatically. Change a field's type/size, or insert/remove a field, and
 * every address after it shifts by itself — no hand renumbering of the switch
 * below.
 *
 *   uint16_t member  -> 1 register (UINT16)
 *
 * Every member is a single uint16_t, so the struct carries NO padding: the
 * range is dense and contiguous, which lets a master bulk-read the whole block
 * in a single FC03 window without an exception (0x02). Block bounds (see below)
 * are base-relative and sizeof()-driven, so they track the struct without naming
 * any field — reordering or renaming members can never desynchronise them.
 *
 * Signed source fields (int8_t board/BQ die temperatures, int16_t battery
 * current and battery NTC) are reinterpreted as their two's-complement uint16_t
 * bit pattern; the master reads them back as signed 16-bit (see the cell names
 * in modbus-tools/power_stats.mbp). All other fields are unsigned.
 */
typedef struct {
	uint16_t valid;          /* checksum valid flag (0/1)            */
	uint16_t prot_ver;       /* 0x1E PROT_VER                        */
	uint16_t seq;            /* 0x1D sample counter                  */
	uint16_t rec_flag;       /* 0x1C REC_FLAG bitfield               */
	uint16_t blk_xsum;       /* 0x1F received checksum byte          */
	uint16_t sys_state;      /* 0x00 SYS_STATE                       */
	uint16_t sys_fault;      /* 0x01 SYS_FAULT (BQ FAULT0 mirror)    */
	uint16_t sys_flags;      /* 0x02 SYS_FLAGS (live alarms)         */
	uint16_t soh_x10;        /* 0x04 State-of-health x10 (%)         */
	uint16_t soc_x10;        /* 0x1A State-of-charge x10 (%)         */
	uint16_t vbat_mv;        /* 0x10 Battery voltage (mV)            */
	uint16_t vpv_mv;         /* 0x12 PV voltage (mV)                 */
	uint16_t vdc_mv;         /* 0x14 DC voltage (mV)                 */
	uint16_t ichg_ma;        /* 0x20 Charge current (mA)             */
	uint16_t ibat_ma;        /* 0x56 Battery current (mA, signed)    */
	uint16_t ibus_ma;        /* 0x58 Bus current (mA)                */
	uint16_t board_temp_c;   /* 0x22 Board NTC (degC, signed)        */
	uint16_t batt_temp_x10;  /* 0x24 Battery NTC x10 (degC, signed)  */
	uint16_t batt_ts;        /* 0x23 JEITA zone 0..4                 */
	uint16_t chg_stat;       /* 0x28 Charge phase 0..7               */
	uint16_t chg_phase;      /* 0x32 Charge phase mirror             */
	uint16_t batt_cap_ah;    /* 0x2C Active capacity echo (Ah)       */
	uint16_t bq_fault0;      /* 0x26 BQ REG20 raw                    */
	uint16_t bq_fault1;      /* 0x27 BQ REG21 raw                    */
	uint16_t alarm_live;     /* 0x30 Live alarm bits                 */
	uint16_t alarm_latch;    /* 0x31 Latched alarm bits              */
	uint16_t pwr_io;         /* 0x44 GPIO mirror                     */
	uint16_t bq_vsys_mv;     /* 0x50 BQ VSYS (mV)                    */
	uint16_t bq_vbus_mv;     /* 0x52 BQ VBUS (mV)                    */
	uint16_t bq_vac1_mv;     /* 0x5A BQ VAC1 = DC input (mV)         */
	uint16_t bq_vac2_mv;     /* 0x5C BQ VAC2 = PV input (mV)         */
	uint16_t bq_tdie_c;      /* 0x5E BQ die temperature (degC, sig.) */
} modbus_pwr_stats_map_t;

/* Logical Modbus address of a layout member. Constant expression, so it is
 * usable as a switch case label. */
#define PWR_REG(member) \
	((uint16_t)(MODBUS_PWR_STATS_ADDR_BASE + \
	            offsetof(modbus_pwr_stats_map_t, member) / sizeof(uint16_t)))

/* Register count the layout occupies. Tracks the struct automatically: add or
 * remove a field and this follows, with no edit here. */
#define PWR_STATS_REG_COUNT   (sizeof(modbus_pwr_stats_map_t) / sizeof(uint16_t))

/* Block bounds — deliberately free of member names so reordering or renaming
 * fields can never desynchronise them (see modbus_system_stats for the same
 * reasoning). */
#define PWR_STATS_ADDR_FIRST   ((uint16_t)MODBUS_PWR_STATS_ADDR_BASE)
#define PWR_STATS_ADDR_LAST    \
	((uint16_t)(MODBUS_PWR_STATS_ADDR_BASE + PWR_STATS_REG_COUNT - 1U))

bool modbus_power_stats_read(uint16_t reg_addr, uint16_t* value)
{
	/* Outside the block: not our register, let the caller try other handlers. */
	if ((reg_addr < PWR_STATS_ADDR_FIRST) || (reg_addr > PWR_STATS_ADDR_LAST)) {
		return false;
	}

	/* Tear-free snapshot of the latest telemetry pushed by the PowerBoard, taken
	 * once per call so every register of a bulk read sees the same sample.
	 * Fields stay decoded even on a checksum mismatch; the `valid` register
	 * exposes that status so a master can qualify the rest of the block. */
	power_board_telemetry_t tlm;
	(void)power_board_get_telemetry(&tlm);

	switch (reg_addr)
	{
		case PWR_REG(valid):          *value = tlm.valid ? 1U : 0U;                return true;
		case PWR_REG(prot_ver):       *value = (uint16_t)tlm.prot_ver;             return true;
		case PWR_REG(seq):            *value = (uint16_t)tlm.seq;                  return true;
		case PWR_REG(rec_flag):       *value = (uint16_t)tlm.rec_flag;             return true;
		case PWR_REG(blk_xsum):       *value = (uint16_t)tlm.blk_xsum;             return true;
		case PWR_REG(sys_state):      *value = (uint16_t)tlm.sys_state;            return true;
		case PWR_REG(sys_fault):      *value = (uint16_t)tlm.sys_fault;            return true;
		case PWR_REG(sys_flags):      *value = (uint16_t)tlm.sys_flags;            return true;
		case PWR_REG(soh_x10):        *value = tlm.soh_x10;                        return true;
		case PWR_REG(soc_x10):        *value = tlm.soc_x10;                        return true;
		case PWR_REG(vbat_mv):        *value = tlm.vbat_mv;                        return true;
		case PWR_REG(vpv_mv):         *value = tlm.vpv_mv;                         return true;
		case PWR_REG(vdc_mv):         *value = tlm.vdc_mv;                         return true;
		case PWR_REG(ichg_ma):        *value = tlm.ichg_ma;                        return true;
		case PWR_REG(ibat_ma):        *value = (uint16_t)tlm.ibat_ma;              return true; /* signed */
		case PWR_REG(ibus_ma):        *value = tlm.ibus_ma;                        return true;
		case PWR_REG(board_temp_c):   *value = (uint16_t)(int16_t)tlm.board_temp_c;   return true; /* int8 sign-extended */
		case PWR_REG(batt_temp_x10):  *value = (uint16_t)tlm.batt_temp_x10;        return true; /* signed */
		case PWR_REG(batt_ts):        *value = (uint16_t)tlm.batt_ts;              return true;
		case PWR_REG(chg_stat):       *value = (uint16_t)tlm.chg_stat;             return true;
		case PWR_REG(chg_phase):      *value = (uint16_t)tlm.chg_phase;            return true;
		case PWR_REG(batt_cap_ah):    *value = (uint16_t)tlm.batt_cap_ah;          return true;
		case PWR_REG(bq_fault0):      *value = (uint16_t)tlm.bq_fault0;            return true;
		case PWR_REG(bq_fault1):      *value = (uint16_t)tlm.bq_fault1;            return true;
		case PWR_REG(alarm_live):     *value = (uint16_t)tlm.alarm_live;           return true;
		case PWR_REG(alarm_latch):    *value = (uint16_t)tlm.alarm_latch;          return true;
		case PWR_REG(pwr_io):         *value = (uint16_t)tlm.pwr_io;               return true;
		case PWR_REG(bq_vsys_mv):     *value = tlm.bq_vsys_mv;                     return true;
		case PWR_REG(bq_vbus_mv):     *value = tlm.bq_vbus_mv;                     return true;
		case PWR_REG(bq_vac1_mv):     *value = tlm.bq_vac1_mv;                     return true;
		case PWR_REG(bq_vac2_mv):     *value = tlm.bq_vac2_mv;                     return true;
		case PWR_REG(bq_tdie_c):      *value = (uint16_t)(int16_t)tlm.bq_tdie_c;      return true; /* int8 sign-extended */
		default:
			/* Dense layout: only reachable if a member loses its case. Return 0
			 * so a bulk read of the block never raises exception 0x02. */
			*value = 0U;
			return true;
	}
}
