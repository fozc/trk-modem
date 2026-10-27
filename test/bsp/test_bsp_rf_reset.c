/*
 * test_bsp_rf_reset.c
 *
 *  Created on: Oct 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify GPIO ordering at the production RF reset hardware boundary.
 */
#include "unity.h"
#include <stdint.h>

static uint32_t port;
static uint32_t operations[8];
static size_t operation_count;
#define RF_RESET_GPIO_Port (&port)
#define RF_RESET_Pin 1024U
#define LL_GPIO_OUTPUT_OPENDRAIN 1U
#define LL_GPIO_PULL_NO 0U
#define LL_GPIO_MODE_OUTPUT 1U
#define LL_GPIO_MODE_INPUT 0U

static void LL_GPIO_ResetOutputPin(uint32_t *gpio, uint32_t pin)
{
    TEST_ASSERT_EQUAL_PTR(&port, gpio);
    TEST_ASSERT_EQUAL_UINT32(RF_RESET_Pin, pin);
    operations[operation_count++] = 1U;
}

static void LL_GPIO_SetPinOutputType(uint32_t *gpio, uint32_t pin, uint32_t type)
{
    TEST_ASSERT_EQUAL_PTR(&port, gpio);
    TEST_ASSERT_EQUAL_UINT32(RF_RESET_Pin, pin);
    TEST_ASSERT_EQUAL_UINT32(LL_GPIO_OUTPUT_OPENDRAIN, type);
    operations[operation_count++] = 2U;
}

static void LL_GPIO_SetPinPull(uint32_t *gpio, uint32_t pin, uint32_t pull)
{
    TEST_ASSERT_EQUAL_PTR(&port, gpio);
    TEST_ASSERT_EQUAL_UINT32(RF_RESET_Pin, pin);
    TEST_ASSERT_EQUAL_UINT32(LL_GPIO_PULL_NO, pull);
    operations[operation_count++] = 3U;
}

static void LL_GPIO_SetPinMode(uint32_t *gpio, uint32_t pin, uint32_t mode)
{
    TEST_ASSERT_EQUAL_PTR(&port, gpio);
    TEST_ASSERT_EQUAL_UINT32(RF_RESET_Pin, pin);
    operations[operation_count++] = (mode == LL_GPIO_MODE_OUTPUT) ? 4U : 5U;
}

#include "../../Application/bsp/bsp_rf_reset.c"

void setUp(void)
{
    operation_count = 0U;
}

void tearDown(void)
{
}

void test_assert_preloads_low_and_open_drain_before_enabling_output(void)
{
    const uint32_t expected[] = {1U, 2U, 3U, 4U};
    bsp_rf_reset(true);
    TEST_ASSERT_EQUAL_size_t(4U, operation_count);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, operations, 4U);
}

void test_release_returns_to_input_without_driving_high(void)
{
    bsp_rf_reset(false);
    TEST_ASSERT_EQUAL_size_t(1U, operation_count);
    TEST_ASSERT_EQUAL_UINT32(5U, operations[0]);
}

/*** end of file ***/
