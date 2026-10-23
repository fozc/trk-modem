/*
 * configuration.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * C/C++ client and float-disabled production API regression checks.
 */
#include "json.h"

#include <assert.h>
#include <string.h>

static const json_span_t keys[] =
{
    JSON_SPAN_INIT("name"),
    JSON_SPAN_INIT("count")
};

int main(void)
{
    char output[128];
    json_writer_t writer;
    assert(json_writer_init(&writer, output, sizeof(output)) == JSON_OK);
    assert(json_write_object_begin(&writer) == JSON_OK);
    assert(json_write_key(&writer, keys[0]) == JSON_OK);
    assert(json_write_string(&writer, JSON_SPAN_LITERAL("AY")) == JSON_OK);
    assert(json_write_key(&writer, keys[1]) == JSON_OK);
    assert(json_write_int32(&writer, INT32_MIN) == JSON_OK);
    assert(json_write_key(&writer, JSON_SPAN_LITERAL("raw")) == JSON_OK);
    assert(json_write_number(&writer, JSON_SPAN_LITERAL("1.25e2")) == JSON_OK);
    assert(json_write_object_end(&writer) == JSON_OK);
    assert(json_writer_finish(&writer) == JSON_DONE);
    const char expected[] =
        "{\"name\":\"AY\",\"count\":-2147483648,\"raw\":1.25e2}";
    assert(writer.used == sizeof(expected) - 1U);
    assert(memcmp(output, expected, writer.used) == 0);

    char scratch[32];
    json_reader_t reader;
    json_token_t token;
    json_span_t input = {output, writer.used};
    assert(json_reader_init(&reader, scratch, sizeof(scratch)) == JSON_OK);
    assert(json_reader_feed(&reader, input, true) == JSON_OK);
    assert(json_read(&reader, &token) == JSON_OK);
    assert(token.type == JSON_OBJECT_BEGIN);
    assert(json_read(&reader, &token) == JSON_OK);
    assert(json_token_equals_span(&token, keys[0]));
    assert(json_read(&reader, &token) == JSON_OK);
    assert(json_token_equals(&token, "AY"));
    assert(json_read(&reader, &token) == JSON_OK);
    assert(json_token_equals_span(&token, keys[1]));
    assert(json_read(&reader, &token) == JSON_OK);
    int32_t count = 0;
    assert(json_token_int32(&token, &count) == JSON_ERROR_NONE);
    assert(count == INT32_MIN);
    assert(json_read(&reader, &token) == JSON_OK);
    assert(json_skip_value(&reader) == JSON_OK);
    assert(json_read(&reader, &token) == JSON_OK);
    assert(token.type == JSON_OBJECT_END);
    assert(json_read(&reader, &token) == JSON_DONE);

    assert(json_writer_init(&writer, output, sizeof(output)) == JSON_OK);
    assert(json_write_uint32(&writer, UINT32_MAX) == JSON_OK);
    assert(writer.used == 10U);
    assert(memcmp(output, "4294967295", writer.used) == 0);
    assert(json_writer_finish(&writer) == JSON_DONE);
    assert(json_writer_init_count(&writer) == JSON_OK);
    assert(json_write_uint32(&writer, UINT32_MAX) == JSON_OK);
    assert(writer.total == 10U);

    assert(json_writer_init(&writer, output, 1U) == JSON_OK);
    json_status_t status = json_write_int32(&writer, INT32_MIN);
    size_t total = writer.used;
    while (status == JSON_NEED_OUTPUT)
    {
        assert(json_writer_set_output(&writer, output + total, 1U) == JSON_OK);
        status = json_writer_resume(&writer);
        total += writer.used;
    }
    assert(status == JSON_OK && total == 11U);
    assert(memcmp(output, "-2147483648", total) == 0);

    assert(json_writer_init(&writer, output, sizeof(output)) == JSON_OK);
    assert(json_write_number(&writer, JSON_SPAN_LITERAL("01")) == JSON_ERROR);
    return 0;
}
/*** end of file ***/
