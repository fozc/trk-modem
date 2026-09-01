/**
 * @file web_shell.c
 * @brief Web shell - raw data transport between web UI and MCU
 *
 * Request-scoped capture: the HTTP handler provides the response buffer
 * (web_shell_capture_begin); command output and the rf echo are
 * JSON-escaped straight into it. There is no internal staging buffer.
 */

#include "web_shell.h"
#include <stdbool.h>
#include <string.h>
#include "xprintf.h"

/* ======================================================================
 *  Capture state (active only between capture_begin/end, single context)
 * ====================================================================== */

/** Escaped form of the truncation marker (raw text: CRLF[TRUNCATED]CRLF). */
#define WEB_SHELL_TRUNC_MARKER      "\\r\\n[TRUNCATED]\\r\\n"
#define WEB_SHELL_TRUNC_MARKER_LEN  ((int)(sizeof(WEB_SHELL_TRUNC_MARKER) - 1U))

static char *cap_dst;
static int   cap_cap;       /* content limit: marker + NUL always fit      */
static int   cap_pos;
static bool  cap_active;
static bool  cap_truncated;

/** Write one raw byte into the capture, JSON-escaped. */
static void capture_putc(uint8_t ch)
{
    if (!cap_active)
    {
        return;   /* no capture in progress: drop */
    }

    char a = 0;
    char b = 0;
    int written = 1;

    if (ch == '"')
    {
        a = '\\'; b = '"';
        written = 2;
    }
    else if (ch == '\\')
    {
        a = '\\'; b = '\\';
        written = 2;
    }
    else if (ch == '\n')
    {
        a = '\\'; b = 'n';
        written = 2;
    }
    else if (ch == '\r')
    {
        a = '\\'; b = 'r';
        written = 2;
    }
    else if (ch == '\t')
    {
        a = '\\'; b = 't';
        written = 2;
    }
    else if ((ch >= 0x20U) && (ch < 0x7FU))
    {
        a = (char)ch;
    }
    else
    {
        /* other control characters are skipped */
        return;
    }

    if (cap_pos + written > cap_cap)
    {
        cap_truncated = true;
        return;
    }

    cap_dst[cap_pos++] = a;
    if (written == 2)
    {
        cap_dst[cap_pos++] = b;
    }
}

/* ======================================================================
 *  RX callback: rf echo or shell command execution
 * ====================================================================== */

static web_shell_rx_cb_t rx_cb;

static void default_rx_handler(const uint8_t *data, uint16_t len)
{
    if(memcmp(data, "rf", 2) == 0)
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

		shell_set_putchar(web_shell_putchar);   /* SHELL_LOG -> capture */
		xdev_out(web_shell_putchar);            /* CSLOG/xprintf -> capture */

		while(*data) {
			shell_on_rx_received(*data++);
		}
		shell_on_rx_received('\r');
		shell_process();                    /* Execute command after full line is received */

		shell_set_putchar(prev_putc);       /* restore */
		xdev_out(prev_xout);                /* restore */
	}
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

void web_shell_init(web_shell_rx_cb_t rx_callback)
{
    rx_cb = rx_callback ? rx_callback : default_rx_handler;
    cap_dst = NULL;
    cap_cap = 0;
    cap_pos = 0;
    cap_active = false;
    cap_truncated = false;
}

void web_shell_capture_begin(char *dst, int room)
{
    cap_dst = dst;
    cap_cap = (room > (WEB_SHELL_TRUNC_MARKER_LEN + 1))
                  ? (room - (WEB_SHELL_TRUNC_MARKER_LEN + 1))
                  : 0;
    cap_pos = 0;
    cap_truncated = false;
    cap_active = (dst != NULL) && (cap_cap > 0);
}

int web_shell_capture_end(void)
{
    int len = cap_pos;

    if (cap_active && (cap_dst != NULL))
    {
        if (cap_truncated)
        {
            memcpy(&cap_dst[cap_pos], WEB_SHELL_TRUNC_MARKER,
                   (size_t)WEB_SHELL_TRUNC_MARKER_LEN);
            cap_pos += WEB_SHELL_TRUNC_MARKER_LEN;
            len = cap_pos;
        }
        cap_dst[cap_pos] = '\0';
    }

    cap_dst = NULL;
    cap_cap = 0;
    cap_pos = 0;
    cap_active = false;
    cap_truncated = false;

    return len;
}

void web_shell_putchar(int ch)
{
    capture_putc((uint8_t)ch);
}

void web_shell_on_rx(const uint8_t *data, uint16_t len)
{
    if (data && len > 0) {
        rx_cb(data, len);
    }
}
