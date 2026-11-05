/*
 * periodic_reset.h
 *
 *  Created on: Apr 27, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Periodic system reset scheduler.
 */

#ifndef PERIODIC_RESET_H_
#define PERIODIC_RESET_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Periyot ust siniri (saniye). Web arayuzu saat girer (0-720 h); 0 =
 * periyodik reset kapali. Saniye cinsinden bu sinir, tick carpiminin
 * (period * CLOCK_SECOND) 32-bit tasmamasini garanti eder (Y3.13). */
#define PERIODIC_RESET_PERIOD_MAX_S  (720UL * 3600UL)   /* 30 gun */

/**
 * @brief Periodic-reset tick - call from the heart_beat_process loop.
 *
 * Re-reads the period from modem_config on every call; arms the timer on
 * the first non-zero period seen, and restarts it automatically when the
 * value changes.  No-op when period == 0.
 */
void periodic_reset_tick(void);

/**
 * @brief Defer periodic reset for 10 minutes from an accepted update start.
 *
 * Each call restarts the delay without changing the configured reset deadline.
 * Call only from cooperative main context. Other reset paths are unaffected.
 */
void periodic_reset_defer_for_update(void);

#ifdef __cplusplus
}
#endif

#endif /* PERIODIC_RESET_H_ */

/*** end of file ***/
