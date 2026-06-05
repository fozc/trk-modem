/*
 * cp56time2a.h
 *
 *  Created on: Mar 8, 2026
 *      Author: fatih
 */

#ifndef LIBIEC104_CP56TIME2A_H_
#define LIBIEC104_CP56TIME2A_H_

#include <stdint.h>
#include <stdbool.h>
#include "iec104_types.h"
#include "bsp.h"

/* ------------------------------------------------------------------ */
/*  CP56Time2a milliseconds field encoding (IEC 60870-5-101/104)      */
/*                                                                    */
/*  The 16-bit "milliseconds" field (0 - 59999) encodes BOTH seconds  */
/*  and sub-second milliseconds in a single value:                    */
/*    seconds   = milliseconds / 1000   (0..59)                       */
/*    actual_ms = milliseconds % 1000   (0..999)                      */
/*  There is NO separate "second" field in cp56time2a_t.              */
/* ------------------------------------------------------------------ */

#define CP56TIME2A_MS_MAX        59999U   /**< Maximum value for milliseconds field */
#define CP56TIME2A_MINUTE_MAX    59U
#define CP56TIME2A_HOUR_MAX      23U
#define CP56TIME2A_DAY_MIN       1U
#define CP56TIME2A_DAY_MAX       31U
#define CP56TIME2A_MONTH_MIN     1U
#define CP56TIME2A_MONTH_MAX     12U
#define CP56TIME2A_YEAR_MAX      99U      /**< 0-99, offset from 2000 */
#define CP56TIME2A_DOW_MIN       1U       /**< 1=Monday */
#define CP56TIME2A_DOW_MAX       7U       /**< 7=Sunday */
#define CP56TIME2A_YEAR_BASE     2000U    /**< Year field base */

/* ------------------------------------------------------------------ */
/*  Millisecond field extraction / encoding helpers                   */
/* ------------------------------------------------------------------ */

/**
 * @brief  Extract the seconds component from the milliseconds field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Seconds value (0-59).
 */
uint8_t cp56time2a_get_second(const cp56time2a_t *ts_ptr);

/**
 * @brief  Extract the sub-second milliseconds from the milliseconds field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Sub-second milliseconds (0-999).
 */
uint16_t cp56time2a_get_ms(const cp56time2a_t *ts_ptr);

/**
 * @brief  Get the minute field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Minute value (0-59).
 */
uint8_t cp56time2a_get_minute(const cp56time2a_t *ts_ptr);

/**
 * @brief  Get the hour field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Hour value (0-23).
 */
uint8_t cp56time2a_get_hour(const cp56time2a_t *ts_ptr);

/**
 * @brief  Get the day of month field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Day value (1-31).
 */
uint8_t cp56time2a_get_day(const cp56time2a_t *ts_ptr);

/**
 * @brief  Get the month field.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Month value (1-12).
 */
uint8_t cp56time2a_get_month(const cp56time2a_t *ts_ptr);

/**
 * @brief  Get the year field (offset from 2000).
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Year value (0-99).
 */
uint8_t cp56time2a_get_year(const cp56time2a_t *ts_ptr);

/**
 * @brief  Encode second + sub-second ms into the IEC104 milliseconds field.
 * @param  second  Seconds (0-59).
 * @param  ms      Sub-second milliseconds (0-999).
 * @return Encoded milliseconds value (0-59999), or 0 on invalid input.
 */
uint16_t cp56time2a_encode_ms(uint8_t second, uint16_t ms);

/**
 * @brief  Compute total milliseconds elapsed since midnight.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return Total milliseconds since 00:00:00.000 (max ~86399999).
 */
uint32_t cp56time2a_to_total_ms(const cp56time2a_t *ts_ptr);

/* ------------------------------------------------------------------ */
/*  Construction / Conversion                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief  Get the current time as a CP56Time2a timestamp (reads BSP RTC).
 * @return CP56Time2a timestamp with current date/time and computed DOW.
 */
cp56time2a_t cp56time2a_now(void);

/**
 * @brief  Convert a BSP RTC time to CP56Time2a format.
 * @param  rtc_ptr  Pointer to BSP RTC time (must not be NULL).
 * @return CP56Time2a timestamp.
 * @note   Encodes second and millisec into the combined milliseconds field:
 *         milliseconds = rtc_ptr->second * 1000 + rtc_ptr->millisec
 */
cp56time2a_t cp56time2a_from_rtc(const bsp_rtc_t *rtc_ptr);

/**
 * @brief  Convert a CP56Time2a timestamp to BSP RTC format.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return BSP RTC time with decoded second and millisec fields.
 * @note   Decodes: second = milliseconds / 1000, millisec = milliseconds % 1000
 */
bsp_rtc_t cp56time2a_to_rtc(const cp56time2a_t *ts_ptr);

/**
 * @brief  Manually construct a CP56Time2a timestamp from individual fields.
 * @param  milliseconds  Combined seconds+ms value (0-59999).
 * @param  minute        Minute (0-59).
 * @param  hour          Hour (0-23).
 * @param  day           Day of month (1-31).
 * @param  dow           Day of week (1=Mon..7=Sun, 0=not used).
 * @param  month         Month (1-12).
 * @param  year          Year offset from 2000 (0-99).
 * @return CP56Time2a timestamp with iv_bit=0, su_bit=0.
 */
cp56time2a_t cp56time2a_make(uint16_t milliseconds, uint8_t minute, uint8_t hour,
                             uint8_t day, uint8_t dow, uint8_t month, uint8_t year);

/* ------------------------------------------------------------------ */
/*  Comparison / Difference                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief  Compare two CP56Time2a timestamps lexicographically.
 * @param  t1_ptr  Pointer to the first timestamp (must not be NULL).
 * @param  t2_ptr  Pointer to the second timestamp (must not be NULL).
 * @return < 0 if t1 < t2, 0 if equal, > 0 if t1 > t2.
 */
int32_t cp56time2a_compare(const cp56time2a_t *t1_ptr, const cp56time2a_t *t2_ptr);

/**
 * @brief  Compute the difference between two timestamps in milliseconds.
 * @param  t1_ptr  Pointer to the first (later) timestamp.
 * @param  t2_ptr  Pointer to the second (earlier) timestamp.
 * @return Difference in milliseconds (t1 - t2). Positive if t1 > t2.
 *         Accurate for timestamps within approximately 24 days of each other.
 */
int32_t cp56time2a_diff_ms(const cp56time2a_t *t1_ptr, const cp56time2a_t *t2_ptr);

/* ------------------------------------------------------------------ */
/*  Validation / Status                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief  Check whether all fields of a CP56Time2a timestamp are within valid ranges.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return true if all fields are valid and iv_bit == 0, false otherwise.
 */
bool cp56time2a_is_valid(const cp56time2a_t *ts_ptr);

/**
 * @brief  Set the IV (invalid) bit of a CP56Time2a timestamp.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 */
void cp56time2a_set_invalid(cp56time2a_t *ts_ptr);

/**
 * @brief  Check whether all fields of a CP56Time2a timestamp are zero.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 * @return true if all date/time fields are zero (uninitialized timestamp).
 */
bool cp56time2a_is_zero(const cp56time2a_t *ts_ptr);

/* ------------------------------------------------------------------ */
/*  Utility                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief  Calculate day of week using Tomohiko Sakamoto's algorithm.
 * @param  day    Day of month (1-31).
 * @param  month  Month (1-12).
 * @param  year   Year offset from 2000 (0-99).
 * @return Day of week (1=Monday .. 7=Sunday), or 0 on invalid input.
 */
uint8_t cp56time2a_calc_dow(uint8_t day, uint8_t month, uint8_t year);

/**
 * @brief  Get the number of days in a given month.
 * @param  month  Month (1-12).
 * @param  year   Year offset from 2000 (0-99).
 * @return Number of days (28-31), or 0 on invalid month.
 */
uint8_t cp56time2a_days_in_month(uint8_t month, uint8_t year);

/**
 * @brief  Print a CP56Time2a timestamp via CSLOG.
 *         Format: "CP56Time2a: DD/MM/YYYY HH:MM:SS.mmm"
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp (must not be NULL).
 */
void cp56time2a_print(const cp56time2a_t *ts_ptr);

#endif /* LIBIEC104_CP56TIME2A_H_ */
