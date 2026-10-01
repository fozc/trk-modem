/*
 * test_system_status.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies aggregation and temperature tracking with fake device inputs.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mock_adc.h"
#include "mock_digital_input.h"
#include "mock_gsm_info.h"
#include "mock_power_board.h"
#include "mock_relay.h"
#include "system_status.h"

static power_board_telemetry_t fake_telemetry;
static uint8_t fake_din[DIN_CH_COUNT];
static uint8_t fake_dip[DIP_SW_COUNT];
static bool fake_relay[RELAY_CH_COUNT];
static uint16_t fake_voltage[ADC_CH_COUNT];
static int16_t fake_mcu_temp;
static uint8_t fake_signal;
static network_generation_t fake_generation;
static uint32_t telemetry_read_count;

static bool get_telemetry_callback(power_board_telemetry_t *output,
                                   int call_count)
{
    (void)call_count;
    *output = fake_telemetry;
    telemetry_read_count++;
    return fake_telemetry.valid;
}

static uint8_t get_digital_input_callback(din_channel_t channel,
                                          int call_count)
{
    (void)call_count;
    return fake_din[channel];
}

static uint8_t get_dip_switch_callback(dip_sw_channel_t channel,
                                       int call_count)
{
    (void)call_count;
    return fake_dip[channel];
}

static bool get_relay_callback(relay_channel_t channel, int call_count)
{
    (void)call_count;
    return fake_relay[channel];
}

static uint16_t get_voltage_callback(adc_channel_t channel, int call_count)
{
    (void)call_count;
    return fake_voltage[channel];
}

static int16_t get_mcu_temp_callback(int call_count)
{
    (void)call_count;
    return fake_mcu_temp;
}

static uint8_t get_signal_callback(int call_count)
{
    (void)call_count;
    return fake_signal;
}

static network_generation_t get_generation_callback(int call_count)
{
    (void)call_count;
    return fake_generation;
}

void setUp(void)
{
    memset(&fake_telemetry, 0, sizeof(fake_telemetry));
    memset(fake_din, 0, sizeof(fake_din));
    memset(fake_dip, 0, sizeof(fake_dip));
    memset(fake_relay, 0, sizeof(fake_relay));
    memset(fake_voltage, 0, sizeof(fake_voltage));
    fake_mcu_temp = 0;
    fake_signal = 0U;
    fake_generation = NETWORK_GEN_UNKNOWN;
    telemetry_read_count = 0U;
    power_board_get_telemetry_StubWithCallback(get_telemetry_callback);
    digital_input_get_StubWithCallback(get_digital_input_callback);
    dip_switch_get_StubWithCallback(get_dip_switch_callback);
    relay_is_on_StubWithCallback(get_relay_callback);
    adc_get_voltage_mv_StubWithCallback(get_voltage_callback);
    adc_get_mcu_temp_c_StubWithCallback(get_mcu_temp_callback);
    gsm_info_get_signal_quality_StubWithCallback(get_signal_callback);
    get_network_generation_StubWithCallback(get_generation_callback);
    system_status_init();
}

void tearDown(void)
{
}

void test_system_status_maps_all_external_inputs(void)
{
    const system_status_t *status;

    fake_din[DIN_CH_1] = 1U;
    fake_din[DIN_CH_3] = 1U;
    fake_dip[DIP_SW_2] = 1U;
    fake_relay[RELAY_CH_1] = true;
    fake_voltage[ADC_CH_3V3] = 3301U;
    fake_voltage[ADC_CH_3V8] = 3802U;
    fake_voltage[ADC_CH_5V] = 5003U;
    fake_mcu_temp = 42;
    fake_signal = 19U;
    fake_generation = NETWORK_GEN_4G;
    fake_telemetry = (power_board_telemetry_t){
        .valid = true,
        .vpv_mv = 18000U,
        .vbat_mv = 12600U,
        .ibat_ma = -321,
        .batt_cap_ah = 12U,
        .soc_x10 = 875,
        .soh_x10 = 963U,
        .batt_temp_x10 = 245,
        .chg_stat = 2U,
        .board_temp_c = 31,
        .heater_state = 1U
    };

    status = system_status_get();

    TEST_ASSERT_EQUAL_UINT32(1U, telemetry_read_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fake_din, status->din, DIN_CH_COUNT);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fake_dip, status->dip_sw, DIP_SW_COUNT);
    TEST_ASSERT_EQUAL_UINT8(1U, status->rly[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, status->rly[1]);
    TEST_ASSERT_EQUAL_UINT16(3301U, status->v3v3);
    TEST_ASSERT_EQUAL_UINT16(3802U, status->v3v8);
    TEST_ASSERT_EQUAL_UINT16(5003U, status->v5v);
    TEST_ASSERT_EQUAL_INT16(0, status->panel_current);
    TEST_ASSERT_EQUAL_UINT16(18000U, status->panel_voltage);
    TEST_ASSERT_EQUAL_UINT16(12600U, status->battery_voltage);
    TEST_ASSERT_EQUAL_INT16(-321, status->battery_current);
    TEST_ASSERT_EQUAL_UINT16(12U, status->battery_capacity);
    TEST_ASSERT_EQUAL_INT16(875, status->battery_charge_x10);
    TEST_ASSERT_EQUAL_INT16(245, status->battery_temp_x10);
    TEST_ASSERT_EQUAL_INT16(875, status->battery_soc_x10);
    TEST_ASSERT_EQUAL_UINT16(963U, status->battery_soh_x10);
    TEST_ASSERT_EQUAL_UINT8(2U, status->charge_state);
    TEST_ASSERT_EQUAL_INT8(19, status->gsm_signal);
    TEST_ASSERT_EQUAL_UINT8(NETWORK_GEN_4G, status->gsm_rat);
    TEST_ASSERT_EQUAL_INT8(31, status->temp);
    TEST_ASSERT_EQUAL_INT8(42, status->tdie_temp);
    TEST_ASSERT_EQUAL_UINT8(1U, status->heater_state);
    TEST_ASSERT_EQUAL_UINT16(0U, status->heater_power);
}

void test_system_status_tracks_temperature_extremes_across_updates(void)
{
    const system_status_t *status;

    fake_telemetry.board_temp_c = 25;
    fake_mcu_temp = 40;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(25, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(25, status->temp_max);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_min);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_max);

    fake_telemetry.board_temp_c = -10;
    fake_mcu_temp = -20;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(-10, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(25, status->temp_max);
    TEST_ASSERT_EQUAL_INT8(-20, status->tdie_temp_min);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_max);

    fake_telemetry.board_temp_c = 60;
    fake_mcu_temp = 70;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(-10, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(60, status->temp_max);
    TEST_ASSERT_EQUAL_INT8(-20, status->tdie_temp_min);
    TEST_ASSERT_EQUAL_INT8(70, status->tdie_temp_max);
}

void test_system_status_ignores_power_board_temperature_error(void)
{
    const system_status_t *status;

    fake_telemetry.board_temp_c = 18;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(18, status->temp);

    fake_telemetry.board_temp_c = POWER_BOARD_BOARD_TEMP_ERROR;
    status = system_status_get();

    TEST_ASSERT_EQUAL_INT8(18, status->temp);
    TEST_ASSERT_EQUAL_INT8(18, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(18, status->temp_max);
}

/*** end of file ***/
