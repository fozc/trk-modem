/*
 * test_modbus_system_stats.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies reset bitmask semantics and the existing register layout.
 */
#include "unity.h"
#include "modbus_system_stats.h"
#include "mock_reset_source.h"
#include "mock_bsp.h"
#include "mock_rtc.h"
#include "mock_adc.h"
#include "mock_digital_input.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_reset_reason_returns_decoded_watchdog_cause(void)
{
    uint16_t value = 0U;

    reset_source_get_flags_ExpectAndReturn(RESET_SOURCE_IWDG);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49009U, &value));
    TEST_ASSERT_EQUAL_HEX16(0x0010U, value);
}

void test_reset_reason_preserves_combined_flags(void)
{
    uint16_t value = 0U;
    reset_source_flag_t flags = (reset_source_flag_t)
        ((uint32_t)RESET_SOURCE_SOFTWARE | (uint32_t)RESET_SOURCE_IWDG |
         (uint32_t)RESET_SOURCE_FIREWALL);

    reset_source_get_flags_ExpectAndReturn(flags);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49009U, &value));
    TEST_ASSERT_EQUAL_HEX16(0x0118U, value);
}

void test_unknown_reset_reason_is_zero(void)
{
    uint16_t value = UINT16_MAX;

    reset_source_get_flags_ExpectAndReturn(RESET_SOURCE_UNKNOWN);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49009U, &value));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

void test_reset_reason_neighbors_keep_their_addresses(void)
{
    uint16_t value = 0U;

    bsp_get_tick_ExpectAndReturn(120000U);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49008U, &value));
    TEST_ASSERT_EQUAL_UINT16(2U, value);
    adc_get_mcu_temp_c_ExpectAndReturn(37);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49010U, &value));
    TEST_ASSERT_EQUAL_UINT16(37U, value);
}

void test_temperature_word_encoding_is_preserved(void)
{
    uint16_t value = 0U;

    adc_get_mcu_temp_c_ExpectAndReturn(-10);
    TEST_ASSERT_TRUE(modbus_system_stats_read(49010U, &value));
    TEST_ASSERT_EQUAL_HEX16(0xFFF6U, value);
}

/*** end of file ***/
