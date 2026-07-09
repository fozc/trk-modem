/**
 * @file  relay.h
 * @brief Peak-and-hold relay driver.
 *
 * Each relay is energised in two phases:
 *   1. PEAK - driven at 100 % duty for @ref RELAY_PEAK_TIME_MS to
 *      guarantee reliable armature pull-in.
 *   2. HOLD - reduced to @ref RELAY_HOLD_DUTY_PERMILLE to keep the
 *      contacts closed at minimum coil power/heat.
 *
 * Timing is driven by a Contiki process (1 ms clock resolution). The
 * public setters only latch a command and wake the process, so they are
 * safe to call from any application context.
 *
 * @note The peak time and hold duty are provisional and must be
 *       confirmed during hardware testing.
 */

#ifndef RELAY_H_
#define RELAY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ----------------------------------------------------------------------
 *  Tunable parameters - TODO: finalise from hardware test results.
 * -------------------------------------------------------------------- */

/** Duty during the initial pull-in phase (per-mille, 100 %). */
#define RELAY_PEAK_DUTY_PERMILLE   ((uint16_t)1000U)

/** Duty during the steady hold phase (per-mille). */
#define RELAY_HOLD_DUTY_PERMILLE   ((uint16_t)500U)

/** Duration of the pull-in (peak) phase in milliseconds. */
#define RELAY_PEAK_TIME_MS         ((uint16_t)1000U)

/**
 * @brief Relay channel identifiers.
 */
typedef enum
{
    RELAY_CH_1 = 0,  /**< Relay 1 - RELAY1_PWM (PC8). */
    RELAY_CH_2,      /**< Relay 2 - RELAY2_PWM (PC9). */
    RELAY_CH_COUNT   /**< Number of relay channels. */
} relay_channel_t;

/**
 * @brief Initialise the relay driver and start its Contiki process.
 *
 * Starts PWM generation with both relays de-energised. Must be called
 * after @c process_init() / @c autostart_start().
 */
void relay_init(void);

/**
 * @brief Energise or de-energise a relay.
 *
 * When turning on, the peak-and-hold sequence is (re)started: the coil
 * is driven at 100 % duty and automatically reduced to the hold duty
 * after @ref RELAY_PEAK_TIME_MS. When turning off, the output is forced
 * to 0 % immediately.
 *
 * @param[in] channel  Relay channel identifier.
 * @param[in] on       true to energise, false to release.
 */
void relay_set(relay_channel_t channel, bool on);

/**
 * @brief Query whether a relay is currently commanded on.
 *
 * @param[in] channel  Relay channel identifier.
 *
 * @return true if the relay is energised (peak or hold phase), false
 *         otherwise or if @p channel is out of range.
 */
bool relay_is_on(relay_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_H_ */
