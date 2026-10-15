/*
 * test_json_config_bounds.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Regression tests for JSON-to-persistent configuration boundaries.
 */
#include "unity.h"
#include "json_config.h"
#include "iec104_util.h"
#include "mock_iec104_config.h"
#include "mock_iec104_process.h"
#include "mock_modbus_config.h"
#include "mock_modbus_process.h"
#include "mock_modem_config.h"
#include "mock_rf_config.h"
#include <string.h>
#include <stdio.h>

TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")

static jiec_config_t iec;
static iec104_config_t saved_iec;
static iec104_line_config_t current_lines[MAX_ARRAYS];
static iec104_line_config_t saved_line;
static modbus_configs_t saved_modbus;

static void capture_iec(const iec104_config_t *config, int call_count)
{
    (void)call_count;
    saved_iec = *config;
}

static void capture_modbus(const modbus_configs_t *config, int call_count)
{
    (void)call_count;
    saved_modbus = *config;
}

void setUp(void)
{
    memset(current_lines, 0, sizeof(current_lines));
    memset(&saved_line, 0, sizeof(saved_line));
    memset(&iec, 0, sizeof(iec));
    memset(&saved_iec, 0, sizeof(saved_iec));
    memset(&saved_modbus, 0, sizeof(saved_modbus));
    iec104_process_config_changed_Ignore();
    (void)strcpy(iec.scada_ip_address, "192.168.1.2");
    iec.scada_port = 2404U;
    iec.periodical_send_interval = 60U;
    iec.t0_timeout = 30U;
    iec.t1_timeout = 15U;
    iec.t2_timeout = 10U;
    iec.t3_timeout = 20U;
    iec.k_max = 12U;
    iec.w_max = 8U;
    iec.common_address = 1U;
    iec.ioa_aku_uyarisi = 10000U;
    iec.ioa_modem_reset = 10001U;
    iec104_get_line_config_IgnoreAndReturn(NULL);
}

void tearDown(void)
{
}

void test_modbus_device_id_rejects_zero_and_reserved_addresses(void)
{
    jmodbus_configs_t config = {0};
    config.baud_rate = 9600U;
    TEST_ASSERT_EQUAL_INT(0,
        parse_modbus_config("{\"CihazID\":0}", &config));
    TEST_ASSERT_EQUAL_INT(0,
        parse_modbus_config("{\"CihazID\":248}", &config));
    TEST_ASSERT_EQUAL_INT(0,
        parse_modbus_config("{\"CihazID\":255}", &config));
}

void test_modbus_device_id_accepts_both_unicast_boundaries(void)
{
    jmodbus_configs_t config = {0};
    config.baud_rate = 9600U;
    TEST_ASSERT_EQUAL_INT(1,
        parse_modbus_config("{\"CihazID\":1}", &config));
    TEST_ASSERT_EQUAL_UINT8(1U, config.device_addr);
    TEST_ASSERT_EQUAL_INT(1,
        parse_modbus_config("{\"CihazID\":247}", &config));
    TEST_ASSERT_EQUAL_UINT8(247U, config.device_addr);
}

void test_modbus_setter_rejects_invalid_device_id_without_storage_calls(void)
{
    const uint8_t addresses[] = {0U, 248U, 255U};
    jmodbus_configs_t config = {0};
    config.baud_rate = 9600U;
    for (size_t index = 0U; index < sizeof(addresses); index++)
    {
        config.device_addr = addresses[index];
        /* Storage and runtime notification mocks must remain untouched. */
        TEST_ASSERT_EQUAL_INT(-1, set_modbus_config(&config));
    }
}

void test_modbus_device_id_247_reaches_storage_unchanged(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 247U;
    config.baud_rate = 9600U;
    modbus_config_get_IgnoreAndReturn(NULL);
    modbus_config_set_Stub(capture_modbus);
    modbus_config_sync_ExpectAndReturn(0);
    modbus_process_notify_config_changed_Expect();
    TEST_ASSERT_EQUAL_INT(0, set_modbus_config(&config));
    TEST_ASSERT_EQUAL_UINT8(247U, saved_modbus.device_addr);
}

void test_scada_ip_high_first_octet_is_saved(void)
{
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_HEX32(0xC0A80102U, saved_iec.scada_ip_address);
}

void test_iec_common_address_supports_existing_16_bit_range(void)
{
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{\"CommonAddr\":300}", &iec));
    TEST_ASSERT_EQUAL_UINT16(300U, iec.common_address);
    TEST_ASSERT_EQUAL_INT(1,
                          parse_iec_config("{\"CommonAddr\":65535}", &iec));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, iec.common_address);
}

void test_iec_originator_rejects_8_bit_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(1,
                          parse_iec_config("{\"OriginatorAddr\":255}", &iec));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, iec.originator_address);
    TEST_ASSERT_EQUAL_INT(0,
                          parse_iec_config("{\"OriginatorAddr\":256}", &iec));
}

void test_inactive_sbo_rejects_storage_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        parse_iec_config("{\"SBO\":false,\"SBOTimeout\":65536}", &iec));
}

void test_wellformed_unknown_key_is_skipped(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 1U;
    config.baud_rate = 9600U;

    /* Unknown keys (e.g. a stray Hat block) are skipped when well-formed;
     * only malformed structure is rejected (see the malformed test). */
    TEST_ASSERT_EQUAL_INT(1,
        parse_modbus_config(
            "{\"Hat\":{\"ADDR_R_AnlikAkim\":[65536]}}", &config));
}

void test_modbus_setter_rejects_invalid_device_id_before_writing(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 0U;
    config.baud_rate = 9600U;

    /* No mock write is expected on invalid input. */
    TEST_ASSERT_EQUAL_INT(-1, set_modbus_config(&config));
}

void test_partial_iec_update_preserves_wide_address_in_storage(void)
{
    iec.common_address = 300U;
    iec.originator_address = 255U;
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{\"Port\":2405}", &iec));
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_UINT16(300U, saved_iec.common_address);
    TEST_ASSERT_EQUAL_UINT8(255U, saved_iec.originator_address);
    TEST_ASSERT_EQUAL_UINT16(2405U, saved_iec.scada_port);
}

void test_common_address_rejects_zero_and_16_bit_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{\"CommonAddr\":0}", &iec));
    TEST_ASSERT_EQUAL_INT(0,
                          parse_iec_config("{\"CommonAddr\":65536}", &iec));
}

void test_sbo_setter_rejects_overflow_before_writing(void)
{
    iec.sbo_timeout = 65536U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_sbo_keeps_existing_active_timeout_limits(void)
{
    TEST_ASSERT_EQUAL_INT(1,
        parse_iec_config("{\"SBO\":false,\"SBOTimeout\":65535}", &iec));
    TEST_ASSERT_EQUAL_INT(1,
        parse_iec_config("{\"SBO\":true,\"SBOTimeout\":300}", &iec));
    TEST_ASSERT_EQUAL_INT(0,
        parse_iec_config("{\"SBO\":true,\"SBOTimeout\":301}", &iec));
}

void test_modbus_valid_boundary_values_reach_storage_unchanged(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 1U;
    config.baud_rate = 9600U;
    modbus_config_get_IgnoreAndReturn(NULL);
    modbus_config_set_Stub(capture_modbus);
    modbus_config_sync_ExpectAndReturn(0);
    modbus_process_notify_config_changed_Expect();

    TEST_ASSERT_EQUAL_INT(0, set_modbus_config(&config));
}

static const iec104_line_config_t *read_line(uint32_t index, int calls)
{
    (void)calls;
    return &current_lines[index];
}

static bool capture_line(uint32_t index, const iec104_line_config_t *line,
                         int calls)
{
    (void)calls;
    if (0U == index)
    {
        saved_line = *line;
    }
    return true;
}

static void enable_valid_lines(void)
{
    uint32_t *points[] =
    {
        iec.line.ioa_r_anlik_akim,
        iec.line.ioa_s_anlik_akim,
        iec.line.ioa_t_anlik_akim,
        iec.line.ioa_r_enerji_varyok,
        iec.line.ioa_s_enerji_varyok,
        iec.line.ioa_t_enerji_varyok,
        iec.line.ioa_r_yuk_akimi_varyok,
        iec.line.ioa_s_yuk_akimi_varyok,
        iec.line.ioa_t_yuk_akimi_varyok,
        iec.line.ioa_r_rfhab_varyok,
        iec.line.ioa_s_rfhab_varyok,
        iec.line.ioa_t_rfhab_varyok,
        iec.line.ioa_r_trip_failed,
        iec.line.ioa_s_trip_failed,
        iec.line.ioa_t_trip_failed
    };
    for (size_t i = 0U; i < MAX_ARRAYS; i++)
    {
        iec.line.in_use[i] = true;
        for (size_t j = 0U; j < (sizeof(points) / sizeof(points[0])); j++)
        {
            points[j][i] = 1000U + ((uint32_t)i * 100U) + (uint32_t)j;
        }
        iec.line.temporary_fault_base[i] = 100000U + ((uint32_t)i * 1000U);
        iec.line.permanent_fault_base[i] = 200000U + ((uint32_t)i * 1000U);
        current_lines[i].temporary_fault =
            iec104_make_ioa_3byte(100000U + ((uint32_t)i * 1000U));
        current_lines[i].permanent_fault =
            iec104_make_ioa_3byte(200000U + ((uint32_t)i * 1000U));
    }
    iec104_get_line_config_StopIgnore();
    iec104_get_line_config_Stub(read_line);
}

void test_iec_rejects_duplicate_global_and_point_addresses(void)
{
    iec.ioa_modem_reset = iec.ioa_aku_uyarisi;
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{}", &iec));
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    TEST_ASSERT_NOT_NULL(strstr(json_config_get_iec_address_error(),
                                "AkuUyarisi"));
    TEST_ASSERT_NOT_NULL(strstr(json_config_get_iec_address_error(),
                                "ModemReset"));
    iec.ioa_modem_reset = 10001U;
    enable_valid_lines();
    iec.line.ioa_r_anlik_akim[0] = iec.ioa_aku_uyarisi;
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{}", &iec));
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_iec_rejects_point_inside_fault_range_including_last_address(void)
{
    enable_valid_lines();
    iec.line.ioa_r_anlik_akim[1] = 100179U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    TEST_ASSERT_NOT_NULL(strstr(json_config_get_iec_address_error(),
                                "Hatlar.IOA_R_AnlikAkim[1]"));
    TEST_ASSERT_NOT_NULL(strstr(json_config_get_iec_address_error(),
                                "Hatlar.TemporaryFaultBase[0]"));
    iec.line.ioa_r_anlik_akim[1] = 100180U;
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{}", &iec));
}

void test_iec_rejects_overlapping_fault_ranges(void)
{
    enable_valid_lines();
    iec.line.permanent_fault_base[0] = 100179U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    iec.line.permanent_fault_base[0] = 100180U;
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{}", &iec));
    iec.line.temporary_fault_base[1] = 99820U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_iec_rejects_zero_and_ioa_overflow_before_writing(void)
{
    iec.ioa_aku_uyarisi = 0U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    iec.ioa_aku_uyarisi = 0x1000000U;
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{}", &iec));
    iec.ioa_aku_uyarisi = 0xFFFFFFU;
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{}", &iec));
    enable_valid_lines();
    iec.line.temporary_fault_base[6] = 0xFFFF00U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    iec.line.temporary_fault_base[6] = 0U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_iec_all_point_fields_check_range_and_uniqueness(void)
{
    enable_valid_lines();
    uint32_t *points[] =
    {
        iec.line.ioa_r_anlik_akim,
        iec.line.ioa_s_anlik_akim,
        iec.line.ioa_t_anlik_akim,
        iec.line.ioa_r_enerji_varyok,
        iec.line.ioa_s_enerji_varyok,
        iec.line.ioa_t_enerji_varyok,
        iec.line.ioa_r_yuk_akimi_varyok,
        iec.line.ioa_s_yuk_akimi_varyok,
        iec.line.ioa_t_yuk_akimi_varyok,
        iec.line.ioa_r_rfhab_varyok,
        iec.line.ioa_s_rfhab_varyok,
        iec.line.ioa_t_rfhab_varyok
    };
    for (size_t j = 0U; j < (sizeof(points) / sizeof(points[0])); j++)
    {
        uint32_t original = points[j][6];
        points[j][6] = 0U;
        TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
        points[j][6] = 0x1000000U;
        TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
        points[j][6] = iec.ioa_aku_uyarisi;
        TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
        points[j][6] = original;
    }
}

void test_iec_partial_save_preserves_fault_bases_and_inactive_settings(void)
{
    enable_valid_lines();
    iec.line.in_use[0] = false;
    current_lines[0].anlik_akim[0] = iec104_make_ioa_3byte(1234U);
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{\"Port\":2405}", &iec));
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_Stub(capture_line);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_FALSE(saved_line.in_use);
    TEST_ASSERT_EQUAL_MEMORY(&current_lines[0].temporary_fault,
                            &saved_line.temporary_fault, sizeof(ioa_3byte_t));
    TEST_ASSERT_EQUAL_MEMORY(&current_lines[0].permanent_fault,
                            &saved_line.permanent_fault, sizeof(ioa_3byte_t));
    TEST_ASSERT_EQUAL_MEMORY(&current_lines[0].anlik_akim[0],
                            &saved_line.anlik_akim[0], sizeof(ioa_3byte_t));
    iec.line.in_use[0] = true;
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_TRUE(saved_line.in_use);
    TEST_ASSERT_EQUAL_MEMORY(&current_lines[0].temporary_fault,
                            &saved_line.temporary_fault, sizeof(ioa_3byte_t));
    TEST_ASSERT_EQUAL_UINT32(1000U,
        iec104_ioa_3byte_to_uint32(saved_line.anlik_akim[0]));
}

void test_iec_fault_bases_can_be_repaired_without_factory_reset(void)
{
    enable_valid_lines();
    iec.line.temporary_fault_base[0] = 1100U;
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{}", &iec));
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config(
        "{\"Hatlar\":{\"TemporaryFaultBase\":[100000],"
        "\"PermanentFaultBase\":[200000]}}", &iec));
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_Stub(capture_line);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_UINT32(100000U,
        iec104_ioa_3byte_to_uint32(saved_line.temporary_fault));
    TEST_ASSERT_EQUAL_STRING("", json_config_get_iec_address_error());
    iec.line.in_use[0] = false;
    iec.line.temporary_fault_base[0] = 0x1000000U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_trip_failure_ioas_parse_validate_and_reach_the_saved_line(void)
{
    enable_valid_lines();
    iec104_set_line_config_StopIgnore();
    iec104_set_line_config_StubWithCallback(capture_line);
    iec104_config_set_StubWithCallback(capture_iec);
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config(
        "{\"Hatlar\":{\"IOA_R_TripFailed\":[500000],"
        "\"IOA_S_TripFailed\":[500001],\"IOA_T_TripFailed\":[500002]}}", &iec));
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_UINT32(500000U,
        iec104_ioa_3byte_to_uint32(saved_line.trip_failed[PHASE_L1]));
    TEST_ASSERT_EQUAL_UINT32(500002U,
        iec104_ioa_3byte_to_uint32(saved_line.trip_failed[PHASE_L3]));
    const iec104_line_config_t before = saved_line;
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config(
        "{\"Hatlar\":{\"IOA_R_TripFailed\":[1000]}}", &iec));
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_MEMORY(&before, &saved_line, sizeof(saved_line));
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config(
        "{\"Hatlar\":{\"IOA_R_TripFailed\":[16777216]}}", &iec));
    TEST_ASSERT_EQUAL_MEMORY(&before, &saved_line, sizeof(saved_line));
}


void test_t3_json_accepts_full_supported_range_and_persists_without_truncation(void)
{
    const uint16_t values[] = {20U, 60U, 255U, 256U, 65525U};
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_IgnoreAndReturn(0);
    iec104_process_config_changed_StopIgnore();
    for (size_t index = 0U; index < 5U; index++)
    {
        char json[32];
        (void)snprintf(json, sizeof(json), "{\"T3\":%u}",
            (unsigned int)values[index]);
        TEST_ASSERT_EQUAL_INT(1, parse_iec_config(json, &iec));
        TEST_ASSERT_EQUAL_UINT16(values[index], iec.t3_timeout);
        iec104_process_config_changed_Expect();
        TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
        TEST_ASSERT_EQUAL_UINT16(values[index], saved_iec.t3_max);
    }
}

void test_invalid_t3_and_timeout_order_never_write_storage(void)
{
    const uint16_t values[] = {0U, 19U, 65526U, 65535U};
    for (size_t index = 0U; index < 4U; index++)
    {
        char json[32];
        (void)snprintf(json, sizeof(json), "{\"T3\":%u}",
            (unsigned int)values[index]);
        TEST_ASSERT_EQUAL_INT(0, parse_iec_config(json, &iec));
        iec.t3_timeout = values[index];
        TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    }
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{\"T3\":65536}", &iec));
    iec.t3_timeout = 60U;
    iec.t2_timeout = iec.t1_timeout;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{\"T2\":16}", &iec));
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_UINT16(0U, saved_iec.t3_max);
}

void test_failed_save_does_not_apply_runtime_config(void)
{
    iec104_process_config_changed_StopIgnore();
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(-1);
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

/*** end of file ***/
