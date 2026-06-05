/*
 * utils.c
 *
 *  Created on: 29 Ağu 2022
 *      Author: fatih
 */
#include "utils.h"
#include <string.h>
#include "xprintf.h"
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

const char * contiki_event_to_str(uint8_t event)
{
	switch(event)
	{
	case 0x80: return "PROCESS_EVENT_NONE";
	case 0x81: return "PROCESS_EVENT_INIT";
	case 0x82: return "PROCESS_EVENT_POLL";
	case 0x83: return "PROCESS_EVENT_EXIT";
	case 0x84: return "PROCESS_EVENT_SERVICE_REMOVED";
	case 0x85: return "PROCESS_EVENT_CONTINUE";
	case 0x86: return "PROCESS_EVENT_MSG";
	case 0x87: return "PROCESS_EVENT_EXITED";
	case 0x88: return "PROCESS_EVENT_TIMER";
	case 0x89: return "PROCESS_EVENT_COM";
	case 0x8A: return "PROCESS_EVENT_MAX";
	default:
		return "UNKOWN EVENT!";
	}
}


// xisspace:  return true if the character is whitespace.
int xisspace(int c)
{
	return (c == ' ' || c == '\n' || c == '\t' || c == '\v' || c =='\f' || c == '\r');
}

// xisdigit: return true if the character is a digit.
int xisdigit(int c)
{
    return (c >= '0' && c <= '9');
}

int is_printable(int c)
{
    return (c >= 32 && c <= 126);
}

int is_numeric(const char *str)
{
    if (str == NULL || *str == '\0') {
        return 0;
    }

    while (*str) {
        if (!xisdigit(*str)) {
            return 0;
        }
        str++;
    }

    return 1; // True
}

#define MAX_INT_INTEGER_DIGITS 10  // Maximum number of digits for integer part
int xstrtoi(const char *str)
{
    int sign = 1;
    int result = 0;
    int digits = 0;

    // Check for sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    // Convert characters to integer, checking for overflow
    while (xisdigit((unsigned char)*str) && digits < MAX_INT_INTEGER_DIGITS) {
        if (result > (INT_MAX - (*str - '0')) / 10) {
            // Overflow detected
            return 0;
        }
        result = result * 10 + (*str - '0');
        str++;
        digits++;
    }

    return sign * result;
}

#define MAX_FLOAT_INTEGER_DIGITS 7
#define MAX_FLOAT_FRACTIONAL_DIGITS 7

float xstrtof(const char *str)
{
    int sign = 1;
    int integer_part = 0;
    int fractional_part = 0;
    int integer_digits = 0;
    int fractional_digits = 0;
    int fractional_divisor = 1;
    float result;

    // Check for sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    // Convert integer part
    while (xisdigit((unsigned char)*str) && integer_digits < MAX_FLOAT_INTEGER_DIGITS) {
        if (integer_part > (INT_MAX - (*str - '0')) / 10) {
            // Overflow detected
            return 0.0f;
        }
        integer_part = integer_part * 10 + (*str - '0');
        str++;
        integer_digits++;
    }

    // Check if there are non-digit characters remaining in integer part
    if (*str != '\0' && *str != '.') {
        return 0.0f;
    }

    // Convert fractional part if present
    if (*str == '.') {
        str++;
        while (xisdigit((unsigned char)*str) && fractional_digits < MAX_FLOAT_FRACTIONAL_DIGITS) {
            fractional_part = fractional_part * 10 + (*str - '0');
            fractional_divisor *= 10;
            str++;
            fractional_digits++;
        }
    }

    // Calculate the final result
    if (fractional_digits > 0) {
        result = integer_part + (fractional_part / (float)fractional_divisor);
    } else {
        result = integer_part;
    }

    return sign * result;
}

int is_hex(int c)
{
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'));
}

// Function to check if the given data is ASCII hex
int is_ascii_hex(const void *data, uint16_t length)
{
    const uint8_t *byteData = (const uint8_t *)data;

    for (size_t i = 0; i < length; i++)
    {
        if (!is_hex(byteData[i]))
        {
            return 0;
        }
    }

    return 1;
}

uint32_t ascii_hex_to_integer(const void *data, uint8_t len)
{
	if(!is_ascii_hex(data, len)){
		return 0;
	}

    const uint8_t *byteData = (const uint8_t *)data;
    uint32_t result = 0;

    for (int i = 0; i < len; i++)
    {
        result <<= 4;
        if (byteData[i] >= '0' && byteData[i] <= '9'){
            result |= (byteData[i] - '0');
        }
        else if (byteData[i] >= 'A' && byteData[i] <= 'F'){
            result |= (byteData[i] - 'A' + 10);
        }
        else if (byteData[i] >= 'a' && byteData[i] <= 'f'){
            result |= (byteData[i] - 'a' + 10);
        }
    }

    return result;
}

int is_contains(const void *main_block, size_t main_block_size, const void *sub_block, size_t sub_block_size)
{
    const char *main_ptr = (const char *)main_block;
    const char *sub_ptr = (const char *)sub_block;

    // If the sub-sequence size is greater than the main-sequence size, the check fails
    if (sub_block_size > main_block_size) {
        return 0; // Not found
    }

    // Use memcmp function for sequence comparison
    for (size_t i = 0; i <= main_block_size - sub_block_size; ++i)
    {
        if (memcmp(main_ptr + i, sub_ptr, sub_block_size) == 0)
        {
            return 1; // Found
        }
    }

    return 0; // Not found
}

int is_valid_mac_address(const char* mac)
{
    size_t len = strlen(mac);

    if (len == 12) {
        // Ayracsız MAC adresi (12 karakter)
        for (size_t i = 0; i < len; i++) {
            if (!isxdigit((unsigned char)mac[i])) {
                return 0;
            }
        }
    } else if (len == 17) {
        // Ayraclı MAC adresi (17 karakter)
        for (size_t i = 0; i < len; i++) {
            if (i % 3 == 2) {
                if (mac[i] != ':' && mac[i] != '-') {
                    return 0;
                }
            } else {
                if (!isxdigit((unsigned char)mac[i])) {
                    return 0;
                }
            }
        }
    } else {
        // Geçersiz uzunlukta
        return 0;
    }

    return 1;
}

#define BYTES_PER_LINE 16

void hex_dump(const void* data, size_t num_bytes)
{
	const uint8_t *buffer = data;

    uint32_t line_len = 0;
    uint32_t offset = 0;

    while (num_bytes > 0)
    {
    	if(num_bytes > BYTES_PER_LINE)
    		line_len = BYTES_PER_LINE;
    	else
    		line_len = num_bytes;

        xprintf("%08x | ", offset);
        for (size_t i = 0; i < line_len; i++)
        {
        	xprintf("%02x ", buffer[i]);
            if (i == 7)
                xprintf(" ");
        }

        xprintf(" | ");
        for (size_t i = 0; i < line_len; i++)
        {
            if (buffer[i] > 31 && buffer[i] < 127)
                xprintf("%c", buffer[i]);
            else
                xprintf(".");
        }
        xprintf(" |\n");

        buffer += line_len;
        offset += line_len;
        num_bytes -= line_len;
    }
}

uint8_t hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    return 0;
}

uint8_t ascii_hex_to_byte(const void *input)
{
    const char *hex = (const char *)input;
    uint8_t byte = 0;

    byte = hex_char_to_val(hex[0]) << 4;
    byte |= hex_char_to_val(hex[1]);

    return byte;
}

void byte_to_ascii_hex(uint8_t byte, void *output)
{
    const char hex_digits[] = "0123456789ABCDEF";
    uint8_t *ptr = output;

    ptr[0] = hex_digits[(byte >> 4) & 0x0F];
    ptr[1] = hex_digits[byte & 0x0F];
}
 
void uint32_to_ascii_bits(uint32_t number, char *buffer, int space)
{
    for (int i = 31; i >= 0; i--) {  // En yuksek bitten en dusuge dogru ilerle
        *buffer++ = (number & (1U << i)) ? '1' : '0';
        if (i != 0 && space) {
            *buffer++ = ' ';
        }
    }
    *buffer = '\0';
}

uint32_t count_set_bits(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    v = (v + (v >> 4)) & 0x0F0F0F0F;
    return (v * 0x01010101) >> 24;
}





#if 0

// Test functions
void test_xstrtoi()
{
    const char *test_cases[] = {"123", "-123", "2147483647", "-2147483648", "2147483648", "-2147483649", "12a34", "000123", "", "+123", "-", "--123", "12345a6789"};
    for (int i = 0; i < 13; i++) {
        int result = xstrtoi(test_cases[i]);
        if (result == 0 && test_cases[i][0] != '0') {
            printf("xstrtoi('%s') -> Overflow or invalid input detected\n", test_cases[i]);
        } else {
            printf("xstrtoi('%s') -> %d\n", test_cases[i], result);
        }
    }
}

void test_xstrtof()
{
    const char *test_cases[] = {"123.456", "-123.456", "123.000456", "0.1234567", "1.23456789", "1234567.1234567", "123a.456", "-0.0001234", "", ".456", "123.", "-123.456e", "1.23.45", "12.34a56"};
    for (int i = 0; i < 14; i++) {
        float result = xstrtof(test_cases[i]);
        if (result == 0.0f && (test_cases[i][0] != '0' && test_cases[i][0] != '.')) {
            printf("xstrtof('%s') -> Overflow or invalid input detected\n", test_cases[i]);
        } else {
            printf("xstrtof('%s') -> %f\n", test_cases[i], result);
        }
    }
}

#endif
