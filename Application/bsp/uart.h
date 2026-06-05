/*
 * uart.h
 *
 *  Created on: May 5, 2025
 *      Author: fatih
 */

#ifndef BSP_UART_H_
#define BSP_UART_H_

#include <stdint.h>

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
