/*
 * gsm_periodical.c
 *
 * Periodical GSM checks: signal quality, SMS, COPS, socket info, GPRS.
 * Each check is implemented as a static step function with
 * send / wait-response phases (mirroring gsm_init pattern).
 */

#include "gsm_periodical.h"
#include "gsm_types.h"
#include "gsm_engine.h"
#include "at_engine2.h"
#include "gsm_process.h"
#include "gsm_listener_process.h"
#include "gsm_signal_led.h"
#include "bsp.h"

#include <stdint.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 *  Timer periods (seconds)
 * --------------------------------------------------------------------- */
#define GSM_CSQ_REQ_TIME_S   60U
#define GSM_COPS_REQ_TIME_S  60U
#define GSM_CMGR_REQ_TIME_S  180U
#define GSM_SI_REQ_TIME_S    30U   /* AT#SI periyodik sorgu suresi */

/* -----------------------------------------------------------------------
 *  Module-scope state
 * --------------------------------------------------------------------- */
extern gsm_t gsm;

static uint8_t s_low_signal_flag         = 0;
static uint8_t s_unknown_signal_lvl_flag = 0;

/* -----------------------------------------------------------------------
 *  Helpers
 * --------------------------------------------------------------------- */

/**
 * @brief Send an AT query and switch to wait-response phase.
 */
static void gsm_periodical_send(uint8_t at_query)
{
	if (gsm_engine_send_query(at_query))
	{
		gsm.periodical_phase = GSM_INIT_PHASE_WAIT_RESPONSE;
	}
}

/**
 * @brief Complete a periodical step: reset state, apply delay, release engine.
 *
 * @param[in] next_state  Next periodical_event_state (0 = idle).
 * @param[in] delay_ms    Post-response delay in ms.
 */
static void gsm_periodical_finish(uint8_t next_state, uint16_t delay_ms)
{
	gsm.periodical_event_state = next_state;
	gsm.periodical_phase       = GSM_INIT_PHASE_SEND;
	if (delay_ms > 0U)
	{
		gsm_set_delay(delay_ms);
	}
	gsm_set_free();
}

/* -----------------------------------------------------------------------
 *  Scheduler — decides which periodical check to run next
 * --------------------------------------------------------------------- */
static void gsm_periodical_schedule(void)
{
	if (gsm_get_main_state() != GSM_NORMAL_MODE)
	{
		return;
	}
	if (gsm.periodical_event_state != 0U)
	{
		return;
	}

	/* Highest priority: listener-requested GPRS check */
	if (gsm.net_check_requested != 0U)
	{
		gsm.periodical_event_state = GSM_CHECK_GPRS_CONNECTION;
		return;
	}

	if (gsm_get_tick() >= gsm.signal_quality_timer)
	{
		gsm.periodical_event_state = GSM_GET_EXT_SIGNAL_QUALITY;
		return;
	}
	if (gsm_get_tick() >= gsm.sms_check_timer)
	{
		gsm.periodical_event_state = GSM_CHECK_SMS;
		return;
	}

	if (gsm_get_tick() >= gsm.network_type_timer)
	{
		gsm.periodical_event_state = GSM_GET_COPS_STATE;
		return;
	}

	if (gsm_get_tick() >= gsm.socket_info_timer)
	{
		gsm.periodical_event_state = GSM_CHECK_ALL_SOCKET_INFO;
		return;
	}

	if (bsp_get_tick() >= gsm.gprs_check_timer)
	{
		gsm.periodical_event_state = GSM_CHECK_GPRS_CONNECTION;
		return;
	}
}

/* -----------------------------------------------------------------------
 *  Step: signal quality (CSQ)
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_signal_quality(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_GET_SIGNAL_QUALITY);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	if (res == GSM_RESPONSE_OK)
	{
		uint8_t signal_quality = gsm_get_signal_quality();

		if ((!s_low_signal_flag) && (signal_quality < 8U))
		{
			s_low_signal_flag = 1;
		}
		if ((!s_unknown_signal_lvl_flag) && (signal_quality == 99U))
		{
			s_unknown_signal_lvl_flag = 1;
		}

		if (signal_quality == 0U)
		{
			LOG(_GSM_, "Sinyal yok, reset ? !");
		}
	}

	gsm.signal_quality_timer = gsm_get_tick() + (GSM_CSQ_REQ_TIME_S * 1000U);
	gsm_periodical_finish(0, 50);
}

/* -----------------------------------------------------------------------
 *  Step: extended signal quality (CESQ)
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_ext_signal_quality(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_GET_EXT_SIGNAL_QUALITY);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	gsm_signal_led_update();
	gsm.signal_quality_timer = gsm_get_tick() + (GSM_CSQ_REQ_TIME_S * 1000U);
	gsm_periodical_finish(0, 50);
}

/* -----------------------------------------------------------------------
 *  Step: SMS check
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_check_sms(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_CMGR);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	uint8_t next = 0;
	if (res != GSM_SMS_NO_SMS)
	{
		next = GSM_DELETE_SMS;
	}
	gsm.sms_check_timer = gsm_get_tick() + (GSM_CMGR_REQ_TIME_S * 1000U);
	gsm_periodical_finish(next, 50);
}

/* -----------------------------------------------------------------------
 *  Step: SMS delete
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_delete_sms(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_CMGD);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	if (res != GSM_RESPONSE_OK)
	{
		LOG_TRACE(_GSM_, "SMS silinemedi !!! ");
	}
	gsm_periodical_finish(0, 50);
}

/* -----------------------------------------------------------------------
 *  Step: COPS (network operator type)
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_cops(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_COPS_STATE);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	gsm.network_type_timer = gsm_get_tick() + (GSM_COPS_REQ_TIME_S * 1000U);
	gsm_periodical_finish(0, 50);
}

/* -----------------------------------------------------------------------
 *  Step: all socket info (AT#SI)
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_socket_info(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_GET_ALL_SOCKET_INFO);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	gsm.socket_info_timer = gsm_get_tick() + (GSM_SI_REQ_TIME_S * 1000UL);

	/* Bireysel soket SI sorgularinin tekrarlanmasini onle */
	gsm.listener[GSM_LISTENER_WEB].data_check_timer    = gsm_get_tick() + (GSM_SI_REQ_TIME_S * 1000UL);
	gsm.listener[GSM_LISTENER_IEC104].data_check_timer = gsm_get_tick() + (GSM_SI_REQ_TIME_S * 1000UL);

	gsm_periodical_finish(0, 50);
}

/* -----------------------------------------------------------------------
 *  Step: GPRS connection check
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_gprs_check(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_GET_GPRS_STATUS);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	gsm.gprs_check_timer = bsp_get_tick() + GSM_LS_GPRS_CHECK_TIMER_MS;

	if (res == GSM_RESPONSE_OK)
	{
		if (gsm_get_gprs_state())
		{
			/* GPRS up */
			gsm.net_check_requested = 0;
			gsm_periodical_finish(0, 50);
		}
		else
		{
			/* GPRS down — try reconnect */
			LOG(WARNING, "GPRS baglantisi kopmus");
			gsm_internet_connection_faild_cd();
			gsm.net_reconnect_retry = 0;
			gsm_periodical_finish(GSM_RECONNECT_GPRS_CONNECTION, 50);
		}
	}
	else
	{
		LOG_TRACE(_GSM_, "GPRS check failed, res=%u", (unsigned)res);
		if (gsm.net_check_requested != 0U)
		{
			/* Listener is waiting — aggressive recovery */
			gsm_reset_process_old();
			return;
		}
		gsm_periodical_finish(0, 50);
	}
}

/* -----------------------------------------------------------------------
 *  Step: GPRS reconnect
 * --------------------------------------------------------------------- */
static void gsm_periodical_step_gprs_reconnect(void)
{
	if (gsm.periodical_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_periodical_send(ATQUERY_ACTIVATE_GPRS);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	if (res == GSM_RESPONSE_OK)
	{
		/* GPRS reconnected */
		gsm.net_check_requested = 0;
		gsm_periodical_finish(0, 50);
	}
	else
	{
		if (gsm.net_reconnect_retry++ < 2U)
		{
			gsm_periodical_finish(GSM_RECONNECT_GPRS_CONNECTION, 1000);
		}
		else
		{
			LOG(WARNING, "GPRS reconnect failed — resetting");
			gsm_reset_process_old();
			return;
		}
	}
}

/* -----------------------------------------------------------------------
 *  Main dispatch
 * --------------------------------------------------------------------- */
void gsm_periodical_event_process(void)
{
	gsm_periodical_schedule();

	switch (gsm.periodical_event_state)
	{
	case GSM_GET_SIGNAL_QUALITY:
		gsm_periodical_step_signal_quality();
		break;

	case GSM_GET_EXT_SIGNAL_QUALITY:
		gsm_periodical_step_ext_signal_quality();
		break;

	case GSM_CHECK_SMS:
		gsm_periodical_step_check_sms();
		break;
	case GSM_DELETE_SMS:
		gsm_periodical_step_delete_sms();
		break;

	case GSM_GET_COPS_STATE:
		gsm_periodical_step_cops();
		break;

	case GSM_CHECK_ALL_SOCKET_INFO:
		gsm_periodical_step_socket_info();
		break;

	case GSM_CHECK_GPRS_CONNECTION:
		gsm_periodical_step_gprs_check();
		break;

	case GSM_RECONNECT_GPRS_CONNECTION:
		gsm_periodical_step_gprs_reconnect();
		break;

	default:
		break;
	}
}
