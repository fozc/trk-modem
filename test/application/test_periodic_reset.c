/*
 * test_periodic_reset.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies periodic reset scheduling, reconfiguration, and expiry.
 */

#include "unity.h"
#include "mock_runtime_stats.h"

#include <stdint.h>

#include "clock-arch.h"
#include "mock_bsp.h"
#include "mock_modem_config.h"
#include "mock_rtc.h"
#include "periodic_reset.h"

#define CLOCK_SECOND CLOCK_CONF_SECOND

struct timer
{
    clock_time_t start;
    clock_time_t interval;
};

static uint32_t fake_period_s;
static int fake_expired;
static clock_time_t captured_interval;
static uint32_t timer_set_count;
static uint32_t timer_expired_count;
static uint32_t system_reset_count;
static uint32_t sync_count;
static int sync_result;
static uint32_t tick;
static uint32_t marker_write_count;

static void write_marker(uint32_t dr, uint32_t value, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    TEST_ASSERT_EQUAL_UINT32(RTC_BKPR_PERIODIC_RESET_DR, dr);
    TEST_ASSERT_EQUAL_HEX32(RTC_BKPR_PERIODIC_RESET_MAGIC, value);
    marker_write_count++;
}

static uint32_t get_tick_callback(int call_count)
{
    (void)call_count;
    return tick;
}

static uint32_t get_period_callback(int call_count)
{
    (void)call_count;
    return fake_period_s;
}

void timer_set(struct timer *timer, clock_time_t interval)
{
    (void)timer;
    captured_interval = interval;
    timer_set_count++;
}

int timer_expired(struct timer *timer)
{
    (void)timer;
    timer_expired_count++;
    return fake_expired;
}

static void system_reset_callback(int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1U, marker_write_count);
    system_reset_count++;
}

static bool sync_callback(int call_count)
{
    (void)call_count;
    sync_count++;
    return sync_result == 0;
}

void setUp(void)
{
    fake_period_s = 0U;
    fake_expired = 0;
    captured_interval = 0U;
    timer_set_count = 0U;
    timer_expired_count = 0U;
    system_reset_count = 0U;
    sync_count = 0U;
    sync_result = 0;
    tick = 0U;
    marker_write_count = 0U;
    rtc_bkpr_write_StubWithCallback(write_marker);
    bsp_get_tick_StubWithCallback(get_tick_callback);
    runtime_stats_save_StubWithCallback(sync_callback);
    modem_config_get_reset_period_StubWithCallback(get_period_callback);
    bsp_system_reset_StubWithCallback(system_reset_callback);

    periodic_reset_tick();
    timer_set_count = 0U;
    timer_expired_count = 0U;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(system_reset_count, marker_write_count);
    /* Let any delay expire through the production entry point. */
    tick += 600000U;
    fake_period_s = 1U;
    fake_expired = 0;
    periodic_reset_tick();
    fake_period_s = 0U;
    periodic_reset_tick();
}

void test_periodic_reset_disabled_does_not_touch_timer(void)
{
    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(0U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(0U, timer_expired_count);
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
}

void test_periodic_reset_new_period_arms_timer_in_clock_ticks(void)
{
    fake_period_s = 60U;

    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(60U * (uint32_t)CLOCK_SECOND,
                             captured_interval);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_expired_count);
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
}

void test_periodic_reset_unchanged_period_does_not_rearm(void)
{
    fake_period_s = 60U;
    periodic_reset_tick();
    timer_set_count = 0U;
    timer_expired_count = 0U;

    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(0U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_expired_count);
}

void test_periodic_reset_period_change_rearms_timer(void)
{
    fake_period_s = 60U;
    periodic_reset_tick();
    timer_set_count = 0U;

    fake_period_s = 120U;
    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(120U * (uint32_t)CLOCK_SECOND,
                             captured_interval);
}

void test_periodic_reset_clamps_legacy_period_before_multiplication(void)
{
    fake_period_s = UINT32_MAX;

    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(
        PERIODIC_RESET_PERIOD_MAX_S * (uint32_t)CLOCK_SECOND,
        captured_interval);
}

void test_periodic_reset_expiry_requests_system_reset(void)
{
    fake_period_s = 1U;
    fake_expired = 1;

    periodic_reset_tick();

    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_periodic_reset_can_be_disabled_and_reenabled(void)
{
    fake_period_s = 30U;
    periodic_reset_tick();
    timer_set_count = 0U;
    timer_expired_count = 0U;

    fake_period_s = 0U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(0U, timer_expired_count);

    fake_period_s = 30U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_expired_count);
}

void test_periodic_reset_save_failure_still_requests_reset(void)
{
    fake_period_s = 1U;
    fake_expired = 1;
    sync_result = -1;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_update_defers_due_reset_until_exactly_ten_minutes(void)
{
    fake_period_s = 1U;
    fake_expired = 1;
    periodic_reset_defer_for_update();
    periodic_reset_tick();
    tick = 599999U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    TEST_ASSERT_EQUAL_UINT32(0U, sync_count);
    tick = 600000U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_new_update_restarts_ten_minute_delay(void)
{
    fake_period_s = 1U;
    fake_expired = 1;
    periodic_reset_defer_for_update();
    tick = 500000U;
    periodic_reset_defer_for_update();
    tick = 600000U;
    periodic_reset_tick();
    tick = 1099999U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    tick = 1100000U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_update_delay_does_not_change_later_reset_deadline(void)
{
    fake_period_s = 3600U;
    periodic_reset_tick();
    timer_set_count = 0U;
    periodic_reset_defer_for_update();
    tick = 600000U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    TEST_ASSERT_EQUAL_UINT32(0U, timer_set_count);
    fake_expired = 1;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_update_delay_survives_tick_wrap(void)
{
    fake_period_s = 1U;
    fake_expired = 1;
    tick = UINT32_MAX - 1000U;
    periodic_reset_defer_for_update();
    tick += 599999U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    tick++;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}

void test_update_does_not_enable_disabled_periodic_reset(void)
{
    fake_expired = 1;
    periodic_reset_defer_for_update();
    periodic_reset_tick();
    tick = 600000U;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(0U, system_reset_count);
    TEST_ASSERT_EQUAL_UINT32(0U, timer_set_count);
}

/*** end of file ***/
