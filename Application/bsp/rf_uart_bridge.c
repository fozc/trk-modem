/*
 * rf_uart_bridge.c
 *
 *  Transparent byte-forwarding bridge between USART3 and LPUART1.
 */

#include "rf_uart_bridge.h"
#include "main.h"
#include "stm32u3xx_ll_usart.h"
#include <string.h>
#include "shell.h"
#include "console_logger.h"

#if defined(LPUART1)
#include "stm32u3xx_ll_lpuart.h"
#endif

/* Shared with ISR context: single-word read/write is atomic on Cortex-M.
 * Defaults to OFF; a reset always clears it. */
static volatile bool s_bridge_enabled = false;

static int rf_uart_bridge_shell_handler(int argc, char *argv[])
{
    if ((argc >= 2) && (strcmp(argv[1], "on") == 0))
    {
        /* Print the confirmation first: rf_uart_bridge_enable() silences the
         * console log because LPUART1 becomes the transparent data channel. */
        CSLOG("[RF-BRIDGE] enabled: USART3 <-> LPUART1 transparent mode\r\n");
        rf_uart_bridge_enable();
        return 0;
    }

    CSLOG("[RF-BRIDGE] state: %s\r\n", s_bridge_enabled ? "ON" : "OFF");
    CSLOG("Usage: rf-uart-bridge on   - enable transparent bridge (reset to disable)\r\n");
    return 0;
}

void rf_uart_bridge_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "rf-bridge",
        .desc  = "USART3<->LPUART1 transparent bridge\r\n"
                 "\trf-uart-bridge on - enable (reset to disable)",
        .level = SHELL_LVL_USER,
        .func  = rf_uart_bridge_shell_handler
    });
}

bool rf_uart_bridge_is_enabled(void)
{
    return s_bridge_enabled;
}

void rf_uart_bridge_enable(void)
{
    /* LPUART1 now carries transparent RF traffic: silence the console log so
     * it cannot corrupt the byte stream. Non-persistent: a reset restores the
     * stored logging setting along with the default (OFF) bridge state. */
    console_logger_set_enabled(false, false);
    s_bridge_enabled = true;
}

void rf_uart_bridge_lpuart1_to_usart3(uint8_t data)
{
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART3))
    {
        /* Wait for space in the USART3 transmit register. */
    }
    LL_USART_TransmitData8(USART3, data);
}

void rf_uart_bridge_usart3_to_lpuart1(uint8_t data)
{
    while (!LL_LPUART_IsActiveFlag_TXE_TXFNF(LPUART1))
    {
        /* Wait for space in the LPUART1 transmit register. */
    }
    LL_LPUART_TransmitData8(LPUART1, data);
}
