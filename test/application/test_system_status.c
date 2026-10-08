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
#include "mock_power_board_scp.h"
#include "mock_relay.h"
#include "system_status.h"
#include "system_status_json.h"

TEST_SOURCE_FILE("system_status_json.c")
TEST_SOURCE_FILE("xprintf.c")

static rf_scp_power_summary_t fake_telemetry;
static uint16_t fake_valid_fields;
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void) { return 0U; }
static uint8_t fake_din[DIN_CH_COUNT];
static uint8_t fake_dip[DIP_SW_COUNT];
static bool fake_relay[RELAY_CH_COUNT];
static uint16_t fake_voltage[ADC_CH_COUNT];
static int16_t fake_mcu_temp;
static uint8_t fake_signal;
static network_generation_t fake_generation;
static uint32_t telemetry_read_count;
static uint8_t fake_telemetry_age;
static uint32_t fake_alarm_mask;

static bool get_telemetry_callback(uint32_t now_ms,
                                   power_board_snapshot_t *output,
                                   int call_count)
{
    (void)call_count;
    (void)now_ms;
    *output = (power_board_snapshot_t){.summary = fake_telemetry,
                                      .valid_fields = fake_valid_fields,
                                      .telemetry_age_sec = fake_telemetry_age,
                                      .active_alarms = fake_alarm_mask};
    telemetry_read_count++;
    return true;
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
    fake_valid_fields = UINT16_MAX;
    power_board_get_snapshot_StubWithCallback(get_telemetry_callback);
    digital_input_get_StubWithCallback(get_digital_input_callback);
    dip_switch_get_StubWithCallback(get_dip_switch_callback);
    relay_is_on_StubWithCallback(get_relay_callback);
    adc_get_voltage_mv_StubWithCallback(get_voltage_callback);
    adc_get_mcu_temp_c_StubWithCallback(get_mcu_temp_callback);
    gsm_info_get_signal_quality_StubWithCallback(get_signal_callback);
    get_network_generation_StubWithCallback(get_generation_callback);
    gsm_info_get_signal_quality_2G_IgnoreAndReturn(0U);
    gsm_info_get_signal_quality_3G_IgnoreAndReturn(0U);
    gsm_info_get_signal_quality_4G_IgnoreAndReturn(0U);
    gsm_info_get_4G_rsrq_IgnoreAndReturn(0U);
    gsm_info_get_creg_IgnoreAndReturn(0U);
    gsm_info_get_cgreg_IgnoreAndReturn(0U);
    gsm_info_get_cereg_IgnoreAndReturn(0U);
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
    fake_telemetry = (rf_scp_power_summary_t){
        .pv_mv = 18000U,
        .battery_mv = 12600U,
        .battery_ma = -321,
        .capacity_ah = 12U,
        .soc_tenths = 875,
        .soh_percent = 96U,
        .battery_temperature = 24,
        .charge_phase = 2U,
        .board_temperature = 31,
        .dc_mv = 13500U,
        .input_ma = 420,
        .input_power_10mw = 5800,
        .battery_power_10mw = -1200,
        .source = 1U,
    };
    fake_telemetry_age = 7U;
    fake_alarm_mask = 0x00089001U;

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
    TEST_ASSERT_EQUAL_INT16(240, status->battery_temp_x10);
    TEST_ASSERT_EQUAL_INT16(875, status->battery_soc_x10);
    TEST_ASSERT_EQUAL_UINT16(960U, status->battery_soh_x10);
    TEST_ASSERT_EQUAL_UINT8(2U, status->charge_state);
    TEST_ASSERT_EQUAL_UINT16(13500U, status->dc_voltage);
    TEST_ASSERT_EQUAL_INT16(420, status->input_current);
    TEST_ASSERT_EQUAL_INT16(5800, status->input_power_10mw);
    TEST_ASSERT_EQUAL_INT16(-1200, status->battery_power_10mw);
    TEST_ASSERT_EQUAL_UINT8(1U, status->power_source);
    TEST_ASSERT_EQUAL_UINT8(7U, status->telemetry_age);
    TEST_ASSERT_EQUAL_UINT32(0x00089001U, status->alarm_mask);
    TEST_ASSERT_EQUAL_INT8(19, status->gsm_signal);
    TEST_ASSERT_EQUAL_UINT8(NETWORK_GEN_4G, status->gsm_rat);
    TEST_ASSERT_EQUAL_INT8(31, status->temp);
    TEST_ASSERT_EQUAL_INT8(42, status->tdie_temp);
    TEST_ASSERT_EQUAL_UINT8(0U, status->heater_state);
    TEST_ASSERT_EQUAL_UINT16(0U, status->heater_power);
}

void test_system_status_tracks_temperature_extremes_across_updates(void)
{
    const system_status_t *status;

    fake_telemetry.board_temperature = 25;
    fake_mcu_temp = 40;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(25, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(25, status->temp_max);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_min);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_max);

    fake_telemetry.board_temperature = -10;
    fake_mcu_temp = -20;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(-10, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(25, status->temp_max);
    TEST_ASSERT_EQUAL_INT8(-20, status->tdie_temp_min);
    TEST_ASSERT_EQUAL_INT8(40, status->tdie_temp_max);

    fake_telemetry.board_temperature = 60;
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

    fake_telemetry.board_temperature = 18;
    status = system_status_get();
    TEST_ASSERT_EQUAL_INT8(18, status->temp);

    fake_telemetry.board_temperature = INT8_MIN;
    fake_valid_fields &= (uint16_t)~POWER_VALID_BOARD_TEMP;
    status = system_status_get();

    TEST_ASSERT_EQUAL_INT8(18, status->temp);
    TEST_ASSERT_EQUAL_INT8(18, status->temp_min);
    TEST_ASSERT_EQUAL_INT8(18, status->temp_max);
}

void test_monitor_json_uses_null_for_invalid_or_unavailable_power_values(void)
{
    char buffer[2048];
    size_t length;

    fake_valid_fields = 0U;
    fake_telemetry.soc_tenths = -125;
    fake_telemetry.battery_mv = 24000U;
    const system_status_t *status = system_status_get();

    TEST_ASSERT_TRUE(system_status_json_build(status, buffer,
                                               sizeof(buffer), &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"BatterySOC\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"BataryaVoltaji\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"HeaterState\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"PanelAkimi\":null"));
    fake_valid_fields = UINT16_MAX;
    status = system_status_get();
    TEST_ASSERT_TRUE(system_status_json_build(status, buffer,
                                               sizeof(buffer), &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"BatterySOC\":-125"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"BataryaVoltaji\":24000"));
}

void test_power_board_extra_fields_json_gating(void)
{
    char buffer[2048];
    size_t length;

    fake_valid_fields = 0U;
    fake_telemetry.dc_mv = 13500U;
    fake_telemetry.input_ma = 420;
    fake_telemetry.input_power_10mw = 5800;
    fake_telemetry.battery_power_10mw = -1200;
    fake_telemetry.source = 1U;
    fake_telemetry_age = 7U;
    fake_alarm_mask = 0x00089001U;
    const system_status_t *status = system_status_get();

    TEST_ASSERT_TRUE(system_status_json_build(status, buffer,
                                               sizeof(buffer), &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"DcVoltaji\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"GirisAkimi\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"GirisGucu\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"AkuGucu\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"Kaynak\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"TelemetriYasi\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"AlarmMaskesi\":null"));

    fake_valid_fields = POWER_VALID_ADC | POWER_VALID_CHARGER |
                        POWER_VALID_POWER | POWER_VALID_SOURCE |
                        POWER_VALID_SUMMARY | POWER_VALID_ALARMS;
    status = system_status_get();
    TEST_ASSERT_TRUE(system_status_json_build(status, buffer,
                                               sizeof(buffer), &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"DcVoltaji\":13500"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"GirisAkimi\":420"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"GirisGucu\":5800"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"AkuGucu\":-1200"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"Kaynak\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"TelemetriYasi\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"AlarmMaskesi\":561153"));
}

void test_monitor_json_rejects_small_buffer_without_partial_response(void)
{
    char buffer[32];
    size_t length = 99U;

    TEST_ASSERT_FALSE(system_status_json_build(system_status_get(), buffer,
                                               sizeof(buffer), &length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_EQUAL_STRING("", buffer);
}

void test_unknown_capacity_signature_produces_a_fresh_maintenance_flag(void)
{
    char json[2048];
    size_t length = 0U;
    fake_valid_fields = POWER_VALID_SUMMARY;
    fake_telemetry.capacity_ah = 7U;
    fake_telemetry.flags2 = 0x30U;
    const system_status_t *status = system_status_get();
    TEST_ASSERT_TRUE(status->battery_capacity_unknown);
    TEST_ASSERT_TRUE(system_status_json_build(status, json, sizeof(json),
                                              &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"BatteryCapacityUnknown\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Capacity\":null"));
    fake_valid_fields = 0U;
    status = system_status_get();
    TEST_ASSERT_FALSE(status->battery_capacity_unknown);
    TEST_ASSERT_TRUE(system_status_json_build(status, json, sizeof(json),
                                              &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"BatteryCapacityUnknown\":null"));
}

void test_valid_seven_ah_capacity_does_not_raise_the_maintenance_flag(void)
{
    fake_telemetry.capacity_ah = 7U;
    fake_telemetry.flags2 = 0x34U;
    TEST_ASSERT_FALSE(system_status_get()->battery_capacity_unknown);
    fake_telemetry.flags2 = 0x20U;
    TEST_ASSERT_FALSE(system_status_get()->battery_capacity_unknown);
}

/*** end of file ***/
