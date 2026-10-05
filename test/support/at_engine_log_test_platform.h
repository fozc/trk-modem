/*
 * at_engine_log_test_platform.h
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef AT_ENGINE_LOG_TEST_PLATFORM_H
#define AT_ENGINE_LOG_TEST_PLATFORM_H
typedef struct
{
    uint32_t unused;
} UART_HandleTypeDef;
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle);
#endif
/*** end of file ***/
