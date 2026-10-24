/**
 * @file http_response.c
 * @brief HTTP response building and sending implementation
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#define CSLOG_MODULE LOG_MOD_HTTP
#include "http_response.h"
#include "sha256.h"
#include "console_logger.h"
#include "html_resources.h"  /* For HTML_COMPRESS_* constants */
#include "xprintf.h"  /* For xsprintf */
#include <string.h>
#include <limits.h>
/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
    int (*send_callback)(const void *data, int length);
    int (*send_static)(const void *data, uint32_t length);
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
    {503, "Service Unavailable"},
    {0, NULL}  /* Sentinel */
};


/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void http_response_init(int (*send_callback)(const void *data, int length))
{
    response_state.send_callback = send_callback;
    response_state.send_static = NULL;
    http_response_note_request_headers(NULL, 0);
}

void http_response_set_static_sender(
    int (*send_static)(const void *data, uint32_t length))
{
    response_state.send_static = send_static;
}

static void send_static_body(const html_resource_t *resource)
{
    if (NULL != response_state.send_static)
    {
        (void)response_state.send_static(resource->data, resource->length);
    }
    else
    {
        /* Preserve the copy-only contract for transports without streaming. */
        (void)http_send_data((const char *)resource->data,
                            (int)resource->length);
    }
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


/* Reject a truncated header before sending the caller's body. */
static bool send_header(const char *header, unsigned int length)
{
    if ((4U > length) || (0 != memcmp(header + length - 4U, "\r\n\r\n", 4U)))
    {
        static const char error[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n";
        (void)http_send_data(error, (int)(sizeof(error) - 1U));
        return false;
    }
    return (int)length == http_send_data(header, (int)length);
}

/* ============================================================================
 * HTTP RESPONSE BUILDER
 * ============================================================================ */
static bool send_response_header(int status_code,
                                 const char *reason_phrase,
                                 const char *content_type,
                                 int body_length)
{
    /* Validate inputs */
    if (!reason_phrase) {
        reason_phrase = http_get_reason_phrase(status_code);
    }
    if (!content_type) {
        content_type = "text/plain";
    }

    /* Build HTTP headers in stack buffer */
    char header_buffer[256];
    unsigned int header_length = xsnprintf(header_buffer, sizeof(header_buffer),
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

    /* Response bodies may contain credentials and session tokens. */
    CSLOG("[HTTP] Response status=%d bytes=%d\r\n",
          status_code, body_length);
    /* Send headers */
    return send_header(header_buffer, header_length);
}

void http_send_response(int status_code,
                        const char *reason_phrase,
                        const char *content_type,
                        const char *body,
                        int body_length)
{
    if (NULL == body)
    {
        body_length = 0;
    }
    if (!send_response_header(status_code, reason_phrase, content_type,
                              body_length))
    {
        return;
    }
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

/* Conditional-GET context: the router copies the incoming
 * If-None-Match header here before dispatching a request. */
#define ETAG_MAX_LEN 67U
#define ETAG_REQUEST_MAX_LEN 256U

static char etag_request[ETAG_REQUEST_MAX_LEN];
static bool etag_request_valid;

static char lower_ascii(char c)
{
    return (('A' <= c) && ('Z' >= c)) ? (char)(c + ('a' - 'A')) : c;
}

static bool header_name_matches(const char *line, size_t length)
{
    static const char name[] = "if-none-match:";
    if (sizeof(name) - 1U > length)
    {
        return false;
    }
    for (size_t index = 0U; sizeof(name) - 1U > index; index++)
    {
        if (lower_ascii(line[index]) != name[index])
        {
            return false;
        }
    }
    return true;
}

void http_response_note_request_headers(const char *buffer, int length)
{
    etag_request_valid = false;
    etag_request[0] = '\0';
    if ((NULL == buffer) || (0 >= length))
    {
        return;
    }
    const size_t end = (size_t)length;
    size_t start = 0U;
    while (end > start)
    {
        size_t next = start;
        while ((end > next) && ('\r' != buffer[next]) &&
               ('\n' != buffer[next]))
        {
            next++;
        }
        if (next == start)
        {
            return; /* Never scan a request body. */
        }
        if (header_name_matches(&buffer[start], next - start))
        {
            size_t value = start + 14U;
            while ((next > value) &&
                   ((' ' == buffer[value]) || ('\t' == buffer[value])))
            {
                value++;
            }
            while ((next > value) &&
                   ((' ' == buffer[next - 1U]) || ('\t' == buffer[next - 1U])))
            {
                next--;
            }
            const size_t count = next - value;
            if ((0U < count) && (sizeof(etag_request) > count))
            {
                (void)memcpy(etag_request, &buffer[value], count);
                etag_request[count] = '\0';
                etag_request_valid = true;
            }
            return;
        }
        start = next;
        if ((end > start) && ('\r' == buffer[start]))
        {
            start++;
        }
        if ((end > start) && ('\n' == buffer[start]))
        {
            start++;
        }
    }
}

static const char *resource_etag(const html_resource_t *resource,
                                 char out[ETAG_MAX_LEN])
{
    if (resource->etag != NULL)
    {
        return resource->etag;
    }

    static const char hex[] = "0123456789abcdef";
    uint8_t hash[32];
    sha256_ctx context;
    sha256_init(&context);
    sha256_update(&context, &resource->is_gzipped, 1U);
    sha256_update(&context, resource->data, resource->length);
    sha256_final(&context, hash);
    out[0] = '"';
    for (size_t index = 0U; sizeof(hash) > index; index++)
    {
        out[1U + index * 2U] = hex[hash[index] >> 4U];
        out[2U + index * 2U] = hex[hash[index] & 15U];
    }
    out[65] = '"';
    out[66] = '\0';
    return out;
}

static bool resource_not_modified(const char *etag)
{
    if (!etag_request_valid)
    {
        return false;
    }
    if (0 == strcmp(etag_request, "*"))
    {
        return true;
    }
    const char *value = etag_request;
    while ('\0' != *value)
    {
        while ((' ' == *value) || ('\t' == *value) || (',' == *value))
        {
            value++;
        }
        if (('W' == value[0]) && ('/' == value[1]))
        {
            value += 2;
        }
        if ('"' != *value)
        {
            return false;
        }
        const char *tag = value++;
        while (('\0' != *value) && ('"' != *value))
        {
            value++;
        }
        if ('"' != *value)
        {
            return false;
        }
        value++;
        const size_t count = (size_t)(value - tag);
        while ((' ' == *value) || ('\t' == *value))
        {
            value++;
        }
        if (('\0' != *value) && (',' != *value))
        {
            return false;
        }
        if ((strlen(etag) == count) && (0 == memcmp(tag, etag, count)))
        {
            return true;
        }
    }
    return false;
}

static bool send_not_modified(const char *etag, const char *cache_control)
{
    if (!resource_not_modified(etag))
    {
        return false;
    }
    char header[256];
    const unsigned int length = xsnprintf(header, sizeof(header),
        "HTTP/1.1 304 Not Modified\r\n"
        "ETag: %s\r\n"
        "Cache-Control: %s\r\n"
        "Connection: keep-alive\r\n"
        "Server: EmbeddedHTTP/1.0\r\n\r\n", etag, cache_control);
    (void)send_header(header, length);
    return true;
}

void http_send_gzip_package(const html_resource_t *resource)
{
    if ((NULL == resource) || (NULL == resource->data) ||
        (0U == resource->length) || (INT_MAX < resource->length) ||
        (HTML_COMPRESS_GZIP != resource->is_gzipped))
    {
        http_send_error(500, "Application package unavailable");
        return;
    }
    char etag_buffer[ETAG_MAX_LEN];
    const char *etag = resource_etag(resource, etag_buffer);
    if (send_not_modified(etag, "private, no-cache"))
    {
        return;
    }
    /* The authenticated loader counts and decompresses the raw gzip. */
    char header[384];
    const unsigned int length = xsnprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/gzip\r\n"
        "Content-Length: %lu\r\n"
        "ETag: %s\r\n"
        "Cache-Control: private, no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Server: EmbeddedHTTP/1.0\r\n\r\n", resource->length, etag);
    if (send_header(header, length))
    {
        send_static_body(resource);
    }
}

void http_send_html_resource(const html_resource_t *resource)
{
    /* NULL-safe: fall back to default 404 page if resource is NULL */
    if (resource == NULL) {
        resource = get_default_html();
    }
    
    if (resource->length > INT_MAX) {
        http_send_error(500, "HTML resource too large");
        return;
    }

    if (resource->is_gzipped != HTML_COMPRESS_NONE) {
        /* Determine Content-Encoding based on compression type */
        const char *encoding = (resource->is_gzipped == HTML_COMPRESS_BR) ? "br" : "gzip";
        char etag_buffer[ETAG_MAX_LEN];
        const char *etag = resource_etag(resource, etag_buffer);

        /* Conditional GET: the page never changes within one immutable
         * representation, so a matching If-None-Match (or "*") saves the full
         * GSM transfer. Cache-Control stays no-cache: the browser must
         * still revalidate on every visit (stale-shell protection). */
        if (send_not_modified(etag, "no-cache"))
        {
            return;
        }

        /* Build HTTP headers with compression encoding */
        char header_buffer[384];
        unsigned int header_length = xsnprintf(header_buffer, sizeof(header_buffer),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Encoding: %s\r\n"
            "Content-Length: %lu\r\n"
            "ETag: %s\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "Server: EmbeddedHTTP/1.0\r\n"
            "\r\n",
            encoding,
            resource->length,
            etag
        );

        /* Send headers and compressed body */
        if (!send_header(header_buffer, header_length)) {
            return;
        }
        send_static_body(resource);
    } else {
        /* Send as regular HTML */
        if (send_response_header(200, "OK", "text/html",
                                 (int)resource->length))
        {
            send_static_body(resource);
        }
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
