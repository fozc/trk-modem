/*
 * test_modbus_bms_stats.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify complete BMS register mapping and fixed-point saturation.
 */

#include "unity.h"
#include "modbus_bms_stats.h"
#include "mock_bms_reader.h"
#include <string.h>

static bms_data_t sample;
static uint32_t snapshot_count;

static void get_sample(bms_data_t *out, int call_count)
{
    (void)call_count;
    *out = sample;
    snapshot_count++;
}

void setUp(void)
{
    (void)memset(&sample, 0, sizeof(sample));
    snapshot_count = 0U;
    bms_reader_get_data_StubWithCallback(get_sample);
}

void tearDown(void)
{
}

static void expect_register(uint16_t address, uint16_t expected)
{
    uint16_t value = 0xA5A5U;
    const uint32_t before = snapshot_count;
    TEST_ASSERT_TRUE(modbus_bms_stats_read(address, &value));
    TEST_ASSERT_EQUAL_UINT16(expected, value);
    TEST_ASSERT_EQUAL_UINT32(before + 1U, snapshot_count);
}

void test_all_scalar_registers_use_documented_order_units_and_sign(void)
{
    sample = (bms_data_t)
    {
        .is_data_valid = true, .is_soh_valid = true,
        .total_voltage_v = 12.3f, .current_a = -4.5f,
        .soc_percent = 67.8f, .soh_percent = 90.1f,
        .life_heartbeat = 106U, .active_cell_count = 7U,
        .temp_sensor_count = 8U, .max_cell_mv = 109U,
        .max_cell_index = 110U, .min_cell_mv = 111U,
        .min_cell_index = 112U, .cell_diff_mv = 113U,
        .max_temp_celsius = -14, .max_temp_index = 115U,
        .min_temp_celsius = -16, .min_temp_index = 117U,
        .temp_diff_celsius = 18, .work_state = BMS_STATE_DISCHARGING,
        .charger_status = 120U, .load_status = 121U,
        .remaining_cap_ah = 12.2f, .cycle_count = 123U,
        .balance_state = 124U, .mos_status_flags = 125U,
        .average_voltage_mv = 126U, .power_w = 127U, .energy_wh = 128U,
        .mos_temperature_celsius = -29, .ambient_temperature_celsius = -30,
        .heating_temperature_celsius = -31, .heating_current_a = 132U,
        .current_limit_state = 133U, .current_limit_a = -13.4f,
        .rtc = {.year = 26U, .month = 10U, .day = 9U,
                .hour = 23U, .minute = 58U, .second = 59U},
        .remaining_charge_min = 141U, .dido_status = 142U,
        .wake_source_flags = 143U, .comm_interface_type = 144U
    };
    const uint16_t expected[] =
    {
        1U, 1U, 123U, 0xFFD3U, 678U, 901U, 106U, 7U, 8U,
        109U, 110U, 111U, 112U, 113U, 0xFFF2U, 115U, 0xFFF0U, 117U,
        18U, 2U, 120U, 121U, 122U, 123U, 124U, 125U, 126U, 127U,
        128U, 0xFFE3U, 0xFFE2U, 0xFFE1U, 132U, 133U, 0xFF7AU,
        2026U, 10U, 9U, 23U, 58U, 59U, 141U, 142U, 143U, 144U
    };
    for (uint16_t index = 0U; sizeof(expected) / sizeof(expected[0]) > index;
         index++)
    {
        expect_register((uint16_t)(49300U + index), expected[index]);
    }
}

void test_array_windows_keep_every_element_and_signed_temperature(void)
{
    for (uint16_t index = 0U; BMS_MAX_CELL_COUNT > index; index++)
    {
        sample.cell_voltage_mv[index] = (uint16_t)(3000U + index);
        expect_register((uint16_t)(49345U + index),
                        sample.cell_voltage_mv[index]);
    }
    const uint16_t temp_base = 49345U + BMS_MAX_CELL_COUNT;
    for (uint16_t index = 0U; BMS_MAX_TEMP_SENSORS > index; index++)
    {
        sample.temperatures_celsius[index] = (int16_t)(-40 + (int16_t)index);
        expect_register((uint16_t)(temp_base + index),
                        (uint16_t)sample.temperatures_celsius[index]);
    }
    const uint16_t balance_base = temp_base + BMS_MAX_TEMP_SENSORS;
    for (uint16_t index = 0U; 3U > index; index++)
    {
        sample.balance_position[index] = (uint16_t)(0x1001U << index);
        expect_register((uint16_t)(balance_base + index),
                        sample.balance_position[index]);
    }
    for (uint16_t index = 0U; 7U > index; index++)
    {
        sample.fault_codes[index] = (uint16_t)(0xAA00U + index);
        expect_register((uint16_t)(balance_base + 3U + index),
                        sample.fault_codes[index]);
    }
}

void test_unsigned_scaling_rounds_clamps_and_keeps_zero(void)
{
    const float values[] = {-1.0f, 0.0f, 1.24f, 1.26f, 7000.0f};
    const uint16_t expected[] = {0U, 0U, 12U, 13U, UINT16_MAX};
    const uint16_t addresses[] = {49302U, 49304U, 49305U, 49322U};
    for (size_t index = 0U; sizeof(values) / sizeof(values[0]) > index; index++)
    {
        sample.total_voltage_v = values[index];
        sample.soc_percent = values[index];
        sample.soh_percent = values[index];
        sample.remaining_cap_ah = values[index];
        const size_t field_count = sizeof(addresses) / sizeof(addresses[0]);
        for (size_t field = 0U; field_count > field; field++)
        {
            expect_register(addresses[field], expected[index]);
        }
    }
}

void test_signed_scaling_rounds_both_signs_and_saturates_at_int16_limits(void)
{
    const float values[] = {0.0f, 1.24f, 1.26f, -1.24f, -1.26f,
                            4000.0f, -4000.0f};
    const uint16_t expected[] = {0U, 12U, 13U, 0xFFF4U, 0xFFF3U,
                                0x7FFFU, 0x8000U};
    for (size_t index = 0U; sizeof(values) / sizeof(values[0]) > index; index++)
    {
        sample.current_a = values[index];
        sample.current_limit_a = values[index];
        expect_register(49303U, expected[index]);
        expect_register(49334U, expected[index]);
    }
}

void test_validity_flags_are_independent_and_invalid_addresses_preserve_output(
    void)
{
    expect_register(49300U, 0U);
    expect_register(49301U, 0U);
    sample.is_data_valid = true;
    expect_register(49300U, 1U);
    expect_register(49301U, 0U);
    sample.is_data_valid = false;
    sample.is_soh_valid = true;
    expect_register(49300U, 0U);
    expect_register(49301U, 1U);
    const uint16_t end =
        49345U + BMS_MAX_CELL_COUNT + BMS_MAX_TEMP_SENSORS + 10U;
    const uint16_t invalid[] = {0U, 49299U, end, UINT16_MAX};
    const uint32_t before = snapshot_count;
    for (size_t index = 0U; sizeof(invalid) / sizeof(invalid[0]) > index;
         index++)
    {
        uint16_t value = 0xA5A5U;
        TEST_ASSERT_FALSE(modbus_bms_stats_read(invalid[index], &value));
        TEST_ASSERT_EQUAL_HEX16(0xA5A5U, value);
    }
    TEST_ASSERT_EQUAL_UINT32(before, snapshot_count);
}

/*** end of file ***/
