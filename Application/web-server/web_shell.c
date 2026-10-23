/**
 * @file web_shell.c
 * @brief Web shell - raw data transport between web UI and MCU
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Request-scoped capture: the HTTP handler provides the response buffer
 * (web_shell_capture_begin); command output and the rf echo are
 * JSON-escaped straight into it. There is no internal staging buffer.
 */

#include "web_shell.h"
#include "web_json.h"
#include <stdbool.h>
#include <string.h>
#include "xprintf.h"

/* ======================================================================
 *  Capture state (active only between capture_begin/end, single context)
 * ====================================================================== */

#define WEB_SHELL_TRUNC_MARKER "\r\n[TRUNCATED]\r\n"
#define WEB_SHELL_TAIL_RESERVE 40U

static json_writer_t *capture_writer;
static size_t capture_start;
static size_t capture_limit;
static bool cap_active;
static bool cap_truncated;
static uint8_t cap_ansi_state;

/** Write one raw byte into the capture, JSON-escaped. */
static void capture_putc(uint8_t ch)
{
    if (!cap_active)
    {
        return; /* no capture in progress: drop */
    }

    /* The web terminal displays text; UART color sequences are not text. */
    if (0x1BU == ch)
    {
        cap_ansi_state = 1U;
        return;
    }
    if (1U == cap_ansi_state)
    {
        cap_ansi_state = ('[' == ch) ? 2U : 0U;
        return;
    }
    if (2U == cap_ansi_state)
    {
        if ((ch >= 0x40U) && (ch <= 0x7EU))
        {
            cap_ansi_state = 0U;
        }
        return;
    }

    if (cap_truncated || capture_writer == NULL)
    {
        return;
    }
    if ((ch < 0x20U && ch != '\n' && ch != '\r' && ch != '\t') || ch >= 0x7FU)
    {
        return;
    }
    if (capture_writer->used + 6U > capture_limit)
    {
        cap_truncated = true;
        return;
    }
    char value = (char)ch;
    json_span_t part = {&value, 1U};
    (void)json_write_string_chunk(capture_writer, part);
}

/* ======================================================================
 *  RX callback: rf echo or shell command execution
 * ====================================================================== */

static web_shell_rx_cb_t rx_cb;

static void default_rx_handler(const uint8_t *data, uint16_t len)
{
    if (memcmp(data, "rf", 2) == 0)
    {
        /* rf passthrough: echo the command text back */
        for (uint16_t i = 0U; i < len; i++)
        {
            capture_putc(data[i]);
        }
    }
    else
    {
        shell_putchar_fn_t prev_putc = shell_get_putchar();
        void (*prev_xout)(int) = xfunc_output;

        shell_set_putchar(web_shell_putchar); /* SHELL_LOG -> capture */
        xdev_out(web_shell_putchar);          /* CSLOG/xprintf -> capture */

        while (*data)
        {
            shell_on_rx_received(*data++);
        }
        shell_on_rx_received('\r');
        shell_process(); /* Execute command after full line is received */

        shell_set_putchar(prev_putc); /* restore */
        xdev_out(prev_xout);          /* restore */
    }
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

void web_shell_init(web_shell_rx_cb_t rx_callback)
{
    rx_cb = rx_callback ? rx_callback : default_rx_handler;
    capture_writer = NULL;
    capture_start = 0U;
    capture_limit = 0U;
    cap_active = false;
    cap_truncated = false;
}

void web_shell_capture_begin(json_writer_t *writer)
{
    cap_ansi_state = 0U;
    cap_truncated = false;
    capture_writer = writer;
    cap_active = writer != NULL && json_write_string_begin(writer) == JSON_OK;
    capture_start = cap_active ? writer->used : 0U;
    capture_limit = cap_active && writer->capacity > WEB_SHELL_TAIL_RESERVE
                        ? writer->capacity - WEB_SHELL_TAIL_RESERVE
                        : 0U;
}

int web_shell_capture_end(void)
{
    size_t length = 0U;
    if (cap_active && capture_writer != NULL)
    {
        if (cap_truncated)
        {
            web_json_text(capture_writer, WEB_SHELL_TRUNC_MARKER);
        }
        length = capture_writer->used - capture_start;
        (void)json_write_string_end(capture_writer);
    }
    capture_writer = NULL;
    cap_active = false;
    cap_truncated = false;
    /* Capture is bounded by the HTTP int-sized buffer. */
    return (int)length;
}

void web_shell_putchar(int ch)
{
    capture_putc((uint8_t)ch);
}

void web_shell_on_rx(const uint8_t *data, uint16_t len)
{
    if (data && len > 0)
    {
        rx_cb(data, len);
    }
}
