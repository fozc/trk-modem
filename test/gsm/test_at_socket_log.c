/*
 * test_at_socket_log.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_gsm_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char logs[4096];
static gsm_log_level_t log_level;
static gsm_log_level_t get_log_level(int call_count)
{
    (void)call_count;
    return log_level;
}
static void capture_log(const char *format, ...)
{
    const size_t used = strlen(logs);
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(logs + used, sizeof(logs) - used,
        format, args);
    va_end(args);
    TEST_ASSERT_TRUE(count >= 0);
    TEST_ASSERT_TRUE((size_t)count < sizeof(logs) - used);
}
#undef CCSLOG
#undef CSLOG_NODT
#define CCSLOG(color, ...) ((void)(color), capture_log(__VA_ARGS__))
#define CSLOG_NODT(...) capture_log(__VA_ARGS__)
#include "../../Application/gsm/at_engine2.c"

void setUp(void)
{
    memset(&at_engine, 0, sizeof(at_engine));
    logs[0] = '\0';
    log_level = GSM_LOG_VERBOSE;
    gsm_log_get_level_Stub(get_log_level);
}
void tearDown(void)
{
}
static void set_command(const char *command)
{
    at_engine.cmd_len = (uint16_t)strlen(command);
    memcpy(at_engine.cmd, command, at_engine.cmd_len);
}
static void set_response(const void *data, uint16_t length)
{
    memcpy(at_engine.response_buffer, data, length);
    at_engine.response_buffer_len = length;
}
void test_iec104_socket_binary_frame_is_visible_as_hex(void)
{
    const uint8_t frame[] = {0x68U, 0x04U, 0x07U, 0U, 0U, 0U};
    set_command("AT#SRECV=3,1024\r");
    set_response(frame, (uint16_t)sizeof(frame));
    at_engine.srecv_payload_end = 6U;
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "68 04 07 00 00 00"));
    TEST_ASSERT_NULL(strstr(logs, "payload omitted"));
}
void test_iec104_socket_partial_error_response_is_visible(void)
{
    const uint8_t frame[] = {0x68U, 0x04U};
    set_command("AT#SRECV=3,1024\r");
    set_response(frame, (uint16_t)sizeof(frame));
    log_response("TIMEOUT");
    TEST_ASSERT_NOT_NULL(strstr(logs, "TIMEOUT"));
    TEST_ASSERT_NOT_NULL(strstr(logs, "68 04"));
}
void test_iec104_full_at_response_reports_total_bytes_and_frame_hex(void)
{
    static const char response[] = "\r\n#SRECV: 3,6\r\n"
        "\x68\x04\x07\x00\x00\x00\r\n\r\nOK\r\n";
    set_command("AT#SRECV=3,1024\r");
    set_response(response, (uint16_t)(sizeof(response) - 1U));
    at_engine.srecv_payload_end = 21U;
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "29 bytes"));
    TEST_ASSERT_NOT_NULL(strstr(logs, "68 04 07 00 00 00"));
    TEST_ASSERT_NOT_NULL(strstr(logs, "0D 0A 4F 4B 0D 0A"));
}
void test_http_socket_secrets_stay_hidden_on_ok_and_timeout(void)
{
    static const char secret[] = "password=admin25&t=SECRET_TOKEN";
    set_command("AT#SRECV=1,1024\r");
    set_response(secret, (uint16_t)(sizeof(secret) - 1U));
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "payload omitted"));
    TEST_ASSERT_NULL(strstr(logs, "SECRET_TOKEN"));
    logs[0] = '\0';
    log_response("TIMEOUT");
    TEST_ASSERT_NOT_NULL(strstr(logs, "payload omitted"));
    TEST_ASSERT_NULL(strstr(logs, "admin25"));
}
void test_socket_three_prefix_does_not_allow_socket_thirty(void)
{
    static const char secret[] = "SECRET_TOKEN";
    set_command("AT#SRECV=30,1024\r");
    set_response(secret, (uint16_t)(sizeof(secret) - 1U));
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "payload omitted"));
    TEST_ASSERT_NULL(strstr(logs, "SECRET_TOKEN"));
}
void test_unknown_socket_payload_marker_stays_hidden(void)
{
    static const char secret[] = "SECRET_TOKEN";
    set_command("AT+CSQ\r");
    set_response(secret, (uint16_t)(sizeof(secret) - 1U));
    at_engine.srecv_payload_end = at_engine.response_buffer_len;
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "payload omitted"));
    TEST_ASSERT_NULL(strstr(logs, "SECRET_TOKEN"));
}
void test_ordinary_modem_response_preserves_text_logging(void)
{
    static const char response[] = "\r\n+CSQ: 20,99\r\n";
    set_command("AT+CSQ\r");
    set_response(response, (uint16_t)(sizeof(response) - 1U));
    log_response("OK");
    TEST_ASSERT_NOT_NULL(strstr(logs, "\\r\\n+CSQ: 20,99\\r\\n"));
}
void test_verbose_gate_still_controls_iec104_logging(void)
{
    const uint8_t frame[] = {0x68U, 0x04U};
    set_command("AT#SRECV=3,1024\r");
    set_response(frame, (uint16_t)sizeof(frame));
    log_level = GSM_LOG_NORMAL;
    log_response("OK");
    TEST_ASSERT_EQUAL_STRING("", logs);
}
/*** end of file ***/
