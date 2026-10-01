/*
 * test_http_request_parser.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies HTTP request parsing, boundaries, and input rejection.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "http_request_parser.h"

#define HTTP_TEST_MAX_CONTENT_LENGTH 4608
#define HTTP_TEST_MAX_PATH_LENGTH    256U

void setUp(void)
{
}

void tearDown(void)
{
}

void test_http_find_header_end_requires_complete_delimiter(void)
{
    static const char complete[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    static const char partial[] = "GET / HTTP/1.1\r\nHost: x\r\n";

    TEST_ASSERT_EQUAL_INT((int)(sizeof(complete) - 1U),
                          http_find_header_end(complete,
                                               (int)(sizeof(complete) - 1U)));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_find_header_end(partial,
                                               (int)(sizeof(partial) - 1U)));
}

void test_http_parse_get_splits_path_and_query(void)
{
    char request_data[] =
        "GET /faults?feeder=3&phase=L2 HTTP/1.1\r\nHost: device\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_GET, request.method);
    TEST_ASSERT_EQUAL_STRING("/faults", request.path);
    TEST_ASSERT_EQUAL_STRING("feeder=3&phase=L2", request.query_string);
    TEST_ASSERT_EQUAL_INT(0, request.content_length);
    TEST_ASSERT_NULL(request.body);
}

void test_http_parse_post_clamps_body_to_content_length(void)
{
    char request_data[] =
        "POST /config HTTP/1.1\r\nContent-Length: 4\r\n\r\nDATAEXTRA";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_POST, request.method);
    TEST_ASSERT_EQUAL_INT(4, request.content_length);
    TEST_ASSERT_EQUAL_INT(4, request.body_length);
    TEST_ASSERT_EQUAL_MEMORY("DATA", request.body, 4U);
    TEST_ASSERT_TRUE(http_is_request_complete(request_data,
                                              (int)(sizeof(request_data) - 1U),
                                              &request));
}

void test_http_request_complete_waits_for_full_body(void)
{
    static const char request_data[] =
        "POST /config HTTP/1.1\r\nContent-Length: 5\r\n\r\nABC";
    http_request_t request = {
        .content_length = 5,
        .header_end_offset = 48
    };
    int header_end = http_find_header_end(request_data,
                                           (int)(sizeof(request_data) - 1U));

    TEST_ASSERT_GREATER_THAN_INT(0, header_end);
    request.header_end_offset = header_end;
    TEST_ASSERT_FALSE(http_is_request_complete(
        request_data, (int)(sizeof(request_data) - 1U), &request));
}

void test_http_parser_rejects_traversal_and_oversized_body(void)
{
    char traversal[] = "GET /../../secret HTTP/1.1\r\n\r\n";
    char oversized[] =
        "POST /fw HTTP/1.1\r\nContent-Length: 4609\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(traversal,
                                             (int)(sizeof(traversal) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(oversized,
                                             (int)(sizeof(oversized) - 1U),
                                             &request));
}

void test_http_query_param_matches_complete_key_and_truncates_value(void)
{
    char value[4];

    TEST_ASSERT_TRUE(http_get_query_param("line_id=7&line=12345", "line",
                                          value, (int)sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("123", value);
    TEST_ASSERT_FALSE(http_get_query_param("line_id=7", "line", value,
                                           (int)sizeof(value)));
}

void test_http_method_helpers_handle_known_and_unknown_methods(void)
{
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_GET, http_method_from_string("GET"));
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_POST, http_method_from_string("POST"));
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_UNKNOWN,
                          http_method_from_string("DELETE"));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                             http_method_to_string(HTTP_METHOD_UNKNOWN));
    TEST_ASSERT_TRUE(http_string_starts_with_ignore_case(
        "cOnTeNt-LeNgTh: 4", "Content-Length:"));
    TEST_ASSERT_FALSE(http_string_starts_with_ignore_case("GET", "GETTING"));
}

void test_http_find_header_end_handles_boundaries_and_null(void)
{
    static const char delimiter[] = "\r\n\r\n";
    static const char prefixed[] = "X\r\n\r\nBODY";

    TEST_ASSERT_EQUAL_INT(-1, http_find_header_end(NULL, 4));
    TEST_ASSERT_EQUAL_INT(-1, http_find_header_end(delimiter, 3));
    TEST_ASSERT_EQUAL_INT(4, http_find_header_end(delimiter, 4));
    TEST_ASSERT_EQUAL_INT(5, http_find_header_end(prefixed,
                                                  (int)sizeof(prefixed)));
}

void test_http_request_complete_handles_exact_boundary_and_invalid_state(void)
{
    static const char request_data[] = "HEADBODY";
    http_request_t request = {
        .content_length = 4,
        .header_end_offset = 4
    };

    TEST_ASSERT_FALSE(http_is_request_complete(NULL, 8, &request));
    TEST_ASSERT_FALSE(http_is_request_complete(request_data, 8, NULL));
    request.header_end_offset = 0;
    TEST_ASSERT_FALSE(http_is_request_complete(request_data, 8, &request));
    request.header_end_offset = 4;
    TEST_ASSERT_TRUE(http_is_request_complete(request_data, 8, &request));
    TEST_ASSERT_FALSE(http_is_request_complete(request_data, 7, &request));
    request.content_length = 0;
    TEST_ASSERT_TRUE(http_is_request_complete(request_data, 4, &request));
}

void test_http_request_line_rejects_missing_tokens_and_null_outputs(void)
{
    char missing_path[] = "GET";
    char missing_version[] = "GET /path";
    char valid[] = "GET /path HTTP/1.1";
    http_method_t method;
    char *path = NULL;
    char *query = NULL;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(NULL, 0, &method,
                                                  &path, &query));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(valid,
                                                  (int)sizeof(valid),
                                                  NULL, &path, &query));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(missing_path,
                                                  (int)sizeof(missing_path),
                                                  &method, &path, &query));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(missing_version,
                                                  (int)sizeof(missing_version),
                                                  &method, &path, &query));
}

void test_http_request_line_does_not_read_past_declared_length(void)
{
    char request_line[] = "GET /path HTTP/1.1";
    http_method_t method;
    char *path = NULL;
    char *query = NULL;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(request_line, 3,
                                                  &method, &path, &query));
}

void test_http_request_line_rejects_each_invalid_output(void)
{
    char request_line[] = "GET /path HTTP/1.1";
    http_method_t method;
    char *path = NULL;
    char *query = NULL;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(request_line, 0,
                                                  &method, &path, &query));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(request_line,
                                                  (int)sizeof(request_line),
                                                  &method, NULL, &query));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request_line(request_line,
                                                  (int)sizeof(request_line),
                                                  &method, &path, NULL));
}

void test_http_content_length_accepts_case_whitespace_and_limit(void)
{
    static const char headers[] =
        "Host: device\r\ncontent-length:\t4608\r\n";

    TEST_ASSERT_EQUAL_INT(HTTP_TEST_MAX_CONTENT_LENGTH,
                          http_parse_content_length(
                              headers, (int)(sizeof(headers) - 1U)));
}

void test_http_content_length_rejects_limit_plus_one_and_numeric_overflow(void)
{
    static const char too_large[] = "Content-Length: 4609\r\n";
    static const char overflow[] =
        "Content-Length: 999999999999999999999999999999\r\n";

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_content_length(
                              too_large, (int)(sizeof(too_large) - 1U)));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_content_length(
                              overflow, (int)(sizeof(overflow) - 1U)));
}

void test_http_content_length_handles_missing_line_end_and_empty_input(void)
{
    static const char no_line_end[] = "Content-Length: 12";

    TEST_ASSERT_EQUAL_INT(0, http_parse_content_length(NULL, 10));
    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_content_length(no_line_end, 0));
    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_content_length(
                              no_line_end,
                              (int)(sizeof(no_line_end) - 1U)));
}

void test_http_parse_request_rejects_invalid_arguments_and_incomplete_headers(void)
{
    char short_request[] = "GET";
    char incomplete[] = "GET / HTTP/1.1\r\nHost: device\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(-1, http_parse_request(NULL, 4, &request));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(short_request,
                                             (int)(sizeof(short_request) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(incomplete,
                                             (int)(sizeof(incomplete) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(incomplete,
                                             (int)(sizeof(incomplete) - 1U),
                                             NULL));
}

void test_http_parse_request_rejects_malformed_request_line(void)
{
    char request_data[] = "MALFORMED\r\n\r\n";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
}

void test_http_parse_request_reports_partial_body_without_clamping_up(void)
{
    char request_data[] =
        "POST /data HTTP/1.1\r\nContent-Length: 4\r\n\r\nAB";
    http_request_t request;

    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data,
                                             (int)(sizeof(request_data) - 1U),
                                             &request));
    TEST_ASSERT_EQUAL_INT(2, request.body_length);
    TEST_ASSERT_FALSE(http_is_request_complete(
        request_data, (int)(sizeof(request_data) - 1U), &request));
}

void test_http_header_end_rejects_near_match_delimiters(void)
{
    static const char wrong_second[] = "\rX\r\n";
    static const char wrong_fourth[] = "\r\n\rX";

    TEST_ASSERT_EQUAL_INT(-1,
                          http_find_header_end(
                              wrong_second,
                              (int)(sizeof(wrong_second) - 1U)));
    TEST_ASSERT_EQUAL_INT(-1,
                          http_find_header_end(
                              wrong_fourth,
                              (int)(sizeof(wrong_fourth) - 1U)));
}

void test_http_path_length_accepts_limit_and_rejects_limit_plus_one(void)
{
    char request_data[HTTP_TEST_MAX_PATH_LENGTH + 32U] = {0};
    http_request_t request;
    size_t offset = 0U;

    memcpy(request_data, "GET /", 5U);
    memset(&request_data[5U], 'a', HTTP_TEST_MAX_PATH_LENGTH - 1U);
    offset = 5U + HTTP_TEST_MAX_PATH_LENGTH - 1U;
    memcpy(&request_data[offset], " HTTP/1.1\r\n\r\n", 13U);
    offset += 13U;
    TEST_ASSERT_EQUAL_INT(0,
                          http_parse_request(request_data, (int)offset,
                                             &request));

    memset(request_data, 0, sizeof(request_data));
    memcpy(request_data, "GET /", 5U);
    memset(&request_data[5U], 'b', HTTP_TEST_MAX_PATH_LENGTH);
    offset = 5U + HTTP_TEST_MAX_PATH_LENGTH;
    memcpy(&request_data[offset], " HTTP/1.1\r\n\r\n", 13U);
    offset += 13U;
    TEST_ASSERT_EQUAL_INT(-1,
                          http_parse_request(request_data, (int)offset,
                                             &request));
}

void test_http_query_param_handles_positions_empty_values_and_small_output(void)
{
    char value[8] = "dirty";
    char one_byte[1] = {'x'};

    TEST_ASSERT_TRUE(http_get_query_param("first=1&middle=2&last=3",
                                          "first", value,
                                          (int)sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("1", value);
    TEST_ASSERT_TRUE(http_get_query_param("first=1&middle=2&last=3",
                                          "middle", value,
                                          (int)sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("2", value);
    TEST_ASSERT_TRUE(http_get_query_param("first=1&middle=2&last=",
                                          "last", value,
                                          (int)sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("", value);
    TEST_ASSERT_TRUE(http_get_query_param("key=value", "key", one_byte,
                                          (int)sizeof(one_byte)));
    TEST_ASSERT_EQUAL_HEX8(0U, (uint8_t)one_byte[0]);
}

void test_http_query_param_and_prefix_helpers_reject_invalid_arguments(void)
{
    char value[4] = "abc";

    TEST_ASSERT_FALSE(http_get_query_param(NULL, "key", value,
                                           (int)sizeof(value)));
    TEST_ASSERT_FALSE(http_get_query_param("key=1", NULL, value,
                                           (int)sizeof(value)));
    TEST_ASSERT_FALSE(http_get_query_param("key=1", "key", NULL,
                                           (int)sizeof(value)));
    TEST_ASSERT_FALSE(http_get_query_param("key=1", "key", value, 0));
    TEST_ASSERT_FALSE(http_string_starts_with_ignore_case(NULL, "GET"));
    TEST_ASSERT_FALSE(http_string_starts_with_ignore_case("GET", NULL));
    TEST_ASSERT_TRUE(http_string_starts_with_ignore_case("GET", ""));
}

void test_http_method_helpers_cover_all_enum_values_and_null(void)
{
    TEST_ASSERT_EQUAL_INT(HTTP_METHOD_UNKNOWN,
                          http_method_from_string(NULL));
    TEST_ASSERT_EQUAL_STRING("GET", http_method_to_string(HTTP_METHOD_GET));
    TEST_ASSERT_EQUAL_STRING("POST",
                             http_method_to_string(HTTP_METHOD_POST));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                             http_method_to_string((http_method_t)99));
}

/*** end of file ***/
