/**
 * @file  relay.h
 * @brief Peak-and-hold relay driver.
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Each relay is energised in two phases:
 *   1. PEAK - driven at 100 % duty for @ref RELAY_PEAK_TIME_MS to
 *      guarantee reliable armature pull-in.
 *   2. HOLD - reduced to @ref RELAY_HOLD_DUTY_PERMILLE to keep the
 *      contacts closed at minimum coil power/heat.
 *
 * Timing is driven by a Contiki process (1 ms clock resolution). The
 * public setter saves a command before waking the process. Call it only
 * from cooperative application context, never from an ISR.
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
 * Starts PWM OFF, then restores saved ON commands if retention is enabled.
 * Restored channels follow the normal peak-and-hold sequence. Call only
 * after NVRAM initialization and
 * after @c process_init() / @c autostart_start().
 */
void relay_init(void);

/**
 * @brief Energise or de-energise a relay.
 *
 * After accepting an ON command, the peak-and-hold sequence is restarted:
 * the coil
 * is driven at 100 % duty and automatically reduced to the hold duty
 * after @ref RELAY_PEAK_TIME_MS. An accepted OFF command forces 0 % when
 * the process consumes it. Enabled persistence is verified first.
 *
 * @param[in] channel  Relay channel identifier.
 * @param[in] on       true to energise, false to release.
 * @return true on acceptance; false preserves the previous command.
 * Call only from cooperative context; never from an ISR.
 */
bool relay_set(relay_channel_t channel, bool on);

/**
 * @brief Query whether a relay is currently commanded on.
 *
 * @param[in] channel  Relay channel identifier.
 *
 * @return true for pending ON, peak or hold; false for commanded OFF or
 *         an invalid channel. This is not contact feedback.
 */
bool relay_is_on(relay_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_H_ */

/*** end of file ***/
