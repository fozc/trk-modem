/*
 * test_http_iec_config_scenario.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify IEC104 HTTP updates through the real parser and save path.
 */
#include "unity.h"
#include "http_handlers.h"
#include "http_request_parser.h"
#include "json_config.h"
#include "iec104_util.h"
#include "mock_iec104_config.h"
#include "mock_iec104_process.h"
#include "mock_modbus_config.h"
#include "mock_modbus_process.h"
#include "mock_modem_config.h"
#include "mock_rf_config.h"
#include "mock_http_response.h"
#include "mock_elog.h"
#include "mock_gsm_engine.h"
#include <string.h>

TEST_SOURCE_FILE("http_handlers.c")
TEST_SOURCE_FILE("modbus_map_json.c")
TEST_SOURCE_FILE("http_request_parser.c")
TEST_SOURCE_FILE("utils.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")

static iec104_config_t current_config;
static iec104_config_t saved_config;

static void capture_config(const iec104_config_t *config, int call_count)
{
    (void)call_count;
    saved_config = *config;
}

void setUp(void)
{
    memset(&current_config, 0, sizeof(current_config));
    memset(&saved_config, 0, sizeof(saved_config));
    current_config.scada_port = 2404U;
    current_config.scada_ip_address = 0xC0A80102U;
    current_config.periodical_send_interval = 60U;
    current_config.t0_max = 30U;
    current_config.t1_max = 45U;
    current_config.t2_max = 30U;
    current_config.t3_max = 300U;
    current_config.k_max = 32U;
    current_config.w_max = 24U;
    current_config.common_address = 1U;
    current_config.ioa_aku_uyarisi = iec104_make_ioa_3byte(10000U);
    current_config.ioa_modem_reset = iec104_make_ioa_3byte(10001U);
    iec104_config_get_IgnoreAndReturn(&current_config);
    iec104_get_line_config_IgnoreAndReturn(NULL);
}

void tearDown(void)
{
}

static void expect_save(void)
{
    iec104_config_set_StubWithCallback(capture_config);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(0);
    iec104_process_config_changed_Expect();
    gsm_get_web_client_ip_ExpectAndReturn(0U);
    elog_log_config_change_Expect(ELOG_CONFIG_IEC104_CHANGED,
        ELOG_SOURCE_WEB, 0U, "iec104", true);
    const char *response =
        "{\"message\":\"IEC104 configuration saved\",\"success\":true}";
    http_send_json_Expect(response, (int)strlen(response));
}

void test_partial_post_preserves_t3_above_byte_range(void)
{
    const uint16_t values[] = {300U, 256U, 65525U};
    for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]);
         index++)
    {
        current_config.t3_max = values[index];
        expect_save();
        handle_post_iec_config_json("{\"Port\":2405}");
        TEST_ASSERT_EQUAL_UINT16(values[index], saved_config.t3_max);
        TEST_ASSERT_EQUAL_UINT16(2405U, saved_config.scada_port);
        TEST_ASSERT_EQUAL_UINT16(values[index], current_config.t3_max);
    }
}

void test_periodic_interval_accepts_disabled_and_enabled_boundaries(void)
{
    const char *requests[] = {"{\"PeriodicSend\":0}",
        "{\"PeriodicSend\":1}", "{\"PeriodicSend\":86400}"};
    const uint32_t expected[] = {0U, 1U, 86400U};
    for (size_t index = 0U; sizeof(expected) / sizeof(expected[0]) > index;
         index++)
    {
        expect_save();
        handle_post_iec_config_json(requests[index]);
        TEST_ASSERT_EQUAL_UINT32(expected[index],
            saved_config.periodical_send_interval);
        TEST_ASSERT_EQUAL_UINT16(300U, saved_config.t3_max);
    }
}

void test_partial_post_preserves_disabled_periodic_interval(void)
{
    current_config.periodical_send_interval = 0U;
    expect_save();
    handle_post_iec_config_json("{\"Port\":2405}");
    TEST_ASSERT_EQUAL_UINT32(0U, saved_config.periodical_send_interval);
    TEST_ASSERT_EQUAL_UINT16(2405U, saved_config.scada_port);
}

void test_invalid_periodic_interval_rejects_without_saving(void)
{
    const char *requests[] = {"{\"PeriodicSend\":-1}",
                              "{\"PeriodicSend\":86401}"};
    for (size_t index = 0U; sizeof(requests) / sizeof(requests[0]) > index;
         index++)
    {
        http_send_error_Expect(400, "Invalid IEC configuration");
        handle_post_iec_config_json(requests[index]);
        TEST_ASSERT_EQUAL_UINT32(60U, current_config.periodical_send_interval);
        TEST_ASSERT_EQUAL_UINT32(0U, saved_config.periodical_send_interval);
    }
}

void test_invalid_t3_post_rejects_update_without_saving(void)
{
    http_send_error_Expect(400, "Invalid IEC configuration");
    handle_post_iec_config_json("{\"T3\":65526}");
    TEST_ASSERT_EQUAL_UINT16(300U, current_config.t3_max);
    TEST_ASSERT_EQUAL_UINT16(0U, saved_config.t3_max);
}

void test_null_post_rejects_update_without_saving(void)
{
    http_send_bad_request_Expect();
    handle_post_iec_config_json(NULL);
    TEST_ASSERT_EQUAL_UINT16(300U, current_config.t3_max);
}

static void check_get_response(const char *json, int length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_INT((int)strlen(json), length);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"T3\":300,"));
}

void test_get_reports_full_t3_without_narrowing(void)
{
    static char response[8192];
    http_handlers_init(response, (int)sizeof(response));
    http_send_json_StubWithCallback(check_get_response);
    handle_get_iec_config_json();
}

static uint32_t firmware_init_calls;

static int init_firmware(uint32_t size)
{
    TEST_ASSERT_EQUAL_UINT32(128U, size);
    firmware_init_calls++;
    return 0;
}

void test_bodyless_firmware_start_rejects_without_changing_progress(void)
{
    static char response[8192];
    const fw_update_callbacks_t callbacks = {.fw_init = init_firmware};
    char request_text[] = "POST /s HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
    http_request_t request;

    http_handlers_init(response, (int)sizeof(response));
    fw_update_register_callbacks(&callbacks);
    firmware_init_calls = 0U;
    http_send_json_Expect("{\"status\":\"ok\",\"cs\":4096,\"received\":0}", 38);
    handle_fw_start("{\"s\":128,\"h\":1}");
    TEST_ASSERT_EQUAL_INT(0, http_parse_request(request_text,
        (int)strlen(request_text), &request));
    TEST_ASSERT_NULL(request.body);
    http_send_bad_request_Expect();
    handle_fw_start(request.body);
    const char *status =
        "{\"active\":true,\"received\":0,\"total\":128,\"fh\":1}";
    http_send_json_Expect(status, (int)strlen(status));
    handle_get_fw_status();
    TEST_ASSERT_EQUAL_UINT32(1U, firmware_init_calls);
}

void test_empty_firmware_start_rejects_without_initializing_flash(void)
{
    const fw_update_callbacks_t callbacks = {.fw_init = init_firmware};
    fw_update_register_callbacks(&callbacks);
    firmware_init_calls = 0U;
    http_send_json_Expect("{\"status\":\"error\",\"error\":\"invalid size\"}", 40);
    handle_fw_start("");
    TEST_ASSERT_EQUAL_UINT32(0U, firmware_init_calls);
}

static char modbus_response[8192];
static modbus_configs_t modbus_settings;

static void check_modbus_response(const char *json, int length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(strlen(json), (size_t)length);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"CihazID\":37"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"BaudRate\":19200"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"RegisterMap\":{\"AddressBase\":40000"));
    TEST_ASSERT_NOT_NULL(strstr(json,
        "[\"anlik_akim\",1,2,\"FLOAT32\",\"A\"]"));
    TEST_ASSERT_EQUAL_CHAR('}', json[(size_t)length - 1U]);
}

void test_modbus_get_returns_settings_and_read_only_register_metadata(void)
{
    modbus_settings = (modbus_configs_t){.device_addr = 37U,
                                          .baud_rate = 19200U};
    http_handlers_init(modbus_response, sizeof(modbus_response));
    modbus_config_get_ExpectAndReturn(&modbus_settings);
    modbus_config_get_last_error_time_ExpectAndReturn(123U);
    http_send_json_StubWithCallback(check_modbus_response);
    handle_get_modbus_config_json();
    TEST_ASSERT_EQUAL_UINT8(37U, modbus_settings.device_addr);
    TEST_ASSERT_EQUAL_UINT32(19200U, modbus_settings.baud_rate);
}

void test_modbus_get_rejects_an_undersized_response_buffer(void)
{
    http_handlers_init(modbus_response, 256);
    modbus_config_get_ExpectAndReturn(&modbus_settings);
    modbus_config_get_last_error_time_ExpectAndReturn(0U);
    http_send_error_Expect(500, "Modbus register map exceeds response buffer");
    handle_get_modbus_config_json();
}

/*** end of file ***/
