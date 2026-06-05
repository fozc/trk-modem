/*
 * rf_dummy.h
 *
 * Dummy RF data generator for development and testing.
 * Produces plausible, incrementally-changing rf_monitor_t values
 * for all configured feeders without requiring real RF hardware.
 *
 * Usage:
 *   1. Call rf_dummy_init() once after nvram_init().
 *   2. Call rf_dummy_tick() periodically (e.g. from the 10 Hz heartbeat).
 *      The function has an internal 2-second gate; calls at higher frequency
 *      are silently ignored until the gate expires.
 *
 * The module is a no-op when RF_DUMMY_ENABLE is not defined.
 */

#ifndef RF_RF_DUMMY_H_
#define RF_RF_DUMMY_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the dummy RF generator.
 *
 * Seeds per-feeder LFSR noise sources and snapshots the current
 * rf_config so that device IDs, hat IDs, and zone IDs stay consistent
 * with the stored configuration.
 *
 * Must be called after nvram_init().
 */
void rf_dummy_init(void);

/**
 * @brief Advance the dummy RF simulation by one step.
 *
 * Internal gate: updates the rf_monitor store at most once every
 * RF_DUMMY_TICK_PERIOD_MS milliseconds regardless of call frequency.
 * Safe to call from the main-loop heartbeat or any non-ISR context.
 */
void rf_dummy_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_DUMMY_H_ */
