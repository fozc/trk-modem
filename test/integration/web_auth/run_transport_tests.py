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
#include "index_html.h"
#include "fw_update_html.h"
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
    if name in ("handle_post_login", "handle_get_index"):
        continue
    ignored = []
    if parameters.strip() != "void":
        for parameter in parameters.split(','):
            argument = re.search(r'(\w+)\s*$', parameter)[1]
            ignored.append(f"(void){argument};")
    stubs.append(f"void {name}({parameters}) {{ {' '.join(ignored)} "
                 'http_send_json("{}", 2); }')

resources = []
for name in ["index", "fw_update"]:
    embedded = (root / f"Application/web-server/{name}_html.c").read_text(encoding="utf-8")
    payload = bytes(int(value, 16) for value in re.findall(
        r"0x([0-9a-fA-F]{2})", embedded.split("};")[0]))
    resources.append(f"static const uint8_t real_{name}_data[] = {{" +
                     ",".join(str(value) for value in payload) + "};\n" +
                     f"static const html_resource_t real_{name} = " +
                     "{real_" + name + "_data, sizeof(real_" + name +
                     "_data), HTML_COMPRESS_GZIP, \"text/html\"};\n")
(output / "real_resources.h").write_text("".join(resources), encoding="ascii")

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
static char wire[65536];
static size_t wire_length;
static bool authenticated;
static web_server_io_t transport;
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
    http_server_reset();
}
static int send_http(const void *data, int length)
{
    assert(0 < length);
    return transport.send(data, (uint32_t)length);
}
void web_server_init(web_server_io_t *io)
{
    transport = *io;
    http_server_init(send_http);
}
void webserver_on_received(const uint8_t *data, int length)
{
    http_server_on_receive(data, length);
}
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
const html_resource_t *get_index_html(void) { return &page; }
const html_resource_t *get_fw_update_html(void) { return &page; }
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
    http_server_init(send_http);
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
    receive_request("GET / HTTP/1.1\r\n\r\n");
    finish_response();
    assert(NULL != strstr(wire, "200 OK"));
    const char *line = strstr(wire, "ETag: ");
    assert(NULL != line);
    char tag[67];
    memcpy(tag, line + 6, 66U);
    tag[66] = '\0';
    char conditional[160];
    snprintf(conditional, sizeof(conditional),
        "GET / HTTP/1.1\r\nIf-None-Match: %s\r\n\r\n", tag);
    wire_length = 0U;
    receive_request(conditional);
    finish_response();
    assert(NULL != strstr(wire, "304 Not Modified"));
    const char *boundary = strstr(wire, "\r\n\r\n");
    assert(NULL != boundary && wire_length == (size_t)(boundary - wire) + 4U);
    wire_length = 0U;
    receive_request("GET / HTTP/1.1\r\n\r\n");
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
        receive_request("GET / HTTP/1.1\r\n\r\n");
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
    static uint8_t full_queue[48U * 1024U - 1U];
    memset(full_queue, 'q', sizeof(full_queue));
    assert((int)sizeof(full_queue) ==
           transport.send(full_queue, sizeof(full_queue)));
    assert(-1 == transport.send("x", 1U));
    assert(-1 == transport.send(NULL, 1U));
    finish_response();
    assert(sizeof(full_queue) == wire_length);
    assert(0 == memcmp(wire, full_queue, sizeof(full_queue)));
    puts("PASS: 48 KiB ring capacity (one reserved byte); rejected writes preserve queued data");

    reset_test();
    static uint8_t first_block[40000];
    static uint8_t second_block[12000];
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
    while (20000U > wire_length)
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
    puts("PASS: queue wraps across 48 KiB while earlier data is still pending");

    reset_test();
    assert((int)sizeof(first_block) ==
           transport.send(first_block, sizeof(first_block)));
    gsm_http_server_reset();
    assert(3 == transport.send("new", 3U));
    finish_response();
    assert(3U == wire_length && 0 == memcmp(wire, "new", 3U));
    puts("PASS: reset discards the old response before the next client");

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
