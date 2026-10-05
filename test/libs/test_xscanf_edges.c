/*
 * test_xscanf_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Conversion-surface edges beyond test_xscanf_format_scenario.c and
 * the embedded suite: %r raw strings, %S delimiters taken from the
 * following format character, the '*' skip with %S, exhausted-input
 * %c and the xscanf_ex stop-position contract.
 */
#include "unity.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "xscanf.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_raw_string_requires_width(void)
{
    char buffer[8] = "keep";

    TEST_ASSERT_EQUAL_INT(0, xscanf("abc", 3U, "%r", buffer));
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_INVALID_FORMAT,
                          xscanf_get_last_error());
    TEST_ASSERT_EQUAL_STRING("keep", buffer);
}

void test_raw_string_reads_exact_width_including_whitespace(void)
{
    char buffer[8];

    TEST_ASSERT_EQUAL_INT(1, xscanf("ab cd", 5U, "%4r", buffer));
    TEST_ASSERT_EQUAL_STRING("ab c", buffer);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());

    /* Short input is not an error: the field reads what is there. */
    TEST_ASSERT_EQUAL_INT(1, xscanf("ab", 2U, "%4r", buffer));
    TEST_ASSERT_EQUAL_STRING("ab", buffer);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());
}

void test_string_delimiter_comes_from_next_format_character(void)
{
    char name[8];
    uint32_t value = 0U;

    /* The ':' after %S is the field delimiter, then a literal ':' must
     * match it before the number is read. */
    TEST_ASSERT_EQUAL_INT(2,
        xscanf("Temp: 25", 8U, "%S: %u32", name, &value));
    TEST_ASSERT_EQUAL_STRING("Temp", name);
    TEST_ASSERT_EQUAL_UINT32(25U, value);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());
}

void test_skip_string_consumes_field_without_counting(void)
{
    uint32_t port = 0U;

    TEST_ASSERT_EQUAL_INT(1,
        xscanf("listen 8080", 11U, "%*S %u32", &port));
    TEST_ASSERT_EQUAL_UINT32(8080U, port);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());
}

void test_char_conversion_reads_exact_char_and_stops_when_exhausted(void)
{
    char first = '\0';
    char second = '\0';

    /* %c consumes the byte as-is: no whitespace skipping. */
    TEST_ASSERT_EQUAL_INT(1, xscanf(" x", 2U, "%c", &first));
    TEST_ASSERT_EQUAL_CHAR(' ', first);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());

    /* Second %c has no byte left: parsing stops cleanly, the first
     * assignment is kept and no error is raised (input exhaustion is
     * not a mismatch). */
    TEST_ASSERT_EQUAL_INT(1, xscanf(" ", 1U, "%c%c", &first, &second));
    TEST_ASSERT_EQUAL_CHAR(' ', first);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());
    TEST_ASSERT_EQUAL_CHAR('\0', second);
}

void test_extended_result_reports_count_error_and_stop_position(void)
{
    xscanf_result_t result;
    uint32_t value = 0U;

    /* Full success: everything consumed. */
    TEST_ASSERT_EQUAL_INT(1,
        xscanf_ex(&result, "MODE=1", 6U, "MODE=%u32", &value));
    TEST_ASSERT_EQUAL_INT(1, result.count);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, result.error);
    TEST_ASSERT_EQUAL_UINT32(6U, result.input_pos);

    /* Literal mismatch: input_pos marks the offending byte. */
    TEST_ASSERT_EQUAL_INT(0,
        xscanf_ex(&result, "AB1", 3U, "AB2%u32", &value));
    TEST_ASSERT_EQUAL_INT(0, result.count);
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NO_MATCH, result.error);
    TEST_ASSERT_EQUAL_UINT32(2U, result.input_pos);

    /* Empty input: nothing consumed, distinct error code. */
    TEST_ASSERT_EQUAL_INT(0,
        xscanf_ex(&result, "", 0U, "%u32", &value));
    TEST_ASSERT_EQUAL_INT(0, result.count);
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_EMPTY_INPUT, result.error);
    TEST_ASSERT_EQUAL_UINT32(0U, result.input_pos);
}

void test_extended_result_reports_null_pointer(void)
{
    xscanf_result_t result;

    TEST_ASSERT_EQUAL_INT(-1,
        xscanf_ex(&result, NULL, 0U, "%u32"));
    TEST_ASSERT_EQUAL_INT(-1, result.count);
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NULL_PTR, result.error);
}

/*** end of file ***/
