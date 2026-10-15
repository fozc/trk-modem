/*
 * test_modbus_map_json.c
 *
 *  Created on: Oct 15, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify RO metadata, buffer boundaries and failure output.
 */
#include "unity.h"
#include "modbus_map_json.h"
#include <string.h>

TEST_SOURCE_FILE("xprintf.c")

static char buffer[8192];

void setUp(void)
{
    memset(buffer, 0x5A, sizeof(buffer));
}

void tearDown(void)
{
}

void test_metadata_uses_production_addresses_and_field_widths(void)
{
    size_t length = 0U;
    TEST_ASSERT_TRUE(modbus_map_json_build(buffer, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_UINT32(strlen(buffer), length);
    TEST_ASSERT_LESS_THAN_UINT32(7800U, length);
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"AddressBase\":40000"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"FeederCount\":4"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"PhaseOffset\":4"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"PhaseStride\":24"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"anlik_akim\",1,2,\"FLOAT32\",\"A\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"rtc_unix\",49006,2,\"UINT32\",\"Unix s\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"battery_ma\",49214,1,\"INT16\",\"mA\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"cell_voltage_mv\",49345,16,\"UINT16\",\"mV\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"gsm_state\",49400,1,\"UINT16\",\"\"]"));
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "[\"modem_reset\",50001,1,\"UINT16\",\"1\"]"));
    TEST_ASSERT_EQUAL_CHAR('}', buffer[length - 1U]);
}

void test_small_buffer_fails_without_writing_past_capacity(void)
{
    size_t length = 123U;
    TEST_ASSERT_FALSE(modbus_map_json_build(buffer, 8U, &length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_EQUAL_CHAR(0, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5AU, buffer[8]);
}

void test_exact_capacity_requires_space_for_nul(void)
{
    size_t length = 0U;
    TEST_ASSERT_TRUE(modbus_map_json_build(buffer, sizeof(buffer), &length));
    const size_t needed = length;
    TEST_ASSERT_FALSE(modbus_map_json_build(buffer, needed, &length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_TRUE(modbus_map_json_build(buffer, needed + 1U, &length));
    TEST_ASSERT_EQUAL_UINT32(needed, length);
}

void test_invalid_arguments_clear_only_writable_outputs(void)
{
    size_t length = 123U;
    TEST_ASSERT_FALSE(modbus_map_json_build(NULL, sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_FALSE(modbus_map_json_build(buffer, 0U, &length));
    TEST_ASSERT_EQUAL_HEX8(0x5AU, buffer[0]);
    TEST_ASSERT_FALSE(modbus_map_json_build(buffer, sizeof(buffer), NULL));
    TEST_ASSERT_EQUAL_CHAR(0, buffer[0]);
}

/*** end of file ***/
