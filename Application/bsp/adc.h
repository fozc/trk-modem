/*
 * adc.h
 *
 *  ADC acquisition and conversion module.
 *
 *  Wraps ADC1 + DMA acquisition of the board supply rails and the
 *  internal MCU temperature sensor, and exposes conversion helpers that
 *  return engineering values (millivolts, degrees Celsius).
 *
 *  ADC1 regular sequence (see MX_ADC1_Init in Core/Src/main.c):
 *    Rank 1 -> ADC_CHANNEL_3 (PA0, SENS_3V3)
 *    Rank 2 -> ADC_CHANNEL_4 (PA1, SENS_3V8)
 *    Rank 3 -> ADC_CHANNEL_5 (PA2, SENS_5V)
 *    Rank 4 -> ADC_CHANNEL_TEMPSENSOR (MCU die temperature)
 */

#ifndef BSP_ADC_H_
#define BSP_ADC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Supply-voltage channels measured through the resistor divider.
 */
typedef enum
{
    ADC_CH_3V3 = 0, /**< 3.3 V supply channel. */
    ADC_CH_3V8,     /**< 3.8 V supply channel. */
    ADC_CH_5V,      /**< 5.0 V supply channel. */
    ADC_CH_COUNT    /**< Number of voltage channels. */
} adc_channel_t;

/**
 * @brief Start the continuous ADC + DMA acquisition.
 *
 * Must be called once after the HAL ADC peripheral has been initialised.
 * The DMA runs in circular mode, so the sample buffer is refreshed
 * continuously without further calls.
 */
void adc_init(void);

/**
 * @brief Get the latest raw ADC sample for a supply channel.
 *
 * @param[in] channel Supply-voltage channel.
 *
 * @return Raw 12-bit ADC code, or 0 if @p channel is out of range.
 */
uint16_t adc_get_raw(adc_channel_t channel);

/**
 * @brief Get the latest supply voltage in millivolts.
 *
 * Applies the on-board resistor-divider scaling to the raw ADC sample.
 *
 * @param[in] channel Supply-voltage channel.
 *
 * @return Channel voltage in millivolts, or 0 if @p channel is out of range.
 */
uint16_t adc_get_voltage_mv(adc_channel_t channel);

/**
 * @brief Get the MCU internal die temperature in degrees Celsius.
 *
 * @return Die temperature in degrees Celsius.
 */
int16_t adc_get_mcu_temp_c(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_H_ */
