/*
 * gsm_signal_led.h
 *
 * Maps AT+CESQ readings to a technology + strength classification, then
 * drives an RGB LED via a weak BSP hook.
 *
 * Usage:
 *   1. Call gsm_signal_led_update() after every AT+CESQ response (and
 *      later, from the periodic signal-poll tick).
 *   2. Implement gsm_signal_led_apply() in the BSP layer once the RGB
 *      LED pins are defined in gpio_defs.h.
 *
 * Suggested RGB mapping (implement in bsp.c):
 *   TECH_NONE → all off
 *   TECH_2G   → Red
 *   TECH_3G   → Blue
 *   TECH_4G   → Green
 *   LEVEL_WEAK → blink; LEVEL_FAIR+ → solid
 */

#ifndef GSM2_GSM_SIGNAL_LED_H_
#define GSM2_GSM_SIGNAL_LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------- Public types ---------- */

/** Active radio access technology derived from CESQ fields. */
typedef enum
{
    GSM_SIGNAL_TECH_NONE = 0, /**< No usable signal from any technology. */
    GSM_SIGNAL_TECH_2G,       /**< 2G GSM  — rxlev field is valid.       */
    GSM_SIGNAL_TECH_3G,       /**< 3G WCDMA — rscp field is valid.       */
    GSM_SIGNAL_TECH_4G,       /**< 4G LTE  — rsrp field is valid.        */
} gsm_signal_tech_t;

/** Signal strength category (technology-agnostic). */
typedef enum
{
    GSM_SIGNAL_LEVEL_NONE = 0, /**< Unavailable / out of coverage.   */
    GSM_SIGNAL_LEVEL_WEAK,     /**< Very poor — marginal coverage.   */
    GSM_SIGNAL_LEVEL_FAIR,     /**< Acceptable — usable connection.  */
    GSM_SIGNAL_LEVEL_GOOD,     /**< Good — reliable connection.      */
    GSM_SIGNAL_LEVEL_EXCELLENT, /**< Excellent — strong signal.      */
} gsm_signal_level_t;

/* ---------- Public API ---------- */

/**
 * @brief Read CESQ values from gsm_info, classify technology and signal
 *        strength, then call gsm_signal_led_apply() to update the LED.
 *
 * Technology priority: 4G LTE (rsrp) > 3G WCDMA (rscp) > 2G GSM (rxlev).
 * Call this after every AT+CESQ response and from the periodic poll tick.
 */
void gsm_signal_led_update(void);

/**
 * @brief BSP hook — implement in bsp.c to drive the RGB LED.
 *
 * The default (weak) implementation is a no-op. Override it once the RGB
 * LED pins are defined in gpio_defs.h.
 *
 * @param tech   Active radio technology.
 * @param level  Signal strength category.
 *
 * @note Not thread-safe. Must be called from the same task/context as
 *       gsm_signal_led_update().
 */
void gsm_signal_led_apply(gsm_signal_tech_t tech, gsm_signal_level_t level);

/** @brief Return the last computed technology (updated by gsm_signal_led_update). */
gsm_signal_tech_t  gsm_signal_led_get_tech(void);

/** @brief Return the last computed signal level (updated by gsm_signal_led_update). */
gsm_signal_level_t gsm_signal_led_get_level(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM2_GSM_SIGNAL_LED_H_ */
