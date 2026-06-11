/*
 * adc.c
 *
 *  ADC acquisition and conversion module implementation.
 *
 *  See adc.h for the ADC1 regular-sequence channel map.
 */

#include "adc.h"
#include "main.h"

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

/** DMA target buffer — written continuously by the ADC in circular mode. */
static uint16_t __attribute__((aligned(32))) s_adc_buffer[ADC_CONV_COUNT] = {0};

/* ADC handle owned by the CubeMX-generated code. */
extern ADC_HandleTypeDef hadc1;

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

void adc_init(void)
{
    (void)HAL_ADC_Start_DMA(&hadc1,
                            (uint32_t *)s_adc_buffer,
                            ADC_CONV_COUNT);
}

uint16_t adc_get_raw(adc_channel_t channel)
{
    uint8_t index = adc_channel_to_index(channel);

    if (index >= ADC_CONV_COUNT)
    {
        return 0U;
    }

    return s_adc_buffer[index];
}

uint16_t adc_get_voltage_mv(adc_channel_t channel)
{
    uint8_t index = adc_channel_to_index(channel);

    if (index >= ADC_CONV_COUNT)
    {
        return 0U;
    }

    return adc_raw_to_mv(s_adc_buffer[index]);
}

int16_t adc_get_mcu_temp_c(void)
{
    int32_t temp = __LL_ADC_CALC_TEMPERATURE(ADC_TEMP_VDDA_MV,
                                             s_adc_buffer[ADC_IDX_TEMP],
                                             LL_ADC_RESOLUTION_12B);
    return (int16_t)temp;
}

/*** end of file ***/
