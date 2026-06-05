/*
 * rtc.c
 *
 *  Created on: Mar 31, 2026
 *      Author: fatih.ozcan
 */
#include "rtc.h"
#include "bsp.h"
#include <stddef.h>

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
#ifdef USE_SOFTWARE_RTC
	bsp_set_rtc(second, minute, hour, day, month, year);
#endif
}

rtc_t rtc_now(void)
{
#ifdef USE_SOFTWARE_RTC
	bsp_rtc_t bsp_dt = bsp_get_datetime();
	return *(const rtc_t *)&bsp_dt; /* bsp_rtc_t and rtc_t must have identical layout */
#endif
	return (rtc_t){0};
}

const char * rtc_get_now_str(void)
{
#ifdef USE_SOFTWARE_RTC
	return bsp_get_rtc_str();
#endif
	return "";
}

void rtc_print_now(void)
{
#ifdef USE_SOFTWARE_RTC
	bsp_print_datetime();
#endif
}



