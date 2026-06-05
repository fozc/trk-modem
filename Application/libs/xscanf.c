/*-----------------------------------------------------------------------*/
/* xscanf - Lightweight Safe Scanner for Embedded Systems                */
/*-----------------------------------------------------------------------*/
/* Copyright (c) 2024                                                     */
/* Implementation file                                                   */
/*-----------------------------------------------------------------------*/

#include "xscanf.h"
#include <stdarg.h>
#include <string.h>
#include <limits.h>

/*-----------------------------------------------------------------------*/
/* Static Variables                                                      */
/*-----------------------------------------------------------------------*/
static xscanf_err_t s_last_error = XSCANF_OK;

/*-----------------------------------------------------------------------*/
/* Type Limits                                                           */
/*-----------------------------------------------------------------------*/
#define U8_MAX_VAL    255U
#define S8_MAX_VAL    127
#define S8_MIN_VAL    (-128)
#define U16_MAX_VAL   65535U
#define S16_MAX_VAL   32767
#define S16_MIN_VAL   (-32768)
#define U32_MAX_VAL   4294967295UL
#define S32_MAX_VAL   2147483647L
#define S32_MIN_VAL   (-2147483647L - 1L)

/*-----------------------------------------------------------------------*/
/* Maximum Digit Counts (for early termination optimization)             */
/*-----------------------------------------------------------------------*/
#define U8_MAX_DIGITS   3   /* 255 */
#define S8_MAX_DIGITS   3   /* 127, -128 */
#define U16_MAX_DIGITS  5   /* 65535 */
#define S16_MAX_DIGITS  5   /* 32767, -32768 */
#define U32_MAX_DIGITS  10  /* 4294967295 */
#define S32_MAX_DIGITS  10  /* 2147483647, -2147483648 */
#define HEX_MAX_DIGITS  8   /* FFFFFFFF */

/*-----------------------------------------------------------------------*/
/* Helper: Effective width (min of user width and type max)              */
/*-----------------------------------------------------------------------*/
#define EFF_WIDTH(user_width, type_max) \
    ((user_width) > 0 ? (((user_width) < (type_max)) ? (user_width) : (type_max)) : (type_max))

/*-----------------------------------------------------------------------*/
/* Internal: Skip whitespace                                             */
/*-----------------------------------------------------------------------*/
static const char* skip_whitespace(const char *s, const char *end)
{
    while (s < end && xscanf_isspace(*s)) {
        s++;
    }
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse unsigned integer with overflow check                  */
/*-----------------------------------------------------------------------*/
static const char* parse_unsigned(const char *s, const char *end, 
                                   uint32_t *out, uint32_t max_val,
                                   int max_digits, xscanf_err_t *err)
{
    uint32_t val = 0;
    int digits = 0;
    const uint32_t overflow_threshold = max_val / 10;
    const uint32_t overflow_digit = max_val % 10;
    
    *err = XSCANF_OK;
    
    /* Skip leading whitespace */
    s = skip_whitespace(s, end);
    
    if (s >= end || !xscanf_isdigit(*s)) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    while (s < end && xscanf_isdigit(*s)) {
        if (max_digits > 0 && digits >= max_digits) {
            break;
        }
        
        uint32_t digit = (uint32_t)(*s - '0');
        
        /* Overflow check */
        if (val > overflow_threshold || 
            (val == overflow_threshold && digit > overflow_digit)) {
            *err = XSCANF_ERR_OVERFLOW;
            /* Continue parsing to consume the number */
            while (s < end && xscanf_isdigit(*s)) {
                s++;
                digits++;
                if (max_digits > 0 && digits >= max_digits) break;
            }
            return s;
        }
        
        val = val * 10 + digit;
        s++;
        digits++;
    }
    
    *out = val;
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse signed integer with overflow check                    */
/*-----------------------------------------------------------------------*/
static const char* parse_signed(const char *s, const char *end,
                                 int32_t *out, int32_t min_val, int32_t max_val,
                                 int max_digits, xscanf_err_t *err)
{
    bool negative = false;
    uint32_t abs_val = 0;
    
    *err = XSCANF_OK;
    
    /* Skip leading whitespace */
    s = skip_whitespace(s, end);
    
    if (s >= end) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    /* Check sign */
    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    if (s >= end || !xscanf_isdigit(*s)) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    /* Calculate max absolute value */
    uint32_t abs_max = negative ? (uint32_t)(-(min_val + 1)) + 1 : (uint32_t)max_val;
    
    s = parse_unsigned(s, end, &abs_val, abs_max, max_digits, err);
    
    if (*err == XSCANF_ERR_OVERFLOW) {
        *err = negative ? XSCANF_ERR_UNDERFLOW : XSCANF_ERR_OVERFLOW;
        return s;
    }
    
    if (*err == XSCANF_OK) {
        *out = negative ? -(int32_t)abs_val : (int32_t)abs_val;
    }
    
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse hexadecimal with overflow check                       */
/*-----------------------------------------------------------------------*/
static const char* parse_hex(const char *s, const char *end,
                              uint32_t *out, int max_digits, xscanf_err_t *err)
{
    uint32_t val = 0;
    int digits = 0;
    
    *err = XSCANF_OK;
    
    /* Skip leading whitespace */
    s = skip_whitespace(s, end);
    
    if (s >= end) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    /* Skip optional 0x or 0X prefix */
    if (s + 1 < end && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    if (s >= end || !xscanf_isxdigit(*s)) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    while (s < end && xscanf_isxdigit(*s)) {
        if (max_digits > 0 && digits >= max_digits) {
            break;
        }
        
        uint8_t digit = xscanf_hexval(*s);
        
        /* Overflow check: if top 4 bits are set, shifting left by 4 would overflow */
        /* 0x0FFFFFFF is the max value that can be shifted left by 4 without overflow */
        /* (0x0FFFFFFF << 4) | 0xF = 0xFFFFFFFF (max uint32_t) */
        if (val > 0x0FFFFFFFU) {
            *err = XSCANF_ERR_OVERFLOW;
            while (s < end && xscanf_isxdigit(*s)) {
                s++;
                digits++;
                if (max_digits > 0 && digits >= max_digits) break;
            }
            return s;
        }
        
        val = (val << 4) | digit;
        s++;
        digits++;
    }
    
    *out = val;
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse string with length limit                              */
/*-----------------------------------------------------------------------*/
static const char* parse_string(const char *s, const char *end,
                                 char *out, size_t out_size, 
                                 char delimiter, xscanf_err_t *err)
{
    size_t i = 0;
    
    *err = XSCANF_OK;
    
    if (out_size == 0) {
        *err = XSCANF_ERR_BUFFER_OVERFLOW;
        return s;
    }
    
    /* Skip leading whitespace (unless delimiter is a quote) */
    if (delimiter != '"' && delimiter != '\'') {
        s = skip_whitespace(s, end);
    }
    
    while (s < end && *s != '\0') {
        /* Stop at delimiter or whitespace (if no specific delimiter) */
        if (delimiter != '\0') {
            if (*s == delimiter) break;
        } else {
            if (xscanf_isspace(*s)) break;
        }
        
        if (i >= out_size - 1) {
            *err = XSCANF_ERR_BUFFER_OVERFLOW;
            /* Null terminate what we have */
            out[out_size - 1] = '\0';
            /* Skip remaining characters until delimiter */
            while (s < end && *s != '\0') {
                if (delimiter != '\0' && *s == delimiter) break;
                if (delimiter == '\0' && xscanf_isspace(*s)) break;
                s++;
            }
            return s;
        }
        
        out[i++] = *s++;
    }
    
    out[i] = '\0';
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse raw string (all chars including whitespace)           */
/*-----------------------------------------------------------------------*/
static const char* parse_raw_string(const char *s, const char *end,
                                     char *out, size_t out_size, 
                                     size_t width, xscanf_err_t *err)
{
    size_t i = 0;
    
    *err = XSCANF_OK;
    
    if (out_size == 0) {
        *err = XSCANF_ERR_BUFFER_OVERFLOW;
        return s;
    }
    
    /* Read exactly 'width' characters (or until end/null) */
    size_t max_read = (width > 0 && width < out_size) ? width : (out_size - 1);
    
    while (s < end && *s != '\0' && i < max_read) {
        out[i++] = *s++;
    }
    
    out[i] = '\0';
    
    /* If width specified but couldn't read enough, that's still OK */
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse character                                             */
/*-----------------------------------------------------------------------*/
static const char* parse_char(const char *s, const char *end,
                               char *out, xscanf_err_t *err)
{
    *err = XSCANF_OK;
    
    if (s >= end || *s == '\0') {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    *out = *s++;
    return s;
}

/*-----------------------------------------------------------------------*/
/* Internal: Parse width from format string                              */
/*-----------------------------------------------------------------------*/
static const char* parse_width(const char *fmt, int *width)
{
    *width = 0;
    
    while (xscanf_isdigit(*fmt)) {
        *width = (*width * 10) + (*fmt - '0');
        fmt++;
    }
    
    return fmt;
}

/*-----------------------------------------------------------------------*/
/* Internal: Match literal character in format                           */
/*-----------------------------------------------------------------------*/
static const char* match_literal(const char *s, const char *end, 
                                  char expected, xscanf_err_t *err)
{
    *err = XSCANF_OK;
    
    if (s >= end || *s != expected) {
        *err = XSCANF_ERR_NO_MATCH;
        return s;
    }
    
    return s + 1;
}

/* Forward declaration of va_list implementation */
static int xscanf_impl(xscanf_result_t *result, const char *input, size_t input_len,
                        const char *format, va_list args);

/*-----------------------------------------------------------------------*/
/* Main Implementation: xscanf                                           */
/*-----------------------------------------------------------------------*/
int xscanf(const char *input, size_t input_len, const char *format, ...)
{
    va_list args;
    int count;
    
    va_start(args, format);
    count = xscanf_impl(NULL, input, input_len, format, args);
    va_end(args);
    
    return count;
}

/*-----------------------------------------------------------------------*/
/* Internal: va_list based implementation                                */
/*-----------------------------------------------------------------------*/
static int xscanf_impl(xscanf_result_t *result, const char *input, size_t input_len,
                        const char *format, va_list args)
{
    const char *s = input;
    const char *end = input + input_len;
    const char *fmt = format;
    int count = 0;
    xscanf_err_t err = XSCANF_OK;
    
    /* Initialize result */
    xscanf_result_t local_result = {0, XSCANF_OK, 0};
    
    /* NULL checks */
    if (input == NULL || format == NULL) {
        s_last_error = XSCANF_ERR_NULL_PTR;
        if (result) {
            result->error = XSCANF_ERR_NULL_PTR;
            result->count = -1;
        }
        return -1;
    }
    
    /* Empty input check */
    if (input_len == 0 || *input == '\0') {
        s_last_error = XSCANF_ERR_EMPTY_INPUT;
        if (result) {
            result->error = XSCANF_ERR_EMPTY_INPUT;
            result->count = 0;
        }
        return 0;
    }
    
    while (*fmt != '\0' && s < end && *s != '\0') {
        /* Skip whitespace in format */
        if (xscanf_isspace(*fmt)) {
            fmt++;
            s = skip_whitespace(s, end);
            continue;
        }
        
        /* Format specifier */
        if (*fmt == '%') {
            fmt++;
            
            /* Literal % */
            if (*fmt == '%') {
                s = match_literal(s, end, '%', &err);
                if (err != XSCANF_OK) break;
                fmt++;
                continue;
            }
            
            /* Skip flag */
            bool skip = false;
            if (*fmt == '*') {
                skip = true;
                fmt++;
            }
            
            /* Width */
            int width = 0;
            fmt = parse_width(fmt, &width);
            
            /* Type specifier */
            char type = *fmt++;
            int subtype = 0;  /* Changed to int for numeric comparison */
            
            /* Check for extended types (u8, s8, u16, s16, u32, s32) */
            if ((type == 'u' || type == 's') && xscanf_isdigit(*fmt)) {
                subtype = (*fmt++) - '0';  /* First digit: 8, 1, 3 */
                if (xscanf_isdigit(*fmt)) {
                    /* Second digit for 16, 32 */
                    subtype = subtype * 10 + (*fmt++ - '0');
                }
            }
            
            /* Parse based on type */
            switch (type) {
                case 'd': {
                    /* Signed int - use min of user width and S32 max digits */
                    int eff_width = EFF_WIDTH(width, S32_MAX_DIGITS);
                    int32_t val;
                    s = parse_signed(s, end, &val, INT_MIN, INT_MAX, eff_width, &err);
                    if (err == XSCANF_OK && !skip) {
                        int *out = va_arg(args, int*);
                        if (out) *out = (int)val;
                        count++;
                    } else if (skip && err == XSCANF_OK) {
                        /* Consume va_arg even when skipping */
                    }
                    break;
                }
                
                case 'u': {
                    if (subtype == 8) {
                        /* uint8_t - max 3 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, U8_MAX_DIGITS);
                        uint32_t val;
                        s = parse_unsigned(s, end, &val, U8_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            uint8_t *out = va_arg(args, uint8_t*);
                            if (out) *out = (uint8_t)val;
                            count++;
                        }
                    } else if (subtype == 16) {
                        /* uint16_t - max 5 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, U16_MAX_DIGITS);
                        uint32_t val;
                        s = parse_unsigned(s, end, &val, U16_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            uint16_t *out = va_arg(args, uint16_t*);
                            if (out) *out = (uint16_t)val;
                            count++;
                        }
                    } else if (subtype == 32) {
                        /* uint32_t - max 10 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, U32_MAX_DIGITS);
                        uint32_t val;
                        s = parse_unsigned(s, end, &val, U32_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            uint32_t *out = va_arg(args, uint32_t*);
                            if (out) *out = val;
                            count++;
                        }
                    } else {
                        /* unsigned int - max 10 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, U32_MAX_DIGITS);
                        uint32_t val;
                        s = parse_unsigned(s, end, &val, UINT_MAX, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            unsigned int *out = va_arg(args, unsigned int*);
                            if (out) *out = (unsigned int)val;
                            count++;
                        }
                    }
                    break;
                }
                
                case 's': {
                    if (subtype == 8) {
                        /* int8_t - max 3 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, S8_MAX_DIGITS);
                        int32_t val;
                        s = parse_signed(s, end, &val, S8_MIN_VAL, S8_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            int8_t *out = va_arg(args, int8_t*);
                            if (out) *out = (int8_t)val;
                            count++;
                        }
                    } else if (subtype == 16) {
                        /* int16_t - max 5 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, S16_MAX_DIGITS);
                        int32_t val;
                        s = parse_signed(s, end, &val, S16_MIN_VAL, S16_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            int16_t *out = va_arg(args, int16_t*);
                            if (out) *out = (int16_t)val;
                            count++;
                        }
                    } else if (subtype == 32) {
                        /* int32_t - max 10 digits, user width capped */
                        int eff_width = EFF_WIDTH(width, S32_MAX_DIGITS);
                        int32_t val;
                        s = parse_signed(s, end, &val, S32_MIN_VAL, S32_MAX_VAL, eff_width, &err);
                        if (err == XSCANF_OK && !skip) {
                            int32_t *out = va_arg(args, int32_t*);
                            if (out) *out = val;
                            count++;
                        }
                    }
                    break;
                }
                
                case 'x':
                case 'X': {
                    /* Hexadecimal - max 8 hex digits, user width capped */
                    int eff_width = EFF_WIDTH(width, HEX_MAX_DIGITS);
                    uint32_t val;
                    s = parse_hex(s, end, &val, eff_width, &err);
                    if (err == XSCANF_OK && !skip) {
                        uint32_t *out = va_arg(args, uint32_t*);
                        if (out) *out = val;
                        count++;
                    }
                    break;
                }
                
                case 'c': {
                    /* Single character */
                    char val;
                    s = parse_char(s, end, &val, &err);
                    if (err == XSCANF_OK && !skip) {
                        char *out = va_arg(args, char*);
                        if (out) *out = val;
                        count++;
                    }
                    break;
                }
                
                case 'S': {
                    /* String with length */
                    if (width == 0) {
                        width = XSCANF_MAX_WIDTH;  /* Default max */
                    }
                    
                    /* Check for delimiter (next char in format) */
                    char delimiter = '\0';
                    if (*fmt != '\0' && !xscanf_isspace(*fmt) && *fmt != '%') {
                        delimiter = *fmt;
                    }
                    
                    if (!skip) {
                        char *out = va_arg(args, char*);
                        if (out) {
                            s = parse_string(s, end, out, (size_t)(width + 1), delimiter, &err);
                            if (err == XSCANF_OK || err == XSCANF_ERR_BUFFER_OVERFLOW) {
                                count++;
                                if (err == XSCANF_ERR_BUFFER_OVERFLOW) {
                                    /* Still count as success but record error */
                                    err = XSCANF_OK;
                                }
                            }
                        }
                    } else {
                        /* Skip string */
                        char dummy[XSCANF_MAX_WIDTH + 1];
                        s = parse_string(s, end, dummy, sizeof(dummy), delimiter, &err);
                    }
                    break;
                }
                
                case 'r':
                case 'R': {
                    /* Raw string - read all characters including whitespace */
                    /* Width is REQUIRED for %r - if not specified, do nothing */
                    if (width == 0) {
                        /* No width specified - skip this specifier, don't consume va_arg */
                        err = XSCANF_ERR_INVALID_FORMAT;
                        break;
                    }
                    
                    if (!skip) {
                        char *out = va_arg(args, char*);
                        if (out) {
                            s = parse_raw_string(s, end, out, (size_t)(width + 1), width, &err);
                            count++;
                        }
                    } else {
                        /* Skip raw string */
                        char dummy[XSCANF_MAX_WIDTH + 1];
                        s = parse_raw_string(s, end, dummy, sizeof(dummy), width, &err);
                    }
                    break;
                }
                
                default:
                    err = XSCANF_ERR_INVALID_FORMAT;
                    break;
            }
            
            if (err != XSCANF_OK) {
                break;
            }
        } else {
            /* Literal character match */
            s = match_literal(s, end, *fmt, &err);
            if (err != XSCANF_OK) break;
            fmt++;
        }
    }
    
    /* Store result */
    local_result.count = count;
    local_result.error = err;
    local_result.input_pos = (size_t)(s - input);
    
    s_last_error = err;
    
    if (result) {
        *result = local_result;
    }
    
    return count;
}

/*-----------------------------------------------------------------------*/
/* Extended xscanf with result structure                                 */
/*-----------------------------------------------------------------------*/
int xscanf_ex(xscanf_result_t *result, const char *input, size_t input_len,
              const char *format, ...)
{
    va_list args;
    int count;
    
    va_start(args, format);
    count = xscanf_impl(result, input, input_len, format, args);
    va_end(args);
    
    return count;
}

/*-----------------------------------------------------------------------*/
/* Get last error                                                        */
/*-----------------------------------------------------------------------*/
xscanf_err_t xscanf_get_last_error(void)
{
    return s_last_error;
}

/*-----------------------------------------------------------------------*/
/* Get error string                                                      */
/*-----------------------------------------------------------------------*/
const char* xscanf_error_str(xscanf_err_t err)
{
    static const char* const error_strings[] = {
        "OK",
        "NULL pointer",
        "Integer overflow",
        "Integer underflow",
        "Invalid format",
        "No match",
        "Buffer overflow",
        "Empty input"
    };
    
    if (err < sizeof(error_strings) / sizeof(error_strings[0])) {
        return error_strings[err];
    }
    return "Unknown error";
}

/*-----------------------------------------------------------------------*/
/* Test Implementation                                                   */
/*-----------------------------------------------------------------------*/
#ifdef XSCANF_ENABLE_TESTS

/* Test helper macro */
#define XSCANF_TEST(name, cond) do { \
    total++; \
    if (!(cond)) { \
        failed++; \
        print("[FAIL] %s\n", name); \
    } else { \
        print("[PASS] %s\n", name); \
    } \
} while(0)

int xscanf_run_all_tests(void (*print_func)(const char *fmt, ...))
{
    int total = 0;
    int failed = 0;
    
    /* Use provided print function or a no-op */
    void (*print)(const char*, ...) = print_func;
    if (!print) return -1;
    
    print("\n=== xscanf Test Suite ===\n\n");
    
    /*-------------------------------------------------------------------*/
    /* Basic Integer Tests                                               */
    /*-------------------------------------------------------------------*/
    print("--- Basic Integer Tests ---\n");
    
    {
        int val = 0;
        int ret = xscanf("42", 3, "%d", &val);
        XSCANF_TEST("Parse positive int", ret == 1 && val == 42);
    }
    
    {
        int val = 0;
        int ret = xscanf("-123", 5, "%d", &val);
        XSCANF_TEST("Parse negative int", ret == 1 && val == -123);
    }
    
    {
        int val = 0;
        int ret = xscanf("  456", 6, "%d", &val);
        XSCANF_TEST("Parse int with leading whitespace", ret == 1 && val == 456);
    }
    
    {
        int val = 0;
        int ret = xscanf("+789", 5, "%d", &val);
        XSCANF_TEST("Parse int with plus sign", ret == 1 && val == 789);
    }
    
    /*-------------------------------------------------------------------*/
    /* Unsigned Integer Tests                                            */
    /*-------------------------------------------------------------------*/
    print("\n--- Unsigned Integer Tests ---\n");
    
    {
        unsigned int val = 0;
        int ret = xscanf("65535", 6, "%u", &val);
        XSCANF_TEST("Parse unsigned int", ret == 1 && val == 65535);
    }
    
    /*-------------------------------------------------------------------*/
    /* Fixed-Width Integer Tests                                         */
    /*-------------------------------------------------------------------*/
    print("\n--- Fixed-Width Integer Tests ---\n");
    
    {
        uint8_t val = 0;
        int ret = xscanf("200", 4, "%u8", &val);
        XSCANF_TEST("Parse uint8_t", ret == 1 && val == 200);
    }
    
    {
        uint8_t val = 0;
        int ret = xscanf("256", 4, "%u8", &val);
        XSCANF_TEST("uint8_t overflow detection", ret == 0);
    }
    
    {
        int8_t val = 0;
        int ret = xscanf("-100", 5, "%s8", &val);
        XSCANF_TEST("Parse int8_t negative", ret == 1 && val == -100);
    }
    
    {
        int8_t val = 0;
        int ret = xscanf("127", 4, "%s8", &val);
        XSCANF_TEST("Parse int8_t max", ret == 1 && val == 127);
    }
    
    {
        int8_t val = 0;
        int ret = xscanf("-128", 5, "%s8", &val);
        XSCANF_TEST("Parse int8_t min", ret == 1 && val == -128);
    }
    
    {
        int8_t val = 0;
        int ret = xscanf("128", 4, "%s8", &val);
        XSCANF_TEST("int8_t overflow detection", ret == 0);
    }
    
    {
        uint16_t val = 0;
        int ret = xscanf("65535", 6, "%u16", &val);
        XSCANF_TEST("Parse uint16_t max", ret == 1 && val == 65535);
    }
    
    {
        uint16_t val = 0;
        int ret = xscanf("65536", 6, "%u16", &val);
        XSCANF_TEST("uint16_t overflow detection", ret == 0);
    }
    
    {
        int16_t val = 0;
        int ret = xscanf("-32768", 7, "%s16", &val);
        XSCANF_TEST("Parse int16_t min", ret == 1 && val == -32768);
    }
    
    {
        uint32_t val = 0;
        int ret = xscanf("4294967295", 11, "%u32", &val);
        XSCANF_TEST("Parse uint32_t max", ret == 1 && val == 4294967295UL);
    }
    
    {
        int32_t val = 0;
        int ret = xscanf("-2147483648", 12, "%s32", &val);
        XSCANF_TEST("Parse int32_t min", ret == 1 && val == -2147483647L - 1L);
    }
    
    /*-------------------------------------------------------------------*/
    /* Hexadecimal Tests                                                 */
    /*-------------------------------------------------------------------*/
    print("\n--- Hexadecimal Tests ---\n");
    
    {
        uint32_t val = 0;
        int ret = xscanf("0xFF", 5, "%x", &val);
        XSCANF_TEST("Parse hex with 0x prefix", ret == 1 && val == 0xFF);
    }
    
    {
        uint32_t val = 0;
        int ret = xscanf("0XAB", 5, "%x", &val);
        XSCANF_TEST("Parse hex with 0X prefix", ret == 1 && val == 0xAB);
    }
    
    {
        uint32_t val = 0;
        int ret = xscanf("DEADBEEF", 9, "%x", &val);
        XSCANF_TEST("Parse hex without prefix", ret == 1 && val == 0xDEADBEEF);
    }
    
    {
        uint32_t val = 0;
        int ret = xscanf("abcd", 5, "%x", &val);
        XSCANF_TEST("Parse lowercase hex", ret == 1 && val == 0xABCD);
    }
    
    {
        uint32_t val = 0;
        int ret = xscanf("12", 3, "%2x", &val);
        XSCANF_TEST("Parse hex with width limit", ret == 1 && val == 0x12);
    }
    
    /*-------------------------------------------------------------------*/
    /* String Tests                                                      */
    /*-------------------------------------------------------------------*/
    print("\n--- String Tests ---\n");
    
    {
        char buf[16] = {0};
        int ret = xscanf("Hello", 6, "%15S", buf);
        XSCANF_TEST("Parse simple string", ret == 1 && strcmp(buf, "Hello") == 0);
    }
    
    {
        char buf[8] = {0};
        int ret = xscanf("VeryLongString", 15, "%7S", buf);
        XSCANF_TEST("String truncation", ret == 1 && strlen(buf) == 7);
    }
    
    {
        char buf[16] = {0};
        int ret = xscanf("\"Quoted\"", 9, "\"%15S\"", buf);
        XSCANF_TEST("Parse quoted string", ret == 1 && strcmp(buf, "Quoted") == 0);
    }
    
    /*-------------------------------------------------------------------*/
    /* Character Tests                                                   */
    /*-------------------------------------------------------------------*/
    print("\n--- Character Tests ---\n");
    
    {
        char c = 0;
        int ret = xscanf("X", 2, "%c", &c);
        XSCANF_TEST("Parse single char", ret == 1 && c == 'X');
    }
    
    /*-------------------------------------------------------------------*/
    /* Skip Tests                                                        */
    /*-------------------------------------------------------------------*/
    print("\n--- Skip Tests ---\n");
    
    {
        int val = 0;
        int ret = xscanf("123 456", 8, "%*d %d", &val);
        XSCANF_TEST("Skip first int", ret == 1 && val == 456);
    }
    
    /*-------------------------------------------------------------------*/
    /* Format Literal Tests                                              */
    /*-------------------------------------------------------------------*/
    print("\n--- Format Literal Tests ---\n");
    
    {
        int a = 0, b = 0;
        int ret = xscanf("Value: 10, 20", 14, "Value: %d, %d", &a, &b);
        XSCANF_TEST("Parse with literal text", ret == 2 && a == 10 && b == 20);
    }
    
    {
        int val = 0;
        int ret = xscanf("100%", 5, "%d%%", &val);
        XSCANF_TEST("Parse with literal percent", ret == 1 && val == 100);
    }
    
    /*-------------------------------------------------------------------*/
    /* AT Command Response Tests                                         */
    /*-------------------------------------------------------------------*/
    print("\n--- AT Command Response Tests ---\n");
    
    {
        uint8_t rssi = 0, ber = 0;
        int ret = xscanf("+CSQ: 18,0", 11, "+CSQ: %u8,%u8", &rssi, &ber);
        XSCANF_TEST("Parse +CSQ response", ret == 2 && rssi == 18 && ber == 0);
    }
    
    {
        int n = 0, stat = 0;
        int ret = xscanf("+CREG: 0,1", 11, "+CREG: %d,%d", &n, &stat);
        XSCANF_TEST("Parse +CREG response", ret == 2 && n == 0 && stat == 1);
    }
    
    {
        int mode = 0, format = 0;
        char oper[32] = {0};
        int ret = xscanf("+COPS: 0,0,\"Turkcell\"", 22, "+COPS: %d,%d,\"%31S\"", &mode, &format, oper);
        XSCANF_TEST("Parse +COPS response", ret == 3 && strcmp(oper, "Turkcell") == 0);
    }
    
    {
        int cid = 0, state = 0;
        int ret = xscanf("#SGACT: 1,1", 12, "#SGACT: %d,%d", &cid, &state);
        XSCANF_TEST("Parse #SGACT response", ret == 2 && cid == 1 && state == 1);
    }
    
    {
        int rxlev = 0, ber = 0, rscp = 0, ecno = 0, rsrq = 0, rsrp = 0;
        int ret = xscanf("+CESQ: 99,99,255,255,20,45", 27, 
                         "+CESQ: %d,%d,%d,%d,%d,%d", 
                         &rxlev, &ber, &rscp, &ecno, &rsrq, &rsrp);
        XSCANF_TEST("Parse +CESQ response", ret == 6 && rsrq == 20 && rsrp == 45);
    }
    
    /*-------------------------------------------------------------------*/
    /* Edge Case Tests                                                   */
    /*-------------------------------------------------------------------*/
    print("\n--- Edge Case Tests ---\n");
    
    {
        int ret = xscanf(NULL, 0, "%d");
        XSCANF_TEST("NULL input returns -1", ret == -1);
    }
    
    {
        int val = 0;
        int ret = xscanf("", 0, "%d", &val);
        XSCANF_TEST("Empty input returns 0", ret == 0);
    }
    
    {
        int val = 0;
        int ret = xscanf("abc", 4, "%d", &val);
        XSCANF_TEST("Non-numeric input", ret == 0);
    }
    
    {
        int val = 42;
        int ret = xscanf("NoMatch", 8, "Match: %d", &val);
        XSCANF_TEST("Format mismatch", ret == 0 && val == 42);
    }
    
    {
        int val = 0;
        int ret = xscanf("12345", 6, "%3d", &val);
        XSCANF_TEST("Width limit (3 digits)", ret == 1 && val == 123);
    }
    
    {
        uint8_t val = 0;
        int ret = xscanf("0", 2, "%u8", &val);
        XSCANF_TEST("Parse zero", ret == 1 && val == 0);
    }
    
    {
        int val = 0;
        int ret = xscanf("007", 4, "%d", &val);
        XSCANF_TEST("Parse with leading zeros", ret == 1 && val == 7);
    }
    
    /*-------------------------------------------------------------------*/
    /* Summary                                                           */
    /*-------------------------------------------------------------------*/
    print("\n=== Test Summary ===\n");
    print("Total: %d, Passed: %d, Failed: %d\n\n", total, total - failed, failed);
    
    return failed;
}

#endif /* XSCANF_ENABLE_TESTS */
