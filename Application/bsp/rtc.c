/*
 * rtc.c
 *
 *  Created on: Mar 31, 2026
 *      Author: fatih.ozcan
 *
 * Hybrid software/hardware RTC service. See rtc.h for the design overview.
 */
#include "rtc.h"
#include "bsp.h"
#include "main.h"
#include "datetime.h"
#include <stddef.h>

/* Hardware RTC handle defined in main.c. */
extern RTC_HandleTypeDef hrtc;

/* Marker written to a backup register once the hardware RTC has been set to a
 * real time. It survives a system reset (and VBAT) but is lost on a backup
 * domain reset, which is exactly the "is the calendar trustworthy?" semantic. */
#define RTC_HW_VALID_MAGIC  (0x32F2U)

_Static_assert(sizeof(rtc_t) == sizeof(bsp_rtc_t),
    "rtc_t and bsp_rtc_t size mismatch");

_Static_assert(offsetof(rtc_t, millisec) == offsetof(bsp_rtc_t, millisec) &&
               offsetof(rtc_t, second)   == offsetof(bsp_rtc_t, second)   &&
               offsetof(rtc_t, minute)   == offsetof(bsp_rtc_t, minute)   &&
               offsetof(rtc_t, hour)     == offsetof(bsp_rtc_t, hour)     &&
               offsetof(rtc_t, day)      == offsetof(bsp_rtc_t, day)      &&
               offsetof(rtc_t, month)    == offsetof(bsp_rtc_t, month)    &&
               offsetof(rtc_t, year)     == offsetof(bsp_rtc_t, year),
    "rtc_t and bsp_rtc_t layout mismatch");



void rtc_set(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year)
{
	bsp_set_rtc(second, minute, hour, day, month, year);
}

rtc_t rtc_now(void)
{
	bsp_rtc_t bsp_dt = bsp_get_datetime();
	return *(const rtc_t *)&bsp_dt; /* bsp_rtc_t and rtc_t must have identical layout */
}

const char * rtc_get_now_str(void)
{
	return bsp_get_rtc_str();
}

void rtc_print_now(void)
{
	bsp_print_datetime();
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/* Update the epoch counter (Unix seconds since 1970) from a calendar time. */
static void rtc_update_epoch(const rtc_t *dt)
{
	datetime_t edt = {0};

	edt.date.year   = (uint16_t)(2000U + (uint16_t)dt->year);
	edt.date.month  = dt->month;
	edt.date.day    = dt->day;
	edt.time.hour   = dt->hour;
	edt.time.minute = dt->minute;
	edt.time.second = dt->second;

	bsp_set_epoch_time(dt_conv_to_epoch(&edt));
}

/* Copy a calendar time into the software RTC (calendar + milliseconds). */
static void rtc_load_sw(const rtc_t *dt)
{
	bsp_set_rtc(dt->second, dt->minute, dt->hour, dt->day, dt->month, dt->year);
	bsp_set_rtc_milisec(dt->millisec);
}

/* ------------------------------------------------------------------ */
/*  Hardware RTC access                                               */
/* ------------------------------------------------------------------ */

bool rtc_hw_is_valid(void)
{
	return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == (uint32_t)RTC_HW_VALID_MAGIC);
}

void rtc_hw_read(rtc_t *out)
{
	RTC_TimeTypeDef t = {0};
	RTC_DateTypeDef d = {0};

	if (out == NULL)
	{
		return;
	}

	/* GetTime locks the shadow registers; GetDate MUST follow to unlock them. */
	(void)HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
	(void)HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

	out->hour   = t.Hours;
	out->minute = t.Minutes;
	out->second = t.Seconds;

	/* Sub-second to milliseconds: ms = (PREDIV_S - SSR) * 1000 / (PREDIV_S + 1). */
	if (t.SecondFraction != 0U)
	{
		uint32_t ms = ((t.SecondFraction - t.SubSeconds) * 1000U) / (t.SecondFraction + 1U);
		out->millisec = (uint16_t)ms;
	}
	else
	{
		out->millisec = 0U;
	}

	out->day   = d.Date;
	out->month = d.Month;
	out->year  = d.Year;
}

void rtc_hw_write(const rtc_t *dt)
{
	RTC_TimeTypeDef t = {0};
	RTC_DateTypeDef d = {0};

	if (dt == NULL)
	{
		return;
	}

	t.Hours          = dt->hour;
	t.Minutes        = dt->minute;
	t.Seconds        = dt->second;
	t.TimeFormat     = RTC_HOURFORMAT12_AM;
	t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	t.StoreOperation = RTC_STOREOPERATION_RESET;
	(void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);

	/* Weekday is not tracked by the software RTC; store a valid placeholder. */
	d.WeekDay = RTC_WEEKDAY_MONDAY;
	d.Month   = dt->month;
	d.Date    = dt->day;
	d.Year    = dt->year;
	(void)HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, (uint32_t)RTC_HW_VALID_MAGIC);
}

/* ------------------------------------------------------------------ */
/*  Unified sync                                                      */
/* ------------------------------------------------------------------ */

void rtc_sync(const rtc_t *dt)
{
	if (dt == NULL)
	{
		return;
	}

	rtc_load_sw(dt);     /* millisecond software RTC (logging)   */
	rtc_hw_write(dt);    /* persistent hardware RTC + marker     */
	rtc_update_epoch(dt);/* epoch counter (wall-clock seconds)   */
}

void rtc_resync_sw_from_hw(void)
{
	rtc_t hw = {0};

	rtc_hw_read(&hw);
	rtc_load_sw(&hw);
	rtc_update_epoch(&hw);
}

void rtc_boot_sync(void)
{
	if (rtc_hw_is_valid())
	{
		rtc_resync_sw_from_hw();
	}
	/* Else: keep the software RTC power-on default and leave the hardware RTC
	 * untouched until a real time source provides a time via rtc_sync(). */
}

/* ------------------------------------------------------------------ */
/*  Epoch helpers                                                     */
/* ------------------------------------------------------------------ */

uint32_t rtc_get_epoch(void)
{
	return bsp_get_epoch_time();
}

uint32_t rtc_get_unix_epoch(void)
{
	/* The epoch counter is already 1970-based, so this is identical. */
	return bsp_get_epoch_time();
}



