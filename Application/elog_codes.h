/*
 * elog_codes.h
 *
 *  Created on: Jan 10, 2026
 *      Author: fatih
 */

#ifndef ELOG_CODES_H_
#define ELOG_CODES_H_

#include <stdint.h>


typedef enum
{
	ELOG_CODE_OK = 0,
	ELOG_CODE_ERROR = 1,
	ELOG_CODE_NO_SPACE = 2,
	ELOG_CODE_INVALID_PARAM = 3,
	ELOG_CODE_READ_FAIL = 4,
	ELOG_CODE_WRITE_FAIL = 5,
	ELOG_CODE_ERASE_FAIL = 6,
	ELOG_CODE_NOT_INITIALIZED = 7,
	ELOG_CODE_ALREADY_INITIALIZED = 8,
	ELOG_CODE_CORRUPTED = 9,

	// GSM specific codes
	ELOG_GSM_EVENT_SIMCARD_CHANGED = 20,
	ELOG_GSM_EVENT_SIGNAL_LOST = 21,
	ELOG_GSM_EVENT_MODEM_RESET = 22,
	ELOG_GSM_WTD_SOFT_RECOVERY = 23,
	ELOG_GSM_WTD_HARD_RESET = 24,
	ELOG_GSM_INIT_EXHAUSTED = 25,

	// Configuration change codes
	ELOG_CONFIG_DEVICE_CHANGED = 30,
	ELOG_CONFIG_IEC104_CHANGED = 31,
	ELOG_CONFIG_MODBUS_CHANGED = 32,
	ELOG_CONFIG_RF_CHANGED     = 33,

	// System codes (40..49)
	ELOG_SYSTEM_RESET_CAUSE    = 40,  /* info: flags(4) raw_csr(4) abnormal(1) */
	ELOG_SYSTEM_NVRAM_RECOVERED = 41, /* info: action(1) stored_crc(4) calc_crc(4) */
	ELOG_SYSTEM_FW_UPDATE      = 42,  /* info: source(1) result(1) size(4) */
	ELOG_SYSTEM_HARDFAULT      = 43,  /* info: pc(4) lr(4) cfsr(4) hfsr(4) */
	ELOG_SYSTEM_FW_APPROVED    = 44,  /* info: - (approval IPC sent) */

	// Power / battery codes (50..59)
	ELOG_PWR_ALARM             = 50,  /* info: latch(1) live(1) sys_fault(1) bq0(1) bq1(1) rising(1) */
	ELOG_BAT_STATE             = 51,  /* info: src(1) event(1) a(1) b(1) soc(1) soh(1) */

	// Communication codes (60..69)

	// Audit codes (70..79)
	ELOG_WEB_LOGIN_FAIL        = 70,  /* info: ip(4) burst_count(2) */
	//
} elog_code_t;

static inline const char* elog_code_to_string(elog_code_t code)
{
	switch(code) 
	{
		case ELOG_CODE_OK: return "-";
		case ELOG_CODE_ERROR: return "ERROR";
		case ELOG_CODE_NO_SPACE: return "NO_SPACE";
		case ELOG_CODE_INVALID_PARAM: return "INVALID_PARAM";
		case ELOG_CODE_READ_FAIL: return "READ_FAIL";
		case ELOG_CODE_WRITE_FAIL: return "WRITE_FAIL";
		case ELOG_CODE_ERASE_FAIL: return "ERASE_FAIL";
		case ELOG_CODE_NOT_INITIALIZED: return "NOT_INITIALIZED";
		case ELOG_CODE_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
		case ELOG_CODE_CORRUPTED: return "CORRUPTED";

		// GSM specific codes
		case ELOG_GSM_EVENT_SIMCARD_CHANGED: return "GSM_EVENT_SIMCARD_CHANGED";
		case ELOG_GSM_EVENT_SIGNAL_LOST: return "GSM_EVENT_SIGNAL_LOST";
		case ELOG_GSM_EVENT_MODEM_RESET: return "GSM_EVENT_MODEM_RESET";
		case ELOG_GSM_WTD_SOFT_RECOVERY: return "GSM_WTD_SOFT_RECOVERY";
		case ELOG_GSM_WTD_HARD_RESET: return "GSM_WTD_HARD_RESET";
		case ELOG_GSM_INIT_EXHAUSTED: return "GSM_INIT_EXHAUSTED";

		case ELOG_CONFIG_DEVICE_CHANGED: return "CONFIG_DEVICE";
		case ELOG_CONFIG_IEC104_CHANGED: return "CONFIG_IEC104";
		case ELOG_CONFIG_MODBUS_CHANGED: return "CONFIG_MODBUS";
		case ELOG_CONFIG_RF_CHANGED:     return "CONFIG_RF";

		case ELOG_SYSTEM_RESET_CAUSE:     return "RESET_CAUSE";
		case ELOG_SYSTEM_NVRAM_RECOVERED: return "NVRAM_RECOVERED";
		case ELOG_SYSTEM_FW_UPDATE:       return "FW_UPDATE";
		case ELOG_SYSTEM_HARDFAULT:       return "HARDFAULT";
		case ELOG_SYSTEM_FW_APPROVED:     return "FW_APPROVED";
		case ELOG_PWR_ALARM:              return "PWR_ALARM";
		case ELOG_BAT_STATE:              return "BAT_STATE";
		case ELOG_WEB_LOGIN_FAIL:         return "WEB_LOGIN_FAIL";

		default: return "UNKNOWN_CODE";
	}
}

#endif /* ELOG_CODES_H_ */
