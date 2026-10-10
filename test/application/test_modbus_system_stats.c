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

static void expect_register(uint16_t address, uint16_t expected)
{
    uint16_t value = 0xA5A5U;
    TEST_ASSERT_TRUE(modbus_system_stats_read(address, &value));
    TEST_ASSERT_EQUAL_UINT16(expected, value);
}

void test_all_calendar_registers_keep_wire_order_and_year_offset(void)
{
    rtc_get_second_ExpectAndReturn(59U);
    expect_register(49000U, 59U);
    rtc_get_minute_ExpectAndReturn(58U);
    expect_register(49001U, 58U);
    rtc_get_hour_ExpectAndReturn(23U);
    expect_register(49002U, 23U);
    rtc_get_day_ExpectAndReturn(31U);
    expect_register(49003U, 31U);
    rtc_get_month_ExpectAndReturn(12U);
    expect_register(49004U, 12U);
    rtc_get_year_ExpectAndReturn(99U);
    expect_register(49005U, 2099U);
}

void test_epoch_and_raw_uptime_use_high_word_before_low_word(void)
{
    rtc_get_unix_epoch_ExpectAndReturn(0x1234ABCDU);
    expect_register(49006U, 0x1234U);
    rtc_get_unix_epoch_ExpectAndReturn(0x1234ABCDU);
    expect_register(49007U, 0xABCDU);
    bsp_get_tick_ExpectAndReturn(0x5678EF01U);
    expect_register(49014U, 0x5678U);
    bsp_get_tick_ExpectAndReturn(0x5678EF01U);
    expect_register(49015U, 0xEF01U);
}

void test_rail_registers_select_correct_adc_channels(void)
{
    adc_get_voltage_mv_ExpectAndReturn(ADC_CH_5V, 5001U);
    expect_register(49011U, 5001U);
    adc_get_voltage_mv_ExpectAndReturn(ADC_CH_3V3, 3302U);
    expect_register(49012U, 3302U);
    adc_get_voltage_mv_ExpectAndReturn(ADC_CH_3V8, 3803U);
    expect_register(49013U, 3803U);
    digital_input_get_all_ExpectAndReturn(UINT8_MAX);
    expect_register(49016U, 0x00FFU);
}

void test_uptime_minute_boundary_and_full_width_tick(void)
{
    bsp_get_tick_ExpectAndReturn(59999U);
    expect_register(49008U, 0U);
    bsp_get_tick_ExpectAndReturn(60000U);
    expect_register(49008U, 1U);
    bsp_get_tick_ExpectAndReturn(UINT32_MAX);
    expect_register(49008U, (uint16_t)((UINT32_MAX / 60000U) & 0xFFFFU));
}

void test_addresses_outside_dense_block_preserve_output(void)
{
    const uint16_t invalid[] = {0U, 48999U, 49017U, UINT16_MAX};
    for (size_t index = 0U; sizeof(invalid) / sizeof(invalid[0]) > index;
         index++)
    {
        uint16_t value = 0xA5A5U;
        TEST_ASSERT_FALSE(modbus_system_stats_read(invalid[index], &value));
        TEST_ASSERT_EQUAL_HEX16(0xA5A5U, value);
    }
}

/*** end of file ***/
