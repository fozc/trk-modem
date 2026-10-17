/*
 * test_modbus_snapshot_process_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * MB-08 process-level test: successive FC03 responses through the real
 * RTU core verify that (a) calendar fields within one response are from
 * the same snapshot, and (b) a new response takes a fresh snapshot.
 */
#include "unity.h"
#include "modbus_system_stats.h"
#include "mock_reset_source.h"
#include "mock_bsp.h"
#include "mock_rtc.h"
#include "mock_adc.h"
#include "mock_digital_input.h"

static uint16_t read_reg(uint16_t addr)
{
    uint16_t value = 0U;
    const bool ok = modbus_system_stats_read(addr, &value);
    TEST_ASSERT_TRUE(ok);
    return value;
}

void setUp(void)
{
    /* Snapshot calls all 8 time getters; mock the calendar ones. */
    rtc_now_IgnoreAndReturn((rtc_t){0});
}

void tearDown(void)
{
}

void test_calendar_fields_within_one_response_are_consistent(void)
{
    rtc_now_StopIgnore();
    /* Minute rollover scenario: the calendar reads second=59 and
     * minute=42, then the real clock ticks to minute=43. The snapshot
     * freezes both at the instant it was taken: the response carries
     * sec=59, min=42 (from the same instant), NOT sec=00, min=43. */
    rtc_get_unix_epoch_ExpectAndReturn(1730000000U);
    bsp_get_tick_ExpectAndReturn(50000U);
    const rtc_t t1 = { .second=59U, .minute=42U, .hour=14U,
                       .day=17U, .month=10U, .year=26U };
    rtc_now_ExpectAndReturn(t1);

    /* Response starts: invalidate → lazy snapshot fires on first read. */
    modbus_system_stats_invalidate_snapshot();
    /* Read sec (first block register → triggers snapshot). */
    TEST_ASSERT_EQUAL_UINT16(59U, read_reg(49000U));
    /* Read min: NO further rtc_get_minute call (from the cache). */
    TEST_ASSERT_EQUAL_UINT16(42U, read_reg(49001U));
    /* Read hour, day, month, year: all from the same cache. */
    TEST_ASSERT_EQUAL_UINT16(14U, read_reg(49002U));
    TEST_ASSERT_EQUAL_UINT16(17U, read_reg(49003U));
    TEST_ASSERT_EQUAL_UINT16(10U, read_reg(49004U));
    TEST_ASSERT_EQUAL_UINT16(2026U, read_reg(49005U));
}

void test_new_response_takes_fresh_snapshot(void)
{
    rtc_now_StopIgnore();
    /* Response 1: snapshot at t=10000. */
    rtc_get_unix_epoch_ExpectAndReturn(1730000000U);
    bsp_get_tick_ExpectAndReturn(10000U);
    const rtc_t t1 = { .second=30U, .minute=10U, .hour=12U,
                       .day=1U, .month=5U, .year=26U };
    rtc_now_ExpectAndReturn(t1);
    modbus_system_stats_invalidate_snapshot();
    TEST_ASSERT_EQUAL_UINT16(30U, read_reg(49000U));
    TEST_ASSERT_EQUAL_UINT16(10U, read_reg(49001U));

    /* Response 2: invalidate → new snapshot at t=20000. */
    rtc_get_unix_epoch_ExpectAndReturn(1730000600U);
    bsp_get_tick_ExpectAndReturn(20000U);
    const rtc_t t2 = { .second=45U, .minute=11U, .hour=12U,
                       .day=1U, .month=5U, .year=26U };
    rtc_now_ExpectAndReturn(t2);
    modbus_system_stats_invalidate_snapshot();
    TEST_ASSERT_EQUAL_UINT16(45U, read_reg(49000U));
    TEST_ASSERT_EQUAL_UINT16(11U, read_reg(49001U));
}
