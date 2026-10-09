"""Exercise complete production HTTP/transport modules with modem doubles.

Only platform includes are replaced. Request parsing, routing, response
formatting, the TX ring and HTTP transport state machine remain production code.
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
    if name == "handle_post_login":
        continue
    ignored = []
    if parameters.strip() != "void":
        for parameter in parameters.split(','):
            argument = re.search(r'(\w+)\s*$', parameter)[1]
            ignored.append(f"(void){argument};")
    stubs.append(f"void {name}({parameters}) {{ {' '.join(ignored)} "
                 'http_send_json("{}", 2); }')

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
static char wire[8192];
static size_t wire_length;
static bool authenticated;
static web_server_io_t transport;
static char handler_buffer[4096];
static const html_resource_t page = {0};

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
                      "-lm", "-o", str(output / "test_transport.exe")]
subprocess.run(command, check=True)
result = subprocess.run([str(output / "test_transport.exe")],
                        capture_output=True, text=True)
print(result.stdout, end="")
print(result.stderr, end="")
(output / "result.log").write_text(result.stdout + result.stderr,
                                   encoding="utf-8")
result.check_returncode()
