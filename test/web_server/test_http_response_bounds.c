/*
 * test_http_response_bounds.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Check real HTTP header formatting and transport length boundaries.
 */
#include "unity.h"
#include "http_response.h"
#include "mock_html_resources.h"
#include "xprintf.h"
#include <limits.h>
#include <string.h>

static char output[2048];
static size_t output_len;

static int capture(const void *data, int length)
{
    TEST_ASSERT_GREATER_THAN_INT(0, length);
    TEST_ASSERT_TRUE(output_len + (size_t)length < sizeof(output));
    memcpy(output + output_len, data, (size_t)length);
    output_len += (size_t)length;
    output[output_len] = '\0';
    return length;
}

void setUp(void)
{
    output_len = 0U;
    output[0] = '\0';
    http_response_init(capture);
}

void tearDown(void)
{
}

void test_normal_response_preserves_complete_header_and_body(void)
{
    http_send_response(200, "OK", "application/json", "{}", 2);
    TEST_ASSERT_EQUAL_STRING(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: 2\r\n"
        "Connection: keep-alive\r\n"
        "Server: EmbeddedHTTP/1.0\r\n\r\n{}", output);
}

void test_long_header_input_returns_valid_error_without_original_body(void)
{
    char reason[400];
    memset(reason, 'A', sizeof(reason) - 1U);
    reason[sizeof(reason) - 1U] = '\0';
    http_send_response(200, reason, "text/plain", "SECRET", 6);
    TEST_ASSERT_EQUAL_STRING(
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n", output);
}

void test_compressed_resource_preserves_payload_length_and_encoding(void)
{
    const uint8_t data[] = {0x1FU, 0x8BU, 0x00U, 0xFFU};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html"};
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Encoding: gzip\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: 4\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - sizeof(data),
                             sizeof(data));
}

void test_oversized_resource_is_rejected_before_reading_body(void)
{
    const html_resource_t resource =
        {NULL, (uint32_t)INT_MAX + 1U, HTML_COMPRESS_NONE, "text/html"};
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 500 "));
    TEST_ASSERT_NOT_NULL(strstr(output, "HTML resource too large"));
}
/*** end of file ***/
