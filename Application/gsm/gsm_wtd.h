/*
 * gsm_wtd.h
 *
 * Software watchdog for the GSM/AT engine.
 * Detects stuck busy states and performs graduated recovery.
 */

#ifndef GSM_GSM_WTD_H_
#define GSM_GSM_WTD_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check the GSM software watchdog timer.
 *
 * Call once per gsm_process cycle.  When the GSM engine stays busy longer
 * than GSM_WTD_TIMEOUT_MS the watchdog triggers a graduated recovery:
 *   1-2) Soft recovery — AT engine reset, release busy, idle listeners.
 *   3)   Hard reset    — full system reset.
 *
 * Diagnostic data (16 bytes) is logged to flash via gsm_log before each
 * recovery action.
 */
void gsm_wtd_check(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_GSM_WTD_H_ */
