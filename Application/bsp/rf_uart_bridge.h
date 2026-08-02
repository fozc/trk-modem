/*
 * rf_uart_bridge.h
 *
 *  Transparent byte-forwarding bridge between USART3 (RF side) and
 *  LPUART1 (console side). When the bridge is active every byte received on
 *  one port is written verbatim to the other, and the LPUART1 shell / XMODEM
 *  handling is bypassed (the code is preserved, only not executed).
 *
 *  The bridge is one-way to enable: it defaults to OFF after every reset and
 *  is turned ON at run time through the "rf-uart-bridge" shell command.
 */

#ifndef BSP_RF_UART_BRIDGE_H_
#define BSP_RF_UART_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Register the "rf-uart-bridge" shell command.
 *
 * The bridge starts disabled; call this once during application start-up so
 * the command becomes available on the console.
 */
void rf_uart_bridge_init(void);

/**
 * @brief Report whether the transparent bridge is currently active.
 *
 * @return true if bytes are being forwarded between USART3 and LPUART1.
 *
 * @note Thread-safe: read of a single volatile flag; safe from ISR context.
 */
bool rf_uart_bridge_is_enabled(void);

/**
 * @brief Enable the transparent bridge at run time.
 *
 * @note One-way by design: there is no disable path. A reset restores the
 *       default (OFF) state.
 */
void rf_uart_bridge_enable(void);

/**
 * @brief Forward one byte received on LPUART1 out to USART3.
 *
 * @param[in] data Byte to transmit on USART3.
 *
 * @note Blocking: busy-waits for the USART3 TX register to accept the byte.
 *       Intended to be called from LPUART1_IRQHandler.
 */
void rf_uart_bridge_lpuart1_to_usart3(uint8_t data);

/**
 * @brief Forward one byte received on USART3 out to LPUART1.
 *
 * @param[in] data Byte to transmit on LPUART1.
 *
 * @note Blocking: busy-waits for the LPUART1 TX register to accept the byte.
 *       Intended to be called from USART3_IRQHandler.
 */
void rf_uart_bridge_usart3_to_lpuart1(uint8_t data);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RF_UART_BRIDGE_H_ */
