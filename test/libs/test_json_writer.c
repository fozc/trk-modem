/*
 * test_json_writer.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Writer grammar, bounded output and exact continuation of public calls.
 */
#include "unity.h"
#include "json.h"

#include <string.h>

static json_writer_t writer;
static char output[1024];
static char segment[1024];
static size_t collected;
static size_t chunk_size;

void setUp(void)
{
    memset(output, '?', sizeof(output));
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, output, sizeof(output)));
    collected = 0U;
    chunk_size = sizeof(segment);
}

void tearDown(void)
{
}

static json_span_t span(const char *text)
{
    json_span_t result = {text, strlen(text)};
    return result;
}

static void assert_output(const char *expected)
{
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(strlen(expected), writer.total);
    TEST_ASSERT_EQUAL_MEMORY(expected, output, writer.total);
    TEST_ASSERT_EQUAL_CHAR('?', output[writer.total]);
}

static void flush_segment(void)
{
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(output) - collected, writer.used);
    memcpy(output + collected, segment, writer.used);
    collected += writer.used;
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_set_output(&writer, segment, chunk_size));
}

static void complete(json_status_t status)
{
    while (status == JSON_NEED_OUTPUT)
    {
        flush_segment();
        status = json_writer_resume(&writer);
    }
    TEST_ASSERT_EQUAL(JSON_OK, status);
}

static void write_sample(void)
{
    complete(json_write_object_begin(&writer));
    complete(json_write_key(&writer, span("APN")));
    complete(json_write_string(&writer, span("a\n\"\\\xE2\x82\xAC")));
    complete(json_write_key(&writer, span("values")));
    complete(json_write_array_begin(&writer));
    complete(json_write_int32(&writer, INT32_MIN));
    complete(json_write_uint32(&writer, UINT32_MAX));
    complete(json_write_bool(&writer, true));
    complete(json_write_bool(&writer, false));
    complete(json_write_null(&writer));
    complete(json_write_number(&writer, span("-0.25E+2")));
    complete(json_write_float(&writer, 1.25F));
    complete(json_write_object_begin(&writer));
    complete(json_write_object_end(&writer));
    complete(json_write_array_end(&writer));
    complete(json_write_object_end(&writer));
}

void test_json_writer_all_types_expected_json_and_exact_count(void)
{
    const char *expected =
        "{\"APN\":\"a\\u000a\\\"\\\\\xE2\x82\xAC\","
        "\"values\":"
        "[-2147483648,4294967295,true,false,null,-0.25E+2,1.25,{}]}";
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, segment, sizeof(segment)));
    write_sample();
    flush_segment();
    assert_output(expected);
    size_t expected_size = writer.total;
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    write_sample();
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(expected_size, writer.total);
    TEST_ASSERT_EQUAL(0U, writer.used);
}

void test_json_writer_every_output_segment_size_matches_full_output(void)
{
    char expected[256];
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, segment, sizeof(segment)));
    write_sample();
    size_t length = writer.used;
    memcpy(expected, segment, length);
    for (size_t size = 1U; size <= length + 1U; ++size)
    {
        collected = 0U;
        chunk_size = size;
        TEST_ASSERT_EQUAL(JSON_OK,
                          json_writer_init(&writer, segment, chunk_size));
        write_sample();
        TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
        flush_segment();
        TEST_ASSERT_EQUAL(length, collected);
        TEST_ASSERT_EQUAL(length, writer.total);
        TEST_ASSERT_EQUAL_MEMORY(expected, output, length);
    }
}

void test_json_writer_zero_output_can_pause_without_losing_the_operation(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, NULL, 0U));
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT,
                      json_write_string(&writer, span("a\nb")));
    TEST_ASSERT_EQUAL(0U, writer.total);
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT, json_writer_resume(&writer));
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_set_output(&writer, output, sizeof(output)));
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_resume(&writer));
    assert_output("\"a\\u000ab\"");
}

void test_json_writer_all_control_bytes_and_zero_are_escaped(void)
{
    char data[32];
    for (size_t index = 0U; index < sizeof(data); ++index)
    {
        data[index] = (char)index;
    }
    json_span_t value = {data, sizeof(data)};
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string(&writer, value));
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(194U, writer.used);
    json_reader_t reader;
    json_token_t token;
    char scratch[32];
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_reader_init(&reader, scratch, sizeof(scratch)));
    json_span_t text = {output, writer.used};
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, text, true));
    TEST_ASSERT_EQUAL(JSON_OK, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(sizeof(data), token.text.length);
    TEST_ASSERT_EQUAL_MEMORY(data, token.text.data, sizeof(data));
    TEST_ASSERT_EQUAL(JSON_DONE, json_read(&reader, &token));
}

void test_json_writer_empty_key_and_string(void)
{
    json_span_t empty = {NULL, 0U};
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_key(&writer, empty));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string(&writer, empty));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_end(&writer));
    assert_output("{\"\":\"\"}");
}

void test_json_writer_does_not_overrun_exact_output_capacity(void)
{
    char guarded[] = {'L', '?', '?', '?', 'R'};
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, guarded + 1U, 3U));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_int32(&writer, -12));
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL_CHAR('L', guarded[0]);
    TEST_ASSERT_EQUAL_CHAR('R', guarded[4]);
    TEST_ASSERT_EQUAL_MEMORY("-12", guarded + 1U, 3U);
}

void test_json_writer_values_require_object_key(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_null(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
    TEST_ASSERT_EQUAL(1U, writer.total);
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_object_end(&writer));
}

void test_json_writer_key_requires_object_and_previous_value(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_key(&writer, span("x")));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_write_array_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_key(&writer, span("x")));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_key(&writer, span("x")));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_key(&writer, span("y")));
}

void test_json_writer_cannot_close_object_with_missing_value(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_key(&writer, span("x")));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_object_end(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
}

void test_json_writer_rejects_mismatched_end_and_extra_root(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_array_end(&writer));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_array_end(&writer));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_write_null(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_null(&writer));
}

void test_json_writer_finish_checks_empty_and_unclosed_document(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_INCOMPLETE, writer.error);
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_write_array_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_finish(&writer));
}

void test_json_writer_depth_exact_limit_and_one_over(void)
{
    for (size_t index = 0U; index < JSON_MAX_DEPTH; ++index)
    {
        TEST_ASSERT_EQUAL(JSON_OK, json_write_array_begin(&writer));
    }
    TEST_ASSERT_EQUAL(JSON_OK, json_write_null(&writer));
    for (size_t index = 0U; index < JSON_MAX_DEPTH; ++index)
    {
        TEST_ASSERT_EQUAL(JSON_OK, json_write_array_end(&writer));
    }
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    setUp();
    for (size_t index = 0U; index < JSON_MAX_DEPTH; ++index)
    {
        TEST_ASSERT_EQUAL(JSON_OK, json_write_array_begin(&writer));
    }
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_object_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_DEPTH, writer.error);
}

void test_json_writer_pending_call_rejects_new_operation(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, output, 1U));
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT, json_write_uint32(&writer, 12345U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_int32(&writer, -999));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
    TEST_ASSERT_EQUAL_CHAR('1', output[0]);
    TEST_ASSERT_EQUAL_MEMORY("12345", writer.number, 5U);
}

void test_json_writer_rejects_nonfinite_float_without_emitting_value(void)
{
    uint32_t patterns[] = {0x7F800000U, 0xFF800000U, 0x7FC00000U, 0x7F800001U,
                           0xFFC00000U};
    for (size_t index = 0U; index < sizeof(patterns) / sizeof(patterns[0]);
         ++index)
    {
        float value;
        memcpy(&value, &patterns[index], sizeof(value));
        setUp();
        TEST_ASSERT_EQUAL(JSON_ERROR, json_write_float(&writer, value));
        TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, writer.error);
        TEST_ASSERT_EQUAL(0U, writer.total);
    }
}

void test_json_writer_rejects_invalid_utf8_before_any_string_bytes(void)
{
    const char *cases[] = {"\x80",         "\xC0\x80",     "\xC2",
                           "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                           "\xFF",         "\xE2\x82x"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        TEST_ASSERT_EQUAL(JSON_ERROR,
                          json_write_string(&writer, span(cases[index])));
        TEST_ASSERT_EQUAL(JSON_ERROR_UTF8, writer.error);
        TEST_ASSERT_EQUAL(0U, writer.total);
    }
}

void test_json_writer_raw_number_requires_exact_json_number(void)
{
    const char *cases[] = {"",    " 1", "1 ",  "01", "1.",   "+1",
                           "NaN", "1e", "1,2", "1]", "0x10", "--1"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        TEST_ASSERT_EQUAL(JSON_ERROR,
                          json_write_number(&writer, span(cases[index])));
        TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, writer.error);
        TEST_ASSERT_EQUAL(0U, writer.total);
    }
}

void test_json_writer_can_reset_after_error_and_resume_when_idle(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_array_end(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_resume(&writer));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_resume(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_bool(&writer, false));
    assert_output("false");
}

void test_json_writer_size_counter_overflow_is_explicit(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    writer.total = SIZE_MAX - 1U;
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_null(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_SIZE, writer.error);
    TEST_ASSERT_EQUAL(SIZE_MAX, writer.total);
}

void test_json_writer_argument_validation(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_init(NULL, output, 1U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_init_count(NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_resume(NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_finish(NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_uint32(NULL, 1U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_string(NULL, span("x")));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_set_output(NULL, output, 1U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_init(&writer, NULL, 1U));
    setUp();
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR,
                      json_writer_set_output(&writer, output, sizeof(output)));
}

void test_json_writer_independent_pending_contexts_do_not_share_storage(void)
{
    json_writer_t other;
    char other_output[48];
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, output, 1U));
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT, json_write_int32(&writer, INT32_MIN));
    TEST_ASSERT_EQUAL(
        JSON_OK, json_writer_init(&other, other_output, sizeof(other_output)));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_float(&other, 1.25F));
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&other));
    TEST_ASSERT_EQUAL_MEMORY("1.25", other_output, other.used);
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_set_output(&writer, output + 1U,
                                                      sizeof(output) - 1U));
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_resume(&writer));
    assert_output("-2147483648");
}

void test_json_writer_integer_output_reference_values(void)
{
    const int32_t values[] = {0, 1, -1, INT32_MAX, INT32_MIN};
    const char *expected[] = {"0", "1", "-1", "2147483647", "-2147483648"};
    for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        setUp();
        TEST_ASSERT_EQUAL(JSON_OK, json_write_int32(&writer, values[index]));
        assert_output(expected[index]);
    }
}

void test_json_writer_bulk_raw_copy_stops_at_size_counter_limit(void)
{
    char guarded[] = {'L', '?', '?', '?', '?', '?', 'R'};
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, guarded + 1U, 5U));
    writer.total = SIZE_MAX - 2U;
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_number(&writer, span("12345")));
    TEST_ASSERT_EQUAL(JSON_ERROR_SIZE, writer.error);
    TEST_ASSERT_EQUAL(SIZE_MAX, writer.total);
    TEST_ASSERT_EQUAL(SIZE_MAX, writer.error_offset);
    TEST_ASSERT_EQUAL(2U, writer.used);
    TEST_ASSERT_EQUAL_MEMORY("L12???R", guarded, sizeof(guarded));
}

void test_json_writer_exact_size_counter_limit_is_not_overflow(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    writer.total = SIZE_MAX - 4U;
    TEST_ASSERT_EQUAL(JSON_OK, json_write_null(&writer));
    TEST_ASSERT_EQUAL(SIZE_MAX, writer.total);
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
}

void test_json_writer_block_wait_then_overflow_keeps_output_guards(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, NULL, 0U));
    writer.total = SIZE_MAX - 1U;
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT, json_write_null(&writer));
    TEST_ASSERT_EQUAL(SIZE_MAX - 1U, writer.total);
    char guarded[] = {'L', '?', '?', 'R'};
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_set_output(&writer, guarded + 1U, 2U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_resume(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_SIZE, writer.error);
    TEST_ASSERT_EQUAL(1U, writer.used);
    TEST_ASSERT_EQUAL_MEMORY("Ln?R", guarded, sizeof(guarded));
}

void test_json_writer_large_plain_string_and_escapes_match_every_segment(void)
{
    char data[400];
    memset(data, 'a', sizeof(data));
    data[99] = '\n';
    data[199] = '"';
    data[299] = '\\';
    json_span_t text = {data, sizeof(data)};
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string(&writer, text));
    size_t length = writer.used;
    char expected[512];
    memcpy(expected, output, length);
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string(&writer, text));
    TEST_ASSERT_EQUAL(length, writer.total);
    for (size_t size = 1U; size <= length; ++size)
    {
        collected = 0U;
        chunk_size = size;
        TEST_ASSERT_EQUAL(JSON_OK,
                          json_writer_init(&writer, segment, chunk_size));
        complete(json_write_string(&writer, text));
        TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
        flush_segment();
        TEST_ASSERT_EQUAL(length, collected);
        TEST_ASSERT_EQUAL_MEMORY(expected, output, length);
    }
}

void test_json_writer_ascii_prefix_does_not_hide_invalid_utf8(void)
{
    const char *cases[] = {"abcdefghijklmnopqrstuvwxyz\xC4"
                           "x",
                           "abcdefghijklmnopqrstuvwxyz\xC4",
                           "abc\x80"
                           "def"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        TEST_ASSERT_EQUAL(JSON_ERROR,
                          json_write_string(&writer, span(cases[index])));
        TEST_ASSERT_EQUAL(JSON_ERROR_UTF8, writer.error);
        TEST_ASSERT_EQUAL(0U, writer.used);
        TEST_ASSERT_EQUAL_CHAR('?', output[0]);
        setUp();
        TEST_ASSERT_EQUAL(JSON_OK, json_write_object_begin(&writer));
        TEST_ASSERT_EQUAL(JSON_ERROR,
                          json_write_key(&writer, span(cases[index])));
        TEST_ASSERT_EQUAL(JSON_ERROR_UTF8, writer.error);
        TEST_ASSERT_EQUAL(1U, writer.used);
        TEST_ASSERT_EQUAL_CHAR('?', output[1]);
    }
}

void test_json_writer_fixed_float_rounding_and_decimal_places(void)
{
    const float values[] = {1.25F, 1.75F,       -1.25F, 2.5F, 3.5F,   0.5F,
                            0.75F, 9.99609375F, -0.0F,  0.0F, 0.125F, 0.375F};
    const uint8_t decimals[] = {1U, 1U, 1U, 0U, 0U, 0U, 0U, 2U, 3U, 9U, 2U, 2U};
    const char *expected[] = {"1.2",    "1.8",         "-1.2", "2",
                              "4",      "0",           "1",    "10.00",
                              "-0.000", "0.000000000", "0.12", "0.38"};
    for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        setUp();
        TEST_ASSERT_EQUAL(
            JSON_OK,
            json_write_float_fixed(&writer, values[index], decimals[index]));
        assert_output(expected[index]);
        size_t length = writer.total;
        TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
        TEST_ASSERT_EQUAL(
            JSON_OK,
            json_write_float_fixed(&writer, values[index], decimals[index]));
        TEST_ASSERT_EQUAL(length, writer.total);
    }
}

void test_json_writer_fixed_float_resumes_at_every_output_size(void)
{
    const char *expected = "-123.125000000";
    for (size_t size = 1U; size <= strlen(expected); ++size)
    {
        collected = 0U;
        chunk_size = size;
        TEST_ASSERT_EQUAL(JSON_OK,
                          json_writer_init(&writer, segment, chunk_size));
        complete(json_write_float_fixed(&writer, -123.125F, 9U));
        TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
        flush_segment();
        TEST_ASSERT_EQUAL(strlen(expected), collected);
        TEST_ASSERT_EQUAL_MEMORY(expected, output, collected);
    }
}

void test_json_writer_fixed_float_rejects_invalid_arguments_without_output(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_float_fixed(NULL, 1.0F, 0U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_float_fixed(&writer, 1.0F, 10U));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, writer.error);
    TEST_ASSERT_EQUAL(0U, writer.used);
    TEST_ASSERT_EQUAL_CHAR('?', output[0]);
    const uint32_t invalid[] = {0x7F800000U, 0xFF800000U, 0x7FC00000U};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index)
    {
        float value;
        memcpy(&value, &invalid[index], sizeof(value));
        setUp();
        TEST_ASSERT_EQUAL(JSON_ERROR,
                          json_write_float_fixed(&writer, value, 2U));
        TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, writer.error);
        TEST_ASSERT_EQUAL(0U, writer.used);
    }
}

void test_json_writer_fixed_float_keeps_its_number_while_pending(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, NULL, 0U));
    TEST_ASSERT_EQUAL(JSON_NEED_OUTPUT,
                      json_write_float_fixed(&writer, 1.25F, 2U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_float_fixed(&writer, 9.0F, 2U));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
    TEST_ASSERT_EQUAL_MEMORY("1.25", writer.number, 4U);
}

void test_json_writer_fixed_float_extremes_preserve_exact_digits(void)
{
    const uint32_t bits[] = {0xFF7FFFFFU, 0x80000001U, 0x00000001U};
    const char *expected[] = {
        "-340282346638528859811704183484516925440.000000000", "-0.000000000",
        "0.000000000"};
    for (size_t index = 0U; index < sizeof(bits) / sizeof(bits[0]); ++index)
    {
        float value;
        memcpy(&value, &bits[index], sizeof(value));
        setUp();
        TEST_ASSERT_EQUAL(JSON_OK, json_write_float_fixed(&writer, value, 9U));
        assert_output(expected[index]);
        TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
        TEST_ASSERT_EQUAL(JSON_OK, json_write_float_fixed(&writer, value, 9U));
        TEST_ASSERT_EQUAL(strlen(expected[index]), writer.total);
        for (size_t size = 1U; size <= strlen(expected[index]); ++size)
        {
            collected = 0U;
            chunk_size = size;
            TEST_ASSERT_EQUAL(JSON_OK,
                              json_writer_init(&writer, segment, chunk_size));
            complete(json_write_float_fixed(&writer, value, 9U));
            TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
            flush_segment();
            TEST_ASSERT_EQUAL(strlen(expected[index]), collected);
            TEST_ASSERT_EQUAL_MEMORY(expected[index], output, collected);
        }
    }
}

void test_json_writer_chunked_string_matches_whole_string_at_each_output_size(
    void)
{
    const char first[] = {'a', '\0', '\n'};
    json_span_t value = {first, sizeof(first)};
    const char *expected = "{\"log\":\"a\\u0000\\u000a\\\"\\\\\xE2\x82\xAC\"}";
    for (chunk_size = 1U; chunk_size <= strlen(expected); ++chunk_size)
    {
        collected = 0U;
        TEST_ASSERT_EQUAL(JSON_OK,
                          json_writer_init(&writer, segment, chunk_size));
        complete(json_write_object_begin(&writer));
        complete(json_write_key(&writer, JSON_SPAN_LITERAL("log")));
        complete(json_write_string_begin(&writer));
        complete(json_write_string_chunk(&writer, value));
        complete(json_write_string_chunk(&writer, JSON_SPAN_LITERAL("")));
        complete(json_write_string_chunk(
            &writer, JSON_SPAN_LITERAL("\"\\\xE2\x82\xAC")));
        complete(json_write_string_end(&writer));
        complete(json_write_object_end(&writer));
        TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
        flush_segment();
        TEST_ASSERT_EQUAL(strlen(expected), collected);
        TEST_ASSERT_EQUAL_MEMORY(expected, output, collected);
    }
}

void test_json_writer_chunked_string_enforces_operation_order(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR,
                      json_write_string_chunk(&writer, JSON_SPAN_LITERAL("x")));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_string_end(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_uint32(&writer, 1U));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, writer.error);
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init_count(&writer));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_writer_finish(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR_INCOMPLETE, writer.error);
}

void test_json_writer_chunked_string_rejects_invalid_utf8_and_null_arguments(
    void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_string_begin(NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR,
                      json_write_string_chunk(NULL, JSON_SPAN_LITERAL("")));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_string_end(NULL));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_string_begin(&writer));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_write_string_chunk(
                                      &writer, JSON_SPAN_LITERAL("\xC2")));
    TEST_ASSERT_EQUAL(JSON_ERROR_UTF8, writer.error);
}

/*** end of file ***/
