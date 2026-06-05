/*-----------------------------------------------------------------------*/
/* xscanf - Lightweight Safe Scanner for Embedded Systems                */
/*-----------------------------------------------------------------------*/
/* Copyright (c) 2024                                                     */
/* This module provides a safe, malloc-free scanf implementation         */
/* optimized for embedded systems with overflow protection.              */
/*-----------------------------------------------------------------------*/
/*
/  Usage Examples:
/  ---------------
/  int val;
/  xscanf("42", 3, "%d", &val);                      // val = 42
/
/  uint8_t a, b;
/  xscanf("192.168", 8, "%u8.%u8", &a, &b);          // a=192, b=168
/
/  uint32_t hex;
/  xscanf("0xABCD", 7, "0x%x", &hex);                // hex = 0xABCD
/
/  char buf[16];
/  xscanf("Hello World", 12, "%16S", buf);           // buf = "Hello"
/
/  uint8_t ip[4];
/  xscanf("192.168.1.100", 14, "%u8.%u8.%u8.%u8",    // IP parse
/         &ip[0], &ip[1], &ip[2], &ip[3]);
/
/  int16_t temp;
/  xscanf("-25C", 5, "%s16C", &temp);                // temp = -25
/
/  uint8_t rssi, ber;
/  xscanf("+CSQ: 18,0", 11, "+CSQ: %u8,%u8",         // AT command
/         &rssi, &ber);                              // rssi=18, ber=0
/
/  xscanf("skip:123", 9, "skip:%*d");                // skip, no store
/
/  Width specifier:
/  xscanf("12345", 6, "%3d", &val);                  // val = 123 (3 digits)
/  xscanf("99999", 6, "%2u8", &a);                   // a = 99 (2 digits)
/
/  Type Limits (auto-applied, user width is capped by type):
/  Type     Max Digits  Max Value       Example
/  %u8      3           255             "1234" -> reads "123" = 123
/  %s8      3           -128..127       "-999" -> reads "-99" = -99
/  %u16     5           65535           "123456" -> reads "12345"
/  %s16     5           -32768..32767   Same as u16
/  %u32     10          4294967295      Full uint32
/  %s32     10          -2147483648..   Full int32
/  %x       8           0xFFFFFFFF      8 hex digits max
/
/  Return: Number of successfully parsed arguments, -1 on error
/  Overflow: Value is capped at type max/min, error flag set
/
*/

/*----------------------------------------------*/
/* Safe string input parsing                    */
/*----------------------------------------------*/
/*  xscanf("42", 3, "%d", &i);                  i = 42
    xscanf("-100", 5, "%d", &i);                i = -100
    xscanf("255", 4, "%u8", &u8);               u8 = 255
    xscanf("256", 4, "%u8", &u8);               u8 = 255 (overflow capped)
    xscanf("-128", 5, "%s8", &s8);              s8 = -128
    xscanf("65535", 6, "%u16", &u16);           u16 = 65535
    xscanf("-32768", 7, "%s16", &s16);          s16 = -32768
    xscanf("4294967295", 11, "%u32", &u32);     u32 = 4294967295
    xscanf("DEADBEEF", 9, "%x", &hex);          hex = 0xDEADBEEF
    xscanf("0xABC", 6, "0x%x", &hex);           hex = 0xABC
    xscanf("A", 2, "%c", &ch);                  ch = 'A'
    xscanf("Hello", 6, "%16S", buf);            buf = "Hello"
    xscanf("Hi World", 9, "%16S", buf);         buf = "Hi" (no delimiter->stops at space)
    xscanf("Hi,World", 9, "%16S,", buf);        buf = "Hi" (delimiter is ',')
    xscanf("Hi World", 9, "%10r", buf);          buf = "Hi World" (raw, width REQUIRED)
    xscanf("Hello World!", 13, "%5r", buf);     buf = "Hello" (raw, 5 chars)
    xscanf("Test", 5, "%r", buf);               ERROR - width required for %r
    xscanf("12345", 6, "%3d", &i);              i = 123 (width limit)
    xscanf("999", 4, "%2u8", &u8);              u8 = 99 (width limit)
    xscanf("12345678901", 12, "%10u8", &u8);    u8 = 123 (type caps width)
    xscanf("skip:42", 8, "skip:%*d");           (skips, no store)
    xscanf("10,20", 6, "%d,%d", &a, &b);        a=10, b=20
    xscanf("192.168.1.1", 12, "%u8.%u8.%u8.%u8", ...);  IP parsing
    xscanf("+CSQ: 18,0", 11, "+CSQ: %u8,%u8", ...);     AT response
*/

#ifndef XSCANF_H
#define XSCANF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*-----------------------------------------------------------------------*/
/* Version                                                               */
/*-----------------------------------------------------------------------*/
#define XSCANF_VERSION      "1.0.0"
#define XSCANF_VERSION_NUM  0x010000

/*-----------------------------------------------------------------------*/
/* Configuration                                                         */
/*-----------------------------------------------------------------------*/
#ifndef XSCANF_MAX_WIDTH
#define XSCANF_MAX_WIDTH    16      /* Default max field width for %S */
#endif

/*-----------------------------------------------------------------------*/
/* Error Codes                                                           */
/*-----------------------------------------------------------------------*/
typedef enum {
    XSCANF_OK = 0,              /* Success */
    XSCANF_ERR_NULL_PTR,        /* NULL pointer passed */
    XSCANF_ERR_OVERFLOW,        /* Integer overflow detected */
    XSCANF_ERR_UNDERFLOW,       /* Integer underflow detected */
    XSCANF_ERR_INVALID_FORMAT,  /* Invalid format specifier */
    XSCANF_ERR_NO_MATCH,        /* Input doesn't match format */
    XSCANF_ERR_BUFFER_OVERFLOW, /* String buffer overflow */
    XSCANF_ERR_EMPTY_INPUT,     /* Empty input string */
} xscanf_err_t;

/*-----------------------------------------------------------------------*/
/* Result Structure                                                      */
/*-----------------------------------------------------------------------*/
typedef struct {
    int         count;          /* Number of successfully parsed arguments */
    xscanf_err_t error;         /* Last error code */
    size_t      input_pos;      /* Position in input where parsing stopped */
} xscanf_result_t;

/*-----------------------------------------------------------------------*/
/* Main API                                                              */
/*-----------------------------------------------------------------------*/

/**
 * @brief Safe scanf implementation
 * 
 * @param input     Input string to parse (null-terminated)
 * @param input_len Maximum length of input buffer (for safety)
 * @param format    Format string
 * @param ...       Output variables (pointers)
 * @return int      Number of successfully parsed arguments, -1 on error
 * 
 * Format Specifiers:
 *   %d      - signed int
 *   %u      - unsigned int  
 *   %u8     - uint8_t (0-255)
 *   %s8     - int8_t (-128 to 127)
 *   %u16    - uint16_t (0-65535)
 *   %s16    - int16_t (-32768 to 32767)
 *   %u32    - uint32_t (0-4294967295)
 *   %s32    - int32_t
 *   %x, %X  - hexadecimal (uint32_t)
 *   %c      - single character
 *   %[n]S   - string with max length n (default 16, stops at whitespace/delimiter)
 *   %[n]r   - raw string, width n is REQUIRED (reads all chars incl. whitespace)
 *   %*      - skip (read but don't store)
 *   %%      - literal '%'
 * 
 * Width modifier:
 *   %5d     - read max 5 digits
 *   %8x     - read max 8 hex digits
 *   %16S    - read max 16 chars into string
 * 
 * Example:
 *   uint8_t rssi, ber;
 *   xscanf("+CSQ: 18,0", 12, "+CSQ: %u8,%u8", &rssi, &ber);
 */
int xscanf(const char *input, size_t input_len, const char *format, ...);

/**
 * @brief Extended scanf with detailed result
 * 
 * @param result    Pointer to result structure (can be NULL)
 * @param input     Input string to parse
 * @param input_len Maximum length of input buffer
 * @param format    Format string
 * @param ...       Output variables
 * @return int      Number of successfully parsed arguments
 */
int xscanf_ex(xscanf_result_t *result, const char *input, size_t input_len, 
              const char *format, ...);

/**
 * @brief Get last error code
 * @return xscanf_err_t Last error
 */
xscanf_err_t xscanf_get_last_error(void);

/**
 * @brief Get error string
 * @param err Error code
 * @return const char* Error description
 */
const char* xscanf_error_str(xscanf_err_t err);

/*-----------------------------------------------------------------------*/
/* Utility Functions                                                     */
/*-----------------------------------------------------------------------*/

/**
 * @brief Check if character is a digit
 */
static inline bool xscanf_isdigit(char c) {
    return (c >= '0' && c <= '9');
}

/**
 * @brief Check if character is a hex digit
 */
static inline bool xscanf_isxdigit(char c) {
    return xscanf_isdigit(c) || 
           (c >= 'a' && c <= 'f') || 
           (c >= 'A' && c <= 'F');
}

/**
 * @brief Check if character is whitespace
 */
static inline bool xscanf_isspace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

/**
 * @brief Convert hex char to value
 */
static inline uint8_t xscanf_hexval(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Test Functions                                                        */
/*-----------------------------------------------------------------------*/
#ifdef XSCANF_ENABLE_TESTS

/**
 * @brief Run all xscanf tests
 * @param print_func Function pointer for printing (e.g., printf)
 * @return int Number of failed tests
 */
int xscanf_run_all_tests(void (*print_func)(const char *fmt, ...));

#endif /* XSCANF_ENABLE_TESTS */

#ifdef __cplusplus
}
#endif

#endif /* XSCANF_H */
