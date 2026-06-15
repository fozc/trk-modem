/*
 * rtc.h
 *
 *  Created on: Mar 31, 2026
 *      Author: fatih.ozcan
 *
 * Hybrid software/hardware RTC service.
 *
 * The software RTC (driven by the SysTick handler in bsp.c) provides
 * millisecond resolution for logging. The hardware RTC (STM32 calendar,
 * backed by VBAT/LSE) provides a persistent time source across resets.
 *
 * - At boot the software RTC is seeded from the hardware RTC (if valid).
 * - Every external time source (NTP, GSM, IEC104) updates BOTH clocks
 *   through rtc_sync().
 * - A periodic resync corrects SysTick drift by reloading the software
 *   RTC from the hardware RTC.
 */

#ifndef BSP_RTC_H_
#define BSP_RTC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	uint16_t millisec;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
}rtc_t;

/* ---- Software RTC accessors (millisecond resolution, used for logging) ---- */
void rtc_set(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year);
rtc_t rtc_now(void);
const char * rtc_get_now_str(void);
void rtc_print_now(void);

/* ---- Hardware RTC access (persistent calendar) ---- */

/**
 * @brief  Check whether the hardware RTC holds a previously set, valid time.
 * @return true if a valid-time marker is present in the backup register.
 */
bool rtc_hw_is_valid(void);

/**
 * @brief  Read the current hardware RTC calendar (including sub-second ms).
 * @param[out] out  Destination time structure. Ignored if NULL.
 */
void rtc_hw_read(rtc_t *out);

/**
 * @brief  Write the hardware RTC calendar and set the valid-time marker.
 * @param[in] dt  Source time structure. Ignored if NULL.
 */
void rtc_hw_write(const rtc_t *dt);

/* ---- Unified time set / sync ---- */

/**
 * @brief  Set the system time from an external source.
 *
 * Updates the software RTC, the hardware RTC, and the epoch counter so that
 * all time views stay consistent. This is the single entry point for NTP,
 * GSM and IEC104 clock updates.
 *
 * @param[in] dt  New time. Ignored if NULL.
 */
void rtc_sync(const rtc_t *dt);

/**
 * @brief  Reload the software RTC (and epoch) from the hardware RTC.
 *
 * Used at boot and by the periodic anti-drift resync.
 */
void rtc_resync_sw_from_hw(void);

/**
 * @brief  Boot-time synchronization.
 *
 * If the hardware RTC is valid, seeds the software RTC from it. Otherwise the
 * software RTC keeps its power-on default and the hardware RTC is left
 * untouched until a real time source becomes available.
 */
void rtc_boot_sync(void);

/* ---- Epoch helpers (wall-clock seconds) ---- */

/**
 * @brief  Get seconds since the Unix epoch (1970-01-01 00:00:00 UTC).
 */
uint32_t rtc_get_epoch(void);

/**
 * @brief  Get seconds since the Unix epoch (1970-01-01 00:00:00 UTC).
 * @note   Alias of rtc_get_epoch(); kept for call-site clarity.
 */
uint32_t rtc_get_unix_epoch(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H_ */
