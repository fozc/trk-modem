/*
 * test_bsp_rtc_write.c
 *
 *  Created on: Oct 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify real RTC writes and deferred ticks at the CMSIS boundary.
 */
#include "unity.h"
#include "bsp.h"
#include "bsp_delay_test_platform.h"
#include <string.h>

extern void bsp_rtc_process(void);
static uint32_t enabled[2];
static uint32_t disable_count;
static uint32_t enable_count;
static uint32_t primask;
static bool pending_tick;
static bool inject_tick;

uint32_t __get_PRIMASK(void)
{
    return primask;
}

void __disable_irq(void)
{
    primask = 1U;
}

void __set_PRIMASK(uint32_t value)
{
    primask = value;
}

uint32_t NVIC_GetEnableIRQ(IRQn_Type irq)
{
    return enabled[(size_t)irq];
}

void NVIC_DisableIRQ(IRQn_Type irq)
{
    TEST_ASSERT_EQUAL_INT(TIM17_IRQn, irq);
    enabled[(size_t)irq] = 0U;
    disable_count++;
    if (inject_tick)
    {
        pending_tick = true;
    }
}

void NVIC_EnableIRQ(IRQn_Type irq)
{
    TEST_ASSERT_EQUAL_INT(TIM17_IRQn, irq);
    enabled[(size_t)irq] = 1U;
    enable_count++;
    if (pending_tick && (0U == primask))
    {
        pending_tick = false;
        bsp_rtc_process();
    }
}

void setUp(void)
{
    enabled[TIM17_IRQn] = 1U;
    enabled[EXTI15_IRQn] = 1U;
    primask = 0U;
    inject_tick = false;
    pending_tick = false;
    const bsp_rtc_t initial = {
        .millisec = 999U, .second = 59U, .minute = 59U,
        .hour = 23U, .day = 31U, .month = 12U, .year = 26U
    };
    bsp_set_datetime(&initial);
    disable_count = 0U;
    enable_count = 0U;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, primask);
    TEST_ASSERT_EQUAL_UINT32(1U, enabled[EXTI15_IRQn]);
}

void test_datetime_load_defers_tick_until_calendar_and_ms_are_updated(void)
{
    const bsp_rtc_t value = {
        .millisec = 0U, .second = 59U, .minute = 59U,
        .hour = 23U, .day = 31U, .month = 12U, .year = 26U
    };
    inject_tick = true;
    bsp_set_datetime(&value);
    const bsp_rtc_t result = bsp_get_datetime();
    TEST_ASSERT_EQUAL_UINT16(1U, result.millisec);
    TEST_ASSERT_EQUAL_UINT8(59U, result.second);
    TEST_ASSERT_EQUAL_UINT8(31U, result.day);
    TEST_ASSERT_EQUAL_UINT8(26U, result.year);
    TEST_ASSERT_EQUAL_UINT32(1U, disable_count);
    TEST_ASSERT_EQUAL_UINT32(1U, enable_count);
    TEST_ASSERT_FALSE(pending_tick);
}

void test_datetime_load_preserves_disabled_irq_and_pending_tick(void)
{
    const bsp_rtc_t value = {.day = 1U, .month = 1U, .year = 27U};
    enabled[TIM17_IRQn] = 0U;
    inject_tick = true;
    bsp_set_datetime(&value);
    const bsp_rtc_t result = bsp_get_datetime();
    TEST_ASSERT_EQUAL_MEMORY(&value, &result, sizeof(value));
    TEST_ASSERT_EQUAL_UINT32(0U, enabled[TIM17_IRQn]);
    TEST_ASSERT_EQUAL_UINT32(0U, enable_count);
    TEST_ASSERT_TRUE(pending_tick);
}

void test_null_datetime_preserves_clock_and_irq_state(void)
{
    const bsp_rtc_t before = bsp_get_datetime();
    bsp_set_datetime(NULL);
    const bsp_rtc_t after = bsp_get_datetime();
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL_UINT32(0U, disable_count);
    TEST_ASSERT_EQUAL_UINT32(0U, enable_count);
}

void test_legacy_calendar_and_ms_setters_preserve_disabled_irq(void)
{
    enabled[TIM17_IRQn] = 0U;
    bsp_set_rtc(1U, 2U, 3U, 4U, 5U, 26U);
    bsp_set_rtc_milisec(456U);
    const bsp_rtc_t value = bsp_get_datetime();
    TEST_ASSERT_EQUAL_UINT16(456U, value.millisec);
    TEST_ASSERT_EQUAL_UINT8(1U, value.second);
    TEST_ASSERT_EQUAL_UINT8(5U, value.month);
    TEST_ASSERT_EQUAL_UINT32(2U, disable_count);
    TEST_ASSERT_EQUAL_UINT32(0U, enable_count);
}

void test_pending_tick_at_year_boundary_runs_after_complete_load(void)
{
    const bsp_rtc_t value = {
        .millisec = 999U, .second = 59U, .minute = 59U,
        .hour = 23U, .day = 31U, .month = 12U, .year = 26U
    };
    inject_tick = true;
    bsp_set_datetime(&value);
    const bsp_rtc_t result = bsp_get_datetime();
    TEST_ASSERT_EQUAL_UINT16(0U, result.millisec);
    TEST_ASSERT_EQUAL_UINT8(0U, result.second);
    TEST_ASSERT_EQUAL_UINT8(1U, result.day);
    TEST_ASSERT_EQUAL_UINT8(1U, result.month);
    TEST_ASSERT_EQUAL_UINT8(27U, result.year);
}

void test_calendar_tick_preserves_month_and_leap_day_rollovers(void)
{
    static const bsp_rtc_t before[] = {
        {.millisec = 999U, .second = 59U, .minute = 59U,
         .hour = 23U, .day = 31U, .month = 1U, .year = 26U},
        {.millisec = 999U, .second = 59U, .minute = 59U,
         .hour = 23U, .day = 30U, .month = 4U, .year = 26U},
        {.millisec = 999U, .second = 59U, .minute = 59U,
         .hour = 23U, .day = 28U, .month = 2U, .year = 26U},
        {.millisec = 999U, .second = 59U, .minute = 59U,
         .hour = 23U, .day = 28U, .month = 2U, .year = 28U},
        {.millisec = 999U, .second = 59U, .minute = 59U,
         .hour = 23U, .day = 29U, .month = 2U, .year = 28U}
    };
    static const bsp_rtc_t expected[] = {
        {.day = 1U, .month = 2U, .year = 26U},
        {.day = 1U, .month = 5U, .year = 26U},
        {.day = 1U, .month = 3U, .year = 26U},
        {.day = 29U, .month = 2U, .year = 28U},
        {.day = 1U, .month = 3U, .year = 28U}
    };

    for (size_t index = 0U; index < (sizeof(before) / sizeof(before[0]));
         index++)
    {
        bsp_set_datetime(&before[index]);
        bsp_rtc_process();
        const bsp_rtc_t actual = bsp_get_datetime();
        TEST_ASSERT_EQUAL_MEMORY(&expected[index], &actual, sizeof(actual));
    }
}

/*** end of file ***/
