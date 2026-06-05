/**
 * @file http_response.h
 * @brief HTTP response building and sending utilities
 * 
 * Provides functions for:
 * - Building HTTP responses with status, headers, and body
 * - Sending data in chunks (for large responses)
 * - Common response helpers (OK, error, HTML, etc.)
 * 
 * Uses global tx_buffer for response assembly.
 * Requires a send callback function to be registered.
 */

#ifndef HTTP_RESPONSE_H_
#define HTTP_RESPONSE_H_

#include <stdint.h>
#include <stdbool.h>
#include "html_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/**
 * @brief Chunk size for sending large responses
 * 
 * Responses are sent in fixed-size chunks to avoid blocking.
 * Typical value: 1024 bytes
 */
#ifndef HTTP_SEND_CHUNK_SIZE
#define HTTP_SEND_CHUNK_SIZE 1024
#endif


/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize response module with send callback
 * 
 * Must be called before any response functions.
 * 
 * @param send_callback Function pointer for sending data
 *                      Returns: bytes sent (>0), or <=0 on error
 */
void http_response_init(int (*send_callback)(const void *data, int length));


/* ============================================================================
 * LOW-LEVEL SEND FUNCTIONS
 * ============================================================================ */

/**
 * @brief Send data in chunks
 * 
 * Splits data into fixed-size chunks and sends each chunk.
 * Stops on first send error.
 * 
 * @param data Pointer to data buffer
 * @param length Number of bytes to send
 * @return Total bytes sent (may be less than length on error)
 */
int http_send_data(const char *data, int length);


/* ============================================================================
 * HTTP RESPONSE BUILDER
 * ============================================================================ */

/**
 * @brief Send complete HTTP response
 * 
 * Builds and sends:
 * - Status line (HTTP/1.1 <code> <reason>)
 * - Content-Type header
 * - Content-Length header
 * - Connection: close header
 * - Server header
 * - Empty line
 * - Body content
 * 
 * @param status_code HTTP status code (200, 404, etc.)
 * @param reason_phrase Status reason ("OK", "Not Found", etc.)
 * @param content_type MIME type ("text/plain", "text/html", etc.)
 * @param body Response body (NULL for no body)
 * @param body_length Body length in bytes
 */
void http_send_response(int status_code,
                        const char *reason_phrase,
                        const char *content_type,
                        const char *body,
                        int body_length);


/* ============================================================================
 * CONVENIENCE FUNCTIONS
 * ============================================================================ */

/**
 * @brief Send 200 OK response with plain text body
 * 
 * @param body Response body
 * @param body_length Body length in bytes
 */
void http_send_ok(const char *body, int body_length);

/**
 * @brief Send error response with plain text message
 * 
 * Common error codes:
 * - 400 Bad Request
 * - 404 Not Found
 * - 405 Method Not Allowed
 * - 413 Payload Too Large
 * - 422 Unprocessable Entity
 * - 500 Internal Server Error
 * 
 * @param status_code HTTP error code
 * @param message Error message
 */
void http_send_error(int status_code, const char *message);

/**
 * @brief Send HTML response with 200 OK
 * 
 * @param html HTML content
 * @param html_length HTML length in bytes
 */
void http_send_html(const char *html, int html_length);

/**
 * @brief Send HTML resource with automatic gzip handling
 * 
 * @param resource HTML resource structure (NULL-safe, uses default 404 page if NULL)
 */
void http_send_html_resource(const html_resource_t *resource);

/**
 * @brief Send JSON response with 200 OK
 * 
 * @param json JSON content
 * @param json_length JSON length in bytes
 */
void http_send_json(const char *json, int json_length);

/**
 * @brief Send 404 Not Found error
 */
void http_send_not_found(void);
void http_send_favicon(void);

/**
 * @brief Send 400 Bad Request error
 */
void http_send_bad_request(void);

/**
 * @brief Send 405 Method Not Allowed error
 */
void http_send_method_not_allowed(void);


/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * @brief Get reason phrase for HTTP status code
 * 
 * @param status_code HTTP status code
 * @return Pointer to static reason phrase string
 */
const char* http_get_reason_phrase(int status_code);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_RESPONSE_H_ */
