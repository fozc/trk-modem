/*
 * datetime.h
 *
 *  Created on: 27 Eki 2025
 *      Author: fatih
 */

#ifndef DATETIME_H_
#define DATETIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

//#define ENABLE_64BIT_TIME

#define DAYS_FROM_0001_TO_UNIX_EPOCH   719561
#define DAYS_FROM_UNIX_EPOCH_TO_2025   20089
#define DAYS_FROM_0001_TO_2025         (DAYS_FROM_0001_TO_UNIX_EPOCH + DAYS_FROM_UNIX_EPOCH_TO_2025)

#define ONE_MIN_SECONDS   (60)
#define ONE_HOUR_SECONDS  (ONE_MIN_SECONDS*ONE_MIN_SECONDS)
#define ONE_DAY_SECONDS   (24*ONE_HOUR_SECONDS)
#define ONE_MONTH_SECONDS (30*ONE_DAY_SECONDS)
#define ONE_YEAR_SECONDS  (12*ONE_MONTH_SECONDS)

#define SECONDS_FROM_1970_TO_2025  1735689600UL

typedef union
{
    uint32_t raw;
    struct
	{
        uint32_t sec   : 6;  // 0-59
        uint32_t min   : 6;  // 0-59
        uint32_t hour  : 5;  // 0-23
        uint32_t day   : 5;  // 1-31
        uint32_t month : 4;  // 1-12
        uint32_t year  : 6;  // 0-63 (2025 + year)
    };
}packet_date_t;

typedef struct __attribute__((__packed__))
{
	uint16_t year;
	uint8_t month;
	uint8_t day;
}date_t;

typedef struct __attribute__((__packed__))
{
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t day_of_week;
	uint16_t milli;
}timee_t;

typedef struct __attribute__((__packed__))
{
	date_t date;
	timee_t time;
}datetime_t;

/* epoch = standard Unix epoch: January 1st, 1970 00:00:00 UTC */
#ifdef ENABLE_64BIT_TIME
typedef int64_t time64_t;
time64_t dt_conv_to_unix64(const datetime_t *date);
void     dt_conv_from_unix64(time64_t t, datetime_t *date);
#else
typedef uint32_t time32_t;
time32_t dt_conv_to_epoch(const datetime_t *date);  /* datetime -> seconds since 1970 Unix epoch */
void     dt_conv_from_epoch(time32_t t, datetime_t *dt); /* seconds since 1970 Unix epoch -> datetime */
uint32_t dt_conv_to_unix(const datetime_t *date);   /* datetime -> seconds since 1970 Unix epoch (alias) */
#endif

void       dt_conv_to_str(datetime_t dt, char *buff);
void       dt_conv_time_to_str(datetime_t dt, char *buff);
datetime_t dt_conv_from_elapsed(uint32_t sec);

int dt_init(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, datetime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* DATETIME_H_ */
