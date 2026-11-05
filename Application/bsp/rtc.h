/*
 * rtc.h
 *
 *  Created on: Mar 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
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

/** Accepted external time source. Internal hardware readback is not a new update. */
typedef enum
{
    RTC_SOURCE_NONE = 0,
    RTC_SOURCE_UNKNOWN,
    RTC_SOURCE_NTP,
    RTC_SOURCE_GSM,
    RTC_SOURCE_IEC104,
    RTC_SOURCE_SHELL,
    RTC_SOURCE_WEB
} rtc_source_t;

typedef struct
{
    rtc_source_t source;
    uint32_t timestamp; /**< Accepted calendar as epoch; zero until the first successful sync. */
} rtc_sync_info_t;

/** @brief Latest successful external update since boot; cooperative context only. */
rtc_sync_info_t rtc_get_last_sync(void);
/** @brief Apply a clock update and record its source only after hardware/software acceptance. */
bool rtc_sync_with_source(const rtc_t *dt, rtc_source_t source);

/* ---- Software RTC accessors (millisecond resolution, used for logging) ---- */
void rtc_set(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year);
rtc_t rtc_now(void);
uint8_t rtc_get_second(void);
uint8_t rtc_get_minute(void);
uint8_t rtc_get_hour(void);
uint8_t rtc_get_day(void);
uint8_t rtc_get_month(void);
uint8_t rtc_get_year(void);

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
 * @note Inconsistent hardware subseconds leave the destination unchanged.
 */
void rtc_hw_read(rtc_t *out);

/**
 * @brief  Write the hardware RTC calendar and set the valid-time marker.
 * @param[in] dt  Source time structure. Ignored if NULL.
 */
/* False on NULL or HAL failure; failed writes clear the valid marker. */
bool rtc_hw_write(const rtc_t *dt);

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
/* True only after calendar acceptance and successful hardware writes. */
bool rtc_sync(const rtc_t *dt);

/**
 * @brief  Reload the software RTC (and epoch) from the hardware RTC.
 *
 * Used at boot and by the periodic anti-drift resync.
 * Invalid hardware calendar/subseconds preserve software time and epoch.
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

/* ---- TAMP backup registers ---- */

/* Shared allocation with the modem bootloader; keep both projects in sync.
 * DR0:    RTC calendar-valid marker (rtc.c).
 * DR1:    HardFault magic; DR2..5: PC, LR, CFSR, HFSR (hardfault_handler.h).
 * DR6:    Trial-alive marker / no-start count (app_ipc/boot_ipc.h).
 * DR7:    Bootloader one-shot LZ test marker (bootloader lz_test_aborts.h).
 * DR8:    Periodic reset marker; written immediately before software reset,
 *         consumed and cleared by elog_log_reset_cause() on the next boot.
 * DR9..31: Unallocated in the application and bootloader sources.
 * Backup-domain reset or loss of backup power can erase these markers.
 */
#define RTC_BKPR_PERIODIC_RESET_DR     8U
#define RTC_BKPR_PERIODIC_RESET_MAGIC  0x50525354U /* "PRST" */

/**
 * @brief  Write an allocated TAMP backup register (DR1..6 or DR8).
 * @note   Plain register access, safe to call from fault context.
 */
void rtc_bkpr_write(uint32_t dr, uint32_t value);

/**
 * @brief  Read an allocated TAMP backup register (DR1..6 or DR8; 0 for others).
 */
uint32_t rtc_bkpr_read(uint32_t dr);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H_ */
