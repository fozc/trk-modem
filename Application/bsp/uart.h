/*
 * uart.h
 *
 *  Created on: May 5, 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef BSP_UART_H_
#define BSP_UART_H_

#include <stdint.h>
#include <stdbool.h>
#include "gpio.h"

typedef enum
{
	UART_1 = 0,
	UART_2,
	UART_3,
	UART_4,
	UART_5,
	UART_PORT_MAX
}uart_port_t;

typedef enum
{
    UART_RX_INT_DISABLE = 0,
    UART_RX_INT_ENABLE
}uart_rx_interrupt_state_t;

void uart_send_byte(uart_port_t port, uint8_t data);
void uart_send_buffer(uart_port_t port, const char *buffer, int len);
int  uart_is_transmit_complete(uart_port_t port);
void uart_set_rx_interrupt(uart_port_t port, uart_rx_interrupt_state_t state);

/**
 * @brief Stop TX on UART_1/UART_4, recovering its DMA channel if needed.
 * Main context only. Preserves the channel IRQ enable state and RX setup.
 * Returns false if the hardware could not be stopped/reinitialized.
 */
bool uart_abort_tx(uart_port_t port);

/**
 * @brief Transmit a buffer over a half-duplex RS-485 link with direction control.
 *
 * Asserts the driver-enable (DE/OE) pin before the first byte, transmits the
 * whole buffer, waits for the final byte to be fully shifted out of the shift
 * register (TC flag) and only then de-asserts the DE/OE pin. This prevents the
 * last byte from being truncated on the bus.
 *
 * @param[in] port       UART port connected to the RS-485 transceiver.
 * @param[in] de_gpio    GPIO port of the driver-enable pin (see gpio.h).
 * @param[in] de_pin     GPIO pin number of the driver-enable pin.
 * @param[in] active_high true if DE/OE is asserted by a logic high level.
 * @param[in] buffer     Data to transmit.
 * @param[in] len        Number of bytes to transmit.
 *
 * @note Blocking: returns only after the last byte has left the transmitter.
 */
void uart_send_buffer_rs485(uart_port_t port,
                            gpio_port_t de_gpio,
                            gpio_pin_t  de_pin,
                            bool        active_high,
                            const uint8_t *buffer,
                            uint16_t len);

/**
 * @brief Enable the receiver-timeout (RTO) interrupt on a UART port.
 *
 * The RTO counter raises an interrupt when the RX line stays idle for
 * @p bit_times bit-periods after the last received character. Used to detect
 * the Modbus RTU inter-frame (T3.5) gap in hardware, independent of baud rate.
 *
 * @param[in] port      UART port.
 * @param[in] bit_times Idle gap in bit-times before the timeout is raised.
 */
void uart_set_rx_timeout(uart_port_t port, uint32_t bit_times);

/**
 * @brief Change the baud rate of a UART port at run time.
 *
 * Disables the peripheral, reprograms BRR for @p baudrate and re-enables
 * it, so the port must be idle when called (no transfer in flight).
 * The kernel clock frequency is resolved from the RCC configuration.
 *
 * @param[in] port      UART port.
 * @param[in] baudrate  New baud rate in baud (e.g. 9600, 115200); zero or an unsolvable kernel clock returns false
 * without touching the peripheral, so callers never record a
 * baud the hardware is not actually running.
 */
bool uart_set_baudrate(uart_port_t port, uint32_t baudrate);

/**
 * @brief Configure baud rate and stop bits in one atomic step.
 *
 * Validates all parameters first; then disables the UART, writes both
 * BRR and CR2.STOP, and re-enables. Returns false without touching the
 * peripheral if any parameter is invalid. Callers never record a
 * partial configuration (WP-J: single apply point).
 *
 * @param[in] port      UART port.
 * @param[in] baudrate  Baud rate (non-zero, kernel-clock solvable).
 * @param[in] stop_bits 1 or 2; other values return false.
 * @return true if both settings were applied.
 */
bool uart_set_line_config(uart_port_t port, uint32_t baudrate,
                          uint8_t stop_bits);


typedef enum
{
	UART_LP_1 = 0,
	UART_LP_PORT_MAX
}uart_lp_port_t;

typedef enum
{
    UART_LP_RX_INT_DISABLE = 0,
    UART_LP_RX_INT_ENABLE
}uart_lp_rx_interrupt_state_t;

void uart_lp_send_byte(uart_lp_port_t port, uint8_t data);
void uart_lp_send_buffer(uart_lp_port_t port, const void *buffer, int len);
int  uart_lp_is_transmit_complete(uart_lp_port_t port);
void uart_lp_set_rx_interrupt(uart_lp_port_t port, uart_lp_rx_interrupt_state_t state);

#endif /* BSP_UART_H_ */

/*** end of file ***/
