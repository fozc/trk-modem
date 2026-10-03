/*
 * test_bsp_random.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * RNG failure boundaries, accepted-word accumulation and fallback behavior.
 */
#include "unity.h"
#include "bsp_random.h"
#include "http_session_token.h"
#include "mock_stm32u3xx_hal_rng.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

RNG_HandleTypeDef hrng;
uint32_t test_rng_flags;
static uint32_t flags_after_read;
static uint32_t next_word;
static uint32_t read_count;
static HAL_StatusTypeDef read_status;

static HAL_StatusTypeDef read_rng(RNG_HandleTypeDef *handle,
                                 uint32_t *random32bit, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_PTR(&hrng, handle);
    read_count++;
    test_rng_flags = flags_after_read;
    *random32bit = next_word;
    return read_status;
}

void setUp(void)
{
    test_rng_flags = 0U;
    flags_after_read = 0U;
    next_word = 0x12345678U;
    read_count = 0U;
    read_status = HAL_OK;
    HAL_RNG_GenerateRandomNumber_StubWithCallback(read_rng);
}

void tearDown(void)
{
}

void test_rng_null_output_does_not_read_hardware(void)
{
    TEST_ASSERT_FALSE(bsp_random_word(NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, read_count);
}

void test_rng_success_adds_only_the_accepted_word(void)
{
    uint32_t before = bsp_get_random_accumulator();
    uint32_t value = 0U;

    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_EQUAL_UINT32(next_word, value);
    TEST_ASSERT_EQUAL_UINT32(before + next_word, bsp_get_random_accumulator());
}

void test_rng_read_error_uses_fallback_and_preserves_accumulator(void)
{
    uint32_t before = bsp_get_random_accumulator();
    uint32_t value = 99U;
    read_status = HAL_ERROR;

    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_NOT_EQUAL(0U, value);
    TEST_ASSERT_EQUAL_UINT32(before, bsp_get_random_accumulator());
}

void test_rng_clock_flags_prevent_hardware_read(void)
{
    uint32_t value = 99U;
    test_rng_flags = RNG_FLAG_CECS;
    TEST_ASSERT_TRUE(bsp_random_word(&value));
    test_rng_flags = RNG_IT_CEI;
    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_NOT_EQUAL(0U, value);
    TEST_ASSERT_EQUAL_UINT32(0U, read_count);
}

void test_rng_error_after_read_uses_fallback_without_accumulating(void)
{
    uint32_t before = bsp_get_random_accumulator();
    uint32_t value = 99U;
    flags_after_read = RNG_IT_SEI;

    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_NOT_EQUAL(0U, value);
    TEST_ASSERT_EQUAL_UINT32(before, bsp_get_random_accumulator());
}

void test_rng_current_seed_error_reaches_hal_recovery(void)
{
    uint32_t value = 0U;
    test_rng_flags = RNG_FLAG_SECS | RNG_IT_SEI;

    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_EQUAL_UINT32(1U, read_count);
    TEST_ASSERT_EQUAL_UINT32(next_word, value);
}

void test_rng_accumulator_wraps_as_unsigned(void)
{
    uint32_t value = 0U;
    next_word = UINT32_MAX - bsp_get_random_accumulator() + 2U;

    TEST_ASSERT_TRUE(bsp_random_word(&value));
    TEST_ASSERT_EQUAL_UINT32(1U, bsp_get_random_accumulator());
}

void test_fallback_produces_valid_tokens_and_advances_state(void)
{
    char first[HTTP_SESSION_TOKEN_SIZE];
    char second[HTTP_SESSION_TOKEN_SIZE];
    char query[HTTP_SESSION_TOKEN_SIZE + 2U] = "t=";

    read_status = HAL_ERROR;
    bsp_random_fallback_seed(UINT32_MAX, UINT32_MAX, UINT32_MAX);
    TEST_ASSERT_TRUE(http_session_token_generate(first));
    TEST_ASSERT_TRUE(http_session_token_generate(second));
    TEST_ASSERT_EQUAL_UINT32(HTTP_SESSION_TOKEN_HEX_LEN, strlen(first));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first, second));
    memcpy(query + 2U, first, sizeof(first));
    TEST_ASSERT_TRUE(http_session_token_matches(first, query));
    TEST_ASSERT_EQUAL_UINT32(8U, read_count);
}

void test_fallback_null_output_does_not_read_hardware_or_accumulate(void)
{
    uint32_t before = bsp_get_random_accumulator();

    TEST_ASSERT_FALSE(bsp_random_word(NULL));
    TEST_ASSERT_EQUAL_UINT32(before, bsp_get_random_accumulator());
    TEST_ASSERT_EQUAL_UINT32(0U, read_count);
}

/*** end of file ***/
