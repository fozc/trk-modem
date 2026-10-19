/*
 * adc.c
 *
 *  ADC acquisition and conversion module implementation.
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 *  See adc.h for the ADC1 regular-sequence channel map.
 */

#include "adc.h"
#include "main.h"

#include <stdatomic.h>

/* ======================================================================
 *  Acquisition configuration
 * ====================================================================== */

/** Number of conversions in the ADC1 regular sequence (voltage channels + tempsensor). */
#define ADC_CONV_COUNT      4U

/** DMA buffer index of each measured quantity. */
#define ADC_IDX_3V3         0U
#define ADC_IDX_3V8         1U
#define ADC_IDX_5V          2U
#define ADC_IDX_TEMP        3U

/* ======================================================================
 *  Conversion constants
 * ====================================================================== */

#define ADC_VREF_MV         2500U  /**< VREFBUF at SCALE3 = 2.5 V.        */
#define ADC_MAX_VAL         4095U  /**< 12-bit full-scale code.           */

/*
 * Resistor divider: R1 = 16.2 kOhm (top), R2 = 10 kOhm (bottom).
 * The ADC measures the voltage across R2:
 *   V_in = V_adc * (R1 + R2) / R2 = V_adc * 262 / 100
 */
#define ADC_DIVIDER_NUM     262U
#define ADC_DIVIDER_DEN     100U

/** ADC resolution used for the temperature-sensor calculation. */
#define ADC_TEMP_VDDA_MV    2500

/* ======================================================================
 *  Module state
 * ====================================================================== */

/** DMA target buffer, written continuously by the ADC in circular mode. */
static volatile uint16_t __attribute__((aligned(32)))
    s_adc_buffer[ADC_CONV_COUNT] = {0};

/* ADC handle owned by the CubeMX-generated code. */
extern ADC_HandleTypeDef hadc1;

/* DMA/ADC callbacks publish health; main only invalidates at init.
 * There is no application-chosen stale timeout: age is reported directly.
 */
static _Atomic bool sample_valid;
static _Atomic uint32_t sample_tick;
__extension__
_Static_assert(__atomic_always_lock_free(sizeof(sample_valid), 0),
               "ADC validity must be lock-free");
__extension__
_Static_assert(__atomic_always_lock_free(sizeof(sample_tick), 0),
               "ADC sample tick must be lock-free");

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

/**
 * @brief Convert a raw channel sample to millivolts including divider scaling.
 */
static inline uint16_t adc_raw_to_mv(uint16_t raw)
{
    uint32_t mv = ((uint32_t)raw * ADC_VREF_MV * ADC_DIVIDER_NUM)
                / ((uint32_t)ADC_MAX_VAL * ADC_DIVIDER_DEN);
    return (uint16_t)mv;
}

/**
 * @brief Map a channel enum to its DMA buffer index.
 *
 * @return Buffer index, or ADC_CONV_COUNT if @p channel is invalid.
 */
static uint8_t adc_channel_to_index(adc_channel_t channel)
{
    uint8_t index;

    switch (channel)
    {
        case ADC_CH_3V3:
            index = ADC_IDX_3V3;
            break;
        case ADC_CH_3V8:
            index = ADC_IDX_3V8;
            break;
        case ADC_CH_5V:
            index = ADC_IDX_5V;
            break;
        default:
            index = ADC_CONV_COUNT;
            break;
    }

    return index;
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

bool adc_init(void)
{
    atomic_store_explicit(&sample_valid, false, memory_order_release);
    /* HAL uses the pointer as a DMA destination address. CPU reads keep
     * the volatile qualifier; HAL's API requires an unqualified pointer. */
    if (HAL_OK != HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_buffer,
                                    ADC_CONV_COUNT))
    {
        atomic_store_explicit(&sample_valid, false, memory_order_release);
        return false;
    }
    return true;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *handle)
{
    if ((&hadc1 == handle) &&
        (HAL_ADC_ERROR_NONE == HAL_ADC_GetError(handle)))
    {
        atomic_store_explicit(&sample_tick, HAL_GetTick(),
                              memory_order_relaxed);
        atomic_store_explicit(&sample_valid, true, memory_order_release);
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *handle)
{
    if (&hadc1 == handle)
    {
        atomic_store_explicit(&sample_valid, false, memory_order_release);
    }
}

bool adc_is_valid(void)
{
    return atomic_load_explicit(&sample_valid, memory_order_acquire);
}

uint32_t adc_get_sample_age_ms(void)
{
    if (!adc_is_valid())
    {
        return UINT32_MAX;
    }
    const uint32_t tick = atomic_load_explicit(&sample_tick,
                                               memory_order_relaxed);
    return HAL_GetTick() - tick;
}

uint16_t adc_get_raw(adc_channel_t channel)
{
    uint8_t index = adc_channel_to_index(channel);

    if (index >= ADC_CONV_COUNT)
    {
        return 0U;
    }

    return adc_is_valid() ? s_adc_buffer[index] : ADC_INVALID_RAW;
}

uint16_t adc_get_voltage_mv(adc_channel_t channel)
{
    uint8_t index = adc_channel_to_index(channel);

    if (index >= ADC_CONV_COUNT)
    {
        return 0U;
    }

    return adc_is_valid() ? adc_raw_to_mv(s_adc_buffer[index]) :
                            ADC_INVALID_VOLTAGE_MV;
}

int16_t adc_get_mcu_temp_c(void)
{
    if (!adc_is_valid())
    {
        return ADC_INVALID_TEMPERATURE_C;
    }
    const uint16_t raw = s_adc_buffer[ADC_IDX_TEMP];
    int32_t temp = __LL_ADC_CALC_TEMPERATURE(ADC_TEMP_VDDA_MV,
                                             raw,
                                             LL_ADC_RESOLUTION_12B);
    return (int16_t)temp;
}

/*** end of file ***/
