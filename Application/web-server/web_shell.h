/**
 * @file web_shell.h
 * @brief Web shell - raw data transport between web UI and MCU
 *
 * Provides a simple transport layer:
 * - Web -> MCU: received data is passed to a registered callback
 * - MCU -> Web: data is buffered via web_shell_send(), then flushed to HTTP response
 */

#ifndef WEB_SHELL_H_
#define WEB_SHELL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_SHELL_TX_BUF_SIZE  1024

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
 * @brief Send data to web client (MCU -> Web)
 *
 * Appends data to the internal TX buffer. Data will be included
 * in the next HTTP response when web_shell_flush() is called.
 *
 * @param data  Pointer to data to send
 * @param len   Length of data in bytes
 * @return Number of bytes written (may be less than len if buffer is full)
 */
int web_shell_send(const uint8_t *data, uint16_t len);

/**
 * @brief Flush TX buffer into output buffer and clear
 *
 * Copies buffered TX data to out_buf, clears the internal buffer,
 * and returns the number of bytes copied.
 * Called by the HTTP handler to build the response.
 *
 * @param out_buf   Destination buffer
 * @param buf_size  Size of destination buffer
 * @return Number of bytes copied to out_buf
 */
uint16_t web_shell_flush(uint8_t *out_buf, uint16_t buf_size);

/**
 * @brief Notify web shell of received data from web client
 *
 * Called by the HTTP handler when data arrives from the web.
 * Invokes the registered rx_callback.
 *
 * @param data  Pointer to received data
 * @param len   Length of received data in bytes
 */
void web_shell_on_rx(const uint8_t *data, uint16_t len);

/**
 * @brief Single-character writer suitable for use as a shell_putchar_fn_t
 *
 * Writes one byte into the web shell TX buffer.
 * Pass to shell_set_putchar() before dispatching a web command so that
 * SHELL_LOG output is captured in the TX buffer instead of going to UART.
 *
 * @param ch  Character to buffer
 */
void web_shell_putchar(int ch);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SHELL_H_ */
