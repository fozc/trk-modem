/*
 * test_xsnprintf_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Conversion-surface edges beyond test_xsnprintf_format_scenario.c:
 * %b/%o radix conversions, zero-padding with a negative value
 * (a documented deviation from C snprintf), unsupported flags and
 * spec characters, and %b truncation.
 */
#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "xprintf.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_binary_and_octal_conversions(void)
{
    char buffer[32];

    TEST_ASSERT_EQUAL_UINT32(5U,
        xsnprintf(buffer, sizeof(buffer), "%b", 22U));
    TEST_ASSERT_EQUAL_STRING("10110", buffer);

    TEST_ASSERT_EQUAL_UINT32(8U,
        xsnprintf(buffer, sizeof(buffer), "%08b", 5U));
    TEST_ASSERT_EQUAL_STRING("00000101", buffer);

    TEST_ASSERT_EQUAL_UINT32(3U,
        xsnprintf(buffer, sizeof(buffer), "%o", 64U));
    TEST_ASSERT_EQUAL_STRING("100", buffer);

    TEST_ASSERT_EQUAL_UINT32(3U,
        xsnprintf(buffer, sizeof(buffer), "%3o", 9U));
    TEST_ASSERT_EQUAL_STRING(" 11", buffer);

    TEST_ASSERT_EQUAL_UINT32(1U,
        xsnprintf(buffer, sizeof(buffer), "%lo", 0UL));
    TEST_ASSERT_EQUAL_STRING("0", buffer);
}

void test_zero_pad_negative_prints_pad_before_sign(void)
{
    char buffer[32];

    /* Deviation from C snprintf (which gives "-0042"): the zero pad is
     * emitted before the sign. Not reachable from current production
     * formats (%02d feeds RTC date fields and sizes, never negative);
     * pinned so that any future change is an explicit decision. */
    TEST_ASSERT_EQUAL_UINT32(5U,
        xsnprintf(buffer, sizeof(buffer), "%05d", -42));
    TEST_ASSERT_EQUAL_STRING("00-42", buffer);

    TEST_ASSERT_EQUAL_UINT32(8U,
        xsnprintf(buffer, sizeof(buffer), "%08d", -42));
    TEST_ASSERT_EQUAL_STRING("00000-42", buffer);

    /* Space padding keeps the C layout. */
    TEST_ASSERT_EQUAL_UINT32(5U,
        xsnprintf(buffer, sizeof(buffer), "%5d", -42));
    TEST_ASSERT_EQUAL_STRING("  -42", buffer);
}

void test_unsupported_flags_and_specifiers_pass_through(void)
{
    char buffer[16];

    /* '#', '+' and unknown spec characters are not parsed as flags or
     * conversions: the character is emitted literally and the rest of
     * the spec degrades to literal text. */
    TEST_ASSERT_EQUAL_UINT32(2U,
        xsnprintf(buffer, sizeof(buffer), "%#x", 255U));
    TEST_ASSERT_EQUAL_STRING("#x", buffer);

    TEST_ASSERT_EQUAL_UINT32(2U,
        xsnprintf(buffer, sizeof(buffer), "%+d", 5));
    TEST_ASSERT_EQUAL_STRING("+d", buffer);

    TEST_ASSERT_EQUAL_UINT32(1U,
        xsnprintf(buffer, sizeof(buffer), "%q"));
    TEST_ASSERT_EQUAL_STRING("q", buffer);
}

void test_binary_truncation_clamps_and_terminates(void)
{
    unsigned char guard_low = 0xA5U;
    char small[4];
    unsigned char guard_high = 0xA5U;

    TEST_ASSERT_EQUAL_UINT32(3U,
        xsnprintf(small, sizeof(small), "%b", 22U));
    TEST_ASSERT_EQUAL_STRING("101", small);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, guard_low);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, guard_high);
}

/*** end of file ***/
