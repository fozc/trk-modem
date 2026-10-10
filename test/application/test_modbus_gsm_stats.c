/*
 * test_modbus_gsm_stats.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify every GSM register, socket selection and rejected read boundary.
 */

#include "unity.h"
#include "modbus_gsm_stats.h"
#include "mock_modbus_config.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_info.h"

void setUp(void)
{
    modbus_config_get_last_error_time_IgnoreAndReturn(0x1234ABCDU);
}

void tearDown(void)
{
}

static void expect_register(uint16_t address, uint16_t expected)
{
    uint16_t value = 0xA5A5U;
    TEST_ASSERT_TRUE(modbus_gsm_stats_read(address, &value));
    TEST_ASSERT_EQUAL_UINT16(expected, value);
}

void test_state_signal_rat_and_error_registers_keep_full_values(void)
{
    gsm_get_main_state_ExpectAndReturn(5U);
    expect_register(49400U, 5U);
    gsm_info_get_signal_quality_ExpectAndReturn(99U);
    expect_register(49401U, 99U);
    get_network_generation_ExpectAndReturn(NETWORK_GEN_4G);
    expect_register(49402U, 4U);
    modbus_config_get_last_error_code_ExpectAndReturn(UINT8_MAX);
    expect_register(49403U, UINT8_MAX);
}

void test_error_timestamp_uses_abcd_word_order(void)
{
    expect_register(49404U, 0x1234U);
    expect_register(49405U, 0xABCDU);
    modbus_config_get_last_error_time_IgnoreAndReturn(0U);
    expect_register(49404U, 0U);
    expect_register(49405U, 0U);
}

void test_socket_registers_select_web_iec104_and_dialer_independently(void)
{
    gsm_get_socket_state_ExpectAndReturn(LISTENER_SOCKET, SOCKET_LISTENING);
    expect_register(49406U, SOCKET_LISTENING);
    gsm_get_socket_state_ExpectAndReturn(IEC104_LISTENER_SOCKET,
                                       SOCKET_CONNECTED);
    expect_register(49407U, SOCKET_CONNECTED);
    gsm_get_socket_state_ExpectAndReturn(DIALER_SOCKET, SOCKET_FAIL);
    expect_register(49408U, SOCKET_FAIL);
}

void test_invalid_address_or_null_returns_false_without_changing_output(void)
{
    const uint16_t invalid[] = {0U, 49399U, 49409U, UINT16_MAX};
    for (size_t index = 0U; sizeof(invalid) / sizeof(invalid[0]) > index;
         index++)
    {
        uint16_t value = 0xA5A5U;
        TEST_ASSERT_FALSE(modbus_gsm_stats_read(invalid[index], &value));
        TEST_ASSERT_EQUAL_HEX16(0xA5A5U, value);
    }
    TEST_ASSERT_FALSE(modbus_gsm_stats_read(49400U, NULL));
    TEST_ASSERT_FALSE(modbus_gsm_stats_read(49408U, NULL));
}

/*** end of file ***/
