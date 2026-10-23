/*
 * test_json_reader.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Public reader API, invalid inputs, chunk boundaries and resource limits.
 */
#include "unity.h"
#include "json.h"

#include <string.h>

static json_reader_t reader;
static char scratch[512];
static json_token_t token;

void setUp(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, scratch,
                                               sizeof(scratch)));
}

void tearDown(void)
{
}

static json_span_t span(const char *text)
{
    json_span_t result = {text, strlen(text)};
    return result;
}

void test_json_span_literals_support_static_and_runtime_values(void)
{
    static const json_span_t keys[] =
    {
        JSON_SPAN_INIT("id"),
        JSON_SPAN_INIT("a\0b"),
        JSON_SPAN_INIT("")
    };
    json_span_t value = JSON_SPAN_LITERAL("APN");
    TEST_ASSERT_EQUAL_UINT(3U, value.length);
    TEST_ASSERT_EQUAL_MEMORY("APN", value.data, value.length);
    TEST_ASSERT_EQUAL_UINT(2U, keys[0].length);
    TEST_ASSERT_EQUAL_UINT(3U, keys[1].length);
    TEST_ASSERT_EQUAL_MEMORY("a\0b", keys[1].data, keys[1].length);
    TEST_ASSERT_EQUAL_UINT(0U, keys[2].length);
}

void test_json_token_equals_span_handles_bounded_and_embedded_zero_text(void)
{
    const char bytes[] = {'a', '\0', 'b', 'x'};
    json_span_t text = {bytes, 3U};
    json_token_t value = {JSON_STRING, {bytes, 3U}, 0U, false};
    TEST_ASSERT_TRUE(json_token_equals_span(&value, text));
    TEST_ASSERT_FALSE(json_token_equals(&value, "a"));
    TEST_ASSERT_FALSE(json_token_equals_span(&value,
        JSON_SPAN_LITERAL("a\0c")));
    text.length = sizeof(bytes);
    TEST_ASSERT_FALSE(json_token_equals_span(&value, text));
    value.type = JSON_KEY;
    TEST_ASSERT_TRUE(json_token_equals_span(&value,
        JSON_SPAN_LITERAL("a\0b")));
}

void test_json_token_equals_span_rejects_invalid_arguments_and_types(void)
{
    json_span_t empty = {NULL, 0U};
    json_span_t invalid = {NULL, 1U};
    json_token_t value = {JSON_STRING, {NULL, 0U}, 0U, false};
    TEST_ASSERT_TRUE(json_token_equals_span(&value, empty));
    TEST_ASSERT_TRUE(json_token_equals_span(&value, JSON_SPAN_LITERAL("")));
    TEST_ASSERT_FALSE(json_token_equals_span(NULL, empty));
    TEST_ASSERT_FALSE(json_token_equals_span(&value, invalid));
    value.text.length = 1U;
    TEST_ASSERT_FALSE(json_token_equals_span(&value, JSON_SPAN_LITERAL("a")));
    value.type = JSON_NUMBER;
    TEST_ASSERT_FALSE(json_token_equals_span(&value, invalid));
    TEST_ASSERT_FALSE(json_token_equals_span(&value, JSON_SPAN_LITERAL("1")));
}

static void feed(const char *text)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span(text), true));
}

static json_status_t drain(void)
{
    json_status_t status;
    do
    {
        status = json_read(&reader, &token);
    } while (status == JSON_OK);
    return status;
}

static void expect_type(json_type_t type)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(type, token.type);
}

void test_json_reader_all_types_and_decoded_key(void)
{
    feed(" {\"A\\u0050N\":\"internet\",\"a\":[-2.5e+3,"
         "true,false,null,{}]} \r\n");
    expect_type(JSON_OBJECT_BEGIN);
    expect_type(JSON_KEY);
    TEST_ASSERT_TRUE(json_token_equals(&token, "APN"));
    expect_type(JSON_STRING);
    TEST_ASSERT_TRUE(json_token_equals(&token, "internet"));
    expect_type(JSON_KEY);
    expect_type(JSON_ARRAY_BEGIN);
    expect_type(JSON_NUMBER);
    TEST_ASSERT_EQUAL_MEMORY("-2.5e+3", token.text.data, token.text.length);
    expect_type(JSON_BOOLEAN);
    TEST_ASSERT_TRUE(token.boolean);
    expect_type(JSON_BOOLEAN);
    TEST_ASSERT_FALSE(token.boolean);
    expect_type(JSON_NULL);
    expect_type(JSON_OBJECT_BEGIN);
    expect_type(JSON_OBJECT_END);
    expect_type(JSON_ARRAY_END);
    expect_type(JSON_OBJECT_END);
    TEST_ASSERT_EQUAL(JSON_DONE, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(JSON_DONE, json_read(&reader, &token));
}

void test_json_reader_valid_root_values(void)
{
    const char *cases[] = {"{}", "[]", "0", "-0", "42", "-1.23E-4",
        "true", "false", "null", "\"\"", " \n[{},[],0]\t"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        feed(cases[index]);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_DONE, drain(), cases[index]);
    }
}

void test_json_reader_rejects_invalid_grammar(void)
{
    const char *cases[] = {"", " ", "{", "[", "[1,]", "{\"x\":1,}",
        "{1:2}", "{\"x\" 1}", "{\"x\":}", "[1 2]", "[1:2]", "[}",
        "{]", "{{}}", "{\"x\"::0}", "[,,]", "true false", "{}[]",
        "True", "NULL", "undefined", "NaN", "Infinity", "-Infinity",
        "+1", ".1", "1.", "01", "-01", "--1", "-", "1e", "1e+",
        "1e-", "1e++2", "0x10", "1_0", "1a", "truex", "nul", "//a\n0",
        "/*a*/0", "[0]x", "\v0", "\f0", "\xEF\xBB\xBF{}"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        feed(cases[index]);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_ERROR, drain(), cases[index]);
        TEST_ASSERT_NOT_EQUAL(JSON_ERROR_NONE, reader.error);
    }
}

static uint32_t event_hash(uint32_t hash, const json_token_t *event)
{
    hash = (hash ^ (uint32_t)event->type) * 16777619U;
    hash = (hash ^ (uint32_t)event->offset) * 16777619U;
    for (size_t index = 0U; index < event->text.length; ++index)
    {
        hash = (hash ^ (uint8_t)event->text.data[index]) * 16777619U;
    }
    return (hash ^ (event->boolean ? 1U : 0U)) * 16777619U;
}

static uint32_t hash_chunks(const char *text, size_t chunk_size)
{
    size_t length = strlen(text);
    size_t offset = 0U;
    uint32_t hash = 2166136261U;
    setUp();
    while (offset < length)
    {
        size_t count = length - offset;
        if (count > chunk_size)
        {
            count = chunk_size;
        }
        json_span_t part = {text + offset, count};
        offset += count;
        TEST_ASSERT_EQUAL(JSON_OK,
            json_reader_feed(&reader, part, offset == length));
        json_status_t status = json_read(&reader, &token);
        while (status == JSON_OK)
        {
            hash = event_hash(hash, &token);
            status = json_read(&reader, &token);
        }
        TEST_ASSERT_EQUAL(offset == length ? JSON_DONE : JSON_NEED_INPUT,
                          status);
    }
    return hash;
}

void test_json_reader_every_chunk_size_preserves_events_and_offsets(void)
{
    const char *text = " \n{\"a\\u0050\":\"\xE2\x82\xAC\\uD83D\\uDE00\\n\\t"
        "\\\\\\\"\\/\",\"a\":[-0.125e-09,123,true,false,null,{}]}\t";
    uint32_t expected = hash_chunks(text, strlen(text));
    for (size_t count = 1U; count <= strlen(text); ++count)
    {
        TEST_ASSERT_EQUAL_HEX32(expected, hash_chunks(text, count));
    }
}

void test_json_reader_every_two_chunk_split_handles_empty_chunks(void)
{
    const char *text = "{\"abc\":[1.2e+10,\"\\ud834\\udd1e\",true]}";
    size_t length = strlen(text);
    uint32_t expected = hash_chunks(text, length);
    for (size_t split = 0U; split <= length; ++split)
    {
        uint32_t hash = 2166136261U;
        json_span_t first = {text, split};
        json_span_t second = {text + split, length - split};
        setUp();
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, first, false));
        json_status_t status = json_read(&reader, &token);
        while (status == JSON_OK)
        {
            hash = event_hash(hash, &token);
            status = json_read(&reader, &token);
        }
        TEST_ASSERT_EQUAL(JSON_NEED_INPUT, status);
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, second, true));
        status = json_read(&reader, &token);
        while (status == JSON_OK)
        {
            hash = event_hash(hash, &token);
            status = json_read(&reader, &token);
        }
        TEST_ASSERT_EQUAL(JSON_DONE, status);
        TEST_ASSERT_EQUAL_HEX32(expected, hash);
    }
}

void test_json_reader_every_truncation_is_incomplete(void)
{
    const char *text = "{\"str\":\"\\ud83d\\ude00\",\"n\":"
        "[12.3e-4,true,false,null]}";
    for (size_t count = 0U; count < strlen(text); ++count)
    {
        json_span_t input = {text, count};
        setUp();
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, input, true));
        TEST_ASSERT_EQUAL(JSON_ERROR, drain());
        TEST_ASSERT_EQUAL(JSON_ERROR_INCOMPLETE, reader.error);
        TEST_ASSERT_EQUAL(count, reader.error_offset);
    }
}

void test_json_reader_unicode_boundaries_and_embedded_zero(void)
{
    static const uint8_t expected[] = {0U, 0x7FU, 0xC2U, 0x80U,
        0xDFU, 0xBFU, 0xE0U, 0xA0U, 0x80U, 0xEDU, 0x9FU, 0xBFU,
        0xEEU, 0x80U, 0x80U, 0xEFU, 0xBFU, 0xBFU, 0xF0U, 0x90U, 0x80U,
        0x80U, 0xF4U, 0x8FU, 0xBFU, 0xBFU};
    feed("\"\\u0000\\u007f\\u0080\\u07ff\\u0800\\ud7ff\\ue000"
         "\\uffff\\ud800\\udc00\\udbff\\udfff\"");
    expect_type(JSON_STRING);
    TEST_ASSERT_EQUAL(sizeof(expected), token.text.length);
    TEST_ASSERT_EQUAL_MEMORY(expected, token.text.data, sizeof(expected));
    char output[32] = "keep";
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE,
                      json_token_copy_string(&token, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("keep", output);
}

void test_json_reader_rejects_bad_escapes_and_surrogates(void)
{
    const char *cases[] = {"\"\\q\"", "\"\\x00\"", "\"\\uZZZZ\"",
        "\"\\uDC00\"", "\"\\uDFFF\"", "\"\\uD800x\"",
        "\"\\uD800\\n\"", "\"\\uD800\\u0000\"", "\"\\uD800\\uD800\"",
        "\"\\uD800\\Udc00\"", "\"a\nb\"", "\"\x7F\x80\"",
        "\"\xC0\xAF\"", "\"\xC1\xBF\"", "\"\xC2\x20\"",
        "\"\xE0\x80\x80\"", "\"\xED\xA0\x80\"",
        "\"\xF0\x80\x80\x80\"", "\"\xF4\x90\x80\x80\"",
        "\"\xF5\x80\x80\x80\"", "\"\xFF\"", "\"\xE2\""};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        feed(cases[index]);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_ERROR, drain(), cases[index]);
    }
}

void test_json_reader_all_raw_control_characters_are_rejected(void)
{
    char text[] = {'"', 'x', '"'};
    json_span_t input = {text, sizeof(text)};
    for (uint8_t byte = 0U; byte < 32U; ++byte)
    {
        setUp();
        text[1] = (char)byte;
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, input, true));
        TEST_ASSERT_EQUAL(JSON_ERROR, drain());
        TEST_ASSERT_EQUAL(1U, reader.error_offset);
    }
}

void test_json_reader_depth_exact_limit_and_one_over(void)
{
    char text[JSON_MAX_DEPTH * 2U + 3U];
    memset(text, '[', JSON_MAX_DEPTH);
    memset(text + JSON_MAX_DEPTH, ']', JSON_MAX_DEPTH);
    text[JSON_MAX_DEPTH * 2U] = '\0';
    feed(text);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
    setUp();
    memset(text, '[', JSON_MAX_DEPTH + 1U);
    text[JSON_MAX_DEPTH + 1U] = '\0';
    feed(text);
    TEST_ASSERT_EQUAL(JSON_ERROR, drain());
    TEST_ASSERT_EQUAL(JSON_ERROR_DEPTH, reader.error);
    TEST_ASSERT_EQUAL(JSON_MAX_DEPTH, reader.error_offset);
}

void test_json_reader_token_capacity_and_canaries(void)
{
    char guarded[5] = {'L', '?', '?', '?', 'R'};
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, guarded + 1U, 3U));
    feed("\"abc\"");
    expect_type(JSON_STRING);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
    TEST_ASSERT_EQUAL_CHAR('L', guarded[0]);
    TEST_ASSERT_EQUAL_CHAR('R', guarded[4]);
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, guarded + 1U, 3U));
    feed("\"abcd\"");
    TEST_ASSERT_EQUAL(JSON_ERROR, drain());
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, reader.error);
    TEST_ASSERT_EQUAL(4U, reader.error_offset);
    TEST_ASSERT_EQUAL_CHAR('R', guarded[4]);
}

void test_json_reader_unicode_and_numbers_obey_buffer_limit(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, scratch, 2U));
    feed("\"\\u20ac\"");
    TEST_ASSERT_EQUAL(JSON_ERROR, drain());
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, reader.error);
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, scratch, 2U));
    feed("123");
    TEST_ASSERT_EQUAL(JSON_ERROR, drain());
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, reader.error);
}

void test_json_reader_zero_scratch_accepts_empty_string_and_structure(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, NULL, 0U));
    feed("{\"\":[true,null,\"\"]}");
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_errors_are_sticky_and_do_not_change_output_token(void)
{
    token.type = JSON_NULL;
    token.offset = 999U;
    feed("[1,]");
    expect_type(JSON_ARRAY_BEGIN);
    expect_type(JSON_NUMBER);
    json_token_t before = token;
    TEST_ASSERT_EQUAL(JSON_ERROR, json_read(&reader, &token));
    TEST_ASSERT_EQUAL_MEMORY(&before, &token, sizeof(token));
    TEST_ASSERT_EQUAL(3U, reader.error_offset);
    TEST_ASSERT_EQUAL(JSON_ERROR, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(&reader, span("0"), true));
    TEST_ASSERT_EQUAL(3U, reader.error_offset);
}

void test_json_reader_rejects_replacing_unconsumed_or_final_input(void)
{
    feed("[]");
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(&reader, span("0"), true));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, reader.error);
    setUp();
    feed("[]");
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(&reader, span("0"), true));
}

void test_json_reader_duplicate_keys_are_visible_in_order(void)
{
    feed("{\"x\":1,\"\\u0078\":2}");
    expect_type(JSON_OBJECT_BEGIN);
    for (size_t index = 0U; index < 2U; ++index)
    {
        expect_type(JSON_KEY);
        TEST_ASSERT_TRUE(json_token_equals(&token, "x"));
        expect_type(JSON_NUMBER);
        TEST_ASSERT_EQUAL_CHAR((char)('1' + index), token.text.data[0]);
    }
    expect_type(JSON_OBJECT_END);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_skips_nested_unknown_value_and_reads_next_field(void)
{
    feed("{\"unknown\":[{\"a\":[false,null,3]},\"x\"],\"APN\":\"internet\"}");
    expect_type(JSON_OBJECT_BEGIN);
    expect_type(JSON_KEY);
    TEST_ASSERT_EQUAL(JSON_OK, json_skip_value(&reader));
    expect_type(JSON_KEY);
    TEST_ASSERT_TRUE(json_token_equals(&token, "APN"));
    expect_type(JSON_STRING);
    TEST_ASSERT_TRUE(json_token_equals(&token, "internet"));
    expect_type(JSON_OBJECT_END);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_skip_resumes_across_every_byte(void)
{
    const char *text = "[{},[1,{\"a\":\"\\u20ac\"}],true]";
    size_t length = strlen(text);
    for (size_t index = 0U; index < length; ++index)
    {
        json_span_t part = {text + index, 1U};
        TEST_ASSERT_EQUAL(JSON_OK,
            json_reader_feed(&reader, part, index == length - 1U));
        TEST_ASSERT_EQUAL(index == length - 1U ? JSON_OK : JSON_NEED_INPUT,
                          json_skip_value(&reader));
    }
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_skip_validates_invalid_unknown_content(void)
{
    feed("[{},[1,]]");
    TEST_ASSERT_EQUAL(JSON_ERROR, json_skip_value(&reader));
    TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, reader.error);
}

void test_json_reader_string_copy_exact_capacity_and_preserves_on_failure(void)
{
    char output[] = "keep";
    feed("\"APN\"");
    expect_type(JSON_STRING);
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, json_token_copy_string(&token,
                                                               output, 3U));
    TEST_ASSERT_EQUAL_STRING("keep", output);
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_copy_string(&token,
                                                              output, 4U));
    TEST_ASSERT_EQUAL_STRING("APN", output);
    TEST_ASSERT_FALSE(json_token_equals(&token, "AP"));
    TEST_ASSERT_FALSE(json_token_equals(&token, "APNN"));
}

void test_json_reader_argument_validation(void)
{
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_init(NULL, NULL, 0U));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(NULL, span(""), true));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_read(NULL, &token));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_skip_value(NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_read(&reader, NULL));
    setUp();
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_init(&reader, NULL, 1U));
    setUp();
    json_span_t invalid = {NULL, 1U};
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(&reader, invalid, true));
    TEST_ASSERT_FALSE(json_token_equals(NULL, "x"));
    TEST_ASSERT_FALSE(json_token_equals(&token, NULL));
}

void test_json_reader_unconsumed_nonfinal_input_cannot_be_replaced(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span("[]"), false));
    expect_type(JSON_ARRAY_BEGIN);
    TEST_ASSERT_EQUAL(JSON_ERROR, json_reader_feed(&reader, span("0"), true));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, reader.error);
    TEST_ASSERT_EQUAL(1U, reader.error_offset);
}

void test_json_reader_nonfinal_complete_root_needs_final_confirmation(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span("[]"), false));
    TEST_ASSERT_EQUAL(JSON_NEED_INPUT, drain());
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span(""), false));
    TEST_ASSERT_EQUAL(JSON_NEED_INPUT, drain());
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span(""), true));
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_skip_handles_all_scalar_and_empty_container_values(void)
{
    const char *cases[] = {"0", "true", "false", "null", "\"x\"", "{}", "[]"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        feed(cases[index]);
        TEST_ASSERT_EQUAL(JSON_OK, json_skip_value(&reader));
        TEST_ASSERT_EQUAL(JSON_DONE, drain());
    }
}

void test_json_reader_cannot_switch_to_read_during_pending_skip(void)
{
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, span("["), false));
    TEST_ASSERT_EQUAL(JSON_NEED_INPUT, json_skip_value(&reader));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(JSON_ERROR_STATE, reader.error);
}

void test_json_reader_independent_contexts_can_be_interleaved(void)
{
    json_reader_t other;
    char other_scratch[16];
    json_token_t other_token;
    TEST_ASSERT_EQUAL(JSON_OK,
        json_reader_init(&other, other_scratch, sizeof(other_scratch)));
    TEST_ASSERT_EQUAL(JSON_OK,
        json_reader_feed(&reader, span("\"\\uD83D"), false));
    TEST_ASSERT_EQUAL(JSON_NEED_INPUT, json_read(&reader, &token));
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&other, span("12e"), false));
    TEST_ASSERT_EQUAL(JSON_NEED_INPUT, json_read(&other, &other_token));
    TEST_ASSERT_EQUAL(JSON_OK,
        json_reader_feed(&reader, span("\\uDE00\""), true));
    expect_type(JSON_STRING);
    TEST_ASSERT_TRUE(json_token_equals(&token, "\xF0\x9F\x98\x80"));
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&other, span("+2"), true));
    TEST_ASSERT_EQUAL(JSON_OK, json_read(&other, &other_token));
    TEST_ASSERT_EQUAL_MEMORY("12e+2", other_token.text.data, 5U);
    TEST_ASSERT_EQUAL(JSON_DONE, json_read(&other, &other_token));
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_raw_nul_is_not_end_of_input(void)
{
    char text[] = {'[', ']', '\0'};
    json_span_t input = {text, sizeof(text)};
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, input, true));
    TEST_ASSERT_EQUAL(JSON_ERROR, drain());
    TEST_ASSERT_EQUAL(2U, reader.error_offset);
}

void test_json_reader_ascii_runs_keep_unicode_and_escapes_at_every_chunk(void)
{
    const char *text = "\"abcdefghijklmnopqrstuvwxyz0123456789"
        "\xC4\xB1" "ABCDEFGHIJKLMNOPQRSTUVWXYZ\\n"
        "abcdefghijklmnopqrstuvwxyz\\u20ac0123456789\"";
    const char *expected = "abcdefghijklmnopqrstuvwxyz0123456789"
        "\xC4\xB1" "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
        "abcdefghijklmnopqrstuvwxyz\xE2\x82\xAC" "0123456789";
    feed(text);
    expect_type(JSON_STRING);
    TEST_ASSERT_EQUAL(strlen(expected), token.text.length);
    TEST_ASSERT_EQUAL_MEMORY(expected, token.text.data, token.text.length);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
    uint32_t hash = hash_chunks(text, strlen(text));
    for (size_t count = 1U; count <= strlen(text); ++count)
    {
        TEST_ASSERT_EQUAL_HEX32(hash, hash_chunks(text, count));
    }
}

void test_json_reader_ascii_run_preserves_error_offset_and_buffer_guards(void)
{
    const char *cases[] = {"\"abcdefg\"", "\"abcdef\\n\"",
        "\"abcdef\xC4\xB1\""};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        char guarded[] = "L??????R";
        TEST_ASSERT_EQUAL(JSON_OK,
            json_reader_init(&reader, guarded + 1U, 6U));
        feed(cases[index]);
        TEST_ASSERT_EQUAL(JSON_ERROR, drain());
        TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, reader.error);
        TEST_ASSERT_EQUAL(index == 1U ? 8U : 7U, reader.error_offset);
        TEST_ASSERT_EQUAL_MEMORY("LabcdefR", guarded, sizeof(guarded));
    }
}

void test_json_reader_ascii_after_utf8_lead_is_not_a_continuation(void)
{
    const char *cases[] = {"\"abcdef\xC4" "x\"",
        "\"abcdef\xC4\\n\"", "\"abcdef\xC4\""};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        setUp();
        feed(cases[index]);
        TEST_ASSERT_EQUAL(JSON_ERROR, drain());
        TEST_ASSERT_EQUAL(JSON_ERROR_UTF8, reader.error);
        TEST_ASSERT_EQUAL(8U, reader.error_offset);
    }
}

void test_json_reader_skip_large_value_needs_no_token_buffer(void)
{
    char data[2000];
    memset(data, 'a', sizeof(data));
    data[0] = '"';
    data[sizeof(data) - 1U] = '"';
    for (size_t size = 1U; size <= sizeof(data); size *= 10U)
    {
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, NULL, 0U));
        for (size_t offset = 0U; offset < sizeof(data); )
        {
            size_t length = sizeof(data) - offset;
            if (length > size)
            {
                length = size;
            }
            json_span_t part = {data + offset, length};
            offset += length;
            TEST_ASSERT_EQUAL(JSON_OK,
                json_reader_feed(&reader, part, offset == sizeof(data)));
            TEST_ASSERT_EQUAL(offset == sizeof(data) ? JSON_OK :
                              JSON_NEED_INPUT, json_skip_value(&reader));
        }
        TEST_ASSERT_EQUAL(JSON_DONE, json_read(&reader, &token));
    }
}

void test_json_reader_skip_does_not_modify_token_storage(void)
{
    char guarded[] = "L???????R";
    TEST_ASSERT_EQUAL(JSON_OK,
        json_reader_init(&reader, guarded + 1U, 7U));
    feed("{\"unknown\":{\"long_nested_key\":[\"\\u20ac\",123456789012345]},"
         "\"APN\":\"value\"}");
    expect_type(JSON_OBJECT_BEGIN);
    expect_type(JSON_KEY);
    char saved[sizeof(guarded)];
    memcpy(saved, guarded, sizeof(saved));
    TEST_ASSERT_EQUAL(JSON_OK, json_skip_value(&reader));
    TEST_ASSERT_EQUAL_MEMORY(saved, guarded, sizeof(saved));
    expect_type(JSON_KEY);
    TEST_ASSERT_TRUE(json_token_equals(&token, "APN"));
    expect_type(JSON_STRING);
    TEST_ASSERT_TRUE(json_token_equals(&token, "value"));
    expect_type(JSON_OBJECT_END);
    TEST_ASSERT_EQUAL(JSON_DONE, drain());
}

void test_json_reader_zero_buffer_skip_still_validates_content(void)
{
    const char *invalid[] = {"\"long_ascii\xC4" "x\"", "\"long\\ud800x\"",
        "[12345678901234567890,]", "{\"very_long_key\":01}", "\"long\n\""};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index)
    {
        TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, NULL, 0U));
        feed(invalid[index]);
        TEST_ASSERT_EQUAL(JSON_ERROR, json_skip_value(&reader));
        TEST_ASSERT_NOT_EQUAL(JSON_ERROR_BUFFER, reader.error);
    }
}

void test_json_reader_zero_buffer_skip_obeys_depth_limit(void)
{
    char data[JSON_MAX_DEPTH * 2U + 2U];
    memset(data, '[', JSON_MAX_DEPTH);
    memset(data + JSON_MAX_DEPTH, ']', JSON_MAX_DEPTH);
    json_span_t input = {data, JSON_MAX_DEPTH * 2U};
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, NULL, 0U));
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, input, true));
    TEST_ASSERT_EQUAL(JSON_OK, json_skip_value(&reader));
    TEST_ASSERT_EQUAL(JSON_DONE, json_read(&reader, &token));
    memset(data, '[', JSON_MAX_DEPTH + 1U);
    input.length = JSON_MAX_DEPTH + 1U;
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_init(&reader, NULL, 0U));
    TEST_ASSERT_EQUAL(JSON_OK, json_reader_feed(&reader, input, true));
    TEST_ASSERT_EQUAL(JSON_ERROR, json_skip_value(&reader));
    TEST_ASSERT_EQUAL(JSON_ERROR_DEPTH, reader.error);
    TEST_ASSERT_EQUAL(JSON_MAX_DEPTH, reader.error_offset);
}

/*** end of file ***/
