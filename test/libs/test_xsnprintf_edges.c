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
#include "xprintf.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

void test_signed_integer_boundaries_match_snprintf(void)
{
    static const int32_t values[] = {
        INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX
    };

    for (size_t index = 0U; index < (sizeof(values) / sizeof(values[0]));
         index++)
    {
        char expected[64];
        char actual[64];
        /* Each int32_t value fits both native formatter argument types. */
        const int length = snprintf(expected, sizeof(expected),
            "%d|%ld|%u", (int)values[index], (long)values[index], 17U);
        const unsigned int count = xsnprintf(actual, sizeof(actual),
            "%d|%ld|%u", (int)values[index], (long)values[index], 17U);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
        TEST_ASSERT_EQUAL_STRING(expected, actual);
        /* The complete reference output is bounded by expected's size. */
        TEST_ASSERT_EQUAL_UINT32((uint32_t)length, count);
    }
}

void test_unsigned_integer_boundaries_match_snprintf(void)
{
    static const uint32_t values[] = {
        0U, 1U, 2147483647U, 2147483648U, UINT32_MAX - 1U, UINT32_MAX
    };

    for (size_t index = 0U; index < (sizeof(values) / sizeof(values[0]));
         index++)
    {
        char expected[128];
        char actual[128];
        /* uint32_t fits the native unsigned int and unsigned long types. */
        const uint32_t value = values[index];
        const int length = snprintf(expected, sizeof(expected),
            "%u|%lu|%x|%lX|%o|%lo|%d",
            (unsigned int)value, (unsigned long)value,
            (unsigned int)value, (unsigned long)value,
            (unsigned int)value, (unsigned long)value, -17);
        const unsigned int count = xsnprintf(actual, sizeof(actual),
            "%u|%lu|%x|%lX|%o|%lo|%d",
            (unsigned int)value, (unsigned long)value,
            (unsigned int)value, (unsigned long)value,
            (unsigned int)value, (unsigned long)value, -17);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
        TEST_ASSERT_EQUAL_STRING(expected, actual);
        /* The complete reference output is bounded by expected's size. */
        TEST_ASSERT_EQUAL_UINT32((uint32_t)length, count);
    }
}

void test_native_long_limits_match_snprintf(void)
{
    char expected[128];
    char actual[128];
    const int length = snprintf(expected, sizeof(expected),
        "%ld|%lu|%ld|%lu|%u", LONG_MIN, ULONG_MAX, LONG_MAX, 0UL, 17U);
    const unsigned int count = xsnprintf(actual, sizeof(actual),
        "%ld|%lu|%ld|%lu|%u", LONG_MIN, ULONG_MAX, LONG_MAX, 0UL, 17U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
    TEST_ASSERT_EQUAL_STRING(expected, actual);
    /* The complete reference output is bounded by expected's size. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)length, count);
}

void test_binary_zero_and_long_maximum_preserve_next_argument(void)
{
    char buffer[64];
    const unsigned int count = xsnprintf(buffer, sizeof(buffer),
        "%b|%08lb|%lb:%u", 0U, 0UL, 4294967295UL, 17U);
    static const char expected[] =
        "0|00000000|11111111111111111111111111111111:17";
    TEST_ASSERT_EQUAL_STRING(expected, buffer);
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) - 1U, count);
}

void test_integer_limits_preserve_float_and_string_arguments(void)
{
    char buffer[96];
    static const char expected[] =
        "4294967295|1.25|-2147483648|ok|-2.5|17";
    const unsigned int count = xsnprintf(buffer, sizeof(buffer),
        "%lu|%.2f|%d|%s|%.1f|%u",
        4294967295UL, 1.25, -2147483647 - 1, "ok", -2.5, 17U);
    TEST_ASSERT_EQUAL_STRING(expected, buffer);
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) - 1U, count);
}

void test_integer_limits_truncate_without_touching_bytes_outside_capacity(void)
{
    static const size_t capacities[] = {0U, 1U, 2U, 10U, 11U, 12U, 64U};
    char reference[64];
    const int length = snprintf(reference, sizeof(reference),
        "%ld:%lu:%u", LONG_MIN, ULONG_MAX, 17U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(reference), (uint32_t)length);

    for (size_t index = 0U; index < (sizeof(capacities) / sizeof(capacities[0]));
         index++)
    {
        struct
        {
            char before;
            char buffer[64];
            char after;
        } guarded;
        (void)memset(&guarded, 0x55, sizeof(guarded));
        const size_t capacity = capacities[index];
        /* snprintf returned a nonnegative length within the 64-byte buffer. */
        size_t expected = (size_t)length;
        if (capacity == 0U)
        {
            expected = 0U;
        }
        else if (expected >= capacity)
        {
            expected = capacity - 1U;
        }
        else
        {
            /* The complete reference output fits. */
        }
        /* Every capacity above is at most 64, within unsigned int range. */
        const unsigned int count = xsnprintf(guarded.buffer,
            (unsigned int)capacity,
            "%ld:%lu:%u", LONG_MIN, ULONG_MAX, 17U);
        TEST_ASSERT_EQUAL_UINT32(expected, count);
        TEST_ASSERT_EQUAL_CHAR(0x55, guarded.before);
        TEST_ASSERT_EQUAL_CHAR(0x55, guarded.after);
        if (capacity > 0U)
        {
            TEST_ASSERT_EQUAL_CHAR('\0', guarded.buffer[expected]);
        }
        if (expected > 0U)
        {
            TEST_ASSERT_EQUAL_MEMORY(reference, guarded.buffer, expected);
        }
        const size_t untouched = (capacity == 0U) ? 0U : expected + 1U;
        for (size_t position = untouched; position < sizeof(guarded.buffer);
             position++)
        {
            TEST_ASSERT_EQUAL_CHAR(0x55, guarded.buffer[position]);
        }
    }
}

/*** end of file ***/
