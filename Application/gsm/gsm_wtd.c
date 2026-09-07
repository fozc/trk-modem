/*
 * gsm_wtd.c
 *
 * Software watchdog for the GSM/AT engine.
 * Detects stuck busy states and performs graduated recovery.
 */

#define CSLOG_MODULE LOG_MOD_GSM
#include "gsm_wtd.h"

#include "gsm_engine.h"
#include "gsm_process.h"
#include "gsm_listener_process.h"
#include "gsm_log.h"
#include "gsm_elog.h"
#include "at_engine2.h"
#include "bsp.h"

#include <stdint.h>

/* ---------------------------------------------------------------------------
 *  Configuration
 * ------------------------------------------------------------------------- */
#define GSM_WTD_TIMEOUT_MS       (5UL * 60UL * 1000UL)  /* 5 minutes */
#define GSM_WTD_RESET_THRESHOLD  2U                      /* soft recoveries before hard reset */

/* Stuck-FREE (liveness) esigi: motor bos gorunurken GSM alt sisteminin
 * "yasadigini" kanitlayan tek sey urettigi AT isidir (07.09 12:44 vakasi:
 * init kilitlenmesi - motor IDLE, hic AT yok, yazilim wtd kor). */
#define GSM_WTD_LIVENESS_TIMEOUT_MS     (10UL * 60UL * 1000UL) /* 10 dk AT sessizligi */
#define GSM_WTD_LIVENESS_MAX_RECOVERIES 2U                      /* restart hakki; sonrasi hard reset */

/* ---------------------------------------------------------------------------
 *  External state
 * ------------------------------------------------------------------------- */
extern gsm_t gsm;

/* ---------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------- */
static uint32_t s_wtd_last_activity;
static uint8_t  s_wtd_soft_recovery_count;
static uint32_t s_liveness_last_activity;
static uint8_t  s_liveness_recovery_count;

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
 * @brief Canlilik damgasi - AT motoru bir isi bitirdiginde cagirir.
 *
 * Timeout disi her tamamlanan AT isi GSM alt sisteminin yasadiginin
 * kanitidir; ping geldiginde kurtarma sayaci da tazelenir (canlilik
 * geri donmustur).
 */
void gsm_wtd_liveness_ping(void)
{
	s_liveness_last_activity = gsm_get_tick();
	s_liveness_recovery_count = 0;
}

/**
 * @brief Stuck-FREE tespiti: motor bos ama alt sistem AT uretmiyor.
 *
 * Son canlilik damgasindan beri GSM_WTD_LIVENESS_TIMEOUT_MS gectiyse
 * alt sistemi sessizce asili sayiyoruz: surec yeniden baslatma
 * (cold boot) istenir; GSM_WTD_LIVENESS_MAX_RECOVERIES kezden sonra
 * bilincl hard reset (elog kayitli).
 */
static void gsm_wtd_log_diagnostic(elog_code_t code);

static void liveness_check(void)
{
	uint32_t silent_ms = gsm_get_tick() - s_liveness_last_activity;

	if (silent_ms < GSM_WTD_LIVENESS_TIMEOUT_MS)
	{
		return;
	}

	/* Tetikleme basina bir kez karar ver: yeni pencere ac. */
	s_liveness_last_activity = gsm_get_tick();
	++s_liveness_recovery_count;

	CSLOG_ERR("[GSM WTD] Liveness: motor FREE ama %u dk'dir AT isi yok - kurtarma %u/%u",
	    (unsigned)(silent_ms / 60000U),
	    (unsigned)s_liveness_recovery_count,
	    (unsigned)GSM_WTD_LIVENESS_MAX_RECOVERIES);

	uint8_t info[16] = {0};
	uint16_t silent_sec = (uint16_t)(silent_ms / 1000U);
	info[0] = (uint8_t)(silent_sec >> 8U);
	info[1] = (uint8_t)(silent_sec & 0xFFU);
	info[2] = s_liveness_recovery_count;
	gsm_elog_modem_event_with_arg(ELOG_GSM_WTD_LIVENESS, info, sizeof(info));

	if (s_liveness_recovery_count <= GSM_WTD_LIVENESS_MAX_RECOVERIES)
	{
		gsm_request_module_restart();
	}
	else
	{
		gsm_wtd_log_diagnostic(ELOG_GSM_WTD_HARD_RESET);
		CSLOG_ERR("[GSM WTD] Liveness: %u kurtarma tukendi - hard reset",
		    (unsigned)GSM_WTD_LIVENESS_MAX_RECOVERIES);
		s_liveness_recovery_count = 0;
		gsm_wtd_feed();
		bsp_system_reset();
	}
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
	gsm_elog_modem_event_with_arg(code, info, sizeof(info));
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
	/* O4b.3 (uygulandi): stuck-FREE kilitleri liveness damgasiyla
	 * yakalaniyor - AT motoru timeout disi bir isi bitirdikce ping atar
	 * (bkz. at_engine2.c DONE durumu). Motor bos + damga eski = alt
	 * sistem sessizce asili: liveness_check() karar verir. Boot'taki
	 * gsm_power_on sinirsiz retry dongusu sirasinda poll dongusu
	 * kosmadigindan bu kontrol tetiklenmez. */

	/* Feed when GSM is not busy — normal operation */
	if (!gsm_is_busy())
	{
		gsm_wtd_feed();
		s_wtd_soft_recovery_count = 0;
		liveness_check();
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
