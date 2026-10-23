/**
 * @file web_shell.h
 * @brief Web shell - raw data transport between web UI and MCU
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * The handler writes the rx key and supplies its writer to capture_begin.
 * Capture opens a chunked string; capture_end closes it and returns the
 * escaped content length, excluding quotes. No staging buffer is used.
 * Output outside this synchronous capture is dropped.
 */

#ifndef WEB_SHELL_H_
#define WEB_SHELL_H_

#include "../../libraries/json/json.h"

#include <stdint.h>
#include <stdbool.h>
#include "shell.h"

#ifdef __cplusplus
extern "C"
{
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

    /* Open a JSON string; the supplied writer must outlive the capture.
 * Space for the truncation marker and response tail is reserved.
 */
void web_shell_capture_begin(json_writer_t *writer);

    /* Close the string, add the truncation marker if needed and clear state.
 * Return escaped content bytes, excluding opening and closing quotes.
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
