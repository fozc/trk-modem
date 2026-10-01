/*
 * test_relay.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies relay command handling and peak-to-hold timing.
 */

#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clock-arch.h"
#include "mock_relay_pwm.h"
#include "mock_shell.h"
#include "mock_utils.h"
#include "relay.h"

#define TEST_PEAK_TICKS \
    (((uint32_t)RELAY_PEAK_TIME_MS * (uint32_t)CLOCK_CONF_SECOND) / 1000U)

struct process;
struct etimer;
typedef void *process_data_t;

static clock_time_t fake_now;
static uint32_t process_start_count;
static uint32_t process_poll_count;
static uint32_t pwm_init_count;
static uint16_t duty[RELAY_CH_COUNT];
static uint32_t duty_set_count[RELAY_CH_COUNT];
static uint32_t timer_set_count;
static uint32_t timer_stop_count;
static clock_time_t timer_interval;
static shell_cmd_t registered_command;
static uint32_t shell_register_count;
static int parsed_channel;

void relay_test_apply_commands(void);
void relay_test_service_timer(void);

clock_time_t clock_time(void)
{
    return fake_now;
}

void process_start(struct process *process, process_data_t data)
{
    (void)process;
    (void)data;
    process_start_count++;
}

void process_poll(struct process *process)
{
    (void)process;
    process_poll_count++;
}

void etimer_set(struct etimer *timer, clock_time_t interval)
{
    (void)timer;
    timer_interval = interval;
    timer_set_count++;
}

void etimer_stop(struct etimer *timer)
{
    (void)timer;
    timer_stop_count++;
}

static void pwm_init_callback(int call_count)
{
    (void)call_count;
    pwm_init_count++;
}

static void set_duty_callback(relay_pwm_channel_t channel,
                              uint16_t duty_permille,
                              int call_count)
{
    (void)call_count;
    duty[channel] = duty_permille;
    duty_set_count[channel]++;
}

static int register_command_callback(const shell_cmd_t *command,
                                     int call_count)
{
    (void)call_count;
    registered_command = *command;
    shell_register_count++;
    return 0;
}

static int parse_channel_callback(const char *text, int call_count)
{
    (void)text;
    (void)call_count;
    return parsed_channel;
}

void setUp(void)
{
    fake_now = 0U;
    process_start_count = 0U;
    process_poll_count = 0U;
    pwm_init_count = 0U;
    timer_set_count = 0U;
    timer_stop_count = 0U;
    timer_interval = 0U;
    shell_register_count = 0U;
    parsed_channel = 1;
    (void)memset(duty, 0, sizeof(duty));
    (void)memset(duty_set_count, 0, sizeof(duty_set_count));
    (void)memset(&registered_command, 0, sizeof(registered_command));

    relay_pwm_init_StubWithCallback(pwm_init_callback);
    relay_pwm_set_duty_StubWithCallback(set_duty_callback);
    shell_register_command_StubWithCallback(register_command_callback);
    xstrtoi_StubWithCallback(parse_channel_callback);

    relay_init();
}

void tearDown(void)
{
}

void test_relay_init_starts_off_and_registers_shell_command(void)
{
    TEST_ASSERT_EQUAL_UINT32(1U, pwm_init_count);
    TEST_ASSERT_EQUAL_UINT32(1U, process_start_count);
    TEST_ASSERT_EQUAL_UINT32(1U, shell_register_count);
    TEST_ASSERT_EQUAL_STRING("relay", registered_command.cmd);
    TEST_ASSERT_NOT_NULL(registered_command.func);
    TEST_ASSERT_EQUAL_UINT16(0U, duty[RELAY_CH_1]);
    TEST_ASSERT_EQUAL_UINT16(0U, duty[RELAY_CH_2]);
    TEST_ASSERT_FALSE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_FALSE(relay_is_on(RELAY_CH_2));
}

void test_relay_set_rejects_invalid_channels(void)
{
    relay_set((relay_channel_t)RELAY_CH_COUNT, true);
    relay_set((relay_channel_t)-1, true);

    TEST_ASSERT_EQUAL_UINT32(0U, process_poll_count);
    TEST_ASSERT_FALSE(relay_is_on((relay_channel_t)RELAY_CH_COUNT));
    TEST_ASSERT_FALSE(relay_is_on((relay_channel_t)-1));
}

void test_relay_pending_command_is_visible_before_process_runs(void)
{
    relay_set(RELAY_CH_1, true);
    TEST_ASSERT_TRUE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_EQUAL_UINT32(1U, process_poll_count);

    relay_set(RELAY_CH_1, false);
    TEST_ASSERT_FALSE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_EQUAL_UINT32(2U, process_poll_count);
}

void test_relay_on_applies_peak_and_arms_full_interval(void)
{
    fake_now = 250U;
    relay_set(RELAY_CH_1, true);
    relay_test_apply_commands();

    TEST_ASSERT_TRUE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_EQUAL_UINT16(RELAY_PEAK_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(TEST_PEAK_TICKS, timer_interval);
}

void test_relay_stays_at_peak_until_deadline_then_changes_to_hold(void)
{
    fake_now = 100U;
    relay_set(RELAY_CH_1, true);
    relay_test_apply_commands();
    timer_set_count = 0U;

    fake_now = (clock_time_t)(100U + TEST_PEAK_TICKS - 1U);
    relay_test_service_timer();
    TEST_ASSERT_EQUAL_UINT16(RELAY_PEAK_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_set_count);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_interval);

    fake_now++;
    relay_test_service_timer();
    TEST_ASSERT_EQUAL_UINT16(RELAY_HOLD_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
    TEST_ASSERT_TRUE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_EQUAL_UINT32(1U, timer_stop_count);
}

void test_relay_off_cancels_peak_and_stops_timer(void)
{
    relay_set(RELAY_CH_1, true);
    relay_test_apply_commands();
    timer_stop_count = 0U;

    relay_set(RELAY_CH_1, false);
    relay_test_apply_commands();

    TEST_ASSERT_EQUAL_UINT16(0U, duty[RELAY_CH_1]);
    TEST_ASSERT_FALSE(relay_is_on(RELAY_CH_1));
    TEST_ASSERT_EQUAL_UINT32(1U, timer_stop_count);
}

void test_relay_timer_tracks_nearest_of_two_peak_deadlines(void)
{
    relay_set(RELAY_CH_1, true);
    relay_test_apply_commands();

    fake_now = 400U;
    relay_set(RELAY_CH_2, true);
    relay_test_apply_commands();
    TEST_ASSERT_EQUAL_UINT32(TEST_PEAK_TICKS - 400U, timer_interval);

    fake_now = TEST_PEAK_TICKS;
    relay_test_service_timer();
    TEST_ASSERT_EQUAL_UINT16(RELAY_HOLD_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
    TEST_ASSERT_EQUAL_UINT16(RELAY_PEAK_DUTY_PERMILLE,
                             duty[RELAY_CH_2]);
    TEST_ASSERT_EQUAL_UINT32(400U, timer_interval);
}

void test_relay_deadline_handles_clock_wraparound(void)
{
    fake_now = UINT32_MAX - 500U;
    relay_set(RELAY_CH_1, true);
    relay_test_apply_commands();

    fake_now = 498U;
    relay_test_service_timer();
    TEST_ASSERT_EQUAL_UINT16(RELAY_PEAK_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
    TEST_ASSERT_EQUAL_UINT32(1U, timer_interval);

    fake_now = 499U;
    relay_test_service_timer();
    TEST_ASSERT_EQUAL_UINT16(RELAY_HOLD_DUTY_PERMILLE,
                             duty[RELAY_CH_1]);
}

void test_relay_shell_validates_arguments_and_sets_selected_channel(void)
{
    char *missing[] = { "relay" };
    char *status[] = { "relay", "status" };
    char *missing_action[] = { "relay", "1" };
    char *on[] = { "relay", "2", "on" };
    char *off[] = { "relay", "2", "off" };
    char *invalid_action[] = { "relay", "2", "toggle" };

    TEST_ASSERT_EQUAL_INT(-1, registered_command.func(1, missing));
    TEST_ASSERT_EQUAL_INT(0, registered_command.func(2, status));
    TEST_ASSERT_EQUAL_INT(-1,
                          registered_command.func(2, missing_action));

    parsed_channel = 0;
    TEST_ASSERT_EQUAL_INT(-1, registered_command.func(3, on));
    parsed_channel = 3;
    TEST_ASSERT_EQUAL_INT(-1, registered_command.func(3, on));
    parsed_channel = 2;
    TEST_ASSERT_EQUAL_INT(-1,
                          registered_command.func(3, invalid_action));
    TEST_ASSERT_EQUAL_INT(0, registered_command.func(3, on));
    TEST_ASSERT_TRUE(relay_is_on(RELAY_CH_2));
    TEST_ASSERT_EQUAL_INT(0, registered_command.func(3, off));
    TEST_ASSERT_FALSE(relay_is_on(RELAY_CH_2));
}

/*** end of file ***/
