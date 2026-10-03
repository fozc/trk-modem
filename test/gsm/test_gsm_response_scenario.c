/*
 * test_gsm_response_scenario.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercises production AT callbacks with deterministic modem responses.
 */
#include "unity.h"
#include "mock_at_engine2.h"
#include "mock_bsp.h"
#include "mock_gsm_socket.h"
#include "mock_gsm_info.h"
#include "mock_iec104_elog.h"
#include <string.h>

/* Keep the actual callbacks and IPv4 parser in this scenario. */
#include "../../Application/gsm/gsm_engine.c"
#include "../../Application/gsm/utils.c"

static const char *response_text;

static const uint8_t *get_response(uint16_t *length, int call_count)
{
    (void)call_count;
    *length = (uint16_t)strlen(response_text);
    return (const uint8_t *)response_text;
}

void setUp(void)
{
    memset(&gsm, 0, sizeof(gsm));
    memset(&gsm_info, 0, sizeof(gsm_info));
    at_engine_get_response_Stub(get_response);
    at_engine_get_result_IgnoreAndReturn(AT_ENGINE_RESULT_OK);
    bsp_get_tick_IgnoreAndReturn(100U);
    gsm_socket_set_state_Ignore();
}

void tearDown(void)
{
}

void test_listener_callbacks_save_peer_ip(void)
{
    response_text = "\r\n#SS: 1,2,10.0.0.1,80,192.168.1.2,1234\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_ss_listener_cb());
    TEST_ASSERT_EQUAL_HEX32(0xC0A80102U, gsm_info.web_session.ip.ip);
    iec104_elog_connected_Expect(0xC0A80102U);
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_ss_iec104_listener_cb());
    TEST_ASSERT_EQUAL_HEX32(0xC0A80102U, gsm_info.iec104_session.ip.ip);
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_ss_trace_cb());
    TEST_ASSERT_EQUAL_HEX32(0xC0A80102U, gsm_info.trace_session.ip.ip);
}

void test_invalid_peer_ip_preserves_session_value(void)
{
    response_text = "\r\n#SS: 1,2,10.0.0.1,80,abc,1234\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_ss_listener_cb());
    TEST_ASSERT_EQUAL_HEX32(0U, gsm_info.web_session.ip.ip);
}

void test_http_receive_counts_payload_between_markers(void)
{
    response_text = "\r\n<<<abc\r\nOK\r\n";
    gsm_info_add_rx_bytes_Expect(3U);
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_httprcv_cb());
    TEST_ASSERT_EQUAL_UINT32(3U, gsm_info.rx_counter);
}

void test_http_receive_missing_marker_leaves_counters_unchanged(void)
{
    response_text = "\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_httprcv_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

void test_http_receive_short_or_trailing_marker_is_ignored(void)
{
    response_text = "<<<";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_httprcv_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
    response_text = "\r\nOK\r\n<<<";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_httprcv_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

void test_http_receive_empty_payload_is_counted_as_zero(void)
{
    response_text = "<<<\r\nOK\r\n";
    gsm_info_add_rx_bytes_Expect(0U);
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_httprcv_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

/*** end of file ***/
