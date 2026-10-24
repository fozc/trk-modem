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

TEST_SOURCE_FILE("sha256.c")

static char output[4096];
static size_t output_len;
static int send_calls;
static int fail_on_call;   /* 0 = never; otherwise 1-based call index */
static uint32_t static_calls;
static const void *static_data;
static uint32_t static_length;

static int defer_static(const void *data, uint32_t length)
{
    static_calls++;
    static_data = data;
    static_length = length;
    return (int)length;
}

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
    static_calls = 0U;
    static_data = NULL;
    static_length = 0U;
    http_response_note_request_headers(NULL, 0);
    http_response_init(capture);
}

void tearDown(void)
{
}

void test_static_html_defers_only_the_body_after_accepting_header(void)
{
    static const uint8_t body[] = {0U, 1U, 2U};
    const html_resource_t resource =
        {body, sizeof(body), HTML_COMPRESS_NONE, "text/html", NULL};
    http_response_set_static_sender(defer_static);
    http_send_html_resource(&resource);
    TEST_ASSERT_EQUAL_UINT32(1U, static_calls);
    TEST_ASSERT_EQUAL_PTR(body, static_data);
    TEST_ASSERT_EQUAL_UINT32(sizeof(body), static_length);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: 3\r\n"));
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", output + output_len - 4U);
}

void test_failed_header_does_not_accept_a_static_body(void)
{
    static const uint8_t body[] = {1U};
    const html_resource_t resource =
        {body, sizeof(body), HTML_COMPRESS_GZIP, "text/html", NULL};
    fail_on_call = 1;
    http_response_set_static_sender(defer_static);
    http_send_html_resource(&resource);
    TEST_ASSERT_EQUAL_UINT32(0U, static_calls);
    TEST_ASSERT_EQUAL_UINT(0U, output_len);
}

void test_json_keeps_copy_sending_with_static_sender_registered(void)
{
    http_response_set_static_sender(defer_static);
    http_send_json("{\"ok\":true}", 11);
    TEST_ASSERT_EQUAL_UINT32(0U, static_calls);
    TEST_ASSERT_NOT_NULL(strstr(output, "\r\n\r\n{\"ok\":true}"));
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
        {data, sizeof(data), HTML_COMPRESS_BR, "text/html", NULL};

    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Encoding: br\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Length: 2\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - sizeof(data),
                             sizeof(data));
}

void test_matching_if_none_match_gets_304_without_body(void)
{
    static const uint8_t data[] = {0x11U, 0x22U};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html", NULL};
    char request[128];

    /* First response reveals the exact ETag ("<content-hash>"). */
    http_send_html_resource(&resource);
    const char *etag_line = strstr(output, "ETag: \"");
    TEST_ASSERT_NOT_NULL(etag_line);
    char etag[67];
    size_t len = 0U;
    const char *cursor = etag_line + 7U;   /* after opening quote */
    while (('"' != *cursor) && (len + 1U < sizeof(etag)))
    {
        etag[len++] = *cursor++;
    }
    etag[len] = '\0';

    /* Second request carries that ETag: 304, header only, no body. */
    output_len = 0U;
    output[0] = '\0';
    (void)xsnprintf(request, sizeof(request),
                   "GET / HTTP/1.1\r\nIf-None-Match: \"%s\"\r\n\r\n", etag);
    http_response_note_request_headers(request, (int)strlen(request));
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 304 Not Modified\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Cache-Control: no-cache\r\n"));
    TEST_ASSERT_NULL(strstr(output, "200 OK"));
    /* Only headers: the response ends with the blank line. */
    TEST_ASSERT_EQUAL_UINT8(0x0DU, output[output_len - 2U]);
    TEST_ASSERT_EQUAL_UINT8(0x0AU, output[output_len - 1U]);
}

void test_star_if_none_match_also_gets_304(void)
{
    static const uint8_t data[] = {0x33U};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html", NULL};

    http_response_note_request_headers(
        "GET / HTTP/1.1\r\nIf-None-Match: *\r\n\r\n", 35);
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 304 Not Modified\r\n"));
}

void test_no_if_none_match_sends_full_page_with_etag(void)
{
    static const uint8_t data[] = {0x44U};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html", NULL};

    http_response_note_request_headers(NULL, 0);
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "ETag: \""));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - 1U, 1U);
}

void test_stale_if_none_match_still_gets_full_page(void)
{
    static const uint8_t data[] = {0x55U};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html", NULL};

    http_response_note_request_headers(
        "GET / HTTP/1.1\r\nIf-None-Match: \"old-hash-999\"\r\n\r\n", 46);
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - 1U, 1U);
}

void test_null_resource_falls_back_to_default_page(void)
{
    static const uint8_t data[] = "DEFAULT";
    const html_resource_t fallback =
        {data, sizeof(data) - 1U, HTML_COMPRESS_NONE, "text/html", NULL};

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

static void resource_tag(const html_resource_t *resource, char tag[67])
{
    http_response_note_request_headers(NULL, 0);
    output_len = 0U;
    http_send_html_resource(resource);
    const char *line = strstr(output, "ETag: ");
    TEST_ASSERT_NOT_NULL(line);
    (void)memcpy(tag, line + 6U, 66U);
    tag[66] = '\0';
    output_len = 0U;
    output[0] = '\0';
}

void test_equal_length_changed_page_cannot_reuse_an_old_etag(void)
{
    static const uint8_t first[] = {1U, 2U};
    static const uint8_t second[] = {1U, 3U};
    html_resource_t resource =
        {first, sizeof(first), HTML_COMPRESS_GZIP, "text/html", NULL};
    char tag[67];
    char request[128];
    resource_tag(&resource, tag);
    (void)xsnprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", tag);
    http_response_note_request_headers(request, (int)strlen(request));
    resource.data = second;
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "200 OK"));
    TEST_ASSERT_EQUAL_MEMORY(second, output + output_len - 2U, 2U);
}

void test_encoding_is_part_of_resource_etag_identity(void)
{
    static const uint8_t bytes[] = {1U};
    html_resource_t resource =
        {bytes, sizeof(bytes), HTML_COMPRESS_GZIP, "text/html", NULL};
    char gzip_tag[67];
    char brotli_tag[67];
    resource_tag(&resource, gzip_tag);
    resource.is_gzipped = HTML_COMPRESS_BR;
    resource_tag(&resource, brotli_tag);
    TEST_ASSERT_FALSE(0 == strcmp(gzip_tag, brotli_tag));
}

void test_weak_tag_in_list_with_case_and_whitespace_gets_bodyless_304(void)
{
    static const uint8_t bytes[] = {1U};
    const html_resource_t resource =
        {bytes, sizeof(bytes), HTML_COMPRESS_GZIP, "text/html", NULL};
    char tag[67];
    char request[160];
    resource_tag(&resource, tag);
    (void)xsnprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\niF-nOnE-mAtCh:\t\"old\", W/%s \t\r\n\r\n", tag);
    http_response_note_request_headers(request, (int)strlen(request));
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "304 Not Modified"));
    const char *end = strstr(output, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(end);
    TEST_ASSERT_EQUAL_size_t(output_len, (size_t)(end - output) + 4U);
}

void test_embedded_header_name_and_body_are_not_conditional_headers(void)
{
    static const uint8_t bytes[] = {1U};
    const html_resource_t resource =
        {bytes, sizeof(bytes), HTML_COMPRESS_GZIP, "text/html", NULL};
    const char *requests[] =
    {
        "GET / HTTP/1.1\r\nX-If-None-Match: *\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\nIf-None-Match: *\r\n"
    };
    for (size_t index = 0U; 2U > index; index++)
    {
        output_len = 0U;
        http_response_note_request_headers(requests[index],
                                           (int)strlen(requests[index]));
        http_send_html_resource(&resource);
        TEST_ASSERT_NOT_NULL(strstr(output, "200 OK"));
    }
}

void test_overlong_conditional_header_cannot_match_a_truncated_prefix(void)
{
    static const uint8_t bytes[] = {1U};
    const html_resource_t resource =
        {bytes, sizeof(bytes), HTML_COMPRESS_GZIP, "text/html", NULL};
    char tag[67];
    char request[400];
    resource_tag(&resource, tag);
    const uint32_t count = xsnprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\nIf-None-Match: %s", tag);
    (void)memset(request + count, 'x', 280U);
    request[count + 280U] = '\0';
    http_response_note_request_headers(request, (int)strlen(request));
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "200 OK"));
}



void test_gzip_package_is_raw_binary_with_private_revalidation(void)
{
    static const uint8_t data[] = {0x1FU, 0x8BU, 0U, 0xFFU};
    const html_resource_t resource =
        {data, sizeof(data), HTML_COMPRESS_GZIP, "text/html", NULL};

    http_send_gzip_package(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "Content-Type: application/gzip"));
    TEST_ASSERT_NULL(strstr(output, "Content-Encoding:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Cache-Control: private, no-cache"));
    TEST_ASSERT_NOT_NULL(strstr(output, "ETag: "));
    TEST_ASSERT_EQUAL_MEMORY(data, output + output_len - sizeof(data),
                             sizeof(data));
}

void test_gzip_package_rejects_invalid_resources_before_hashing(void)
{
    const html_resource_t invalid[] =
    {
        {NULL, 1U, HTML_COMPRESS_GZIP, "text/html", NULL},
        {(const uint8_t *)"x", 0U, HTML_COMPRESS_GZIP, "text/html", NULL},
        {(const uint8_t *)"x", 1U, HTML_COMPRESS_NONE, "text/html", NULL},
        {(const uint8_t *)"x", (uint32_t)INT_MAX + 1U,
         HTML_COMPRESS_GZIP, "text/html", NULL}
    };

    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         index++)
    {
        output_len = 0U;
        output[0] = '\0';
        http_send_gzip_package(&invalid[index]);
        TEST_ASSERT_NOT_NULL(strstr(output, "500 Internal Server Error"));
    }
    output_len = 0U;
    http_send_gzip_package(NULL);
    TEST_ASSERT_NOT_NULL(strstr(output, "500 Internal Server Error"));
}

void test_precomputed_etag_matches_runtime_for_html_and_package(void)
{
    static const uint8_t bytes[] = {0U, 1U, 0xFFU};
    html_resource_t resource =
        {bytes, sizeof(bytes), HTML_COMPRESS_GZIP, "text/html", NULL};
    char expected[67];
    char actual[67];
    char request[160];

    resource_tag(&resource, expected);
    resource.etag = expected;
    resource_tag(&resource, actual);
    TEST_ASSERT_EQUAL_STRING(expected, actual);

    (void)xsnprintf(request, sizeof(request),
        "GET / HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", expected);
    http_response_note_request_headers(request, (int)strlen(request));
    http_send_html_resource(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "304 Not Modified"));
    const char *end = strstr(output, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(end);
    TEST_ASSERT_EQUAL_size_t(output_len, (size_t)(end - output) + 4U);

    output_len = 0U;
    output[0] = '\0';
    http_send_gzip_package(&resource);
    TEST_ASSERT_NOT_NULL(strstr(output, "304 Not Modified"));
    TEST_ASSERT_NOT_NULL(strstr(output, "private, no-cache"));
    end = strstr(output, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(end);
    TEST_ASSERT_EQUAL_size_t(output_len, (size_t)(end - output) + 4U);
}

/*** end of file ***/
