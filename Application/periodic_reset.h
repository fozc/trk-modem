/*
 * periodic_reset.h
 *
 *  Created on: Apr 27, 2026
 *      Author: Fatih Özcan
 *              fatihozcan@gmail.com
 *
 * Periodic system reset scheduler.
 */

#ifndef PERIODIC_RESET_H_
#define PERIODIC_RESET_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Periodic-reset tick — call from the heart_beat_process loop.
 *
 * Re-reads the period from modem_config on every call; arms the timer on
 * the first non-zero period seen, and restarts it automatically when the
 * value changes.  No-op when period == 0.
 */
void periodic_reset_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* PERIODIC_RESET_H_ */
