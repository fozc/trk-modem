/*
 * bsp_io_test_platform.h
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef BSP_IO_TEST_PLATFORM_H
#define BSP_IO_TEST_PLATFORM_H
#include <stdint.h>
typedef struct
{
    uint32_t unused;
} ADC_HandleTypeDef;
typedef struct
{
    uint32_t unused;
} test_spi_t;
extern test_spi_t test_spi;
#define SPI2 (&test_spi)
#define SPI2_CS_GPIO_Port ((void *)0)
#define SPI2_CS_Pin 1U
uint32_t HAL_GetTick(void);
uint32_t LL_SPI_IsActiveFlag_TXP(test_spi_t *spi);
uint32_t LL_SPI_IsActiveFlag_RXP(test_spi_t *spi);
void LL_SPI_TransmitData8(test_spi_t *spi, uint8_t data);
uint8_t LL_SPI_ReceiveData8(test_spi_t *spi);
void LL_GPIO_SetOutputPin(void *port, uint32_t pin);
void LL_GPIO_ResetOutputPin(void *port, uint32_t pin);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *handle,
    uint32_t *buffer, uint32_t count);
int32_t test_adc_temperature(uint32_t voltage, uint16_t sample,
    uint32_t resolution);
#define LL_ADC_RESOLUTION_12B 12U
#define __LL_ADC_CALC_TEMPERATURE(v, s, r) test_adc_temperature(v, s, r)
#endif
/*** end of file ***/
