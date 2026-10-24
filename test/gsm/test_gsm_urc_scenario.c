/*
 * test_gsm_urc_scenario.c
 *
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Verify production URC logging and existing socket disconnect effects.
 */
#include "unity.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_info.h"
#include "mock_gsm_socket.h"
#include "mock_web_server.h"
#include "mock_iec104_process.h"
#include "gsm_types.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char log_output[1024];
static size_t log_length;

static void capture_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(log_output + log_length,
        sizeof(log_output) - log_length, format, args);
    va_end(args);
    TEST_ASSERT_TRUE(written >= 0);
    TEST_ASSERT_TRUE((size_t)written < sizeof(log_output) - log_length);
    log_length += (size_t)written;
}

#undef CSLOG_WARN
#undef CCSLOG
#define CSLOG_WARN(...) capture_log(__VA_ARGS__)
#define CCSLOG(color, ...) capture_log(__VA_ARGS__)
/* These unrelated process definitions also appear in the engine mock. */
#define gsm_set_tx_state unused_process_set_tx_state
#define gsm_set_tx_error unused_process_set_tx_error
#define gsm_reset_process_old unused_process_reset
/* Match legacy process diagnostics; observable URC behavior stays real. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wswitch"
#include "../../Application/gsm/gsm_process.c"
#pragma GCC diagnostic pop
#undef gsm_set_tx_state
#undef gsm_set_tx_error
#undef gsm_reset_process_old

gsm_t gsm;

void setUp(void)
{
    memset(&gsm, 0, sizeof(gsm));
    memset(log_output, 0, sizeof(log_output));
    log_length = 0U;
}

void tearDown(void)
{
}

static void receive_iec104(const char *text)
{
    uint8_t message[80];
    const size_t length = strlen(text);
    TEST_ASSERT_TRUE(length < sizeof(message));
    memcpy(message, text, length + 1U);
    gsm_set_socket_state_Expect(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_listener_set_no_carrier_Expect(GSM_LISTENER_IEC104, 1U);
    gsm_URC_callback(message, (uint16_t)length);
}

void test_all_documented_close_causes_are_logged(void)
{
    const char *names[] = {
        "not available", "remote FIN/END", "RST or fatal socket error",
        "socket inactivity timeout", "network PDP deactivation"
    };
    for (size_t index = 0U; index < 5U; index++)
    {
        char text[] = "NO CARRIER:3,0";
        text[13] = (char)('0' + (char)index);
        log_length = 0U;
        receive_iec104(text);
        TEST_ASSERT_NOT_NULL(strstr(log_output, names[index]));
        TEST_ASSERT_NOT_NULL(strstr(log_output, "socket=3 cause="));
    }
}

void test_spaces_and_leading_crlf_preserve_cause(void)
{
    receive_iec104("\r\nNO CARRIER: 3, 3 \r\n");
    TEST_ASSERT_NOT_NULL(strstr(log_output,
        "socket=3 cause=3 (socket inactivity timeout)"));
}

void test_missing_cause_is_unavailable(void)
{
    receive_iec104("NO CARRIER:3");
    TEST_ASSERT_NOT_NULL(strstr(log_output, "cause=unavailable"));
}

void test_non_digit_cause_is_unavailable(void)
{
    const char *messages[] = {
        "NO CARRIER:3,", "NO CARRIER:3,-1", "NO CARRIER:3,x", "NO CARRIER:3\r\nOTHER,3"
    };
    for (size_t index = 0U; index < 4U; index++)
    {
        log_length = 0U;
        receive_iec104(messages[index]);
        TEST_ASSERT_NOT_NULL(strstr(log_output, "cause=unavailable"));
    }
}

void test_unknown_digit_cause_keeps_numeric_value(void)
{
    receive_iec104("NO CARRIER:3,9");
    TEST_ASSERT_NOT_NULL(strstr(log_output, "cause=9 (unknown)"));
}

void test_web_and_dialer_log_causes_without_changing_routing(void)
{
    uint8_t web[] = "NO CARRIER:1,1";
    gsm_listener_set_no_carrier_Expect(GSM_LISTENER_WEB, 1U);
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_URC_callback(web, sizeof(web) - 1U);
    TEST_ASSERT_NOT_NULL(strstr(log_output, "socket=1 cause=1"));

    uint8_t dialer[] = "NO CARRIER: 2,4";
    gsm_URC_callback(dialer, sizeof(dialer) - 1U);
    TEST_ASSERT_EQUAL_UINT8(1U, gsm.dialer_socket_no_carrier);
    TEST_ASSERT_NOT_NULL(strstr(log_output, "socket=2 cause=4"));
}

void test_null_message_preserves_state(void)
{
    gsm_URC_callback(NULL, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.dialer_socket_no_carrier);
    TEST_ASSERT_EQUAL_UINT32(0U, log_length);
}

void test_bare_no_carrier_logs_without_assigning_socket(void)
{
    uint8_t message[] = "NO CARRIER";
    gsm_URC_callback(message, sizeof(message) - 1U);
    TEST_ASSERT_NOT_NULL(strstr(log_output,
        "socket=unavailable cause=unavailable"));
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.dialer_socket_no_carrier);
}

void test_qss_removal_enters_service_restart_only_in_normal_mode(void)
{
    uint8_t message[] = "#QSS: 0";
    gsm_info_set_sim_state_Expect(GSM_SIM_STATE_NOT_INSERTED);
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_set_main_state_Expect(GSM_SIM_ERROR_MODE);
    gsm_URC_callback(message, sizeof(message) - 1U);

    gsm_info_set_sim_state_Expect(GSM_SIM_STATE_NOT_INSERTED);
    gsm_get_main_state_ExpectAndReturn(GSM_MODULE_INIT_MODE);
    gsm_URC_callback(message, sizeof(message) - 1U);
}

void test_qss_maps_inserted_unlocked_and_ready_states(void)
{
    uint8_t message[] = "#QSS: 1";
    gsm_info_set_sim_state_Expect(GSM_SIM_STATE_UNKNOWN);
    gsm_URC_callback(message, sizeof(message) - 1U);
    message[6] = '2';
    gsm_info_set_sim_state_Expect(GSM_SIM_STATE_READY);
    gsm_URC_callback(message, sizeof(message) - 1U);
    message[6] = '3';
    gsm_info_set_sim_state_Expect(GSM_SIM_STATE_READY);
    gsm_URC_callback(message, sizeof(message) - 1U);
}

void test_qss_rejects_unknown_or_incomplete_status(void)
{
    uint8_t invalid[] = "#QSS: 4";
    gsm_URC_callback(invalid, sizeof(invalid) - 1U);
    invalid[6] = '\0';
    gsm_URC_callback(invalid, 6U);
}

void test_sim_error_closes_services_and_requests_existing_module_restart(void)
{
    gsm_clear_module_restart();
    for (uint8_t socket = 0U; socket < (uint8_t)GSM_SOCKET_COUNT; socket++)
    {
        gsm_socket_set_state_Expect(socket, '0');
    }
    gsm_reset_web_session_info_Expect();
    gsm_set_gprs_state_Expect(0U);
    gsm_set_socket_state_Expect(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_set_socket_state_Expect(DIALER_SOCKET, SOCKET_CLOSED);
    webserver_on_connection_closed_Expect();
    iec104_process_socket_closed_cb_Expect();
    gsm_reset_iec104_session_info_Expect();
    gsm_sim_error_mode();
    TEST_ASSERT_TRUE(gsm_module_restart_requested());
    gsm_clear_module_restart();
}

/*** end of file ***/
