/*
 * test_datetime.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies date construction, formatting, ordering, and epoch conversion.
 */

#include "unity.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "datetime.h"

#define TEST_FORMAT_BUFFER_LEN 64U

unsigned int xsprintf(char *buffer, const char *format, ...)
{
    int result;
    va_list arguments;

    va_start(arguments, format);
    result = vsnprintf(buffer, TEST_FORMAT_BUFFER_LEN, format, arguments);
    va_end(arguments);

    return (result < 0) ? 0U : (unsigned int)result;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_datetime_init_accepts_boundaries_and_populates_fields(void)
{
    datetime_t date_time = {0};

    TEST_ASSERT_EQUAL_INT(0, dt_init(2025U, 1U, 1U, 0U, 0U, 0U,
                                     &date_time));
    TEST_ASSERT_EQUAL_UINT16(2025U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(1U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(1U, date_time.date.day);

    TEST_ASSERT_EQUAL_INT(0, dt_init(UINT16_MAX, 12U, 31U, 23U, 59U,
                                     59U, &date_time));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(23U, date_time.time.hour);
    TEST_ASSERT_EQUAL_UINT8(59U, date_time.time.minute);
    TEST_ASSERT_EQUAL_UINT8(59U, date_time.time.second);
}

void test_datetime_init_rejects_each_out_of_range_field(void)
{
    datetime_t date_time = {0};

    TEST_ASSERT_EQUAL_INT(-1, dt_init(2024U, 1U, 1U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 0U, 1U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 13U, 1U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 0U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 32U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 1U, 24U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 1U, 0U, 60U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 1U, 0U, 0U, 60U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2025U, 1U, 1U, 0U, 0U, 0U,
                                      NULL));
}

void test_datetime_init_validates_month_length_and_leap_year(void)
{
    datetime_t date_time = {0};

    TEST_ASSERT_EQUAL_INT(-1, dt_init(2026U, 2U, 29U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2026U, 4U, 31U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(0, dt_init(2028U, 2U, 29U, 0U, 0U, 0U,
                                     &date_time));
    TEST_ASSERT_EQUAL_INT(-1, dt_init(2100U, 2U, 29U, 0U, 0U, 0U,
                                      &date_time));
    TEST_ASSERT_EQUAL_INT(0, dt_init(2400U, 2U, 29U, 0U, 0U, 0U,
                                     &date_time));
}

void test_datetime_formats_date_and_time(void)
{
    datetime_t date_time = {
        .date = {.year = 2026U, .month = 9U, .day = 28U},
        .time = {.hour = 7U, .minute = 8U, .second = 9U}
    };
    char buffer[TEST_FORMAT_BUFFER_LEN];

    dt_conv_to_str(date_time, buffer);
    TEST_ASSERT_EQUAL_STRING("2026 09 28-07:08:09", buffer);
    dt_conv_time_to_str(date_time, buffer);
    TEST_ASSERT_EQUAL_STRING("07:08:09", buffer);
}

void test_datetime_elapsed_uses_documented_fixed_units(void)
{
    uint32_t elapsed = ONE_YEAR_SECONDS + (2U * ONE_MONTH_SECONDS) +
                       (3U * ONE_DAY_SECONDS) +
                       (4U * ONE_HOUR_SECONDS) +
                       (5U * ONE_MIN_SECONDS) + 6U;
    datetime_t date_time = dt_conv_from_elapsed(elapsed);

    TEST_ASSERT_EQUAL_UINT16(1U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(2U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(3U, date_time.date.day);
    TEST_ASSERT_EQUAL_UINT8(4U, date_time.time.hour);
    TEST_ASSERT_EQUAL_UINT8(5U, date_time.time.minute);
    TEST_ASSERT_EQUAL_UINT8(6U, date_time.time.second);
}

void test_datetime_computes_known_days_of_week(void)
{
    TEST_ASSERT_EQUAL_UINT8(4U, dt_compute_day_of_week(1970U, 1U, 1U));
    TEST_ASSERT_EQUAL_UINT8(4U, dt_compute_day_of_week(2024U, 2U, 29U));
    TEST_ASSERT_EQUAL_UINT8(1U, dt_compute_day_of_week(2026U, 9U, 28U));
}

void test_datetime_compare_orders_every_component(void)
{
    datetime_t base = {
        .date = {.year = 2026U, .month = 9U, .day = 28U},
        .time = {
            .hour = 10U,
            .minute = 20U,
            .second = 30U,
            .milli = 400U
        }
    };
    datetime_t later = base;

    TEST_ASSERT_EQUAL_INT(0, dt_compare_date_time(&base, &later));
    later.date.year++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.date.month++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.date.day++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.time.hour++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.time.minute++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.time.second++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
    later = base;
    later.time.milli++;
    TEST_ASSERT_LESS_THAN_INT(0, dt_compare_date_time(&base, &later));
    TEST_ASSERT_GREATER_THAN_INT(0, dt_compare_date_time(&later, &base));
}

void test_datetime_converts_unix_epoch_and_leap_day(void)
{
    datetime_t date_time;

    dt_conv_from_epoch(0U, &date_time);
    TEST_ASSERT_EQUAL_UINT16(1970U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(1U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(1U, date_time.date.day);
    TEST_ASSERT_EQUAL_UINT8(4U, date_time.time.day_of_week);
    TEST_ASSERT_EQUAL_UINT32(0U, dt_conv_to_epoch(&date_time));

    dt_conv_from_epoch(1709164800UL, &date_time);
    TEST_ASSERT_EQUAL_UINT16(2024U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(2U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(29U, date_time.date.day);
    TEST_ASSERT_EQUAL_UINT32(1709164800UL, dt_conv_to_epoch(&date_time));
    TEST_ASSERT_EQUAL_UINT32(1709164800UL, dt_conv_to_unix(&date_time));

    dt_conv_from_epoch(1790553600UL, &date_time);
    TEST_ASSERT_EQUAL_UINT16(2026U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(9U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(28U, date_time.date.day);
    TEST_ASSERT_EQUAL_UINT32(1790553600UL, dt_conv_to_epoch(&date_time));
}

void test_datetime_round_trip_handles_uint32_maximum_epoch(void)
{
    datetime_t date_time;

    dt_conv_from_epoch(UINT32_MAX, &date_time);

    TEST_ASSERT_EQUAL_UINT16(2106U, date_time.date.year);
    TEST_ASSERT_EQUAL_UINT8(2U, date_time.date.month);
    TEST_ASSERT_EQUAL_UINT8(7U, date_time.date.day);
    TEST_ASSERT_EQUAL_UINT8(6U, date_time.time.hour);
    TEST_ASSERT_EQUAL_UINT8(28U, date_time.time.minute);
    TEST_ASSERT_EQUAL_UINT8(15U, date_time.time.second);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, dt_conv_to_epoch(&date_time));
}

/*** end of file ***/
