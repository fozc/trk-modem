/*
 * Host tests for the xscanf format contract (Y10.3/Y10.4).
 *
 * Contract under test:
 *   - bare "%s" (no size suffix) is rejected loudly with
 *     XSCANF_ERR_INVALID_FORMAT instead of silently doing nothing and
 *     shifting the va_arg order for later conversions
 *   - a truncated "%S" still assigns the fitting part and is counted,
 *     but reports XSCANF_ERR_BUFFER_OVERFLOW through
 *     xscanf_get_last_error() instead of pretending success
 *   - the production patterns (%u32 AT parsing, %S fits, %s32) are
 *     unaffected
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

/* XSCANF_ENABLE_TESTS Makefile'den -D ile gelir (gomulu test paketi acilir) */
#include "xscanf.h"

static int passed = 0;
static int failed = 0;

/* printf'in donus tipi beklenen imzayla uyusmadigi icin va_list koprusu */
static void print_shim(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
}

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

static void test_bare_s_rejected(void)
{
    int32_t v = 7;
    int ret = xscanf("12", 2, "%s", &v);

    check((ret == 0) && (xscanf_get_last_error() == XSCANF_ERR_INVALID_FORMAT),
          "bare %s is rejected loudly");
}

static void test_s32_regression(void)
{
    int32_t v = 0;
    int ret = xscanf("-42", 3, "%s32", &v);

    check((ret == 1) && (v == -42) && (xscanf_get_last_error() == XSCANF_OK),
          "%s32 still parses a signed 32-bit value");
}

static void test_S_fits(void)
{
    char buf[8] = {0};
    int ret = xscanf("hello", 5, "%7S", buf);

    check((ret == 1) && (strcmp(buf, "hello") == 0) &&
          (xscanf_get_last_error() == XSCANF_OK),
          "%S fit: assigned, no error");
}

static void test_S_truncation_reported(void)
{
    char buf[4] = {0};
    int ret = xscanf("hello", 5, "%3S", buf);

    check((ret == 1) && (strcmp(buf, "hel") == 0) &&
          (xscanf_get_last_error() == XSCANF_ERR_BUFFER_OVERFLOW),
          "%S truncation: assigned and counted, but reported");
}

static void test_u32_at_parse_regression(void)
{
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int ret = xscanf(",1,22,333,4444", 14, ",%u32,%u32,%u32,%u32", &a, &b, &c, &d);

    check((ret == 4) && (a == 1U) && (b == 22U) && (c == 333U) && (d == 4444U) &&
          (xscanf_get_last_error() == XSCANF_OK),
          "AT-style %u32 battery still parses");
}

int main(void)
{
    int embedded_failed = xscanf_run_all_tests(print_shim);
    check(embedded_failed == 0, "embedded xscanf suite passes");

    test_bare_s_rejected();
    test_s32_regression();
    test_S_fits();
    test_S_truncation_reported();
    test_u32_at_parse_regression();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
