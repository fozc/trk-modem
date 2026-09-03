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
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "xprintf.h"

static int passed = 0;
static int failed = 0;

static void check(bool cond, const char *name)
{
    if (cond) {
        printf("PASS: %s\n", name);
        passed++;
    } else {
        printf("FAIL: %s\n", name);
        failed++;
    }
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

static void test_fit_matches_snprintf(void)
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
        case 0:  ref_len = snprintf(ref, sizeof(ref), fmts[i], -1234); break;
        case 1:  ref_len = snprintf(ref, sizeof(ref), fmts[i], 70000u); break;
        case 2:  ref_len = snprintf(ref, sizeof(ref), fmts[i], "troika"); break;
        case 3:  ref_len = snprintf(ref, sizeof(ref), fmts[i], 0xbeefu); break;
        case 4:  ref_len = snprintf(ref, sizeof(ref), fmts[i], 5u); break;
        case 5:  ref_len = snprintf(ref, sizeof(ref), fmts[i], 'Z'); break;
        case 6:  ref_len = snprintf(ref, sizeof(ref), fmts[i]); break;
        case 7:  ref_len = snprintf(ref, sizeof(ref), fmts[i], "ab"); break;
        case 8:  ref_len = snprintf(ref, sizeof(ref), fmts[i], "ab"); break;
        case 9:  ref_len = snprintf(ref, sizeof(ref), fmts[i], 0x1234ul); break;
        case 10: ref_len = snprintf(ref, sizeof(ref), fmts[i], "abcdef"); break;
        default: ref_len = snprintf(ref, sizeof(ref), fmts[i], "a", 7, "b"); break;
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

static void test_exact_fit(void)
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

static void test_truncation_clamps_return(void)
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

static void test_len_zero(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 0U, "abc");

    check(ret == 0U, "len=0: return 0");
    check((c.buf[0] == 0x55) && guards_intact(&c), "len=0: nothing written");
}

static void test_len_one(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 1U, "abc");

    check((ret == 0U) && (c.buf[0] == '\0') && (c.buf[1] == 0x55),
          "len=1: only the NUL is written");
    check(guards_intact(&c), "len=1: guards intact");
}

static void test_number_truncation(void)
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
static void test_accumulation_idiom(void)
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

static void test_null_string(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, BUF_N, "[%s]", NULL);

    check((ret == 2U) && (strcmp(c.buf, "[]") == 0),
          "NULL %s maps to empty string");
    check(guards_intact(&c), "NULL %s: guards intact");
}

static void test_empty_format(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, BUF_N, "");

    check((ret == 0U) && (c.buf[0] == '\0'), "empty format: return 0, NUL only");
    check(guards_intact(&c), "empty format: guards intact");
}

static void test_interleaved_calls(void)
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

static void test_float_fit(void)
{
    char ref[64];
    char got[64];
    int ref_len = snprintf(ref, sizeof(ref), "%.3f", 3.14159);
    unsigned int got_len = xsnprintf(got, sizeof(got), "%.3f", 3.14159);

    check(((unsigned int)ref_len == got_len) && (strcmp(ref, got) == 0),
          "float fits: matches snprintf");
}

static void test_width_pad_truncation(void)
{
    canvas_t c;
    canvas_init(&c);
    unsigned int ret = xsnprintf(c.buf, 6U, "%10s", "ab"); /* "        ab" */

    check((ret == 5U) && (c.buf[5U] == '\0') && (strncmp(c.buf, "     ", 5U) == 0),
          "width pad truncation: clamped and terminated");
    check(guards_intact(&c), "width pad truncation: guards intact");
}

int main(void)
{
    test_fit_matches_snprintf();
    test_exact_fit();
    test_truncation_clamps_return();
    test_len_zero();
    test_len_one();
    test_number_truncation();
    test_accumulation_idiom();
    test_null_string();
    test_empty_format();
    test_interleaved_calls();
    test_float_fit();
    test_width_pad_truncation();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
