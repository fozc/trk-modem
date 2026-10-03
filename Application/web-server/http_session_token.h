/*
 * http_session_token.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Fixed-size HTTP session tokens with an injected entropy source.
 */
#ifndef HTTP_SESSION_TOKEN_H
#define HTTP_SESSION_TOKEN_H

#include "bsp_random.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HTTP_SESSION_TOKEN_HEX_LEN 32U
#define HTTP_SESSION_TOKEN_SIZE (HTTP_SESSION_TOKEN_HEX_LEN + 1U)

/** Format four BSP random words. Failure clears the output. */
static inline bool http_session_token_generate(
    char token[HTTP_SESSION_TOKEN_SIZE])
{
    static const char hex_digits[] = "0123456789ABCDEF";
    uint32_t combined = 0U;

    if (NULL == token)
    {
        return false;
    }
    memset(token, 0, HTTP_SESSION_TOKEN_SIZE);
    for (size_t word_index = 0U; word_index < 4U; word_index++)
    {
        uint32_t word = 0U;

        if (!bsp_random_word(&word))
        {
            memset(token, 0, HTTP_SESSION_TOKEN_SIZE);
            return false;
        }
        combined |= word;
        for (size_t digit = 0U; digit < 8U; digit++)
        {
            uint32_t shift = (uint32_t)((7U - digit) * 4U);
            size_t index = (word_index * 8U) + digit;

            token[index] = hex_digits[(word >> shift) & 0x0FU];
        }
    }
    if (0U == combined)
    {
        memset(token, 0, HTTP_SESSION_TOKEN_SIZE);
        return false;
    }
    return true;
}

/** Match an exact t= parameter; compare all digits of a valid token. */
static inline bool http_session_token_matches(
    const char *token, const char *query_string)
{
    const char *value;
    uint32_t difference = 0U;

    if ((NULL == token) || ('\0' == token[0]) || (NULL == query_string))
    {
        return false;
    }
    if (0 == strncmp(query_string, "t=", 2U))
    {
        value = query_string + 2U;
    }
    else
    {
        value = strstr(query_string, "&t=");
        if (NULL == value)
        {
            return false;
        }
        value += 3U;
    }

    for (size_t digit = 0U; digit < HTTP_SESSION_TOKEN_HEX_LEN; digit++)
    {
        uint8_t character = (uint8_t)value[digit];

        if (((uint8_t)'a' <= character) && ((uint8_t)'f' >= character))
        {
            character = (uint8_t)(character - (uint8_t)'a' + (uint8_t)'A');
        }
        else
        {
            /* Uppercase hex and decimal digits need no conversion. */
        }
        if (!((((uint8_t)'0' <= character) &&
               ((uint8_t)'9' >= character)) ||
              (((uint8_t)'A' <= character) &&
               ((uint8_t)'F' >= character))))
        {
            return false;
        }
        difference |= (uint32_t)(character ^ (uint8_t)token[digit]);
    }
    if (('\0' != value[HTTP_SESSION_TOKEN_HEX_LEN]) &&
        ('&' != value[HTTP_SESSION_TOKEN_HEX_LEN]))
    {
        return false;
    }
    return 0U == difference;
}

#endif /* HTTP_SESSION_TOKEN_H */
/*** end of file ***/
