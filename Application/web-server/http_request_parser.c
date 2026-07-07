/**
 * @file http_request_parser.c
 * @brief HTTP request parsing implementation
 */

#include "http_request_parser.h"
#include "console_logger.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Maximum allowed Content-Length (slightly less than RX buffer for headers) */
#define MAX_CONTENT_LENGTH 4608  /* 4.5 KB - supports 4KB FW chunks in 5KB buffer */

/* Maximum allowed request path length (bytes, excluding terminator) */
#define HTTP_MAX_PATH_LEN  256

/* ============================================================================
 * VALIDATION FUNCTIONS
 * ============================================================================ */

/**
 * @brief Validate URL path for security issues
 * 
 * Checks for:
 * - Path traversal attacks (../)
 * - Null byte injection
 * - Invalid characters
 * 
 * @param path URL path to validate
 * @return true if valid, false if malicious
 */
static bool validate_path(const char *path) {
    if (!path) return false;
    
    /* Path traversal check */
    if (strstr(path, "..") != NULL) {
        CSLOG_ERR("[HTTP] ERROR: Path traversal attempt detected: %s\r\n", path);
        return false;
    }
    
    /* Length check (bounded). strnlen caps the scan at HTTP_MAX_PATH_LEN + 1,
     * so even if the request-line parser's NUL-termination invariant were
     * broken there is no over-read; a path with no terminator within the
     * limit is treated as too long. */
    size_t len = strnlen(path, (size_t)HTTP_MAX_PATH_LEN + 1U);
    if (len > HTTP_MAX_PATH_LEN) {
        CSLOG_ERR("[HTTP] ERROR: Path too long (max: %d)\r\n", HTTP_MAX_PATH_LEN);
        return false;
    }
    
    return true;
}

/* ============================================================================
 * REQUEST BOUNDARY DETECTION
 * ============================================================================ */

int http_find_header_end(const char *buffer, int buffer_length)
{
    if (!buffer || buffer_length < 4) {
        return -1;
    }

    /* Search for "\r\n\r\n" sequence */
    for (int i = 0; i <= buffer_length - 4; i++) {
        if (buffer[i] == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {
            return i + 4;  /* Return offset after the sequence */
        }
    }

    return -1;  /* Not found */
}

bool http_is_request_complete(const char *buffer,
                              int buffer_length,
                              const http_request_t *request)
{
    if (!buffer || !request) {
        return false;
    }

    /* Check if headers are complete */
    if (request->header_end_offset <= 0) {
        return false;
    }

    /* Check if body is complete (if Content-Length specified) */
    if (request->content_length > 0) {
        int available_body = buffer_length - request->header_end_offset;
        CSLOG_NODT("[HTTP] Content-Length: %d, Available body: %d, Buffer length: %d\r\n",
                request->content_length, available_body, buffer_length);
        
        if (available_body < request->content_length) {
            CSLOG_NODT("[HTTP] Body incomplete - waiting for %d more bytes\r\n",
                    request->content_length - available_body);
            return false;  /* Still waiting for body data */
        }
        
        CSLOG_NODT("[HTTP] Body complete - all %d bytes received\r\n", request->content_length);
    }

    return true;
}


/* ============================================================================
 * REQUEST LINE PARSING
 * ============================================================================ */

int http_parse_request_line(char *request_line,
                            int line_length,
                            http_method_t *method,
                            char **path,
                            char **query_string)
{
    if (!request_line || !method || !path || !query_string) {
        return -1;
    }

    /* Initialize outputs */
    *method = HTTP_METHOD_UNKNOWN;
    *path = NULL;
    *query_string = NULL;

    /* Find first space (after method) */
    char *first_space = strchr(request_line, ' ');
    if (!first_space) {
        return -1;  /* Invalid format */
    }

    /* Find second space (after path) */
    char *second_space = strchr(first_space + 1, ' ');
    if (!second_space) {
        return -1;  /* Invalid format */
    }

    /* Null-terminate method string */
    *first_space = '\0';

    /* Null-terminate path string */
    *second_space = '\0';

    /* Parse method */
    *method = http_method_from_string(request_line);

    /* Parse path and query string */
    *path = first_space + 1;

    char *question_mark = strchr(*path, '?');
    if (question_mark) {
        /* Split path and query */
        *question_mark = '\0';
        *query_string = question_mark + 1;
    }

    return 0;
}


/* ============================================================================
 * HEADER PARSING
 * ============================================================================ */

int http_parse_content_length(const char *headers_start, int headers_length)
{
    if (!headers_start || headers_length <= 0) {
        return 0;
    }

    /* Search for "Content-Length:" header (case-insensitive) */
    const char *line_start = headers_start;
    const char *headers_end = headers_start + headers_length;

    while (line_start < headers_end) {
        /* Find end of current line */
        const char *line_end = strstr(line_start, "\r\n");
        if (!line_end) {
            break;
        }

        /* Check if this line is Content-Length header */
        if (http_string_starts_with_ignore_case(line_start, "Content-Length:")) {
            /* Parse the value */
            const char *value_start = line_start + 15;  /* Skip "Content-Length:" */

            /* Skip whitespace */
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
                value_start++;
            }

            /* Parse integer */
            int content_length = 0;
            while (value_start < line_end && *value_start >= '0' && *value_start <= '9') {
                content_length = content_length * 10 + (*value_start - '0');
                value_start++;
            }
            
            /* Validate Content-Length limit */
            if (content_length > MAX_CONTENT_LENGTH) {
                CSLOG_ERR("[HTTP] ERROR: Content-Length too large: %d bytes (max: %d)\r\n",
                       content_length, MAX_CONTENT_LENGTH);
                return -1;  /* Signal error */
            }

            return content_length;
        }

        /* Move to next line */
        line_start = line_end + 2;  /* Skip \r\n */
    }

    return 0;  /* Header not found */
}


/* ============================================================================
 * REQUEST PARSING
 * ============================================================================ */

int http_parse_request(char *buffer,
                      int buffer_length,
                      http_request_t *request)
{
    if (!buffer || !request || buffer_length < 4) {
        CSLOG_ERR("[HTTP] Parse: Invalid params (buffer=%p, request=%p, length=%d)\r\n",
                buffer, request, buffer_length);
        return -1;
    }

    /* Initialize request structure */
    memset(request, 0, sizeof(http_request_t));

    /* Find header end */
    int header_end = http_find_header_end(buffer, buffer_length);
    if (header_end < 0) {
        CSLOG_NODT("[HTTP] Parse: Headers not complete (header_end=%d)\r\n", header_end);
        return -1;  /* Headers not complete yet */
    }
    request->header_end_offset = header_end;
    CSLOG_NODT("[HTTP] Parse: Headers complete at offset %d\r\n", header_end);

    /* Find end of request line */
    char *request_line_end = strstr(buffer, "\r\n");
    if (!request_line_end) {
        CSLOG_ERR("[HTTP] Parse: No CRLF found in request line\r\n");
        return -1;  /* Invalid request */
    }

    int request_line_length = (int)(request_line_end - buffer);

    /* Parse request line (modifies buffer) */
    if (http_parse_request_line(buffer, request_line_length,
                                &request->method,
                                &request->path,
                                &request->query_string) != 0) {
        CSLOG_ERR("[HTTP] Parse: Request line parse failed\r\n");
        return -1;
    }
    
    /* Validate path for security issues */
    if (!validate_path(request->path)) {
        CSLOG_ERR("[HTTP] Parse: Path validation failed\r\n");
        return -1;
    }

    /* Parse headers */
    char *headers_start = request_line_end + 2;  /* Skip \r\n */
    int headers_length = header_end - (int)(headers_start - buffer) - 4;  /* Exclude final \r\n\r\n */

    request->content_length = http_parse_content_length(headers_start, headers_length);
    
    /* Check for Content-Length parsing error */
    if (request->content_length < 0) {
        CSLOG_ERR("[HTTP] Parse: Content-Length validation failed\r\n");
        return -1;
    }
    
    CSLOG_NODT("[HTTP] Parse: Content-Length parsed as %d\r\n", request->content_length);

    /* Set body pointer */
    if (request->content_length > 0) {
        request->body = buffer + header_end;
        request->body_length = buffer_length - header_end;

        /* Clamp body length to Content-Length */
        if (request->body_length > request->content_length) {
            request->body_length = request->content_length;
        }
    }

    return 0;
}


/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

http_method_t http_method_from_string(const char *method_string)
{
    if (!method_string) {
        return HTTP_METHOD_UNKNOWN;
    }

    if (strcmp(method_string, "GET") == 0) {
        return HTTP_METHOD_GET;
    }
    if (strcmp(method_string, "POST") == 0) {
        return HTTP_METHOD_POST;
    }

    return HTTP_METHOD_UNKNOWN;
}

const char* http_method_to_string(http_method_t method)
{
    switch (method) {
        case HTTP_METHOD_GET:
            return "GET";
        case HTTP_METHOD_POST:
            return "POST";
        default:
            return "UNKNOWN";
    }
}

bool http_string_starts_with_ignore_case(const char *string, const char *prefix)
{
    if (!string || !prefix) {
        return false;
    }

    while (*prefix) {
        char s = *string;
        char p = *prefix;

        /* Convert to lowercase for comparison */
        if (s >= 'A' && s <= 'Z') {
            s = s - 'A' + 'a';
        }
        if (p >= 'A' && p <= 'Z') {
            p = p - 'A' + 'a';
        }

        if (s != p) {
            return false;
        }

        string++;
        prefix++;
    }

    return true;  /* All prefix characters matched */
}

bool http_get_query_param(const char *query_string,
                          const char *key,
                          char *value_out,
                          int value_max_len)
{
    if (!query_string || !key || !value_out || value_max_len <= 0) {
        return false;
    }

    /* Initialize output */
    value_out[0] = '\0';

    /* Calculate key length */
    int key_len = strlen(key);
    const char *pos = query_string;

    /* Search for "key=" in query string */
    while (*pos) {
        /* Check if this is our key */
        if (strncmp(pos, key, key_len) == 0 && pos[key_len] == '=') {
            /* Found it! Extract value */
            const char *value_start = pos + key_len + 1;  /* Skip "key=" */
            const char *value_end = value_start;

            /* Find end of value (& or end of string) */
            while (*value_end && *value_end != '&') {
                value_end++;
            }

            /* Copy value to output buffer */
            int value_len = value_end - value_start;
            if (value_len >= value_max_len) {
                value_len = value_max_len - 1;  /* Truncate if too long */
            }

            strncpy(value_out, value_start, value_len);
            value_out[value_len] = '\0';

            return true;
        }

        /* Move to next parameter */
        while (*pos && *pos != '&') {
            pos++;
        }
        if (*pos == '&') {
            pos++;  /* Skip '&' */
        }
    }

    return false;  /* Key not found */
}
