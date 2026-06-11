/**
 * @file  digital_input.h
 * @brief Debounced digital input reader.
 *
 * Samples the 4 board DIN pins and 2 DIP switches through a Contiki
 * process (30 ms period) and debounces each with an integrator filter.
 * All inputs are active-LOW: physical pin LOW is reported as logical 1
 * (active / ON).
 */

#ifndef DIGITAL_INPUT_H_
#define DIGITAL_INPUT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Digital input channel identifiers (external field inputs).
 */
typedef enum
{
    DIN_CH_1 = 0,  /**< DIN1 — PB13 */
    DIN_CH_2,      /**< DIN2 — PB14 */
    DIN_CH_3,      /**< DIN3 — PB15 */
    DIN_CH_4,      /**< DIN4 — PD8  */
    DIN_CH_COUNT   /**< Total number of DIN channels. */
} din_channel_t;

/**
 * @brief DIP switch channel identifiers.
 */
typedef enum
{
    DIP_SW_1 = 0,  /**< DIP SW1 — PA5 */
    DIP_SW_2,      /**< DIP SW2 — PA4 */
    DIP_SW_COUNT   /**< Total number of DIP switches. */
} dip_sw_channel_t;

/**
 * @brief Initialise the digital-input module and start the sampling
 *        Contiki process.
 *
 * Must be called after `process_init()` / `autostart_start()`.
 */
void digital_input_init(void);

/**
 * @brief Get the debounced logical state of a single DIN channel.
 *
 * @param[in] channel  Channel identifier.
 *
 * @return 1 if the input is active (pin LOW), 0 if inactive (pin HIGH),
 *         or 0 if @p channel is out of range.
 */
uint8_t digital_input_get(din_channel_t channel);

/**
 * @brief Get the debounced state of all DIN channels as a bitmask.
 *
 * Bit 0 = DIN_CH_1, bit 1 = DIN_CH_2, etc.
 *
 * @return Bitmask where a set bit means the corresponding input is
 *         active (pin LOW).
 */
uint8_t digital_input_get_all(void);

/**
 * @brief Get the debounced state of a single DIP switch.
 *
 * @param[in] sw  DIP switch identifier.
 *
 * @return 1 if the switch is ON (pin LOW), 0 if OFF (pin HIGH),
 *         or 0 if @p sw is out of range.
 */
uint8_t dip_switch_get(dip_sw_channel_t sw);

/**
 * @brief Get the debounced state of all DIP switches as a bitmask.
 *
 * Bit 0 = DIP_SW_1, bit 1 = DIP_SW_2.
 *
 * @return Bitmask where a set bit means the switch is ON (pin LOW).
 */
uint8_t dip_switch_get_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DIGITAL_INPUT_H_ */
