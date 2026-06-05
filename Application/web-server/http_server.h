/**
 * @file http_server.h
 * @brief Main HTTP server - Public API
 * 
 * Minimal embedded HTTP server for smart breaker configuration.
 * 
 * Features:
 * - Single-client model (one request at a time)
 * - Static buffer allocation (no malloc)
 * - Request parsing and routing
 * - REST-like API endpoints
 * - Hex-encoded data format
 * 
 * Usage:
 * 1. Call http_server_init() with send callback
 * 2. Call http_server_on_receive() when data arrives
 * 3. Server automatically parses, routes, and responds
 */

#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize HTTP server
 * 
 * Must be called once before using the server.
 * Initializes all modules and registers send callback.
 * 
 * @param send_function Callback for sending data
 *                      Prototype: int send(const void *data, int length)
 *                      Returns: bytes sent (>0), or <=0 on error
 */
void http_server_init(int (*send_function)(const void *data, int length));

/**
 * @brief Process incoming HTTP data
 * 
 * Call this function when TCP data arrives.
 * Server will:
 * 1. Accumulate data in RX buffer
 * 2. Parse HTTP request when complete
 * 3. Route to appropriate handler
 * 4. Send response
 * 5. Reset for next request
 * 
 * @param data Pointer to received data
 * @param length Number of bytes received
 * 
 * @note Server handles one request at a time
 * @note Buffer overflow protection: excess data is discarded
 */
void http_server_on_receive(const uint8_t *data, int length);

/**
 * @brief Reset HTTP server state
 * 
 * Clears all buffers and resets the server to initial state.
 * Call this when the underlying transport connection is closed/reset.
 * 
 * @note Safe to call at any time
 */
void http_server_reset(void);


/* ============================================================================
 * CONFIGURATION (can be overridden before including)
 * ============================================================================ */

/**
 * @brief RX buffer size (for incoming requests)
 * 
 * Must be large enough for:
 * - Request line (method + path + query)
 * - Headers (Content-Length, etc.)
 * - Body (if POST)
 * 
 * Typical: 5120 bytes (5KB) - supports 4KB FW chunks + headers
 */
#ifndef HTTP_RX_BUFFER_SIZE
#define HTTP_RX_BUFFER_SIZE 5120
#endif

/**
 * @brief TX buffer size (for outgoing responses)
 * 
 * Must be large enough for largest response:
 * - index.html (~10KB)
 * - lines data (worst case ~2KB)
 * 
 * Typical: 4096 bytes (4KB) for responses,
 *          but index.html needs larger (handled separately).
 * RF config response includes per-phase monitor arrays (~4.5 KB body);
 * 8 KB provides adequate headroom.
 */
#ifndef HTTP_TX_BUFFER_SIZE
#define HTTP_TX_BUFFER_SIZE 8192
#endif

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H_ */
