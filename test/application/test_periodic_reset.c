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

#include <stdint.h>

#include "clock-arch.h"
#include "mock_bsp.h"
#include "mock_modem_config.h"
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
    system_reset_count++;
}

static int sync_callback(int call_count)
{
    (void)call_count;
    sync_count++;
    return sync_result;
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
    modem_config_sync_StubWithCallback(sync_callback);
    modem_config_get_reset_period_StubWithCallback(get_period_callback);
    bsp_system_reset_StubWithCallback(system_reset_callback);

    periodic_reset_tick();
    timer_set_count = 0U;
    timer_expired_count = 0U;
}

void tearDown(void)
{
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

/*** end of file ***/

void test_periodic_reset_save_failure_still_requests_reset(void)
{
    fake_period_s = 1U;
    fake_expired = 1;
    sync_result = -1;
    periodic_reset_tick();
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1U, system_reset_count);
}
