/*
 * test_modbus_config_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * NVRAM boundary tests for the production configuration API.
 */
#include "unity.h"
#include "modbus_config.h"
#include "mock_nvram.h"
#include <string.h>

static modbus_configs_t config;
static breaker_t breaker;

void setUp(void)
{
    memset(&config, 0, sizeof(config));
    memset(&breaker, 0, sizeof(breaker));
    nvram_get_modbus_config_rw_IgnoreAndReturn(&config);
    nvram_get_breaker_IgnoreAndReturn(&breaker);
    modbus_config_set_last_error_time(0U);
}
void tearDown(void)
{
}
void test_modbus_config_null_preserves_config_and_valid_set_copies_input(void)
{
    const modbus_configs_t input = {
        .device_addr = 247U, .last_error_code = 0xFFU,
        .baud_rate = 115200U, .addr_aku_uyarisi = UINT16_MAX,
        .addr_modem_reset = 1U
    };
    modbus_config_set(&input);
    modbus_config_set(NULL);
    TEST_ASSERT_EQUAL_MEMORY(&input, modbus_config_get(), sizeof(input));
    TEST_ASSERT_EQUAL_PTR(&config, modbus_config_get());
}
void test_modbus_config_sync_returns_both_success_and_error_unchanged(void)
{
    nvram_sync_ExpectAndReturn(false, 0);
    TEST_ASSERT_EQUAL_INT(0, modbus_config_sync());
    nvram_sync_ExpectAndReturn(false, -7);
    TEST_ASSERT_EQUAL_INT(-7, modbus_config_sync());
}
void test_modbus_config_fields_preserve_full_width_values_without_auto_sync(void)
{
    modbus_config_set_device_addr(UINT8_MAX);
    modbus_config_set_last_error_code(UINT8_MAX);
    modbus_config_set_baud_rate(UINT32_MAX);
    modbus_config_set_addr_aku_uyarisi(UINT16_MAX);
    modbus_config_set_addr_modem_reset(UINT16_MAX);
    modbus_config_set_last_error_time(UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, modbus_config_get_device_addr());
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, modbus_config_get_last_error_code());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, modbus_config_get_baud_rate());
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX,
        modbus_config_get_addr_aku_uyarisi());
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, modbus_config_get_addr_modem_reset());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, modbus_config_get_last_error_time());
    modbus_config_set_baud_rate(0U);
    TEST_ASSERT_EQUAL_UINT32(0U, modbus_config_get_baud_rate());
}
void test_modbus_line_invalid_index_or_null_preserves_all_lines(void)
{
    modbus_line_config_t input;
    memset(&input, 0xA5, sizeof(input));
    const breaker_t before = breaker;
    TEST_ASSERT_FALSE(modbus_set_line_config(MAX_POWER_LINE_COUNT, &input));
    TEST_ASSERT_FALSE(modbus_set_line_config(UINT32_MAX, &input));
    TEST_ASSERT_FALSE(modbus_set_line_config(0U, NULL));
    TEST_ASSERT_NULL(modbus_get_line_config(MAX_POWER_LINE_COUNT));
    TEST_ASSERT_NULL(modbus_get_line_config(UINT32_MAX));
    TEST_ASSERT_FALSE(modbus_is_line_in_use(UINT32_MAX));
    TEST_ASSERT_EQUAL_MEMORY(&before, &breaker, sizeof(before));
}
void test_modbus_line_busy_rejects_write_and_preserves_previous_value(void)
{
    modbus_line_config_t input;
    memset(&input, 0xA5, sizeof(input));
    const breaker_t before = breaker;
    nvram_is_busy_ExpectAndReturn(true);
    TEST_ASSERT_FALSE(modbus_set_line_config(0U, &input));
    TEST_ASSERT_EQUAL_MEMORY(&before, &breaker, sizeof(before));
}
void test_modbus_line_first_and_last_write_copy_and_do_not_modify_neighbors(void)
{
    modbus_line_config_t input;
    memset(&input, 0xA5, sizeof(input));
    input.in_use = 2U;
    nvram_is_busy_ExpectAndReturn(false);
    TEST_ASSERT_TRUE(modbus_set_line_config(0U, &input));
    TEST_ASSERT_EQUAL_MEMORY(&input, modbus_get_line_config(0U),
        sizeof(input));
    TEST_ASSERT_TRUE(modbus_is_line_in_use(0U));
    const modbus_line_config_t first = *modbus_get_line_config(0U);
    memset(&input, 0x5A, sizeof(input));
    input.in_use = 0U;
    nvram_is_busy_ExpectAndReturn(false);
    TEST_ASSERT_TRUE(modbus_set_line_config(MAX_POWER_LINE_COUNT - 1U,
        &input));
    TEST_ASSERT_EQUAL_MEMORY(&input,
        modbus_get_line_config(MAX_POWER_LINE_COUNT - 1U), sizeof(input));
    TEST_ASSERT_FALSE(modbus_is_line_in_use(MAX_POWER_LINE_COUNT - 1U));
    TEST_ASSERT_EQUAL_MEMORY(&first, modbus_get_line_config(0U),
        sizeof(first));
    const modbus_line_config_t empty = {0};
    for (uint32_t i = 1U; i < MAX_POWER_LINE_COUNT - 1U; i++)
    {
        TEST_ASSERT_EQUAL_MEMORY(&empty, modbus_get_line_config(i),
            sizeof(empty));
    }
}
/*** end of file ***/
