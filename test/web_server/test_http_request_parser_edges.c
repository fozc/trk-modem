/*
 * test_http_request_parser_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Parser edge cases beyond test_http_request_parser.c: Content-Length
 * value forms and duplicates, header/body region scoping, method and
 * line-ending leniency, query splitting and path strictness.
 */
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "http_request_parser.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_content_length_zero_keeps_body_null_and_request_complete(void)
{
    char request_data[] = "POST /d HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(0, request.content_length);
    TEST_ASSERT_NULL(request.body);
    TEST_ASSERT_EQUAL_INT(0, request.body_length);
    TEST_ASSERT_TRUE(http_is_request_complete(
        request_data, (int)(sizeof(request_data) - 1U), &request));
}

void test_content_length_first_header_wins_when_duplicated(void)
{
    /* Request-smuggling shape: the parser stops at the first match. */
    static const char headers[] =
        "Content-Length: 4\r\nX-Y: z\r\nContent-Length: 0\r\n";

    TEST_ASSERT_EQUAL_INT(4,
                          http_parse_content_length(
                              headers, (int)(sizeof(headers) - 1U)));
}

void test_content_length_negative_or_blank_value_reads_as_absent(void)
{
    static const char negative[] = "Content-Length: -5\r\n";
    static const char blank[] = "Content-Length: \r\n";

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_content_length(
                              negative, (int)(sizeof(negative) - 1U)));
    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_content_length(
                              blank, (int)(sizeof(blank) - 1U)));
}

void test_content_length_leading_zeros_and_trailing_garbage(void)
{
    /* Current lenient contract: digits are consumed until the first
     * non-digit and whatever follows is ignored. */
    static const char zeros[] = "Content-Length: 0004\r\n";
    static const char garbage[] = "Content-Length: 12abc\r\n";

    TEST_ASSERT_EQUAL_INT(4,
                          http_parse_content_length(
                              zeros, (int)(sizeof(zeros) - 1U)));
    TEST_ASSERT_EQUAL_INT(12,
                          http_parse_content_length(
                              garbage, (int)(sizeof(garbage) - 1U)));
}

void test_content_length_is_not_searched_inside_body_region(void)
{
    char request_data[] =
        "POST /d HTTP/1.1\r\nHost: x\r\n\r\nContent-Length: 99";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(0, request.content_length);
    TEST_ASSERT_NULL(request.body);
}

void test_lowercase_method_parses_but_is_reported_unknown(void)
{
    /* Method recognition is exact-case; dispatch decides what to do
     * with HTTP_METHOD_UNKNOWN. */
    char request_data[] = "get /path HTTP/1.1\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_UNKNOWN, request.method);
    TEST_ASSERT_EQUAL_STRING("/path", request.path);
}

void test_lf_only_line_endings_leave_headers_incomplete(void)
{
    char lf_only[] = "GET / HTTP/1.1\nHost: x\n\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_find_header_end(lf_only,
                                               (int)(sizeof(lf_only) - 1U)));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(lf_only,
                                             (int)(sizeof(lf_only) - 1U),
                                             &request));
}

void test_query_split_keeps_extra_question_marks_and_empty_tail(void)
{
    char two_marks[] = "GET /a?b?c HTTP/1.1\r\n\r\n";
    char empty_query[] = "GET /path? HTTP/1.1\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(two_marks,
                                             (int)(sizeof(two_marks) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_STRING("/a", request.path);
    TEST_ASSERT_EQUAL_STRING("b?c", request.query_string);

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(empty_query,
                                             (int)(sizeof(empty_query) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_STRING("/path", request.path);
    TEST_ASSERT_NOT_NULL(request.query_string);
    TEST_ASSERT_EQUAL_STRING("", request.query_string);
}

void test_long_query_does_not_inflate_the_path_length_check(void)
{
    /* The 256-byte limit applies to the path segment only; the query
     * string is split off before validation. */
    char request_data[600];
    http_request_t request;
    size_t offset = 0U;

    memcpy(request_data, "GET /", 5U);
    memset(&request_data[5U], 'p', 200U);
    request_data[205U] = '?';
    memset(&request_data[206U], 'q', 250U);
    offset = 206U + 250U;
    memcpy(&request_data[offset], " HTTP/1.1\r\n\r\n", 13U);
    offset += 13U;
    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data, (int)offset,
                                             &request));
    TEST_ASSERT_EQUAL_UINT(201U, strlen(request.path));
    TEST_ASSERT_EQUAL_UINT(250U, strlen(request.query_string));
}

void test_declared_body_with_zero_received_bytes_is_not_complete(void)
{
    char request_data[] = "POST /d HTTP/1.1\r\nContent-Length: 4\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(0, request.body_length);
    TEST_ASSERT_FALSE(http_is_request_complete(
        request_data, (int)(sizeof(request_data) - 1U), &request));
}

void test_query_param_requires_equals_sign_and_always_clears_output(void)
{
    char value[4] = "keep";

    /* The output is initialized on entry even when the key is absent
     * or has no '=': callers must not rely on preserved content. */
    TEST_ASSERT_FALSE(http_get_query_param("a=1&key", "key", value,
                                           (int)sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("", value);
}

void test_double_dot_anywhere_in_path_is_rejected_strictly(void)
{
    /* strstr-based check: even a benign "a..b" filename is refused. */
    char request_data[] = "GET /css/a..b.css HTTP/1.1\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
}

/*** end of file ***/
