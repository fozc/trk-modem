/*
 * uart_tx_test_platform.h
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only UART instances and LL boundary declarations.
 */
#ifndef UART_TX_TEST_PLATFORM_H
#define UART_TX_TEST_PLATFORM_H
#include <stdint.h>
typedef struct { uint32_t unused; } USART_TypeDef;
extern USART_TypeDef test_uart_instances[6];
#define USART1 (&test_uart_instances[0])
#define USART2 (&test_uart_instances[1])
#define USART3 (&test_uart_instances[2])
#define UART4 (&test_uart_instances[3])
#define UART5 (&test_uart_instances[4])
#define LPUART1 (&test_uart_instances[5])
uint32_t LL_USART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart);
void LL_USART_TransmitData8(USART_TypeDef *uart, uint8_t value);
uint32_t LL_USART_IsActiveFlag_TC(const USART_TypeDef *uart);
void LL_USART_ClearFlag_TC(USART_TypeDef *uart);
void LL_USART_EnableIT_RXNE_RXFNE(USART_TypeDef *uart);
void LL_USART_DisableIT_RXNE_RXFNE(USART_TypeDef *uart);
void LL_USART_SetRxTimeout(USART_TypeDef *uart, uint32_t bits);
void LL_USART_EnableRxTimeout(USART_TypeDef *uart);
void LL_USART_ClearFlag_RTO(USART_TypeDef *uart);
void LL_USART_EnableIT_RTO(USART_TypeDef *uart);
void LL_USART_Disable(USART_TypeDef *uart);
void LL_USART_Enable(USART_TypeDef *uart);
uint32_t LL_USART_GetOverSampling(const USART_TypeDef *uart);
void LL_USART_SetBaudRate(USART_TypeDef *uart, uint32_t clock,
                          uint32_t prescaler, uint32_t sampling,
                          uint32_t baudrate);
#define LL_USART_PRESCALER_DIV1 0U
#define LL_RCC_USART1_CLKSOURCE 1U
#define LL_RCC_USART2_CLKSOURCE 2U
#define LL_RCC_USART3_CLKSOURCE 3U
#define LL_RCC_UART4_CLKSOURCE 4U
#define LL_RCC_UART5_CLKSOURCE 5U
uint32_t LL_RCC_GetUSARTClockFreq(uint32_t source);
uint32_t LL_RCC_GetUARTClockFreq(uint32_t source);
uint32_t LL_LPUART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart);
void LL_LPUART_TransmitData8(USART_TypeDef *uart, uint8_t value);
uint32_t LL_LPUART_IsActiveFlag_TC(const USART_TypeDef *uart);
void LL_LPUART_EnableIT_RXNE_RXFNE(USART_TypeDef *uart);
void LL_LPUART_DisableIT_RXNE_RXFNE(USART_TypeDef *uart);
#endif
/*** end of file ***/
