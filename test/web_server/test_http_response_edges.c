/*
 * test_http_response_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Transport and fallback edge cases for the response builder beyond
 * test_http_response_bounds.c: chunked sends, partial-send failure,
 * reason-phrase fallbacks, NULL-argument defaults, brotli encoding and
 * the default-page resource path.
 */
#include "unity.h"
#include "http_response.h"
#include "mock_html_resources.h"
#include "xprintf.h"
#include <limits.h>
#include <string.h>

static char output[4096];
static size_t output_len;
static int send_calls;
static int fail_on_call;   /* 0 = never; otherwise 1-based call index */

static int capture(const void *data, int length)
{
    TEST_ASSERT_GREATER_THAN_INT(0, length);
    TEST_ASSERT_TRUE(output_len + (size_t)length < sizeof(output));
    send_calls++;
    if ((0 != fail_on_call) && (fail_on_call == send_calls))
    {
        return 0;
    }
    memcpy(output + output_len, data, (size_t)length);
    output_len += (size_t)length;
    output[output_len] = '\0';
    return length;
}

void setUp(void)
{
    output_len = 0U;
    output[0] = '\0';
    send_calls = 0;
    fail_on_call = 0;
    http_response_init(capture);
}

void tearDown(void)
{
}

void test_send_data_splits_large_payload_into_chunk_calls(void)
{
    static char payload[2500];
    int sent;

    memset(payload, 'A', sizeof(payload));
    sent = http_send_data(payload, (int)sizeof(payload));
    TEST_ASSERT_EQUAL_INT((int)sizeof(payload), sent);
    TEST_ASSERT_EQUAL_INT(3, send_calls);  /* 1024 + 1024 + 452 */
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), output_len);
    TEST_ASSERT_EQUAL_UINT8('A', output[0]);
    TEST_ASSERT_EQUAL_UINT8('A', output[output_len - 1U]);
}

void test_send_data_stops_on_callback_failure_and_reports_partial(void)
{
    static char payload[2048];
    int sent;

    memset(payload, 'B', sizeof(payload));
    fail_on_call = 2;  /* First chunk (1024) succeeds, second fails. */
    sent = http_send_data(payload, (int)sizeof(payload));
    TEST_ASSERT_EQUAL_INT(1024, sent);
    TEST_ASSERT_EQUAL_INT(2, send_calls);
    TEST_ASSERT_EQUAL_UINT(1024U, output_len);
}

void test_send_data_rejects_invalid_arguments_without_calling_back(void)
{
    TEST_ASSERT_EQUAL_INT(0, http_send_data(NULL, 16));
    TEST_ASSERT_EQUAL_INT(0, http_send_data("x", 0));
    TEST_ASSERT_EQUAL_INT(0, http_send_data("x", -3));
    TEST_ASSERT_EQUAL_INT(0, send_calls);
}

void test_send_data_without_registered_callback_returns_zero(void)
{
    http_response_init(NULL);
    TEST_ASSERT_EQUAL_INT(0, http_send_data("x", 1));
}

void test_reason_phrase_falls_back_to_table_and_status_class(void)
{
    http_send_response(404, NULL, "text/plain", "x", 1);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 404 Not Found\r\n"));

    TEST_ASSERT_EQUAL_STRING("Payload Too Large",
                             http_get_reason_phrase(413));
    TEST_ASSERT_EQUAL_STRING("Service Unavailable",
                             http_get_reason_phrase(503));
    /* Codes outside the table degrade to their status class. */
    TEST_ASSERT_EQUAL_STRING("Success", http_get_reason_phrase(299));
    TEST_ASSERT_EQUAL_STRING("Client Error", http_get_reason_phrase(418));
    TEST_ASSERT_EQUAL_STRING("Server Error", http_get_reason_phrase(599));
    /* 1xx/3xx have no class branch. */
    TEST_ASSERT_EQUAL_STRING("Unknown", http_get_reason_phrase(100));
    TEST_ASSERT_EQUAL_STRING("Unknown", http_get_reason_phrase(302));
}

void test_null_content_type_and_body_default_to_text_plain_and_empty(void)
{
    http_send_response(200, "OK", NULL, NULL, 99);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Type: text/plain"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: 0\r\n"));
    /* No body may follow the header block. */
    TEST_ASSERT_EQUAL_STRING("\r\n", output + output_len - 2U);
}

void test_send_error_without_message_uses_table_phrase_as_body(void)
{
    http_send_error(503, NULL);
    TEST_ASSERT_NOT_NULL(strstr(output, "503 Service Unavailable\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "\r\n\r\nService Unavailable"));
}

void test_negative_body_length_is_announced_but_never_sent(void)
{
    /* Pinned current behavior: a negative length reaches the header as
     * a negative Content-Length and no body bytes are sent. Callers
     * must not pass negative lengths; this documents the fallout. */
    http_send_response(200, "OK", "text/plain", "AB", -5);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: -5\r\n"));
    TEST_ASSERT_EQUAL_STRING("\r\n", output + output_len - 2U);
}

void test_brotli_resource_sends_br_encoding_and_payload(void)
{
    static const uint8_t data[] = {0x11U, 0x22U};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_BR, "text/html"};

    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Encoding: br\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: 2\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - sizeof(data),
                             sizeof(data));
}

void test_null_resource_falls_back_to_default_page(void)
{
    static const uint8_t data[] = "DEFAULT";
    const html_resource_t fallback =
        {data, sizeof(data) - 1U, HTML_COMPRESS_NONE, "text/html"};

    get_default_html_ExpectAndReturn(&fallback);
    http_send_html_resource(NULL);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - (sizeof(data) - 1U),
                             sizeof(data) - 1U);
}

void test_convenience_helpers_wire_status_and_content_types(void)
{
    http_send_json("{}", 2);
    TEST_ASSERT_NOT_NULL(strstr(output, "application/json"));

    http_send_ok("hi", 2);
    TEST_ASSERT_NOT_NULL(strstr(output, "200 OK"));

    http_send_not_found();
    TEST_ASSERT_NOT_NULL(strstr(output, "404 Not Found"));

    http_send_bad_request();
    TEST_ASSERT_NOT_NULL(strstr(output, "400 Bad Request"));

    http_send_method_not_allowed();
    TEST_ASSERT_NOT_NULL(strstr(output, "405 Method Not Allowed"));
}

/*** end of file ***/
