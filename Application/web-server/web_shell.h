/**
 * @file web_shell.h
 * @brief Web shell - raw data transport between web UI and MCU
 *
 * Capture model (request-scoped, no internal buffering):
 * - The HTTP handler writes the JSON prefix, calls web_shell_capture_begin()
 *   with the response buffer, then web_shell_on_rx() which executes the
 *   command. All terminal output (SHELL_LOG path and the rf echo) is
 *   JSON-escaped straight into that buffer. web_shell_capture_end() closes
 *   the capture (truncation marker if needed) and returns the length.
 * - Output produced while no capture is active is dropped: only the
 *   synchronous command response is captured, never background logging.
 */

#ifndef WEB_SHELL_H_
#define WEB_SHELL_H_

#include <stdint.h>
#include <stdbool.h>
#include "shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for data received from web
 *
 * Called when the web client sends data via POST /serial.
 * The user implements this to process incoming data.
 *
 * @param data  Pointer to received data (not null-terminated)
 * @param len   Length of received data in bytes
 */
typedef void (*web_shell_rx_cb_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Initialize web shell module
 *
 * @param rx_callback  Callback for data received from web (can be NULL)
 */
void web_shell_init(web_shell_rx_cb_t rx_callback);

/**
 * @brief Begin capturing terminal output into a caller-provided buffer.
 *
 * Must be called before web_shell_on_rx() for the request. The destination
 * receives JSON-escaped output; room for the [TRUNCATED] marker and the
 * NUL terminator is reserved internally, so the marker always fits.
 *
 * @param dst   Destination inside the JSON body (after the opening quote)
 * @param room  Writable bytes in dst (caller reserves the closing quote)
 */
void web_shell_capture_begin(char *dst, int room);

/**
 * @brief End the capture and return the captured length.
 *
 * Appends the [TRUNCATED] marker when the output was cut, NUL-terminates
 * the destination and clears the capture state.
 *
 * @return Number of bytes written to dst; 0 when nothing was captured
 */
int web_shell_capture_end(void);

/**
 * @brief Notify web shell of received data from web client
 *
 * Called by the HTTP handler when data arrives from the web.
 * Invokes the registered rx_callback.
 *
 * @param data  Pointer to received data
 * @param len   Length of received data
 */
void web_shell_on_rx(const uint8_t *data, uint16_t len);

/**
 * @brief Single-character writer suitable for use as a shell_putchar_fn_t
 *
 * While a capture is active, writes the JSON-escaped character into the
 * capture destination; otherwise the character is dropped.
 *
 * @param ch  Character to buffer
 */
void web_shell_putchar(int ch);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SHELL_H_ */
