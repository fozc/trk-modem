/**
 * @file http_request_parser.h
 * @brief HTTP request parsing utilities
 * 
 * Provides functions for:
 * - Parsing HTTP request line (method, path, query)
 * - Parsing HTTP headers (Content-Length, etc.)
 * - Validating request structure
 * - Finding request boundaries
 * 
 * Works with mutable buffer (typically rx_buffer).
 * Modifies buffer in-place for efficiency (zero-copy parsing).
 */

#ifndef HTTP_REQUEST_PARSER_H_
#define HTTP_REQUEST_PARSER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

/**
 * @brief HTTP request method enumeration
 */
typedef enum {
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST
} http_method_t;

/**
 * @brief Parsed HTTP request structure
 * 
 * All string pointers point into the original buffer.
 * Buffer must remain valid while using this structure.
 */
typedef struct {
    /* Request line */
    http_method_t method;          /**< HTTP method (GET, POST, etc.) */
    char *path;                    /**< Request path (e.g., "/api/data") */
    char *query_string;            /**< Query string (after '?'), or NULL */
    
    /* Headers */
    int content_length;            /**< Content-Length header value, or 0 */
    
    /* Body */
    char *body;                    /**< Pointer to body start, or NULL */
    int body_length;               /**< Body length in bytes */
    
    /* Internal state */
    int header_end_offset;         /**< Offset where headers end (after \r\n\r\n) */
} http_request_t;


/* ============================================================================
 * REQUEST BOUNDARY DETECTION
 * ============================================================================ */

/**
 * @brief Find end of HTTP headers in buffer
 * 
 * Searches for "\r\n\r\n" sequence that marks end of headers.
 * 
 * @param buffer Request buffer
 * @param buffer_length Number of bytes in buffer
 * @return Offset after "\r\n\r\n", or -1 if not found
 */
int http_find_header_end(const char *buffer, int buffer_length);

/**
 * @brief Check if request is complete
 * 
 * A request is complete when:
 * 1. Headers have ended (\r\n\r\n found)
 * 2. Body is fully received (if Content-Length specified)
 * 
 * @param buffer Request buffer
 * @param buffer_length Number of bytes in buffer
 * @param request Parsed request (content_length must be set)
 * @return true if request is complete, false otherwise
 */
bool http_is_request_complete(const char *buffer, 
                              int buffer_length,
                              const http_request_t *request);


/* ============================================================================
 * REQUEST PARSING
 * ============================================================================ */

/**
 * @brief Parse HTTP request from buffer
 * 
 * Parses:
 * - Request line (GET /path?query HTTP/1.1)
 * - Content-Length header
 * - Body location
 * 
 * IMPORTANT: This function modifies the buffer in-place:
 * - Inserts null terminators to split strings
 * - Original buffer content is altered
 * 
 * @param buffer Mutable request buffer
 * @param buffer_length Number of bytes in buffer
 * @param request Output: parsed request structure
 * @return 0 on success, -1 on parse error
 */
int http_parse_request(char *buffer, 
                      int buffer_length, 
                      http_request_t *request);


/* ============================================================================
 * REQUEST LINE PARSING
 * ============================================================================ */

/**
 * @brief Parse HTTP request line
 * 
 * Parses "METHOD PATH HTTP/VERSION" format.
 * Splits path and query string at '?' character.
 * 
 * Example: "GET /api/data?id=123 HTTP/1.1"
 *   -> method=GET, path="/api/data", query="id=123"
 * 
 * IMPORTANT: Modifies buffer in-place (inserts null terminators).
 * 
 * @param request_line Mutable request line buffer
 * @param line_length Length of request line
 * @param method Output: HTTP method
 * @param path Output: pointer to path (in original buffer)
 * @param query_string Output: pointer to query string, or NULL
 * @return 0 on success, -1 on parse error
 */
int http_parse_request_line(char *request_line,
                            int line_length,
                            http_method_t *method,
                            char **path,
                            char **query_string);


/* ============================================================================
 * HEADER PARSING
 * ============================================================================ */

/**
 * @brief Parse Content-Length header
 * 
 * Searches for "Content-Length:" header and parses its value.
 * Search is case-insensitive.
 * 
 * @param headers_start Pointer to start of headers section
 * @param headers_length Length of headers section
 * @return Content-Length value, or 0 if not found
 */
int http_parse_content_length(const char *headers_start, int headers_length);


/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * @brief Convert method string to enum
 * 
 * @param method_string Method string ("GET", "POST", etc.)
 * @return http_method_t enum value
 */
http_method_t http_method_from_string(const char *method_string);

/**
 * @brief Convert method enum to string
 * 
 * @param method HTTP method enum
 * @return Static string ("GET", "POST", "UNKNOWN")
 */
const char* http_method_to_string(http_method_t method);

/**
 * @brief Case-insensitive string prefix check
 * 
 * @param string String to check
 * @param prefix Prefix to match
 * @return true if string starts with prefix (case-insensitive)
 */
bool http_string_starts_with_ignore_case(const char *string, const char *prefix);

/**
 * @brief Get query parameter value from query string
 * 
 * Searches for "key=value" in query string and returns value.
 * 
 * Example: "line=3&other=abc" with key="line" returns "3"
 * 
 * @param query_string Full query string (e.g., "line=3&foo=bar")
 * @param key Parameter name to find
 * @param value_out Output buffer for value
 * @param value_max_len Maximum length of output buffer
 * @return true if parameter found, false otherwise
 */
bool http_get_query_param(const char *query_string, 
                          const char *key,
                          char *value_out,
                          int value_max_len);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_REQUEST_PARSER_H_ */
