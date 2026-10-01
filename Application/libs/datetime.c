/*
 * datetime.c
 *
 *  Created on: 27 Eki 2025
 *      Author: fatih
 */
#include "datetime.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stddef.h>

static bool is_leap_year(uint16_t year)
{
    return (((year % 4U) == 0U) && ((year % 100U) != 0U)) ||
           ((year % 400U) == 0U);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    uint8_t days;

    if ((month < 1U) || (month > 12U)) {
        return 0U;
    }

    days = month_days[month - 1U];
    if ((2U == month) && is_leap_year(year)) {
        days = 29U;
    }

    return days;
}

void dt_conv_to_str(datetime_t dt, char *buff)
{
	 xsprintf(buff, "%04u %02u %02u-%02u:%02u:%02u" , dt.date.year, dt.date.month, dt.date.day,
			dt.time.hour, dt.time.minute, dt.time.second);
}

void dt_conv_time_to_str(datetime_t dt, char *buff)
{
	 xsprintf(buff, "%02u:%02u:%02u", dt.time.hour, dt.time.minute, dt.time.second);
}

datetime_t dt_conv_from_elapsed(uint32_t sec)
{
	datetime_t dt;
	dt.date.year = (uint16_t)(sec / ONE_YEAR_SECONDS);
    sec -= (uint32_t)dt.date.year * ONE_YEAR_SECONDS;

	dt.date.month = (uint8_t)(sec / ONE_MONTH_SECONDS);
    sec -= (uint32_t)dt.date.month * ONE_MONTH_SECONDS;

	dt.date.day = (uint8_t)(sec / ONE_DAY_SECONDS);
    sec -= (uint32_t)dt.date.day * ONE_DAY_SECONDS;

	dt.time.hour = (uint8_t)(sec / ONE_HOUR_SECONDS);
    sec -= (uint32_t)dt.time.hour * ONE_HOUR_SECONDS;

	dt.time.minute = (uint8_t)(sec / ONE_MIN_SECONDS);
    sec -= (uint32_t)dt.time.minute * ONE_MIN_SECONDS;

	dt.time.second = (uint8_t)sec;

	return dt;
}

uint8_t dt_compute_day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
	uint32_t  h;
   uint32_t  j;
   uint32_t  k;

   //January and February are counted as months 13 and 14 of the previous year
   if(m <= 2)
   {
      m += 12;
      y -= 1;
   }

   //J is the century
   j = y / 100;
   //K the year of the century
   k = y % 100;

   //Compute H using Zeller's congruence
   h = (uint32_t)d + (26U * ((uint32_t)m + 1U) / 10U) + k +
       (k / 4U) + (5U * j) + (j / 4U);

   //Return the day of the week
   return (uint8_t)(((h + 5U) % 7U) + 1U);
}

int dt_init(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, datetime_t *dt)
{
    if ((NULL == dt) || (year < 2025U) ||
        (day < 1U) || (day > days_in_month(year, month)) ||
        (hour > 23U) || (minute > 59U) || (second > 59U))
    {
        return -1;
    }

    dt->date.year = year;
    dt->date.month = month;
    dt->date.day = day;
    dt->time.hour = hour;
    dt->time.minute = minute;
    dt->time.second = second;

    return 0;
}

int dt_compare_date_time(const datetime_t *date1, const datetime_t *date2)
 {
    int res;

    if(date1->date.year < date2->date.year)
    {
       res = -1;
    }
    else if(date1->date.year > date2->date.year)
    {
       res = 1;
    }
    else if(date1->date.month < date2->date.month)
    {
       res = -1;
    }
    else if(date1->date.month > date2->date.month)
    {
       res = 1;
    }
    else if(date1->date.day < date2->date.day)
    {
       res = -1;
    }
    else if(date1->date.day > date2->date.day)
    {
       res = 1;
    }
    else if(date1->time.hour < date2->time.hour)
    {
       res = -1;
    }
    else if(date1->time.hour > date2->time.hour)
    {
       res = 1;
    }
    else if(date1->time.minute < date2->time.minute)
    {
       res = -1;
    }
    else if(date1->time.minute > date2->time.minute)
    {
       res = 1;
    }
    else if(date1->time.second < date2->time.second)
    {
       res = -1;
    }
    else if(date1->time.second > date2->time.second)
    {
       res = 1;
    }
    else if(date1->time.milli < date2->time.milli)
    {
       res = -1;
    }
    else if(date1->time.milli > date2->time.milli)
    {
       res = 1;
    }
    else
    {
       res = 0;
    }
 
    return res;
 }

#ifndef ENABLE_64BIT_TIME
void dt_conv_from_epoch(time32_t t, datetime_t *dt)
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;

    //Clear milliseconds
    dt->time.milli = 0;

    //Retrieve hours, minutes and seconds
    dt->time.second = (uint8_t)(t % 60U);
    t /= 60U;
    dt->time.minute = (uint8_t)(t % 60U);
    t /= 60U;
    dt->time.hour = (uint8_t)(t % 24U);
    t /= 24U;

    //Convert Unix time (1970-based seconds) to date
    a = (uint32_t) ((4 * t + 102032) / 146097 + 15);
    b = (uint32_t) (t + 2442113 + a - (a / 4));
    c = (20 * b - 2442) / 7305;
    d = b - 365 * c - (c / 4);
    e = d * 1000 / 30601;
    f = d - e * 30 - e * 601 / 1000;

    //January and February are counted as months 13 and 14 of the previous year
    if(e <= 13)
    {
       c -= 4716;
       e -= 1;
    }
    else
    {
       c -= 4715;
       e -= 13;
    }

    //Retrieve year, month and day
    dt->date.year = (uint16_t)c;
    dt->date.month = (uint8_t)e;
    dt->date.day = (uint8_t)f;

    //Calculate day of week
    dt->time.day_of_week = dt_compute_day_of_week(
        (uint16_t)c, (uint8_t)e, (uint8_t)f);
}
 
time32_t dt_conv_to_epoch(const datetime_t *date)
{
   uint32_t y;
   uint32_t m;
   uint32_t d;
   uint32_t t;

   //Year
   y = date->date.year;
   //Month of year
   m = date->date.month;
   //Day of month
   d = date->date.day;

   //January and February are counted as months 13 and 14 of the previous year
   if(m <= 2)
   {
      m += 12;
      y -= 1;
   }

   //Convert years to days
   t = (365U * y) + (y / 4U) - (y / 100U) + (y / 400U);
   //Convert months to days
   t += (30U * m) + (3U * (m + 1U) / 5U) + d;
   //Unix time starts on January 1st, 1970
   t -= DAYS_FROM_0001_TO_UNIX_EPOCH;
   //Convert days to seconds
   t *= 86400U;
   //Add hours, minutes and seconds
   t += (3600U * (uint32_t)date->time.hour) +
        (60U * (uint32_t)date->time.minute) +
        (uint32_t)date->time.second;

   //Return Unix time
   return t;
}

uint32_t dt_conv_to_unix(const datetime_t *date)
{
   /* dt_conv_to_epoch already returns 1970-based Unix seconds. */
   return dt_conv_to_epoch(date);
}
#else
void dt_conv_from_unix64(time64_t t, datetime_t *date)
 {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
  
    //Negative Unix time values are not supported
    if(t < 1)
    {
       t = 0;
    }
  
    //Clear milliseconds
    date->time.milli = 0;
  
    //Retrieve hours, minutes and seconds
    date->time.second = t % 60;
    t /= 60;
    date->time.minute = t % 60;
    t /= 60;
    date->time.hour = t % 24;
    t /= 24;
  
    //Convert Unix time to date
    a = (uint32_t) ((4 * t + 102032) / 146097 + 15);
    b = (uint32_t) (t + 2442113 + a - (a / 4));
    c = (20 * b - 2442) / 7305;
    d = b - 365 * c - (c / 4);
    e = d * 1000 / 30601;
    f = d - e * 30 - e * 601 / 1000;
  
    //January and February are counted as months 13 and 14 of the previous year
    if(e <= 13)
    {
       c -= 4716;
       e -= 1;
    }
    else
    {
       c -= 4715;
       e -= 13;
    }
  
    //Retrieve year, month and day
    date->date.year = c;
    date->date.month = e;
    date->date.day = f;

    //Calculate day of week
    date->time.day_of_week = computeDayOfWeek(c, e, f);
 }

time64_t dt_conv_to_unix64(const datetime_t *date)
 {
    uint32_t y;
    uint32_t m;
    uint32_t d;
    time64_t t;
  
    //Year
    y = date->date.year;
    //Month of year
    m = date->date.month;
    //Day of month
    d = date->date.day;

    //January and February are counted as months 13 and 14 of the previous year
    if(m <= 2)
    {
       m += 12;
       y -= 1;
    }
  
    //Convert years to days
    t = (365 * y) + (y / 4) - (y / 100) + (y / 400);
    //Convert months to days
    t += (30 * m) + (3 * (m + 1) / 5) + d;
    //Unix time starts on January 1st, 1970
    t -= 719561;
    //Convert days to seconds
    t *= 86400;
    //Add hours, minutes and seconds
    t += (3600 * date->time.hour) + (60 * date->time.minute) + date->time.second;
  
    //Return 64-bit Unix time
    return t;
 }

#endif
