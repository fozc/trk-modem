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

/*** end of file ***/
