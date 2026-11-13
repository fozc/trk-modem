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

void test_signed_32_bit_minimum_and_adjacent_values(void)
{
    int32_t value = 7;
    TEST_ASSERT_EQUAL_INT(1,
        xscanf("-2147483648", 11U, "%s32", &value));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, value);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, xscanf_get_last_error());
    TEST_ASSERT_EQUAL_INT(1,
        xscanf("-2147483647", 11U, "%s32", &value));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN + 1, value);
    TEST_ASSERT_EQUAL_INT(1,
        xscanf("2147483647", 10U, "%d", &value));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, value);
}

void test_signed_32_bit_underflow_preserves_destination(void)
{
    int32_t value = 7;
    TEST_ASSERT_EQUAL_INT(0,
        xscanf("-2147483649", 11U, "%s32", &value));
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_UNDERFLOW, xscanf_get_last_error());
    TEST_ASSERT_EQUAL_INT32(7, value);
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

    /* Ikinci %c icin veri yoktur; ilk atama korunur ve NO_MATCH bildirilir. */
    TEST_ASSERT_EQUAL_INT(1, xscanf(" ", 1U, "%c%c", &first, &second));
    TEST_ASSERT_EQUAL_CHAR(' ', first);
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NO_MATCH, xscanf_get_last_error());
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

void test_scanner_numeric_prefix_is_not_json_number_validation(void)
{
    const char *input[] = {"01", "1e2", "42949672950"};
    const size_t consumed[] = {2U, 1U, 10U};
    const uint32_t expected[] = {1U, 1U, UINT32_MAX};
    for (size_t index = 0U; index < sizeof(input) / sizeof(input[0]); ++index)
    {
        xscanf_result_t result;
        uint32_t value = 73U;
        TEST_ASSERT_EQUAL_INT(1, xscanf_ex(&result, input[index],
            strlen(input[index]), "%u32", &value));
        TEST_ASSERT_EQUAL_INT(XSCANF_OK, result.error);
        TEST_ASSERT_EQUAL(consumed[index], result.input_pos);
        TEST_ASSERT_EQUAL_UINT32(expected[index], value);
    }
}

void test_scanner_float_format_is_explicitly_unsupported(void)
{
    xscanf_result_t result;
    float value = 73.0F;
    TEST_ASSERT_EQUAL_INT(0,
        xscanf_ex(&result, "1.25", 4U, "%f", &value));
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_INVALID_FORMAT, result.error);
    TEST_ASSERT_EQUAL(0U, result.input_pos);
    const float expected = 73.0F;
    TEST_ASSERT_EQUAL_MEMORY(&expected, &value, sizeof(value));
}

void test_string_width_reserves_an_extra_byte_for_terminator(void)
{
    char buffer[17];
    xscanf_result_t result;
    const char input[] = "1234567890123456";

    TEST_ASSERT_EQUAL_INT(1, xscanf_ex(&result, input, 16U, "%16S", buffer));
    TEST_ASSERT_EQUAL_STRING(input, buffer);
    TEST_ASSERT_EQUAL_CHAR('\0', buffer[16]);
    TEST_ASSERT_EQUAL_INT(XSCANF_OK, result.error);
    TEST_ASSERT_EQUAL(16U, result.input_pos);
}

void test_null_numeric_and_string_outputs_are_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(0, xscanf("7", 1U, "%u32", (uint32_t *)NULL));
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NULL_PTR, xscanf_get_last_error());
    TEST_ASSERT_EQUAL_INT(0, xscanf("abc", 3U, "%3S", (char *)NULL));
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NULL_PTR, xscanf_get_last_error());
}

void test_invalid_formats_preserve_destination(void)
{
    const char *formats[] = {"%u64", "%99999999999999999999u32", "%"};
    for (size_t index = 0U; index < sizeof(formats) / sizeof(formats[0]); index++)
    {
        uint32_t value = 73U;
        TEST_ASSERT_EQUAL_INT(0, xscanf("7", 1U, formats[index], &value));
        TEST_ASSERT_EQUAL_INT(XSCANF_ERR_INVALID_FORMAT, xscanf_get_last_error());
        TEST_ASSERT_EQUAL_UINT32(73U, value);
    }
}

void test_long_skip_fields_leave_following_conversion_aligned(void)
{
    char input[103];
    const char *formats[] = {"%*100S,%u32", "%*100r,%u32"};
    memset(input, 'a', 100U);
    memcpy(&input[100], ",7", 3U);
    for (size_t index = 0U; index < sizeof(formats) / sizeof(formats[0]); index++)
    {
        uint32_t value = 73U;
        xscanf_result_t result;
        TEST_ASSERT_EQUAL_INT(1, xscanf_ex(&result, input, 102U, formats[index], &value));
        TEST_ASSERT_EQUAL_UINT32(7U, value);
        TEST_ASSERT_EQUAL_INT(XSCANF_OK, result.error);
        TEST_ASSERT_EQUAL(102U, result.input_pos);
    }
}

void test_missing_final_literal_reports_error_after_completed_assignment(void)
{
    uint32_t value = 73U;
    xscanf_result_t result;
    TEST_ASSERT_EQUAL_INT(1, xscanf_ex(&result, "12", 2U, "%u32,", &value));
    TEST_ASSERT_EQUAL_UINT32(12U, value);
    TEST_ASSERT_EQUAL_INT(1, result.count);
    TEST_ASSERT_EQUAL_INT(XSCANF_ERR_NO_MATCH, result.error);
    TEST_ASSERT_EQUAL(2U, result.input_pos);
}

/*** end of file ***/
