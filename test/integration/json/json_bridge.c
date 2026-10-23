/*
 * json_bridge.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only ABI bridge to test the production public JSON API from Python.
 */
#include "json.h"

#include <string.h>

typedef struct
{
    size_t input_chunk;
    size_t output_chunk;
} json_test_options_t;

static json_status_t write_token(json_writer_t *writer,
                                 const json_token_t *token)
{
    switch (token->type)
    {
        case JSON_OBJECT_BEGIN: return json_write_object_begin(writer);
        case JSON_OBJECT_END: return json_write_object_end(writer);
        case JSON_ARRAY_BEGIN: return json_write_array_begin(writer);
        case JSON_ARRAY_END: return json_write_array_end(writer);
        case JSON_KEY: return json_write_key(writer, token->text);
        case JSON_STRING: return json_write_string(writer, token->text);
        case JSON_NUMBER: return json_write_number(writer, token->text);
        case JSON_BOOLEAN: return json_write_bool(writer, token->boolean);
        case JSON_NULL: return json_write_null(writer);
        default: return JSON_ERROR;
    }
}

typedef struct
{
    char *data;
    size_t capacity;
    size_t used;
    char segment[256];
    size_t chunk;
} test_sink_t;

static bool flush(json_writer_t *writer, test_sink_t *sink)
{
    if (writer->used > sink->capacity - sink->used)
    {
        return false;
    }
    memcpy(sink->data + sink->used, sink->segment, writer->used);
    sink->used += writer->used;
    return json_writer_set_output(writer, sink->segment,
                                   sink->chunk) == JSON_OK;
}

static bool transfer(json_reader_t *reader, json_writer_t *writer,
                     test_sink_t *sink)
{
    json_token_t token;
    for (;;)
    {
        json_status_t status = json_read(reader, &token);
        if (status == JSON_NEED_INPUT || status == JSON_DONE)
        {
            return true;
        }
        if (status != JSON_OK)
        {
            return false;
        }
        status = write_token(writer, &token);
        while (status == JSON_NEED_OUTPUT)
        {
            if (!flush(writer, sink))
            {
                return false;
            }
            status = json_writer_resume(writer);
        }
        if (status != JSON_OK)
        {
            return false;
        }
    }
}

/* Return emitted length, -1 on syntax/resource failure. Input limits are
 * deliberately outside the JSON core and are only this host bridge's limit.
 */
int32_t json_test_transcode(const char *data, size_t length,
                            char *output, size_t capacity,
                            const json_test_options_t *options)
{
    json_reader_t reader;
    json_writer_t writer;
    char scratch[8192];
    test_sink_t sink = {output, capacity, 0U, {0}, options->output_chunk};
    if (options->input_chunk == 0U || sink.chunk == 0U ||
        sink.chunk > sizeof(sink.segment)) { return -1; }
    if (json_reader_init(&reader, scratch, sizeof(scratch)) != JSON_OK ||
        json_writer_init(&writer, sink.segment, sink.chunk) != JSON_OK)
    {
        return -1;
    }
    size_t offset = 0U;
    do
    {
        size_t count = length - offset;
        if (count > options->input_chunk)
        {
            count = options->input_chunk;
        }
        json_span_t input = {data + offset, count};
        offset += count;
        if (json_reader_feed(&reader, input, offset == length) != JSON_OK ||
            !transfer(&reader, &writer, &sink)) { return -1; }
    } while (offset < length);
    if (json_writer_finish(&writer) != JSON_DONE || !flush(&writer, &sink) ||
        sink.used > INT32_MAX) { return -1; }
    return (int32_t)sink.used;
}

json_error_t json_test_float_read(const char *data, size_t length,
                                 uint32_t *bits)
{
    json_token_t token = {JSON_NUMBER, {data, length}, 0U, false};
    float value;
    json_error_t error = json_token_float(&token, &value);
    if (error == JSON_ERROR_NONE)
    {
        memcpy(bits, &value, sizeof(*bits));
    }
    return error;
}

int32_t json_test_float_write(uint32_t bits, char *data, size_t capacity)
{
    json_writer_t writer;
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (json_writer_init(&writer, data, capacity) != JSON_OK ||
        json_write_float(&writer, value) != JSON_OK ||
        json_writer_finish(&writer) != JSON_DONE) { return -1; }
    return (int32_t)writer.used;
}

json_error_t json_test_validate(const char *data, size_t length,
                                size_t chunk, size_t *error_offset)
{
    json_reader_t reader;
    json_token_t token;
    char scratch[8192];
    (void)json_reader_init(&reader, scratch, sizeof(scratch));
    if (chunk == 0U)
    {
        return JSON_ERROR_ARGUMENT;
    }
    size_t offset = 0U;
    json_status_t status = JSON_NEED_INPUT;
    do
    {
        size_t count = length - offset;
        if (count > chunk)
        {
            count = chunk;
        }
        json_span_t input = {data + offset, count};
        offset += count;
        (void)json_reader_feed(&reader, input, offset == length);
        do
        {
            status = json_read(&reader, &token);
        } while (status == JSON_OK);
    } while (status == JSON_NEED_INPUT && offset < length);
    *error_offset = reader.error_offset;
    return status == JSON_DONE ? JSON_ERROR_NONE : reader.error;
}

json_error_t json_test_skip(const char *data, size_t length,
                             size_t chunk, size_t *error_offset)
{
    json_reader_t reader;
    json_token_t token;
    (void)json_reader_init(&reader, NULL, 0U);
    if (chunk == 0U)
    {
        return JSON_ERROR_ARGUMENT;
    }
    size_t offset = 0U;
    bool skipped = false;
    json_status_t status;
    do
    {
        size_t count = length - offset;
        if (count > chunk)
        {
            count = chunk;
        }
        json_span_t input = {data + offset, count};
        offset += count;
        (void)json_reader_feed(&reader, input, offset == length);
        do
        {
            status = skipped ? json_read(&reader, &token) :
                               json_skip_value(&reader);
            if (status == JSON_OK)
            {
                skipped = true;
            }
        } while (status == JSON_OK);
    } while (status == JSON_NEED_INPUT && offset < length);
    *error_offset = reader.error_offset;
    return status == JSON_DONE ? JSON_ERROR_NONE : reader.error;
}

int32_t json_test_float_fixed(uint32_t bits, uint8_t decimals,
                              char *data, size_t capacity)
{
    json_writer_t writer;
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (json_writer_init(&writer, data, capacity) != JSON_OK ||
        json_write_float_fixed(&writer, value, decimals) != JSON_OK ||
        json_writer_finish(&writer) != JSON_DONE)
    {
        return -1;
    }
    return (int32_t)writer.used;
}

size_t json_test_reader_size(void)
{
    return sizeof(json_reader_t);
}

size_t json_test_writer_size(void)
{
    return sizeof(json_writer_t);
}

/*** end of file ***/
