/*
 * test_bsp_rng_transition_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real RNG transition logging with controlled HAL errors and recovery.
 */
#include "unity.h"
#include "mock_stm32u3xx_hal_rng.h"
#include "../support/production_logging.inc"
static uint32_t fallback_logs;
#undef CSLOG_WARN
#define CSLOG_WARN(...) ((void)++fallback_logs)
#include "../../Application/bsp/bsp_random.c"
RNG_HandleTypeDef hrng;
uint32_t test_rng_flags;
static uint32_t hal_calls, recovery_calls;
static HAL_StatusTypeDef hal_status = HAL_OK;
static bool inject_clock_error;
static HAL_StatusTypeDef read_rng(RNG_HandleTypeDef *handle, uint32_t *word, int call)
{
    (void)handle;
    (void)call;
    hal_calls++;
    if (hal_status != HAL_OK)
        return hal_status;
    if ((test_rng_flags & RNG_IT_SEI) != 0U && (test_rng_flags & RNG_FLAG_SECS) == 0U)
        return HAL_ERROR;
    test_rng_flags &= ~(RNG_FLAG_SECS | RNG_IT_SEI);
    if (inject_clock_error)
        test_rng_flags |= RNG_FLAG_CECS;
    *word = 0x12345678U;
    return HAL_OK;
}
static HAL_StatusTypeDef recover_rng(RNG_HandleTypeDef *handle, int call)
{
    (void)handle;
    (void)call;
    recovery_calls++;
    test_rng_flags &= ~RNG_IT_SEI;
    return HAL_OK;
}
void test_rng_fallback_logging_rearms_and_accumulator_accepts_only_hardware(void)
{
    uint32_t value = 99U;
    uint32_t initial = bsp_get_random_accumulator();
    TEST_ASSERT_TRUE(!bsp_random_word(NULL) && hal_calls == 0U);
    test_rng_flags = RNG_FLAG_CECS;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && value != 0U && hal_calls == 0U);
    test_rng_flags = RNG_IT_CEI;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && value != 0U && hal_calls == 0U);
    TEST_ASSERT_TRUE(fallback_logs == 1U);
    puts("PASS: clock errors select fallback without a HAL read");
    test_rng_flags = RNG_FLAG_SECS | RNG_IT_SEI;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && hal_calls == 1U);
    TEST_ASSERT_TRUE(value == 0x12345678U);
    TEST_ASSERT_TRUE(bsp_get_random_accumulator() == initial + value);
    puts("PASS: seed error reaches existing HAL recovery path");
    hal_status = HAL_ERROR;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && value != 0U);
    puts("PASS: HAL failure selects availability fallback");
    hal_status = HAL_OK;
    inject_clock_error = true;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && value != 0U);
    TEST_ASSERT_TRUE(fallback_logs == 2U);
    puts("PASS: clock error after HAL read selects fallback without accumulating");
    TEST_ASSERT_TRUE(bsp_get_random_accumulator() == initial + 0x12345678U);
    test_rng_flags = 0U;
    inject_clock_error = false;
    random_accumulator = UINT32_MAX - 0x12345678U + 2U;
    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_TRUE(bsp_get_random_accumulator() == 1U);
    puts("PASS: RNG accumulator accepts only successful words and wraps unsigned");
    hal_status = HAL_ERROR;
    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_TRUE(fallback_logs == 3U);
    TEST_ASSERT_TRUE(bsp_random_word(&value) && fallback_logs == 3U);
    puts("PASS: fallback logs once per transition and re-arms after hardware recovery");
    hal_status = HAL_OK;
    test_rng_flags = RNG_IT_SEI;
    TEST_ASSERT_TRUE(bsp_random_word(&value) && value == 0x12345678U && recovery_calls == 1U);
    puts("PASS: latched seed interrupt uses existing HAL recovery before reading entropy");
}

void setUp(void)
{
    HAL_RNG_GenerateRandomNumber_Stub(read_rng);
    RNG_RecoverSeedError_Stub(recover_rng);
}
void tearDown(void)
{
}
/*** end of file ***/
