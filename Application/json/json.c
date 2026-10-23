/*
 * json.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Bounded JSON syntax, Unicode and binary32 conversion.
 */
#include "json.h"

#if JSON_ENABLE_FLOAT
#include <float.h>
#include <fenv.h>
#endif
#include <limits.h>
#include <string.h>

_Static_assert(JSON_MAX_DEPTH > 0U, "JSON needs a nesting frame");
_Static_assert(CHAR_BIT == 8, "JSON requires octets");
#if JSON_ENABLE_FLOAT
_Static_assert(sizeof(float) == sizeof(uint32_t), "binary32 required");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "binary32 required");
#endif

enum
{
    OBJECT_FIRST,
    OBJECT_KEY,
    OBJECT_COLON,
    OBJECT_VALUE,
    OBJECT_NEXT,
    ARRAY_FIRST,
    ARRAY_VALUE,
    ARRAY_NEXT
};
enum
{
    MODE_IDLE,
    MODE_STRING,
    MODE_NUMBER,
    MODE_LITERAL
};
enum
{
    NUM_START,
    NUM_MINUS,
    NUM_ZERO,
    NUM_INT,
    NUM_DOT,
    NUM_FRAC,
    NUM_E,
    NUM_SIGN,
    NUM_EXP,
    NUM_INVALID
};
enum
{
    ESC_NONE,
    ESC_START,
    ESC_HEX,
    ESC_LOW_SLASH,
    ESC_LOW_U,
    ESC_LOW_HEX
};
enum
{
    WRITE_OBJECT_FIRST,
    WRITE_OBJECT_VALUE,
    WRITE_OBJECT_NEXT,
    WRITE_ARRAY_FIRST,
    WRITE_ARRAY_NEXT
};
enum
{
    PENDING_NONE,
    PENDING_RAW,
    PENDING_STRING
};

static bool is_digit(uint8_t byte)
{
    return byte >= (uint8_t)'0' && byte <= (uint8_t)'9';
}

static bool is_space(uint8_t byte)
{
    return byte == (uint8_t)' ' || byte == (uint8_t)'\t' ||
           byte == (uint8_t)'\r' || byte == (uint8_t)'\n';
}

static uint8_t number_next(uint8_t state, uint8_t byte)
{
    switch (state)
    {
    case NUM_START:
        if (byte == (uint8_t)'-')
        {
            return NUM_MINUS;
        }
        /* The unsigned starting states share the digit rules below. */
        return byte == (uint8_t)'0'
                   ? NUM_ZERO
                   : (byte >= (uint8_t)'1' && byte <= (uint8_t)'9'
                          ? NUM_INT
                          : NUM_INVALID);
    case NUM_MINUS:
        return byte == (uint8_t)'0'
                   ? NUM_ZERO
                   : (byte >= (uint8_t)'1' && byte <= (uint8_t)'9'
                          ? NUM_INT
                          : NUM_INVALID);
    case NUM_ZERO:
    case NUM_INT:
        if (state == NUM_INT && is_digit(byte))
        {
            return NUM_INT;
        }
        if (byte == (uint8_t)'.')
        {
            return NUM_DOT;
        }
        return byte == (uint8_t)'e' || byte == (uint8_t)'E' ? NUM_E
                                                            : NUM_INVALID;
    case NUM_DOT:
        return is_digit(byte) ? NUM_FRAC : NUM_INVALID;
    case NUM_FRAC:
        if (is_digit(byte))
        {
            return NUM_FRAC;
        }
        return byte == (uint8_t)'e' || byte == (uint8_t)'E' ? NUM_E
                                                            : NUM_INVALID;
    case NUM_E:
        if (byte == (uint8_t)'+' || byte == (uint8_t)'-')
        {
            return NUM_SIGN;
        }
        return is_digit(byte) ? NUM_EXP : NUM_INVALID;
    case NUM_SIGN:
    case NUM_EXP:
        return is_digit(byte) ? NUM_EXP : NUM_INVALID;
    default:
        return NUM_INVALID;
    }
}

static bool number_complete(uint8_t state)
{
    return state == NUM_ZERO || state == NUM_INT || state == NUM_FRAC ||
           state == NUM_EXP;
}

static bool valid_number(json_span_t text)
{
    uint8_t state = NUM_START;
    if (text.data == NULL)
    {
        return false;
    }
    for (size_t index = 0U; index < text.length; ++index)
    {
        state = number_next(state, (uint8_t)text.data[index]);
        if (state == NUM_INVALID)
        {
            return false;
        }
    }
    return number_complete(state);
}

/* Validate shortest-form UTF-8, excluding surrogates and > U+10FFFF. */
static bool utf8_byte(uint8_t byte, uint8_t *remaining, uint32_t *codepoint,
                      uint32_t *minimum)
{
    if ((*remaining) != 0U)
    {
        if ((byte & 0xC0U) != 0x80U)
        {
            return false;
        }
        (*codepoint) = ((*codepoint) << 6U) | (byte & 0x3FU);
        --(*remaining);
        if ((*remaining) == 0U)
        {
            return (*codepoint) >= (*minimum) && (*codepoint) <= 0x10FFFFU &&
                   !((*codepoint) >= 0xD800U && (*codepoint) <= 0xDFFFU);
        }
        return true;
    }
    if (byte < 0x80U)
    {
        return true;
    }
    if (byte >= 0xC2U && byte <= 0xDFU)
    {
        (*codepoint) = byte & 0x1FU;
        (*remaining) = 1U;
        (*minimum) = 0x80U;
    }
    else if (byte >= 0xE0U && byte <= 0xEFU)
    {
        (*codepoint) = byte & 0x0FU;
        (*remaining) = 2U;
        (*minimum) = 0x800U;
    }
    else if (byte >= 0xF0U && byte <= 0xF4U)
    {
        (*codepoint) = byte & 0x07U;
        (*remaining) = 3U;
        (*minimum) = 0x10000U;
    }
    else
    {
        return false;
    }
    return true;
}

static json_status_t reader_error(json_reader_t *reader, json_error_t error)
{
    if (reader->error == JSON_ERROR_NONE)
    {
        reader->error = error;
        reader->error_offset = reader->offset;
    }
    return JSON_ERROR;
}

static json_status_t reader_empty(json_reader_t *reader)
{
    return reader->final ? reader_error(reader, JSON_ERROR_INCOMPLETE)
                         : JSON_NEED_INPUT;
}

static void consume_byte(json_reader_t *reader)
{
    ++reader->consumed;
    ++reader->offset;
}

static bool append_byte(json_reader_t *reader, uint8_t byte)
{
    if (reader->skipping)
    {
        return true;
    }
    if (reader->length == reader->capacity)
    {
        (void)reader_error(reader, JSON_ERROR_BUFFER);
        return false;
    }
    reader->buffer[reader->length++] = (char)byte;
    return true;
}

static bool append_codepoint(json_reader_t *reader)
{
    uint32_t codepoint = reader->codepoint;
    if (codepoint < 0x80U)
    {
        return append_byte(reader, (uint8_t)codepoint);
    }
    if (codepoint < 0x800U)
    {
        if (!append_byte(reader, (uint8_t)(0xC0U | (codepoint >> 6U))))
        {
            return false;
        }
    }
    else
    {
        if (codepoint >= 0x10000U)
        {
            if (!append_byte(reader, (uint8_t)(0xF0U | (codepoint >> 18U))))
            {
                return false;
            }
            if (!append_byte(reader,
                             (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3FU))))
            {
                return false;
            }
        }
        else if (!append_byte(reader, (uint8_t)(0xE0U | (codepoint >> 12U))))
        {
            return false;
        }
        else
        {
            /* First byte was written. */
        }
        if (!append_byte(reader,
                         (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU))))
        {
            return false;
        }
    }
    return append_byte(reader, (uint8_t)(0x80U | (codepoint & 0x3FU)));
}

static uint8_t hex_digit(uint8_t byte)
{
    if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9')
    {
        return (uint8_t)(byte - (uint8_t)'0');
    }
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f')
    {
        return (uint8_t)(byte - (uint8_t)'a' + 10U);
    }
    if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F')
    {
        return (uint8_t)(byte - (uint8_t)'A' + 10U);
    }
    return 0xFFU;
}

static bool read_hex(json_reader_t *reader, uint8_t byte)
{
    uint8_t digit = hex_digit(byte);
    if (digit == 0xFFU)
    {
        return false;
    }
    reader->codepoint = (reader->codepoint << 4U) | digit;
    ++reader->digits;
    if (reader->digits != 4U)
    {
        return true;
    }
    if (reader->escape == ESC_HEX && reader->codepoint >= 0xD800U &&
        reader->codepoint <= 0xDBFFU)
    {
        reader->high_surrogate = (uint16_t)reader->codepoint;
        reader->escape = ESC_LOW_SLASH;
        return true;
    }
    if (reader->escape == ESC_LOW_HEX)
    {
        if (reader->codepoint < 0xDC00U || reader->codepoint > 0xDFFFU)
        {
            return false;
        }
        reader->codepoint =
            0x10000U + (((uint32_t)reader->high_surrogate - 0xD800U) << 10U) +
            reader->codepoint - 0xDC00U;
    }
    else if (reader->codepoint >= 0xDC00U && reader->codepoint <= 0xDFFFU)
    {
        return false;
    }
    else
    {
        /* A single BMP code point. */
    }
    reader->escape = ESC_NONE;
    return append_codepoint(reader);
}

static bool read_escape(json_reader_t *reader, uint8_t byte)
{
    static const char escaped[] = "\"\\/bfnrt";
    static const char decoded[] = "\"\\/\b\f\n\r\t";
    if (reader->escape == ESC_HEX || reader->escape == ESC_LOW_HEX)
    {
        return read_hex(reader, byte);
    }
    if (reader->escape == ESC_LOW_SLASH)
    {
        reader->escape = ESC_LOW_U;
        return byte == (uint8_t)'\\';
    }
    if (reader->escape == ESC_LOW_U)
    {
        reader->escape = ESC_LOW_HEX;
        reader->codepoint = 0U;
        reader->digits = 0U;
        return byte == (uint8_t)'u';
    }
    if (byte == (uint8_t)'u')
    {
        reader->escape = ESC_HEX;
        reader->digits = 0U;
        reader->codepoint = 0U;
        return true;
    }
    for (size_t index = 0U; index < sizeof(escaped) - 1U; ++index)
    {
        if (byte == (uint8_t)escaped[index])
        {
            reader->escape = ESC_NONE;
            return append_byte(reader, (uint8_t)decoded[index]);
        }
    }
    return false;
}

static bool copy_ascii(json_reader_t *reader)
{
    size_t available = reader->input.length - reader->consumed;
    size_t capacity = reader->capacity - reader->length;
    if (!reader->skipping && available > capacity)
    {
        available = capacity;
    }
    const char *data = reader->input.data + reader->consumed;
    size_t length = 0U;
    while (length < available && (uint8_t)data[length] >= 0x20U &&
           (uint8_t)data[length] < 0x80U && data[length] != '"' &&
           data[length] != '\\')
    {
        ++length;
    }
    if (length == 0U)
    {
        return false;
    }
    if (!reader->skipping)
    {
        memcpy(reader->buffer + reader->length, data, length);
        reader->length += length;
    }
    reader->consumed += length;
    reader->offset += length;
    return true;
}

static json_status_t read_string(json_reader_t *reader)
{
    while (reader->consumed < reader->input.length)
    {
        if (reader->escape == ESC_NONE && reader->utf8_remaining == 0U &&
            copy_ascii(reader))
        {
            continue;
        }
        uint8_t byte = (uint8_t)reader->input.data[reader->consumed];
        if (reader->escape != ESC_NONE)
        {
            if (!read_escape(reader, byte))
            {
                return reader_error(reader, JSON_ERROR_ESCAPE);
            }
        }
        else if (byte == (uint8_t)'"' && reader->utf8_remaining == 0U)
        {
            consume_byte(reader);
            reader->mode = MODE_IDLE;
            return JSON_OK;
        }
        else if (byte == (uint8_t)'\\' && reader->utf8_remaining == 0U)
        {
            reader->escape = ESC_START;
        }
        else
        {
            if (byte < 0x20U)
            {
                return reader_error(reader, JSON_ERROR_SYNTAX);
            }
            if (!utf8_byte(byte, &reader->utf8_remaining, &reader->codepoint,
                           &reader->utf8_min))
            {
                return reader_error(reader, JSON_ERROR_UTF8);
            }
            if (!append_byte(reader, byte))
            {
                return JSON_ERROR;
            }
        }
        consume_byte(reader);
    }
    return reader_empty(reader);
}

static json_status_t read_number(json_reader_t *reader)
{
    while (reader->consumed < reader->input.length)
    {
        uint8_t byte = (uint8_t)reader->input.data[reader->consumed];
        uint8_t next = number_next(reader->number_state, byte);
        if (next == NUM_INVALID)
        {
            if (!number_complete(reader->number_state) ||
                !(is_space(byte) || byte == (uint8_t)',' ||
                  byte == (uint8_t)']' || byte == (uint8_t)'}'))
            {
                return reader_error(reader, JSON_ERROR_SYNTAX);
            }
            reader->mode = MODE_IDLE;
            return JSON_OK;
        }
        if (!append_byte(reader, byte))
        {
            return JSON_ERROR;
        }
        reader->number_state = next;
        consume_byte(reader);
    }
    if (reader->final && number_complete(reader->number_state))
    {
        reader->mode = MODE_IDLE;
        return JSON_OK;
    }
    return reader_empty(reader);
}

static json_status_t read_literal(json_reader_t *reader)
{
    while (reader->literal[reader->literal_index] != '\0')
    {
        if (reader->consumed == reader->input.length)
        {
            return reader_empty(reader);
        }
        if (reader->input.data[reader->consumed] !=
            reader->literal[reader->literal_index])
        {
            return reader_error(reader, JSON_ERROR_SYNTAX);
        }
        ++reader->literal_index;
        consume_byte(reader);
    }
    reader->mode = MODE_IDLE;
    return JSON_OK;
}

json_status_t json_reader_init(json_reader_t *reader, char *buffer,
                               size_t capacity)
{
    if (reader == NULL)
    {
        return JSON_ERROR;
    }
    memset(reader, 0, sizeof(*reader));
    reader->buffer = buffer;
    reader->capacity = capacity;
    if (buffer == NULL && capacity != 0U)
    {
        return reader_error(reader, JSON_ERROR_ARGUMENT);
    }
    return JSON_OK;
}

json_status_t json_reader_feed(json_reader_t *reader, json_span_t input,
                               bool final)
{
    if (reader == NULL)
    {
        return JSON_ERROR;
    }
    if (reader->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (reader->final || reader->consumed != reader->input.length)
    {
        return reader_error(reader, JSON_ERROR_STATE);
    }
    if ((input.data == NULL && input.length != 0U) ||
        input.length > SIZE_MAX - reader->offset)
    {
        return reader_error(reader, JSON_ERROR_ARGUMENT);
    }
    reader->input = input;
    reader->consumed = 0U;
    reader->final = final;
    return JSON_OK;
}

static void value_started(json_reader_t *reader)
{
    if (reader->depth == 0U)
    {
        reader->started = true;
    }
    else
    {
        uint8_t *state = &reader->stack[reader->depth - 1U];
        *state = *state == OBJECT_VALUE ? OBJECT_NEXT : ARRAY_NEXT;
    }
}

static json_status_t start_value(json_reader_t *reader, uint8_t byte)
{
    if (byte == (uint8_t)'{' || byte == (uint8_t)'[')
    {
        if (reader->depth == JSON_MAX_DEPTH)
        {
            return reader_error(reader, JSON_ERROR_DEPTH);
        }
        value_started(reader);
        reader->stack[reader->depth++] =
            byte == (uint8_t)'{' ? OBJECT_FIRST : ARRAY_FIRST;
        reader->type =
            byte == (uint8_t)'{' ? JSON_OBJECT_BEGIN : JSON_ARRAY_BEGIN;
        consume_byte(reader);
        return JSON_OK;
    }
    if (byte == (uint8_t)'"')
    {
        reader->type = JSON_STRING;
        reader->mode = MODE_STRING;
        consume_byte(reader);
    }
    else if (byte == (uint8_t)'-' || is_digit(byte))
    {
        reader->type = JSON_NUMBER;
        reader->mode = MODE_NUMBER;
        reader->number_state = NUM_START;
    }
    else if (byte == (uint8_t)'t' || byte == (uint8_t)'f' ||
             byte == (uint8_t)'n')
    {
        reader->type = byte == (uint8_t)'n' ? JSON_NULL : JSON_BOOLEAN;
        reader->literal = byte == (uint8_t)'t'
                              ? "true"
                              : (byte == (uint8_t)'f' ? "false" : "null");
        reader->literal_index = 0U;
        reader->mode = MODE_LITERAL;
    }
    else
    {
        return reader_error(reader, JSON_ERROR_SYNTAX);
    }
    value_started(reader);
    return JSON_NEED_INPUT; /* Internal: lexer must run, even in this chunk. */
}

static json_status_t read_structure(json_reader_t *reader, uint8_t byte)
{
    uint8_t *state = &reader->stack[reader->depth - 1U];
    bool object_end = byte == (uint8_t)'}' &&
                      (*state == OBJECT_FIRST || *state == OBJECT_NEXT);
    bool array_end =
        byte == (uint8_t)']' && (*state == ARRAY_FIRST || *state == ARRAY_NEXT);
    if (object_end || array_end)
    {
        --reader->depth;
        reader->type = object_end ? JSON_OBJECT_END : JSON_ARRAY_END;
        consume_byte(reader);
        return JSON_OK;
    }
    switch (*state)
    {
    case OBJECT_FIRST:
    case OBJECT_KEY:
        if (byte != (uint8_t)'"')
        {
            return reader_error(reader, JSON_ERROR_SYNTAX);
        }
        *state = OBJECT_COLON;
        reader->type = JSON_KEY;
        reader->mode = MODE_STRING;
        consume_byte(reader);
        break;
    case OBJECT_COLON:
        if (byte != (uint8_t)':')
        {
            return reader_error(reader, JSON_ERROR_SYNTAX);
        }
        *state = OBJECT_VALUE;
        consume_byte(reader);
        break;
    case OBJECT_NEXT:
    case ARRAY_NEXT:
        if (byte != (uint8_t)',')
        {
            return reader_error(reader, JSON_ERROR_SYNTAX);
        }
        *state = *state == OBJECT_NEXT ? OBJECT_KEY : ARRAY_VALUE;
        consume_byte(reader);
        break;
    case OBJECT_VALUE:
    case ARRAY_FIRST:
    case ARRAY_VALUE:
        return start_value(reader, byte);
    default:
        return reader_error(reader, JSON_ERROR_STATE);
    }
    return JSON_NEED_INPUT;
}

static json_status_t next_token(json_reader_t *reader, json_token_t *token)
{
    json_status_t status;
    for (;;)
    {
        if (reader->mode != MODE_IDLE)
        {
            status = reader->mode == MODE_STRING
                         ? read_string(reader)
                         : (reader->mode == MODE_NUMBER ? read_number(reader)
                                                        : read_literal(reader));
            break;
        }
        if (reader->consumed == reader->input.length)
        {
            if (reader->started && reader->depth == 0U)
            {
                return reader->final ? JSON_DONE : JSON_NEED_INPUT;
            }
            return reader_empty(reader);
        }
        uint8_t byte = (uint8_t)reader->input.data[reader->consumed];
        if (is_space(byte))
        {
            consume_byte(reader);
            continue;
        }
        reader->token_offset = reader->offset;
        reader->length = 0U;
        if (reader->depth == 0U)
        {
            if (reader->started)
            {
                return reader_error(reader, JSON_ERROR_SYNTAX);
            }
            status = start_value(reader, byte);
        }
        else
        {
            status = read_structure(reader, byte);
        }
        if (status != JSON_NEED_INPUT)
        {
            break;
        }
    }
    if (status == JSON_OK)
    {
        json_token_t result = {reader->type,
                               {reader->buffer, reader->length},
                               reader->token_offset,
                               false};
        result.boolean =
            reader->type == JSON_BOOLEAN && reader->literal[0] == 't';
        *token = result;
    }
    return status;
}

json_status_t json_read(json_reader_t *reader, json_token_t *token)
{
    if (reader == NULL)
    {
        return JSON_ERROR;
    }
    if (reader->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (token == NULL)
    {
        return reader_error(reader, JSON_ERROR_ARGUMENT);
    }
    if (reader->skipping)
    {
        return reader_error(reader, JSON_ERROR_STATE);
    }
    return next_token(reader, token);
}

json_status_t json_skip_value(json_reader_t *reader)
{
    json_token_t token;
    if (reader == NULL || reader->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (!reader->skipping)
    {
        reader->skipping = true;
        reader->skip_depth = SIZE_MAX;
    }
    for (;;)
    {
        json_status_t status = next_token(reader, &token);
        if (status != JSON_OK)
        {
            return status == JSON_DONE ? reader_error(reader, JSON_ERROR_STATE)
                                       : status;
        }
        if (token.type == JSON_KEY || token.type == JSON_OBJECT_END ||
            token.type == JSON_ARRAY_END)
        {
            if (reader->skip_depth == SIZE_MAX)
            {
                return reader_error(reader, JSON_ERROR_STATE);
            }
        }
        if (reader->skip_depth == SIZE_MAX)
        {
            bool container = token.type == JSON_OBJECT_BEGIN ||
                             token.type == JSON_ARRAY_BEGIN;
            reader->skip_depth = container ? reader->depth - 1U : reader->depth;
        }
        if (reader->depth == reader->skip_depth)
        {
            reader->skipping = false;
            return JSON_OK;
        }
    }
}

bool json_token_equals(const json_token_t *token, const char *text)
{
    if (token == NULL || text == NULL ||
        (token->type != JSON_KEY && token->type != JSON_STRING))
    {
        return false;
    }
    json_span_t value = {text, strlen(text)};
    return json_token_equals_span(token, value);
}

bool json_token_equals_span(const json_token_t *token, json_span_t text)
{
    if (token == NULL || (text.data == NULL && text.length != 0U) ||
        (token->type != JSON_KEY && token->type != JSON_STRING))
    {
        return false;
    }
    return text.length == token->text.length &&
           (text.length == 0U ||
            (token->text.data != NULL &&
             memcmp(token->text.data, text.data, text.length) == 0));
}

json_error_t json_token_copy_string(const json_token_t *token, char *output,
                                    size_t capacity)
{
    if (token == NULL || output == NULL ||
        (token->text.data == NULL && token->text.length != 0U))
    {
        return JSON_ERROR_ARGUMENT;
    }
    if (token->type != JSON_KEY && token->type != JSON_STRING)
    {
        return JSON_ERROR_TYPE;
    }
    if (token->text.length >= capacity)
    {
        return JSON_ERROR_BUFFER;
    }
    if (token->text.length != 0U)
    {
        if (memchr(token->text.data, '\0', token->text.length) != NULL)
        {
            return JSON_ERROR_RANGE;
        }
        memmove(output, token->text.data, token->text.length);
    }
    output[token->text.length] = '\0';
    return JSON_ERROR_NONE;
}

json_error_t json_token_bool(const json_token_t *token, bool *value)
{
    if (token == NULL || value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    if (token->type != JSON_BOOLEAN)
    {
        return JSON_ERROR_TYPE;
    }
    *value = token->boolean;
    return JSON_ERROR_NONE;
}

static json_error_t integer_value(const json_token_t *token, uint32_t limit,
                                  uint32_t *value, bool allow_negative)
{
    uint32_t result = 0U;
    if (token == NULL || value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    if (token->type != JSON_NUMBER)
    {
        return JSON_ERROR_TYPE;
    }
    json_span_t text = token->text;
    if (text.data == NULL || text.length == 0U)
    {
        return JSON_ERROR_SYNTAX;
    }
    size_t index = text.data[0] == '-' ? 1U : 0U;
    if (index != 0U && !allow_negative)
    {
        return valid_number(text) ? JSON_ERROR_RANGE : JSON_ERROR_SYNTAX;
    }
    if (index == text.length ||
        (text.data[index] == '0' && text.length - index > 1U &&
         is_digit((uint8_t)text.data[index + 1U])))
    {
        return JSON_ERROR_SYNTAX;
    }
    uint32_t cutoff = limit / 10U;
    uint32_t remainder = limit % 10U;
    for (; index < text.length; ++index)
    {
        uint8_t byte = (uint8_t)text.data[index];
        if (!is_digit(byte))
        {
            return valid_number(text) ? JSON_ERROR_TYPE : JSON_ERROR_SYNTAX;
        }
        uint32_t digit = byte - (uint8_t)'0';
        if (result > cutoff || (result == cutoff && digit > remainder))
        {
            return valid_number(text) ? JSON_ERROR_RANGE : JSON_ERROR_SYNTAX;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return JSON_ERROR_NONE;
}

json_error_t json_token_uint32(const json_token_t *token, uint32_t *value)
{
    return integer_value(token, UINT32_MAX, value, false);
}

json_error_t json_token_int32(const json_token_t *token, int32_t *value)
{
    uint32_t magnitude;
    if (value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    json_error_t error = integer_value(token, 2147483648U, &magnitude, true);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    bool negative = token->text.data[0] == '-';
    if (!negative && magnitude > (uint32_t)INT32_MAX)
    {
        return JSON_ERROR_RANGE;
    }
    *value = negative
                 ? (magnitude == 2147483648U ? INT32_MIN : -(int32_t)magnitude)
                 : (int32_t)magnitude;
    return JSON_ERROR_NONE;
}

json_error_t json_token_uint16(const json_token_t *token, uint16_t *value)
{
    uint32_t result;
    if (value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    json_error_t error = json_token_uint32(token, &result);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    if (result > UINT16_MAX)
    {
        return JSON_ERROR_RANGE;
    }
    *value = (uint16_t)result;
    return JSON_ERROR_NONE;
}

json_error_t json_token_int16(const json_token_t *token, int16_t *value)
{
    int32_t result;
    if (value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    json_error_t error = json_token_int32(token, &result);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    if (result < INT16_MIN || result > INT16_MAX)
    {
        return JSON_ERROR_RANGE;
    }
    *value = (int16_t)result;
    return JSON_ERROR_NONE;
}

json_error_t json_token_uint8(const json_token_t *token, uint8_t *value)
{
    uint32_t result;
    if (value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    json_error_t error = json_token_uint32(token, &result);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    if (result > UINT8_MAX)
    {
        return JSON_ERROR_RANGE;
    }
    *value = (uint8_t)result;
    return JSON_ERROR_NONE;
}

json_error_t json_token_int8(const json_token_t *token, int8_t *value)
{
    int32_t result;
    if (value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    json_error_t error = json_token_int32(token, &result);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    if (result < INT8_MIN || result > INT8_MAX)
    {
        return JSON_ERROR_RANGE;
    }
    *value = (int8_t)result;
    return JSON_ERROR_NONE;
}

/* A bounded integer is used only by the binary32 codec. With <=128 decimal
 * digits and decimal order in [-46,38], every intermediate fits 768 bits.
 * This avoids libc locale/heap/errno and any floating-point arithmetic.
 */
#if JSON_ENABLE_FLOAT
#define BIG_WORDS 24U
typedef struct
{
    uint32_t words[BIG_WORDS];
    size_t used;
} big_uint_t;

static void big_init(big_uint_t *number, uint32_t value)
{
    memset(number, 0, sizeof(*number));
    number->words[0] = value;
    number->used = 1U;
}

static void big_multiply(big_uint_t *number, uint32_t factor)
{
    uint64_t carry = 0U;
    for (size_t index = 0U; index < number->used; ++index)
    {
        uint64_t product = (uint64_t)number->words[index] * factor + carry;
        number->words[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0U && number->used < BIG_WORDS)
    {
        number->words[number->used++] = (uint32_t)carry;
    }
}

static void big_add_digit(big_uint_t *number, uint8_t digit)
{
    big_multiply(number, 10U);
    uint64_t carry = digit;
    for (size_t index = 0U; index < number->used && carry != 0U; ++index)
    {
        carry += number->words[index];
        number->words[index] = (uint32_t)carry;
        carry >>= 32U;
    }
    if (carry != 0U && number->used < BIG_WORDS)
    {
        number->words[number->used++] = (uint32_t)carry;
    }
}

static void big_shift(big_uint_t *number, uint32_t bits)
{
    while (bits != 0U)
    {
        uint32_t shift = bits > 31U ? 31U : bits;
        big_multiply(number, UINT32_C(1) << shift);
        bits -= shift;
    }
}

static int32_t big_compare(const big_uint_t *left, const big_uint_t *right)
{
    size_t length = left->used > right->used ? left->used : right->used;
    while (length != 0U)
    {
        --length;
        uint32_t a = length < left->used ? left->words[length] : 0U;
        uint32_t b = length < right->used ? right->words[length] : 0U;
        if (a != b)
        {
            return a > b ? 1 : -1;
        }
    }
    return 0;
}

static void big_subtract(big_uint_t *left, const big_uint_t *right)
{
    uint64_t borrow = 0U;
    for (size_t index = 0U; index < left->used; ++index)
    {
        uint64_t subtrahend =
            borrow + (index < right->used ? right->words[index] : 0U);
        uint32_t original = left->words[index];
        left->words[index] = original - (uint32_t)subtrahend;
        borrow = (uint64_t)original < subtrahend ? 1U : 0U;
    }
    while (left->used > 1U && left->words[left->used - 1U] == 0U)
    {
        --left->used;
    }
}

static int32_t compare_binary(const big_uint_t *numerator,
                              const big_uint_t *denominator, uint32_t mantissa,
                              int32_t exponent)
{
    big_uint_t left = *numerator;
    big_uint_t right = *denominator;
    big_multiply(&right, mantissa);
    if (exponent >= 0)
    {
        big_shift(&right, (uint32_t)exponent);
    }
    else
    {
        big_shift(&left, (uint32_t)-exponent);
    }
    return big_compare(&left, &right);
}

static uint32_t float_mantissa(uint32_t bits)
{
    uint32_t mantissa = bits & 0x7FFFFFU;
    return (bits & 0x7F800000U) == 0U ? mantissa : mantissa | 0x800000U;
}

static int32_t float_exponent(uint32_t bits)
{
    uint32_t exponent = (bits >> 23U) & 0xFFU;
    return exponent == 0U ? -149 : (int32_t)exponent - 150;
}

static bool parse_exponent(json_span_t text, size_t index, uint8_t state,
                           int32_t *exponent)
{
    bool negative = false;
    for (; index < text.length; ++index)
    {
        uint8_t byte = (uint8_t)text.data[index];
        state = number_next(state, byte);
        if (state == NUM_INVALID)
        {
            return false;
        }
        if (state == NUM_SIGN)
        {
            negative = byte == (uint8_t)'-';
        }
        else if (state == NUM_EXP && *exponent < 10000)
        {
            *exponent = *exponent * 10 + (int32_t)(byte - (uint8_t)'0');
        }
        else
        {
            /* The exponent marker does not contribute to its value. */
        }
    }
    if (negative)
    {
        *exponent = -*exponent;
    }
    return number_complete(state);
}

static json_error_t decimal_ratio(json_span_t text, big_uint_t *numerator,
                                  big_uint_t *denominator, bool *negative)
{
    if (text.data == NULL || text.length == 0U)
    {
        return JSON_ERROR_SYNTAX;
    }
    size_t index = text.data[0] == '-' ? 1U : 0U;
    uint8_t state = index != 0U ? NUM_MINUS : NUM_START;
    size_t digits = 0U;
    size_t fraction = 0U;
    size_t first_nonzero = SIZE_MAX;
    bool after_point = false;
    int32_t exponent = 0;
    big_init(numerator, 0U);
    big_init(denominator, 1U);
    *negative = index != 0U;
    while (index < text.length && text.data[index] != 'e' &&
           text.data[index] != 'E')
    {
        uint8_t byte = (uint8_t)text.data[index++];
        state = number_next(state, byte);
        if (state == NUM_INVALID)
        {
            return JSON_ERROR_SYNTAX;
        }
        if (byte == (uint8_t)'.')
        {
            after_point = true;
            continue;
        }
        if (digits == JSON_FLOAT_MAX_DIGITS)
        {
            return valid_number(text) ? JSON_ERROR_BUFFER : JSON_ERROR_SYNTAX;
        }
        if (byte != (uint8_t)'0' && first_nonzero == SIZE_MAX)
        {
            first_nonzero = digits;
        }
        ++digits;
        if (after_point)
        {
            ++fraction;
        }
        big_add_digit(numerator, (uint8_t)(byte - (uint8_t)'0'));
    }
    if (!parse_exponent(text, index, state, &exponent))
    {
        return JSON_ERROR_SYNTAX;
    }
    if (first_nonzero == SIZE_MAX)
    {
        return JSON_ERROR_NONE;
    }
    exponent -= (int32_t)fraction;
    int32_t order = exponent + (int32_t)(digits - first_nonzero - 1U);
    if (order < -46 || order > 38)
    {
        return JSON_ERROR_RANGE;
    }
    while (exponent > 0)
    {
        big_multiply(numerator, 10U);
        --exponent;
    }
    while (exponent < 0)
    {
        big_multiply(denominator, 10U);
        ++exponent;
    }
    return JSON_ERROR_NONE;
}

/* Clinger fast path: exact integer and exact power-of-ten operands,
 * followed by one binary32 operation rounded to nearest. Other evaluation
 * or rounding modes use the integer-only conversion below.
 */
static bool parse_float_fast(json_span_t text, float *value)
{
    static const float powers[] = {1.0F, 1e1F, 1e2F, 1e3F, 1e4F, 1e5F,
                                   1e6F, 1e7F, 1e8F, 1e9F, 1e10F};
    if (text.data == NULL || text.length == 0U)
    {
        return false;
    }
    bool negative = text.data[0] == '-';
    size_t index = negative ? 1U : 0U;
    uint8_t state = negative ? NUM_MINUS : NUM_START;
    uint32_t mantissa = 0U;
    size_t digits = 0U;
    int32_t fraction = 0;
    int32_t exponent = 0;
    bool after_point = false;
    while (index < text.length && text.data[index] != 'e' &&
           text.data[index] != 'E')
    {
        uint8_t byte = (uint8_t)text.data[index++];
        state = number_next(state, byte);
        if (state == NUM_INVALID)
        {
            return false;
        }
        if (byte == (uint8_t)'.')
        {
            after_point = true;
            continue;
        }
        uint32_t digit = byte - (uint8_t)'0';
        if (++digits > JSON_FLOAT_MAX_DIGITS ||
            mantissa > (16777216U - digit) / 10U)
        {
            return false;
        }
        mantissa = mantissa * 10U + digit;
        if (after_point)
        {
            ++fraction;
        }
    }
    if (!parse_exponent(text, index, state, &exponent))
    {
        return false;
    }
    exponent -= fraction;
    if (exponent < -10 || exponent > 10 || FLT_EVAL_METHOD != 0 ||
        fegetround() != FE_TONEAREST)
    {
        return false;
    }
    float result = (float)mantissa;
    if (exponent < 0)
    {
        result /= powers[(size_t)-exponent];
    }
    else
    {
        result *= powers[(size_t)exponent];
    }
    *value = negative ? -result : result;
    return true;
}

static json_error_t parse_float_exact(json_span_t text, float *value)
{
    big_uint_t numerator;
    big_uint_t denominator;
    bool negative;
    json_error_t error =
        decimal_ratio(text, &numerator, &denominator, &negative);
    if (error != JSON_ERROR_NONE)
    {
        return error;
    }
    uint32_t lower = 0U;
    uint32_t upper = 0x7F7FFFFFU;
    if (!(numerator.used == 1U && numerator.words[0] == 0U))
    {
        while (lower < upper)
        {
            uint32_t middle = lower + (upper - lower + 1U) / 2U;
            if (compare_binary(&numerator, &denominator, float_mantissa(middle),
                               float_exponent(middle)) >= 0)
            {
                lower = middle;
            }
            else
            {
                upper = middle - 1U;
            }
        }
        int32_t rounding = compare_binary(&numerator, &denominator,
                                          float_mantissa(lower) * 2U + 1U,
                                          float_exponent(lower) - 1);
        if (rounding > 0 || (rounding == 0 && (lower & 1U) != 0U))
        {
            ++lower;
        }
        if (lower == 0U || lower == 0x7F800000U)
        {
            return JSON_ERROR_RANGE;
        }
    }
    lower |= negative ? 0x80000000U : 0U;
    memcpy(value, &lower, sizeof(lower));
    return JSON_ERROR_NONE;
}

json_error_t json_token_float(const json_token_t *token, float *value)
{
    if (token == NULL || value == NULL)
    {
        return JSON_ERROR_ARGUMENT;
    }
    if (token->type != JSON_NUMBER)
    {
        return JSON_ERROR_TYPE;
    }
    return parse_float_fast(token->text, value)
               ? JSON_ERROR_NONE
               : parse_float_exact(token->text, value);
}

#endif

static size_t format_unsigned(char *output, uint32_t value)
{
    char reverse[10];
    size_t length = 0U;
    do
    {
        reverse[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    for (size_t index = 0U; index < length; ++index)
    {
        output[index] = reverse[length - index - 1U];
    }
    return length;
}

#if JSON_ENABLE_FLOAT
static int32_t float_digits(uint32_t bits, char *digits, int32_t decimals,
                            size_t *count)
{
    big_uint_t numerator;
    big_uint_t denominator;
    big_uint_t scaled;
    int32_t binary_exponent = float_exponent(bits);
    int32_t exponent = 0;
    big_init(&numerator, float_mantissa(bits));
    big_init(&denominator, 1U);
    if (binary_exponent >= 0)
    {
        big_shift(&numerator, (uint32_t)binary_exponent);
    }
    else
    {
        big_shift(&denominator, (uint32_t)-binary_exponent);
    }
    scaled = denominator;
    big_multiply(&scaled, 10U);
    while (big_compare(&numerator, &scaled) >= 0)
    {
        denominator = scaled;
        big_multiply(&scaled, 10U);
        ++exponent;
    }
    while (big_compare(&numerator, &denominator) < 0)
    {
        big_multiply(&numerator, 10U);
        --exponent;
    }
    int32_t places = decimals < 0 ? 9 : exponent + 1 + decimals;
    if (places <= 0)
    {
        big_multiply(&denominator, 5U);
        bool round_up =
            places == 0 && big_compare(&numerator, &denominator) > 0;
        digits[0] = round_up ? '1' : '0';
        *count = 1U;
        return round_up ? -decimals : 0;
    }
    *count = (size_t)places;
    for (size_t index = 0U; index < *count; ++index)
    {
        digits[index] = '0';
        while (big_compare(&numerator, &denominator) >= 0)
        {
            big_subtract(&numerator, &denominator);
            ++digits[index];
        }
        if (index + 1U < *count)
        {
            big_multiply(&numerator, 10U);
        }
    }
    big_multiply(&numerator, 2U);
    int32_t rounding = big_compare(&numerator, &denominator);
    if (rounding > 0 || (rounding == 0 && (digits[*count - 1U] & 1) != 0))
    {
        size_t index = *count;
        while (index != 0U && digits[index - 1U] == '9')
        {
            digits[--index] = '0';
        }
        if (index == 0U)
        {
            digits[0] = '1';
            ++exponent;
        }
        else
        {
            ++digits[index - 1U];
        }
    }
    return exponent;
}

static size_t format_float(char *output, uint32_t bits)
{
    char digits[9];
    size_t length = 0U;
    if ((bits & 0x80000000U) != 0U)
    {
        output[length++] = '-';
    }
    bits &= 0x7FFFFFFFU;
    if (bits == 0U)
    {
        output[length++] = '0';
        return length;
    }
    size_t count;
    int32_t exponent = float_digits(bits, digits, -1, &count);
    while (count > 1U && digits[count - 1U] == '0')
    {
        --count;
    }
    if (exponent >= -4 && exponent < 9)
    {
        int32_t point = exponent + 1;
        if (point <= 0)
        {
            output[length++] = '0';
            output[length++] = '.';
            for (int32_t index = 0; index < -point; ++index)
            {
                output[length++] = '0';
            }
        }
        for (size_t index = 0U;
             index < count || (point > 0 && index < (size_t)point); ++index)
        {
            if (point > 0 && index == (size_t)point)
            {
                output[length++] = '.';
            }
            output[length++] = index < count ? digits[index] : '0';
        }
    }
    else
    {
        output[length++] = digits[0];
        if (count > 1U)
        {
            output[length++] = '.';
        }
        for (size_t index = 1U; index < count; ++index)
        {
            output[length++] = digits[index];
        }
        output[length++] = 'e';
        if (exponent < 0)
        {
            output[length++] = '-';
            exponent = -exponent;
        }
        length += format_unsigned(output + length, (uint32_t)exponent);
    }
    return length;
}

#endif

static json_status_t writer_error(json_writer_t *writer, json_error_t error)
{
    if (writer->error == JSON_ERROR_NONE)
    {
        writer->error = error;
        writer->error_offset = writer->total;
    }
    return JSON_ERROR;
}

json_status_t json_writer_init(json_writer_t *writer, char *output,
                               size_t capacity)
{
    if (writer == NULL)
    {
        return JSON_ERROR;
    }
    memset(writer, 0, sizeof(*writer));
    return json_writer_set_output(writer, output, capacity);
}

json_status_t json_writer_init_count(json_writer_t *writer)
{
    json_status_t status = json_writer_init(writer, NULL, 0U);
    if (status == JSON_OK)
    {
        writer->count_only = true;
    }
    return status;
}

json_status_t json_writer_set_output(json_writer_t *writer, char *output,
                                     size_t capacity)
{
    if (writer == NULL)
    {
        return JSON_ERROR;
    }
    if (writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (writer->count_only || (output == NULL && capacity != 0U))
    {
        return writer_error(writer, JSON_ERROR_ARGUMENT);
    }
    writer->output = output;
    writer->capacity = capacity;
    writer->used = 0U;
    return JSON_OK;
}

static json_status_t writer_ready(json_writer_t *writer)
{
    if (writer == NULL || writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    return writer->pending != PENDING_NONE || writer->string_open
               ? writer_error(writer, JSON_ERROR_STATE)
               : JSON_OK;
}

static bool emit_byte(json_writer_t *writer, char byte)
{
    if (writer->total == SIZE_MAX)
    {
        (void)writer_error(writer, JSON_ERROR_SIZE);
        return false;
    }
    if (!writer->count_only)
    {
        if (writer->used == writer->capacity)
        {
            return false;
        }
        writer->output[writer->used++] = byte;
    }
    ++writer->total;
    return true;
}

static size_t emit_bytes(json_writer_t *writer, const char *data, size_t count)
{
    size_t length = count;
    if (length > SIZE_MAX - writer->total)
    {
        length = SIZE_MAX - writer->total;
    }
    if (!writer->count_only)
    {
        /* write_source already limits count to the output capacity. */
        if (length != 0U)
        {
            memcpy(writer->output + writer->used, data, length);
            writer->used += length;
        }
    }
    writer->total += length;
    if (length != count && writer->total == SIZE_MAX)
    {
        (void)writer_error(writer, JSON_ERROR_SIZE);
    }
    return length;
}

static void prepare_escape(json_writer_t *writer, uint8_t byte)
{
    static const char hex[] = "0123456789abcdef";
    writer->escape_index = 0U;
    writer->escape[0] = '\\';
    if (byte == (uint8_t)'"' || byte == (uint8_t)'\\')
    {
        writer->escape[1] = (char)byte;
        writer->escape_length = 2U;
    }
    else
    {
        memcpy(writer->escape, "\\u00", 4U);
        writer->escape[4] = hex[byte >> 4U];
        writer->escape[5] = hex[byte & 0x0FU];
        writer->escape_length = 6U;
    }
}

static json_status_t output_wait(json_writer_t *writer)
{
    return writer->error == JSON_ERROR_NONE ? JSON_NEED_OUTPUT : JSON_ERROR;
}

static json_status_t write_source(json_writer_t *writer)
{
    while (writer->source_index < writer->source.length ||
           writer->escape_index < writer->escape_length)
    {
        while (writer->escape_index < writer->escape_length)
        {
            if (!emit_byte(writer, writer->escape[writer->escape_index]))
            {
                return output_wait(writer);
            }
            ++writer->escape_index;
        }
        if (writer->source_index == writer->source.length)
        {
            break;
        }
        if (writer->total == SIZE_MAX)
        {
            return writer_error(writer, JSON_ERROR_SIZE);
        }
        size_t count = writer->source.length - writer->source_index;
        if (!writer->count_only && count > writer->capacity - writer->used)
        {
            count = writer->capacity - writer->used;
        }
        if (count == 0U)
        {
            return JSON_NEED_OUTPUT;
        }
        const char *data = writer->source.data + writer->source_index;
        if (writer->pending == PENDING_STRING)
        {
            size_t index = 0U;
            while (index < count && (uint8_t)data[index] >= 0x20U &&
                   data[index] != '"' && data[index] != '\\')
            {
                ++index;
            }
            count = index;
            if (count == 0U)
            {
                prepare_escape(writer, (uint8_t)data[0]);
                ++writer->source_index;
                continue;
            }
        }
        size_t written = emit_bytes(writer, data, count);
        writer->source_index += written;
        if (written != count)
        {
            return output_wait(writer);
        }
    }
    return JSON_OK;
}

json_status_t json_writer_resume(json_writer_t *writer)
{
    if (writer == NULL || writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (writer->pending == PENDING_NONE)
    {
        return JSON_OK;
    }
    while (writer->prefix_index < writer->prefix_length)
    {
        if (!emit_byte(writer, writer->prefix[writer->prefix_index]))
        {
            return output_wait(writer);
        }
        ++writer->prefix_index;
    }
    json_status_t status = write_source(writer);
    if (status != JSON_OK)
    {
        return status;
    }
    while (writer->suffix[writer->suffix_index] != '\0')
    {
        if (!emit_byte(writer, writer->suffix[writer->suffix_index]))
        {
            return output_wait(writer);
        }
        ++writer->suffix_index;
    }
    writer->pending = PENDING_NONE;
    return JSON_OK;
}

static void prepare_output(json_writer_t *writer, json_span_t text, bool string,
                           bool key)
{
    writer->source = text;
    writer->source_index = 0U;
    writer->prefix_index = 0U;
    writer->escape_index = 0U;
    writer->escape_length = 0U;
    writer->suffix_index = 0U;
    writer->pending = string ? PENDING_STRING : PENDING_RAW;
    writer->suffix = string ? (key ? "\":" : "\"") : "";
    if (string)
    {
        writer->prefix[writer->prefix_length++] = '"';
    }
}

static json_status_t writer_value(json_writer_t *writer)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    writer->prefix_length = 0U;
    if (writer->depth == 0U)
    {
        if (writer->started)
        {
            return writer_error(writer, JSON_ERROR_STATE);
        }
        writer->started = true;
        return JSON_OK;
    }
    uint8_t *state = &writer->stack[writer->depth - 1U];
    switch (*state)
    {
    case WRITE_OBJECT_VALUE:
        *state = WRITE_OBJECT_NEXT;
        break;
    case WRITE_ARRAY_FIRST:
        *state = WRITE_ARRAY_NEXT;
        break;
    case WRITE_ARRAY_NEXT:
        writer->prefix[writer->prefix_length++] = ',';
        break;
    default:
        return writer_error(writer, JSON_ERROR_STATE);
    }
    return JSON_OK;
}

static bool valid_utf8(json_span_t text)
{
    uint8_t remaining = 0U;
    uint32_t codepoint = 0U;
    uint32_t minimum = 0U;
    if (text.data == NULL && text.length != 0U)
    {
        return false;
    }
    for (size_t index = 0U; index < text.length; ++index)
    {
        if ((uint8_t)text.data[index] < 0x80U && remaining == 0U)
        {
            continue;
        }
        if (!utf8_byte((uint8_t)text.data[index], &remaining, &codepoint,
                       &minimum))
        {
            return false;
        }
    }
    return remaining == 0U;
}

json_status_t json_write_key(json_writer_t *writer, json_span_t key)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (!valid_utf8(key))
    {
        return writer_error(writer, JSON_ERROR_UTF8);
    }
    if (writer->depth == 0U)
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    uint8_t *state = &writer->stack[writer->depth - 1U];
    if (*state != WRITE_OBJECT_FIRST && *state != WRITE_OBJECT_NEXT)
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    writer->prefix_length = 0U;
    if (*state == WRITE_OBJECT_NEXT)
    {
        writer->prefix[writer->prefix_length++] = ',';
    }
    *state = WRITE_OBJECT_VALUE;
    prepare_output(writer, key, true, true);
    return json_writer_resume(writer);
}

json_status_t json_write_string(json_writer_t *writer, json_span_t value)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (!valid_utf8(value))
    {
        return writer_error(writer, JSON_ERROR_UTF8);
    }
    if (writer_value(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    prepare_output(writer, value, true, false);
    return json_writer_resume(writer);
}

json_status_t json_write_string_begin(json_writer_t *writer)
{
    if (writer_value(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    json_span_t empty = {NULL, 0U};
    prepare_output(writer, empty, true, false);
    writer->suffix = "";
    writer->string_open = true;
    return json_writer_resume(writer);
}

json_status_t json_write_string_chunk(json_writer_t *writer, json_span_t value)
{
    if (writer == NULL || writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (!writer->string_open || writer->pending != PENDING_NONE)
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    if (!valid_utf8(value))
    {
        return writer_error(writer, JSON_ERROR_UTF8);
    }
    writer->prefix_length = 0U;
    prepare_output(writer, value, true, false);
    writer->prefix_length = 0U;
    writer->suffix = "";
    return json_writer_resume(writer);
}

json_status_t json_write_string_end(json_writer_t *writer)
{
    if (writer == NULL || writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (!writer->string_open || writer->pending != PENDING_NONE)
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    writer->string_open = false;
    writer->prefix_length = 0U;
    json_span_t empty = {NULL, 0U};
    prepare_output(writer, empty, true, false);
    writer->suffix = "";
    return json_writer_resume(writer);
}

static json_status_t write_raw(json_writer_t *writer, json_span_t text)
{
    if (writer_value(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    prepare_output(writer, text, false, false);
    return json_writer_resume(writer);
}

static json_status_t write_begin(json_writer_t *writer, bool object)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (writer->depth == JSON_MAX_DEPTH)
    {
        return writer_error(writer, JSON_ERROR_DEPTH);
    }
    if (writer_value(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    writer->stack[writer->depth++] =
        object ? WRITE_OBJECT_FIRST : WRITE_ARRAY_FIRST;
    json_span_t text = {object ? "{" : "[", 1U};
    prepare_output(writer, text, false, false);
    return json_writer_resume(writer);
}

static json_status_t write_end(json_writer_t *writer, bool object)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (writer->depth == 0U)
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    uint8_t state = writer->stack[writer->depth - 1U];
    if (object ? (state != WRITE_OBJECT_FIRST && state != WRITE_OBJECT_NEXT)
               : (state != WRITE_ARRAY_FIRST && state != WRITE_ARRAY_NEXT))
    {
        return writer_error(writer, JSON_ERROR_STATE);
    }
    --writer->depth;
    writer->prefix_length = 0U;
    json_span_t text = {object ? "}" : "]", 1U};
    prepare_output(writer, text, false, false);
    return json_writer_resume(writer);
}

json_status_t json_write_object_begin(json_writer_t *writer)
{
    return write_begin(writer, true);
}

json_status_t json_write_object_end(json_writer_t *writer)
{
    return write_end(writer, true);
}

json_status_t json_write_array_begin(json_writer_t *writer)
{
    return write_begin(writer, false);
}

json_status_t json_write_array_end(json_writer_t *writer)
{
    return write_end(writer, false);
}

json_status_t json_write_bool(json_writer_t *writer, bool value)
{
    json_span_t text = {value ? "true" : "false", value ? 4U : 5U};
    return write_raw(writer, text);
}

json_status_t json_write_null(json_writer_t *writer)
{
    json_span_t text = {"null", 4U};
    return write_raw(writer, text);
}

json_status_t json_write_uint32(json_writer_t *writer, uint32_t value)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    json_span_t text = {writer->number, format_unsigned(writer->number, value)};
    return write_raw(writer, text);
}

json_status_t json_write_int32(json_writer_t *writer, int32_t value)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    size_t length = 0U;
    uint32_t magnitude = (uint32_t)value;
    if (value < 0)
    {
        writer->number[length++] = '-';
        magnitude = 0U - magnitude;
    }
    length += format_unsigned(writer->number + length, magnitude);
    json_span_t text = {writer->number, length};
    return write_raw(writer, text);
}

json_status_t json_write_number(json_writer_t *writer, json_span_t number)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (!valid_number(number))
    {
        return writer_error(writer, JSON_ERROR_SYNTAX);
    }
    return write_raw(writer, number);
}

#if JSON_ENABLE_FLOAT
json_status_t json_write_float(json_writer_t *writer, float value)
{
    uint32_t bits;
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7F800000U) == 0x7F800000U)
    {
        return writer_error(writer, JSON_ERROR_RANGE);
    }
    json_span_t text = {writer->number, format_float(writer->number, bits)};
    return write_raw(writer, text);
}

static size_t format_float_fixed(char *output, uint32_t bits, uint8_t decimals)
{
    char digits[39U + JSON_FLOAT_MAX_DECIMALS];
    size_t count = 1U;
    size_t length = 0U;
    int32_t exponent = 0;
    if ((bits & 0x80000000U) != 0U)
    {
        output[length++] = '-';
    }
    bits &= 0x7FFFFFFFU;
    digits[0] = '0';
    if (bits != 0U)
    {
        exponent = float_digits(bits, digits, (int32_t)decimals, &count);
    }
    int32_t first = exponent > 0 ? exponent : 0;
    for (int32_t place = first; place >= -(int32_t)decimals; --place)
    {
        if (place == -1)
        {
            output[length++] = '.';
        }
        int32_t index = exponent - place;
        output[length++] =
            index >= 0 && (size_t)index < count ? digits[(size_t)index] : '0';
    }
    return length;
}

json_status_t json_write_float_fixed(json_writer_t *writer, float value,
                                     uint8_t decimals)
{
    if (writer_ready(writer) != JSON_OK)
    {
        return JSON_ERROR;
    }
    if (decimals > JSON_FLOAT_MAX_DECIMALS)
    {
        return writer_error(writer, JSON_ERROR_ARGUMENT);
    }
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7F800000U) == 0x7F800000U)
    {
        return writer_error(writer, JSON_ERROR_RANGE);
    }
    json_span_t text = {writer->number,
                        format_float_fixed(writer->number, bits, decimals)};
    return write_raw(writer, text);
}

#endif

json_status_t json_writer_finish(json_writer_t *writer)
{
    if (writer == NULL || writer->error != JSON_ERROR_NONE)
    {
        return JSON_ERROR;
    }
    if (writer->pending != PENDING_NONE)
    {
        return JSON_NEED_OUTPUT;
    }
    if (!writer->started || writer->depth != 0U || writer->string_open)
    {
        return writer_error(writer, JSON_ERROR_INCOMPLETE);
    }
    return JSON_DONE;
}

/*** end of file ***/
