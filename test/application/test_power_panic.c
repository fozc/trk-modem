/*
 * test_power_panic.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies power-panic edge handling and one-log-per-episode behavior.
 */

#include "unity.h"

#include <stdint.h>

#include "gpio_defs.h"
#include "mock_elog.h"
#include "mock_gpio.h"
#include "mock_bsp.h"
#include "power_panic.h"

static uint8_t panic_level;
static uint32_t elog_count;
static elog_power_panic_reason_t last_reason;
static uint32_t mirror_write_count;
static gpio_port_t mirror_port;
static gpio_pin_t mirror_pin;
static uint8_t mirror_level;

static uint8_t read_pin_callback(gpio_port_t port, gpio_pin_t pin,
                                 int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL(PWR_PANIC_BSP_GPIO, port);
    TEST_ASSERT_EQUAL(PWR_PANIC_BSP_PIN, pin);
    return panic_level;
}

static void set_pin_callback(gpio_port_t port, gpio_pin_t pin,
                             uint8_t level, int call_count)
{
    (void)call_count;
    mirror_port = port;
    mirror_pin = pin;
    mirror_level = level;
    mirror_write_count++;
}

static void log_panic_callback(elog_power_panic_reason_t reason, int call_count)
{
    (void)call_count;
    elog_count++;
    last_reason = reason;
}

void setUp(void)
{
    panic_level = 1U;
    elog_count = 0U;
    last_reason = ELOG_POWER_PANIC_UNKNOWN;
    mirror_write_count = 0U;
    mirror_port = GPIO_A;
    mirror_pin = PIN_0;
    mirror_level = 0U;

    bsp_power_panic_irq_disable_IgnoreAndReturn(1U);
    bsp_power_panic_irq_restore_Ignore();
    gpio_read_pin_StubWithCallback(read_pin_callback);
    gpio_set_pin_StubWithCallback(set_pin_callback);
    elog_log_power_panic_StubWithCallback(log_panic_callback);

    power_panic_init();
    power_panic_check();
    elog_count = 0U;
    mirror_write_count = 0U;
}

void tearDown(void)
{
}

void test_power_panic_init_mirrors_high_input(void)
{
    power_panic_init();

    TEST_ASSERT_EQUAL_UINT32(1U, mirror_write_count);
    TEST_ASSERT_EQUAL(PWR_PANIC_MIRROR_GPIO, mirror_port);
    TEST_ASSERT_EQUAL(PWR_PANIC_MIRROR_PIN, mirror_pin);
    TEST_ASSERT_EQUAL_UINT8(1U, mirror_level);
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(0U, elog_count);
}

void test_power_panic_init_low_opens_episode_and_logs_once(void)
{
    panic_level = 0U;
    power_panic_init();

    TEST_ASSERT_EQUAL_UINT8(0U, mirror_level);
    power_panic_check();
    power_panic_check();

    TEST_ASSERT_EQUAL_UINT32(1U, elog_count);
    TEST_ASSERT_EQUAL(ELOG_POWER_PANIC_STARTUP_LOW, last_reason);
}

void test_power_panic_falling_edge_logs_only_while_line_is_low(void)
{
    power_panic_isr_falling_edge();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(0U, elog_count);

    panic_level = 0U;
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(1U, elog_count);
}

void test_power_panic_rising_edge_closes_episode_only_when_line_is_high(void)
{
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();

    power_panic_isr_rising_edge();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(1U, elog_count);

    panic_level = 1U;
    power_panic_isr_rising_edge();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(1U, elog_count);
}

void test_power_panic_new_low_episode_logs_again(void)
{
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();

    panic_level = 1U;
    power_panic_isr_rising_edge();
    power_panic_check();

    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();

    TEST_ASSERT_EQUAL_UINT32(2U, elog_count);
    TEST_ASSERT_EQUAL(ELOG_POWER_PANIC_FALLING_EDGE, last_reason);
}

void test_power_panic_rise_and_fall_between_checks_log_the_new_episode(void)
{
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();
    panic_level = 1U;
    power_panic_isr_rising_edge();
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(2U, elog_count);
}

void test_startup_low_then_runtime_edge_have_distinct_reasons(void)
{
    panic_level = 0U;
    power_panic_init();
    power_panic_check();
    TEST_ASSERT_EQUAL(ELOG_POWER_PANIC_STARTUP_LOW, last_reason);
    panic_level = 1U;
    power_panic_isr_rising_edge();
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(2U, elog_count);
    TEST_ASSERT_EQUAL(ELOG_POWER_PANIC_FALLING_EDGE, last_reason);
}

void test_startup_low_cleared_before_check_is_not_reported_as_a_new_edge(void)
{
    panic_level = 0U;
    power_panic_init();
    panic_level = 1U;
    power_panic_isr_rising_edge();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(0U, elog_count);
    panic_level = 0U;
    power_panic_isr_falling_edge();
    power_panic_check();
    TEST_ASSERT_EQUAL_UINT32(1U, elog_count);
    TEST_ASSERT_EQUAL(ELOG_POWER_PANIC_FALLING_EDGE, last_reason);
}

/*** end of file ***/
