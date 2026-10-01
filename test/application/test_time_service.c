/*
 * test_time_service.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies tick counting and wrap-safe elapsed-time checks.
 */

#include "unity.h"

#include <stdint.h>

#include "time_service.h"

void setUp(void)
{
    time_service_reset();
}

void tearDown(void)
{
}

void test_time_service_starts_at_zero_and_counts_ticks(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, get_system_uptime());

    time_service_tick();
    time_service_tick();

    TEST_ASSERT_EQUAL_UINT32(2U, get_system_uptime());
}

void test_time_get_elapsed_subtracts_without_wrap(void)
{
    time_service_set_ticks(1250U);

    TEST_ASSERT_EQUAL_UINT32(250U, time_get_elapsed(1000U));
}

void test_time_get_elapsed_handles_uint32_wrap(void)
{
    time_service_set_ticks(4U);

    TEST_ASSERT_EQUAL_UINT32(10U,
                             time_get_elapsed(UINT32_MAX - 5U));
}

void test_time_service_tick_wraps_to_zero(void)
{
    time_service_set_ticks(UINT32_MAX);

    time_service_tick();

    TEST_ASSERT_EQUAL_UINT32(0U, get_system_uptime());
}

void test_time_has_elapsed_includes_exact_deadline(void)
{
    time_service_set_ticks(1100U);

    TEST_ASSERT_FALSE(time_has_elapsed(1000U, 101U));
    TEST_ASSERT_TRUE(time_has_elapsed(1000U, 100U));
    TEST_ASSERT_TRUE(time_has_elapsed(1000U, 0U));
}

void test_time_has_elapsed_works_across_wrap(void)
{
    time_service_set_ticks(3U);

    TEST_ASSERT_FALSE(time_has_elapsed(UINT32_MAX - 4U, 9U));
    TEST_ASSERT_TRUE(time_has_elapsed(UINT32_MAX - 4U, 8U));
}

/*** end of file ***/
