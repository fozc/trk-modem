/*
 * test_debouncer.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies integrator debouncing, saturation, and state changes.
 */

#include "unity.h"

#include <stdint.h>

#include "debouncer.h"

static debouncer_t state;

void setUp(void)
{
    state = (debouncer_t){
        .integrator = 0U,
        .debounce_time = 3U,
        .stable_state = 0U,
        .flag = 0U
    };
}

void tearDown(void)
{
}

void test_debouncer_requires_consecutive_high_samples(void)
{
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.stable_state);
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
    TEST_ASSERT_EQUAL_UINT8(1U, state.flag);
}

void test_debouncer_requires_consecutive_low_samples(void)
{
    state.integrator = state.debounce_time;
    state.stable_state = 1U;

    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
    TEST_ASSERT_EQUAL_INT(1, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.stable_state);
}

void test_debouncer_noise_moves_integrator_without_false_transition(void)
{
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 0U));
    TEST_ASSERT_EQUAL_UINT8(0U, state.integrator);
    TEST_ASSERT_EQUAL_UINT8(0U, state.stable_state);
}

void test_debouncer_saturates_at_configured_threshold(void)
{
    state.integrator = state.debounce_time;
    state.stable_state = 1U;

    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_UINT8(state.debounce_time, state.integrator);
    TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
}

void test_debouncer_does_not_overflow_at_maximum_threshold(void)
{
    state.debounce_time = UINT8_MAX;
    state.integrator = UINT8_MAX;
    state.stable_state = 1U;

    TEST_ASSERT_EQUAL_INT(0, debouncer(&state, 1U));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, state.integrator);
    TEST_ASSERT_EQUAL_UINT8(1U, state.stable_state);
}

/*** end of file ***/
