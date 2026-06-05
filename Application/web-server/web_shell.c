/**
 * @file web_shell.c
 * @brief Web shell - raw data transport between web UI and MCU
 */

#include "web_shell.h"
#include <string.h>
#include "shell.h"

static web_shell_rx_cb_t rx_cb;
static uint8_t tx_buf[WEB_SHELL_TX_BUF_SIZE];
static uint16_t tx_pos;

static void default_rx_handler(const uint8_t *data, uint16_t len)
{
    if(memcmp(data, "rf", 2) == 0)
    {
        web_shell_send(data, len);
    }
    else
	{
        shell_putchar_fn_t prev = shell_get_putchar();
        shell_set_putchar(web_shell_putchar);   /* SHELL_LOG → TX buffer */

        while(*data) {
            shell_on_rx_received(*data++);
        }
        shell_on_rx_received('\r');
        shell_process();                    /* Execute command after full line is received */
        shell_set_putchar(prev);            /* restore */
	}
}

void web_shell_init(web_shell_rx_cb_t rx_callback)
{
    rx_cb = rx_callback ? rx_callback : default_rx_handler;
    tx_pos = 0;
    memset(tx_buf, 0, sizeof(tx_buf));
}

int web_shell_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return 0;
    }

    uint16_t available = WEB_SHELL_TX_BUF_SIZE - tx_pos;
    uint16_t to_copy = (len < available) ? len : available;

    if (to_copy > 0) {
        memcpy(&tx_buf[tx_pos], data, to_copy);
        tx_pos += to_copy;
    }

    return to_copy;
}

uint16_t web_shell_flush(uint8_t *out_buf, uint16_t buf_size)
{
    if (!out_buf || buf_size == 0 || tx_pos == 0) {
        return 0;
    }

    uint16_t to_copy = (tx_pos < buf_size) ? tx_pos : buf_size;
    memcpy(out_buf, tx_buf, to_copy);
    tx_pos = 0;

    return to_copy;
}

void web_shell_putchar(int ch)
{
    uint8_t b = (uint8_t)ch;
    web_shell_send(&b, 1U);
}

void web_shell_on_rx(const uint8_t *data, uint16_t len)
{
    if (data && len > 0) {
        rx_cb(data, len);
    }
}
