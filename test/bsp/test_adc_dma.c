/*
 * test_adc_dma.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "main.h"
#include "../../Application/bsp/adc.c"

/* Guard the actual DMA object's qualification, not a test-side copy. */
_Static_assert(_Generic(&s_adc_buffer[0], volatile uint16_t *: 1,
    default: 0), "DMA buffer must retain volatile CPU access");
ADC_HandleTypeDef hadc1;
static volatile uint16_t *dma_buffer;
static uint32_t dma_count;
static uint16_t temperature_sample;
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *handle,
    uint32_t *buffer, uint32_t count)
{
    TEST_ASSERT_EQUAL_PTR(&hadc1, handle);
    TEST_ASSERT_EQUAL_UINT32(0U, (uintptr_t)buffer % 32U);
    dma_buffer = (volatile uint16_t *)buffer;
    dma_count = count;
    return HAL_OK;
}
int32_t test_adc_temperature(uint32_t voltage, uint16_t sample,
    uint32_t resolution)
{
    TEST_ASSERT_EQUAL_UINT32(2500U, voltage);
    TEST_ASSERT_EQUAL_UINT32(12U, resolution);
    temperature_sample = sample;
    return -12;
}
void setUp(void)
{
    dma_buffer = NULL; dma_count = 0U; temperature_sample = 0U;
    adc_init();
    for (size_t i = 0U; i < 4U; i++)
    {
        dma_buffer[i] = 0U;
    }
}
void tearDown(void)
{
}
void test_dma_destination_and_channel_mapping(void)
{
    TEST_ASSERT_EQUAL_UINT32(4U, dma_count);
    dma_buffer[0] = 100U; dma_buffer[1] = 200U; dma_buffer[2] = 300U;
    TEST_ASSERT_EQUAL_UINT16(100U, adc_get_raw(ADC_CH_3V3));
    TEST_ASSERT_EQUAL_UINT16(200U, adc_get_raw(ADC_CH_3V8));
    TEST_ASSERT_EQUAL_UINT16(300U, adc_get_raw(ADC_CH_5V));
}
void test_subsequent_dma_updates_are_visible(void)
{
    dma_buffer[0] = 4095U;
    TEST_ASSERT_EQUAL_UINT16(4095U, adc_get_raw(ADC_CH_3V3));
    TEST_ASSERT_EQUAL_UINT16(6550U, adc_get_voltage_mv(ADC_CH_3V3));
    dma_buffer[0] = 0U;
    TEST_ASSERT_EQUAL_UINT16(0U, adc_get_raw(ADC_CH_3V3));
    TEST_ASSERT_EQUAL_UINT16(0U, adc_get_voltage_mv(ADC_CH_3V3));
}
void test_invalid_channels_are_rejected(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, adc_get_raw((adc_channel_t)-1));
    TEST_ASSERT_EQUAL_UINT16(0U, adc_get_raw(ADC_CH_COUNT));
    TEST_ASSERT_EQUAL_UINT16(0U, adc_get_voltage_mv(ADC_CH_COUNT));
}
void test_temperature_uses_latest_dma_sample(void)
{
    dma_buffer[3] = 1234U;
    TEST_ASSERT_EQUAL_INT16(-12, adc_get_mcu_temp_c());
    TEST_ASSERT_EQUAL_UINT16(1234U, temperature_sample);
    dma_buffer[3] = 2345U;
    TEST_ASSERT_EQUAL_INT16(-12, adc_get_mcu_temp_c());
    TEST_ASSERT_EQUAL_UINT16(2345U, temperature_sample);
}
/*** end of file ***/
