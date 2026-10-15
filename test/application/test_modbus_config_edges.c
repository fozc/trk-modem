/*
 * test_modbus_config_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * NVRAM boundary tests for the production configuration API.
 * Register addresses are compile-time fixed (modbus_map.h); only the serial
 * line settings and the RAM-only last-exception mirror remain in NVRAM.
 */
#include "unity.h"
#include "modbus_config.h"
#include "mock_nvram.h"
#include <string.h>

static modbus_configs_t config;

void setUp(void)
{
    memset(&config, 0, sizeof(config));
    nvram_get_modbus_config_rw_IgnoreAndReturn(&config);
    modbus_config_set_last_error_time(0U);
}
void tearDown(void)
{
}
void test_modbus_config_null_preserves_config_and_valid_set_copies_input(void)
{
    const modbus_configs_t input = {
        .device_addr = 247U, .last_error_code = 0xFFU,
        .baud_rate = 115200U
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
    modbus_config_set_last_error_time(UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, modbus_config_get_device_addr());
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, modbus_config_get_last_error_code());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, modbus_config_get_baud_rate());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, modbus_config_get_last_error_time());
    modbus_config_set_baud_rate(0U);
    TEST_ASSERT_EQUAL_UINT32(0U, modbus_config_get_baud_rate());
}
/*** end of file ***/
