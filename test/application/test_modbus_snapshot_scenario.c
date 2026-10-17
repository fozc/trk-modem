/*
 * test_modbus_snapshot_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * MB-08: FC03 multi-register responses must not tear UINT32 values or
 * cross calendar/age boundaries mid-response. The fix takes one
 * snapshot of time-sensitive sources per FC03 response.
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

void test_uint32_epoch_words_come_from_one_snapshot_call(void)
{
    /* MB-08 regression: rtc_unix high and low words must come from a
     * single rtc_get_unix_epoch() call (the snapshot), not from two
     * separate calls that could tear across a second boundary.
     *
     * Without the snapshot fix, the code calls rtc_get_unix_epoch()
     * twice — once per word. The mock allows only ONE call; a second
     * call fails the test, proving the fix. */
    rtc_now_StopIgnore();
    rtc_get_unix_epoch_ExpectAndReturn(0x0000FFFFU);
    bsp_get_tick_ExpectAndReturn(0U);
    rtc_now_ExpectAndReturn((rtc_t){0});

    modbus_system_stats_snapshot();   /* freeze time sources */

    /* Both words read from the cached value; no further getter calls. */
    TEST_ASSERT_EQUAL_UINT16(0x0000U, read_reg(49006U)); /* high */
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, read_reg(49007U)); /* low  */
}

void test_uint32_uptime_words_come_from_one_snapshot_call(void)
{
    /* Same pattern for uptime_raw (bsp_get_tick). */
    rtc_now_StopIgnore();
    rtc_get_unix_epoch_ExpectAndReturn(0U);
    bsp_get_tick_ExpectAndReturn(0x5678EF01U);
    rtc_now_ExpectAndReturn((rtc_t){0});

    modbus_system_stats_snapshot();

    TEST_ASSERT_EQUAL_UINT16(0x5678U, read_reg(49014U)); /* high */
    TEST_ASSERT_EQUAL_UINT16(0xEF01U, read_reg(49015U)); /* low  */
}

void test_epoch_second_boundary_does_not_tear_response(void)
{
    /* Cross-check: even if the epoch is about to tick over from
     * 0x0000FFFF to 0x00010000, both words in one response come from
     * the same snapshot instant. */
    rtc_get_unix_epoch_ExpectAndReturn(0x0000FFFFU);
    bsp_get_tick_ExpectAndReturn(0U);
    modbus_system_stats_snapshot();

    /* Simulate: after the snapshot, the real clock ticks over. The
     * cached value stays at 0x0000FFFF — both words consistent. */
    TEST_ASSERT_EQUAL_UINT16(0x0000U, read_reg(49006U));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, read_reg(49007U));

    /* Next response takes a new snapshot. */
    rtc_get_unix_epoch_ExpectAndReturn(0x00010000U);
    bsp_get_tick_ExpectAndReturn(0U);
    modbus_system_stats_snapshot();
    TEST_ASSERT_EQUAL_UINT16(0x0001U, read_reg(49006U));
    TEST_ASSERT_EQUAL_UINT16(0x0000U, read_reg(49007U));
}
