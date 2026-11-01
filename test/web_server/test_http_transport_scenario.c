/*
 * test_http_transport_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Whole HTTP/transport modules with controlled asynchronous modem completion and stream ownership.
 */
#include "unity.h"
#include "../support/web_transport_headers.inc"
TEST_SOURCE_FILE("http_server.c")
TEST_SOURCE_FILE("http_response.c")
TEST_SOURCE_FILE("web_server.c")
TEST_SOURCE_FILE("gsm_http_server.c")
TEST_SOURCE_FILE("http_request_parser.c")
TEST_SOURCE_FILE("ring_buff.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("sha256.c")
static uint8_t socket_state = SOCKET_CONNECTED;
static uint32_t tx_pending;
static uint32_t close_count;
static uint16_t pending_length;
static uint8_t pending_data[1500];
static char wire[131072];
static size_t wire_length;
static bool authenticated;
static uint32_t hash_calls;
void __real_sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void __wrap_sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    hash_calls++;
    __real_sha256_update(ctx, data, len);
}
extern web_server_io_t io;
#define transport io
static bool reject_tx;
static uint32_t rfwu_received;
static char handler_buffer[4096];
#include "../support/web_transport_resources.inc"
static const uint8_t page_data[] = {1U, 2U, 3U};
static const html_resource_t page = {page_data, sizeof(page_data), HTML_COMPRESS_GZIP, "text/html", NULL};

static const html_resource_t *selected_page = &page;

uint32_t gsm_tx_is_ready(void)
{
    return tx_pending;
}
uint8_t gsm_get_listener_socket_state(void)
{
    return socket_state;
}
uint32_t gsm_send_to_socket(const void *data, uint16_t length, uint32_t direction, bool close_after_tx, bool crypto)
{
    if (reject_tx)
        return 1U;
    TEST_ASSERT_TRUE(0U == tx_pending && sizeof(pending_data) >= length);
    TEST_ASSERT_TRUE(GSM_TX_DIR_LISTENER_SOCKET == direction);
    TEST_ASSERT_TRUE(!close_after_tx && !crypto);
    memcpy(pending_data, data, length);
    pending_length = length;
    tx_pending = 1U;
    return 0U;
}
void gsm_listener_socket_event_handler(gsm_listener_id_t id, gsm_user_event_t event)
{
    TEST_ASSERT_TRUE(GSM_LISTENER_WEB == id && GSM_USER_EVENT_CLOSE_SOCKET == event);
    TEST_ASSERT_TRUE(0U == tx_pending);
    close_count++;
    socket_state = SOCKET_CLOSED;
    gsm_http_server_reset();
    webserver_on_connection_closed();
}
void rfwu_init(int (*send_callback)(const void *, int))
{
    (void)send_callback;
}
void rfwu_on_receive(const uint8_t *data, int length)
{
    (void)data;
    rfwu_received += (uint32_t)length;
}
void rfwu_on_disconnect(void)
{
}
void http_handlers_init(char *buffer, int length)
{
    (void)buffer;
    (void)length;
    authenticated = false;
}
void http_handlers_reset(void)
{
    authenticated = false;
}
void http_handlers_set_auth_from_token(const char *query)
{
    authenticated = NULL != query && 0 == strcmp(query, "t=valid");
}
bool http_handlers_is_authenticated(void)
{
    return authenticated;
}
bool http_handlers_is_admin(void)
{
    return authenticated;
}
void http_handlers_set_query_string(const char *query)
{
    (void)query;
}
char *http_handlers_get_tx_buffer(int *length)
{
    *length = (int)sizeof(handler_buffer);
    return handler_buffer;
}
void handle_post_login(const char *body)
{
    (void)body;
    http_send_json("{}", 2);
}
void handle_get_index(void)
{
    http_send_html_resource(selected_page);
}
void reboot_system_delayed(uint32_t delay)
{
    (void)delay;
}
const html_resource_t *get_index_html(void)
{
    return selected_page;
}
const html_resource_t *get_fw_update_html(void)
{
    return &page;
}
const html_resource_t *get_loading_html(void)
{
    return &real_loading;
}
const html_resource_t *get_default_html(void)
{
    return &page;
}

static void complete_tx(void)
{
    TEST_ASSERT_TRUE(0U != tx_pending);
    TEST_ASSERT_TRUE(sizeof(wire) > wire_length + pending_length);
    memcpy(wire + wire_length, pending_data, pending_length);
    wire_length += pending_length;
    wire[wire_length] = '\0';
    tx_pending = 0U;
}
static void reset_test(void)
{
    webserver_on_connection_closed();
    reject_tx = false;
    tx_pending = 0U;
    close_count = 0U;
    socket_state = SOCKET_CONNECTED;
    wire_length = 0U;
    wire[0] = '\0';
    gsm_http_server_reset();
    gsm_http_server_init();
}
static void receive_request(const char *request)
{
    size_t length = strlen(request);
    TEST_ASSERT_TRUE(UINT16_MAX >= length);
    gsm_http_server_client_data_received((const uint8_t *)request, (uint16_t)length);
    gsm_http_server_process(); /* idle -> receive */
    gsm_http_server_process(); /* parse, route and queue the response */
    TEST_ASSERT_TRUE(0U == close_count);
    gsm_http_server_process(); /* enqueue the modem TX */
    TEST_ASSERT_TRUE(0U != tx_pending && 0U == close_count);
}
static void unauthorized_request(void)
{
    wire_length = 0U;
    receive_request("GET /status/rf-group HTTP/1.1\r\n\r\n");
    gsm_http_server_process(); /* busy TX must prevent close */
    TEST_ASSERT_TRUE(0U == close_count);
    complete_tx();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "HTTP/1.1 401 "));
    TEST_ASSERT_TRUE(NULL != strstr(wire, "\r\n\r\nUnauthorized"));
    gsm_http_server_process();
}
static void finish_response(void)
{
    for (size_t step = 0U; 128U > step; step++)
    {
        if (0U != tx_pending)
            complete_tx();
        gsm_http_server_process();
    }
    TEST_ASSERT_TRUE(0U == tx_pending);
}

static const uint8_t large_static_body[100U * 1024U + 1U] = {0x83U, [100U * 1024U] = 0x7dU};

static void check_static_stream(void)
{
    const uint32_t lengths[] = {1U, 1499U, 1500U, 1501U, 3000U, sizeof(large_static_body)};
    for (size_t index = 0U; sizeof(lengths) / sizeof(lengths[0]) > index; index++)
    {
        reset_test();
        const html_resource_t resource = {large_static_body, lengths[index], HTML_COMPRESS_GZIP, "text/html", NULL};
        selected_page = &resource;
        receive_request("GET /index.html HTTP/1.1\r\n\r\n");
        TEST_ASSERT_TRUE(-1 == transport.send_static("replace", 7U));
        TEST_ASSERT_TRUE(-1 == transport.send("interleave", 10U));
        gsm_http_server_close_after_response();
        gsm_http_server_process();
        TEST_ASSERT_TRUE(0U == close_count); /* Header still in flight. */
        complete_tx();
        reject_tx = true;
        gsm_http_server_process(); /* Rejected body must not advance. */
        TEST_ASSERT_TRUE(0U == tx_pending && 0U == close_count);
        reject_tx = false;
        finish_response();
        const char *body = strstr(wire, "\r\n\r\n");
        TEST_ASSERT_TRUE(NULL != body);
        const size_t header_length = (size_t)(body + 4 - wire);
        TEST_ASSERT_TRUE(wire_length == header_length + lengths[index]);
        TEST_ASSERT_TRUE(0 == memcmp(body + 4, large_static_body, lengths[index]));
        TEST_ASSERT_TRUE(1U == close_count);
    }
    selected_page = &page;
    puts("PASS: 100 KiB static stream, chunk boundaries, busy/rejected TX and final completion");
}

static void check_json_copy_and_rejected_queue_send(void)
{
    reset_test();
    static char json[HTTP_TX_BUFFER_SIZE];
    memset(json, ' ', sizeof(json));
    json[0] = '[';
    json[sizeof(json) - 1U] = ']';
    http_send_json(json, (int)sizeof(json));
    memset(json, 'x', sizeof(json)); /* Caller may reuse its mutable buffer. */
    gsm_http_server_process();
    reject_tx = true;
    gsm_http_server_process();
    TEST_ASSERT_TRUE(0U == tx_pending);
    reject_tx = false;
    finish_response();
    const char *body = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(NULL != body);
    TEST_ASSERT_TRUE(wire_length == (size_t)(body + 4 - wire) + sizeof(json));
    TEST_ASSERT_TRUE('[' == body[4] && ']' == wire[wire_length - 1U]);
    for (size_t index = 5U; sizeof(json) + 3U > index; index++)
    {
        TEST_ASSERT_TRUE(' ' == body[index]);
    }
    puts("PASS: full 8 KiB JSON is copied; modem rejection preserves the ring");
}

static void check_stream_disconnect_and_pipelining(void)
{
    const html_resource_t resource = {large_static_body, sizeof(large_static_body), HTML_COMPRESS_GZIP, "text/html",
                                      NULL};
    const char request[] = "GET /status/board?t=valid HTTP/1.1\r\n\r\n";
    for (uint32_t chunks = 0U; 3U > chunks; chunks++)
    {
        reset_test();
        selected_page = &resource;
        receive_request("GET /index.html HTTP/1.1\r\n\r\n");
        for (uint32_t index = 0U; chunks > index; index++)
        {
            complete_tx();
            gsm_http_server_process();
        }
        socket_state = SOCKET_CLOSED;
        gsm_http_server_process();
        tx_pending = 0U;
        reset_test();
        selected_page = &page;
        receive_request(request);
        finish_response();
        TEST_ASSERT_TRUE(NULL != strstr(wire, "\r\n\r\n{}"));
        TEST_ASSERT_TRUE(1024U > wire_length);
    }
    reset_test();
    selected_page = &resource;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    gsm_http_server_client_data_received((const uint8_t *)request, (uint16_t)(sizeof(request) - 1U));
    finish_response();
    const char *body = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(NULL != body);
    TEST_ASSERT_TRUE(0 == memcmp(body + 4, large_static_body, sizeof(large_static_body)));
    const char *second = body + 4 + sizeof(large_static_body);
    TEST_ASSERT_TRUE(0 == strncmp(second, "HTTP/1.1 200", 12U));
    reset_test();
    selected_page = &resource;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    for (uint32_t index = 0U; 2U > index; index++)
    {
        gsm_http_server_client_data_received((const uint8_t *)request, (uint16_t)(sizeof(request) - 1U));
    }
    finish_response();
    TEST_ASSERT_TRUE(1U == close_count);
    body = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(wire_length == (size_t)(body + 4 - wire) + sizeof(large_static_body));
    selected_page = &page;
    puts("PASS: disconnect/reset discards static cursor; pipelined responses never interleave");
}

static void check_rfwu_copy_transport(void)
{
    reset_test();
    rfwu_received = 0U;
    const uint8_t request[] = {0x52U, 0x46U, 0x57U, 0x55U, 4U, 0U, 0U, 0U};
    gsm_http_server_client_data_received(request, sizeof(request));
    gsm_http_server_process();
    gsm_http_server_process();
    TEST_ASSERT_TRUE(sizeof(request) == rfwu_received);
    uint8_t reply[33]; /* Largest current STATUS response: 12 + 17 + 4. */
    memset(reply, 0x5a, sizeof(reply));
    TEST_ASSERT_TRUE((int)sizeof(reply) == transport.send(reply, sizeof(reply)));
    memset(reply, 0, sizeof(reply));
    finish_response();
    TEST_ASSERT_TRUE(sizeof(reply) == wire_length);
    for (size_t index = 0U; wire_length > index; index++)
    {
        TEST_ASSERT_TRUE(0x5a == (uint8_t)wire[index]);
    }
    puts("PASS: production RFWU demultiplexer retains copied response transport");
}

static char conditional[160];
static const char *boundary;

static void check_unauthorized_streak(void)
{
    reset_test();
    unauthorized_request();
    unauthorized_request();
    TEST_ASSERT_TRUE(0U == close_count);
    unauthorized_request();
    TEST_ASSERT_TRUE(1U == close_count);
    puts("PASS: third 401 is delivered before close; modem TX must finish");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    gsm_http_server_reset();
    http_server_reset();
    unauthorized_request();
    unauthorized_request();
    TEST_ASSERT_TRUE(0U == close_count);
    unauthorized_request();
    TEST_ASSERT_TRUE(1U == close_count);
    puts("PASS: reconnect resets the unauthorized streak");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    web_server_init(&transport);
    unauthorized_request();
    TEST_ASSERT_TRUE(0U == close_count);
    puts("PASS: server initialization resets the unauthorized streak");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    receive_request("GET /config/device?t=valid HTTP/1.1\r\n\r\n");
    complete_tx();
    gsm_http_server_process();
    http_server_on_receive(NULL, 0);
    TEST_ASSERT_TRUE(authenticated);
    unauthorized_request();
    unauthorized_request();
    TEST_ASSERT_TRUE(0U == close_count);
    puts("PASS: authenticated request resets streak; invalid input is inert");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    receive_request("POST /auth/login HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}");
    complete_tx();
    gsm_http_server_process();
    unauthorized_request();
    unauthorized_request();
    TEST_ASSERT_TRUE(0U == close_count);
    puts("PASS: public login remains accessible and resets the streak");
}

static void check_chunk_completion_and_cache(void)
{

    reset_test();
    static char response_body[3500];
    memset(response_body, 'x', sizeof(response_body));
    http_send_response(200, "OK", "text/plain", response_body, (int)sizeof(response_body));
    gsm_http_server_close_after_response();
    gsm_http_server_process();
    for (size_t chunk = 0U; 3U > chunk; chunk++)
    {
        gsm_http_server_process();
        TEST_ASSERT_TRUE(0U != tx_pending && 0U == close_count);
        gsm_http_server_process();
        TEST_ASSERT_TRUE(0U == close_count);
        complete_tx();
    }
    gsm_http_server_process();
    TEST_ASSERT_TRUE(1U == close_count);
    char *payload = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(NULL != payload);
    TEST_ASSERT_TRUE(sizeof(response_body) == strlen(payload + 4));
    TEST_ASSERT_TRUE(0 == memcmp(payload + 4, response_body, sizeof(response_body)));
    puts("PASS: all response chunks finish before close");

    reset_test();
    gsm_http_server_close_after_response();
    gsm_http_server_reset();
    for (size_t tick = 0U; 4U > tick; tick++)
        gsm_http_server_process();
    TEST_ASSERT_TRUE(0U == close_count);
    socket_state = SOCKET_CLOSED;
    gsm_http_server_close_after_response();
    for (size_t tick = 0U; 4U > tick; tick++)
        gsm_http_server_process();
    socket_state = SOCKET_CONNECTED;
    for (size_t tick = 0U; 4U > tick; tick++)
        gsm_http_server_process();
    TEST_ASSERT_TRUE(0U == close_count);
    puts("PASS: disconnect/reset clears deferred close for the next client");
    reset_test();
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    const char *line = strstr(wire, "ETag: ");
    TEST_ASSERT_TRUE(NULL != line);
    char tag[67];
    memcpy(tag, line + 6, 66U);
    tag[66] = '\0';
    snprintf(conditional, sizeof(conditional), "GET /index.html HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", tag);
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "304 Not Modified"));
    boundary = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(NULL != boundary && wire_length == (size_t)(boundary - wire) + 4U);
    wire_length = 0U;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    TEST_ASSERT_TRUE(0 == memcmp(wire + wire_length - sizeof(page_data), page_data, sizeof(page_data)));
    puts("PASS: real GET router/transport emits 304 without body and clears stale request context");
}

static void check_real_resources_and_loader(void)
{

    const html_resource_t *resources[] = {&real_index, &real_fw_update};
    hash_calls = 0U;
    for (size_t index = 0U; 2U > index; index++)
    {
        reset_test();
        selected_page = resources[index];
        receive_request("GET /index.html HTTP/1.1\r\n\r\n");
        finish_response();
        const char *body = strstr(wire, "\r\n\r\n");
        TEST_ASSERT_TRUE(NULL != body);
        body += 4;
        const size_t received = wire_length - (size_t)(body - wire);
        if (selected_page->length != received)
        {
            fprintf(stderr, "HTML truncated: expected %lu, received %lu\n", (unsigned long)selected_page->length,
                    (unsigned long)received);
        }
        TEST_ASSERT_TRUE(selected_page->length == received);
        TEST_ASSERT_TRUE(0 == memcmp(body, selected_page->data, received));
        TEST_ASSERT_TRUE(0U == close_count);
    }
    puts("PASS: actual embedded index and firmware pages reach the modem byte-for-byte");
    selected_page = &page;

    reset_test();
    receive_request("GET /loading.html HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    TEST_ASSERT_TRUE(NULL != strstr(wire, "Content-Encoding: gzip"));
    const char *loading_body = strstr(wire, "\r\n\r\n") + 4;
    TEST_ASSERT_TRUE(real_loading.length == wire_length - (size_t)(loading_body - wire));
    TEST_ASSERT_TRUE(0 == memcmp(loading_body, real_loading.data, real_loading.length));

    reset_test();
    receive_request("GET / HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    TEST_ASSERT_TRUE(0 == memcmp(wire + wire_length - real_loading.length, real_loading.data, real_loading.length));
    reset_test();
    receive_request("GET /index.html.gz HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "HTTP/1.1 401 "));
    puts("PASS: root serves loader; application package requires a session");
}

static void check_protected_package_cache(void)
{

    reset_test();
    selected_page = &real_index;
    receive_request("GET /index.html.gz?t=valid HTTP/1.1\r\n\r\n");
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    TEST_ASSERT_TRUE(NULL != strstr(wire, "Content-Type: application/gzip"));
    TEST_ASSERT_TRUE(NULL == strstr(wire, "Content-Encoding:"));
    const char *package_body = strstr(wire, "\r\n\r\n") + 4;
    TEST_ASSERT_TRUE(real_index.length == wire_length - (size_t)(package_body - wire));
    TEST_ASSERT_TRUE(0 == memcmp(package_body, real_index.data, real_index.length));
    puts("PASS: public loading page and protected raw gzip use production handlers and exact bytes");
    const char *package_tag_line = strstr(wire, "ETag: ");
    TEST_ASSERT_TRUE(NULL != package_tag_line);
    char package_tag[67];
    memcpy(package_tag, package_tag_line + 6, 66U);
    package_tag[66] = '\0';
    snprintf(conditional, sizeof(conditional), "GET /index.html.gz?t=valid HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n",
             package_tag);
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "304 Not Modified"));
    TEST_ASSERT_TRUE(NULL != strstr(wire, "Cache-Control: private, no-cache"));
    boundary = strstr(wire, "\r\n\r\n");
    TEST_ASSERT_TRUE(NULL != boundary && wire_length == (size_t)(boundary - wire) + 4U);
    selected_page = &real_fw_update;
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "200 OK"));
    TEST_ASSERT_TRUE(0 ==
                     memcmp(wire + wire_length - real_fw_update.length, real_fw_update.data, real_fw_update.length));
    wire_length = 0U;
    snprintf(conditional, sizeof(conditional), "GET /index.html.gz HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", package_tag);
    receive_request(conditional);
    finish_response();
    TEST_ASSERT_TRUE(NULL != strstr(wire, "HTTP/1.1 401 "));
    puts("PASS: validated package saves body bytes; changed content sends new bytes; ETag never bypasses auth");
    TEST_ASSERT_TRUE(0U == hash_calls);
    puts("PASS: generated HTML/package ETags need no runtime SHA-256 calls");
}

static void check_queue_boundaries_and_disconnects(void)
{

    const html_resource_t invalid_package = {page_data, sizeof(page_data), HTML_COMPRESS_NONE, "text/html", NULL};
    const html_resource_t *invalid_resources[] = {NULL, &invalid_package};
    for (size_t index = 0U; 2U > index; index++)
    {
        reset_test();
        selected_page = invalid_resources[index];
        receive_request("GET /index.html.gz?t=valid HTTP/1.1\r\n\r\n");
        finish_response();
        TEST_ASSERT_TRUE(NULL != strstr(wire, "500 Internal Server Error"));
    }
    selected_page = &page;
    puts("PASS: unavailable/non-gzip application package fails explicitly");

    reset_test();
    static uint8_t full_queue[10U * 1024U - 1U];
    memset(full_queue, 'q', sizeof(full_queue));
    TEST_ASSERT_TRUE((int)sizeof(full_queue) == transport.send(full_queue, sizeof(full_queue)));
    TEST_ASSERT_TRUE(-1 == transport.send("x", 1U));
    TEST_ASSERT_TRUE(-1 == transport.send(NULL, 1U));
    finish_response();
    TEST_ASSERT_TRUE(sizeof(full_queue) == wire_length);
    TEST_ASSERT_TRUE(0 == memcmp(wire, full_queue, sizeof(full_queue)));
    puts("PASS: 10 KiB ring capacity (one reserved byte); rejected writes preserve queued data");

    reset_test();
    static uint8_t first_block[9000];
    static uint8_t second_block[4000];
    for (size_t index = 0U; sizeof(first_block) > index; index++)
    {
        first_block[index] = (uint8_t)(index % 251U);
    }
    for (size_t index = 0U; sizeof(second_block) > index; index++)
    {
        second_block[index] = (uint8_t)(index % 239U);
    }
    TEST_ASSERT_TRUE((int)sizeof(first_block) == transport.send(first_block, sizeof(first_block)));
    while (4000U > wire_length)
    {
        gsm_http_server_process();
        if (0U != tx_pending)
            complete_tx();
    }
    TEST_ASSERT_TRUE((int)sizeof(second_block) == transport.send(second_block, sizeof(second_block)));
    finish_response();
    TEST_ASSERT_TRUE(sizeof(first_block) + sizeof(second_block) == wire_length);
    TEST_ASSERT_TRUE(0 == memcmp(wire, first_block, sizeof(first_block)));
    TEST_ASSERT_TRUE(0 == memcmp(wire + sizeof(first_block), second_block, sizeof(second_block)));
    puts("PASS: queue wraps across 10 KiB while earlier data is still pending");

    reset_test();
    TEST_ASSERT_TRUE((int)sizeof(first_block) == transport.send(first_block, sizeof(first_block)));
    gsm_http_server_reset();
    TEST_ASSERT_TRUE(3 == transport.send("new", 3U));
    finish_response();
    TEST_ASSERT_TRUE(3U == wire_length && 0 == memcmp(wire, "new", 3U));
    puts("PASS: reset discards the old response before the next client");

    check_rfwu_copy_transport();
    check_static_stream();
    check_json_copy_and_rejected_queue_send();
    check_stream_disconnect_and_pipelining();
}

void test_http_routes_and_modem_transport_preserve_existing_scenarios(void)
{
    check_unauthorized_streak();
    check_chunk_completion_and_cache();
    check_real_resources_and_loader();
    check_protected_package_cache();
    check_queue_boundaries_and_disconnects();
}


void handle_get_fw_update(void)
{
    http_send_json("{}", 2);
}
void handle_post_logout(void)
{
    http_send_json("{}", 2);
}
void handle_post_echo(const char *body, int body_length)
{
    (void)body;
    (void)body_length;
    http_send_json("{}", 2);
}
void handle_get_device_config_json(void)
{
    http_send_json("{}", 2);
}
void handle_post_device_config_json(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_get_board_status_json(void)
{
    http_send_json("{}", 2);
}
void handle_get_syslogs_json(void)
{
    http_send_json("{}", 2);
}
void handle_get_fault_records_json(void)
{
    http_send_json("{}", 2);
}
void handle_get_iec_config_json(void)
{
    http_send_json("{}", 2);
}
void handle_post_iec_config_json(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_get_modbus_config_json(void)
{
    http_send_json("{}", 2);
}
void handle_post_modbus_config_json(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_get_rf_config_json(void)
{
    http_send_json("{}", 2);
}
void handle_get_rf_discovery_json(void)
{
    http_send_json("{}", 2);
}
void handle_post_rf_config_json(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_post_rf_alarm_ack(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_get_rf_group_status_json(void)
{
    http_send_json("{}", 2);
}
void handle_post_rf_apply(const char *suffix)
{
    (void)suffix;
    http_send_json("{}", 2);
}
void handle_post_rf_abort(void)
{
    http_send_json("{}", 2);
}
void handle_get_rf_monitor_json(void)
{
    http_send_json("{}", 2);
}
void handle_get_rf_monitor_line_json(int line_id)
{
    (void)line_id;
    http_send_json("{}", 2);
}
void handle_get_fw_version(void)
{
    http_send_json("{}", 2);
}
void handle_get_fw_status(void)
{
    http_send_json("{}", 2);
}
void handle_fw_start(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_fw_chunk(uint32_t offset, const uint8_t *data, uint32_t size, bool has_checksum, uint16_t expected_checksum)
{
    (void)offset;
    (void)data;
    (void)size;
    (void)has_checksum;
    (void)expected_checksum;
    http_send_json("{}", 2);
}
void handle_fw_finish(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_fw_reboot(void)
{
    http_send_json("{}", 2);
}
void handle_fw_apply(void)
{
    http_send_json("{}", 2);
}
void handle_post_web_shell(const char *json_body)
{
    (void)json_body;
    http_send_json("{}", 2);
}
void handle_get_web_shell(void)
{
    http_send_json("{}", 2);
}
/* Handler boundaries; real loader/package selection is checked in test_http_version_scenario.c. */
void handle_get_loading(void)
{
    http_send_html_resource(get_loading_html());
}
void handle_get_index_package(void)
{
    http_send_gzip_package(get_index_html());
}
void setUp(void)
{
}

void tearDown(void)
{
}

/*** end of file ***/
