/*
 * web_json.h
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * NUL-terminated HTTP buffers and named fields over the common JSON core.
 */
#ifndef WEB_JSON_H
#define WEB_JSON_H

#include "../json/json.h"
#include "xprintf.h"

#include <string.h>

static inline json_span_t web_json_span(const char *text)
{
    json_span_t span = {text, text != NULL ? strlen(text) : 1U};
    return span;
}

static inline bool web_json_init(json_writer_t *writer, char *buffer,
                                 size_t capacity)
{
    if (buffer == NULL || capacity == 0U)
    {
        return false;
    }
    buffer[0] = '\0';
    return json_writer_init(writer, buffer, capacity - 1U) == JSON_OK;
}

static inline bool web_json_finish(json_writer_t *writer, char *buffer,
                                   size_t *length)
{
    if (json_writer_finish(writer) != JSON_DONE)
    {
        buffer[0] = '\0';
        *length = 0U;
        return false;
    }
    buffer[writer->used] = '\0';
    *length = writer->used;
    return true;
}

static inline void web_json_key(json_writer_t *writer, const char *name)
{
    (void)json_write_key(writer, web_json_span(name));
}

static inline void web_json_uint32(json_writer_t *writer, const char *name,
                                   uint32_t value)
{
    web_json_key(writer, name);
    (void)json_write_uint32(writer, value);
}

static inline void web_json_int32(json_writer_t *writer, const char *name,
                                  int32_t value)
{
    web_json_key(writer, name);
    (void)json_write_int32(writer, value);
}

static inline void web_json_bool(json_writer_t *writer, const char *name,
                                 bool value)
{
    web_json_key(writer, name);
    (void)json_write_bool(writer, value);
}

static inline void web_json_string(json_writer_t *writer, const char *name,
                                   const char *value)
{
    web_json_key(writer, name);
    (void)json_write_string(writer, web_json_span(value));
}

static inline void web_json_object(json_writer_t *writer, const char *name)
{
    web_json_key(writer, name);
    (void)json_write_object_begin(writer);
}

static inline void web_json_array(json_writer_t *writer, const char *name)
{
    web_json_key(writer, name);
    (void)json_write_array_begin(writer);
}

static inline void web_json_null(json_writer_t *writer, const char *name)
{
    web_json_key(writer, name);
    (void)json_write_null(writer);
}

/* Preserve existing endpoint decimal/scientific formatting and rounding.
 * xprintf formats a number only; the common writer validates its grammar.
 */
static inline void web_json_float(json_writer_t *writer, float value,
                                  uint8_t decimals)
{
    char number[64];
    const char *format =
        decimals == 1U
            ? "%.1f"
            : (decimals == 2U ? "%.2f" : (decimals == 3U ? "%.3f" : "%.8e"));
    (void)xsnprintf(number, sizeof(number), format, (double)value);
    (void)json_write_number(writer, web_json_span(number));
}

static inline void web_json_text(json_writer_t *writer, const char *text)
{
    (void)json_write_string_chunk(writer, web_json_span(text));
}

static inline bool web_json_text_fits(const json_writer_t *writer,
                                      const char *text, size_t tail)
{
    json_writer_t counter;
    if (writer->capacity - writer->used < tail ||
        json_writer_init_count(&counter) != JSON_OK ||
        json_write_string(&counter, web_json_span(text)) != JSON_OK)
    {
        return false;
    }
    /* Counting a complete string includes two quote delimiters. */
    return counter.total - 2U <= writer->capacity - writer->used - tail;
}

#endif
/*** end of file ***/
