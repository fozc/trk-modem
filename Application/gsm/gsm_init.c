/*
 * gsm_init.c
 *
 * GSM modem initialization state machine.
 * Handles the full init sequence: AT command setup, SIM check, network
 * registration, GPRS activation, and listener socket opening.
 */

#include "gsm_init.h"
#include "gsm_types.h"
#include "gsm_engine.h"
#include "gsm_info.h"
#include "at_engine2.h"
#include "gsm_process.h"
#include "gsm_listener_process.h"
#include "modem_config.h"
#include "led_driver.h"
#include <string.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 *  Timeout definitions (milliseconds)
 * --------------------------------------------------------------------- */

#define GSM_NETWORK_REGISTRATION_TIMEOUT_MS   (160UL * 1000UL)
#define GSM_NETWORK_SEARCH_TIMEOUT_MS         (180UL * 1000UL)
#define GSM_SIGNAL_QUALITY_PERIOD_MS          (30UL * 1000UL)
#define GSM_NETWORK_CHECK_PERIOD_MS           (60UL * 1000UL)

/* -----------------------------------------------------------------------
 *  Module-scope state
 * --------------------------------------------------------------------- */

extern gsm_t gsm;

static uint32_t s_connection_time = 0;
static uint8_t  s_pin_reset_state = 0;

/* -----------------------------------------------------------------------
 *  Init vectors
 * --------------------------------------------------------------------- */

static const uint8_t gsm_common_init_vector[] =
{
	GSM_CHECHK_MODULE,
	GSM_ENABLE_TELIT_LOG_1,
	GSM_ENABLE_TELIT_LOG_2,
	GSM_DISABLE_FLOW_CNTRL,
	GSM_SET_BAUDRATE,
	GSM_DISABLE_ECHO,
	GSM_GET_IMEI,
	GSM_ENABLE_ERROR_CODES,
	GSM_GET_MODULE_NAME,
	GSM_GET_MODULE_FW_VER,
	GSM_INIT_DONE
};

static const uint8_t gsm_LE910R1_init_vector[] =
{
	GSM_CHECK_SIMCARD_AND_PIN,
	//GSM_CHECK_GSM_VOICE_SMS_NEWTWORK_STATE,
	GSM_SET_APN,
	GSM_GET_SIGNAL_QUALITY,
	GSM_CHECK_GPRS_NEWTWORK_STATE,
	GSM_CHECK_LTE_NEWTWORK_STATE,
	GSM_GET_SIMCARD_IMSI,
	GSM_GET_PHONE_NUMBER,

	GSM_CONFIG_SOCKET_WEB_SCFG,
	GSM_CONFIG_SOCKET_WEB_SCFGEXT,
	GSM_CONFIG_SOCKET_WEB_SCFGEXT2,

	GSM_CONFIG_SOCKET_IEC104_SCFG,
	GSM_CONFIG_SOCKET_IEC104_SCFGEXT,
	GSM_CONFIG_SOCKET_IEC104_SCFGEXT2,

	GSM_ACTIVATE_GPRS,
	GSM_CONNECT_TO_GPRS,
	GSM_CONFIG_PING_SUPPORT,
	GSM_GET_SIGNAL_QUALITY,
	GSM_READ_DATETIME,
	GSM_GET_NTP_DATE_TIME,
	GSM_OPEN_WEB_LISTENER_SOCKET,
	GSM_OPEN_IEC104_LISTENER_SOCKET,
	GSM_CONFIG_SMS_FORMAT,
	GSM_INIT_DONE
};

static const uint8_t s_common_init_vector_size  = (uint8_t)sizeof(gsm_common_init_vector);
static const uint8_t s_LE910R1_init_vector_size = (uint8_t)sizeof(gsm_LE910R1_init_vector);

/* -----------------------------------------------------------------------
 *  Init state / vector management
 * --------------------------------------------------------------------- */

void gsm_set_init_state(gsm_init_state_t state)
{
	gsm.prev_state = (uint8_t)gsm.init_state;
	gsm.init_state = state;
	gsm.init_phase = GSM_INIT_PHASE_SEND;
}

gsm_init_state_t gsm_get_init_state(void)
{
	return gsm.init_state;
}

void gsm_reset_init_vector(void)
{
	if (gsm.init_vec_len > 0U)
	{
		gsm_set_init_state(gsm.init_vector[0]);
		gsm.init_step = 1;
	}
}

static void gsm_set_init_vector(const uint8_t *p_vec, uint8_t len)
{
	if (len > 0U)
	{
		gsm.init_vec_len = len;
		gsm.init_vector  = p_vec;
		gsm_reset_init_vector();
	}
}

void gsm_load_common_init_vector(void)
{
	gsm_set_init_vector(gsm_common_init_vector, s_common_init_vector_size);
}

void gsm_load_LE910R1_init_vector(void)
{
	gsm_set_init_vector(gsm_LE910R1_init_vector, s_LE910R1_init_vector_size);
}

void gsm_init_next_step(void)
{
	if (gsm.init_vec_len > 0U)
	{
		uint8_t step = gsm.init_vector[gsm.init_step++];

		if ((step != GSM_INIT_DONE) && (gsm.init_step < gsm.init_vec_len))
		{
			gsm_set_init_state(step);
			if ((step == GSM_CHECK_GPRS_NEWTWORK_STATE) || (step == GSM_CHECK_LTE_NEWTWORK_STATE))
			{
				gsm.gsm_network_timer = gsm_get_tick();
			}
		}
		else
		{
			gsm_set_init_state(GSM_INIT_DONE);
		}
	}
}

void gsm_init_set_step_old(uint32_t step)
{
	for (uint8_t i = 0; i < gsm.init_vec_len; i++)
	{
		if (gsm.init_vector[i] == (uint8_t)step)
		{
			gsm.init_step = i;
			break;
		}
	}
}

/* -----------------------------------------------------------------------
 *  Init helpers
 * --------------------------------------------------------------------- */

/**
 * @brief Send an AT query and switch to wait-response phase.
 *
 * @param[in] at_query  AT query ID to send.
 */
static void gsm_init_send(uint8_t at_query)
{
	if (gsm_engine_send_query(at_query))
	{
		gsm.init_phase = GSM_INIT_PHASE_WAIT_RESPONSE;
	}
}

/**
 * @brief Handle a generic init response (wait-response phase).
 *
 * OK  -> gsm_init_next_step()
 * ERR -> gsm_init_next_step() if skip_on_error, else GSM_RESET_MODULE
 *
 * @param[in] skip_on_error  true = continue on error, false = reset on error.
 * @param[in] delay_ms       Post-response delay in ms (0 = no delay).
 */
static void gsm_init_generic_response(bool skip_on_error, uint16_t delay_ms)
{
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			gsm_init_next_step();
		}
		else
		{
			if (skip_on_error)
			{
				gsm_init_next_step();
			}
			else
			{
				gsm_set_init_state(GSM_RESET_MODULE);
			}
		}
		if (delay_ms > 0U)
		{
			gsm_set_delay(delay_ms);
		}
		gsm_set_free();
	}
}

/**
 * @brief Simple init step: send AT query, then generic response.
 *
 * @param[in] at_query       AT query ID to send.
 * @param[in] skip_on_error  true = continue on error, false = reset on error.
 * @param[in] delay_ms       Post-response delay in ms.
 */
static void gsm_init_simple_step(uint8_t at_query, bool skip_on_error, uint16_t delay_ms)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(at_query);
		return;
	}
	gsm_init_generic_response(skip_on_error, delay_ms);
}

/* -----------------------------------------------------------------------
 *  Init step functions — complex steps (send + custom response)
 * --------------------------------------------------------------------- */

static void gsm_init_step_soft_init(void)
{
	GSM_LOG_INF("GSM soft init...\r\n");
	led_driver_set_gsm_mode(LED_GSM_OFF);
	gsm_info_init();
	gsm_set_access_technology(GSM_NO_SIGNAL);
	gsm.reboot_counter = 0;
	gsm.no_sim = 0;
	gsm.temp_counter = 0;
	gsm.init_cgreg_state = 0;
	gsm.init_cereg_state = 0;
	gsm.tx_flag = GSM_TX_NOT_AVAILABLE;
	gsm.module_state  = 1;
	gsm_listener_set_no_carrier(GSM_LISTENER_WEB, 0);
	gsm.dialer_socket_no_carrier = 0;
	gsm.listener[GSM_LISTENER_WEB].state = 0;
	gsm.dialer_socket_state = 0;
	gsm_set_signal_quality(99);
	gsm_reset_init_vector();
	if (gsm_get_main_state() == GSM_COMMON_INIT_MODE)
	{
		gsm_init_next_step();
	}
	at_engine_clear_buff();
}

static void gsm_init_step_reset_module(void)
{
	led_driver_set_modem_mode(LED_MODEM_ERROR);
	if (s_pin_reset_state)
	{
		LOG(DEBUG, "! s_pin_reset_state: 1, hard reset !");
		while(1);
	}
	if (++gsm.reboot_counter > 2)
	{
		gsm_set_init_state(GSM_PIN_RESET);
		s_pin_reset_state = 1;
	}
	else
	{
		gsm_set_init_state(GSM_REBOOT);
	}
	gsm.prev_creg = 0;
}

static void gsm_init_step_pin_reset(void)
{
	uint32_t res = gsm_pin_reset_module();
	if (res == GSM_OK)
	{
		gsm.no_sim = 0;
		gsm.temp_counter = 0;
		gsm.tx_flag = GSM_TX_NOT_AVAILABLE;
		gsm.module_state = 1;
		gsm_set_signal_quality(99);
		gsm_set_delay(3000);
		gsm_load_common_init_vector();
		gsm_set_main_state(GSM_COMMON_INIT_MODE);
		gsm_set_init_state(GSM_SOFT_INIT);
	}
}

static void gsm_init_step_check_module(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_AT);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			led_driver_set_modem_mode(LED_MODEM_INIT);
			gsm.temp_counter = 0;
			gsm_init_next_step();
		}
		else if (gsm.temp_counter++ > 3)
		{
			gsm_set_init_state(GSM_RESET_MODULE);
		}
		else
		{
			gsm_set_init_state(GSM_CHECHK_MODULE);
		}
		gsm_set_delay(1000);
		gsm_set_free();
	}
}

static void gsm_init_step_get_module_name(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_CGMM);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			gsm_init_next_step();
		}
		else
		{
			gsm_set_delay(50);
			gsm_set_init_state(GSM_RESET_MODULE);
		}
		gsm_set_free();
	}
}

static void gsm_init_step_check_simcard(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_GET_SIMCARD_STATUS);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res) {
		case GSM_CPIN_READY:
			led_driver_set_modem_mode(LED_MODEM_SEARCHING);
			gsm_set_delay(50);
			s_connection_time = gsm_get_tick();
			gsm.gsm_network_timer = gsm_get_tick();
			gsm_init_next_step();
			break;
		case GSM_CPIN_BUSY:
			gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
			gsm_set_delay(300);
			break;
		case GSM_CPIN_SIM_FAILUER:
			led_driver_set_modem_mode(LED_MODEM_NO_SIM);
			LOG(_GSM_, "SIM FAILUER !!!");
			gsm_set_init_state(GSM_RESET_MODULE);
			break;
		case GSM_CPIN_NO_SIM:
			led_driver_set_modem_mode(LED_MODEM_NO_SIM);
			if (!gsm.no_sim)
			{
				gsm.no_sim = 1;
				LOG(_GSM_, "No sim hata kayidi.");
			}
			LOG(_GSM_, "NO SIM... !");
			gsm_set_delay(3000);
			gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
			break;
		case GSM_CPIN_SIMPIN:
			gsm_set_delay(50);
			gsm_set_init_state(GSM_SET_PIN_NO);
			break;
		case GSM_CPIN_SIMPUK:
			led_driver_set_modem_mode(LED_MODEM_NO_SIM);
			gsm_set_delay(3000);
			gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
			break;
		case GSM_ERROR:
		case GSM_CPIN_ERROR:
		case GSM_TIMEOUT:
			led_driver_set_modem_mode(LED_MODEM_NO_SIM);
			if (gsm.no_sim != 0U)
			{
				/* SIM absence already confirmed — keep polling. */
				gsm_set_delay(3000);
				gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
			}
			else
			{
				gsm_set_delay(50);
				gsm_set_init_state(GSM_RESET_MODULE);
			}
			break;
		default:
			break;
	}
	if (res != 0U)
	{
		gsm_set_free();
	}
}

static void gsm_init_step_set_pin_no(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_SET_PIN_NO);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		switch (res)
		{
		case GSM_RESPONSE_OK:
			gsm_init_next_step();
			break;
		case GSM_CPIN_PINNO_ERROR:
			LOG(WARNING, "SIM PIN Hatali !");
			/* fallthrough */
		case GSM_CPIN_SIM_FAILUER:
		case GSM_ERROR:
			gsm_set_main_state(GSM_SIM_ERROR_MODE);
			break;
		case GSM_TIMEOUT:
			gsm_set_init_state(GSM_RESET_MODULE);
			gsm_set_delay(50);
			break;
		default:
			break;
		}
		gsm_set_free();
	}
}

static void gsm_init_step_check_voice_sms_network(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_GET_VOICE_SMS_NETWORK_STATE);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U)
	{
		return;
	}

	if (gsm.prev_creg != res && res == GSM_NETWORK_DENIED)
	{
		gsm.prev_creg = res;
		gsm.gsm_network_timer = gsm_get_tick();
	}
	if (gsm.prev_creg == 0U)
	{
		gsm.prev_creg = res;
	}

	switch (res)
	{
		case GSM_NETWORK_NOT_SEARCHING:
			gsm_set_delay(3000);
			if (!gsm.creg_not_searching_counter++)
			{
				/* log placeholder */
			}
			gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
			gsm_init_set_step_old(GSM_CHECK_SIMCARD_AND_PIN);
			break;
		case GSM_NETWORK_REGISTERED:
		case GSM_NETWORK_ROAMING:
			gsm_set_delay(50);
			gsm_init_next_step();
			gsm.gsm_network_timer = gsm_get_tick();
			s_connection_time = gsm_get_tick() - s_connection_time;
			LOG(_GSM_, "Sebekeye %d saniyede baglandi.", s_connection_time / 1000);
			break;
		case GSM_NETWORK_ONLY_EMERGENCY: /* fallthrough */
		case GSM_NETWORK_SEARCHING:
			gsm_set_delay(1000);
			gsm_set_init_state(GSM_CHECK_GSM_VOICE_SMS_NEWTWORK_STATE);
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_REGISTRATION_TIMEOUT_MS) {
				LOG(_GSM_, "5dk boyunca sebekeye baglnamadi!");
				gsm_set_init_state(GSM_RESET_MODULE);
			}
			break;
		case GSM_NETWORK_UNKNOW:
		case GSM_NETWORK_DENIED:
			gsm_set_delay(1000);
			gsm_set_init_state(GSM_CHECK_GSM_VOICE_SMS_NEWTWORK_STATE);
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_REGISTRATION_TIMEOUT_MS) {
				gsm_set_init_state(GSM_RESET_MODULE);
				if (!gsm.f_creg)
				{
					gsm.f_creg = 1;
				}
			}
			break;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			gsm_set_init_state(GSM_RESET_MODULE);
			break;
		default:
			break;
	}
	gsm_set_free();
}

static void gsm_init_step_set_apn(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_SET_APN);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		switch (res)
		{
			case GSM_RESPONSE_OK:
				gsm_init_next_step();
				break;
			case GSM_CONTEXT_ALREADY_ACTIVATED:
			case GSM_ERROR:
			case GSM_TIMEOUT:
				gsm_set_init_state(GSM_RESET_MODULE);
				break;
			default:
				break;
		}
		gsm_set_delay(50);
		gsm_set_free();
	}
}

static void gsm_init_step_activate_gprs(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_ATTACH_GPRS);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		switch (res)
		{
			case GSM_RESPONSE_OK:
				gsm_init_next_step();
				break;
			case GSM_NO_NETWORK_SERVICE:
			case GSM_ERROR:
			case GSM_TIMEOUT:
				gsm_set_init_state(GSM_GET_CEER);
				break;
			default:
				break;
		}
		gsm_set_delay(50);
		gsm_set_free();
	}
}

static void gsm_init_step_check_gprs_network(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_GET_GPRS_NETWORK_STATE);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res) {
		case GSM_GPRS_NETWORK_REGISTERED:
		case GSM_GPRS_NETWORK_ROAMING:
			gsm_init_next_step();
			gsm.init_cgreg_state = 1;
			gsm_set_delay(50);
			break;
		case GSM_GPRS_NETWORK_SEARCHING:
			gsm.init_cgreg_state = 0;
			if (!gsm.init_cereg_state) {
				gsm_set_init_state(GSM_CHECK_LTE_NEWTWORK_STATE);
			} else {
				gsm_init_next_step();
				gsm_init_next_step();
			}
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_SEARCH_TIMEOUT_MS) {
				gsm_set_init_state(GSM_GET_CEER);
			}
			break;
		case GSM_GPRS_NETWORK_DENIED:
		case GSM_GPRS_NETWORK_UNKNOW:
			gsm_set_delay(500);
			gsm.init_cgreg_state = 0;
			gsm_set_init_state(GSM_CHECK_LTE_NEWTWORK_STATE);
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_REGISTRATION_TIMEOUT_MS) {
				gsm_set_init_state(GSM_GET_CEER);
			}
			break;
		case GSM_TIMEOUT:
			gsm_set_init_state(GSM_RESET_MODULE);
			break;
		default:
			break;
	}
	if (res != 0U) {
		gsm_set_free();
	}
}

static void gsm_init_step_check_lte_network(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_GET_LTE_NETWORK_STATE);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res) {
		case GSM_LTE_NETWORK_REGISTERED:
		case GSM_LTE_NETWORK_ROAMING:
			gsm_init_next_step();
			gsm.init_cereg_state = 1;
			gsm_set_delay(50);
			break;
		case GSM_LTE_NETWORK_SEARCHING:
			gsm.init_cereg_state = 0;
			if (!gsm.init_cgreg_state) {
				gsm_set_init_state(GSM_CHECK_GPRS_NEWTWORK_STATE);
			} else {
				gsm_init_next_step();
			}
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_SEARCH_TIMEOUT_MS) {
				gsm_set_init_state(GSM_GET_CEER);
			}
			break;
		case GSM_LTE_NETWORK_DENIED:
		case GSM_LTE_NETWORK_UNKNOW:
			gsm_set_delay(500);
			gsm.init_cereg_state = 0;
			if (!gsm.init_cgreg_state) {
				gsm_set_init_state(GSM_CHECK_GPRS_NEWTWORK_STATE);
			} else {
				gsm_init_next_step();
			}
			if (gsm_get_tick() - gsm.gsm_network_timer > GSM_NETWORK_REGISTRATION_TIMEOUT_MS) {
				gsm_set_init_state(GSM_GET_CEER);
			}
			break;
		case GSM_TIMEOUT:
			if (!gsm.init_cgreg_state) {
				gsm_set_init_state(GSM_RESET_MODULE);
			} else {
				gsm_init_next_step();
			}
			break;
		default:
			break;
	}
	if (res != 0U) {
		gsm_set_free();
	}
}

static void gsm_init_step_connect_gprs(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_ACTIVATE_GPRS);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res) {
		case GSM_RESPONSE_OK:
			gsm_internet_connection_on_cb();
			led_driver_set_modem_mode(LED_MODEM_READY);
			gsm_set_delay(50);
			gsm_init_next_step();
			gsm.temp_counter = 0;
			break;
		case GSM_ERROR:
			if (strstr((char *) at_engine_get_response(NULL), "ERROR: 553") != NULL) {
				LOG_TRACE(WARNING,"_GSM_ GPRS daha once aktif edilmis !. Ama resetten geliyoruz :)");
			}
			if (strstr((char *) at_engine_get_response(NULL), "ERROR: 555") != NULL) {
				LOG_TRACE(WARNING,"_GSM_ Activation faid !");
			}
			if (gsm.temp_counter++ > 2)
			{
				gsm_set_init_state(GSM_RESET_MODULE);
			}
			else
			{
				gsm_set_delay(2000);
				gsm_set_init_state(GSM_CONNECT_TO_GPRS);
			}
			break;
		case GSM_TIMEOUT:
			gsm_set_init_state(GSM_RESET_MODULE);
			break;
		default:
			break;
	}
	if (res != 0U) {
		gsm_set_free();
	}
}

static void gsm_init_step_get_gprs_state(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_GET_GPRS_STATUS);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		switch (res) {
			case GSM_RESPONSE_OK:
				if (gsm_get_gprs_state())
				{
					gsm_set_init_state(GSM_OPEN_WEB_LISTENER_SOCKET);
					gsm_init_set_step_old(GSM_OPEN_WEB_LISTENER_SOCKET);
				}
				else
				{
					LOG(WARNING, "GPRS baglantisi kopmus !");
					gsm_set_init_state(GSM_CONNECT_TO_GPRS);
					gsm_init_set_step_old(GSM_CONNECT_TO_GPRS);
				}
				break;
			case GSM_ERROR:
			case GSM_TIMEOUT:
				LOG_TRACE(_GSM_, "GSM_GET_RES_READ_GPRS_STATE reset!");
				gsm_set_init_state(GSM_RESET_MODULE);
				break;
			default:
				break;
		}
		gsm_set_delay(50);
		gsm_set_free();
	}
}

static void gsm_init_step_config_firewall(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_SET_FIREWALL);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			gsm.temp_counter = 0;
			gsm_init_next_step();
		}
		else
		{
			if (gsm.temp_counter++ < 2)
			{
				gsm_set_init_state(GSM_CONFIG_FIREWALL);
			}
			else
			{
				gsm_set_init_state(GSM_RESET_MODULE);
			}
			gsm_set_delay(50);
		}
		gsm_set_free();
	}
}

static void gsm_init_step_get_ntp(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		if (modem_config_is_ntp_active()) {
			gsm_init_send(ATQUERY_GET_NTP_DATETIME);
		} else {
			gsm_init_next_step();
		}
		return;
	}
	gsm_init_generic_response(true, 50);
}

static void gsm_init_step_get_cops_state(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_COPS_STATE);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		switch (res)
		{
		case GSM_COPS_MANUEL_UNLOCKED_MODE:
		case GSM_COPS_DEREGISTER_MODE:
			gsm_set_init_state(GSM_MANUEL_NETWORK_REGISTER);
			break;
		case GSM_COPS_AUTOMOATIC_MODE:
		case GSM_COPS_3_MODE:
		case GSM_COPS_MANUEL_AUTOMATIC_MODE:
		case GSM_COPS_MANUEL_LOCKED_MODE:
		default:
			gsm_init_next_step();
			break;
		}
		gsm_set_delay(50);
		gsm_set_free();
	}
}

static void gsm_init_step_reboot(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_ENHRST);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			gsm.reboot_counter = 0;
			gsm_set_delay(15000);
			gsm_set_init_vector(gsm_common_init_vector, sizeof(gsm_common_init_vector));
			gsm_set_main_state(GSM_COMMON_INIT_MODE);
			gsm_set_init_state(GSM_SOFT_INIT);
		}
		else
		{
			gsm_set_delay(50);
			gsm_set_init_state(GSM_RESET_MODULE);
		}
		gsm_set_free();
	}
}

static void gsm_init_step_shdn(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_SHDN);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		if (res == GSM_RESPONSE_OK)
		{
			gsm.reboot_counter = 0;
			gsm_set_delay(6500);
			gsm_set_init_vector(gsm_common_init_vector, sizeof(gsm_common_init_vector));
			gsm_set_main_state(GSM_COMMON_INIT_MODE);
			gsm_set_init_state(GSM_SOFT_INIT);
		}
		else
		{
			gsm_set_delay(50);
			gsm_set_init_state(GSM_RESET_MODULE);
		}
		gsm_set_free();
	}
}

static void gsm_init_step_get_ceer(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_CEER);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	if (res != 0U)
	{
		gsm_set_init_state(GSM_RESET_MODULE);
		gsm_set_delay(50);
		gsm_set_free();
	}
}

static void gsm_init_step_open_listener_socket(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_OPEN_LISTENER_SOCKET);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res)
	{
		case GSM_RESPONSE_OK:
			gsm_init_next_step();
			gsm_set_socket_state(LISTENER_SOCKET, SOCKET_LISTENING);
			led_driver_set_web_mode(LED_LISTENER_LISTENING);
			gsm_set_delay(50);
			gsm.signal_quality_timer = gsm_get_tick() + GSM_SIGNAL_QUALITY_PERIOD_MS;
			gsm.listener[GSM_LISTENER_WEB].socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			gsm.gsm_network_timer = gsm_get_tick() + GSM_NETWORK_CHECK_PERIOD_MS;
			gsm_set_tx_state(GSM_TX_READY);
			break;
		case GSM_SOCKET_ERROR_ALREADY_OPEN:
		case GSM_ERROR:
		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "ERR: Listener Socket: %d", res);
			LOG_TRACE(_GSM_, "Listener Soket acilamadi !");
			gsm_set_delay(1500);
			/* fallthrough */
		case GSM_CONTEXT_NOT_OPENED:
			gsm_set_init_state(GSM_GET_GPRS_STATE);
			if (gsm.temp_counter++ > 2) {
				gsm.temp_counter = 0;
				gsm_set_init_state(GSM_RESET_MODULE);
				LOG_TRACE(_GSM_, "Listener Soket acilamadi, RESET !");
			}
			break;
		default:
			break;
	}
	if (res != 0U) {
		gsm_set_free();
	}
}

static void gsm_init_step_open_iec104_listener(void)
{
	if (gsm.init_phase == GSM_INIT_PHASE_SEND)
	{
		gsm_init_send(ATQUERY_OPEN_IEC104_LISTENER_SOCKET);
		return;
	}
	uint32_t res = gsm_engine_get_query_res();
	switch (res)
	{
		case GSM_RESPONSE_OK:
			gsm_init_next_step();
			gsm_set_socket_state(IEC104_LISTENER_SOCKET, SOCKET_LISTENING);
			led_driver_set_iec104_mode(LED_LISTENER_LISTENING);
			gsm_set_delay(50);
			LOG_TRACE(_GSM_, "IEC104 Listener Socket Acildi...", res);
			gsm.listener[GSM_LISTENER_IEC104].socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			gsm_set_tx_state(GSM_TX_READY);
			break;
		case GSM_SOCKET_ERROR_ALREADY_OPEN:
		case GSM_ERROR:
		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "ERR: IEC104 Listener Socket: %d", res);
			LOG_TRACE(_GSM_, "IEC104 Listener Soket acilamadi !");
			gsm_set_delay(1500);
			/* fallthrough */
		case GSM_CONTEXT_NOT_OPENED:
			gsm_set_init_state(GSM_GET_GPRS_STATE);
			if (gsm.temp_counter++ > 2) {
				gsm.temp_counter = 0;
				gsm_set_init_state(GSM_RESET_MODULE);
				LOG_TRACE(_GSM_, "IEC104 Listener Soket acilamadi, RESET !");
			}
			break;
		default:
			break;
	}
	if (res != 0U) {
		gsm_set_free();
	}
}

static void gsm_init_step_done(void)
{
	gsm.module_state = 0;
	gsm_set_init_state(0);
	gsm.reboot_counter = 0;
	if (gsm_get_main_state() == GSM_COMMON_INIT_MODE)
	{
		gsm_set_init_vector(gsm_LE910R1_init_vector, sizeof(gsm_LE910R1_init_vector));
		gsm_set_main_state(GSM_MODULE_INIT_MODE);
	}
	else
	{
		led_driver_set_modem_mode(LED_MODEM_READY);
		gsm_set_main_state(GSM_NORMAL_MODE);
	}
}

/* -----------------------------------------------------------------------
 *  gsm_init_old — pure dispatch
 * --------------------------------------------------------------------- */

void gsm_init_old(void)
{
	switch (gsm_get_init_state())
	{
		/* Control states (no AT command) */
		case GSM_SOFT_INIT:                       gsm_init_step_soft_init();              break;
		case GSM_RESET_MODULE:                    gsm_init_step_reset_module();           break;
		case GSM_PIN_RESET:                       gsm_init_step_pin_reset();              break;

		/* Simple steps: error -> reset, delay 50 */
		case GSM_FACTORY_RESET:                   gsm_init_simple_step(ATQUERY_FACTORY_RESET,            false, 50);   break;
		case GSM_DISABLE_FLOW_CNTRL:              gsm_init_simple_step(ATQUERY_SET_FLOW_CONTROL,         false, 50);   break;
		case GSM_SET_BAUDRATE:                    gsm_init_simple_step(ATQUERY_SET_BAUDRATE,             false, 50);   break;
		case GSM_SET_LOW_BAUDRATE:                gsm_init_simple_step(ATQUERY_SET_LOW_BAUDRATE,         false, 50);   break;
		case GSM_DISABLE_ECHO:                    gsm_init_simple_step(ATQUERY_DISABLE_ECHO,             false, 50);   break;
		case GSM_ENABLE_ERROR_CODES:              gsm_init_simple_step(ATQUERY_SET_CMEE,                 false, 50);   break;
		case GSM_CONFIG_SIMIN_PIN:                gsm_init_simple_step(ATQUERY_SET_SIMDETECT_PIN_CFG,    false, 50);   break;
		case GSM_CONFIG_SIM_DETECTION_MODE:       gsm_init_simple_step(ATQUERY_SET_SIMDETECT_MODE,       false, 50);   break;
		case GSM_ENABLE_STAT_LED:                 gsm_init_simple_step(ATQUERY_SET_STATUS_LED,           false, 50);   break;
		case GSM_GET_MODULE_FW_VER:               gsm_init_simple_step(ATQUERY_GET_MODULE_FW_VERSION,    false, 50);   break;
		case GSM_GET_IMEI:                        gsm_init_simple_step(ATQUERY_CGSN,                     false, 50);   break;
		case GSM_SET_GPRS_CLASS:                  gsm_init_simple_step(ATQUERY_SET_GPRS_CLASS,           false, 50);   break;
		case GSM_CONFIG_SOCKET_WEB_SCFG:          gsm_init_simple_step(ATQUERY_SOCKET_1_CONFIG_SCFG,     false, 50);   break;
		case GSM_CONFIG_SOCKET_WEB_SCFGEXT:       gsm_init_simple_step(ATQUERY_SOCKET_1_CONFIG_SCFGEXT,  false, 50);   break;
		case GSM_CONFIG_SOCKET_WEB_SCFGEXT2:      gsm_init_simple_step(ATQUERY_SOCKET_1_CONFIG_SCFGEXT2, false, 50);   break;
		case GSM_CONFIG_SOCKET_WEB_SCFGEXT3:      gsm_init_simple_step(ATQUERY_SOCKET_1_CONFIG_SCFGEXT3, false, 50);   break;
		case GSM_CONFIG_SOCKET_IEC104_SCFG:       gsm_init_simple_step(ATQUERY_SOCKET_3_CONFIG_SCFG,     false, 50);   break;
		case GSM_CONFIG_SOCKET_IEC104_SCFGEXT:    gsm_init_simple_step(ATQUERY_SOCKET_3_CONFIG_SCFGEXT,  false, 50);   break;
		case GSM_CONFIG_SOCKET_IEC104_SCFGEXT2:   gsm_init_simple_step(ATQUERY_SOCKET_3_CONFIG_SCFGEXT2, false, 50);   break;
		case GSM_CONFIG_SOCKET_IEC104_SCFGEXT3:   gsm_init_simple_step(ATQUERY_SOCKET_3_CONFIG_SCFGEXT3, false, 50);   break;
		case GSM_CONFIG_PING_SUPPORT:             gsm_init_simple_step(ATQUERY_ICMP,                     false, 50);   break;
		case GSM_GET_SIGNAL_QUALITY:              gsm_init_simple_step(ATQUERY_GET_SIGNAL_QUALITY,       false, 50);   break;
		case GSM_SET_GPRS_Event_Reporting:        gsm_init_simple_step(ATQUERY_CGEREP,                   false, 50);   break;
		case GSM_SET_SEARCHLIM:                   gsm_init_simple_step(ATQUERY_SEARCHLIM,                false, 50);   break;
		case GSM_SET_FPLMN:                       gsm_init_simple_step(ATQUERY_FPLMN,                    false, 50);   break;

		/* Simple steps: error -> reset, delay 2000 */
		case GSM_SET_SERVICE_CLASS:                gsm_init_simple_step(ATQUERY_SET_SERVICE_CLASS,        false, 2000); break;

		/* Simple steps: error -> skip, delay 50 */
		case GSM_GET_SIMCARD_IMSI:                gsm_init_simple_step(ATQUERY_CIMI,                     true, 50);    break;
		case GSM_GET_PHONE_NUMBER:                gsm_init_simple_step(ATQUERY_GET_PHONE_NUMBER,         true, 50);    break;
		case GSM_CONFIG_TCP_WINDOW_SIZE:          gsm_init_simple_step(ATQUERY_SET_TCPWINDOW,            true, 50);    break;
		case GSM_GET_NETWORK_INFO:                gsm_init_simple_step(ATQUERY_GET_CCED,                 true, 50);    break;
		case GSM_CONFIG_SMS_FORMAT:               gsm_init_simple_step(ATQUERY_CMGF,                     true, 50);    break;
		case GSM_CELL_MON:                        gsm_init_simple_step(ATQUERY_MONI,                     true, 50);    break;
		case GSM_SET_NETWORK_TECHNOLOGY:          gsm_init_simple_step(ATQUERY_WS46,                     true, 50);    break;
		case GSM_SET_DATETIME_UPDATE:             gsm_init_simple_step(ATQUERY_NTIZ,                     true, 50);    break;
		case GSM_READ_DATETIME:                   gsm_init_simple_step(ATQUERY_CCLK,                     true, 50);    break;
		case GSM_GET_SIMCARD_CCID:                gsm_init_simple_step(ATQUERY_CCID,                     true, 50);    break;

		/* Simple steps: error -> skip, no delay */
		case GSM_ENABLE_TELIT_LOG_1:              gsm_init_simple_step(ATQUERY_TELIT_LOG_1_ENABLE,       true, 0);     break;
		case GSM_ENABLE_TELIT_LOG_2:              gsm_init_simple_step(ATQUERY_TELIT_LOG_2_ENABLE,       true, 0);     break;

		/* Complex steps */
		case GSM_CHECHK_MODULE:                   gsm_init_step_check_module();            break;
		case GSM_GET_MODULE_NAME:                 gsm_init_step_get_module_name();          break;
		case GSM_CHECK_SIMCARD_AND_PIN:           gsm_init_step_check_simcard();            break;
		case GSM_SET_PIN_NO:                      gsm_init_step_set_pin_no();               break;
		case GSM_CHECK_GSM_VOICE_SMS_NEWTWORK_STATE: gsm_init_step_check_voice_sms_network(); break;
		case GSM_SET_APN:                         gsm_init_step_set_apn();                  break;
		case GSM_ACTIVATE_GPRS:                   gsm_init_step_activate_gprs();            break;
		case GSM_CHECK_GPRS_NEWTWORK_STATE:       gsm_init_step_check_gprs_network();       break;
		case GSM_CHECK_LTE_NEWTWORK_STATE:        gsm_init_step_check_lte_network();        break;
		case GSM_CONNECT_TO_GPRS:                 gsm_init_step_connect_gprs();             break;
		case GSM_GET_GPRS_STATE:                  gsm_init_step_get_gprs_state();           break;
		case GSM_CONFIG_FIREWALL:                 gsm_init_step_config_firewall();          break;
		case GSM_GET_NTP_DATE_TIME:               gsm_init_step_get_ntp();                  break;
		case GSM_GET_COPS_STATE:                  gsm_init_step_get_cops_state();           break;
		case GSM_REBOOT:                          gsm_init_step_reboot();                   break;
		case GSM_SHDN:                            gsm_init_step_shdn();                     break;
		case GSM_GET_CEER:                        gsm_init_step_get_ceer();                 break;
		case GSM_OPEN_WEB_LISTENER_SOCKET:        gsm_init_step_open_listener_socket();     break;
		case GSM_OPEN_IEC104_LISTENER_SOCKET:     gsm_init_step_open_iec104_listener();     break;
		case GSM_GET_LISTENER_SOCKET_INFO:        gsm_init_simple_step(ATQUERY_GET_LISTENER_SOCKET_INFO, true, 0); break;
		case GSM_INIT_DONE:                       gsm_init_step_done();                     break;

		default:
			break;
	}
}
