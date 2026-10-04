/*
 * test_rtc_sync.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real RTC validation and epoch conversion, mocking BSP/HAL boundaries.
 */
#include "unity.h"
#include "rtc.h"
#include "datetime.h"
#include "mock_bsp.h"
#include "mock_stm32u3xx_hal_rtc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

RTC_HandleTypeDef hrtc;
test_tamp_t test_tamp;
static bsp_rtc_t stored;
static uint32_t epoch_value;
static uint32_t write_count;
static uint32_t marker;
static RTC_TimeTypeDef hw_time;
static RTC_DateTypeDef hw_date;

/* Calendar formatting is outside these synchronization scenarios. */
unsigned int xsprintf(char *buffer, const char *format, ...)
{
    (void)buffer;
    (void)format;
    return 0U;
}

static void set_calendar(uint8_t second, uint8_t minute, uint8_t hour,
                         uint8_t day, uint8_t month, uint8_t year,
                         int call_count)
{
    (void)call_count;
    stored.second = second;
    stored.minute = minute;
    stored.hour = hour;
    stored.day = day;
    stored.month = month;
    stored.year = year;
    write_count++;
}

static void set_ms(uint16_t ms, int call_count)
{
    (void)call_count;
    stored.millisec = ms;
    write_count++;
}

static void set_epoch(uint32_t epoch, int call_count)
{
    (void)call_count;
    epoch_value = epoch;
    write_count++;
}

static HAL_StatusTypeDef set_time(RTC_HandleTypeDef *handle,
                                  RTC_TimeTypeDef *value, uint32_t format,
                                  int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_PTR(&hrtc, handle);
    TEST_ASSERT_EQUAL_UINT32(RTC_FORMAT_BIN, format);
    hw_time = *value;
    write_count++;
    return HAL_OK;
}

static HAL_StatusTypeDef set_date(RTC_HandleTypeDef *handle,
                                  RTC_DateTypeDef *value, uint32_t format,
                                  int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_PTR(&hrtc, handle);
    TEST_ASSERT_EQUAL_UINT32(RTC_FORMAT_BIN, format);
    hw_date = *value;
    write_count++;
    return HAL_OK;
}

static void set_marker(RTC_HandleTypeDef *handle, uint32_t reg,
                       uint32_t value, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_PTR(&hrtc, handle);
    TEST_ASSERT_EQUAL_UINT32(RTC_BKP_DR0, reg);
    marker = value;
    write_count++;
}

void setUp(void)
{
    memset(&stored, 0, sizeof(stored));
    stored.year = 26U;
    stored.month = 10U;
    stored.day = 4U;
    epoch_value = 1791062400U;
    marker = 0x1234U;
    memset(&hw_time, 0, sizeof(hw_time));
    memset(&hw_date, 0, sizeof(hw_date));
    write_count = 0U;
    bsp_set_rtc_StubWithCallback(set_calendar);
    bsp_set_rtc_milisec_StubWithCallback(set_ms);
    bsp_set_epoch_time_StubWithCallback(set_epoch);
    HAL_RTC_SetTime_StubWithCallback(set_time);
    HAL_RTC_SetDate_StubWithCallback(set_date);
    HAL_RTCEx_BKUPWrite_StubWithCallback(set_marker);
}

void tearDown(void)
{
}

static rtc_t valid_time(void)
{
    const rtc_t value = {
        .millisec = 999U, .second = 59U, .minute = 59U, .hour = 23U,
        .day = 31U, .month = 12U, .year = 26U
    };
    return value;
}

static void assert_rejected(const rtc_t *value)
{
    const bsp_rtc_t before = stored;
    const uint32_t old_epoch = epoch_value;
    const uint32_t old_marker = marker;
    rtc_sync(value);
    TEST_ASSERT_EQUAL_UINT32(0U, write_count);
    TEST_ASSERT_EQUAL_MEMORY(&before, &stored, sizeof(stored));
    TEST_ASSERT_EQUAL_UINT32(old_epoch, epoch_value);
    TEST_ASSERT_EQUAL_UINT32(old_marker, marker);
}

void test_rtc_sync_valid_time_updates_all_views(void)
{
    const rtc_t value = valid_time();
    rtc_sync(&value);
    TEST_ASSERT_EQUAL_UINT32(6U, write_count);
    TEST_ASSERT_EQUAL_UINT16(value.millisec, stored.millisec);
    TEST_ASSERT_EQUAL_UINT8(value.month, stored.month);
    TEST_ASSERT_EQUAL_UINT32(1798761599U, epoch_value);
    TEST_ASSERT_EQUAL_UINT8(value.hour, hw_time.Hours);
    TEST_ASSERT_EQUAL_UINT8(value.second, hw_time.Seconds);
    TEST_ASSERT_EQUAL_UINT8(value.month, hw_date.Month);
    TEST_ASSERT_EQUAL_UINT8(value.day, hw_date.Date);
    TEST_ASSERT_EQUAL_UINT8(value.year, hw_date.Year);
    TEST_ASSERT_EQUAL_UINT32(0x32F2U, marker);
}

void test_rtc_sync_null_preserves_all_state(void)
{
    assert_rejected(NULL);
}

void test_rtc_sync_invalid_months_preserve_all_state(void)
{
    rtc_t value = valid_time();
    const uint8_t months[] = {0U, 13U, 255U};
    for (size_t index = 0U; index < sizeof(months); index++)
    {
        value.month = months[index];
        assert_rejected(&value);
    }
}

void test_rtc_sync_invalid_days_preserve_all_state(void)
{
    rtc_t value = valid_time();
    value.day = 0U;
    assert_rejected(&value);
    value.day = 32U;
    assert_rejected(&value);
    value.month = 4U;
    value.day = 31U;
    assert_rejected(&value);
}

void test_rtc_sync_non_leap_february_29_is_rejected(void)
{
    rtc_t value = valid_time();
    value.month = 2U;
    value.day = 29U;
    assert_rejected(&value);
}

void test_rtc_sync_leap_february_29_is_accepted(void)
{
    rtc_t value = valid_time();
    value.year = 28U;
    value.month = 2U;
    value.day = 29U;
    rtc_sync(&value);
    TEST_ASSERT_EQUAL_UINT32(6U, write_count);
    TEST_ASSERT_EQUAL_UINT32(1835481599U, epoch_value);
}

void test_rtc_sync_invalid_clock_fields_preserve_all_state(void)
{
    rtc_t value = valid_time();
    value.hour = 24U;
    assert_rejected(&value);
    value = valid_time();
    value.minute = 60U;
    assert_rejected(&value);
    value = valid_time();
    value.second = 60U;
    assert_rejected(&value);
}

void test_rtc_sync_source_before_2026_is_rejected(void)
{
    rtc_t value = valid_time();
    value.year = 25U;
    assert_rejected(&value);
}

void test_rtc_sync_epoch_floor_is_accepted(void)
{
    const rtc_t value = {.day = 1U, .month = 1U, .year = 26U};
    rtc_sync(&value);
    TEST_ASSERT_EQUAL_UINT32(1767225600U, epoch_value);
    TEST_ASSERT_EQUAL_UINT32(6U, write_count);
}

void test_rtc_sync_year_99_is_accepted(void)
{
    rtc_t value = valid_time();
    value.year = 99U;
    rtc_sync(&value);
    TEST_ASSERT_EQUAL_UINT8(99U, hw_date.Year);
    TEST_ASSERT_EQUAL_UINT32(6U, write_count);
}

void test_rtc_sync_year_100_is_rejected_before_any_write(void)
{
    rtc_t value = valid_time();
    value.year = 100U;
    assert_rejected(&value);
}

void test_rtc_sync_millisecond_1000_is_rejected_before_any_write(void)
{
    rtc_t value = valid_time();
    value.millisec = 1000U;
    assert_rejected(&value);
}
/*** end of file ***/
