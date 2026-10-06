/*
 * test_debouncer_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * Integrator thresholds, hysteresis and caller-owned event latch.
 */
#include "unity.h"
#include "debouncer.h"

static debouncer_t state;
void setUp(void)
{
    state = (debouncer_t){0U, 3U, 0U, 0U};
}
void tearDown(void)
{
}
void test_debounce_single_sample_threshold_changes_both_directions(void)
{
    state.debounce_time = 1U;
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.stable_state);
}
void test_debounce_all_nonzero_pin_values_are_high(void)
{
    state.debounce_time = 1U;
    for (uint16_t pin = 1U; pin <= UINT8_MAX; pin++)
    {
        state.integrator = 0U;
        state.stable_state = 0U;
        TEST_ASSERT_EQUAL_INT(1, debouncer(&state, (uint8_t)pin));
        TEST_ASSERT_EQUAL_UINT8(1U, state.integrator);
    }
}
void test_debounce_maximum_threshold_requires_255_samples_each_direction(void)
{
    state.debounce_time = UINT8_MAX;
    for (uint16_t i = 0U; i < UINT8_MAX - 1U; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    }
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 1U));
    for (uint16_t i = 0U; i < UINT8_MAX - 1U; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    }
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.integrator);
}
void test_debounce_event_flag_stays_latched_until_caller_clears_it(void)
{
    state.flag = 1U;
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(1U, state.flag);
    state.flag = 0U;
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.flag);
}
void test_debounce_partial_noise_does_not_clear_high_stable_state(void)
{
    state.integrator = 3U;
    state.stable_state = 1U;
    for (uint16_t i = 0U; i < 1000U; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
        TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
        TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
    }
    TEST_ASSERT_EQUAL_UINT8(0U, state.flag);
}
void test_debounce_independent_instances_do_not_share_history(void)
{
    debouncer_t other = {3U, 3U, 1U, 0U};
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&other, 0U));
    TEST_ASSERT_EQUAL_UINT8(1U, state.integrator);
    TEST_ASSERT_EQUAL_UINT8(2U, other.integrator);
    TEST_ASSERT_EQUAL_UINT8(0U, state.stable_state);
    TEST_ASSERT_EQUAL_UINT8(1U, other.stable_state);
}
/*** end of file ***/
