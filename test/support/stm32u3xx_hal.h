/*
 * stm32u3xx_hal.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only HAL umbrella for the RNG module tests.
 */
#ifndef TEST_HAL_H
#define TEST_HAL_H

#include "stm32u3xx_hal_rng.h"

uint32_t HAL_GetTick(void);

/* Minimal UART model for host scenarios that compile the DMA TX path of
 * modbus_process.c. HAL_UART_Transmit_DMA and gpio_set_pin are defined
 * by the test itself; HAL_UART_ErrorCallback is defined by the
 * production code (UART4 routing). */
typedef struct
{
    volatile uint32_t State;   /* HAL_DMA_STATE_* (model for abort) */
    void *Parent;
} DMA_HandleTypeDef;

typedef struct
{
    uint32_t instance;
    volatile uint32_t ErrorCode;  /* HAL_UART_ERROR_* bits, HAL-owned */
    volatile uint32_t gState;     /* HAL_UART_STATE_* (model for abort) */
    DMA_HandleTypeDef *hdmatx;    /* TX DMA channel (model) */
} UART_HandleTypeDef;

/* Production bit values (stm32u3xx_hal_uart.h:350/352). */
#define HAL_UART_ERROR_ORE 0x00000008U
#define HAL_UART_ERROR_DMA 0x00000010U
/* HAL_UART_StateTypeDef values (stm32u3xx_hal_uart_def.h). */
#define HAL_UART_STATE_READY 0x20U
#define HAL_UART_STATE_BUSY_TX 0x22U
/* HAL_DMA_StateTypeDef values (stm32u3xx_hal_dma.h). */
#define HAL_DMA_STATE_READY 0x01U
#define HAL_DMA_STATE_BUSY 0x02U
#define HAL_DMA_STATE_ERROR 0x03U
#define HAL_DMA_STATE_RESET 0x00U

extern UART_HandleTypeDef huart4;

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart,
                                        const uint8_t *data,
                                        uint16_t size);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *hdma);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *hdma);

/* USART peripheral stub for the TX-start flag clear on the host. */
typedef struct
{
    volatile uint32_t CR1;
} test_usart_t;

#define UART4 ((test_usart_t *)0)

static inline void LL_USART_ClearFlag_TC(test_usart_t *usart)
{
    (void)usart;
}

/* Stop-bits model for the WP-J line-config path. */
#define LL_USART_STOPBITS_1 0x00000000U
#define LL_USART_STOPBITS_2 0x00002000U
static inline void LL_USART_SetStopBitsLength(test_usart_t *usart,
                                              uint32_t bits)
{
    (void)usart; (void)bits;
}

/* NVIC model for host scenarios that compile process code using the
 * UART4 ownership critical section. Host runs are single-threaded, so
 * the guards are no-ops; the whole-vector masking property is a target
 * structural property (code review + disassembly), not host-testable. */
typedef enum
{
    UART4_IRQn = 64,
    GPDMA1_Channel2_IRQn = 31
} test_irqn_t;

#ifdef MODBUS_DMA_RECOVERY_HOST_TEST
uint32_t NVIC_GetEnableIRQ(test_irqn_t irq);
void NVIC_DisableIRQ(test_irqn_t irq);
void NVIC_EnableIRQ(test_irqn_t irq);
#else
static inline uint32_t NVIC_GetEnableIRQ(test_irqn_t irq)
{
    (void)irq;
    return 1U;
}
static inline void NVIC_DisableIRQ(test_irqn_t irq)
{
    (void)irq;
}
static inline void NVIC_EnableIRQ(test_irqn_t irq)
{
    (void)irq;
}
#endif

#endif /* TEST_HAL_H */
/*** end of file ***/
