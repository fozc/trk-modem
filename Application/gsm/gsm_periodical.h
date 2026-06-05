/*
 * gsm_periodical.h
 *
 * Periodical GSM checks: signal quality, SMS, COPS, socket info, GPRS.
 */

#ifndef GSM_PERIODICAL_H
#define GSM_PERIODICAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the periodical-event scheduler and state machine.
 *
 * Checks timer expiry for signal quality, SMS, COPS, socket info
 * and GPRS connection, then dispatches the appropriate AT query
 * send / wait-response pair.
 *
 * Called once per gsm_process cycle in normal mode.
 */
void gsm_periodical_event_process(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_PERIODICAL_H */
