/*
 * test_cp56time2a.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies IEC 60870 CP56Time2a conversion, validation, and arithmetic.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cp56time2a.h"

static bsp_rtc_t fake_rtc;

bsp_rtc_t bsp_get_datetime(void)
{
    return fake_rtc;
}

void setUp(void)
{
    memset(&fake_rtc, 0, sizeof(fake_rtc));
}

void tearDown(void)
{
}

void test_cp56time2a_encodes_and_extracts_millisecond_field_boundaries(void)
{
    cp56time2a_t timestamp = cp56time2a_make(59999U, 59U, 23U,
                                             31U, 7U, 12U, 99U);

    TEST_ASSERT_TRUE(cp56time2a_is_valid(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(59U, cp56time2a_get_second(&timestamp));
    TEST_ASSERT_EQUAL_UINT16(999U, cp56time2a_get_ms(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(59U, cp56time2a_get_minute(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(23U, cp56time2a_get_hour(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(31U, cp56time2a_get_day(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(12U, cp56time2a_get_month(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(99U, cp56time2a_get_year(&timestamp));
    TEST_ASSERT_EQUAL_UINT16(59999U, cp56time2a_encode_ms(59U, 999U));
    TEST_ASSERT_EQUAL_UINT16(0U, cp56time2a_encode_ms(60U, 0U));
    TEST_ASSERT_EQUAL_UINT16(0U, cp56time2a_encode_ms(0U, 1000U));
}

void test_cp56time2a_accessors_handle_null(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_second(NULL));
    TEST_ASSERT_EQUAL_UINT16(0U, cp56time2a_get_ms(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_minute(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_hour(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_day(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_month(NULL));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_get_year(NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, cp56time2a_to_total_ms(NULL));
}

void test_cp56time2a_total_ms_reaches_end_of_day(void)
{
    cp56time2a_t timestamp = cp56time2a_make(59999U, 59U, 23U,
                                             1U, 1U, 1U, 26U);

    TEST_ASSERT_EQUAL_UINT32(86399999U,
                             cp56time2a_to_total_ms(&timestamp));
}

void test_cp56time2a_rtc_round_trip_preserves_all_time_fields(void)
{
    bsp_rtc_t input = {
        .millisec = 987U,
        .second = 58U,
        .minute = 57U,
        .hour = 23U,
        .day = 28U,
        .month = 9U,
        .year = 26U
    };
    cp56time2a_t timestamp = cp56time2a_from_rtc(&input);
    bsp_rtc_t output = cp56time2a_to_rtc(&timestamp);

    TEST_ASSERT_TRUE(cp56time2a_is_valid(&timestamp));
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.dow);
    TEST_ASSERT_EQUAL_MEMORY(&input, &output, sizeof(input));
}

void test_cp56time2a_null_and_invalid_rtc_inputs_are_marked_invalid(void)
{
    bsp_rtc_t invalid_rtc = {0};
    cp56time2a_t null_timestamp = cp56time2a_from_rtc(NULL);
    cp56time2a_t invalid_timestamp = cp56time2a_from_rtc(&invalid_rtc);
    bsp_rtc_t zero_rtc = cp56time2a_to_rtc(NULL);

    TEST_ASSERT_EQUAL_UINT8(1U, null_timestamp.iv_bit);
    TEST_ASSERT_EQUAL_UINT8(1U, invalid_timestamp.iv_bit);
    TEST_ASSERT_EQUAL_MEMORY(&(bsp_rtc_t){0}, &zero_rtc, sizeof(zero_rtc));
}

void test_cp56time2a_rejects_rtc_fields_before_bit_field_assignment(void)
{
    bsp_rtc_t rtc = {
        .millisec = 0U,
        .second = 0U,
        .minute = 0U,
        .hour = 0U,
        .day = 1U,
        .month = 1U,
        .year = 0U
    };
    cp56time2a_t timestamp;

    rtc.second = 60U;
    timestamp = cp56time2a_from_rtc(&rtc);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    TEST_ASSERT_EQUAL_UINT8(0U, timestamp.minute);

    rtc.second = 0U;
    rtc.millisec = 1000U;
    timestamp = cp56time2a_from_rtc(&rtc);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);

    rtc.millisec = 0U;
    rtc.minute = 64U;
    timestamp = cp56time2a_from_rtc(&rtc);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    TEST_ASSERT_EQUAL_UINT8(0U, timestamp.minute);
}

void test_cp56time2a_make_rejects_values_that_bit_fields_would_truncate(void)
{
    cp56time2a_t timestamp;

    timestamp = cp56time2a_make(0U, 64U, 0U, 1U, 0U, 1U, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    TEST_ASSERT_EQUAL_UINT8(0U, timestamp.minute);

    timestamp = cp56time2a_make(0U, 0U, 32U, 1U, 0U, 1U, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    timestamp = cp56time2a_make(0U, 0U, 0U, 32U, 0U, 1U, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    timestamp = cp56time2a_make(0U, 0U, 0U, 1U, 8U, 1U, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    timestamp = cp56time2a_make(0U, 0U, 0U, 1U, 0U, 16U, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    timestamp = cp56time2a_make(0U, 0U, 0U, 1U, 0U, 1U, 128U);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
}

void test_cp56time2a_now_uses_bsp_clock(void)
{
    fake_rtc = (bsp_rtc_t){
        .millisec = 321U,
        .second = 12U,
        .minute = 34U,
        .hour = 5U,
        .day = 28U,
        .month = 9U,
        .year = 26U
    };

    cp56time2a_t timestamp = cp56time2a_now();

    TEST_ASSERT_EQUAL_UINT16(12321U, timestamp.milliseconds);
    TEST_ASSERT_EQUAL_UINT8(fake_rtc.minute, timestamp.minute);
    TEST_ASSERT_EQUAL_UINT8(fake_rtc.hour, timestamp.hour);
    TEST_ASSERT_EQUAL_UINT8(fake_rtc.day, timestamp.day);
}

void test_cp56time2a_validation_rejects_each_invalid_field(void)
{
    cp56time2a_t valid = cp56time2a_make(0U, 0U, 0U,
                                         1U, 1U, 1U, 0U);
    cp56time2a_t candidate;

    TEST_ASSERT_FALSE(cp56time2a_is_valid(NULL));

    candidate = valid;
    candidate.iv_bit = 1U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
    candidate = valid;
    candidate.milliseconds = 60000U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
    candidate = valid;
    candidate.month = 0U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
    candidate = valid;
    candidate.day = 0U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
    candidate = valid;
    candidate.month = 2U;
    candidate.day = 30U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
    candidate = valid;
    candidate.year = 100U;
    TEST_ASSERT_FALSE(cp56time2a_is_valid(&candidate));
}

void test_cp56time2a_calendar_handles_leap_years_and_invalid_months(void)
{
    TEST_ASSERT_EQUAL_UINT8(29U, cp56time2a_days_in_month(2U, 0U));
    TEST_ASSERT_EQUAL_UINT8(29U, cp56time2a_days_in_month(2U, 96U));
    TEST_ASSERT_EQUAL_UINT8(28U, cp56time2a_days_in_month(2U, 99U));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_days_in_month(0U, 26U));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_days_in_month(13U, 26U));
    TEST_ASSERT_EQUAL_UINT8(6U, cp56time2a_calc_dow(1U, 1U, 0U));
    TEST_ASSERT_EQUAL_UINT8(1U, cp56time2a_calc_dow(28U, 9U, 26U));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_calc_dow(0U, 1U, 26U));
    TEST_ASSERT_EQUAL_UINT8(0U, cp56time2a_calc_dow(1U, 13U, 26U));
}

void test_cp56time2a_compare_orders_every_field_and_null(void)
{
    cp56time2a_t base = cp56time2a_make(1000U, 2U, 3U,
                                        4U, 5U, 6U, 26U);
    cp56time2a_t later = base;

    TEST_ASSERT_EQUAL_INT32(0, cp56time2a_compare(&base, &base));
    TEST_ASSERT_EQUAL_INT32(0, cp56time2a_compare(NULL, NULL));
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(NULL, &base));
    TEST_ASSERT_GREATER_THAN_INT32(0, cp56time2a_compare(&base, NULL));

    later.year++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
    later = base;
    later.month++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
    later = base;
    later.day++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
    later = base;
    later.hour++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
    later = base;
    later.minute++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
    later = base;
    later.milliseconds++;
    TEST_ASSERT_LESS_THAN_INT32(0, cp56time2a_compare(&base, &later));
}

void test_cp56time2a_diff_handles_midnight_leap_day_and_reverse(void)
{
    cp56time2a_t before = cp56time2a_make(59900U, 59U, 23U,
                                          28U, 3U, 2U, 24U);
    cp56time2a_t after = cp56time2a_make(100U, 0U, 0U,
                                         29U, 4U, 2U, 24U);

    TEST_ASSERT_EQUAL_INT32(200,
                            cp56time2a_diff_ms(&after, &before));
    TEST_ASSERT_EQUAL_INT32(-200,
                            cp56time2a_diff_ms(&before, &after));
    TEST_ASSERT_EQUAL_INT32(0, cp56time2a_diff_ms(NULL, &after));
}

void test_cp56time2a_invalid_and_zero_helpers_are_null_safe(void)
{
    cp56time2a_t timestamp = {0};

    TEST_ASSERT_TRUE(cp56time2a_is_zero(NULL));
    TEST_ASSERT_TRUE(cp56time2a_is_zero(&timestamp));
    cp56time2a_set_invalid(NULL);
    cp56time2a_set_invalid(&timestamp);
    TEST_ASSERT_EQUAL_UINT8(1U, timestamp.iv_bit);
    TEST_ASSERT_FALSE(cp56time2a_is_zero(&timestamp));
}

/*** end of file ***/
