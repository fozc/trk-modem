/*
 * test_digital_input.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies active-low input seeding and user-reset duration actions.
 */

#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clock-arch.h"
#include "digital_input.h"
#include "gpio_defs.h"
#include "mock_bsp.h"
#include "mock_debouncer.h"
#include "mock_gpio.h"
#include "mock_nvram.h"

#define TEST_RESET_TICKS  ((uint32_t)CLOCK_CONF_SECOND * 3U)
#define TEST_NVRAM_TICKS  ((uint32_t)CLOCK_CONF_SECOND * 10U)
#define TEST_OTHER_TICKS  ((uint32_t)CLOCK_CONF_SECOND * 15U)

struct process;
struct etimer;
typedef void *process_data_t;

static uint8_t pin_levels[8][16];
static uint32_t process_start_count;
static uint32_t defaults_count;
static uint32_t sync_count;
static bool sync_force;
static int sync_result;
static uint32_t reset_count;
static clock_time_t fake_now;

void digital_input_test_dispatch_user_reset(uint32_t held_ticks);
void digital_input_test_sample(void);

void process_start(struct process *process, process_data_t data)
{
    (void)process;
    (void)data;
    process_start_count++;
}

void etimer_set(struct etimer *timer, clock_time_t interval)
{
    (void)timer;
    (void)interval;
}

void etimer_restart(struct etimer *timer)
{
    (void)timer;
}

int etimer_expired(struct etimer *timer)
{
    (void)timer;
    return 0;
}

clock_time_t clock_time(void)
{
    return fake_now;
}

static uint8_t read_pin_callback(gpio_port_t port, gpio_pin_t pin,
                                 int call_count)
{
    (void)call_count;
    return pin_levels[port][pin];
}

static void set_defaults_callback(int call_count)
{
    (void)call_count;
    defaults_count++;
}

static int sync_callback(bool force, int call_count)
{
    (void)call_count;
    sync_force = force;
    sync_count++;
    return sync_result;
}

static void reset_callback(int call_count)
{
    (void)call_count;
    reset_count++;
}

static int debounce_callback(debouncer_t *instance, uint8_t raw,
                             int call_count)
{
    int changed;

    (void)call_count;
    changed = (instance->stable_state != raw) ? 1 : 0;
    instance->stable_state = raw;
    return changed;
}

void setUp(void)
{
    for (size_t port = 0U; port < 8U; port++)
    {
        for (size_t pin = 0U; pin < 16U; pin++)
        {
            pin_levels[port][pin] = 1U;
        }
    }
    process_start_count = 0U;
    defaults_count = 0U;
    sync_count = 0U;
    sync_force = false;
    sync_result = 0;
    reset_count = 0U;
    fake_now = 0U;
    gpio_read_pin_StubWithCallback(read_pin_callback);
    debouncer_StubWithCallback(debounce_callback);
    nvram_set_defaults_StubWithCallback(set_defaults_callback);
    nvram_sync_StubWithCallback(sync_callback);
    bsp_system_reset_StubWithCallback(reset_callback);
}

void tearDown(void)
{
}

void test_digital_input_init_seeds_active_low_states_and_starts_process(void)
{
    pin_levels[DIN1_BSP_GPIO][DIN1_BSP_PIN] = 0U;
    pin_levels[DIN3_BSP_GPIO][DIN3_BSP_PIN] = 0U;
    pin_levels[DIP_SW2_GPIO][DIP_SW2_PIN] = 0U;

    digital_input_init();

    TEST_ASSERT_EQUAL_UINT32(1U, process_start_count);
    TEST_ASSERT_EQUAL_UINT8(1U, digital_input_get(DIN_CH_1));
    TEST_ASSERT_EQUAL_UINT8(0U, digital_input_get(DIN_CH_2));
    TEST_ASSERT_EQUAL_UINT8(1U, digital_input_get(DIN_CH_3));
    TEST_ASSERT_EQUAL_UINT8(0U, digital_input_get(DIN_CH_4));
    TEST_ASSERT_EQUAL_HEX8(0x05U, digital_input_get_all());
    TEST_ASSERT_EQUAL_UINT8(0U, dip_switch_get(DIP_SW_1));
    TEST_ASSERT_EQUAL_UINT8(1U, dip_switch_get(DIP_SW_2));
    TEST_ASSERT_EQUAL_HEX8(0x02U, dip_switch_get_all());
}

void test_digital_input_getters_reject_invalid_channels(void)
{
    digital_input_init();

    TEST_ASSERT_EQUAL_UINT8(0U,
        digital_input_get((din_channel_t)DIN_CH_COUNT));
    TEST_ASSERT_EQUAL_UINT8(0U, digital_input_get((din_channel_t)-1));
    TEST_ASSERT_EQUAL_UINT8(0U,
        dip_switch_get((dip_sw_channel_t)DIP_SW_COUNT));
    TEST_ASSERT_EQUAL_UINT8(0U,
        dip_switch_get((dip_sw_channel_t)-1));
}

void test_digital_input_reinit_reseeds_changed_levels(void)
{
    digital_input_init();
    TEST_ASSERT_EQUAL_HEX8(0U, digital_input_get_all());

    pin_levels[DIN4_BSP_GPIO][DIN4_BSP_PIN] = 0U;
    pin_levels[DIP_SW1_GPIO][DIP_SW1_PIN] = 0U;
    digital_input_init();

    TEST_ASSERT_EQUAL_HEX8(0x08U, digital_input_get_all());
    TEST_ASSERT_EQUAL_HEX8(0x01U, dip_switch_get_all());
    TEST_ASSERT_EQUAL_UINT32(2U, process_start_count);
}

void test_digital_input_sample_updates_din_and_dip_states(void)
{
    digital_input_init();
    pin_levels[DIN2_BSP_GPIO][DIN2_BSP_PIN] = 0U;
    pin_levels[DIP_SW1_GPIO][DIP_SW1_PIN] = 0U;

    digital_input_test_sample();

    TEST_ASSERT_EQUAL_HEX8(0x02U, digital_input_get_all());
    TEST_ASSERT_EQUAL_HEX8(0x01U, dip_switch_get_all());

    pin_levels[DIN2_BSP_GPIO][DIN2_BSP_PIN] = 1U;
    pin_levels[DIP_SW1_GPIO][DIP_SW1_PIN] = 1U;
    digital_input_test_sample();

    TEST_ASSERT_EQUAL_HEX8(0U, digital_input_get_all());
    TEST_ASSERT_EQUAL_HEX8(0U, dip_switch_get_all());
}

void test_user_reset_sample_measures_press_and_release_duration(void)
{
    digital_input_init();
    fake_now = 200U;
    pin_levels[USER_RESET_SW_GPIO][USER_RESET_SW_PIN] = 0U;
    digital_input_test_sample();

    fake_now = (clock_time_t)(200U + TEST_RESET_TICKS);
    pin_levels[USER_RESET_SW_GPIO][USER_RESET_SW_PIN] = 1U;
    digital_input_test_sample();

    TEST_ASSERT_EQUAL_UINT32(1U, reset_count);
}

void test_user_reset_sample_ignores_unchanged_button_state(void)
{
    digital_input_init();

    digital_input_test_sample();

    TEST_ASSERT_EQUAL_UINT32(0U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(0U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_user_reset_short_press_is_ignored(void)
{
    digital_input_test_dispatch_user_reset(TEST_RESET_TICKS - 1U);

    TEST_ASSERT_EQUAL_UINT32(0U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(0U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_user_reset_three_seconds_requests_mcu_reset(void)
{
    digital_input_test_dispatch_user_reset(TEST_RESET_TICKS);

    TEST_ASSERT_EQUAL_UINT32(0U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(0U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_count);
}

void test_user_reset_ten_seconds_persists_defaults_then_resets(void)
{
    digital_input_test_dispatch_user_reset(TEST_NVRAM_TICKS);

    TEST_ASSERT_EQUAL_UINT32(1U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_TRUE(sync_force);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_count);
}

void test_user_reset_does_not_reboot_when_defaults_cannot_be_saved(void)
{
    sync_result = -1;

    digital_input_test_dispatch_user_reset(TEST_NVRAM_TICKS);

    TEST_ASSERT_EQUAL_UINT32(1U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_user_reset_reserved_duration_only_logs(void)
{
    digital_input_test_dispatch_user_reset(TEST_OTHER_TICKS);

    TEST_ASSERT_EQUAL_UINT32(0U, defaults_count);
    TEST_ASSERT_EQUAL_UINT32(0U, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

/*** end of file ***/
