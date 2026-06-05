/*
 * cp56time2a.c
 *
 *  Created on: Mar 8, 2026
 *      Author: fatih
 */
#include "cp56time2a.h"
#include <string.h>
#include "rtc.h"

_Static_assert(sizeof(cp56time2a_t) == 7, "CP56Time2a must be exactly 7 bytes (IEC 60870-5-101/104)");

/* ------------------------------------------------------------------ */
/*  Private: lookup tables                                            */
/* ------------------------------------------------------------------ */

/** Days per month for non-leap and leap years (index 0 unused). */
static const uint8_t s_days_in_month[2][13] = {
    { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },  /* non-leap */
    { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }   /* leap     */
};

/** Sakamoto lookup table for day-of-week calculation. */
static const uint8_t s_dow_table[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

/* ------------------------------------------------------------------ */
/*  Private helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief  Check if a year (offset from 2000) is a leap year.
 * @param  year  Year offset from 2000 (0-99).
 * @return true if leap year, false otherwise.
 */
static bool is_leap_year(uint8_t year)
{
    const uint16_t full_year = CP56TIME2A_YEAR_BASE + (uint16_t)year;
    return ((full_year % 4U == 0U) && (full_year % 100U != 0U)) ||
           (full_year % 400U == 0U);
}

/* ================================================================== */
/*  Millisecond field extraction / encoding helpers                   */
/* ================================================================== */

uint8_t cp56time2a_get_second(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return (uint8_t)(ts_ptr->milliseconds / 1000U);
}

uint16_t cp56time2a_get_ms(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->milliseconds % 1000U;
}

uint8_t cp56time2a_get_minute(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->minute;
}

uint8_t cp56time2a_get_hour(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->hour;
}

uint8_t cp56time2a_get_day(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->day;
}

uint8_t cp56time2a_get_month(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->month;
}

uint8_t cp56time2a_get_year(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return ts_ptr->year;
}

uint16_t cp56time2a_encode_ms(uint8_t second, uint16_t ms)
{
    if ((second >= 60U) || (ms >= 1000U)) {
        return 0U;
    }
    return (uint16_t)((uint16_t)second * 1000U + ms);
}

uint32_t cp56time2a_to_total_ms(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return 0U;
    }
    return (uint32_t)ts_ptr->hour * 3600000UL +
           (uint32_t)ts_ptr->minute * 60000UL +
           (uint32_t)ts_ptr->milliseconds;
}

/* ================================================================== */
/*  Construction / Conversion                                         */
/* ================================================================== */

cp56time2a_t cp56time2a_from_rtc(const bsp_rtc_t *rtc_ptr)
{
    cp56time2a_t ts = {0};

    if (rtc_ptr == NULL) {
        ts.iv_bit = 1U;
        return ts;
    }

    ts.milliseconds = cp56time2a_encode_ms(rtc_ptr->second, rtc_ptr->millisec);
    ts.minute       = rtc_ptr->minute;
    ts.hour         = rtc_ptr->hour;
    ts.day          = rtc_ptr->day;
    ts.month        = rtc_ptr->month;
    ts.year         = rtc_ptr->year;
    ts.dow          = cp56time2a_calc_dow(rtc_ptr->day, rtc_ptr->month, rtc_ptr->year);
    ts.iv_bit       = 0U;
    ts.su_bit       = 0U;

    return ts;
}

bsp_rtc_t cp56time2a_to_rtc(const cp56time2a_t *ts_ptr)
{
    bsp_rtc_t rtc = {0};

    if (ts_ptr == NULL) {
        return rtc;
    }

    rtc.millisec = ts_ptr->milliseconds % 1000U;
    rtc.second   = (uint8_t)(ts_ptr->milliseconds / 1000U);
    rtc.minute   = ts_ptr->minute;
    rtc.hour     = ts_ptr->hour;
    rtc.day      = ts_ptr->day;
    rtc.month    = ts_ptr->month;
    rtc.year     = ts_ptr->year;

    return rtc;
}

cp56time2a_t cp56time2a_now(void)
{
    bsp_rtc_t rtc = bsp_get_datetime();
    return cp56time2a_from_rtc(&rtc);
}

cp56time2a_t cp56time2a_make(uint16_t milliseconds, uint8_t minute, uint8_t hour,
                             uint8_t day, uint8_t dow, uint8_t month, uint8_t year)
{
    cp56time2a_t ts = {0};

    ts.milliseconds = milliseconds;
    ts.minute       = minute;
    ts.hour         = hour;
    ts.day          = day;
    ts.dow          = dow;
    ts.month        = month;
    ts.year         = year;
    ts.iv_bit       = 0U;
    ts.su_bit       = 0U;

    return ts;
}

/* ================================================================== */
/*  Comparison / Difference                                           */
/* ================================================================== */

int32_t cp56time2a_compare(const cp56time2a_t *t1_ptr, const cp56time2a_t *t2_ptr)
{
    if ((t1_ptr == NULL) || (t2_ptr == NULL)) {
        if (t1_ptr == t2_ptr) {
            return 0;
        }
        return (t1_ptr == NULL) ? -1 : 1;
    }

    if (t1_ptr->year != t2_ptr->year)               { return (int32_t)t1_ptr->year - (int32_t)t2_ptr->year; }
    if (t1_ptr->month != t2_ptr->month)              { return (int32_t)t1_ptr->month - (int32_t)t2_ptr->month; }
    if (t1_ptr->day != t2_ptr->day)                  { return (int32_t)t1_ptr->day - (int32_t)t2_ptr->day; }
    if (t1_ptr->hour != t2_ptr->hour)                { return (int32_t)t1_ptr->hour - (int32_t)t2_ptr->hour; }
    if (t1_ptr->minute != t2_ptr->minute)            { return (int32_t)t1_ptr->minute - (int32_t)t2_ptr->minute; }
    if (t1_ptr->milliseconds != t2_ptr->milliseconds){ return (int32_t)t1_ptr->milliseconds - (int32_t)t2_ptr->milliseconds; }

    return 0;
}

/**
 * @brief  Convert a CP56Time2a timestamp to an absolute day count from year 2000.
 * @param  ts_ptr  Pointer to a CP56Time2a timestamp.
 * @return Total days since 2000-01-01.
 */
static uint32_t cp56time2a_to_days(const cp56time2a_t *ts_ptr)
{
    uint32_t days = 0U;

    /* Accumulate full years */
    for (uint8_t y = 0U; y < ts_ptr->year; y++) {
        days += is_leap_year(y) ? 366U : 365U;
    }

    /* Accumulate full months of current year */
    const uint8_t leap = is_leap_year(ts_ptr->year) ? 1U : 0U;
    for (uint8_t m = 1U; m < ts_ptr->month; m++) {
        days += s_days_in_month[leap][m];
    }

    /* Add days within current month */
    days += ts_ptr->day;

    return days;
}

int32_t cp56time2a_diff_ms(const cp56time2a_t *t1_ptr, const cp56time2a_t *t2_ptr)
{
    if ((t1_ptr == NULL) || (t2_ptr == NULL)) {
        return 0;
    }

    const int32_t day_diff = (int32_t)cp56time2a_to_days(t1_ptr) - (int32_t)cp56time2a_to_days(t2_ptr);
    const int32_t ms1      = (int32_t)cp56time2a_to_total_ms(t1_ptr);
    const int32_t ms2      = (int32_t)cp56time2a_to_total_ms(t2_ptr);

    /* day_diff * 86400000 can overflow int32 for large day ranges.
     * This is accurate for ~24 day range (INT32_MAX / 86400000 ≈ 24.8). */
    return day_diff * 86400000L + (ms1 - ms2);
}

/* ================================================================== */
/*  Validation / Status                                               */
/* ================================================================== */

bool cp56time2a_is_valid(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return false;
    }

    if (ts_ptr->iv_bit != 0U) {
        return false;
    }

    if (ts_ptr->milliseconds > CP56TIME2A_MS_MAX) {
        return false;
    }

    if (ts_ptr->minute > CP56TIME2A_MINUTE_MAX) {
        return false;
    }

    if (ts_ptr->hour > CP56TIME2A_HOUR_MAX) {
        return false;
    }

    if ((ts_ptr->month < CP56TIME2A_MONTH_MIN) || (ts_ptr->month > CP56TIME2A_MONTH_MAX)) {
        return false;
    }

    if ((ts_ptr->day < CP56TIME2A_DAY_MIN) ||
        (ts_ptr->day > cp56time2a_days_in_month(ts_ptr->month, ts_ptr->year))) {
        return false;
    }

    if (ts_ptr->year > CP56TIME2A_YEAR_MAX) {
        return false;
    }

    return true;
}

void cp56time2a_set_invalid(cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return;
    }
    ts_ptr->iv_bit = 1U;
}

bool cp56time2a_is_zero(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        return true;
    }

    const cp56time2a_t zero = {0};
    return (memcmp(ts_ptr, &zero, sizeof(cp56time2a_t)) == 0);
}

/* ================================================================== */
/*  Utility                                                           */
/* ================================================================== */

uint8_t cp56time2a_calc_dow(uint8_t day, uint8_t month, uint8_t year)
{
    if ((month < CP56TIME2A_MONTH_MIN) || (month > CP56TIME2A_MONTH_MAX) ||
        (day < CP56TIME2A_DAY_MIN) || (day > CP56TIME2A_DAY_MAX)) {
        return 0U;
    }

    /* Tomohiko Sakamoto's algorithm (returns 0=Sun..6=Sat) */
    uint16_t y = CP56TIME2A_YEAR_BASE + (uint16_t)year;
    if (month < 3U) {
        y--;
    }
    uint16_t dow_raw = (uint16_t)(y + y / 4U - y / 100U + y / 400U +
                                  s_dow_table[month - 1U] + day) % 7U;

    /* Convert from 0=Sun..6=Sat  to  1=Mon..7=Sun (IEC convention) */
    return (dow_raw == 0U) ? 7U : (uint8_t)dow_raw;
}

uint8_t cp56time2a_days_in_month(uint8_t month, uint8_t year)
{
    if ((month < CP56TIME2A_MONTH_MIN) || (month > CP56TIME2A_MONTH_MAX)) {
        return 0U;
    }
    return s_days_in_month[is_leap_year(year) ? 1U : 0U][month];
}

void cp56time2a_print(const cp56time2a_t *ts_ptr)
{
    if (ts_ptr == NULL) {
        CSLOG("CP56Time2a: NULL\r\n");
        return;
    }

    const uint8_t  sec = (uint8_t)(ts_ptr->milliseconds / 1000U);
    const uint16_t ms  = ts_ptr->milliseconds % 1000U;

    CSLOG("CP56Time2a: %02u/%02u/%04u %02u:%02u:%02u.%03u [IV=%u SU=%u DOW=%u]\r\n",
          ts_ptr->day, ts_ptr->month, CP56TIME2A_YEAR_BASE + ts_ptr->year,
          ts_ptr->hour, ts_ptr->minute, sec, ms,
          ts_ptr->iv_bit, ts_ptr->su_bit, ts_ptr->dow);
}

