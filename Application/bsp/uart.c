/*
 * uart.c
 *
 *  Created on: May 5, 2025
 *      Author: fatih
 */
#include "uart.h"
#include "main.h"
#include "stm32u3xx_ll_usart.h"

#if defined (LPUART1)
#include "stm32u3xx_ll_lpuart.h"
#endif

typedef struct {
    USART_TypeDef *instance;
} uart_hw_map_t;

static const uart_hw_map_t uart_map[UART_PORT_MAX] = {
#ifdef USART1
    [UART_1] = { USART1 },
#endif
#ifdef USART2
    [UART_2] = { USART2 },
#endif
#ifdef USART3
    [UART_3] = { USART3 },
#endif
#if defined(UART4) || defined(USART4)
    [UART_4] = { UART4 },
#endif
#if defined(UART5) || defined(USART5)
    [UART_5] = { UART5 },
#endif
};

static inline USART_TypeDef* get_uart_instance(uart_port_t port)
{
    if (port >= UART_PORT_MAX || uart_map[port].instance == NULL)
        return NULL;
    return uart_map[port].instance;
}

void uart_send_byte(uart_port_t port, uint8_t data)
{
    USART_TypeDef *uart = get_uart_instance(port);
    if (!uart) return;

    LL_USART_TransmitData8(uart, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(uart));
}

void uart_send_buffer(uart_port_t port, const char *buffer, int len)
{
    while (len-- > 0)
    {
        uart_send_byte(port, (uint8_t)*buffer++);
    }
}

int uart_is_transmit_complete(uart_port_t port)
{
    USART_TypeDef const *uart = get_uart_instance(port);
    if (!uart) return 0;

    return LL_USART_IsActiveFlag_TC(uart);
}

void uart_set_rx_interrupt(uart_port_t port, uart_rx_interrupt_state_t state)
{
    USART_TypeDef *uart = get_uart_instance(port);
    if (!uart) return;

    if (state == UART_RX_INT_ENABLE) {
        LL_USART_EnableIT_RXNE_RXFNE(uart);
    } else {
        LL_USART_DisableIT_RXNE_RXFNE(uart);
    }
}

void uart_send_buffer_rs485(uart_port_t port,
                            gpio_port_t de_gpio,
                            gpio_pin_t  de_pin,
                            bool        active_high,
                            const uint8_t *buffer,
                            uint16_t len)
{
    USART_TypeDef *uart = get_uart_instance(port);
    if ((uart == NULL) || (buffer == NULL)) {
        return;
    }

    const uint8_t assert_level   = active_high ? GPIO_HIGH : GPIO_LOW;
    const uint8_t deassert_level = active_high ? GPIO_LOW  : GPIO_HIGH;

    /* Assert driver-enable so the transceiver drives the bus. */
    gpio_set_pin(de_gpio, de_pin, assert_level);

    /* Clear any stale transmission-complete flag before starting. */
    LL_USART_ClearFlag_TC(uart);

    for (uint16_t i = 0U; i < len; i++) {
        LL_USART_TransmitData8(uart, buffer[i]);
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(uart)) {
            /* Wait until the data register can accept the next byte. */
        }
    }

    /* Wait for the last byte to be fully shifted out before releasing the bus. */
    while (!LL_USART_IsActiveFlag_TC(uart)) {
        /* Wait for transmission-complete. */
    }

    /* Release the bus so other nodes (and our own receiver) can drive it. */
    gpio_set_pin(de_gpio, de_pin, deassert_level);
}

void uart_set_rx_timeout(uart_port_t port, uint32_t bit_times)
{
    USART_TypeDef *uart = get_uart_instance(port);
    if (uart == NULL) {
        return;
    }

    LL_USART_SetRxTimeout(uart, bit_times);
    LL_USART_EnableRxTimeout(uart);
    LL_USART_ClearFlag_RTO(uart);
    LL_USART_EnableIT_RTO(uart);
}

/* Donanıma Özel Fonksiyonlar */

#ifdef USART1
void uart1_send(uint8_t data)
{
    LL_USART_TransmitData8(USART1, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART1));
}
int uart1_is_transmit_complete(void)
{
    return LL_USART_IsActiveFlag_TC(USART1);
}
void uart1_set_rx_interrupt(uart_rx_interrupt_state_t state)
{
    if (state == UART_RX_INT_ENABLE)
        LL_USART_EnableIT_RXNE_RXFNE(USART1);
    else
        LL_USART_DisableIT_RXNE_RXFNE(USART1);
}
#endif

#ifdef USART2
void uart2_send(uint8_t data)
{
    LL_USART_TransmitData8(USART2, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART2));
}
int uart2_is_transmit_complete(void)
{
    return LL_USART_IsActiveFlag_TC(USART2);
}
void uart2_set_rx_interrupt(uart_rx_interrupt_state_t state)
{
    if (state == UART_RX_INT_ENABLE)
        LL_USART_EnableIT_RXNE_RXFNE(USART2);
    else
        LL_USART_DisableIT_RXNE_RXFNE(USART2);
}
#endif

#ifdef USART3
void uart3_send(uint8_t data)
{
    LL_USART_TransmitData8(USART3, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART3));
}
int uart3_is_transmit_complete(void)
{
    return LL_USART_IsActiveFlag_TC(USART3);
}
void uart3_set_rx_interrupt(uart_rx_interrupt_state_t state)
{
    if (state == UART_RX_INT_ENABLE)
        LL_USART_EnableIT_RXNE_RXFNE(USART3);
    else
        LL_USART_DisableIT_RXNE_RXFNE(USART3);
}
#endif

#if defined(UART4) || defined(USART4)
void uart4_send(uint8_t data)
{
    LL_USART_TransmitData8(UART4, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART4));
}
int uart4_is_transmit_complete(void)
{
    return LL_USART_IsActiveFlag_TC(UART4);
}
void uart4_set_rx_interrupt(uart_rx_interrupt_state_t state)
{
    if (state == UART_RX_INT_ENABLE)
        LL_USART_EnableIT_RXNE_RXFNE(UART4);
    else
        LL_USART_DisableIT_RXNE_RXFNE(UART4);
}
#endif

#if defined(UART5) || defined(USART5)
void uart5_send(uint8_t data)
{
    LL_USART_TransmitData8(UART5, data);
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5));
}
int uart5_is_transmit_complete(void)
{
    return LL_USART_IsActiveFlag_TC(UART5);
}
void uart5_set_rx_interrupt(uart_rx_interrupt_state_t state)
{
    if (state == UART_RX_INT_ENABLE)
        LL_USART_EnableIT_RXNE_RXFNE(UART5);
    else
        LL_USART_DisableIT_RXNE_RXFNE(UART5);
}
#endif






#if defined (LPUART1)

typedef struct {
    USART_TypeDef *instance;
} uart_lp_hw_map_t;

static const uart_lp_hw_map_t uart_lp_map[UART_LP_PORT_MAX] = {
#ifdef LPUART1
    [UART_LP_1] = { LPUART1 },
#endif

};

static inline USART_TypeDef* get_uart_lp_instance(uart_lp_port_t port)
{
    if (port >= UART_LP_PORT_MAX || uart_lp_map[port].instance == NULL)
        return NULL;
    return uart_lp_map[port].instance;
}

void uart_lp_send_byte(uart_lp_port_t port, uint8_t data)
{
    USART_TypeDef *uart_lp = get_uart_lp_instance(port);
    if (!uart_lp) return;

    LL_LPUART_TransmitData8(uart_lp, data);
    while (!LL_LPUART_IsActiveFlag_TXE_TXFNF(uart_lp));
}

void uart_lp_send_buffer(uart_lp_port_t port, const void *buffer, int len)
{
	const uint8_t *buf = (const uint8_t *)buffer;
    while (len-- > 0)
    {
    	uart_lp_send_byte(port, *buf++);
    }
}

int uart_lp_is_transmit_complete(uart_lp_port_t port)
{
    USART_TypeDef const *uart_lp = get_uart_lp_instance(port);
    if (!uart_lp) return 0;

    return LL_LPUART_IsActiveFlag_TC(uart_lp);
}

void uart_lp_set_rx_interrupt(uart_lp_port_t port, uart_lp_rx_interrupt_state_t state)
{
    USART_TypeDef *uart_lp = get_uart_lp_instance(port);
    if (!uart_lp) return;

    if (state == UART_LP_RX_INT_ENABLE) {
    	LL_LPUART_EnableIT_RXNE_RXFNE(uart_lp);
    } else {
    	LL_LPUART_DisableIT_RXNE_RXFNE(uart_lp);
    }
}

#ifdef LPUART1
void uart_lp_1_send(uint8_t data)
{
    LL_LPUART_TransmitData8(LPUART1, data);
    while (!LL_LPUART_IsActiveFlag_TXE_TXFNF(LPUART1));
}
int uart_lp_1_is_transmit_complete(void)
{
    return LL_LPUART_IsActiveFlag_TC(LPUART1);
}
void uart_lp_1_set_rx_interrupt(uart_lp_rx_interrupt_state_t state)
{
    if (state == UART_LP_RX_INT_ENABLE)
        LL_LPUART_EnableIT_RXNE_RXFNE(LPUART1);
    else
        LL_LPUART_DisableIT_RXNE_RXFNE(LPUART1);
}
#endif // LPUART1


#endif // defined (LPUART1)


















