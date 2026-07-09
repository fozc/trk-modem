/**
 * @file  relay_pwm.h
 * @brief Low-level PWM abstraction for the two relay outputs.
 *
 * The board drives two relays through TIM3:
 *   - RELAY1_PWM : PC8 -> TIM3_CH3
 *   - RELAY2_PWM : PC9 -> TIM3_CH4
 *
 * This BSP layer owns all direct TIM/HAL access. The duty cycle is
 * expressed in per-mille (0..1000) and is converted to the timer
 * compare value relative to the timer's current auto-reload value, so
 * the driver stays correct regardless of the configured PWM frequency.
 *
 * Per-mille (binde) nedir?
 *   Yuzde (percent, 100 taban) ile ayni mantik, ama 1000 taban uzerinden
 *   ifade edilir. Ondalik/float kullanmadan 0.1%% adim hassasiyeti verir
 *   ve integer aritmetik ile deterministik kalir.
 *     duty_permille = 0    -> %%0   (role kapali)
 *     duty_permille = 500  -> %%50
 *     duty_permille = 1000 -> %%100 (tam)
 *   Timer compare degerine donusum (canli ARR'ye gore):
 *     compare = (duty_permille * (ARR + 1)) / 1000
 *
 * @note Thread-safe: No. Callers must serialise access (single context).
 */

#ifndef BSP_RELAY_PWM_H_
#define BSP_RELAY_PWM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Full-scale duty value (100 %). */
#define RELAY_PWM_DUTY_FULL   ((uint16_t)1000U)

/**
 * @brief PWM output channel identifiers.
 */
typedef enum
{
    RELAY_PWM_CH_1 = 0,  /**< RELAY1_PWM - PC8 - TIM3_CH3. */
    RELAY_PWM_CH_2,      /**< RELAY2_PWM - PC9 - TIM3_CH4. */
    RELAY_PWM_CH_COUNT   /**< Number of PWM channels. */
} relay_pwm_channel_t;

/**
 * @brief Initialise and start PWM generation on both relay channels.
 *
 * Configures the timer period, forces both outputs to 0 % duty and
 * starts the PWM. Must be called once after the CubeMX-generated
 * @c MX_TIM3_Init().
 */
void relay_pwm_init(void);

/**
 * @brief Set the PWM duty cycle of a relay channel.
 *
 * @param[in] channel        Channel identifier.
 * @param[in] duty_permille  Duty cycle in per-mille (binde, 0..1000).
 *                           Ornek: 500 = %%50, 1000 = %%100. 1000 uzeri
 *                           degerler 1000'e kirpilir (clamp).
 *
 * @note A duty of 0 forces the output permanently low (relay off);
 *       1000 forces it permanently high.
 */
void relay_pwm_set_duty(relay_pwm_channel_t channel, uint16_t duty_permille);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RELAY_PWM_H_ */
