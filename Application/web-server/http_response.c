/**
 * @file http_response.c
 * @brief HTTP response building and sending implementation
 */

#include "http_response.h"
#include "html_resources.h"  /* For HTML_COMPRESS_* constants */
#include "xprintf.h"  /* For xsprintf */
#include <string.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
    int (*send_callback)(const void *data, int length);
} response_state;


/* ============================================================================
 * STATUS CODE LOOKUP TABLE
 * ============================================================================ */

typedef struct {
    int code;
    const char *phrase;
} status_code_entry_t;

static const status_code_entry_t STATUS_CODES[] = {
    {200, "OK"},
    {400, "Bad Request"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {413, "Payload Too Large"},
    {422, "Unprocessable Entity"},
    {500, "Internal Server Error"},
    {0, NULL}  /* Sentinel */
};


/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void http_response_init(int (*send_callback)(const void *data, int length))
{
    response_state.send_callback = send_callback;
}


/* ============================================================================
 * LOW-LEVEL SEND FUNCTIONS
 * ============================================================================ */

int http_send_data(const char *data, int length)
{
    if (!data || length <= 0) {
        return 0;
    }

    if (!response_state.send_callback) {
        /* No send callback registered - cannot send */
        return 0;
    }

    int total_sent = 0;

    while (total_sent < length) {
        int remaining = length - total_sent;
        int chunk_size = (remaining > HTTP_SEND_CHUNK_SIZE) 
                         ? HTTP_SEND_CHUNK_SIZE 
                         : remaining;

        int sent = response_state.send_callback(data + total_sent, chunk_size);

        if (sent <= 0) {
            /* Send failed - stop sending */
            break;
        }

        total_sent += sent;
    }

    return total_sent;
}


/* ============================================================================
 * HTTP RESPONSE BUILDER
 * ============================================================================ */
//TODO: Burasi gsm module uzerinden gonderilecek. Dogrudan oranıın buffer'ina yazim yapilabilir.
void http_send_response(int status_code,
                        const char *reason_phrase,
                        const char *content_type,
                        const char *body,
                        int body_length)
{
    /* Validate inputs */
    if (!reason_phrase) {
        reason_phrase = http_get_reason_phrase(status_code);
    }
    if (!content_type) {
        content_type = "text/plain";
    }
    if (!body) {
        body = "";
        body_length = 0;
    }

    /* Build HTTP headers in stack buffer */
    char header_buffer[256];
    int header_length = xsprintf(header_buffer,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n"
        "Server: EmbeddedHTTP/1.0\r\n"
        "\r\n",
        status_code,
        reason_phrase,
        content_type,
        body_length
    );

#if 1
    /* Print response for debugging */
    xcprintf(XCOLOR_CYAN, "\r\n=== HTTP RESPONSE ===\r\n");
    xprintf("%s", header_buffer);
    xprintf("[Body: %d bytes] \r\n%s", body_length, body);
    xcprintf(XCOLOR_CYAN,"\r\n=== END RESPONSE ===\r\n\r\n");
#endif
    /* Send headers */
    http_send_data(header_buffer, header_length);

    /* Send body if present */
    if (body_length > 0) {
        http_send_data(body, body_length);
    }
}


/* ============================================================================
 * CONVENIENCE FUNCTIONS
 * ============================================================================ */

void http_send_ok(const char *body, int body_length)
{
    http_send_response(200, "OK", "text/plain", body, body_length);
}

void http_send_error(int status_code, const char *message)
{
    if (!message) {
        message = http_get_reason_phrase(status_code);
    }

    int message_length = (int)strlen(message);
    http_send_response(status_code, NULL, "text/plain", message, message_length);
}

void http_send_html(const char *html, int html_length)
{
    http_send_response(200, "OK", "text/html", html, html_length);
}

void http_send_html_resource(const html_resource_t *resource)
{
    /* NULL-safe: fall back to default 404 page if resource is NULL */
    if (resource == NULL) {
        resource = get_default_html();
    }
    
    if (resource->is_gzipped != HTML_COMPRESS_NONE) {
        /* Determine Content-Encoding based on compression type */
        const char *encoding = (resource->is_gzipped == HTML_COMPRESS_BR) ? "br" : "gzip";
        
        /* Build HTTP headers with compression encoding */
        char header_buffer[256];
        int header_length = xsprintf(header_buffer,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Encoding: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n"
            "Server: EmbeddedHTTP/1.0\r\n"
            "\r\n",
            encoding,
            resource->length
        );

        /* Print response for debugging */
        xprintf("\r\n=== HTTP RESPONSE (%s) ===\r\n", encoding);
        xprintf("%s", header_buffer);
        xprintf("[Body: %d bytes compressed]\r\n", resource->length);
        xprintf("=== END RESPONSE ===\r\n\r\n");

        /* Send headers and compressed body */
        http_send_data(header_buffer, header_length);
        http_send_data((const char *)resource->data, resource->length);
    } else {
        /* Send as regular HTML */
        http_send_response(200, "OK", "text/html", (const char *)resource->data, resource->length);
    }
}

void http_send_json(const char *json, int json_length)
{
    http_send_response(200, "OK", "application/json", json, json_length);
}

void http_send_not_found(void)
{
    http_send_error(404, "Not Found");
}

void http_send_bad_request(void)
{
    http_send_error(400, "Bad Request");
}

void http_send_method_not_allowed(void)
{
    http_send_error(405, "Method Not Allowed");
}


/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

const char* http_get_reason_phrase(int status_code)
{
    /* Look up status code in table */
    for (int i = 0; STATUS_CODES[i].phrase != NULL; i++) {
        if (STATUS_CODES[i].code == status_code) {
            return STATUS_CODES[i].phrase;
        }
    }

    /* Unknown status code - return generic phrase */
    if (status_code >= 200 && status_code < 300) {
        return "Success";
    } else if (status_code >= 400 && status_code < 500) {
        return "Client Error";
    } else if (status_code >= 500 && status_code < 600) {
        return "Server Error";
    }

    return "Unknown";
}
