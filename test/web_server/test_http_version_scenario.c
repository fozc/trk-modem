/*
 * test_http_version_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real version/status capacity checks and loader/package resource selection.
 */
#include "unity.h"
#include "../support/http_handler_platform.inc"
TEST_SOURCE_FILE("json.c")
TEST_SOURCE_FILE("xprintf.c")
static int response_length, response_status;
void http_send_error(int status, const char *text)
{
    (void)text;
    response_status = status;
    response_length = 0;
}
void http_send_json(const char *body, int length);
#include "../../Application/web-server/http_handlers.c"
void http_send_json(const char *body, int length)
{
    TEST_ASSERT_TRUE(length >= 0 && length < handler_state.tx_buffer_size);
    TEST_ASSERT_EQUAL_UINT32(strlen(body), (size_t)length);
    response_length = length;
    response_status = 200;
}
static void check_response(void (*handler)(void), const char *expected)
{
    static char canvas[8194];
    const int capacities[] = {1, 2, 8, 32, 128, 8192};
    for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); i++)
    {
        int capacity = capacities[i];
        memset(canvas, 0x55, sizeof(canvas));
        canvas[0] = '*';
        canvas[capacity + 1] = '*';
        handler_state.tx_buffer = canvas + 1;
        handler_state.tx_buffer_size = capacity;
        handler();
        size_t length = strlen(expected);
        if (length > (size_t)(capacity - 1))
        {
            TEST_ASSERT_TRUE(response_status == 500 && response_length == 0);
            TEST_ASSERT_TRUE(canvas[1] == '\0');
        }
        else
        {
            TEST_ASSERT_TRUE(response_status == 200 && response_length == (int)length);
            TEST_ASSERT_TRUE(memcmp(canvas + 1, expected, length) == 0);
            TEST_ASSERT_TRUE(canvas[length + 1] == '\0');
        }
        TEST_ASSERT_TRUE(canvas[0] == '*' && canvas[capacity + 1] == '*');
    }
}
void test_version_and_update_status_preserve_lengths_guards_and_values(void)
{
    fw_update_set_version_info(NULL, "2026-10-03", "TROIKA-SCB-v1");
    char expected[256];
    snprintf(expected, sizeof(expected),
             "{\"version\":\"%u.%u.%u\",\"buildDate\":\"2026-10-03\",\"hardware\":\"TROIKA-SCB-v1\"}",
             (unsigned int)VERSION_MAJOR, (unsigned int)VERSION_MINOR, (unsigned int)VERSION_PATCH);
    check_response(handle_get_fw_version, expected);
    fw_update_set_version_info("2.3.4", "2026-10-04", "CUSTOM");
    check_response(handle_get_fw_version,
                   "{\"version\":\"2.3.4\",\"buildDate\":\"2026-10-04\",\"hardware\":\"CUSTOM\"}");
    fw_update_set_version_info(NULL, NULL, NULL);
    check_response(handle_get_fw_version,
                   "{\"version\":\"2.3.4\",\"buildDate\":\"2026-10-04\",\"hardware\":\"CUSTOM\"}");
    check_response(handle_get_fw_status, "{\"active\":false}");
    fw_state.in_progress = true;
    fw_state.received_bytes = 4294967295U;
    fw_state.total_size = 4294967295U;
    fw_state.file_hash = 4294967295U;
    check_response(handle_get_fw_status,
                   "{\"active\":true,\"received\":4294967295,\"total\":4294967295,\"fh\":4294967295}");
    fw_state.in_progress = false;
    check_response(handle_get_fw_status, "{\"active\":false}");
    fw_state.complete = true;
    check_response(handle_get_fw_status,
                   "{\"active\":false,\"ready\":true,\"received\":4294967295,\"total\":4294967295,\"fh\":4294967295}");
    puts("PASS: actual HTTP handlers and formatter preserve bytes, bounded lengths and guards");
}

static const uint8_t resource_bytes[] = {1U};
static const html_resource_t loader_resource = {.data = resource_bytes, .length = sizeof(resource_bytes)};
static const html_resource_t package_resource = {.data = resource_bytes, .length = sizeof(resource_bytes)};
static const html_resource_t *sent_resource;
static bool sent_as_package;
const html_resource_t *get_loading_html(void)
{
    return &loader_resource;
}
const html_resource_t *get_index_html(void)
{
    return &package_resource;
}
void http_send_html_resource(const html_resource_t *resource)
{
    sent_resource = resource;
    sent_as_package = false;
}
void http_send_gzip_package(const html_resource_t *resource)
{
    sent_resource = resource;
    sent_as_package = true;
}

void test_loader_and_package_handlers_choose_the_correct_resource_and_encoding(void)
{
    handle_get_loading();
    TEST_ASSERT_EQUAL_PTR(&loader_resource, sent_resource);
    TEST_ASSERT_FALSE(sent_as_package);
    handle_get_index_package();
    TEST_ASSERT_EQUAL_PTR(&package_resource, sent_resource);
    TEST_ASSERT_TRUE(sent_as_package);
}

void setUp(void)
{
}

void tearDown(void)
{
}

/*** end of file ***/
