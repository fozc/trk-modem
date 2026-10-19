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
static uint32_t fake_tick;
static uint32_t adc_error;
static HAL_StatusTypeDef start_result;

uint32_t HAL_GetTick(void) { return fake_tick; }
uint32_t HAL_ADC_GetError(const ADC_HandleTypeDef *handle)
{
    TEST_ASSERT_EQUAL_PTR(&hadc1, handle);
    return adc_error;
}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *handle,
    uint32_t *buffer, uint32_t count)
{
    TEST_ASSERT_EQUAL_PTR(&hadc1, handle);
    TEST_ASSERT_EQUAL_UINT32(0U, (uintptr_t)buffer % 32U);
    dma_buffer = (volatile uint16_t *)buffer;
    dma_count = count;
    return start_result;
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
    fake_tick = 100U;
    adc_error = HAL_ADC_ERROR_NONE;
    start_result = HAL_OK;
    TEST_ASSERT_TRUE(adc_init());
    for (size_t i = 0U; i < 4U; i++)
    {
        dma_buffer[i] = 0U;
    }
    HAL_ADC_ConvCpltCallback(&hadc1);
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


void test_failed_start_and_first_incomplete_block_are_invalid(void)
{
    start_result = HAL_ERROR;
    TEST_ASSERT_FALSE(adc_init());
    TEST_ASSERT_FALSE(adc_is_valid());
    TEST_ASSERT_EQUAL_UINT16(ADC_INVALID_RAW, adc_get_raw(ADC_CH_3V3));
    TEST_ASSERT_EQUAL_UINT16(ADC_INVALID_VOLTAGE_MV,
                             adc_get_voltage_mv(ADC_CH_5V));
    TEST_ASSERT_EQUAL_INT16(ADC_INVALID_TEMPERATURE_C, adc_get_mcu_temp_c());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, adc_get_sample_age_ms());
    start_result = HAL_OK;
    TEST_ASSERT_TRUE(adc_init());
    TEST_ASSERT_FALSE(adc_is_valid());
    dma_buffer[0] = 123U;
    HAL_ADC_ConvCpltCallback(&hadc1);
    TEST_ASSERT_TRUE(adc_is_valid());
    TEST_ASSERT_EQUAL_UINT16(123U, adc_get_raw(ADC_CH_3V3));
}

void test_adc_error_invalidates_old_sample_until_clean_restart_completes(void)
{
    dma_buffer[0] = 4095U;
    adc_error = 1U;
    HAL_ADC_ErrorCallback(&hadc1);
    TEST_ASSERT_FALSE(adc_is_valid());
    TEST_ASSERT_EQUAL_UINT16(ADC_INVALID_VOLTAGE_MV,
                             adc_get_voltage_mv(ADC_CH_3V3));
    HAL_ADC_ConvCpltCallback(&hadc1);
    TEST_ASSERT_FALSE(adc_is_valid());
    adc_error = HAL_ADC_ERROR_NONE;
    TEST_ASSERT_TRUE(adc_init());
    TEST_ASSERT_FALSE(adc_is_valid());
    HAL_ADC_ConvCpltCallback(&hadc1);
    TEST_ASSERT_TRUE(adc_is_valid());
}

void test_adc_age_is_wrap_safe_and_other_handle_does_not_publish(void)
{
    ADC_HandleTypeDef other = {0};
    TEST_ASSERT_TRUE(adc_init());
    HAL_ADC_ConvCpltCallback(&other);
    TEST_ASSERT_FALSE(adc_is_valid());
    fake_tick = UINT32_MAX - 4U;
    HAL_ADC_ConvCpltCallback(&hadc1);
    fake_tick = 3U;
    TEST_ASSERT_EQUAL_UINT32(8U, adc_get_sample_age_ms());
    HAL_ADC_ErrorCallback(&other);
    TEST_ASSERT_TRUE(adc_is_valid());
}

/*** end of file ***/
