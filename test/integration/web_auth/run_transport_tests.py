"""Exercise complete production HTTP/transport modules with modem doubles.

Only platform includes are replaced. Request parsing, routing, response
formatting, the TX queue and HTTP transport state machine remain production code.
No ports are opened; modem TX completion is explicitly controlled by the test.
"""

import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="gcc")
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
output = root / "test/build/web_auth/transport"
output.mkdir(parents=True, exist_ok=True)

header = r'''
#ifndef HTTP_TRANSPORT_TEST_H
#define HTTP_TRANSPORT_TEST_H
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_server.h"
#include "http_handlers.h"
#include "http_response.h"
#include "sha256.h"
#include "web_server.h"
#include "raw_tcp_fw_update.h"
#include "index_html.h"
#include "fw_update_html.h"
#include "loading_html.h"
#include "ring_buff.h"
#include "xprintf.h"
#define CSLOG(...) ((void)0)
#define CSLOG_ERR(...) ((void)0)
#define CSLOG_WARN(...) ((void)0)
#define CSLOG_NODT(...) ((void)0)
#define CCSLOG(...) ((void)0)
/* Modem boundary values have no hardware meaning in this host double. */
typedef enum { GSM_LISTENER_WEB } gsm_listener_id_t;
typedef enum { GSM_USER_EVENT_CLOSE_SOCKET } gsm_user_event_t;
#define SOCKET_CLOSED 0U
#define SOCKET_CONNECTED 1U
#define GSM_TX_DIR_LISTENER_SOCKET 1U
uint32_t gsm_tx_is_ready(void);
uint32_t gsm_get_listener_socket_state(void);
uint32_t gsm_send_to_socket(const void *data, uint16_t length,
    uint32_t direction, bool close_after_tx, bool crypto);
void gsm_listener_socket_event_handler(gsm_listener_id_t id,
    gsm_user_event_t event);
void reboot_system_delayed(uint32_t delay);
void gsm_http_server_init(void);
void gsm_http_server_reset(void);
void gsm_http_server_process(void);
void gsm_http_server_close_after_response(void);
void gsm_http_server_client_data_received(const uint8_t *data,
    uint16_t length);
#endif
'''
(output / "transport_test.h").write_text(header, encoding="utf-8")

# Compile whole current modules, with platform dependencies declared above.
sources = []
for relative in ["Application/web-server/http_server.c",
                 "Application/web-server/http_response.c",
                 "Application/web-server/web_server.c",
                 "Application/gsm/gsm_http_server.c"]:
    source = (root / relative).read_text(encoding="utf-8")
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    target = output / Path(relative).name
    target.write_text('#include "transport_test.h"\n' + source,
                      encoding="utf-8")
    sources.append(str(target))

# Unused endpoint handlers are neighboring-module doubles, with signatures
# taken from the real public header. The tested auth gate is never replaced.
handlers = (root / "Application/web-server/http_handlers.h").read_text(
    encoding="utf-8")
stubs = []
for name, parameters in re.findall(
        r'void ((?:handle_get_|handle_post_|handle_fw_)\w+)\(([^;]*?)\);',
        handlers, flags=re.S):
    if name in ("handle_post_login", "handle_get_index",
                "handle_get_loading", "handle_get_index_package"):
        continue
    ignored = []
    if parameters.strip() != "void":
        for parameter in parameters.split(','):
            argument = re.search(r'(\w+)\s*$', parameter)[1]
            ignored.append(f"(void){argument};")
    stubs.append(f"void {name}({parameters}) {{ {' '.join(ignored)} "
                 'http_send_json("{}", 2); }')

resources = []
for name in ["index", "fw_update", "loading"]:
    embedded = (root / f"Application/web-server/{name}_html.c").read_text(encoding="utf-8")
    payload = bytes(int(value, 16) for value in re.findall(
        r"0x([0-9a-fA-F]{2})", embedded.split("};")[0]))
    resources.append(f"static const uint8_t real_{name}_data[] = {{" +
                     ",".join(str(value) for value in payload) + "};\n" +
                     f"static const html_resource_t real_{name} = " +
                     "{real_" + name + "_data, sizeof(real_" + name +
                     "_data), HTML_COMPRESS_GZIP, \"text/html\"};\n")
(output / "real_resources.h").write_text("".join(resources), encoding="ascii")

production_handlers = (root / "Application/web-server/http_handlers.c").read_text(
    encoding="utf-8")
for name in ["handle_get_loading", "handle_get_index_package"]:
    match = re.search(r'^void ' + name + r'\(void\)\n\{.*?\n\}',
                      production_handlers, flags=re.M | re.S)
    if not match:
        raise RuntimeError("Cannot extract production handler: " + name)
    stubs.append(match.group(0))

test = r'''
/*
 * HTTP transport regression harness.
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "transport_test.h"
static uint32_t socket_state = SOCKET_CONNECTED;
static uint32_t tx_pending;
static uint32_t close_count;
static uint16_t pending_length;
static uint8_t pending_data[1500];
static char wire[131072];
static size_t wire_length;
static bool authenticated;
extern web_server_io_t io;
#define transport io
static bool reject_tx;
static uint32_t rfwu_received;
static char handler_buffer[4096];
#include "real_resources.h"
static const uint8_t page_data[] = {1U, 2U, 3U};
static const html_resource_t page =
    {page_data, sizeof(page_data), HTML_COMPRESS_GZIP, "text/html"};

static const html_resource_t *selected_page = &page;

uint32_t gsm_tx_is_ready(void) { return tx_pending; }
uint32_t gsm_get_listener_socket_state(void) { return socket_state; }
uint32_t gsm_send_to_socket(const void *data, uint16_t length,
    uint32_t direction, bool close_after_tx, bool crypto)
{
    if (reject_tx) return 1U;
    assert(0U == tx_pending && sizeof(pending_data) >= length);
    assert(GSM_TX_DIR_LISTENER_SOCKET == direction);
    assert(!close_after_tx && !crypto);
    memcpy(pending_data, data, length);
    pending_length = length;
    tx_pending = 1U;
    return 0U;
}
void gsm_listener_socket_event_handler(gsm_listener_id_t id,
    gsm_user_event_t event)
{
    assert(GSM_LISTENER_WEB == id && GSM_USER_EVENT_CLOSE_SOCKET == event);
    assert(0U == tx_pending);
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
void rfwu_on_disconnect(void) { }
void http_handlers_init(char *buffer, int length)
{
    (void)buffer;
    (void)length;
    authenticated = false;
}
void http_handlers_reset(void) { authenticated = false; }
void http_handlers_set_auth_from_token(const char *query)
{
    authenticated = NULL != query && 0 == strcmp(query, "t=valid");
}
bool http_handlers_is_authenticated(void) { return authenticated; }
bool http_handlers_is_admin(void) { return authenticated; }
void http_handlers_set_query_string(const char *query) { (void)query; }
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
void handle_get_index(void) { http_send_html_resource(selected_page); }
void reboot_system_delayed(uint32_t delay) { (void)delay; }
const html_resource_t *get_index_html(void) { return selected_page; }
const html_resource_t *get_fw_update_html(void) { return &page; }
const html_resource_t *get_loading_html(void) { return &real_loading; }
const html_resource_t *get_default_html(void) { return &page; }

static void complete_tx(void)
{
    assert(0U != tx_pending);
    assert(sizeof(wire) > wire_length + pending_length);
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
    assert(UINT16_MAX >= length);
    gsm_http_server_client_data_received((const uint8_t *)request,
                                        (uint16_t)length);
    gsm_http_server_process(); /* idle -> receive */
    gsm_http_server_process(); /* parse, route and queue the response */
    assert(0U == close_count);
    gsm_http_server_process(); /* enqueue the modem TX */
    assert(0U != tx_pending && 0U == close_count);
}
static void unauthorized_request(void)
{
    wire_length = 0U;
    receive_request("GET /status/rf-group HTTP/1.1\r\n\r\n");
    gsm_http_server_process(); /* busy TX must prevent close */
    assert(0U == close_count);
    complete_tx();
    assert(NULL != strstr(wire, "HTTP/1.1 401 "));
    assert(NULL != strstr(wire, "\r\n\r\nUnauthorized"));
    gsm_http_server_process();
}
static void finish_response(void)
{
    for (size_t step = 0U; 128U > step; step++)
    {
        if (0U != tx_pending) complete_tx();
        gsm_http_server_process();
    }
    assert(0U == tx_pending);
}

static const uint8_t large_static_body[100U * 1024U + 1U] =
    {0x83U, [100U * 1024U] = 0x7dU};

static void check_static_stream(void)
{
    const uint32_t lengths[] = {1U, 1499U, 1500U, 1501U, 3000U,
                               sizeof(large_static_body)};
    for (size_t index = 0U; sizeof(lengths) / sizeof(lengths[0]) > index; index++)
    {
        reset_test();
        const html_resource_t resource = {large_static_body, lengths[index],
                                         HTML_COMPRESS_GZIP, "text/html"};
        selected_page = &resource;
        receive_request("GET /index.html HTTP/1.1\r\n\r\n");
        assert(-1 == transport.send_static("replace", 7U));
        assert(-1 == transport.send("interleave", 10U));
        gsm_http_server_close_after_response();
        gsm_http_server_process();
        assert(0U == close_count); /* Header still in flight. */
        complete_tx();
        reject_tx = true;
        gsm_http_server_process(); /* Rejected body must not advance. */
        assert(0U == tx_pending && 0U == close_count);
        reject_tx = false;
        finish_response();
        const char *body = strstr(wire, "\r\n\r\n");
        assert(NULL != body);
        const size_t header_length = (size_t)(body + 4 - wire);
        assert(wire_length == header_length + lengths[index]);
        assert(0 == memcmp(body + 4, large_static_body, lengths[index]));
        assert(1U == close_count);
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
    assert(0U == tx_pending);
    reject_tx = false;
    finish_response();
    const char *body = strstr(wire, "\r\n\r\n");
    assert(NULL != body);
    assert(wire_length == (size_t)(body + 4 - wire) + sizeof(json));
    assert('[' == body[4] && ']' == wire[wire_length - 1U]);
    for (size_t index = 5U; sizeof(json) + 3U > index; index++)
    {
        assert(' ' == body[index]);
    }
    puts("PASS: full 8 KiB JSON is copied; modem rejection preserves the ring");
}

static void check_stream_disconnect_and_pipelining(void)
{
    const html_resource_t resource = {large_static_body,
        sizeof(large_static_body), HTML_COMPRESS_GZIP, "text/html"};
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
        assert(NULL != strstr(wire, "\r\n\r\n{}"));
        assert(1024U > wire_length);
    }
    reset_test();
    selected_page = &resource;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    gsm_http_server_client_data_received((const uint8_t *)request,
                                         (uint16_t)(sizeof(request) - 1U));
    finish_response();
    const char *body = strstr(wire, "\r\n\r\n");
    assert(NULL != body);
    assert(0 == memcmp(body + 4, large_static_body, sizeof(large_static_body)));
    const char *second = body + 4 + sizeof(large_static_body);
    assert(0 == strncmp(second, "HTTP/1.1 200", 12U));
    reset_test();
    selected_page = &resource;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    for (uint32_t index = 0U; 2U > index; index++)
    {
        gsm_http_server_client_data_received((const uint8_t *)request,
                                             (uint16_t)(sizeof(request) - 1U));
    }
    finish_response();
    assert(1U == close_count);
    body = strstr(wire, "\r\n\r\n");
    assert(wire_length == (size_t)(body + 4 - wire) + sizeof(large_static_body));
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
    assert(sizeof(request) == rfwu_received);
    uint8_t reply[33]; /* Largest current STATUS response: 12 + 17 + 4. */
    memset(reply, 0x5a, sizeof(reply));
    assert((int)sizeof(reply) == transport.send(reply, sizeof(reply)));
    memset(reply, 0, sizeof(reply));
    finish_response();
    assert(sizeof(reply) == wire_length);
    for (size_t index = 0U; wire_length > index; index++)
    {
        assert(0x5a == (uint8_t)wire[index]);
    }
    puts("PASS: production RFWU demultiplexer retains copied response transport");
}

int main(void)
{
    reset_test();
    unauthorized_request();
    unauthorized_request();
    assert(0U == close_count);
    unauthorized_request();
    assert(1U == close_count);
    puts("PASS: third 401 is delivered before close; modem TX must finish");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    gsm_http_server_reset();
    http_server_reset();
    unauthorized_request();
    unauthorized_request();
    assert(0U == close_count);
    unauthorized_request();
    assert(1U == close_count);
    puts("PASS: reconnect resets the unauthorized streak");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    web_server_init(&transport);
    unauthorized_request();
    assert(0U == close_count);
    puts("PASS: server initialization resets the unauthorized streak");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    receive_request("GET /config/device?t=valid HTTP/1.1\r\n\r\n");
    complete_tx();
    gsm_http_server_process();
    http_server_on_receive(NULL, 0);
    assert(authenticated);
    unauthorized_request();
    unauthorized_request();
    assert(0U == close_count);
    puts("PASS: authenticated request resets streak; invalid input is inert");

    reset_test();
    unauthorized_request();
    unauthorized_request();
    receive_request("POST /auth/login HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}");
    complete_tx();
    gsm_http_server_process();
    unauthorized_request();
    unauthorized_request();
    assert(0U == close_count);
    puts("PASS: public login remains accessible and resets the streak");

    reset_test();
    static char body[3500];
    memset(body, 'x', sizeof(body));
    http_send_response(200, "OK", "text/plain", body, (int)sizeof(body));
    gsm_http_server_close_after_response();
    gsm_http_server_process();
    for (size_t chunk = 0U; 3U > chunk; chunk++)
    {
        gsm_http_server_process();
        assert(0U != tx_pending && 0U == close_count);
        gsm_http_server_process();
        assert(0U == close_count);
        complete_tx();
    }
    gsm_http_server_process();
    assert(1U == close_count);
    char *payload = strstr(wire, "\r\n\r\n");
    assert(NULL != payload);
    assert(sizeof(body) == strlen(payload + 4));
    assert(0 == memcmp(payload + 4, body, sizeof(body)));
    puts("PASS: all response chunks finish before close");

    reset_test();
    gsm_http_server_close_after_response();
    gsm_http_server_reset();
    for (size_t tick = 0U; 4U > tick; tick++) gsm_http_server_process();
    assert(0U == close_count);
    socket_state = SOCKET_CLOSED;
    gsm_http_server_close_after_response();
    for (size_t tick = 0U; 4U > tick; tick++) gsm_http_server_process();
    socket_state = SOCKET_CONNECTED;
    for (size_t tick = 0U; 4U > tick; tick++) gsm_http_server_process();
    assert(0U == close_count);
    puts("PASS: disconnect/reset clears deferred close for the next client");
    reset_test();
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    const char *line = strstr(wire, "ETag: ");
    assert(NULL != line);
    char tag[67];
    memcpy(tag, line + 6, 66U);
    tag[66] = '\0';
    char conditional[160];
    snprintf(conditional, sizeof(conditional),
        "GET /index.html HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", tag);
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    assert(NULL != strstr(wire, "304 Not Modified"));
    const char *boundary = strstr(wire, "\r\n\r\n");
    assert(NULL != boundary && wire_length == (size_t)(boundary - wire) + 4U);
    wire_length = 0U;
    receive_request("GET /index.html HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    assert(0 == memcmp(wire + wire_length - sizeof(page_data),
                       page_data, sizeof(page_data)));
    puts("PASS: real GET router/transport emits 304 without body and clears stale request context");

    const html_resource_t *resources[] = {&real_index, &real_fw_update};
    for (size_t index = 0U; 2U > index; index++)
    {
        reset_test();
        selected_page = resources[index];
        receive_request("GET /index.html HTTP/1.1\r\n\r\n");
        finish_response();
        const char *body = strstr(wire, "\r\n\r\n");
        assert(NULL != body);
        body += 4;
        const size_t received = wire_length - (size_t)(body - wire);
        if (selected_page->length != received)
        {
            fprintf(stderr, "HTML truncated: expected %lu, received %lu\n",
                    (unsigned long)selected_page->length,
                    (unsigned long)received);
        }
        assert(selected_page->length == received);
        assert(0 == memcmp(body, selected_page->data, received));
        assert(0U == close_count);
    }
    puts("PASS: actual embedded index and firmware pages reach the modem byte-for-byte");
    selected_page = &page;

    reset_test();
    receive_request("GET /loading.html HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    assert(NULL != strstr(wire, "Content-Encoding: gzip"));
    const char *loading_body = strstr(wire, "\r\n\r\n") + 4;
    assert(real_loading.length == wire_length -
           (size_t)(loading_body - wire));
    assert(0 == memcmp(loading_body, real_loading.data, real_loading.length));

    reset_test();
    receive_request("GET / HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    assert(0 == memcmp(wire + wire_length - real_loading.length,
                       real_loading.data, real_loading.length));
    reset_test();
    receive_request("GET /index.html.gz HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "HTTP/1.1 401 "));
    puts("PASS: root serves loader; application package requires a session");

    reset_test();
    selected_page = &real_index;
    receive_request("GET /index.html.gz?t=valid HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    assert(NULL != strstr(wire, "Content-Type: application/gzip"));
    assert(NULL == strstr(wire, "Content-Encoding:"));
    const char *package_body = strstr(wire, "\r\n\r\n") + 4;
    assert(real_index.length == wire_length -
           (size_t)(package_body - wire));
    assert(0 == memcmp(package_body, real_index.data, real_index.length));
    puts("PASS: public loading page and protected raw gzip use production handlers and exact bytes");
    const char *package_tag_line = strstr(wire, "ETag: ");
    assert(NULL != package_tag_line);
    char package_tag[67];
    memcpy(package_tag, package_tag_line + 6, 66U);
    package_tag[66] = '\0';
    snprintf(conditional, sizeof(conditional),
        "GET /index.html.gz?t=valid HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n",
        package_tag);
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    assert(NULL != strstr(wire, "304 Not Modified"));
    assert(NULL != strstr(wire, "Cache-Control: private, no-cache"));
    boundary = strstr(wire, "\r\n\r\n");
    assert(NULL != boundary && wire_length == (size_t)(boundary - wire) + 4U);
    selected_page = &real_fw_update;
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    assert(0 == memcmp(wire + wire_length - real_fw_update.length,
                       real_fw_update.data, real_fw_update.length));
    wire_length = 0U;
    snprintf(conditional, sizeof(conditional),
        "GET /index.html.gz HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n",
        package_tag);
    receive_request(conditional);
    finish_response();
    assert(NULL != strstr(wire, "HTTP/1.1 401 "));
    puts("PASS: validated package saves body bytes; changed content sends new bytes; ETag never bypasses auth");


    const html_resource_t invalid_package = {
        page_data, sizeof(page_data), HTML_COMPRESS_NONE, "text/html"};
    const html_resource_t *invalid_resources[] = {NULL, &invalid_package};
    for (size_t index = 0U; 2U > index; index++)
    {
        reset_test();
        selected_page = invalid_resources[index];
        receive_request("GET /index.html.gz?t=valid HTTP/1.1\r\n\r\n");
        finish_response();
        assert(NULL != strstr(wire, "500 Internal Server Error"));
    }
    selected_page = &page;
    puts("PASS: unavailable/non-gzip application package fails explicitly");

    reset_test();
    static uint8_t full_queue[10U * 1024U - 1U];
    memset(full_queue, 'q', sizeof(full_queue));
    assert((int)sizeof(full_queue) ==
           transport.send(full_queue, sizeof(full_queue)));
    assert(-1 == transport.send("x", 1U));
    assert(-1 == transport.send(NULL, 1U));
    finish_response();
    assert(sizeof(full_queue) == wire_length);
    assert(0 == memcmp(wire, full_queue, sizeof(full_queue)));
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
    assert((int)sizeof(first_block) ==
           transport.send(first_block, sizeof(first_block)));
    while (4000U > wire_length)
    {
        gsm_http_server_process();
        if (0U != tx_pending) complete_tx();
    }
    assert((int)sizeof(second_block) ==
           transport.send(second_block, sizeof(second_block)));
    finish_response();
    assert(sizeof(first_block) + sizeof(second_block) == wire_length);
    assert(0 == memcmp(wire, first_block, sizeof(first_block)));
    assert(0 == memcmp(wire + sizeof(first_block), second_block,
                       sizeof(second_block)));
    puts("PASS: queue wraps across 10 KiB while earlier data is still pending");

    reset_test();
    assert((int)sizeof(first_block) ==
           transport.send(first_block, sizeof(first_block)));
    gsm_http_server_reset();
    assert(3 == transport.send("new", 3U));
    finish_response();
    assert(3U == wire_length && 0 == memcmp(wire, "new", 3U));
    puts("PASS: reset discards the old response before the next client");

    check_rfwu_copy_transport();
    check_static_stream();
    check_json_copy_and_rejected_queue_send();
    check_stream_disconnect_and_pipelining();
    return 0;
}
/*** end of file ***/
'''
(output / "test_transport.c").write_text(
    test + '\n' + '\n'.join(stubs), encoding="utf-8")
command = [args.cc, "-std=c11", "-DUNIT_TEST", "-O0", "-Wall", "-Wextra",
           "-Werror"]
for directory in [output, root / "Application/web-server",
                  root / "Application/gsm", root / "Application/libs"]:
    command.append("-I" + str(directory))
command += sources + [str(output / "test_transport.c"),
                      str(root / "Application/web-server/http_request_parser.c"),
                      str(root / "Application/gsm/ring_buff.c"),
                      str(root / "Application/libs/xprintf.c"),
                      str(root / "Application/libs/sha256.c"),
                      "-lm", "-o", str(output / "test_transport.exe")]
subprocess.run(command, check=True)
result = subprocess.run([str(output / "test_transport.exe")],
                        capture_output=True, text=True)
print(result.stdout, end="")
print(result.stderr, end="")
(output / "result.log").write_text(result.stdout + result.stderr,
                                   encoding="utf-8")
result.check_returncode()
