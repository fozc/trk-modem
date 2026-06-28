/**
 * @file http_server.c
 * @brief Main HTTP server implementation
 * 
 * Coordinates all HTTP modules:
 * - http_encoding: URL/hex encode/decode
 * - http_request_parser: Request parsing
 * - http_response: Response building
 * - http_handlers: Endpoint handlers
 */

#include "http_server.h"
#include "http_request_parser.h"
#include "http_response.h"
#include "http_handlers.h"
#include "fw_update_html.h"
#include "test_logs.h"
#include <string.h>
#include <stdlib.h>
#include "bsp.h"
#include "reboot.h"
/* ============================================================================
 * GLOBAL STATE AND BUFFERS
 * ============================================================================ */

static struct {
    /* Buffers */
    char rx_buffer[HTTP_RX_BUFFER_SIZE];
    char tx_buffer[HTTP_TX_BUFFER_SIZE];
    int rx_length;
    
    /* State */
    int (*send_function)(const void *data, int length);
    bool initialized;
    
    /* Cached parsed request (to avoid re-parsing modified buffer) */
    http_request_t cached_request;
    bool request_parsed;
} server_state;


/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static void route_and_handle_request(http_request_t *request);
static void reset_server_state(void);


/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void http_server_init(int (*send_function)(const void *data, int length))
{
    /* Clear state */
    memset(&server_state, 0, sizeof(server_state));
    
    /* Register send callback */
    server_state.send_function = send_function;
    
    /* Initialize submodules */
    http_response_init(send_function);
    http_handlers_init(server_state.tx_buffer, HTTP_TX_BUFFER_SIZE);
    
    server_state.initialized = true;
}


/* ============================================================================
 * REQUEST ROUTING
 * ============================================================================ */

static void route_and_handle_request(http_request_t *request)
{
    if (!request) {
        http_send_bad_request();
        return;
    }

    /* Restore authentication from session token embedded in query string.
     * Must run before the auth gate so token-carrying requests are accepted
     * after a socket reconnect without requiring a new login. */
    http_handlers_set_auth_from_token(request->query_string);

    /* Check authentication for protected endpoints */
    bool is_public_endpoint = false;
    
    /* Allow unauthenticated access to login, logout and index page */
    if (strcmp(request->path, "/") == 0 || 
        strcmp(request->path, "/index.html") == 0 ||
        strcmp(request->path, "/fw_update.html") == 0 ||
        strcmp(request->path, "/auth/login") == 0 ||
        strcmp(request->path, "/auth/logout") == 0) {
        is_public_endpoint = true;
    }
    
    /* Require authentication for all other endpoints */
    if (!is_public_endpoint && !http_handlers_is_authenticated()) {
    	CCSLOG(XCOLOR_RED, "[HTTP] Unauthorized access attempt to: %s\r\n", request->path);
        http_send_error(401, "Unauthorized");
        return;
    }

    /* GET requests */
    if (request->method == HTTP_METHOD_GET) {
        
        /* GET / or GET /index.html */
        if (strcmp(request->path, "/") == 0 || 
            strcmp(request->path, "/index.html") == 0) {
            handle_get_index();
            return;
        }

        /*  GET requests JSON endpoints */
        if (strcmp(request->path, "/config/iec104") == 0) {
        	handle_get_iec_config_json();
            return;
        }

        if (strcmp(request->path, "/config/modbus") == 0) {
            handle_get_modbus_config_json();
            return;
        }

        if (strcmp(request->path, "/config/rf") == 0) {
            handle_get_rf_config_json();
            return;
        }

        if (strcmp(request->path, "/monitor/rf/") == 0) {
            handle_get_rf_monitor_json();
            return;
        }

        if (strcmp(request->path, "/config/device") == 0) {
            handle_get_device_config_json();
            return;
        }

        if(strncmp(request->path, "/monitor/rf/", 12) == 0){
            const char *id_str = request->path + 12;
            uint32_t id = id_str[0] - '0';
            handle_get_rf_monitor_line_json(id);
            return;
        }

        if (strcmp(request->path, "/status/board") == 0) {
            handle_get_board_status_json();
            return;
        }

        if (strcmp(request->path, "/syslogs") == 0) {
            http_handlers_set_query_string(request->query_string);
            handle_get_syslogs_json();
            return;
        }

        if (strcmp(request->path, "/faults") == 0) {
            http_handlers_set_query_string(request->query_string);
            handle_get_fault_records_json();
            return;
        }

        /* GET /serial - Poll for pending web shell response */
        if (strcmp(request->path, "/serial") == 0) {
            handle_get_web_shell();
            return;
        }

        if (strcmp(request->path, "/device/reboot") == 0) {
            xcprintf(XCOLOR_YELLOW, "[HTTP] GET /device/reboot - Rebooting device\r\n");
            return;
        }

        
        if (strcmp(request->path, "/fw_update.html") == 0) {
            handle_get_fw_update();
            return;
        }

        /* GET /fw_status - resume support */
        if (strcmp(request->path, "/fw_status") == 0) {
            handle_get_fw_status();
            return;
        }
        
        /* GET /r?... (read endpoints) */
        if (strcmp(request->path, "/r") == 0) {
            if (!request->query_string) {
                http_send_bad_request();
                return;
            }
            
            /* Set query string for handlers to access */
            http_handlers_set_query_string(request->query_string);
             
            if (strstr(request->query_string, "fwVersion") == request->query_string) {
                handle_get_fw_version();
                return;
            }
            
            /* Unknown selector */
            http_send_not_found();
            return;
        }
        
        /* Unknown GET path */
        http_send_not_found();
        return;
    }
    
    /* POST requests */
    if (request->method == HTTP_METHOD_POST) {
        
        /* POST /auth/login */
        if (strcmp(request->path, "/auth/login") == 0) {
            handle_post_login(request->body);
            return;
        }
        
        /* POST /auth/logout */
        if (strcmp(request->path, "/auth/logout") == 0) {
            handle_post_logout();
            return;
        }
        
        /* POST /echo */
        if (strcmp(request->path, "/echo") == 0) {
            handle_post_echo(request->body, request->body_length);
            return;
        }

        /* All other POST endpoints require admin role */
        if (!http_handlers_is_admin()) {
        	CCSLOG(XCOLOR_RED, "[HTTP] Forbidden: Admin role required for: %s\r\n", request->path);
            http_send_error(403, "Admin role required");
            return;
        }

        /* POST /serial - Web shell */
        if (strcmp(request->path, "/serial") == 0) {
            handle_post_web_shell(request->body);
            return;
        }

        /*  POST requests JSON endpoints */
        if (strcmp(request->path, "/config/iec104") == 0) {
        	handle_post_iec_config_json(request->body);
            return;
        }

        if (strcmp(request->path, "/config/modbus") == 0) {
        	handle_post_modbus_config_json(request->body);
            return;
        }

        if (strcmp(request->path, "/config/rf") == 0) {
        	handle_post_rf_config_json(request->body);
            return;
        }

        if (strcmp(request->path, "/config/device") == 0) {
            handle_post_device_config_json(request->body);
            return;
        }

        if (strcmp(request->path, "/device/reboot") == 0) {
            xcprintf(XCOLOR_YELLOW, "[HTTP] GET /device/reboot - Rebooting device\r\n");
            const char *response_body = "{\"message\":\"Rebooting\",\"success\":true}";
            http_send_json(response_body, strlen(response_body));

            reboot_system_delayed(3000);  /* Delay 3 seconds to allow response to be sent */
            return;
        }

        




        /* POST /fw_start or /s - Start firmware update (short URL for minimal overhead) */
        if (strcmp(request->path, "/fw_start") == 0 || strcmp(request->path, "/s") == 0) {
            handle_fw_start(request->body);
            return;
        }
        
        /* POST /fw_chunk?offset=X or /c?o=X&cs=Y - Receive firmware chunk (short URL) */
        if (strcmp(request->path, "/fw_chunk") == 0 || strcmp(request->path, "/c") == 0) {
            uint32_t offset = 0;
            uint16_t checksum = 0;
            bool has_checksum = false;
            
            if (request->query_string) {
                /* Parse offset from query string - support both "offset=" and "o=" */
                const char *offset_ptr = strstr(request->query_string, "offset=");
                if (offset_ptr) {
                    offset = strtoul(offset_ptr + 7, NULL, 10);
                } else {
                    offset_ptr = strstr(request->query_string, "o=");
                    if (offset_ptr) offset = strtoul(offset_ptr + 2, NULL, 10);
                }
                
                /* Parse checksum - "cs=" parameter */
                const char *cs_ptr = strstr(request->query_string, "cs=");
                if (cs_ptr) {
                    checksum = (uint16_t)strtoul(cs_ptr + 3, NULL, 10);
                    has_checksum = true;
                }
            }
            handle_fw_chunk(offset, (const uint8_t *)request->body, request->body_length, 
                           has_checksum, checksum);
            return;
        }
        
        /* POST /fw_finish or /f - Finalize firmware update (short URL) */
        if (strcmp(request->path, "/fw_finish") == 0 || strcmp(request->path, "/f") == 0) {
            handle_fw_finish(request->body);
            return;
        }
        
        /* POST /fw_reboot or /rb - Reboot device (short URL) */
        if (strcmp(request->path, "/fw_reboot") == 0 || strcmp(request->path, "/rb") == 0) {
            handle_fw_reboot();
            return;
        }
        
        /* POST /fw_apply - Apply downloaded firmware via bootloader */
        if (strcmp(request->path, "/fw_apply") == 0) {
            handle_fw_apply();
            return;
        }
        
        /* Unknown POST path */
        http_send_not_found();
        return;
    }
    
    /* Unknown or unsupported method */
    http_send_method_not_allowed();
}


/* ============================================================================
 * STATE MANAGEMENT
 * ============================================================================ */

static void reset_server_state(void)
{
    /* Clear RX buffer and reset length */
    server_state.rx_length = 0;
    memset(server_state.rx_buffer, 0, sizeof(server_state.rx_buffer));
    
    /* Clear cached request */
    memset(&server_state.cached_request, 0, sizeof(server_state.cached_request));
    server_state.request_parsed = false;
}

void http_server_reset(void)
{
    if (!server_state.initialized) {
        return;
    }
    
    CSLOG("[HTTP] Server reset - clearing all buffers\r\n");
    reset_server_state();
    http_handlers_reset();
}


/* ============================================================================
 * REQUEST RECEPTION AND PROCESSING
 * ============================================================================ */

void http_server_on_receive(const uint8_t *data, int length)
{
    if (!server_state.initialized) {
        return;  /* Server not initialized */
    }
    
    if (!data || length <= 0) {
        return;  /* Invalid input */
    }
    
    /* Check for buffer overflow */
    if (server_state.rx_length + length > HTTP_RX_BUFFER_SIZE) {
        /* Buffer would overflow - reset and discard */
        reset_server_state();
        return;
    }
    
    /* Append data to RX buffer */
    memcpy(server_state.rx_buffer + server_state.rx_length, data, length);
    server_state.rx_length += length;
    
    CSLOG("[HTTP] Appended %d bytes, total rx_length=%d\r\n", length, server_state.rx_length);
    
    /* Parse request only once (first time headers are complete) */
    if (!server_state.request_parsed) {
        int parse_result = http_parse_request(server_state.rx_buffer, 
                                             server_state.rx_length, 
                                             &server_state.cached_request);
        
        CSLOG("[HTTP] Parse result: %d\r\n", parse_result);
        
        if (parse_result != 0) {
            /* Request not complete yet, or parse error */
            
            /* Check if we have header end but parse failed */
            int header_end = http_find_header_end(server_state.rx_buffer, 
                                                 server_state.rx_length);
            if (header_end > 0) {
                /* Headers are complete but parsing failed - bad request */
            	CCSLOG(XCOLOR_RED, "[HTTP] ERROR: Headers complete but parse failed - Bad Request\r\n");
                http_send_bad_request();
                reset_server_state();
                return;
            }
            
            /* Otherwise, wait for more data */
            CSLOG("[HTTP] Headers not complete yet - waiting\r\n");
            return;
        }
        
        /* Parse successful - cache it */
        server_state.request_parsed = true;
        CSLOG("[HTTP] Request parsed and cached\r\n");
    } else {
        CSLOG("[HTTP] Using cached parsed request\r\n");
    }
    
    /* Check if request is complete (headers + body) */
    if (!http_is_request_complete(server_state.rx_buffer, 
                                  server_state.rx_length, 
                                  &server_state.cached_request)) {
        /* Wait for more body data */
        CSLOG("[HTTP] Request incomplete - waiting for more data (rx_length=%d)\n",
                server_state.rx_length);
        return;
    }
    
    /* Update body_length now that all data is received */
    if (server_state.cached_request.content_length > 0) {
        server_state.cached_request.body = server_state.rx_buffer + 
                                           server_state.cached_request.header_end_offset;
        server_state.cached_request.body_length = server_state.cached_request.content_length;
        CSLOG("[HTTP] Body pointer updated, body_length=%d\r\n",
                server_state.cached_request.body_length);
    }
    
    CSLOG("[HTTP] Request complete - processing...\r\n");
    
    /* Request is complete - route and handle it */
    route_and_handle_request(&server_state.cached_request);
    
    /* Reset state for next request */
    reset_server_state();
}
