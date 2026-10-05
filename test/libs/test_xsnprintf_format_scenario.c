/*
 * Host tests for the xsnprintf return contract (Y6.6/Y7.1).
 *
 * Contract under test:
 *   - writes never exceed the buffer and the output is NUL-terminated
 *   - the return value never exceeds len-1 (truncation-clamped), so the
 *     "pos += xsnprintf(buf + pos, size - pos, ...)" accumulation idiom
 *     can never push pos past the end or underflow size - pos
 *   - when nothing is truncated the return equals the full logical
 *     length and matches C snprintf byte for byte
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "xprintf.h"

static void check(bool condition, const char *message)
{
    TEST_ASSERT_TRUE_MESSAGE(condition, message);
}

/* Canary-guarded buffer: catches any write outside [buf, buf+len). */
#define GUARD 8U
#define BUF_N 32U

typedef struct {
    unsigned char pre[GUARD];
    char buf[BUF_N];
    unsigned char post[GUARD];
} canvas_t;

static void canvas_init(canvas_t *c)
{
    memset(c->pre, 0xAA, sizeof(c->pre));
    memset(c->buf, 0x55, sizeof(c->buf));
    memset(c->post, 0xAA, sizeof(c->post));
}

static bool guards_intact(const canvas_t *c)
{
    for (unsigned int i = 0U; i < GUARD; i++) {
        if ((c->pre[i] != 0xAAU) || (c->post[i] != 0xAAU)) {
            return false;
        }
    }
    return true;
}

void test_fit_matches_snprintf(void)
{
    static const char *fmts[] = {
        "%d", "%u", "%s", "%x", "%02X", "%c", "%%",
        "%5s|", "%-5s|", "%08lX", "%.2s", "%s-%d-%s"
    };
    bool all_ok = true;

    for (unsigned int i = 0U; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        char ref[128];
        char got[128];
        int ref_len;
        unsigned int got_len;

        switch (i) { /* one representative argument set per format */
        case 0:  ref_len = snprintf(ref, sizeof(ref), "%d", -1234); break;
        case 1:  ref_len = snprintf(ref, sizeof(ref), "%u", 70000U); break;
        case 2:  ref_len = snprintf(ref, sizeof(ref), "%s", "troika"); break;
        case 3:  ref_len = snprintf(ref, sizeof(ref), "%x", 0xbeefU); break;
        case 4:  ref_len = snprintf(ref, sizeof(ref), "%02X", 5U); break;
        case 5:  ref_len = snprintf(ref, sizeof(ref), "%c", 'Z'); break;
        case 6:  ref_len = snprintf(ref, sizeof(ref), "%%"); break;
        case 7:  ref_len = snprintf(ref, sizeof(ref), "%5s|", "ab"); break;
        case 8:  ref_len = snprintf(ref, sizeof(ref), "%-5s|", "ab"); break;
        case 9:  ref_len = snprintf(ref, sizeof(ref), "%08lX", 0x1234UL); break;
        case 10: ref_len = snprintf(ref, sizeof(ref), "%.2s", "abcdef"); break;
        default: ref_len = snprintf(ref, sizeof(ref), "%s-%d-%s",
                                    "a", 7, "b"); break;
        }

        switch (i) {
        case 0:  got_len = xsnprintf(got, sizeof(got), fmts[i], -1234); break;
        case 1:  got_len = xsnprintf(got, sizeof(got), fmts[i], 70000u); break;
        case 2:  got_len = xsnprintf(got, sizeof(got), fmts[i], "troika"); break;
        case 3:  got_len = xsnprintf(got, sizeof(got), fmts[i], 0xbeefu); break;
        case 4:  got_len = xsnprintf(got, sizeof(got), fmts[i], 5u); break;
        case 5:  got_len = xsnprintf(got, sizeof(got), fmts[i], 'Z'); break;
        case 6:  got_len = xsnprintf(got, sizeof(got), fmts[i]); break;
        case 7:  got_len = xsnprintf(got, sizeof(got), fmts[i], "ab"); break;
        case 8:  got_len = xsnprintf(got, sizeof(got), fmts[i], "ab"); break;
        case 9:  got_len = xsnprintf(got, sizeof(got), fmts[i], 0x1234ul); break;
        case 10: got_len = xsnprintf(got, sizeof(got), fmts[i], "abcdef"); break;
        default: got_len = xsnprintf(got, sizeof(got), fmts[i], "a", 7, "b"); break;
        }

        if ((ref_len < 0) || ((unsigned int)ref_len != got_len) ||
            (strcmp(ref, got) != 0)) {
            printf("  mismatch on '%s': snprintf='%s'(%d) xsnprintf='%s'(%u)\n",
                   fmts[i], ref, ref_len, got, got_len);
            all_ok = false;
        }
    }
    check(all_ok, "fits: output and return match C snprintf");
}

void test_exact_fit(void)
{
    canvas_t c;
    canvas_init(&c);
    /* 31 characters exactly fill a 32-byte buffer (31 + NUL) */
    unsigned int ret = xsnprintf(c.buf, BUF_N, "%s",
                                 "0123456789012345678901234567890");

    check((ret == 31U) && (strcmp(c.buf, "0123456789012345678901234567890") == 0),
          "exact fit: return equals length, content complete");
    check(guards_intact(&c), "exact fit: guards intact");
}

void test_truncation_clamps_return(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, BUF_N, "%s",
                                 "0123456789012345678901234567890123456789"); /* 40 */

    check(ret == (BUF_N - 1U), "truncation: return clamped to len-1");
    check(c.buf[BUF_N - 1U] == '\0', "truncation: NUL at last byte");
    check(strncmp(c.buf, "0123456789012345678901234567890123456789", BUF_N - 1U) == 0,
          "truncation: prefix preserved");
    check(guards_intact(&c), "truncation: no write outside buffer");
}

void test_len_zero(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 0U, "abc");

    check(ret == 0U, "len=0: return 0");
    check((c.buf[0] == 0x55) && guards_intact(&c), "len=0: nothing written");
}

void test_len_one(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 1U, "abc");

    check((ret == 0U) && (c.buf[0] == '\0') && (c.buf[1] == 0x55),
          "len=1: only the NUL is written");
    check(guards_intact(&c), "len=1: guards intact");
}

void test_number_truncation(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 4U, "%d", 12345678);

    check((ret == 3U) && (strcmp(c.buf, "123") == 0),
          "number truncation: clamped return and prefix");
    check(guards_intact(&c), "number truncation: guards intact");
}

/* Replicates the production idiom: pos += xsnprintf(buf+pos, size-pos, ...).
 * A long %s in the middle used to push pos past the buffer so that the
 * next size - pos underflowed; with the clamp pos must pin at size-1 and
 * every later append degrade to a safe no-op. */
void test_accumulation_idiom(void)
{
    char acc[16];
    unsigned int pos = 0U;
    bool ok = true;

    memset(acc, 0x00, sizeof(acc));

    pos += xsnprintf(acc + pos, sizeof(acc) - pos, "%s,", "head");   /* 5 */
    if (pos != 5U) { ok = false; }

    pos += xsnprintf(acc + pos, sizeof(acc) - pos, "%s",
                     "0123456789012345678901234567890123456789");    /* truncates */
    if (pos > (sizeof(acc) - 1U)) { ok = false; }                     /* pinned at 15 */

    for (unsigned int i = 0U; i < 4U; i++) {                          /* further appends */
        pos += xsnprintf(acc + pos, sizeof(acc) - pos, "%s", "TAIL");
        if (pos > (sizeof(acc) - 1U)) { ok = false; }
    }

    if (strncmp(acc, "head,0123456789", 15U) != 0) { ok = false; }
    if (acc[sizeof(acc) - 1U] != '\0') { ok = false; }

    check(ok, "accumulation idiom: pos never passes the buffer, stays terminated");
}

void test_null_string(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, BUF_N, "[%s]", NULL);

    check((ret == 2U) && (strcmp(c.buf, "[]") == 0),
          "NULL %s maps to empty string");
    check(guards_intact(&c), "NULL %s: guards intact");
}

void test_empty_format(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, BUF_N, "");

    check((ret == 0U) && (c.buf[0] == '\0'), "empty format: return 0, NUL only");
    check(guards_intact(&c), "empty format: guards intact");
}

void test_interleaved_calls(void)
{
    canvas_t a;
    canvas_t b;
    canvas_init(&a);
    canvas_init(&b);

    unsigned int ra = xsnprintf(a.buf, BUF_N, "first=%d", 11);
    unsigned int rb = xsnprintf(b.buf, BUF_N, "second=%s", "xy");
    ra += xsnprintf(a.buf + ra, BUF_N - ra, ";%u", 7u);
    rb += xsnprintf(b.buf + rb, BUF_N - rb, ";%u", 8u);

    check((strcmp(a.buf, "first=11;7") == 0) && (strcmp(b.buf, "second=xy;8") == 0),
          "interleaved calls: independent buffers, no shared state");
    check(guards_intact(&a) && guards_intact(&b), "interleaved calls: guards intact");
}

void test_float_fit(void)
{
    char ref[64];
    char got[64];
    int ref_len = snprintf(ref, sizeof(ref), "%.3f", 3.14159);
    unsigned int got_len = xsnprintf(got, sizeof(got), "%.3f", 3.14159);

    check(((unsigned int)ref_len == got_len) && (strcmp(ref, got) == 0),
          "float fits: matches snprintf");
}

/* E-notation worst case: sign(1) + digit(1) + '.'(1) + prec digits +
 * 'e'(1) + exp-sign(1) + exp(2) + NUL(1) = prec + 8 bytes. prec=24 fills
 * the 32-byte canvas exactly; the guard must reject prec >= 25 with "OV". */
void test_float_e_notation_boundaries(void)
{
    canvas_t c;
    unsigned int ret;

    /* An exact integral fixture isolates capacity from float rounding. */
    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.24e", -1.0);
    TEST_ASSERT_EQUAL_UINT32(31U, ret);
    TEST_ASSERT_EQUAL_STRING("-1.000000000000000000000000e+00", c.buf);
    check(guards_intact(&c), "e-notation prec=24: guards intact");

    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.25e", -1.0);
    TEST_ASSERT_EQUAL_UINT32(3U, ret);
    TEST_ASSERT_EQUAL_STRING("-OV", c.buf);
    check(guards_intact(&c), "e-notation prec=25: guards intact");

    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.26e", -1.0);
    TEST_ASSERT_EQUAL_UINT32(3U, ret);
    TEST_ASSERT_EQUAL_STRING("-OV", c.buf);
    check(guards_intact(&c), "e-notation prec=26: guards intact");
}

void test_float_e_notation_value_and_exponent(void)
{
    canvas_t c;
    unsigned int ret;

    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.6e", -1.5);
    TEST_ASSERT_EQUAL_UINT32(13U, ret);
    TEST_ASSERT_EQUAL_STRING("-1.500000e+00", c.buf);
    check(guards_intact(&c), "e-notation: guards intact");

    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.6E", 150.0);
    TEST_ASSERT_EQUAL_UINT32(12U, ret);
    TEST_ASSERT_EQUAL_STRING("1.500000E+02", c.buf);
    check(guards_intact(&c), "E-notation positive exponent: guards intact");

    canvas_init(&c);
    ret = xsnprintf(c.buf, BUF_N, "%.6e", 0.015);
    TEST_ASSERT_EQUAL_UINT32(12U, ret);
    TEST_ASSERT_EQUAL_STRING("1.500000e-02", c.buf);
    check(guards_intact(&c), "e-notation negative exponent: guards intact");
}

void test_width_pad_truncation(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 6U, "%10s", "ab"); /* "        ab" */

    check((ret == 5U) && (c.buf[5U] == '\0') && (strncmp(c.buf, "     ", 5U) == 0),
          "width pad truncation: clamped and terminated");
    check(guards_intact(&c), "width pad truncation: guards intact");
}

void test_long_integer_formats_used_by_arm_logs(void)
{
    char buffer[48];

    (void)xsnprintf(buffer, sizeof(buffer), "%lu %ld %08lX",
                   4294967295UL, -2147483647L, 0x1234ABUL);
    TEST_ASSERT_EQUAL_STRING("4294967295 -2147483647 001234AB", buffer);
}

void test_http_sized_accumulation_preserves_prefix_and_guards(void)
{
    static char guarded[8194];
    static const unsigned int capacities[] = {1U, 2U, 32U, 8192U};
    static const char fragment[] = "4294967295,65535,-32768;";
    const unsigned int fragment_len = (unsigned int)(sizeof(fragment) - 1U);

    for (size_t i = 0U; i < sizeof(capacities) / sizeof(capacities[0]); i++)
    {
        const unsigned int capacity = capacities[i];
        unsigned int pos = 0U;
        char *buffer = &guarded[1];
        memset(guarded, 0x55, sizeof(guarded));
        guarded[0] = (char)0x2A;
        guarded[capacity + 1U] = (char)0x2A;

        for (unsigned int field = 0U; field < 500U; field++)
        {
            pos += xsnprintf(buffer + pos, capacity - pos, "%s", fragment);
            TEST_ASSERT_LESS_THAN_UINT32(capacity, pos);
            TEST_ASSERT_EQUAL_INT((int)pos, (int)strlen(buffer));
        }
        TEST_ASSERT_EQUAL_UINT32(capacity - 1U, pos);
        TEST_ASSERT_EQUAL_CHAR('\0', buffer[pos]);
        TEST_ASSERT_EQUAL_CHAR((char)0x2A, guarded[0]);
        TEST_ASSERT_EQUAL_CHAR((char)0x2A, guarded[capacity + 1U]);
        for (unsigned int j = 0U; j < pos; j++)
        {
            TEST_ASSERT_EQUAL_CHAR(fragment[j % fragment_len], buffer[j]);
        }
    }
}

void setUp(void)
{
}

void tearDown(void)
{
}

/*** end of file ***/

void test_minimum_signed_values_do_not_overflow_negation(void)
{
    char buffer[64];
    (void)xsnprintf(buffer, sizeof(buffer), "%d", INT32_MIN);
    TEST_ASSERT_EQUAL_STRING("-2147483648", buffer);
    (void)xsnprintf(buffer, sizeof(buffer), "%lld", (long long)INT64_MIN);
    TEST_ASSERT_EQUAL_STRING("-9223372036854775808", buffer);
}

void test_negative_dynamic_width_retains_left_padding_contract(void)
{
    char buffer[16];
    (void)xsnprintf(buffer, sizeof(buffer), "%*d", -6, 123);
    TEST_ASSERT_EQUAL_STRING("123   ", buffer);
    (void)xsnprintf(buffer, sizeof(buffer), "%*d", 6, 123);
    TEST_ASSERT_EQUAL_STRING("   123", buffer);
}

void test_character_width_and_alignment_match_snprintf(void)
{
    char buffer[24];
    char expected[24];
    static const int widths[] = {0, 1, 5, -5};
    for (size_t i = 0U; i < sizeof(widths) / sizeof(widths[0]); i++)
    {
        const int length = snprintf(expected, sizeof(expected),
            "%*c:%u", widths[i], 'A', 7U);
        const unsigned int actual = xsnprintf(buffer, sizeof(buffer),
            "%*c:%u", widths[i], 'A', 7U);
        TEST_ASSERT_EQUAL_STRING(expected, buffer);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)length, actual);
    }
    (void)xsnprintf(buffer, sizeof(buffer), "%5c|%-5c", 'A', 'B');
    TEST_ASSERT_EQUAL_STRING("    A|B    ", buffer);
}

void test_character_padding_truncates_without_overwriting_guards(void)
{
    canvas_t c;
    canvas_init(&c);
    TEST_ASSERT_EQUAL_UINT32(2U, xsnprintf(c.buf, 3U, "%5c", 'A'));
    TEST_ASSERT_EQUAL_STRING("  ", c.buf);
    TEST_ASSERT_EQUAL_HEX8(0x55U, (uint8_t)c.buf[3]);
    TEST_ASSERT_TRUE(guards_intact(&c));
    TEST_ASSERT_EQUAL_UINT32(2U, xsnprintf(c.buf, 3U, "%-5c", 'A'));
    TEST_ASSERT_EQUAL_STRING("A ", c.buf);
    TEST_ASSERT_TRUE(guards_intact(&c));
}

void test_character_nul_padding_preserves_output_count(void)
{
    char buffer[8];
    static const char expected[] = {' ', ' ', '\0', '\0'};
    TEST_ASSERT_EQUAL_UINT32(3U,
        xsnprintf(buffer, sizeof(buffer), "%3c", 0));
    TEST_ASSERT_EQUAL_MEMORY(expected, buffer, sizeof(expected));
}
