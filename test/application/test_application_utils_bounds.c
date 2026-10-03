/*
 * test_application_utils_bounds.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise actual numeric parser outputs after explicit conversions.
 */
#include "unity.h"
#include "xprintf.h"
#include <stdio.h>
#include "../../Application/utils.c"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_integer_parser_preserves_values_above_float_exact_range(void)
{
    TEST_ASSERT_EQUAL_INT(16777217, xstrtoi("16777217"));
    TEST_ASSERT_EQUAL_INT(-16777217, xstrtoi("-16777217"));
    TEST_ASSERT_EQUAL_INT(2147483647, xstrtoi("2147483647"));
}

void test_float_parser_preserves_sign_fraction_and_integer_paths(void)
{
    TEST_ASSERT_EQUAL_FLOAT(123.25f, xstrtof("123.25"));
    TEST_ASSERT_EQUAL_FLOAT(-123.25f, xstrtof("-123.25"));
    TEST_ASSERT_EQUAL_FLOAT(9999999.0f, xstrtof("9999999"));
}

void test_hex_parser_retains_all_32_bits_and_both_letter_cases(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU,
                          ascii_hex_to_integer("DEadBEef", 8U));
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX,
                          ascii_hex_to_integer("ffffffff", 8U));
    TEST_ASSERT_EQUAL_HEX32(0U, ascii_hex_to_integer("00000000", 8U));
}
void test_hex_byte_conversion_matches_reference_for_every_byte(void)
{
    for (uint32_t value = 0U; value <= UINT8_MAX; value++)
    {
        uint8_t canvas[4] = {0xAAU, 0U, 0U, 0xBBU};
        char expected[3];
        (void)snprintf(expected, sizeof(expected), "%02X", (unsigned int)value);
        byte_to_ascii_hex((uint8_t)value, canvas + 1);
        TEST_ASSERT_EQUAL_MEMORY(expected, canvas + 1, 2U);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)value, ascii_hex_to_byte(canvas + 1));
        TEST_ASSERT_EQUAL_HEX8(0xAAU, canvas[0]);
        TEST_ASSERT_EQUAL_HEX8(0xBBU, canvas[3]);
    }
}
/*** end of file ***/
