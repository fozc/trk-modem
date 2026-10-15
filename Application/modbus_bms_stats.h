/*
 * modbus_bms_stats.h
 *
 *  Created on: Aug 1, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef MODBUS_BMS_STATS_H_
#define MODBUS_BMS_STATS_H_

#include <stdint.h>
#include "bms.h"
#include <stdbool.h>
#include "modbus_map.h"

/* First logical Modbus address of the read-only BMS telemetry block (pack
 * voltage/current, SoC/SoH, per-cell voltages, temperatures, work state, MOS
 * and fault bitfields...). Lives in the holding-register space so it is
 * reachable via FC03. Mirrors modbus_power_stats / modbus_system_stats; the
 * block length is derived from the layout struct in the header, see
 * modbus_bms_stats_read().
 *
 * Wire (PDU) address = MODBUS_BMS_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE
 * (49300 - 40000 = 9300): that is the address a master (e.g. Modbus Poll) must
 * use, exactly like the 49200/9200 split of the power-stats block. The base
 * and length are owned by the fixed map (modbus_map.h). */
#define MODBUS_BMS_STATS_ADDR_BASE   MODBUS_MAP_BMS_BASE

/* Register layout shared by the reader and RO web metadata. */
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

bool modbus_bms_stats_read(uint16_t reg_addr, uint16_t* value);

#endif /* MODBUS_BMS_STATS_H_ */

/*** end of file ***/
