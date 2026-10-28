/*
 * test_power_panic_init.c
 *
 *  Created on: Oct 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Inject a falling edge during initialization at the GPIO/IRQ boundary.
 */
#include "unity.h"
#include "power_panic.h"
#include "gpio_defs.h"
#include "mock_bsp.h"
#include "mock_gpio.h"
#include "mock_elog.h"

static uint8_t level;
static uint8_t mirror;
static bool irq_enabled;
static bool pending_edge;
static bool inject_edge;
static bool inject_on_restore;

static uint32_t disable_irq(int call_count)
{
    (void)call_count;
    const uint32_t was_enabled = irq_enabled ? 1U : 0U;
    irq_enabled = false;
    return was_enabled;
}

static void restore_irq(uint32_t was_enabled, int call_count)
{
    (void)call_count;
    if (inject_on_restore)
    {
        inject_on_restore = false;
        level = 0U;
        pending_edge = true;
    }
    if (0U != was_enabled)
    {
        irq_enabled = true;
        if (pending_edge)
        {
            pending_edge = false;
            power_panic_isr_falling_edge();
            mirror = level;
        }
    }
}

static uint8_t read_pin(gpio_port_t port, gpio_pin_t pin, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT8(PWR_PANIC_BSP_GPIO, port);
    TEST_ASSERT_EQUAL_UINT8(PWR_PANIC_BSP_PIN, pin);
    const uint8_t sampled = level;
    if (inject_edge)
    {
        inject_edge = false;
        level = 0U;
        if (irq_enabled)
        {
            power_panic_isr_falling_edge();
            mirror = level;
        }
        else
        {
            pending_edge = true;
        }
    }
    return sampled;
}

static void write_pin(gpio_port_t port, gpio_pin_t pin, uint8_t value,
                      int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT8(PWR_PANIC_MIRROR_GPIO, port);
    TEST_ASSERT_EQUAL_UINT8(PWR_PANIC_MIRROR_PIN, pin);
    mirror = value;
}

void setUp(void)
{
    level = 1U;
    mirror = 0U;
    irq_enabled = true;
    pending_edge = false;
    inject_edge = false;
    inject_on_restore = false;
    gpio_read_pin_StubWithCallback(read_pin);
    gpio_set_pin_StubWithCallback(write_pin);
    bsp_power_panic_irq_disable_StubWithCallback(disable_irq);
    bsp_power_panic_irq_restore_StubWithCallback(restore_irq);
    power_panic_isr_rising_edge();
    power_panic_check();
}

void tearDown(void)
{
}

void test_falling_edge_during_init_is_preserved_and_logged_once(void)
{
    inject_edge = true;
    power_panic_init();
    TEST_ASSERT_TRUE(irq_enabled);
    TEST_ASSERT_FALSE(pending_edge);
    TEST_ASSERT_EQUAL_UINT8(0U, mirror);
    elog_log_power_panic_Expect();
    power_panic_check();
    power_panic_check();
}

void test_boot_low_line_is_logged_without_an_edge(void)
{
    level = 0U;
    power_panic_init();
    TEST_ASSERT_EQUAL_UINT8(0U, mirror);
    elog_log_power_panic_Expect();
    power_panic_check();
}

void test_boot_high_line_is_not_logged(void)
{
    power_panic_init();
    TEST_ASSERT_EQUAL_UINT8(1U, mirror);
    power_panic_check();
}

void test_init_preserves_previously_disabled_irq_and_pending_edge(void)
{
    irq_enabled = false;
    inject_edge = true;
    power_panic_init();
    TEST_ASSERT_FALSE(irq_enabled);
    TEST_ASSERT_TRUE(pending_edge);
    restore_irq(1U, 0);
    elog_log_power_panic_Expect();
    power_panic_check();
}

void test_check_preserves_previously_disabled_irq(void)
{
    level = 0U;
    power_panic_init();
    irq_enabled = false;
    elog_log_power_panic_Expect();
    power_panic_check();
    TEST_ASSERT_FALSE(irq_enabled);
}

void test_edge_arriving_during_check_is_preserved_for_the_next_check(void)
{
    power_panic_init();
    inject_on_restore = true;
    power_panic_check();
    TEST_ASSERT_TRUE(irq_enabled);
    TEST_ASSERT_FALSE(pending_edge);
    elog_log_power_panic_Expect();
    power_panic_check();
    power_panic_check();
}

/*** end of file ***/
