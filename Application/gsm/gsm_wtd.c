/*
 * gsm_wtd.c
 *
 * Software watchdog for the GSM/AT engine.
 * Detects stuck busy states and performs graduated recovery.
 */

#include "gsm_wtd.h"

#include "gsm_engine.h"
#include "gsm_process.h"
#include "gsm_listener_process.h"
#include "gsm_log.h"
#include "at_engine2.h"
#include "bsp.h"

#include <stdint.h>

/* ---------------------------------------------------------------------------
 *  Configuration
 * ------------------------------------------------------------------------- */
#define GSM_WTD_TIMEOUT_MS       (5UL * 60UL * 1000UL)  /* 5 minutes */
#define GSM_WTD_RESET_THRESHOLD  2U                      /* soft recoveries before hard reset */

/* ---------------------------------------------------------------------------
 *  External state
 * ------------------------------------------------------------------------- */
extern gsm_t gsm;

/* ---------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------- */
static uint32_t s_wtd_last_activity;
static uint8_t  s_wtd_soft_recovery_count;

/* ---------------------------------------------------------------------------
 *  Internal helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Feed the GSM watchdog — call whenever real progress occurs.
 *
 * "Progress" = gsm is not busy, or a state transition happened.
 */
static void gsm_wtd_feed(void)
{
	s_wtd_last_activity = gsm_get_tick();
}

/**
 * @brief Packed diagnostic snapshot for elog info[16] field.
 *
 * Layout (16 bytes):
 *   [0]  at_engine state (first char of state string)
 *   [1]  gsm.is_module_busy
 *   [2]  gsm.query_state
 *   [3]  gsm.query_id             (last AT command sent)
 *   [4]  gsm.main_state
 *   [5]  gsm.init_state
 *   [6]  gsm.delay_flag
 *   [7]  gsm.periodical_event_state
 *   [8]  gsm.periodical_phase
 *   [9]  listener[WEB].state
 *   [10] listener[WEB].phase
 *   [11] listener[IEC104].state
 *   [12] listener[IEC104].phase
 *   [13] soft_recovery_count
 *   [14..15] busy duration in seconds (uint16, big-endian)
 */
static void gsm_wtd_log_diagnostic(elog_code_t code)
{
	uint32_t busy_ms = gsm_get_tick() - s_wtd_last_activity;
	uint16_t busy_sec = (uint16_t)(busy_ms / 1000U);

	uint8_t info[16] = {0};
	const char *at_str = at_engine_get_state_str();
	info[0]  = (uint8_t)at_str[0];
	info[1]  = gsm.is_module_busy;
	info[2]  = gsm.query_state;
	info[3]  = gsm.query_id;
	info[4]  = gsm.main_state;
	info[5]  = gsm.init_state;
	info[6]  = gsm.delay_flag;
	info[7]  = gsm.periodical_event_state;
	info[8]  = gsm.periodical_phase;
	info[9]  = gsm.listener[GSM_LISTENER_WEB].state;
	info[10] = gsm.listener[GSM_LISTENER_WEB].phase;
	info[11] = gsm.listener[GSM_LISTENER_IEC104].state;
	info[12] = gsm.listener[GSM_LISTENER_IEC104].phase;
	info[13] = s_wtd_soft_recovery_count;
	info[14] = (uint8_t)(busy_sec >> 8U);
	info[15] = (uint8_t)(busy_sec & 0xFFU);

	/* Console log for real-time debug */
	CSLOG_ERR("[GSM WTD] DIAG at=%s busy=%u qs=%u qid=%u "
	             "main=%u init=%u dly=%u per=%u/%u "
	             "ls[W]=%u/%u ls[I]=%u/%u dur=%us",
	    at_str,
	    (unsigned)gsm.is_module_busy,
	    (unsigned)gsm.query_state,
	    (unsigned)gsm.query_id,
	    (unsigned)gsm.main_state,
	    (unsigned)gsm.init_state,
	    (unsigned)gsm.delay_flag,
	    (unsigned)gsm.periodical_event_state,
	    (unsigned)gsm.periodical_phase,
	    (unsigned)gsm.listener[GSM_LISTENER_WEB].state,
	    (unsigned)gsm.listener[GSM_LISTENER_WEB].phase,
	    (unsigned)gsm.listener[GSM_LISTENER_IEC104].state,
	    (unsigned)gsm.listener[GSM_LISTENER_IEC104].phase,
	    (unsigned)busy_sec);

	/* Persistent log */
	gsm_log_modem_event_with_arg(code, info, sizeof(info));
}

/**
 * @brief Soft recovery — release busy lock, reset AT engine, idle listeners.
 */
static void gsm_wtd_soft_recover(void)
{
	gsm_wtd_log_diagnostic(ELOG_GSM_WTD_SOFT_RECOVERY);

	LOG(WARNING, "[GSM WTD] Soft recovery (%u/%u)",
	    (unsigned)(s_wtd_soft_recovery_count + 1U),
	    (unsigned)GSM_WTD_RESET_THRESHOLD);

	at_engine_clear_buff();
	at_engine_reset();
	gsm_set_free();

	gsm.delay_flag = 0;
	gsm.query_state = 0;

	/* Listener state machines -> IDLE */
	for (uint8_t i = 0; i < (uint8_t)GSM_LISTENER_COUNT; ++i)
	{
		gsm.listener[i].state = GSM_LS_IDLE;
		gsm.listener[i].phase = GSM_INIT_PHASE_SEND;
		gsm.listener[i].socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
	}

	/* Periodical -> idle */
	gsm.periodical_event_state = 0;
	gsm.periodical_phase       = GSM_INIT_PHASE_SEND;

	++s_wtd_soft_recovery_count;
	gsm_wtd_feed();
}

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */
void gsm_wtd_check(void)
{
	/* Feed when GSM is not busy — normal operation */
	if (!gsm_is_busy())
	{
		gsm_wtd_feed();
		s_wtd_soft_recovery_count = 0;
		return;
	}

	/* GSM is busy — check if stuck */
	if ((gsm_get_tick() - s_wtd_last_activity) < GSM_WTD_TIMEOUT_MS)
	{
		return; /* Still within timeout */
	}

	/* Timeout expired while busy */
	if (s_wtd_soft_recovery_count < GSM_WTD_RESET_THRESHOLD)
	{
		gsm_wtd_soft_recover();
	}
	else
	{
		gsm_wtd_log_diagnostic(ELOG_GSM_WTD_HARD_RESET);

		CSLOG_ERR("[GSM WTD] Hard reset — %u soft recoveries exhausted",
		    (unsigned)GSM_WTD_RESET_THRESHOLD);
		s_wtd_soft_recovery_count = 0;
		gsm_wtd_feed();
		bsp_system_reset();
	}
}
