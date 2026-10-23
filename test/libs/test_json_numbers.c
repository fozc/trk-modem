/*
 * test_json_numbers.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Typed conversion boundaries, IEEE-754 rounding and round-trip contracts.
 */
#include "unity.h"
#include "json.h"

#include <string.h>
#include <fenv.h>

static json_token_t token;
static int original_rounding;

void setUp(void)
{
    memset(&token, 0, sizeof(token));
    token.type = JSON_NUMBER;
    original_rounding = fegetround();
}

void tearDown(void)
{
    (void)fesetround(original_rounding);
}

static void number(const char *text)
{
    token.text.data = text;
    token.text.length = strlen(text);
}

static uint32_t bits_of(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void test_json_numbers_uint32_boundaries_and_unchanged_on_error(void)
{
    uint32_t value = 17U;
    number("4294967295");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_uint32(&token, &value));
    TEST_ASSERT_EQUAL(UINT32_MAX, value);
    const char *bad[] = {"4294967296", "99999999999999999999999999999",
                         "-1", "-0", "-4294967295"};
    for (size_t index = 0U; index < sizeof(bad) / sizeof(bad[0]); ++index)
    {
        number(bad[index]);
        value = 17U;
        TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint32(&token, &value));
        TEST_ASSERT_EQUAL(17U, value);
    }
    number("0");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_uint32(&token, &value));
    TEST_ASSERT_EQUAL(0U, value);
}

void test_json_numbers_int32_boundaries_and_unchanged_on_error(void)
{
    int32_t value;
    number("-2147483648");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int32(&token, &value));
    TEST_ASSERT_EQUAL(INT32_MIN, value);
    number("2147483647");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int32(&token, &value));
    TEST_ASSERT_EQUAL(INT32_MAX, value);
    const char *bad[] = {"2147483648", "-2147483649", "4294967295",
                         "-4294967296", "999999999999999999999"};
    for (size_t index = 0U; index < sizeof(bad) / sizeof(bad[0]); ++index)
    {
        number(bad[index]);
        value = 17;
        TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_int32(&token, &value));
        TEST_ASSERT_EQUAL(17, value);
    }
    number("-0");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int32(&token, &value));
    TEST_ASSERT_EQUAL(0, value);
}

void test_json_numbers_small_unsigned_boundaries(void)
{
    uint8_t byte = 7U;
    uint16_t word = 7U;
    number("255");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_uint8(&token, &byte));
    TEST_ASSERT_EQUAL(255U, byte);
    number("256");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint8(&token, &byte));
    TEST_ASSERT_EQUAL(255U, byte);
    number("65535");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_uint16(&token, &word));
    TEST_ASSERT_EQUAL(65535U, word);
    number("65536");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint16(&token, &word));
    TEST_ASSERT_EQUAL(65535U, word);
    number("-1");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint8(&token, &byte));
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint16(&token, &word));
}

void test_json_numbers_small_signed_boundaries(void)
{
    int8_t byte = 7;
    int16_t word = 7;
    number("-128");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int8(&token, &byte));
    TEST_ASSERT_EQUAL(-128, byte);
    number("127");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int8(&token, &byte));
    TEST_ASSERT_EQUAL(127, byte);
    number("-129");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_int8(&token, &byte));
    TEST_ASSERT_EQUAL(127, byte);
    number("128");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_int8(&token, &byte));
    number("-32768");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int16(&token, &word));
    TEST_ASSERT_EQUAL(-32768, word);
    number("32767");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_int16(&token, &word));
    TEST_ASSERT_EQUAL(32767, word);
    number("32768");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_int16(&token, &word));
    number("-32769");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_int16(&token, &word));
    TEST_ASSERT_EQUAL(32767, word);
}

void test_json_numbers_integer_api_rejects_fraction_and_exponent(void)
{
    const char *cases[] = {"1.0", "1e0", "1E+2", "-0.0", "0.01"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        number(cases[index]);
        int32_t signed_value = 77;
        uint32_t unsigned_value = 77U;
        TEST_ASSERT_EQUAL(JSON_ERROR_TYPE,
            json_token_int32(&token, &signed_value));
        if (cases[index][0] != '-')
        {
            TEST_ASSERT_EQUAL(JSON_ERROR_TYPE,
                json_token_uint32(&token, &unsigned_value));
        }
        TEST_ASSERT_EQUAL(77, signed_value);
        TEST_ASSERT_EQUAL(77U, unsigned_value);
    }
}

void test_json_numbers_converters_validate_fabricated_token_syntax(void)
{
    const char *cases[] = {"", "-", "01", "-01", "+1", "1 ", " 1",
                           "NaN", "inf", "1e", "1.2.3", "0xFF", ".5"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        number(cases[index]);
        int32_t signed_value = 5;
        uint32_t unsigned_value = 5U;
        float real = 5.0F;
        TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX,
            json_token_int32(&token, &signed_value));
        TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX,
            json_token_uint32(&token, &unsigned_value));
        TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, json_token_float(&token, &real));
        TEST_ASSERT_EQUAL(5, signed_value);
        TEST_ASSERT_EQUAL(5U, unsigned_value);
        TEST_ASSERT_EQUAL_HEX32(0x40A00000U, bits_of(real));
    }
}

void test_json_numbers_float_reference_vectors_and_negative_zero(void)
{
    static const struct
    {
        const char *text;
        uint32_t bits;
    } cases[] = {
        {"0", 0U}, {"-0", 0x80000000U}, {"-0.000e99999999", 0x80000000U},
        {"0.1", 0x3DCCCCCDU}, {"1.25", 0x3FA00000U},
        {"16777217", 0x4B800000U}, {"16777219", 0x4B800002U},
        {"3.40282346638528859811704183484516925440e38", 0x7F7FFFFFU},
        {"1.17549435082228750796873653722224568e-38", 0x00800000U},
        {"1.17549421069244107548702944484928735e-38", 0x007FFFFFU},
        {"1.40129846432481707092372958328991613e-45", 0x00000001U},
        {"1.000000059604644775390625", 0x3F800000U},
        {"1.000000059604644775390626", 0x3F800001U},
        {"1.000000178813934326171875", 0x3F800002U},
        {"-1.000000059604644775390626", 0xBF800001U}};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        float value = 9.0F;
        number(cases[index].text);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_ERROR_NONE,
            json_token_float(&token, &value), cases[index].text);
        TEST_ASSERT_EQUAL_HEX32(cases[index].bits, bits_of(value));
    }
}

void test_json_numbers_float_range_errors_preserve_destination(void)
{
    const char *cases[] = {"3.4028236e38", "-3.4028236e38", "1e39", "1e99999",
        "1e-99999", "1e-100", "1e-46", "7.006492321624085e-46",
        "-1e-46", "9999999999999999999999999999999999999999"};
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        float value = 9.0F;
        number(cases[index]);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_ERROR_RANGE,
            json_token_float(&token, &value), cases[index]);
        TEST_ASSERT_EQUAL_HEX32(0x41100000U, bits_of(value));
    }
}

void test_json_numbers_float_zero_and_max_rounding_thresholds(void)
{
    float value = 0.0F;
    number("7.006492321624086e-46");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(1U, bits_of(value));
    number("340282356779733661637539395458142568447");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(0x7F7FFFFFU, bits_of(value));
    number("340282356779733661637539395458142568448");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(0x7F7FFFFFU, bits_of(value));
}

void test_json_numbers_float_bounded_significand_and_huge_zero_exponent(void)
{
    char text[JSON_FLOAT_MAX_DIGITS + 8U];
    memset(text, '9', JSON_FLOAT_MAX_DIGITS);
    memcpy(text + JSON_FLOAT_MAX_DIGITS, "e-127", 6U);
    number(text);
    float value = 3.0F;
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(0x41200000U, bits_of(value));
    text[JSON_FLOAT_MAX_DIGITS] = '9';
    memcpy(text + JSON_FLOAT_MAX_DIGITS + 1U, "e-128", 6U);
    number(text);
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(0x41200000U, bits_of(value));
    number("0e-999999999999999999999999999999999999");
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(0U, bits_of(value));
}

static void round_trip(uint32_t bits)
{
    json_writer_t writer;
    char text[48];
    float value;
    memcpy(&value, &bits, sizeof(value));
    TEST_ASSERT_EQUAL(JSON_OK, json_writer_init(&writer, text, sizeof(text)));
    TEST_ASSERT_EQUAL(JSON_OK, json_write_float(&writer, value));
    TEST_ASSERT_EQUAL(JSON_DONE, json_writer_finish(&writer));
    token.text.data = text;
    token.text.length = writer.used;
    value = 0.0F;
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(bits, bits_of(value));
}

void test_json_numbers_float_all_exponents_and_mantissa_edges_round_trip(void)
{
    const uint32_t mantissas[] = {0U, 1U, 2U, 0x3FFFFFU, 0x400000U,
                                 0x7FFFFEU, 0x7FFFFFU};
    for (uint32_t exponent = 0U; exponent < 255U; ++exponent)
    {
        for (size_t index = 0U; index < sizeof(mantissas) /
             sizeof(mantissas[0]); ++index)
        {
            uint32_t bits = (exponent << 23U) | mantissas[index];
            round_trip(bits);
            round_trip(bits | 0x80000000U);
        }
    }
}

void test_json_numbers_float_deterministic_random_round_trip(void)
{
    uint32_t state = 0x37452819U;
    for (size_t index = 0U; index < 10000U; ++index)
    {
        state = state * 1664525U + 1013904223U;
        if ((state & 0x7F800000U) != 0x7F800000U)
        {
            round_trip(state);
        }
    }
}

void test_json_numbers_float_common_output_spellings(void)
{
    const float values[] = {0.0F, 1.0F, -1.0F, 1.25F, 1000.0F,
                            0.5F, 0.0001F, 1000000000.0F};
    const char *expected[] = {"0", "1", "-1", "1.25", "1000", "0.5",
                              "9.99999975e-5", "1e9"};
    for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        json_writer_t writer;
        char output[48];
        TEST_ASSERT_EQUAL(JSON_OK,
            json_writer_init(&writer, output, sizeof(output)));
        TEST_ASSERT_EQUAL(JSON_OK, json_write_float(&writer, values[index]));
        TEST_ASSERT_EQUAL(strlen(expected[index]), writer.used);
        TEST_ASSERT_EQUAL_MEMORY(expected[index], output, writer.used);
    }
}

void test_json_numbers_bool_is_typed_and_preserved_on_error(void)
{
    bool value = true;
    number("0");
    TEST_ASSERT_EQUAL(JSON_ERROR_TYPE, json_token_bool(&token, &value));
    TEST_ASSERT_TRUE(value);
    token.type = JSON_BOOLEAN;
    token.boolean = false;
    TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_bool(&token, &value));
    TEST_ASSERT_FALSE(value);
}

void test_json_numbers_null_arguments_and_type_mismatch(void)
{
    uint32_t unsigned_value;
    int32_t signed_value;
    float real;
    bool boolean;
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT,
        json_token_uint32(NULL, &unsigned_value));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT,
        json_token_int32(NULL, &signed_value));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_float(NULL, &real));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_bool(NULL, &boolean));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_uint32(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_int32(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_uint16(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_int16(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_uint8(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_int8(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_float(&token, NULL));
    TEST_ASSERT_EQUAL(JSON_ERROR_ARGUMENT, json_token_bool(&token, NULL));
    token.type = JSON_NULL;
    TEST_ASSERT_EQUAL(JSON_ERROR_TYPE,
        json_token_uint32(&token, &unsigned_value));
    TEST_ASSERT_EQUAL(JSON_ERROR_TYPE, json_token_int32(&token, &signed_value));
    TEST_ASSERT_EQUAL(JSON_ERROR_TYPE, json_token_float(&token, &real));
}

void test_json_numbers_single_scan_keeps_syntax_error_priority(void)
{
    const char *invalid[] = {"42949672960x", "999999999999.0x", "-00",
                             "000000000000000000000000000", "1.e0"};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index)
    {
        uint32_t value = 73U;
        number(invalid[index]);
        TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX,
            json_token_uint32(&token, &value));
        TEST_ASSERT_EQUAL(73U, value);
    }
    uint32_t value = 73U;
    number("999999999999.0");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint32(&token, &value));
    number("1e0");
    TEST_ASSERT_EQUAL(JSON_ERROR_TYPE, json_token_uint32(&token, &value));
    number("-1e0");
    TEST_ASSERT_EQUAL(JSON_ERROR_RANGE, json_token_uint32(&token, &value));
    TEST_ASSERT_EQUAL(73U, value);
}

void test_json_numbers_combined_float_scan_rejects_invalid_tokens(void)
{
    const char *invalid[] = {"", "-", "+1", "01", "-01", ".1", "1.",
        "1..0", "1e", "1e+", "1e-", "e1", "-e1", "1.e1", "1e1.0",
        "1e--1", "1e+-1", "1e1x", " 1", "1 ", "1-2", "1e100000x"};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index)
    {
        float value = 73.0F;
        number(invalid[index]);
        TEST_ASSERT_EQUAL_MESSAGE(JSON_ERROR_SYNTAX,
            json_token_float(&token, &value), invalid[index]);
        TEST_ASSERT_EQUAL_HEX32(bits_of(73.0F), bits_of(value));
    }
    float value = 73.0F;
    token.text.data = NULL;
    token.text.length = 1U;
    TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(bits_of(73.0F), bits_of(value));
}

void test_json_numbers_long_float_keeps_syntax_priority_over_digit_limit(void)
{
    char data[JSON_FLOAT_MAX_DIGITS + 3U];
    memset(data, '1', sizeof(data));
    token.text.data = data;
    token.text.length = JSON_FLOAT_MAX_DIGITS + 1U;
    float value = 73.0F;
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, json_token_float(&token, &value));
    data[token.text.length - 1U] = 'x';
    TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, json_token_float(&token, &value));
    data[token.text.length - 1U] = '1';
    data[token.text.length++] = 'e';
    TEST_ASSERT_EQUAL(JSON_ERROR_SYNTAX, json_token_float(&token, &value));
    data[token.text.length++] = '1';
    TEST_ASSERT_EQUAL(JSON_ERROR_BUFFER, json_token_float(&token, &value));
    TEST_ASSERT_EQUAL_HEX32(bits_of(73.0F), bits_of(value));
}

void test_json_numbers_cutoff_accepts_last_digit_at_each_integer_limit(void)
{
    const char *values[] = {"4294967289", "4294967290", "4294967295",
        "4294967296", "2147483647", "2147483648", "2147483649",
        "-2147483648", "-2147483649"};
    const json_error_t unsigned_errors[] = {JSON_ERROR_NONE, JSON_ERROR_NONE,
        JSON_ERROR_NONE, JSON_ERROR_RANGE, JSON_ERROR_NONE, JSON_ERROR_NONE,
        JSON_ERROR_NONE, JSON_ERROR_RANGE, JSON_ERROR_RANGE};
    const json_error_t signed_errors[] = {JSON_ERROR_RANGE, JSON_ERROR_RANGE,
        JSON_ERROR_RANGE, JSON_ERROR_RANGE, JSON_ERROR_NONE, JSON_ERROR_RANGE,
        JSON_ERROR_RANGE, JSON_ERROR_NONE, JSON_ERROR_RANGE};
    for (size_t index = 0U; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        uint32_t unsigned_value = 73U;
        int32_t signed_value = 73;
        number(values[index]);
        TEST_ASSERT_EQUAL(unsigned_errors[index],
            json_token_uint32(&token, &unsigned_value));
        TEST_ASSERT_EQUAL(signed_errors[index],
            json_token_int32(&token, &signed_value));
        if (unsigned_errors[index] != JSON_ERROR_NONE)
        {
            TEST_ASSERT_EQUAL(73U, unsigned_value);
        }
        if (signed_errors[index] != JSON_ERROR_NONE)
        {
            TEST_ASSERT_EQUAL(73, signed_value);
        }
    }
}

void test_json_numbers_clinger_short_values_and_bounds(void)
{
    const char *text[] = {"-123.125", "0.016", "1e2", "16777216",
        "16777216e10", "16777216e-10", "16777217", "1e11", "1e-11",
        "-0", "-0.000", "0.0000000000"};
    const uint32_t expected[] = {0xC2F64000U, 0x3C83126FU, 0x42C80000U,
        0x4B800000U, 0x5C1502F9U, 0x3ADBE6FFU, 0x4B800000U,
        0x51BA43B7U, 0x2D2FEBFFU, 0x80000000U, 0x80000000U, 0U};
    TEST_ASSERT_EQUAL(0, fesetround(FE_TONEAREST));
    for (size_t index = 0U; index < sizeof(text) / sizeof(text[0]); ++index)
    {
        float value = 73.0F;
        number(text[index]);
        TEST_ASSERT_EQUAL(JSON_ERROR_NONE, json_token_float(&token, &value));
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected[index], bits_of(value),
                                        text[index]);
    }
}

void test_json_numbers_clinger_falls_back_for_other_rounding_modes(void)
{
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    const char *text[] = {"0.1", "-0.1", "123.125", "-0", "1e-30"};
    const uint32_t expected[] = {0x3DCCCCCDU, 0xBDCCCCCDU, 0x42F64000U,
                                 0x80000000U, 0x0DA24260U};
    for (size_t mode = 0U; mode < sizeof(modes) / sizeof(modes[0]); ++mode)
    {
        TEST_ASSERT_EQUAL(0, fesetround(modes[mode]));
        for (size_t index = 0U; index < sizeof(text) / sizeof(text[0]); ++index)
        {
            float value = 73.0F;
            number(text[index]);
            TEST_ASSERT_EQUAL(JSON_ERROR_NONE,
                              json_token_float(&token, &value));
            TEST_ASSERT_EQUAL_HEX32(expected[index], bits_of(value));
            TEST_ASSERT_EQUAL(modes[mode], fegetround());
        }
    }
}

/*** end of file ***/
