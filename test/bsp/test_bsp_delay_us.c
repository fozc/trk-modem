/*
 * test_bsp_delay_us.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real BSP delay entry with deterministic SysTick completion on the host.
 */
#include "unity.h"
#include "bsp.h"
#include "bsp_delay_test_platform.h"
#include <string.h>

#define TEST_TICKS_PER_US 96U
#define TEST_MAX_CHUNK_US 174762U
static bsp_delay_test_systick_t registers;
static uint32_t access_count;
static uint32_t chunk_count;
static uint32_t first_load;
static uint32_t last_load;
static uint64_t total_ticks;
static bool active;

bsp_delay_test_systick_t *bsp_delay_test_systick(void)
{
    access_count++;
    if (0U != (registers.CTRL & SysTick_CTRL_ENABLE_Msk))
    {
        if (!active)
        {
            active = true;
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(SysTick_LOAD_RELOAD_Msk,
                                             registers.LOAD);
            TEST_ASSERT_EQUAL_UINT32(
                SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk,
                registers.CTRL);
            if (0U == chunk_count)
            {
                first_load = registers.LOAD;
            }
            last_load = registers.LOAD;
            chunk_count++;
            total_ticks += (uint64_t)registers.LOAD + 1U;
        }
        /* Model completion, not elapsed wall time or a physical interrupt. */
        registers.CTRL |= SysTick_CTRL_COUNTFLAG_Msk;
    }
    else
    {
        active = false;
    }
    return &registers;
}

void setUp(void)
{
    memset(&registers, 0, sizeof(registers));
    access_count = 0U;
    chunk_count = 0U;
    first_load = 0U;
    last_load = 0U;
    total_ticks = 0U;
    active = false;
}

void tearDown(void)
{
}

static void assert_delay(uint32_t us, uint32_t expected_chunks,
                         uint32_t expected_last_us)
{
    bsp_delay_us(us);
    TEST_ASSERT_EQUAL_UINT32(expected_chunks, chunk_count);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)us * TEST_TICKS_PER_US,
                             total_ticks);
    TEST_ASSERT_EQUAL_UINT32(expected_last_us * TEST_TICKS_PER_US - 1U,
                             last_load);
    TEST_ASSERT_EQUAL_UINT32(0U, registers.CTRL);
    TEST_ASSERT_EQUAL_UINT32(0U, registers.VAL);
}

void test_delay_zero_does_not_access_or_modify_systick(void)
{
    registers.CTRL = 0x200U;
    registers.LOAD = 123U;
    registers.VAL = 456U;
    const bsp_delay_test_systick_t before = registers;
    bsp_delay_us(0U);
    TEST_ASSERT_EQUAL_UINT32(0U, access_count);
    TEST_ASSERT_EQUAL_MEMORY(&before, &registers, sizeof(registers));
}

void test_delay_one_microsecond_uses_96_ticks(void)
{
    assert_delay(1U, 1U, 1U);
    TEST_ASSERT_EQUAL_UINT32(95U, first_load);
}

void test_delay_small_value_preserves_requested_ticks(void)
{
    assert_delay(123U, 1U, 123U);
}

void test_delay_max_single_chunk_stays_within_reload_limit(void)
{
    assert_delay(TEST_MAX_CHUNK_US, 1U, TEST_MAX_CHUNK_US);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFBFU, first_load);
}

void test_delay_limit_plus_one_splits_without_truncating(void)
{
    assert_delay(TEST_MAX_CHUNK_US + 1U, 2U, 1U);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFBFU, first_load);
}

void test_delay_two_exact_chunks_has_no_extra_zero_chunk(void)
{
    assert_delay(TEST_MAX_CHUNK_US * 2U, 2U, TEST_MAX_CHUNK_US);
}

void test_delay_one_second_preserves_total_requested_ticks(void)
{
    assert_delay(1000000U, 6U, 126190U);
}

void test_delay_uint32_max_does_not_overflow_tick_calculation(void)
{
    const uint32_t full_chunks = UINT32_MAX / TEST_MAX_CHUNK_US;
    const uint32_t remainder = UINT32_MAX % TEST_MAX_CHUNK_US;
    assert_delay(UINT32_MAX, full_chunks + 1U, remainder);
}
/*** end of file ***/
