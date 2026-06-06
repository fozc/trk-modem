/*
 * gsm_listener_process.c
 *
 * Unified listener socket state machine.
 * Both the web-server and IEC104 listener sockets are driven by a single
 * parametric process, differentiated only by their const config struct.
 */

#include "gsm_listener_process.h"
#include "gsm_process.h"
#include "gsm_socket.h"
#include "at_engine2.h"
#include "bsp.h"
#include "modem_config.h"
#include "iec104_process.h"
#include "web_server.h"
#include "led_driver.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 *  External state
 * ------------------------------------------------------------------------- */
extern gsm_t gsm;

/* ---------------------------------------------------------------------------
 *  Config instances — one per listener socket
 * ------------------------------------------------------------------------- */
static const gsm_listener_cfg_t s_cfg_web = {
	.id              = GSM_LISTENER_WEB,
	.socket_id       = LISTENER_SOCKET,
	.tx_direction    = GSM_TX_DIR_LISTENER_SOCKET,

	.at_cmd = {
		[LS_AT_CHECK_INFO]   = ATQUERY_GET_LISTENER_SOCKET_INFO,
		[LS_AT_DATA_MODE]    = ATQUERY_SET_LISTENER_SOCKET_DATA_MODE,
		[LS_AT_SEND_DATA]    = ATQUERY_SEND_DATA_TO_LISTENER_SOCKET,
		[LS_AT_CHECK_SOCKET] = ATQUERY_GET_LISTENER_SOCKET_STATUS,
		[LS_AT_CLOSE_SOCKET] = ATQUERY_CLOSE_LISTENER_SOCKET,
		[LS_AT_OPEN_SOCKET]  = ATQUERY_OPEN_LISTENER_SOCKET,
		[LS_AT_READ_DATA]    = ATQUERY_GET_LISTENER_SOCKET_DATA,
		[LS_AT_ACCEPT_CONN]  = ATQUERY_ACCEPT_LISTENER_CONN,
	},

	.on_connected    = modem_config_webserver_client_connected_cb,
	.on_closed       = webserver_on_connection_closed,
	.reset_session   = gsm_reset_web_session_info,
};

static const gsm_listener_cfg_t s_cfg_iec104 = {
	.id              = GSM_LISTENER_IEC104,
	.socket_id       = IEC104_LISTENER_SOCKET,
	.tx_direction    = GSM_TX_DIR_IEC104_SOCKET,

	.at_cmd = {
		[LS_AT_CHECK_INFO]   = ATQUERY_GET_IEC104_LISTENER_SOCKET_INFO,
		[LS_AT_DATA_MODE]    = ATQUERY_SET_IEC104_LISTENER_SOCKET_DATA_MODE,
		[LS_AT_SEND_DATA]    = ATQUERY_SEND_DATA_TO_IEC104_LISTENER_SOCKET,
		[LS_AT_CHECK_SOCKET] = ATQUERY_GET_ALL_SOCKET_STATUS,
		[LS_AT_CLOSE_SOCKET] = ATQUERY_CLOSE_IEC104_LISTENER_SOCKET,
		[LS_AT_OPEN_SOCKET]  = ATQUERY_OPEN_IEC104_LISTENER_SOCKET,
		[LS_AT_READ_DATA]    = ATQUERY_GET_IEC104_LISTENER_SOCKET_DATA,
		[LS_AT_ACCEPT_CONN]  = ATQUERY_ACCEPT_IEC104_LISTENER_CONN,
	},

	.on_connected    = modem_config_iec_client_connected_cb,
	.on_closed       = iec104_process_socket_closed_cb,
	.reset_session   = gsm_reset_iec104_session_info,
};

/* ---------------------------------------------------------------------------
 *  Convenience wrappers — one call per listener, config stays module-private
 * ------------------------------------------------------------------------- */
void gsm_listener_web_process(void)
{
	gsm_listener_socket_process(&s_cfg_web);
}

void gsm_listener_iec104_process(void)
{
	gsm_listener_socket_process(&s_cfg_iec104);
}

/* ---------------------------------------------------------------------------
 *  Static helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Update the LED for a given listener socket.
 */
static void ls_led_update(gsm_listener_id_t id,
                           led_listener_mode_t mode)
{
	if (id == GSM_LISTENER_IEC104)
	{
		led_driver_set_iec104_mode(mode);
	}
	else
	{
		led_driver_set_web_mode(mode);
	}
}

/**
 * @brief Send an AT query and switch the listener context to wait-response phase.
 */
static void ls_send(gsm_listener_ctx_t *ctx, uint8_t at_query)
{
	if (gsm_engine_send_query(at_query))
	{
		ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
	}
}

/**
 * @brief Transition listener state — always resets phase to SEND.
 *
 * Every state transition must go through this function so the phase is
 * never left stale in WAIT_RESPONSE when the next handler expects SEND.
 */
static inline void ls_set_state(gsm_listener_ctx_t *ctx, uint8_t new_state)
{
	ctx->state = new_state;
	ctx->phase = GSM_INIT_PHASE_SEND;
}

static void handle_idle(const gsm_listener_cfg_t *p_cfg,
                        gsm_listener_ctx_t *ctx)
{
	if (gsm_listener_get_no_carrier(p_cfg->id))
	{
		gsm_listener_set_no_carrier(p_cfg->id, 0);
		ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
		gsm_socket_record_disconnect(p_cfg->socket_id, SOCK_DISC_NO_CARRIER);
		if (p_cfg->on_closed != NULL) {
			p_cfg->on_closed();
		}
		ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
	}
	else if (gsm_listener_get_new_conn_req(p_cfg->id))
	{
		gsm_listener_set_new_conn_req(p_cfg->id, 0);
		ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
		ls_set_state(ctx, GSM_LS_ACCEPT_CONNECTION);
	}
	else if (gsm_listener_get_rx_available(p_cfg->id))
	{
		gsm_listener_set_rx_available(p_cfg->id, 0);
		ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
		ls_set_state(ctx, GSM_LS_READ_SOCKET_DATA);
	}
	else if (gsm_get_tx_direction() == p_cfg->tx_direction
	         && gsm_get_tx_state()  == GSM_TX_FIRED
	         && !gsm_is_busy())
	{
		if (gsm_get_socket_state(p_cfg->socket_id) == SOCKET_CONNECTED)
		{
			ctx->tcp_ack_timer = 0;
			/* Recent activity (<3 s) → skip CHECK_SOCKET_INFO, go directly to DATA_MODE */
			if (gsm_get_tick() - ctx->socket_timer > 3000U)
			{
				ls_set_state(ctx, GSM_LS_CHECK_SOCKET_INFO);
			}
			else
			{
				ls_set_state(ctx, GSM_LS_DATA_MODE);
			}
		}
		else
		{
			LOG_TRACE(WARNING, "[LS:%u] TX fired but socket not connected",
			          (unsigned)p_cfg->id);
			gsm_set_tx_state(GSM_TX_READY);
			gsm_set_free();
			gsm_set_tx_error();
			if (p_cfg->on_closed != NULL) {
				p_cfg->on_closed();
			}
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
		}
	}
	else if (gsm_get_socket_state(p_cfg->socket_id) == SOCKET_CONNECTED
	         && ctx->data_check_timer < bsp_get_tick()
	         && !gsm_is_busy())
	{
		/* Periodic data availability check */
		ctx->tcp_ack_timer = 0;
		ls_set_state(ctx, GSM_LS_CHECK_SOCKET_INFO);
	}
	else
	{
		/* MISRA 15.7 — no action */
	}
}

static void handle_check_socket_info(const gsm_listener_cfg_t *p_cfg,
                                         gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_CHECK_INFO]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) {
		return;
	}

	ctx->data_check_timer = bsp_get_tick() + GSM_LS_DATA_CHECK_TIMER_MS;

	switch (res)
	{
		case GSM_RESPONSE_OK:
			/*
			 * Veri gonderimi oncesinde soketin hala acik olup olmadigini tespit
			 * etmek icin kullanilir.  Daha once veri akisi olmus (had_data_activity)
			 * ancak SI tamamen sifir donuyorsa, soket kapanmis demektir —
			 * veri gondermeden soketi kapat ve yeniden ac.
			 */
#if 1
			if ((ctx->si_all_zero != 0U) && (ctx->had_data_activity != 0U))
			{
				LOG(WARNING, "[LS:%u] #SI all-zero: soket kapali", (unsigned)p_cfg->id);
				gsm_socket_record_disconnect(p_cfg->socket_id, SOCK_DISC_SOCKET_CLOSED);
				gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
				if (p_cfg->reset_session != NULL) {
					p_cfg->reset_session();
				}
				if (p_cfg->on_closed != NULL) {
					p_cfg->on_closed();
				}
				gsm_set_tx_error();
				ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
				break;
			}
#endif
			if (gsm_get_tx_state()    == GSM_TX_FIRED
			    && gsm_get_tx_direction() == p_cfg->tx_direction)
			{
				if (ctx->ack_waiting <= GSM_LS_ACK_THRESHOLD_BYTES)
				{
					ls_set_state(ctx, GSM_LS_DATA_MODE);
				}
				else if (ctx->tcp_ack_timer++ < 30U)
				{
					gsm_set_delay(1500);
					ls_set_state(ctx, GSM_LS_CHECK_SOCKET_INFO);
				}
				else
				{
					LOG_TRACE(_GSM_, "[LS:%u] TCP ACK FAIL", (unsigned)p_cfg->id);
					ctx->tcp_ack_timer = 0;
					gsm_set_tx_error();
					ls_set_state(ctx, GSM_LS_IDLE);
				}
			}
			else
			{
				ls_set_state(ctx, GSM_LS_IDLE);
			}
			break;

		case GSM_NO_CARRIER:
			gsm_socket_record_disconnect(p_cfg->socket_id, SOCK_DISC_NO_CARRIER);
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
			if (p_cfg->reset_session != NULL) {
				p_cfg->reset_session();
			}
			gsm_set_tx_error();
			break;

		case GSM_ERROR:
			if (ctx->tcp_ack_timer < 10U)
			{
				gsm_set_delay(1500);
				ls_set_state(ctx, GSM_LS_CHECK_SOCKET_INFO);
			}
			else
			{
				gsm_set_delay(50);
				ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			}
			break;

		case GSM_TIMEOUT:
			gsm_set_delay(50);
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			gsm_set_tx_error();
			break;

		default:
			ls_set_state(ctx, GSM_LS_IDLE);
			break;
	}
	gsm_set_free();
}

static void handle_data_mode(const gsm_listener_cfg_t *p_cfg,
                                 gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_DATA_MODE]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			gsm_set_busy();
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_DATA_MODE);
			ls_set_state(ctx, GSM_LS_SEND_DATA);
			/* Attempt immediate send to save one tick */
			if (gsm_engine_socket_send(p_cfg->at_cmd[LS_AT_SEND_DATA]))
			{
				ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
			}
			break;

		case GSM_NO_CARRIER:
			gsm_socket_record_disconnect(p_cfg->socket_id, SOCK_DISC_NO_CARRIER);
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
			if (p_cfg->reset_session != NULL) { p_cfg->reset_session(); }
			gsm_set_delay(1000);
			ls_set_state(ctx, GSM_LS_OPEN_SOCKET);
			gsm_set_free();
			break;

		case GSM_ERROR:
			LOG_TRACE(_GSM_, "[LS:%u] DATA_MODE Error", (unsigned)p_cfg->id);
			ctx->socket_timer = gsm_get_tick() + 2000U;
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_tx_error();
			gsm_set_tx_state(GSM_TX_READY);
			gsm_set_delay(500);
			gsm_set_free();
			break;

		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "[LS:%u] DATA_MODE timeout", (unsigned)p_cfg->id);
			ctx->socket_timer = gsm_get_tick() + 2000U;
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_tx_error();
			gsm_set_tx_state(GSM_TX_READY);
			gsm_set_delay(500);
			gsm_set_free();
			break;

		default:
			gsm_set_free();
			break;
	}
}

static void handle_send_data(const gsm_listener_cfg_t *p_cfg,
                                 gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		if (gsm_engine_socket_send(p_cfg->at_cmd[LS_AT_SEND_DATA]))
		{
			ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
		}
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			gsm_set_delay(50);
			ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CONNECTED);
			ctx->had_data_activity = 1U;
			ls_set_state(ctx, GSM_LS_IDLE);
			break;

		case GSM_NO_CARRIER:
			gsm_set_delay(1000);
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			gsm_set_tx_error();
			gsm_set_free();
			break;

		case GSM_SOCKET_DISCONNECTED_TIMEOUT:
			LOG_TRACE(_GSM_, "[LS:%u] Send failed — disconnected/timeout",
			          (unsigned)p_cfg->id);
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_delay(50);
			gsm_set_tx_error();
			break;

		case GSM_ERROR:
			gsm_set_delay(50);
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_tx_error();
			break;

		case GSM_TIMEOUT:
			/* Modem may be stuck in data mode — AT commands would be
			 * interpreted as data bytes. Only safe recovery is full reset. */
			LOG_TRACE(_GSM_, "[LS:%u] SEND_DATA timeout — resetting",
			          (unsigned)p_cfg->id);
			gsm_reset_process_old();
			gsm_set_tx_error();
			break;

		default:
			break;
	}
	gsm_set_tx_state(GSM_TX_READY);
	gsm_set_free();
}

static void handle_check_socket(const gsm_listener_cfg_t *p_cfg,
                                    gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_CHECK_SOCKET]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			switch (gsm_get_raw_socket_state(p_cfg->socket_id))
			{
				case '0': /* Closed */
					LOG_TRACE(_GSM_, "[LS:%u] Soket Kapali", (unsigned)p_cfg->id);
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
					if (p_cfg->reset_session != NULL) { p_cfg->reset_session(); }
					if (p_cfg->on_closed    != NULL) { p_cfg->on_closed(); }
					if (gsm_listener_get_new_conn_req(p_cfg->id))
					{
						gsm_listener_set_new_conn_req(p_cfg->id, 0);
					}
					ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
					break;
				case '1': /* Opened */
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_OPENED);
					ls_set_state(ctx, GSM_LS_IDLE);
					break;
				case '2': /* Suspended — client connected, no pending data */
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_CONNECTED);
					ls_led_update(p_cfg->id, LED_LISTENER_CONNECTED);
					ls_set_state(ctx, GSM_LS_IDLE);
					break;
				case '3': /* Suspended — data pending */
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_CONNECTED);
					ls_led_update(p_cfg->id, LED_LISTENER_CONNECTED);
					ls_set_state(ctx, GSM_LS_READ_SOCKET_DATA);
					gsm_set_delay(50);
					break;
				case '4': /* Listening */
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_LISTENING);
					ls_led_update(p_cfg->id, LED_LISTENER_LISTENING);
					ls_set_state(ctx, GSM_LS_IDLE);
					break;
				case '5': /* Incoming connection pending */
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_OPENED);
					ls_set_state(ctx, GSM_LS_ACCEPT_CONNECTION);
					gsm_set_delay(50);
					break;
				case '6': /* fall-through */
				case '7':
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_OPENED);
					ls_set_state(ctx, GSM_LS_IDLE);
					break;
				default:
					LOG(_GSM_, "[LS:%u] Bilinmeyen soket durumu: %c",
					    (unsigned)p_cfg->id,
					    gsm_get_raw_socket_state(p_cfg->socket_id));
					gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
					if (p_cfg->reset_session != NULL) { p_cfg->reset_session(); }
					if (p_cfg->on_closed    != NULL) { p_cfg->on_closed(); }
					ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
					break;
			}

			/* Clear stale rx_available flag when no data actually pending */
			if (gsm_get_raw_socket_state(p_cfg->socket_id) != '3'
			    && gsm_listener_get_rx_available(p_cfg->id))
			{
				gsm_listener_set_rx_available(p_cfg->id, 0);
			}
			break;

		case GSM_SS_BLOCKED_IP:
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
			if (p_cfg->on_closed != NULL) { p_cfg->on_closed(); }
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			break;

		case GSM_NO_CARRIER:
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
			if (p_cfg->on_closed != NULL) { p_cfg->on_closed(); }
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			break;

		case GSM_ERROR:
			ls_set_state(ctx, GSM_LS_CHECK_NET);
			break;

		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "[LS:%u] CHECK_SOCKET timeout", (unsigned)p_cfg->id);
			ls_set_state(ctx, GSM_LS_CHECK_NET);
			break;

		default:
			ls_set_state(ctx, GSM_LS_IDLE);
			break;
	}
	gsm_set_delay(50);
	gsm_set_free();
}

static void handle_close_socket(const gsm_listener_cfg_t *p_cfg,
                                    gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_CLOSE_SOCKET]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	if (p_cfg->on_closed != NULL) { p_cfg->on_closed(); }
	ls_led_update(p_cfg->id, LED_LISTENER_OFF);

	if (gsm_listener_get_no_carrier(p_cfg->id))
	{
		gsm_listener_set_no_carrier(p_cfg->id, 0);
	}

	switch (res)
	{
		case GSM_RESPONSE_OK:
		case GSM_ERROR:
			if (p_cfg->reset_session != NULL) { p_cfg->reset_session(); }
			ctx->temp_counter = 0;
			ls_set_state(ctx, GSM_LS_CHECK_NET);
			gsm_set_delay(50);
			break;

		case GSM_TIMEOUT:
			if (ctx->temp_counter++ < 2U)
			{
				ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
				gsm_set_delay(50);
			}
			else
			{
				ls_set_state(ctx, GSM_LS_CHECK_NET);
				gsm_set_delay(50);
			}
			break;

		default:
			ls_set_state(ctx, GSM_LS_CHECK_NET);
			gsm_set_delay(50);
			break;
	}
	gsm_set_free();
}

static void handle_open_socket(const gsm_listener_cfg_t *p_cfg,
                                   gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		gsm_socket_record_attempt(p_cfg->socket_id);
		ls_send(ctx, p_cfg->at_cmd[LS_AT_OPEN_SOCKET]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			ctx->temp_counter = 0;
			ctx->had_data_activity = 0U;
			gsm_listener_set_no_carrier(p_cfg->id, 0U); /* Clear stale flag */
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_LISTENING);
			ls_led_update(p_cfg->id, LED_LISTENER_LISTENING);
			ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			ls_set_state(ctx, GSM_LS_IDLE);
			break;

		case GSM_CONTEXT_NOT_OPENED:
			if (ctx->temp_counter++ >= 2U)
			{
				LOG_TRACE(_GSM_, "[LS:%u] GPRS context not opened — resetting",
				          (unsigned)p_cfg->id);
				gsm_reset_process_old();
			}
			else
			{
				gsm_set_delay(1000);
				ls_set_state(ctx, GSM_LS_CHECK_NET);
			}
			break;

		case GSM_SOCKET_ERROR_ALREADY_OPEN:
			LOG_TRACE(_GSM_, "[LS:%u] Soket zaten acik", (unsigned)p_cfg->id);
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_LISTENING);
			ls_led_update(p_cfg->id, LED_LISTENER_LISTENING);
			ls_set_state(ctx, GSM_LS_IDLE);
			break;

		case GSM_ERROR:
			gsm_set_delay(500);
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			break;

		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "[LS:%u] OPEN_SOCKET timeout", (unsigned)p_cfg->id);
			gsm_set_delay(2000);
			ls_set_state(ctx, GSM_LS_CHECK_NET);
			break;

		default:
			ls_set_state(ctx, GSM_LS_IDLE);
			break;
	}
	gsm_set_free();
}

static void handle_read_socket_data(const gsm_listener_cfg_t *p_cfg,
                                        gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_READ_DATA]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			ctx->socket_timer = gsm_get_tick() + GSM_LS_SOCKET_TIMER_MS;
			gsm_set_delay(50);
			ctx->had_data_activity = 1U;
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			break;

		case GSM_SOCKET_NO_CARRIER:
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CLOSED);
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			break;

		case GSM_ERROR:
			gsm_set_delay(50);
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			break;

		case GSM_TIMEOUT:
			LOG_TRACE(_GSM_, "[LS:%u] READ_SOCKET_DATA timeout — resetting",
			          (unsigned)p_cfg->id);
			gsm_reset_process_old();
			break;

		default:
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			break;
	}
	gsm_set_free();
}

static void handle_accept_connection(const gsm_listener_cfg_t *p_cfg,
                                         gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		ls_send(ctx, p_cfg->at_cmd[LS_AT_ACCEPT_CONN]);
		return;
	}

	uint32_t res = gsm_engine_get_query_res();
	if (res == 0U) { return; }

	switch (res)
	{
		case GSM_RESPONSE_OK:
			if (gsm_listener_get_new_conn_req(p_cfg->id))
			{
				gsm_listener_set_new_conn_req(p_cfg->id, 0);
			}
			if (p_cfg->on_connected != NULL) { p_cfg->on_connected(); }
			ctx->socket_timer     = gsm_get_tick() + 750U;
			gsm_set_socket_state(p_cfg->socket_id, SOCKET_CONNECTED);
			ls_led_update(p_cfg->id, LED_LISTENER_CONNECTED);
			gsm_socket_set_state(p_cfg->socket_id, '2'); /* Mark session start */
			ctx->data_check_timer = bsp_get_tick() + GSM_LS_DATA_CHECK_TIMER_MS;
			ctx->had_data_activity = 0U;
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_delay(50);
			break;

		case GSM_TIMEOUT:
			ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
			gsm_set_delay(50);
			break;

		case GSM_NO_CARRIER:
			ls_set_state(ctx, GSM_LS_IDLE);
			LOG_TRACE(WARNING, "[LS:%u] ACCEPT No Carrier", (unsigned)p_cfg->id);
			break;

		case GSM_ERROR:
			ls_set_state(ctx, GSM_LS_CLOSE_SOCKET);
			gsm_set_delay(50);
			break;

		default:
			ls_set_state(ctx, GSM_LS_IDLE);
			break;
	}
	gsm_set_free();
}

static void handle_check_net(const gsm_listener_cfg_t *p_cfg,
                                 gsm_listener_ctx_t *ctx)
{
	if (ctx->phase == GSM_INIT_PHASE_SEND)
	{
		gsm.net_check_requested = 1;
		ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
		return;
	}

	/* Wait for gsm_periodical_event_process to complete the GPRS check */
	if (gsm.net_check_requested != 0U) { return; }

	(void)p_cfg;

	/* Check completed — proceed to re-open the listener socket */
	ls_set_state(ctx, GSM_LS_OPEN_SOCKET);
}

/* ---------------------------------------------------------------------------
 *  gsm_listener_socket_process — unified listener state machine
 * ------------------------------------------------------------------------- */
void gsm_listener_socket_process(const gsm_listener_cfg_t *p_cfg)
{
	if (p_cfg == NULL) { return; }

	gsm_listener_ctx_t *ctx = &gsm.listener[p_cfg->id];

	/* Timer-triggered periodic socket check */
	if (!gsm_listener_get_no_carrier(p_cfg->id)
	    && gsm_get_tick() >= ctx->socket_timer
	    && gsm_get_main_state() == GSM_NORMAL_MODE
	    && ctx->state == GSM_LS_IDLE)
	{
		ls_set_state(ctx, GSM_LS_CHECK_SOCKET);
	}

	switch (ctx->state)
	{
		case GSM_LS_IDLE:
			handle_idle(p_cfg, ctx);
			break;

		case GSM_LS_CHECK_SOCKET_INFO:
			handle_check_socket_info(p_cfg, ctx);
			break;

		case GSM_LS_DATA_MODE:
			handle_data_mode(p_cfg, ctx);
			break;

		case GSM_LS_SEND_DATA:
			handle_send_data(p_cfg, ctx);
			break;

		case GSM_LS_CHECK_SOCKET:
			handle_check_socket(p_cfg, ctx);
			break;

		case GSM_LS_CLOSE_SOCKET:
			handle_close_socket(p_cfg, ctx);
			break;

		case GSM_LS_OPEN_SOCKET:
			handle_open_socket(p_cfg, ctx);
			break;

		case GSM_LS_READ_SOCKET_DATA:
			handle_read_socket_data(p_cfg, ctx);
			break;

		case GSM_LS_ACCEPT_CONNECTION:
			handle_accept_connection(p_cfg, ctx);
			break;

		case GSM_LS_CHECK_NET:
			handle_check_net(p_cfg, ctx);
			break;

		default:
			/* Unknown state — return to idle as fail-safe */
			ls_set_state(ctx, GSM_LS_IDLE);
			break;
	}
}
