/*
 * json.h
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Bounded, incremental JSON reader and writer. No platform dependency.
 */
#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 32U
#endif

#ifndef JSON_ENABLE_FLOAT
/* All translation units must use the same depth and float settings. */
#define JSON_ENABLE_FLOAT 1
#endif

#if JSON_ENABLE_FLOAT != 0 && JSON_ENABLE_FLOAT != 1
#error "JSON_ENABLE_FLOAT must be 0 or 1"
#endif

#define JSON_FLOAT_MAX_DIGITS 128U
#define JSON_FLOAT_MAX_DECIMALS 9U

    typedef enum
    {
        JSON_OK,
        JSON_DONE,
        JSON_NEED_INPUT,
        JSON_NEED_OUTPUT,
        JSON_ERROR
    } json_status_t;

    typedef enum
    {
        JSON_ERROR_NONE,
        JSON_ERROR_ARGUMENT,
        JSON_ERROR_SYNTAX,
        JSON_ERROR_INCOMPLETE,
        JSON_ERROR_DEPTH,
        JSON_ERROR_BUFFER,
        JSON_ERROR_UTF8,
        JSON_ERROR_ESCAPE,
        JSON_ERROR_TYPE,
        JSON_ERROR_RANGE,
        JSON_ERROR_STATE,
        JSON_ERROR_SIZE
    } json_error_t;

    typedef enum
    {
        JSON_OBJECT_BEGIN,
        JSON_OBJECT_END,
        JSON_ARRAY_BEGIN,
        JSON_ARRAY_END,
        JSON_KEY,
        JSON_STRING,
        JSON_NUMBER,
        JSON_BOOLEAN,
        JSON_NULL
    } json_type_t;

    typedef struct
    {
        const char *data;
        size_t length;
    } json_span_t;

/* String literals only. INIT also works in static/global initializers. */
#define JSON_SPAN_INIT(text)                                                   \
    {                                                                          \
        ("" text ""), sizeof(text) - 1U                                        \
    }
#ifdef __cplusplus
#define JSON_SPAN_LITERAL(text) json_span_t JSON_SPAN_INIT(text)
#else
#define JSON_SPAN_LITERAL(text) ((json_span_t)JSON_SPAN_INIT(text))
#endif

    typedef struct
    {
        json_type_t type;
        json_span_t text;
        size_t offset;
        bool boolean;
    } json_token_t;

    /* Public storage for static/stack allocation; treat fields as read-only.
     * Error and error_offset are sticky until init. Input must stay alive until
     * consumed == input.length. Token text lasts until the next read/skip call.
     * Duplicate keys are emitted in order; field policy belongs to the caller.
     */
    typedef struct
    {
        json_span_t input;
        size_t consumed;
        size_t offset;
        size_t error_offset;
        size_t token_offset;
        char *buffer;
        size_t capacity;
        size_t length;
        size_t literal_index;
        const char *literal;
        size_t depth;
        size_t skip_depth;
        uint32_t codepoint;
        uint32_t utf8_min;
        json_type_t type;
        json_error_t error;
        uint16_t high_surrogate;
        uint8_t utf8_remaining;
        uint8_t digits;
        uint8_t escape;
        uint8_t mode;
        uint8_t number_state;
        uint8_t stack[JSON_MAX_DEPTH];
        bool final;
        bool started;
        bool skipping;
    } json_reader_t;

    typedef struct
    {
        char *output;
        size_t capacity;
        size_t used;
        size_t total;
        size_t error_offset;
        size_t depth;
        json_span_t source;
        size_t source_index;
        size_t prefix_length;
        size_t prefix_index;
        size_t escape_length;
        size_t escape_index;
        const char *suffix;
        size_t suffix_index;
        json_error_t error;
        uint8_t stack[JSON_MAX_DEPTH];
        bool started;
        bool count_only;
        bool string_open;
        uint8_t pending;
        char prefix[3];
        char escape[6];
#if JSON_ENABLE_FLOAT
        /* Fixed binary32: sign + 39 integer digits + point + decimals. */
        char number[41U + JSON_FLOAT_MAX_DECIMALS];
#else
    /* Sign plus ten decimal digits for int32_t/uint32_t. */
    char number[11];
#endif
    } json_writer_t;

    json_status_t json_reader_init(json_reader_t *reader, char *buffer,
                                   size_t capacity);
    /* Feed only after the old input is consumed. final seals the input stream.
     * Feed/read never retain pointers to json_token_t itself. Empty chunks
     * work.
     */
    json_status_t json_reader_feed(json_reader_t *reader, json_span_t input,
                                   bool final);
    json_status_t json_read(json_reader_t *reader, json_token_t *token);
    /* Skip the next value (after its KEY, if any). Resume with this same call
     * on NEED_INPUT. Syntax/UTF-8/depth are validated without token storage.
     */
    json_status_t json_skip_value(json_reader_t *reader);

    bool json_token_equals(const json_token_t *token, const char *text);
    /* Key/string only. Embedded NUL is compared; NULL/0 is an empty span. */
    bool json_token_equals_span(const json_token_t *token, json_span_t text);
    json_error_t json_token_copy_string(const json_token_t *token, char *output,
                                        size_t capacity);
    json_error_t json_token_bool(const json_token_t *token, bool *value);
    json_error_t json_token_uint32(const json_token_t *token, uint32_t *value);
    json_error_t json_token_int32(const json_token_t *token, int32_t *value);
    json_error_t json_token_uint16(const json_token_t *token, uint16_t *value);
    json_error_t json_token_int16(const json_token_t *token, int16_t *value);
    json_error_t json_token_uint8(const json_token_t *token, uint8_t *value);
    json_error_t json_token_int8(const json_token_t *token, int8_t *value);
#if JSON_ENABLE_FLOAT
    json_error_t json_token_float(const json_token_t *token, float *value);
#endif

    json_status_t json_writer_init(json_writer_t *writer, char *output,
                                   size_t capacity);
    json_status_t json_writer_init_count(json_writer_t *writer);
    /* On NEED_OUTPUT, consume output[0..used), supply another segment and
     * resume. Do not repeat the starting operation. Strings must live until
     * resume OK. Output is length-delimited, NOT NUL-terminated. No bytes are
     * rolled back.
     */
    json_status_t json_writer_set_output(json_writer_t *writer, char *output,
                                         size_t capacity);
    json_status_t json_writer_resume(json_writer_t *writer);
    json_status_t json_writer_finish(json_writer_t *writer);
    json_status_t json_write_object_begin(json_writer_t *writer);
    json_status_t json_write_object_end(json_writer_t *writer);
    json_status_t json_write_array_begin(json_writer_t *writer);
    json_status_t json_write_array_end(json_writer_t *writer);
    json_status_t json_write_key(json_writer_t *writer, json_span_t key);
    json_status_t json_write_string(json_writer_t *writer, json_span_t value);
    /* Each chunk must contain complete UTF-8 sequences. Resume before the next
     * operation on NEED_OUTPUT; keep its source alive until resume returns OK.
     */
    json_status_t json_write_string_begin(json_writer_t *writer);
    json_status_t json_write_string_chunk(json_writer_t *writer,
                                          json_span_t value);
    json_status_t json_write_string_end(json_writer_t *writer);
    json_status_t json_write_bool(json_writer_t *writer, bool value);
    json_status_t json_write_null(json_writer_t *writer);
    json_status_t json_write_uint32(json_writer_t *writer, uint32_t value);
    json_status_t json_write_int32(json_writer_t *writer, int32_t value);
#if JSON_ENABLE_FLOAT
    json_status_t json_write_float(json_writer_t *writer, float value);
    /* Exactly decimals digits after the point (0..JSON_FLOAT_MAX_DECIMALS).
     * Round to nearest, ties to even; preserve negative zero. No exponent.
     */
    json_status_t json_write_float_fixed(json_writer_t *writer, float value,
                                         uint8_t decimals);
#endif
    /* Strict number spelling, no conversion (e.g. forwarding an input token).
     * There is intentionally no unchecked raw object/string injection API.
     */
    json_status_t json_write_number(json_writer_t *writer, json_span_t number);

#ifdef __cplusplus
}
#endif

#endif
/*** end of file ***/
