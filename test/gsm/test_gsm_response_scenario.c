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
#include "mock_modem_config.h"
#include "mock_iec104_config.h"
#include "mock_rtc.h"
#include "mock_led_driver.h"
#include "mock_iec104_process.h"
#include "mock_gsm_http_server.h"
#include "mock_gsm_elog.h"
#include "xscanf.h"
#include <string.h>
#include "xprintf.h"
#include "gsm_signal_led.h"
TEST_SOURCE_FILE("gsm_signal_led.c")

/* Keep the actual callbacks and IPv4 parser in this scenario. */
#include "../../Application/gsm/gsm_engine.c"
#include "../../Application/gsm/utils.c"

static const char *response_text;
/* Match the AT motor's actual allocation and NUL-termination contract. */
static uint8_t response_buffer[1280];
static at_engine_result_t response_result;
static uint32_t signal_apply_calls;
static gsm_signal_tech_t signal_tech;
static gsm_signal_level_t signal_level;

/* Observe the real classifier's hardware output boundary. */
void gsm_signal_led_apply(gsm_signal_tech_t tech, gsm_signal_level_t level)
{
    signal_apply_calls++;
    signal_tech = tech;
    signal_level = level;
}

static at_engine_result_t get_result(int call_count)
{
    (void)call_count;
    return response_result;
}

static const uint8_t *get_response(uint16_t *length, int call_count)
{
    (void)call_count;
    const size_t response_length = strlen(response_text);
    TEST_ASSERT_TRUE(response_length < sizeof(response_buffer));
    memcpy(response_buffer, response_text, response_length + 1U);
    *length = (uint16_t)response_length;
    return response_buffer;
}

void setUp(void)
{
    memset(&gsm, 0, sizeof(gsm));
    memset(&gsm_info, 0, sizeof(gsm_info));
    at_engine_get_response_Stub(get_response);
    response_result = AT_ENGINE_RESULT_OK;
    signal_apply_calls = 0U;
    signal_tech = GSM_SIGNAL_TECH_NONE;
    signal_level = GSM_SIGNAL_LEVEL_NONE;
    at_engine_get_result_Stub(get_result);
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

void test_at_result_codes_preserve_callback_contract(void)
{
    static const at_engine_result_t inputs[] =
    {
        AT_ENGINE_RESULT_NONE, AT_ENGINE_RESULT_OK,
        AT_ENGINE_RESULT_ERROR, AT_ENGINE_RESULT_TIMEOUT
    };
    static const int32_t expected[] =
    {
        0, GSM_RESPONSE_OK, GSM_ERROR, GSM_TIMEOUT
    };
    response_text = "\r\nOK\r\n";
    for (size_t i = 0U; i < sizeof(inputs) / sizeof(inputs[0]); i++)
    {
        response_result = inputs[i];
        TEST_ASSERT_EQUAL_INT32(expected[i], gsm_at_response_ready());
    }
}

void test_no_carrier_code_preserved_for_ok_and_error_results(void)
{
    response_text = "\r\nNO CARRIER\r\n";
    TEST_ASSERT_EQUAL_INT32(GSM_NO_CARRIER, gsm_at_response_ready());
    response_result = AT_ENGINE_RESULT_ERROR;
    TEST_ASSERT_EQUAL_INT32(GSM_NO_CARRIER, gsm_at_response_ready());
}

void test_no_sim_result_preserves_error_and_mode_transition(void)
{
    response_text = "\r\nERROR\r\n";
    gsm_set_main_state(GSM_NORMAL_MODE);
    response_result = AT_ENGINE_RESULT_NO_SIM;
    TEST_ASSERT_EQUAL_INT32(GSM_ERROR, gsm_at_response_ready());
    TEST_ASSERT_EQUAL_UINT8(GSM_SIM_ERROR_MODE, gsm_get_main_state());
}

void test_substring_boundaries_and_resume_pointer_are_preserved(void)
{
    char source[] = "before<123>after";
    char output[6] = {'*', 'x', 'x', 'x', 'x', '*'};
    TEST_ASSERT_EQUAL_PTR(source + 11,
        str_substr(source, output + 1, "<", ">", 4U));
    TEST_ASSERT_EQUAL_STRING("123", output + 1);
    TEST_ASSERT_EQUAL_CHAR('*', output[0]);
    TEST_ASSERT_EQUAL_CHAR('*', output[5]);
    TEST_ASSERT_NULL(str_substr(source, output + 1, "<", ">", 3U));
    TEST_ASSERT_EQUAL_CHAR('\0', output[1]);
    output[1] = 'x';
    TEST_ASSERT_NULL(str_substr(source, output + 1, "[", ">", 4U));
    TEST_ASSERT_EQUAL_CHAR('x', output[1]);
    TEST_ASSERT_NULL(str_substr(source, output + 1, "<", "]", 4U));
    TEST_ASSERT_EQUAL_CHAR('x', output[1]);
}

void test_empty_substring_respects_zero_capacity(void)
{
    char source[] = "<>tail";
    char output[1] = {'x'};
    TEST_ASSERT_EQUAL_PTR(source + 2,
        str_substr(source, output, "<", ">", 1U));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_NULL(str_substr(source, output, "<", ">", 0U));
}

void test_string_suffix_result_contract_is_preserved(void)
{
    TEST_ASSERT_EQUAL_INT32(1, str_end_withs("modem-ready", "ready"));
    TEST_ASSERT_EQUAL_INT32(0, str_end_withs("modem-ready", "wrong"));
    TEST_ASSERT_EQUAL_INT32(-1, str_end_withs("a", "long"));
    TEST_ASSERT_EQUAL_INT32(1, str_end_withs("", ""));
}

void test_ipv4_decimal_octets_keep_zero_padding_and_buffer_bounds(void)
{
    for (uint32_t octet = 0U; octet < 256U; octet++)
    {
        uint8_t guarded[18];
        char expected[16];
        memset(guarded, 0x55, sizeof(guarded));
        (void)xsnprintf(expected, sizeof(expected), "%03u.%03u.%03u.%03u",
                       (unsigned int)octet, (unsigned int)octet,
                       (unsigned int)octet, (unsigned int)octet);
        TEST_ASSERT_EQUAL_UINT32(1U,
            ipv4_to_str(octet * 0x01010101U, guarded + 1));
        TEST_ASSERT_EQUAL_STRING(expected, (char *)(guarded + 1));
        TEST_ASSERT_EQUAL_HEX8(0x55U, guarded[0]);
        TEST_ASSERT_EQUAL_HEX8(0x55U, guarded[17]);
    }
}

static const char *expected_command;

/* Signature is imposed by the existing CMock command callback API. */
static bool check_at_command(const void *command, uint16_t command_len,
                            const void *expected_response,
                            uint8_t response_len, uint8_t retries,
                            uint32_t timeout, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(strlen(expected_command), command_len);
    TEST_ASSERT_EQUAL_MEMORY(expected_command, command, command_len);
    TEST_ASSERT_EQUAL_MEMORY(GSM_OK_STR, expected_response, GSM_OK_STR_LEN);
    TEST_ASSERT_EQUAL_UINT8(GSM_OK_STR_LEN, response_len);
    TEST_ASSERT_EQUAL_UINT8(3U, retries);
    TEST_ASSERT_EQUAL_UINT32(2000U, timeout);
    return true;
}

void test_at_command_bytes_and_length_are_unchanged(void)
{
    at_engine_is_busy_IgnoreAndReturn(false);
    at_engine_send_at_command_Stub(check_at_command);
    expected_command = "ATE0\r";
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_engine_send_query(ATQUERY_DISABLE_ECHO));
    gsm_set_free();
    modem_config_get_sim_apn_IgnoreAndReturn("internet");
    expected_command = "AT+CGDCONT=1,\"IP\",\"internet\"\r";
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_engine_send_query(ATQUERY_SET_APN));
}

void test_long_at_command_is_rejected_before_sending(void)
{
    static char apn[160];
    memset(apn, 'a', sizeof(apn) - 1U);
    apn[sizeof(apn) - 1U] = '\0';
    at_engine_is_busy_IgnoreAndReturn(false);
    modem_config_get_sim_apn_IgnoreAndReturn(apn);
    /* No send expectation: any send is an unexpected mock call. */
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_engine_send_query(ATQUERY_SET_APN));
    TEST_ASSERT_FALSE(gsm_is_busy());
}


void test_cesq_stores_2g_and_4g_measurements_separately(void)
{
    response_text = "\r\n+CESQ: 51,0,255,255,20,41\r\n\r\nOK\r\n";
    gsm_info_set_signal_quality_2G_Expect(51U);
    gsm_info_set_2G_ber_Expect(0U);
    gsm_info_set_signal_quality_3G_Expect(255U);
    gsm_info_set_3G_ecno_Expect(255U);
    gsm_info_set_4G_rsrq_Expect(20U);
    gsm_info_set_signal_quality_4G_Expect(41U);
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_cesq_cb());
}

void test_cesq_incomplete_response_does_not_change_measurements(void)
{
    response_text = "\r\n+CESQ: 51,0,255,255,20\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_cesq_cb());
}

void test_lte_registration_callback_reports_registered_and_searching(void)
{
    response_text = "\r\n+CEREG: 0,1\r\n\r\nOK\r\n";
    gsm_info_set_cereg_Expect(GSM_NET_REG_REGISTERED);
    TEST_ASSERT_EQUAL_INT(GSM_LTE_NETWORK_REGISTERED, gsm_cereg_get_cb());
    response_text = "\r\n+CEREG: 0,2\r\n\r\nOK\r\n";
    gsm_info_set_cereg_Expect(GSM_NET_REG_SEARCHING);
    TEST_ASSERT_EQUAL_INT(GSM_LTE_NETWORK_SEARCHING, gsm_cereg_get_cb());
}

void test_2g_registration_callback_reports_home_and_roaming(void)
{
    response_text = "\r\n+CGREG: 0,1\r\n\r\nOK\r\n";
    gsm_info_set_cgreg_Expect(GSM_NET_REG_REGISTERED);
    TEST_ASSERT_EQUAL_INT(GSM_GPRS_NETWORK_REGISTERED, gsm_cgreg_get_cb());
    response_text = "\r\n+CGREG: 0,5\r\n\r\nOK\r\n";
    gsm_info_set_cgreg_Expect(GSM_NET_REG_ROAMING);
    TEST_ASSERT_EQUAL_INT(GSM_GPRS_NETWORK_ROAMING, gsm_cgreg_get_cb());
}

/*** end of file ***/

void test_dialer_si_preserves_zero_and_maximum_ack_count(void)
{
    response_text = "\r\n#SI: 2,123,456,789,0\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_dialer_si_cb());
    TEST_ASSERT_EQUAL_UINT16(0U, gsm.dialer_ack_waiting);
    response_text = "\r\n#SI: 2,123,456,789,65535\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_dialer_si_cb());
    TEST_ASSERT_EQUAL_UINT16(65535U, gsm.dialer_ack_waiting);
}

void test_dialer_and_trace_si_reject_bad_fields_without_wrap(void)
{
    static const char *cases[] =
    {
        "\r\n#SI: 2,1,2,3,65536\r\n\r\nOK\r\n",
        "\r\n#SI: 2,1,2,3,4294967295\r\n\r\nOK\r\n",
        "\r\n#SI: 2,1,2\r\n\r\nOK\r\n",
        "\r\n#SI: 2,1,2,3,x\r\n\r\nOK\r\n",
        "\r\n#SI: 3,1,2,3,10\r\n\r\nOK\r\n"
    };
    gsm.dialer_ack_waiting = 123U;
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        response_text = cases[i];
        TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_dialer_si_cb());
        TEST_ASSERT_EQUAL_UINT16(123U, gsm.dialer_ack_waiting);
    }
    response_text = "\r\n#SI: 3,1,2,3,65535\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_trace_si_cb());
    response_text = "\r\n#SI: 3,1,2,3,65536\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_trace_si_cb());
}

void test_cced_parses_decimal_and_hex_fields_from_real_response(void)
{
    response_text = "\r\n+CCED: 286,01,6B2A,F271,11,50,47,0,\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_CCED_cb());
    TEST_ASSERT_EQUAL_UINT16(286U, gsm_info.cell_info.mobile_country_code);
    TEST_ASSERT_EQUAL_UINT16(1U, gsm_info.cell_info.mobile_network_code);
    TEST_ASSERT_EQUAL_HEX16(0x6B2AU, gsm_info.cell_info.location_area_code);
    TEST_ASSERT_EQUAL_HEX16(0xF271U, gsm_info.cell_info.cell_id);
}

void test_cced_rejects_missing_delimiters_and_oversized_fields(void)
{
    static const char *cases[] =
    {
        "\r\n+CCED: 286\r\n\r\nOK\r\n",
        "\r\n+CCED: 286,01,6B2A,\r\n\r\nOK\r\n",
        "\r\n+CCED: 286,01,6B2A,10000,11\r\n\r\nOK\r\n",
        "\r\n+CCED: 65536,01,6B2A,F271,11\r\n\r\nOK\r\n",
        "\r\n+CCED: 286,01,nothex,F271,11\r\n\r\nOK\r\n"
    };
    gsm_info.cell_info.mobile_country_code = 123U;
    gsm_info.cell_info.cell_id = 456U;
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        response_text = cases[i];
        TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_CCED_cb());
        TEST_ASSERT_EQUAL_UINT16(123U, gsm_info.cell_info.mobile_country_code);
        TEST_ASSERT_EQUAL_UINT16(456U, gsm_info.cell_info.cell_id);
    }
}

void test_listener_si_rejects_ack_overflow_without_changing_state(void)
{
    gsm.listener[GSM_LISTENER_WEB].ack_waiting = 123U;
    gsm.listener[GSM_LISTENER_IEC104].ack_waiting = 456U;
    response_text = "\r\n#SI: 1,0,0,0,65536\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_listener_si_cb());
    TEST_ASSERT_EQUAL_UINT16(123U, gsm.listener[GSM_LISTENER_WEB].ack_waiting);
    response_text = "\r\n#SI: 3,0,0,0,65536\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_iec104_listener_si_cb());
    TEST_ASSERT_EQUAL_UINT16(456U,
                           gsm.listener[GSM_LISTENER_IEC104].ack_waiting);
}

void test_si_all_keeps_full_counters_and_rejects_narrow_ack_overflow(void)
{
    response_text = "\r\n#SI: 2,4294967295,65536,0,65535\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_si_all_cb());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm.si_info[1].sent);
    TEST_ASSERT_EQUAL_UINT32(65536U, gsm.si_info[1].received);
    TEST_ASSERT_EQUAL_UINT16(65535U, gsm.dialer_ack_waiting);
    response_text = "\r\n#SI: 2,1,2,0,65536\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_si_all_cb());
    TEST_ASSERT_EQUAL_UINT16(65535U, gsm.dialer_ack_waiting);
    TEST_ASSERT_FALSE(gsm.si_info[1].is_valid);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm.si_info[1].sent);
}

void test_si_all_parses_telit_reference_six_socket_example(void)
{
    response_text = "\r\n#SI: 1,123,400,10,50\r\n"
        "#SI: 2,0,100,0,0\r\n"
        "#SI: 3,589,100,10,100\r\n"
        "#SI: 4,0,0,0,0\r\n"
        "#SI: 5,0,0,0,0\r\n"
        "#SI: 6,0,98,60,0\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_si_all_cb());
    for (size_t i = 0U; i < GSM_MODEM_SOCKET_COUNT; i++)
    {
        TEST_ASSERT_TRUE(gsm.si_info[i].is_valid);
    }
    TEST_ASSERT_EQUAL_UINT32(123U, gsm.si_info[0].sent);
    TEST_ASSERT_EQUAL_UINT32(400U, gsm.si_info[0].received);
    TEST_ASSERT_EQUAL_UINT32(50U, gsm.si_info[0].ack_waiting);
    TEST_ASSERT_EQUAL_UINT32(589U, gsm.si_info[2].sent);
    TEST_ASSERT_EQUAL_UINT32(100U, gsm.si_info[2].ack_waiting);
    TEST_ASSERT_EQUAL_UINT32(98U, gsm.si_info[5].received);
    TEST_ASSERT_EQUAL_UINT32(60U, gsm.si_info[5].buff_in);
}

void test_si_all_four_maximum_uint32_fields_fit_parse_window(void)
{
    response_text = "\r\n#SI: 4,4294967295,4294967295,"
        "4294967295,4294967295\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_RESPONSE_OK, gsm_si_all_cb());
    TEST_ASSERT_TRUE(gsm.si_info[3].is_valid);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm.si_info[3].sent);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm.si_info[3].received);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm.si_info[3].buff_in);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm.si_info[3].ack_waiting);
}

void test_si_all_missing_final_field_is_not_a_valid_socket_record(void)
{
    response_text = "\r\n#SI: 4,1,2,3,\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_si_all_cb());
    TEST_ASSERT_FALSE(gsm.si_info[3].is_valid);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm.si_info[3].ack_waiting);
}

static void set_signal_readings(uint8_t rxlev, uint8_t rscp, uint8_t rsrp)
{
    gsm_info_get_signal_quality_2G_IgnoreAndReturn(rxlev);
    gsm_info_get_signal_quality_3G_IgnoreAndReturn(rscp);
    gsm_info_get_signal_quality_4G_IgnoreAndReturn(rsrp);
}

void test_cops_reserved_and_unknown_power_do_not_drive_a_normal_signal_led(void)
{
    static const struct
    {
        const char *response;
        uint8_t rat;
    } cases[] = {
        {"\r\n+COPS: 0,0,\"OP\",0\r\n\r\nOK\r\n", 0U},
        {"\r\n+COPS: 0,0,\"OP\",2\r\n\r\nOK\r\n", 2U},
        {"\r\n+COPS: 0,0,\"OP\",7\r\n\r\nOK\r\n", 7U}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        response_text = cases[i].response;
        set_signal_readings(64U, 97U, 98U);
        gsm_info_set_access_technology_Expect(cases[i].rat);
        signal_apply_calls = 0U;
        TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
        TEST_ASSERT_EQUAL_UINT32(1U, signal_apply_calls);
        TEST_ASSERT_EQUAL(GSM_SIGNAL_TECH_NONE, signal_tech);
        TEST_ASSERT_EQUAL(GSM_SIGNAL_LEVEL_NONE, signal_level);
    }
    set_signal_readings(99U, 255U, 255U);
    gsm_info_set_access_technology_Expect(7U);
    signal_apply_calls = 0U;
    TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(1U, signal_apply_calls);
    TEST_ASSERT_EQUAL(GSM_SIGNAL_TECH_NONE, signal_tech);
}

void test_cops_uses_valid_cesq_fallback_without_changing_reported_rat(void)
{
    response_text = "\r\n+COPS: 0,0,\"OP\",7\r\n\r\nOK\r\n";
    set_signal_readings(63U, 40U, 98U);
    gsm_info_set_access_technology_Expect(7U);
    TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(1U, signal_apply_calls);
    TEST_ASSERT_EQUAL(GSM_SIGNAL_TECH_3G, signal_tech);
    TEST_ASSERT_EQUAL(GSM_SIGNAL_LEVEL_FAIR, signal_level);
    set_signal_readings(63U, 97U, 255U);
    gsm_info_set_access_technology_Expect(7U);
    TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(2U, signal_apply_calls);
    TEST_ASSERT_EQUAL(GSM_SIGNAL_TECH_2G, signal_tech);
    TEST_ASSERT_EQUAL(GSM_SIGNAL_LEVEL_EXCELLENT, signal_level);
}

void test_cops_valid_lte_uses_common_strength_boundaries_and_priority(void)
{
    static const uint8_t values[] = {0U, 24U, 25U, 48U, 49U, 72U, 73U, 97U};
    static const gsm_signal_level_t levels[] = {
        GSM_SIGNAL_LEVEL_WEAK, GSM_SIGNAL_LEVEL_WEAK,
        GSM_SIGNAL_LEVEL_FAIR, GSM_SIGNAL_LEVEL_FAIR,
        GSM_SIGNAL_LEVEL_GOOD, GSM_SIGNAL_LEVEL_GOOD,
        GSM_SIGNAL_LEVEL_EXCELLENT, GSM_SIGNAL_LEVEL_EXCELLENT
    };
    response_text = "\r\n+COPS: 0,0,\"OP\",7\r\n\r\nOK\r\n";
    for (size_t i = 0U; i < sizeof(values); i++)
    {
        set_signal_readings(63U, 96U, values[i]);
        gsm_info_set_access_technology_Expect(7U);
        signal_apply_calls = 0U;
        TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
        TEST_ASSERT_EQUAL_UINT32(1U, signal_apply_calls);
        TEST_ASSERT_EQUAL(GSM_SIGNAL_TECH_4G, signal_tech);
        TEST_ASSERT_EQUAL(levels[i], signal_level);
    }
}

void test_cops_without_rat_preserves_led_and_reports_operator_mode(void)
{
    response_text = "\r\n+COPS: 0,0,\"OP\"\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_COPS_AUTOMOATIC_MODE, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, signal_apply_calls);
    response_text = "\r\n+COPS: 2\r\n\r\nOK\r\n";
    TEST_ASSERT_EQUAL_INT(GSM_COPS_DEREGISTER_MODE, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, signal_apply_calls);
}

void test_cops_error_and_timeout_do_not_update_rat_or_led(void)
{
    response_text = "\r\nERROR\r\n";
    response_result = AT_ENGINE_RESULT_ERROR;
    TEST_ASSERT_EQUAL_INT(GSM_ERROR, gsm_COPS_state_cb());
    response_result = AT_ENGINE_RESULT_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(GSM_TIMEOUT, gsm_COPS_state_cb());
    TEST_ASSERT_EQUAL_UINT32(0U, signal_apply_calls);
}
