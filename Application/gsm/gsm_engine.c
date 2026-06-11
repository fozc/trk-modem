/*
 * gsm_engine.c
 *
 *  Created on: 20 Mar 2018
 *      Author: fozcan
 */

/*
 * DTE: Computer (mcu)
 * DCE: Modem
 *
 *
 * Olasi AT komut cevaplari:
 * 	\r\nOK\r\n
 * 	\r\nERROR\r\n
 * 	\r\nBUSY\r\n
 * 	\r\nNO CARRIER\r\n
 * 	\r\nRING\r\n
 *	\r\nSRING\r\n
 *	\r\n#SRECV: ...\r\n\r\nOK\r\n
 * 	\r\n+CREG: 0,1\r\n\r\nOK\r\n
 *
 *
 * MCC: Mobile Country Codes, MNC: Mobile Network Code, LAC: Location Area Code, CID: Cell ID (istasyona ait data), HNI: Home network Identify
 *
 * Turkcell MCC: 286 MNC: 01 HNI: 28601
 * Vodafone MCC: 286 MNC: 02 HNI: 28602
 * Avea     MCC: 286 MNC: 03/04 (avea/aria/turktelekom)
 *
 *
 *
 * AT+CREG=1 -> Set Command
 * AT+CREG?  -> Get Command
 * AT+CREG=? -> Test Command
 * AT+CREG   -> Execution Command
 *
 */

#include <gsm_types.h>
#include "gsm_engine.h"
#include "gsm_info.h"
#include "at_engine2.h"
#include <string.h>

#include "gsm_process.h"
#include "utils.h"
#include "xprintf.h"
#include "xscanf.h"
#include "gsm_http_server.h"
#include "modem_config.h"
#include "gsm_log.h"
#include "time_service.h"
#include "iec104_process.h"
#include "iec104_config.h"
#include "gsm_socket.h"
#include "bsp.h"
#include "datetime.h"

/* !!! DIKKAT: max 6 hane olabilir !, Fazlasi icin at engine modifiye edilmeli */
const uint8_t GSM_OK_F_STR       [] = "\r\nOK\r\n"; //TODO: vifw dosyasi sifreli, aksi halde bu kisim dosya indirmede sikintiya yol acar !!!
const uint8_t GSM_OK_STR         [] = "OK\r\n";
const uint8_t GSM_OK_STR_LEN        = sizeof(GSM_OK_STR) - 1;
const uint8_t GSM_REC_READY      [] = "\r\n> ";
const uint8_t GSM_REC_READY_LEN     = sizeof(GSM_REC_READY) - 1;


gsm_t gsm = {0};
uint8_t simcard_mcc_mnc[6] = {};
uint8_t at_err_try = 0;

/*
 * Response view � read-only pointer into at_engine2's internal response buffer.
 * Synced each time gsm_at_response_ready() is called via at_engine_get_response().
 * Field names kept for compatibility with existing parsing code.
 */
static struct
{
	const uint8_t *buff;
	uint16_t       len;
} rx = {0};

static uint32_t gsm_at_response_ready(void)
{
	uint16_t len = 0U;
	rx.buff = at_engine_get_response(&len);
	rx.len  = len;

	at_engine_result_t r = at_engine_get_result();
	switch(r)
	{
		case AT_ENGINE_RESULT_OK:
			if(str_index_of((const char *)rx.buff, "NO CARRIER") >= 0)
			{
				return (uint32_t)GSM_NO_CARRIER;
			}
			return (uint32_t)GSM_RESPONSE_OK;

		case AT_ENGINE_RESULT_ERROR:
			if(str_index_of((const char *)rx.buff, "NO CARRIER") >= 0)
			{
				return (uint32_t)GSM_NO_CARRIER;
			}
			return (uint32_t)GSM_ERROR;

		case AT_ENGINE_RESULT_NO_SIM:
			GSM_LOG_ERR("NO SIM CARD !\r\n");
			if (gsm_get_main_state() == GSM_NORMAL_MODE)
			{
				gsm_set_main_state(GSM_SIM_ERROR_MODE);
			}
			return (uint32_t)GSM_ERROR;

		case AT_ENGINE_RESULT_TIMEOUT:
			return (uint32_t)GSM_TIMEOUT;

		case AT_ENGINE_RESULT_NONE:
		default:
			return 0U;
	}
}

typedef struct
{
	const char  *str;
	uint8_t    (*callback)();
	const char  *res;
	uint8_t    try;
	uint16_t timeout;
}at_cmd_t;

typedef struct
{
	uint8_t        connection_state;
	uint32_t       connection_time;
	ip_addr_t ip;
	gsm_cell_info_t cell_info;
	uint8_t  	  imei[17];
	uint8_t  	  access_technology;
	uint8_t 		  signal_quality;
	uint8_t 		  signal_quality2G;
	uint8_t 		  signal_quality4G;
	uint8_t        gprs_state;
	uint8_t        module_model;
	uint8_t		  socket_info[SOCKET_MAX];
	uint8_t       socket_state_raw[SOCKET_MAX];
	uint8_t		  module_version[16];
	ip_session_t iec104_session;
	ip_session_t web_session;
	ip_session_t trace_session;
	uint32_t       tx_counter;
	uint32_t		  rx_counter;

	uint32_t iec104_ls_rx_counter_last;
	uint32_t ls_rx_counter_last;

	uint32_t gsm_connection_time;

}gsm_info_t;


gsm_info_t gsm_info = {};

void gsm_set_gprs_state(uint8_t state)
{
	gsm_info.gprs_state = state;
	gsm_info_set_gprs_state(state);
}

uint8_t gsm_get_gprs_state(void)
{
	return gsm_info_get_gprs_state();
}

uint32_t gsm_get_ip_addr(void)
{
	return gsm_info_get_ip();
}

void gsm_set_access_technology(uint8_t val)
{
	gsm_info.access_technology = val;
	gsm_info_set_access_technology(val);
}

uint8_t gsm_get_access_technology(void)
{
	return gsm_info_get_access_technology();
}

void gsm_set_signal_quality(uint8_t val)
{
	gsm_info.signal_quality = val;
	gsm_info_set_signal_quality(val);
}

uint8_t gsm_get_signal_quality(void)
{
	return gsm_info_get_signal_quality();
}

void gsm_set_signal_quality2G(uint8_t val)
{
	gsm_info.signal_quality2G = val;
	gsm_info_set_signal_quality_2G(val);
}

uint8_t gsm_get_signal_quality2G(void)
{
	return gsm_info_get_signal_quality_2G();
}

void gsm_set_signal_quality4G(uint8_t val)
{
	gsm_info.signal_quality4G = val;
	gsm_info_set_signal_quality_4G(val);
}

uint8_t gsm_get_signal_quality4G(void)
{
	return gsm_info_get_signal_quality_4G();
}

const socket_si_info_t *gsm_get_si_info(uint8_t conn_id)
{
	if ((conn_id == 0U) || (conn_id > GSM_MODEM_SOCKET_COUNT)) {
		return NULL;
	}
	return &gsm.si_info[conn_id - 1U];
}


void gsm_get_cell_info(gsm_cell_info_t *cell_info)
{
	*cell_info = gsm_info.cell_info;
}

uint8_t gsm_get_module_model(void)
{
	return gsm_info.module_model;
}

void gsm_reset_web_session_info(void)
{
	LOG(_GSM_, "Reset Web Session Info\r\n");
	LOG(_GSM_, "Prev IP: [%lu.%lu.%lu.%lu] Total Rx: [%d]\r\n",
		gsm_info.web_session.ip.a,
		gsm_info.web_session.ip.b,
		gsm_info.web_session.ip.c,
		gsm_info.web_session.ip.d,
		gsm_info.ls_rx_counter_last);

	gsm_info.web_session.ip.ip = 0;
	gsm_info.ls_rx_counter_last = 0;
}

void gsm_reset_iec104_session_info(void)
{
	LOG(_GSM_, "IEC104 Remote Session Info\r\n");
	LOG(_GSM_, "Prev IP: [%lu.%lu.%lu.%lu] Total Rx: [%d]\r\n",
		gsm_info.iec104_session.ip.a,
		gsm_info.iec104_session.ip.b,
		gsm_info.iec104_session.ip.c,
		gsm_info.iec104_session.ip.d,
		gsm_info.ls_rx_counter_last);

	gsm_info.iec104_session.ip.ip = 0;
	gsm_info.iec104_ls_rx_counter_last = 0;
}

uint32_t gsm_get_web_client_ip(void)
{
	return gsm_info.web_session.ip.ip;
}

void gsm_get_rxtx_counters(uint32_t *tx, uint32_t *rx)
{
	gsm_info_get_rxtx_counters(tx, rx);
}

void gsm_set_dialer_socket_state(uint8_t state)
{
	if(state >= SOCKET_STATE_MAX){
		return;
	}
	gsm_info.socket_info[DIALER_SOCKET] = state;
}

void gsm_set_listener_socket_state(uint8_t state)
{
	if(state >= SOCKET_STATE_MAX){
		return;
	}
	gsm_info.socket_info[LISTENER_SOCKET] = state;
}

uint8_t gsm_get_dialer_socket_state(void)
{
	return gsm_info.socket_info[DIALER_SOCKET];
}

uint8_t gsm_get_listener_socket_state(void)
{
	return gsm_info.socket_info[LISTENER_SOCKET];
}

void gsm_set_socket_state(uint8_t socket, uint8_t state)
{
	if(socket >= SOCKET_MAX || state >= SOCKET_STATE_MAX){
		return;
	}
	gsm_info.socket_info[socket] = state;
}

uint8_t gsm_get_socket_state(uint8_t socket)
{
	if(socket >= SOCKET_MAX){
		return SOCKET_FAIL;
	}
	return gsm_info.socket_info[socket];
}

void gsm_set_raw_socket_state(uint8_t socket, uint8_t state)
{
	if(socket >= SOCKET_MAX){
		return;
	}

	if(socket < DIALER_SOCKET && state != gsm_info.socket_state_raw[socket])
	{
		gsm_listener_id_t lid = (socket == LISTENER_SOCKET) ? GSM_LISTENER_WEB : GSM_LISTENER_IEC104;

		if(state == '0'){
			GSM_LOG_WRN("[#SS] %s baglantisi kapandi!\r\n", (lid == GSM_LISTENER_WEB) ? "WEB SOCKET" : "IEC104");
			if(gsm_get_socket_state(socket) != SOCKET_CLOSED){
				gsm_listener_set_no_carrier(lid, 1);
			}
			gsm_set_socket_state(socket, SOCKET_CLOSED);
		}
		if(state == '3'){
			GSM_LOG_INF_C(XCOLOR_CYAN, "[#SS] %s okunmayi bekleyen data var!\r\n", (lid == GSM_LISTENER_WEB) ? "WEB SOCKET" : "IEC104 SOCKET");
			gsm_listener_set_rx_available(lid, 1);
		}
		if(state == '5'){
			GSM_LOG_WRN("[#SS] %s baglanti istegi geldi!\r\n", (lid == GSM_LISTENER_WEB) ? "WEB SOCKET" : "IEC104 SOCKET");
			gsm_listener_set_new_conn_req(lid, 1);
		}
	}
	gsm_info.socket_state_raw[socket] = state;

	switch(socket)
	{
	case LISTENER_SOCKET:
		if(state != '0'){
			gsm.listener[GSM_LISTENER_WEB].socket_timer = gsm_get_tick() + 17*1000;
		}
		break;
	case IEC104_LISTENER_SOCKET:
		if(state != '0'){
			gsm.listener[GSM_LISTENER_IEC104].socket_timer = gsm_get_tick() + 15*1000;
		}
		break;
	default:
		break;
	}
}

uint8_t gsm_get_raw_socket_state(uint8_t socket)
{
	if(socket >= SOCKET_MAX){
		return SOCKET_FAIL;
	}
	return gsm_info.socket_state_raw[socket];
}


void gsm_set_busy(void)
{
	//LOG(_GSM_, "GSM BUSY");
	gsm.is_module_busy = 1;
}

void gsm_set_free(void)
{
	//LOG(_GSM_, "GSM FREE");
	gsm.is_module_busy = 0;
	gsm.query_state = 0;
}

/*
 * Modul'e at cmd gonderilmis ve cevap bekleniyorsa true, diger durumlarda false dondurur.
*/
bool gsm_is_busy(void)
{
	return (gsm.is_module_busy != 0U);
}

/* gsm_engine_init() kaldirildi: gsm_process_init_old() tum baslangic degerlerini set eder. */

uint32_t gms_get_status(void)
{
	return gsm.main_state;
}

void gsm_set_main_state(uint8_t state)
{
	gsm.main_state = state;
}

uint8_t gsm_get_main_state(void)
{
	return gsm.main_state;
}

/* --- Listener socket event flags (indexed) --- */

void gsm_listener_set_no_carrier(gsm_listener_id_t id, uint8_t v)
{
	if (id < GSM_LISTENER_COUNT) { gsm.listener[id].no_carrier = v; }
}

uint8_t gsm_listener_get_no_carrier(gsm_listener_id_t id)
{
	return (id < GSM_LISTENER_COUNT) ? gsm.listener[id].no_carrier : 0U;
}

void gsm_listener_set_rx_available(gsm_listener_id_t id, uint8_t v)
{
	if (id < GSM_LISTENER_COUNT) { gsm.listener[id].rx_available = v; }
}

uint8_t gsm_listener_get_rx_available(gsm_listener_id_t id)
{
	return (id < GSM_LISTENER_COUNT) ? gsm.listener[id].rx_available : 0U;
}

void gsm_listener_set_new_conn_req(gsm_listener_id_t id, uint8_t v)
{
	if (id < GSM_LISTENER_COUNT) { gsm.listener[id].new_connection_req = v; }
}

uint8_t gsm_listener_get_new_conn_req(gsm_listener_id_t id)
{
	return (id < GSM_LISTENER_COUNT) ? gsm.listener[id].new_connection_req : 0U;
}

uint8_t gsm_listener_get_state(gsm_listener_id_t id)
{
	return (id < GSM_LISTENER_COUNT) ? gsm.listener[id].state : 0U;
}

void gsm_connect_to_hes(void)
{
	gsm.connect_to_headend = 2;
	if(gsm_get_dialer_socket_state() == SOCKET_FAIL){
		gsm_set_dialer_socket_state(SOCKET_CLOSED);
	}
}

void gsm_close_hes_connection(void)
{
	gsm.connect_to_headend = 1;
}

/*
 * gsm_tx_states_t 0 -> TX free, 1 -> Tx Busy
*/
uint32_t gsm_tx_is_ready(void)
{
	return gsm.tx_flag;
}

uint32_t gsm_get_tx_state(void)
{
	return gsm.tx_flag;
}
//gsm_tx_direction_t
void gsm_set_tx_direction(uint32_t direction)
{
	gsm.tx_direction = direction;
}

uint32_t gsm_get_tx_direction()
{
	return gsm.tx_direction;
}

uint32_t gsm_send_to_socket(const void *buff, uint16_t length, uint32_t socket, bool tx_close_socket_after_tx, bool crypto)
{
	if(gsm.tx_flag || length > 1500 || length == 0){
		return 1;
	}

	memcpy(gsm.tx_buff, buff, length);
	gsm.tx_index     = length;
	gsm.tx_flag      = GSM_TX_FIRED;
	gsm.tx_direction = socket;
	gsm.tx_error = 0;
	gsm.tx_close_socket_after_tx = tx_close_socket_after_tx;
	return 0;
}

uint32_t gsm_get_tick(void)
{
	return bsp_get_tick();
}

void gsm_set_delay(uint32_t d_time)
{
	gsm.delay_flag = 1;
	gsm.delay_timer = gsm_get_tick() + d_time;
}



uint32_t gsm_pin_reset_module(void)
{
	static uint8_t state = 0;
	uint32_t res = 0;
	switch (state) {
		case 0:
			gsm_set_busy();
			LOG(_GSM_, "RESET: -> %d\r\n", gsm.prev_state);
			LOG(_GSM_, "Module Pin Reset...\r\n");
			//gpio_write_pin(GSM_MDL_RESET_PORT, GSM_MDL_RESET_PIN, GPIO_PIN_HIGH);
			gsm_set_delay(200);
			state = 1;
			break;
		case 1:
			//gpio_write_pin(GSM_MDL_RESET_PORT, GSM_MDL_RESET_PIN, GPIO_PIN_LOW);
			LOG(_GSM_, "OK\r\n");
			gsm_set_delay(750);
			gsm_set_free();
			at_engine_clear_buff();
			at_engine_reset();
			res = GSM_OK;
			state = 0;
			break;
 	}
	return res;
}

int32_t gsm_send_data_to_listener_cb(void)
{
	return 0;
}

int32_t gsm_send_data_to_dialer_cb(void)
{
	return 0;
}

int32_t gsm_srcev_dialer_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			if(strstr((char *)rx.buff, "NO CARRIER") != NULL) /* Soketten data gelmis fakat su an soket kapali durumda. */
			{
				at_res = GSM_SOCKET_NO_CARRIER;
				break;
			}
			const char *hash_ptr = strstr((char *)rx.buff, "\r\n#");
			if(hash_ptr == NULL){
				return GSM_ERROR;
			}
			hash_ptr = strstr((hash_ptr + 3), "\r\n#");          //#SRECV 'i atla
			const char *ok_ptr = strstr((char *)rx.buff, "\r\nOK");
 
			if(hash_ptr != NULL && ok_ptr != NULL)
			{
				ptrdiff_t raw_len = (ok_ptr - 2) - (hash_ptr + 2);
				if(raw_len <= 0) { break; }
				uint16_t length = (uint16_t)raw_len;
				uint16_t index  = (uint16_t)(hash_ptr - (char *)rx.buff + 2);

				iec104_on_data(&rx.buff[index], length, NULL);


				//hes_receive_new_dialer_package(&rx.buff[index], length);
				//flash_add_gsm_rx_counter(length);
				gsm_info.rx_counter += length;
				gsm_info_add_rx_bytes(length);
				gsm_socket_touch_activity(DIALER_SOCKET);
			}
			else
			{
				//hes_receive_new_dialer_package((uint8_t *)"ERR", 5);
			}
		}
			break;

		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_ss_dialer_cb(void)
{
	int32_t response = gsm_at_response_ready();
	if(response == GSM_RESPONSE_OK)
	{
		char *ptr = strstr((char *)rx.buff, "#SS: ");
		if(ptr == NULL)
		{
			return GSM_ERROR;
		}
		uint16_t index = (uint16_t)(ptr - (char *)rx.buff) + 7U;
		if(index >= rx.len)
		{
			gsm_set_raw_socket_state(DIALER_SOCKET, '0');
			gsm_socket_set_state(DIALER_SOCKET, '0');
		}
		else if(!ISNUM(rx.buff[index]))
		{
			/* Tekrar sorgu yapilmasi gerek */
			gsm_set_raw_socket_state(DIALER_SOCKET, '0');
			gsm_socket_set_state(DIALER_SOCKET, '0');
		}
		else
		{
			gsm_set_raw_socket_state(DIALER_SOCKET, rx.buff[index]);
			gsm_socket_set_state(DIALER_SOCKET, rx.buff[index]);
		}
	}
	return response;
}

int32_t gsm_sd_dialer_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			at_res = GSM_SOCKET_OPEN;
			break;
		case GSM_NO_CARRIER:
			at_res = GSM_SOCKET_NO_CARRIER;
			break;
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "+CME ERROR: 4") != NULL) /* Soket zaten acik */
			{
				LOG_TRACE(_GSM_, "Dialer Soket zaten acik !");
				at_res = GSM_SOCKET_ERROR_ALREADY_OPEN;
			}
			else if(strstr((char *)rx.buff, "ERROR: 559") != NULL)
			{
				at_res = GSM_SOCKET_ERROR_TIMEOUT;
			}
			else if(strstr((char *)rx.buff, "ERROR: 556") != NULL)
			{
				at_res = GSM_CONTEXT_NOT_OPENED;
			}
			else
			{
				at_res = GSM_SOCKET_ERR;
			}
			break;
		case GSM_TIMEOUT:
			break;
	}
	return at_res;
}


int32_t gsm_srcev_listener_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		/* NO CARRIER may have been extracted by the URC handler before we
		 * see the response buffer.  Check both the buffer and the volatile
		 * flag set by gsm_URC_callback() to avoid missing the event. */
		if(strstr((char *)rx.buff, "NO CARRIER:1") != NULL ||
		   strstr((char *)rx.buff, "NO CARRIER: 1") != NULL ||
		   gsm_listener_get_no_carrier(GSM_LISTENER_WEB))
		{
			at_res = GSM_SOCKET_NO_CARRIER;
			return at_res;
		}

		char *hash_ptr = strstr((char *)rx.buff, "\r\n#");
		if(hash_ptr == NULL){
			return GSM_ERROR;
		}

		hash_ptr = strstr((hash_ptr + 3), "\r\n");          //#SRECV 'i atla

		/* Guard against underflow: rx.len is uint16_t, (rx.len - 4) wraps
		 * to a huge value if rx.len < 4, causing out-of-bounds access. */
		char *ok_ptr = NULL;
		if(rx.len >= 4U)
		{
			ok_ptr = strstr((char *)&rx.buff[rx.len - 4U], "OK\r\n");
		}

		if(hash_ptr != NULL && ok_ptr != NULL)
		{
			ptrdiff_t raw_len = (ok_ptr - 4) - (hash_ptr + 2);
			if(raw_len <= 0) { return at_res; }
			uint16_t length = (uint16_t)raw_len;
			uint16_t index  = (uint16_t)(hash_ptr - (char *)rx.buff + 2);

			//hes_receive_new_listener_package(&rx.buff[index], length); /* Gelen datayi hes processe gonder */
			//flash_add_gsm_rx_counter(length);

			gsm_http_server_client_data_received(&rx.buff[index], length);

			gsm_info.rx_counter += length;
			gsm_info_add_rx_bytes(length);
			gsm_info.ls_rx_counter_last += length;
			gsm_socket_touch_activity(LISTENER_SOCKET);
		}
		else
		{
			//hes_receive_new_listener_package((uint8_t *)"ERR", 5);
		}
	}
	return at_res;
}

int32_t gsm_srcev_iec104_listener_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		/* NO CARRIER may have been extracted by the URC handler before we
		 * see the response buffer.  Check both the buffer and the volatile
		 * flag set by gsm_URC_callback() to avoid missing the event. */
		if(strstr((char *)rx.buff, "NO CARRIER:3") != NULL ||
		   strstr((char *)rx.buff, "NO CARRIER: 3") != NULL ||
		   gsm_listener_get_no_carrier(GSM_LISTENER_IEC104))
		{
			at_res = GSM_SOCKET_NO_CARRIER;
			return at_res;
		}

		char *hash_ptr = strstr((char *)rx.buff, "\r\n#");
		if(hash_ptr == NULL){
			return GSM_ERROR;
		}
		hash_ptr = strstr((hash_ptr + 3), "\r\n");          //#SRECV 'i atla

		/* Guard against underflow: rx.len is uint16_t, (rx.len - 4) wraps
		 * to a huge value if rx.len < 4, causing out-of-bounds access. */
		char *ok_ptr = NULL;
		if(rx.len >= 4U)
		{
			ok_ptr = strstr((char *)&rx.buff[rx.len - 4U], "OK\r\n");
		}

		if(hash_ptr != NULL && ok_ptr != NULL)
		{
			ptrdiff_t raw_len = (ok_ptr - 4) - (hash_ptr + 2);
			if(raw_len <= 0) { return at_res; }
			uint16_t length = (uint16_t)raw_len;
			uint16_t index  = (uint16_t)(hash_ptr - (char *)rx.buff + 2);

			//hes_receive_new_listener_package(&rx.buff[index], length); /* Gelen datayi hes processe gonder */
			//flash_add_gsm_rx_counter(length);

			iec104_on_data(&rx.buff[index], length, NULL);

			gsm_info.rx_counter += length;
			gsm_info_add_rx_bytes(length);
			gsm_info.iec104_ls_rx_counter_last += length;
			gsm_socket_touch_activity(IEC104_LISTENER_SOCKET);
		}
		else
		{
			//hes_receive_new_listener_package((uint8_t *)"ERR", 5);
		}
	}
	return at_res;
}

int32_t gsm_ss_listener_cb(void)
{
	/*
	* * 0-> Socket kapali
	* * 1-> Socket acik, ...
	* * 2-> Socket beklemede (suspend), sokete bagli cihaz var !
	* * 3-> Socket beklemede ve bufferda okunmayi bekleyen data var
	* * 4-> Listening
	* * 5-> Socket baglantinin onaylanmsi bekleniyor, istek var
	* * 6-> Socket acik, ...
	* * 7-> Baglanti kuruluyor
	* *  Bu durumda 0 haricindekiler de sorun yok. 0 ise tektar socket acilmali.
	*/
	int32_t response = gsm_at_response_ready();
	if(response == GSM_RESPONSE_OK) 				/* \r\nSRECV: 1,25\r\n\r\n...data 25 byte....\r\n*/
	{                                               /* "\r\n#SS: 1,2,188.59.76.58,5000,85.98.16.1,56609\r\n\r\nOK\r\n" */
		char *ptr = strstr((char *)rx.buff, "#SS: ");
		if(ptr == NULL)
		{
			response = GSM_ERROR;
			return response;
		}
		uint16_t index = (uint16_t)(ptr - (char *)rx.buff) + 7U;
		if(index >= rx.len)
		{
			response = GSM_ERROR;
			return response;
		}

		if(!ISNUM(rx.buff[index]))
		{
			response = GSM_ERROR;
			return response;
		}

		gsm_set_raw_socket_state(LISTENER_SOCKET, rx.buff[index]);
		gsm_socket_set_state(LISTENER_SOCKET, rx.buff[index]);

		if(!gsm_info.web_session.ip.ip) /* Bu baglanti yeni ise log al. */
		{
			uint8_t ip_buff[16] = {0}, ip_index = 0, comma = 0;
			for(uint32_t i = 0; i < rx.len && i < 55U; i++)
			{
				if(rx.buff[i] == ',')
				{
					comma++;
				}
				else if((comma == 4) && (ip_index < sizeof(ip_buff) - 1U))
				{
					ip_buff[ip_index++] = rx.buff[i];
				}
				if(comma == 5)
				{
					ip_buff[ip_index] = '\0';
					break;
				}
			}
			if(comma == 5)
			{
				if(!gsm_info.web_session.ip.ip)
				{
					ipv4_to_int((const char *)ip_buff, &gsm_info.web_session.ip.ip);
					LOG(_GSM_, "Web client IP: %lu.%lu.%lu.%lu",
						gsm_info.web_session.ip.a,
						gsm_info.web_session.ip.b,
						gsm_info.web_session.ip.c,
						gsm_info.web_session.ip.d);
				}
			}
		}
	}
	return response;
}

int32_t gsm_ss_iec104_listener_cb(void)
{
	/*
	* * 0-> Socket kapali
	* * 1-> Socket acik, ...
	* * 2-> Socket beklemede (suspend), sokete bagli cihaz var !
	* * 3-> Socket beklemede ve bufferda okunmayi bekleyen data var
	* * 4-> Listening
	* * 5-> Socket baglantinin onaylanmsi bekleniyor, istek var
	* * 6-> Socket acik, ...
	* * 7-> Baglanti kuruluyor
	* *  Bu durumda 0 haricindekiler de sorun yok. 0 ise tektar socket acilmali.
	*/
	int32_t response = gsm_at_response_ready();
	if(response == GSM_RESPONSE_OK) 				/* \r\nSRECV: 1,25\r\n\r\n...data 25 byte....\r\n*/
	{                                               /* "\r\n#SS: 1,2,188.59.76.58,5000,85.98.16.1,56609\r\n\r\nOK\r\n" */
		char *ptr = strstr((char *)rx.buff, "#SS: ");
		if(ptr == NULL)
		{
			response = GSM_ERROR;
			return response;
		}
		uint16_t index = (uint16_t)(ptr - (char *)rx.buff) + 7U;
		if(index >= rx.len)
		{
			response = GSM_ERROR;
			return response;
		}

		if(!ISNUM(rx.buff[index]))
		{
			response = GSM_ERROR;
			return response;
		}

		gsm_set_raw_socket_state(IEC104_LISTENER_SOCKET, rx.buff[index]);
		gsm_socket_set_state(IEC104_LISTENER_SOCKET, rx.buff[index]);

		if(!gsm_info.iec104_session.ip.ip) /* Bu baglanti yeni ise log al. */
		{
			uint8_t ip_buff[16] = {0}, ip_index = 0, comma = 0;
			for(uint32_t i = 0; i < rx.len && i < 15; i++)
			{
				if(rx.buff[i] == ',')
				{
					comma++;
				}
				else if(comma == 4)
				{
					if(ip_index >= (sizeof(ip_buff) - 1U))
					{
						break; /* IP string too long, stop parsing */
					}
					ip_buff[ip_index++] = rx.buff[i];
				}
				if(comma == 5)
				{
					ip_buff[ip_index] = 0;
					break;
				}
			}
			if(comma == 5)
			{
				if(!gsm_info.iec104_session.ip.ip)
				{
					ipv4_to_int((const char *)ip_buff, &gsm_info.iec104_session.ip.ip);
					LOG(_GSM_, "IEC104 client IP: %lu.%lu.%lu.%lu",
						gsm_info.iec104_session.ip.a,
						gsm_info.iec104_session.ip.b,
						gsm_info.iec104_session.ip.c,
						gsm_info.iec104_session.ip.d);
				}
			}
		}
	}
	return response;
}

int32_t gsm_ss_all_cb(void)
{
	/* Response format:
	 * \r\n#SS: 1,4,188.59.76.58,80\r\n#SS: 2,0\r\n#SS: 3,4,...\r\n...\r\nOK\r\n
	 *
	 * Each line: #SS: <modem_socket>,<state>[,<ip>,<port>,...]\r\n
	 * Modem socket mapping:
	 *   1 -> LISTENER_SOCKET (web)
	 *   2 -> DIALER_SOCKET
	 *   3 -> IEC104_LISTENER_SOCKET
	 *
	 * State: 0=closed, 1=open, 2=suspended, 3=has_data, 4=listening,
	 *        5=incoming, 6=open, 7=connecting
	 */
	int32_t response = gsm_at_response_ready();
	if(response != GSM_RESPONSE_OK)
	{
		return response;
	}

	const char *ptr = strstr((char *)rx.buff, "#SS: ");
	if(ptr == NULL)
	{
		return GSM_ERROR;
	}

	/* Iterate over all #SS: lines */
	while(ptr != NULL)
	{
		ptr += 5U; /* skip "#SS: " */

		/* Parse modem socket id (1 or 2 digits) */
		uint8_t modem_socket = 0;
		if(ISNUM((uint8_t)*ptr))
		{
			modem_socket = (uint8_t)(*ptr - '0');
			ptr++;
			if(ISNUM((uint8_t)*ptr)) /* 2-digit socket id (e.g. "10") */
			{
				modem_socket = modem_socket * 10U + (uint8_t)(*ptr - '0');
				ptr++;
			}
		}
		else
		{
			break; /* unexpected format */
		}

		if(*ptr != ',')
		{
			break; /* unexpected format */
		}
		ptr++; /* skip comma */

		if(!ISNUM((uint8_t)*ptr))
		{
			break; /* state digit expected */
		}
		uint8_t state = (uint8_t)*ptr;

		/* Map modem socket id to application socket type */
		switch(modem_socket)
		{
			case 1U:
				gsm_set_raw_socket_state(LISTENER_SOCKET, state);
				gsm_socket_set_state(LISTENER_SOCKET, state);
				break;
			case 2U:
				gsm_set_raw_socket_state(DIALER_SOCKET, state);
				gsm_socket_set_state(DIALER_SOCKET, state);
				break;
			case 3U:
				gsm_set_raw_socket_state(IEC104_LISTENER_SOCKET, state);
				gsm_socket_set_state(IEC104_LISTENER_SOCKET, state);
				break;
			default:
				/* Other sockets (4-10) are not used by the application */
				break;
		}

		/* Advance to next #SS: line */
		ptr = strstr(ptr, "#SS: ");
	}

	return response;
}

int32_t gsm_sl_listener_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			break;
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "ERROR: 556") != NULL)	/* GPRS aktif degilse sebeke baglantisi kopmus, modul resetlenmis olabilir. */
			{
				at_res = GSM_CONTEXT_NOT_OPENED;
			}
			else if(strstr((char *)rx.buff, "ERROR: 564") != NULL)
			{
				at_res = GSM_SOCKET_ERROR_ALREADY_OPEN;
			}
			break;
		case GSM_TIMEOUT:
			break;
	}
	return at_res;
}

int32_t gsm_srcev_trace_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		if(strstr((char *)rx.buff, "NO CARRIER:3") != NULL || strstr((char *)rx.buff, "NO CARRIER: 3") != NULL) /* Soketten data gelmis fakat su an soket kapali durumda. */
		{
			at_res = GSM_SOCKET_NO_CARRIER;
			return at_res;
		}

		char *hash_ptr = strstr((char *)rx.buff, "\r\n#");
		if(hash_ptr == NULL)
		{
			return at_res;
		}
		hash_ptr = strstr((hash_ptr + 3), "\r\n");          //#SRECV 'i atla

		char *ok_ptr = strstr((char *)&rx.buff[rx.len - 4], "OK\r\n");

		if(hash_ptr != NULL && ok_ptr != NULL)
		{
			ptrdiff_t raw_len = (ok_ptr - 4) - (hash_ptr + 2);
			if(raw_len <= 0) { return at_res; }
			uint16_t length = (uint16_t)raw_len;
			//uint16_t index  = (hash_ptr - (char *)rx.buff + 2);
			//trace_gsm_callback(TR_EVENT_RX_DATA, &rx.buff[index], length); /* Gelen datayi hes processe gonder */
			//flash_add_gsm_rx_counter(length);
			gsm_info.rx_counter += length;
			gsm_info_add_rx_bytes(length);
		}
		else
		{
			//hes_receive_new_listener_package((uint8_t *)"ERR", 5);
		}
	}
	return at_res;
}

int32_t gsm_ss_trace_cb(void)
{
	/*
	* * 0-> Socket kapali
	* * 1-> Socket acik, ...
	* * 2-> Socket beklemede (suspend), sokete bagli cihaz var !
	* * 3-> Socket beklemede ve bufferda okunmayi bekleyen data var
	* * 4-> Listening
	* * 5-> Socket baglantinin onaylanmsi bekleniyor, istek var
	* * 6-> Socket acik, ...
	* * 7-> Baglanti kuruluyor
	* *  Bu durumda 0 haricindekiler de sorun yok. 0 ise tektar socket acilmali.
	*/
	int32_t response = gsm_at_response_ready();
	if(response == GSM_RESPONSE_OK) 				/* \r\nSRECV: 1,25\r\n\r\n...data 25 byte....\r\n*/
	{
		char *ptr = strstr((char *)rx.buff, "#SS: ");
		if(ptr == NULL)
		{
			response = GSM_ERROR;
			return response;
		}
		uint16_t index = (uint16_t)(ptr - (char *)rx.buff) + 7U;
		if(index >= rx.len)
		{
			response = GSM_ERROR;
			return response;
		}

		if(!ISNUM(rx.buff[index]))
		{
			response = GSM_ERROR;
			return response;
		}

		//trace_gsm_callback(TR_EVENT_SOCKET_STATE, &rx.buff[index], 1);

		if(!gsm_info.trace_session.ip.ip) /* Bu baglanti yeni ise log al. */
		{
			uint8_t ip_buff[16] = {0}, ip_index = 0, comma = 0;
			for(uint32_t i = 0; i < rx.len; i++)
			{
				if(rx.buff[i] == ',')
				{
					comma++;
				}
				else if(comma == 4)
				{
					if(ip_index >= (sizeof(ip_buff) - 1U))
					{
						break; /* IP string too long, stop parsing */
					}
					ip_buff[ip_index++] = rx.buff[i];
				}
				if(comma == 5)
				{
					ip_buff[ip_index] = 0;
					break;
				}
			}
			if(comma == 5)
			{
				if(!gsm_info.trace_session.ip.ip)
				{
					ipv4_to_int((const char *)ip_buff, &gsm_info.trace_session.ip.ip);
					LOG(_GSM_, "Trace client IP: %lu.%lu.%lu.%lu",
						gsm_info.trace_session.ip.a,
						gsm_info.trace_session.ip.b,
						gsm_info.trace_session.ip.c,
						gsm_info.trace_session.ip.d);
				}
			}
		}
	}
	return response;
}

int32_t gsm_sl_trace_cb(void)
{
	return gsm_sl_listener_cb();
}

int32_t gsm_listener_socket_data(void)
{
	return 0;
}

int32_t gsm_dialer_socket_data(void)
{
	return 0;
}

int32_t gsm_ntp_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			const char *sep = strstr((char *)rx.buff, ": ");
			int32_t index = 0;
			if(sep != NULL){
				index = (int32_t)(sep - (char *)rx.buff);
			}
			if(index)
			{
				date_time_t dt = {0};
				index += 2;
				const uint8_t *ptr = rx.buff + index;
				uint8_t res = 0;
				if(str_to_int(ptr, 2, &dt.year, 2, 0))
				{
					ptr += 3;
					if(str_to_int(ptr, 2, &dt.month, 2, 0))
					{
						ptr += 3;
						if(str_to_int(ptr, 2, &dt.day, 2, 0))
						{
							ptr += 3;
							if(str_to_int(ptr, 2, &dt.hour, 2, 0))
							{
								ptr += 3;
								if(str_to_int(ptr, 2, &dt.minute, 2, 0))
								{
									ptr += 3;
									if(str_to_int(ptr, 2, &dt.second, 2, 0))
									{
										res = 1;
									}
								}
							}
						}
					}
				}

				if(res)
				{
					static const uint8_t days_in_month[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
					int32_t tz       = modem_config_get_timezone();
					int32_t hour_adj = (int32_t)dt.hour + tz;

					if(hour_adj >= 24)
					{
						dt.hour = (uint8_t)(hour_adj - 24);
						dt.day++;

						/* Artik yil: 2000-2099 arasi, year%4==0 yeterli */
						uint8_t max_day = days_in_month[dt.month];
						if(dt.month == 2 && (dt.year % 4 == 0))
							max_day = 29;

						if(dt.day > max_day)
						{
							dt.day = 1;
							dt.month++;
							if(dt.month > 12)
							{
								dt.month = 1;
								dt.year = (dt.year >= 99u) ? 0u : dt.year + 1u;
							}
						}
					}
					else if(hour_adj < 0)
					{
						dt.hour = (uint8_t)(hour_adj + 24);

						if(dt.day > 1)
						{
							dt.day--;
						}
						else
						{
							/* Bir ay geri */
							if(dt.month > 1){
								dt.month--;
							}
							else
							{
								dt.month = 12;
								dt.year = (dt.year > 0u) ? dt.year - 1u : 99u;
							}

							dt.day = days_in_month[dt.month];
							if(dt.month == 2 && (dt.year % 4 == 0))
								dt.day = 29;
						}
					}
					else
					{
						dt.hour = (uint8_t)hour_adj;
					}

					LOG(_GSM_, "NTP: %02d/%02d/%02d %02d:%02d:%02d (tz=%d)",
						dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second, tz);
					bsp_set_rtc(dt.second, dt.minute, dt.hour, dt.day, dt.month, dt.year);

					/* Sync epoch counter so bsp_get_epoch_time() returns
					 * correct seconds-since-2025 from this point on. */
					{
						datetime_t epoch_dt = {0};
						epoch_dt.date.year   = 2000U + (uint16_t)dt.year;
						epoch_dt.date.month  = dt.month;
						epoch_dt.date.day    = dt.day;
						epoch_dt.time.hour   = dt.hour;
						epoch_dt.time.minute = dt.minute;
						epoch_dt.time.second = dt.second;
						bsp_set_epoch_time(dt_conv_to_epoch(&epoch_dt));
					}
				}

			}
		}
			break;
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "ERROR: 563") != NULL) //Serverdan zaman okunamadi
			{
			}
		case GSM_TIMEOUT:
			break;
	}
	return at_res;
}

int32_t gsm_csq_cb(void)
{
	/* \r\n+CSQ: 13,4\r\n\r\nOK\r\n */
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		char *ptr = strstr((char *)rx.buff, "+CSQ: ");
		if(ptr != NULL)
		{
			char buff[4] = {};
			uint8_t level = 99;
			str_substr(ptr, buff, " ", "," ,3);

			if(strlen(buff) == 2 && ISNUM(buff[0]) && ISNUM(buff[1]))
				level = (buff[0] -'0') * 10 + (buff[1] - '0');
			else if(strlen(buff) == 1 && ISNUM(buff[0]))
				level = (buff[0] -'0');

			gsm_set_signal_quality(level);
		}
	}
	return at_res;
}

int32_t gsm_cesq_cb(void)
{
	/*<CR><LF>+CESQ: 99,99,255,255,26,55<CR><LF><CR><LF>OK<CR><LF>*/
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		char *ptr = strstr((char *)rx.buff, "+CESQ: ");
		if(ptr != NULL)
		{
			char buff[4] = {};
			uint8_t level = 99;
			str_substr(ptr, buff, " ", "," ,3);

			if(strlen(buff) == 2 && ISNUM(buff[0]) && ISNUM(buff[1]))
				level = (buff[0] -'0') * 10 + (buff[1] - '0');
			else if(strlen(buff) == 1 && ISNUM(buff[0]))
				level = (buff[0] -'0');

			gsm_set_signal_quality2G(level);

			int32_t index1 = str_index_of_th(ptr, ",", 4);

			if(index1 > 0)
			{
				ptr += index1;
				str_substr(ptr, buff, ",", "," ,3);

				if(strlen(buff) == 2 && ISNUM(buff[0]) && ISNUM(buff[1]))
					level = (buff[0] -'0') * 10 + (buff[1] - '0');
				else if(strlen(buff) == 1 && ISNUM(buff[0]))
					level = (buff[0] -'0');

				gsm_set_signal_quality4G(level);
			}

			LOG_TRACE(_GSM_, "CESQ2G: %d, CESQ4G: %d", gsm_get_signal_quality2G(), gsm_get_signal_quality4G());
		}
	}
	return at_res;
}

int32_t gsm_cgatt_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			break;
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "CME ERROR: 30"))
			{
				LOG_TRACE(_GSM_, "+CME 30, no network service\r\n");
				at_res = GSM_NO_NETWORK_SERVICE;
			}

		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_sgact_set_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			char *rx_ptr = strstr((char *)rx.buff, "#SGACT: ");
			if(rx_ptr != NULL)	 /* Get Local IP addr */
			{
				char ip_str[16];

				str_substr((char *)rx.buff, ip_str, "#SGACT: ", "\r", 15);
				uint32_t ip = gsm_info.ip.ip;
				ipv4_to_int(ip_str, &ip);
				gsm_info.ip.ip = ip;
				gsm_info_set_ip(ip);
			}
			gsm_set_gprs_state(1);
		}
			break;
		case GSM_ERROR:
			break;
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_sgact_get_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			uint8_t state = 0;
			if(strstr((char *)rx.buff, "#SGACT: 1,1") != NULL){
				state = 1; /* GPRS baglantisi kurulu */
			}

			gsm_set_gprs_state(state);
		}
			break;
		case GSM_ERROR:
			gsm_set_gprs_state(0);
			break;
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

/**
 * @brief Parse a "+CUSD: 2" subscriber-number response and persist the
 *        phone number if it changed.
 *
 * Handles the USSD result whether it arrives inline (AT callback) or as
 * an unsolicited result code (URC).  Expected formats of the number:
 *   turkcell -> 05529533502",15
 *   avea     -> *0552 953 35 02",15
 *
 * @param[in] msg  Null-terminated buffer containing the "+CUSD:" line.
 *
 * @return true if a "+CUSD: 2" response with a phone number was parsed.
 */
bool gsm_cusd_parse_phone_number(const char *msg)
{
	if(msg == NULL)
	{
		return false;
	}

	const char *ptr = strstr(msg, "+CUSD: 2"); /* USSD mesaji basarili ise */
	if(ptr == NULL)
	{
		return false;
	}

	ptr = strstr(msg, "05");
	if(ptr == NULL)
	{
		return false;
	}

	phone_number_t new_number = {0};
	phone_number_t prev_number = {0};

	modem_config_get_simcard_phone_number(&prev_number);

	ptr++; /* 0 atlandi */
	for(int i = 0; i < 10; i++)
	{
		if(is_digit(*ptr)){
			new_number.number[i] = *ptr;
		}
		else{
			break;
		}

		ptr++;
		if(*ptr == ' '){ /* whitespace atla, sadece bir tane olabilir */
			ptr++;
		}
	}

	if(memcmp(new_number.number, prev_number.number, 10) != 0) /* Sim kart no degismis mi ? */
	{
		LOG(_GSM_, "Yeni Simn: %s Eski: %s", new_number.number, prev_number.number);
		modem_config_set_simcard_phone_number(&new_number);
		gsm_log_modem_event_with_arg(ELOG_GSM_EVENT_SIMCARD_CHANGED, &new_number, sizeof(new_number));
	}

	return true;
}

int32_t gsm_cusd_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		(void)gsm_cusd_parse_phone_number((const char *)rx.buff);
	}
	else
	{
		char *ptr = strstr((const char *)rx.buff, "+CUSD: 4");
		if(ptr != NULL)
		{
			LOG(_GSM_, "Sebeke mesgul / komut desteklenmiyor!");
			return GSM_RESPONSE_OK;
		}
		ptr = strstr((const char *)rx.buff, "+CUSD: 5");
		if(ptr != NULL)
		{
			LOG(_GSM_, "network time out!");
			return GSM_RESPONSE_OK;
		}
	}
	return at_res;
}


int32_t gsm_scfgext1_set_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "ERROR: 554") != NULL) /* see datasheet: Zaten akif diiyor ??? */
				at_res = GSM_RESPONSE_OK;
			break;
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_scfgext2_set_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "ERROR: 554") != NULL) /* see datasheet: Zaten akif diiyor ??? */
				at_res = GSM_RESPONSE_OK;
			break;
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_scfgext3_set_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "ERROR: 554") != NULL) /* see datasheet: Zaten akif diiyor ??? */
				at_res = GSM_RESPONSE_OK;
			break;
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_creg_get_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			if(strstr((const char *)rx.buff, "+CREG: 0,11") != NULL){
				at_res = GSM_NETWORK_ONLY_EMERGENCY;
				gsm_info_set_creg(GSM_NET_REG_DENIED);
			}else if(strstr((const char *)rx.buff, "+CREG: 0,0") != NULL){
				at_res = GSM_NETWORK_NOT_SEARCHING;
				gsm_info_set_creg(GSM_NET_REG_NOT_REGISTERED);
			}else if(strstr((const char *)rx.buff, "+CREG: 0,1") != NULL){
				at_res = GSM_NETWORK_REGISTERED;
				gsm_info_set_creg(GSM_NET_REG_REGISTERED);
			}else if(strstr((const char *)rx.buff, "+CREG: 0,5") != NULL){
				at_res = GSM_NETWORK_ROAMING;
				gsm_info_set_creg(GSM_NET_REG_ROAMING);
			}else if(strstr((const char *)rx.buff, "+CREG: 0,2") != NULL){
				at_res = GSM_NETWORK_SEARCHING;
				gsm_info_set_creg(GSM_NET_REG_SEARCHING);
			}else if(strstr((const char *)rx.buff, "+CREG: 0,3") != NULL){
				at_res = GSM_NETWORK_DENIED;
				gsm_info_set_creg(GSM_NET_REG_DENIED);
			}else{
				at_res = GSM_NETWORK_UNKNOW;
				gsm_info_set_creg(GSM_NET_REG_UNKNOWN);
			}
			break;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_cgreg_get_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			if(strstr((const char *)rx.buff, "+CGREG: 0,1") != NULL){
				at_res = GSM_GPRS_NETWORK_REGISTERED;
				gsm_info_set_cgreg(GSM_NET_REG_REGISTERED);
			}
			else if(strstr((const char *)rx.buff, "+CGREG: 0,5") != NULL){
				at_res = GSM_GPRS_NETWORK_ROAMING;
				gsm_info_set_cgreg(GSM_NET_REG_ROAMING);
			}
			else if(strstr((const char *)rx.buff, "+CGREG: 0,2") != NULL){
				at_res = GSM_GPRS_NETWORK_SEARCHING;
				gsm_info_set_cgreg(GSM_NET_REG_SEARCHING);
			}
			else if(strstr((const char *)rx.buff, "+CGREG: 0,3") != NULL){
				at_res = GSM_GPRS_NETWORK_DENIED;
				gsm_info_set_cgreg(GSM_NET_REG_DENIED);
			}
			else{
				at_res = GSM_GPRS_NETWORK_UNKNOW;
				gsm_info_set_cgreg(GSM_NET_REG_UNKNOWN);
			}
			break;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_cereg_get_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			if(strstr((const char *)rx.buff, "+CEREG: 0,1") != NULL){
				at_res = GSM_LTE_NETWORK_REGISTERED;
				gsm_info_set_cereg(GSM_NET_REG_REGISTERED);
			}
			else if(strstr((const char *)rx.buff, "+CEREG: 0,5") != NULL){
				at_res = GSM_LTE_NETWORK_ROAMING;
				gsm_info_set_cereg(GSM_NET_REG_ROAMING);
			}
			else if(strstr((const char *)rx.buff, "+CEREG: 0,2") != NULL){
				at_res = GSM_LTE_NETWORK_SEARCHING;
				gsm_info_set_cereg(GSM_NET_REG_SEARCHING);
			}
			else if(strstr((const char *)rx.buff, "+CEREG: 0,3") != NULL){
				at_res = GSM_LTE_NETWORK_DENIED;
				gsm_info_set_cereg(GSM_NET_REG_DENIED);
			}
			else{
				at_res = GSM_LTE_NETWORK_UNKNOW;
				gsm_info_set_cereg(GSM_NET_REG_UNKNOWN);
			}
			break;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

/*
 * Pin no giris.
 */
int32_t gsm_cpin_set_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			if(str_index_of((const char *)rx.buff, "READY") > -1)
				at_res = GSM_RESPONSE_OK;
			else
				if(str_index_of((const char *)rx.buff, "ERROR: 13") > -1)
					at_res = GSM_CPIN_SIM_FAILUER;
			else
			if(str_index_of((const char *)rx.buff, "ERROR: 16") > -1) /*<\r><\n>ERROR<\r><\n> hatali pin  +CME ERROR: 16 */
				at_res = GSM_CPIN_PINNO_ERROR;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

/*
 * Sim kart durum sorgusu.
 */
int32_t gsm_cpin_get_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res)
	{
		case GSM_RESPONSE_OK:
			if(strstr((const char *)rx.buff, "READY") != NULL) /* <\r><\n>+CPIN: READY<\r><\n><\r><\n>OK<\r><\n> */
			{
				at_res = GSM_CPIN_READY;
				gsm_info_set_sim_state(GSM_SIM_STATE_READY);
			}
			else
			if(strstr((const char *)rx.buff, "SIM PIN") != NULL)               /* enter pin number  */
			{
				at_res = GSM_CPIN_SIMPIN;
				gsm_info_set_sim_state(GSM_SIM_STATE_PIN_REQUIRED);
			}
			else
			if(strstr((const char *)rx.buff, "SIM BUSY") != NULL)
			{
				at_res = GSM_CPIN_BUSY;
				gsm_info_set_sim_state(GSM_SIM_STATE_BUSY);
			}
			else
			if(strstr((const char *)rx.buff, "SIM PUK") != NULL)
			{
				at_res = GSM_CPIN_SIMPUK;
				gsm_info_set_sim_state(GSM_SIM_STATE_PUK_REQUIRED);
			}
			else
			if(strstr((const char *)rx.buff, "ERROR") != NULL)
			{
				at_res = GSM_CPIN_ERROR;
				gsm_info_set_sim_state(GSM_SIM_STATE_ERROR);
			}
			else
			{
				at_res = GSM_CPIN_ERROR;
				gsm_info_set_sim_state(GSM_SIM_STATE_ERROR);
			}
			break;
		case GSM_ERROR:
			if(strstr((const char *)rx.buff, "ERROR: 10") != NULL)
			{
				at_res = GSM_CPIN_NO_SIM;
				gsm_info_set_sim_state(GSM_SIM_STATE_NOT_INSERTED);
			}
			if(str_index_of((const char *)rx.buff, "ERROR: 13\r\n") != -1)
			{
				at_res = GSM_CPIN_SIM_FAILUER;
				gsm_info_set_sim_state(GSM_SIM_STATE_FAILURE);
			}
			break;
		case GSM_TIMEOUT:
			break;
		default:
			at_res = -1;
			break;
	}
	return at_res;
}

/*
 * Module verisyon
 */
int32_t gsm_get_module_fw_verison(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			/* \r\n16.01.173\r\n\r\nOK\r\n */
			char *ptr = str_substr((char *)rx.buff, (char *)gsm_info.module_version, "\r\n", "\r\n\r\nOK", 11);
 			if(ptr != NULL)
			{
				gsm_info_set_fw_version((const char *)gsm_info.module_version);
				LOG(_GSM_, "Module Fw Ver: %s", gsm_info_get_fw_version());
			}
		}
		break;
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_default_at_cb(void)
{
	int32_t at_res = 0;
	at_res = gsm_at_response_ready();
	switch (at_res)
	{
		case GSM_RESPONSE_OK:
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

void gsm_engine_trace_socket_set_len(uint16_t len)
{
	gsm.trace_index = len;
}

uint32_t gsm_engine_trace_socket_send(uint8_t socket, const uint8_t *buff, uint16_t len)
{
	uint32_t res = 0;
	if(socket == ATQUERY_SEND_DATA_TO_TRACE_SOCKET)
	{
		at_engine_send_data(buff, len, 2000);
		res = 1;
		gsm.at_callback = gsm_default_at_cb;
		gsm.query_id    = socket;
		gsm.query_state = GSM_WAITING_RESPONSE; /* AT sorgusu modeme gonderildi, simdi cevap bekleniyor */
	}
	return res;
}

uint32_t gsm_engine_socket_send(uint8_t socket)
{
	uint32_t res = 0;
	if((socket == ATQUERY_SEND_DATA_TO_DIALER_SOCKET   && gsm_get_socket_state(DIALER_SOCKET)   == SOCKET_DATA_MODE) ||
	   (socket == ATQUERY_SEND_DATA_TO_LISTENER_SOCKET && gsm_get_socket_state(LISTENER_SOCKET) == SOCKET_DATA_MODE) ||
	   (socket == ATQUERY_SEND_DATA_TO_IEC104_LISTENER_SOCKET && gsm_get_socket_state(IEC104_LISTENER_SOCKET) == SOCKET_DATA_MODE))
	{
		at_engine_send_data(gsm.tx_buff, gsm.tx_index, 20000);
		res = 1;
		gsm.at_callback = gsm_default_at_cb;
		gsm.query_id    = socket;
		gsm.query_state = GSM_WAITING_RESPONSE; /* AT sorgusu modeme gonderildi, simdi cevap bekleniyor */
	}
	return res;
}

int32_t gsm_httprcv_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res)
	{
		case GSM_RESPONSE_OK:
		{
			if(strstr((char *)rx.buff, "NO CARRIER") != NULL) /* Soketten data gelmis fakat su an soket kapali durumda. */
			{
				at_res = GSM_SOCKET_NO_CARRIER;
				return at_res;
			}

			char *hash_ptr = strstr((char *)rx.buff, "<<<");
			char *ok_ptr = strstr((char *)&rx.buff[rx.len - 6], "\r\nOK\r\n");

			if(hash_ptr != NULL && ok_ptr != NULL)
			{
				/* Format-> \r\n<<<........n byte data........\r\nOK\r\n */
				uint16_t length = (ok_ptr - 1 - hash_ptr - 2);
				uint16_t index  = (hash_ptr - (char *)rx.buff + 3);
				//gsm_http_receive_data(&rx.buff[index], length);
				//flash_add_gsm_rx_counter(length);
				gsm_info.rx_counter += length;
				gsm_info_add_rx_bytes(length);
			}
			else
			{
				//gsm_http_receive_data((uint8_t *)"ERR", 5);
			}
			break;
		}
		case GSM_ERROR:
		{
			if(rx.len > 15)
			{
				if(strstr((char *)&rx.buff[rx.len - 15], "+CME ERROR: 562"))
				{
					LOG_TRACE(_GSM_, "+CME 562, GSM_HTTP_CONN_FAIL");
					at_res = GSM_HTTP_CONN_FAIL;
				}
			}
			LOG_TRACE(DEBUG,"Http baglanti kapali/koptu!");
			break;
		}
		case GSM_TIMEOUT:
		{
			if(rx.len > 15)
			{
				if(strstr((char *)&rx.buff[rx.len - 15], "+CME ERROR: 562"))
				{
					LOG_TRACE(_GSM_, "+CME 562, GSM_HTTP_CONN_FAIL");
					at_res = GSM_HTTP_CONN_FAIL;
				}
			}
		}
			break;
	}
	return at_res;
}

int32_t gsm_trace_si_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		if(strstr((char *)rx.buff, "#SI: 3") == NULL)
		{
			at_res = GSM_ERROR;
			return at_res;
		}
		int32_t index1 = str_index_of_th((char *)rx.buff, ",", 4);
		int32_t index2 = str_index_of_th((char *)rx.buff, "\r\n", 2);
		if(index1 > 0 && index2 > 0)
		{
			uint16_t ack_waiting = 0, mul = 1;
			uint16_t len = index2 - index1 - 1;
			index2--;
			while(len--)
			{
				if(is_digit(rx.buff[index2]))
				{
					ack_waiting += (rx.buff[index2--] - 48) * mul;
					mul *= 10;
				}
				else
				{
					break;
				}
			}

			//trace_gsm_callback(TR_EVENT_TCP_ACK_WAITING, &ack_waiting, sizeof(ack_waiting));
			LOG(_GSM_, "Trace TCP ACK: %d", ack_waiting);
		}
		else
		{
			LOG_TRACE(_GSM_, "Trace ERR TCP ACK");
		}
	}
	return at_res;
}

int32_t gsm_listener_si_cb(void)
{
	//<CR><LF>#SI: 1,0,1221,0,0<CR><LF><CR><LF>OK<CR><LF>
	    //"\r\n#SI: 1,0,91,0,0\r\n\r\nOK\r\n"
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		const char *ptr = strstr((char *)rx.buff, "#SI: 1");
		if(ptr == NULL)
		{
			at_res = GSM_ERROR;
			return at_res;
		}

		uint32_t f1 = 0U;
		uint32_t f2 = 0U;
		uint32_t f3 = 0U;
		uint32_t f4 = 0U;

		int32_t index1 = str_index_of_th((char *)rx.buff, ",", 1);
		if(index1 > 0)
		{
			int res = xscanf((const char *)&rx.buff[index1], 64, ",%u32,%u32,%u32,%u32", &f1, &f2, &f3, &f4);
			if(res == 4)
			{
				gsm.listener[GSM_LISTENER_WEB].ack_waiting   = (uint16_t)f4;
				/* SI tum alanlari sifir ise soket kapanmis olabilir (veri gonderimi oncesi kontrol) */
				gsm.listener[GSM_LISTENER_WEB].si_all_zero   = ((f1 == 0U) && (f2 == 0U) && (f3 == 0U) && (f4 == 0U)) ? 1U : 0U;
				LOG(_GSM_, "Listener TCP ACK: %d, DATA: %d, all_zero: %d", gsm.listener[GSM_LISTENER_WEB].ack_waiting, f3, gsm.listener[GSM_LISTENER_WEB].si_all_zero);
				if(f3 > 0U) {
					LOG(_GSM_, "Listener DATA WAITING: %d", f3);
					gsm_listener_set_rx_available(GSM_LISTENER_WEB, 1);
				}

				return at_res;
			}
		}

		return GSM_ERROR;
	}
	return at_res;
}

int32_t gsm_iec104_listener_si_cb(void)
{
	//<CR><LF>#SI: 3,0,1221,0,0<CR><LF><CR><LF>OK<CR><LF>
	    //"\r\n#SI: 3,0,91,0,0\r\n\r\nOK\r\n"
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		const char *ptr = strstr((char *)rx.buff, "#SI: 3");
		if(ptr == NULL)
		{
			at_res = GSM_ERROR;
			return at_res;
		}

		uint32_t f1 = 0U;
		uint32_t f2 = 0U;
		uint32_t f3 = 0U;
		uint32_t f4 = 0U;

		int32_t index1 = str_index_of_th((char *)rx.buff, ",", 1);
		if(index1 > 0)
		{
			int res = xscanf((const char *)&rx.buff[index1], 64, ",%u32,%u32,%u32,%u32", &f1, &f2, &f3, &f4);
			if(res == 4)
			{
				gsm.listener[GSM_LISTENER_IEC104].ack_waiting  = (uint16_t)f4;
				/* SI tum alanlari sifir ise soket kapanmis olabilir (veri gonderimi oncesi kontrol) */
				gsm.listener[GSM_LISTENER_IEC104].si_all_zero  = ((f1 == 0U) && (f2 == 0U) && (f3 == 0U) && (f4 == 0U)) ? 1U : 0U;
				LOG(_GSM_, "IEC104 Listener TCP ACK: %d, DATA: %d, all_zero: %d", gsm.listener[GSM_LISTENER_IEC104].ack_waiting, f3, gsm.listener[GSM_LISTENER_IEC104].si_all_zero);
				if(f3 > 0U) {
					LOG(_GSM_, "IEC104 Listener DATA WAITING: %d", f3);
					gsm_listener_set_rx_available(GSM_LISTENER_IEC104, 1);
				}

				return at_res;
			}
		}

		return GSM_ERROR;
	}
	return at_res;
}

int32_t gsm_listener_all_si_cb(void)
{
	/*
		<\r><\n>
		#SI: 1,0,0,0,0<\r><\n>
		#SI: 2,0,0,0,0<\r><\n>
		#SI: 3,0,0,0,0<\r><\n>
		#SI: 4,0,0,0,0<\r><\n>
		#SI: 5,0,0,0,0<\r><\n>
		#SI: 6,0,0,0,0<\r><\n>
		<\r><\n>
		OK<\r><\n>
    */

	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		bool si_1_found = false;
		bool si_3_found = false;

		const char *ptr = strstr((char *)rx.buff, "#SI: 1");
		if(ptr != NULL)
		{
			uint32_t ack_waiting = 0;
			uint32_t data_waiting = 0;

			int32_t index1 = str_index_of_th((char *)rx.buff, ",", 3);
			if(index1 > 0)
			{
				int res = xscanf((const char *)&rx.buff[index1], 64, ",%u32,%u32", &data_waiting, &ack_waiting);
				if(res == 2)
				{
					gsm.listener[GSM_LISTENER_WEB].ack_waiting = (uint16_t)ack_waiting;
					LOG(_GSM_, "Listener TCP ACK: %d, DATA: %d", gsm.listener[GSM_LISTENER_WEB].ack_waiting, data_waiting);
					if(data_waiting){
						LOG(_GSM_, "Listener DATA WAITING: %d", data_waiting);
						gsm_listener_set_rx_available(GSM_LISTENER_WEB, 1);
					}

					si_1_found = true;
				}
			}
		}

		ptr = strstr((char *)rx.buff, "#SI: 3");
		if(ptr != NULL)
		{
			uint32_t ack_waiting = 0;
			uint32_t data_waiting = 0;

			int32_t index1 = str_index_of_th((char *)rx.buff, ",", 3);
			if(index1 > 0)
			{
				int res = xscanf((const char *)&rx.buff[index1], 64, ",%u32,%u32", &data_waiting, &ack_waiting);
				if(res == 2)
				{
					gsm.listener[GSM_LISTENER_IEC104].ack_waiting = (uint16_t)ack_waiting;
					LOG(_GSM_, "IEC104 Listener TCP ACK: %d, DATA: %d", gsm.listener[GSM_LISTENER_IEC104].ack_waiting, data_waiting);
					if(data_waiting){
						LOG(_GSM_, "IEC104 Listener DATA WAITING: %d", data_waiting);
						gsm_listener_set_rx_available(GSM_LISTENER_IEC104, 1);
					}

					si_3_found = true;
				}
			}
		}

		return si_1_found && si_3_found ? GSM_RESPONSE_OK : GSM_ERROR;
	}

	return at_res;
}

/**
 * @brief AT#SI komutu ile tum soketlerin bilgisini parse eder.
 *
 * Beklenen cevap formati (her soket icin ayri satir):
 *   #SI: <connId>,<sent>,<received>,<buff_in>,<ack_waiting>[,<cause>]
 *
 * Parse edilen bilgiler gsm.si_info[] dizisine yazilir (index = connId - 1).
 *
 * @return GSM_RESPONSE_OK en az bir soket bilgisi basariyla parse edildi,
 *         GSM_ERROR hicbir soket bilgisi bulunamadi.
 */
static int32_t gsm_si_all_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res != GSM_RESPONSE_OK)
	{
		return at_res;
	}

	const char *ptr = strstr((const char *)rx.buff, "#SI: ");
	if(ptr == NULL)
	{
		return GSM_ERROR;
	}

	/* Onceki bilgileri temizle */
	(void)memset(gsm.si_info, 0, sizeof(gsm.si_info));

	uint8_t parsed_count = 0U;

	while(ptr != NULL)
	{
		ptr += 5U; /* "#SI: " atla */

		/* connId parse et (1-2 basamak) */
		uint8_t conn_id = 0U;
		if(!ISNUM((uint8_t)*ptr))
		{
			break;
		}
		conn_id = (uint8_t)(*ptr - '0');
		ptr++;
		if(ISNUM((uint8_t)*ptr))
		{
			conn_id = (conn_id * 10U) + (uint8_t)(*ptr - '0');
			ptr++;
		}

		/* connId 1-6 araliginda olmali */
		if((conn_id < 1U) || (conn_id > GSM_MODEM_SOCKET_COUNT))
		{
			ptr = strstr(ptr, "#SI: ");
			continue;
		}

		if(*ptr != ',')
		{
			break;
		}

		/* Kalan alanlari xscanf ile parse et: ,<sent>,<received>,<buff_in>,<ack_waiting> */
		uint32_t sent        = 0U;
		uint32_t received    = 0U;
		uint32_t buff_in     = 0U;
		uint32_t ack_waiting = 0U;

		int32_t res = xscanf(ptr, 64, ",%u32,%u32,%u32,%u32",
		                     &sent, &received, &buff_in, &ack_waiting);
		if(res >= 4)
		{
			uint8_t idx = conn_id - 1U;
			gsm.si_info[idx].sent        = sent;
			gsm.si_info[idx].received    = received;
			gsm.si_info[idx].buff_in     = buff_in;
			gsm.si_info[idx].ack_waiting = ack_waiting;
			gsm.si_info[idx].is_valid    = true;
			parsed_count++;

			LOG(_GSM_, "SI[%u] sent:%u rcv:%u buf:%u ack:%u",
			    conn_id, sent, received, buff_in, ack_waiting);

			bool is_all_zero = ((sent == 0U) && (received == 0U) &&
			                    (buff_in == 0U) && (ack_waiting == 0U));

			/* Uygulama soketleri icin mevcut gsm state alanlarini guncelle */
			switch(conn_id)
			{
				case 1U: /* Listener socket */
					gsm.listener[GSM_LISTENER_WEB].ack_waiting = (uint16_t)ack_waiting;
					gsm.listener[GSM_LISTENER_WEB].si_all_zero = is_all_zero ? 1U : 0U;
					if(buff_in > 0U)
					{
						gsm_listener_set_rx_available(GSM_LISTENER_WEB, 1);
					}
					break;
				case 2U: /* Dialer socket */
					gsm.dialer_ack_waiting = (uint16_t)ack_waiting;
					break;
				case 3U: /* IEC104 listener socket */
					gsm.listener[GSM_LISTENER_IEC104].ack_waiting = (uint16_t)ack_waiting;
					gsm.listener[GSM_LISTENER_IEC104].si_all_zero = is_all_zero ? 1U : 0U;
					if(buff_in > 0U)
					{
						gsm_listener_set_rx_available(GSM_LISTENER_IEC104, 1);
					}
					break;
				default:
					/* Soket 4-6: ek islem yok */
					break;
			}
		}

		/* Sonraki #SI: satirina atla */
		ptr = strstr(ptr, "#SI: ");
	}

	return (parsed_count > 0U) ? GSM_RESPONSE_OK : GSM_ERROR;
}

int32_t gsm_dialer_si_cb(void)
{
	//TODO: soket acik ise SI ile periyodik sorgulama yapilip, sokete gelen ve okunmayan veri kontrolu yapilsin.
	/* #SS: 1,4,10.206.32.127,2020 */
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)
	{
		if(strstr((char *)rx.buff, "#SI: 2") == NULL)
		{
			at_res = GSM_ERROR;
			return at_res;
		}
		int32_t index1 = str_index_of_th((char *)rx.buff, ",", 4);
		int32_t index2 = str_index_of_th((char *)rx.buff, "\r\n", 2);
		if(index1 > 0 && index2 > 0)
		{
			uint16_t ack_waiting = 0, mul = 1;
			uint16_t len = index2 - index1 - 1;
			index2--;
			while(len--)
			{
				if(is_digit(rx.buff[index2]))
				{
					ack_waiting += (rx.buff[index2--] - 48) * mul;
					mul *= 10;
				}
				else
				{
					break;
				}
			}
			gsm.dialer_ack_waiting = ack_waiting;
			LOG(_GSM_, "Dialer TCP ACK: %d", gsm.dialer_ack_waiting);
		}
		else
		{
			LOG_TRACE(_GSM_, "Dialer ERR TCP ACK");
		}
	}
	return at_res;
}

int32_t gsm_CCED_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK)   /* <\r><\n>+CCED: 286,01,6B2A,F271,11,50,47,47,47,0,0,0,<\r><\n><\r><\n>OK<\r><\n> */
	{
		/* mcc -> 3 digit, mnc-> 2/3 digit */
		int32_t start_index = str_index_of((char *)rx.buff, "+CCED: ");
		if(start_index != -1)
		{
			int32_t index1 = str_index_of((char *)rx.buff, " ");
			int32_t index2 = str_index_of((char *)rx.buff, ",");
			str_to_int(&rx.buff[index1 + 1], index2 - index1 - 1, &gsm_info.cell_info.mobile_country_code, 1, 0);
			index1 = str_index_of_th((char *)rx.buff, ",", 2);

			if(index1 > 0)
			{
				str_to_int(&rx.buff[index2 + 1], index1 - index2 - 1, &gsm_info.cell_info.mobile_network_code, 1, 0);
				index2 = str_index_of_th((char *)rx.buff, ",", 3);
				if(index1 > 0 && index2 > 0 )
				{
					str_to_int(&rx.buff[index1 + 1], index2 - index1 - 1, &gsm_info.cell_info.location_area_code, 1, 1);
					index1 = str_index_of_th((char *)rx.buff, ",", 4);
					if(index1 > 0)
					{
						str_to_int(&rx.buff[index2 + 1], index1 - index2 - 1, &gsm_info.cell_info.cell_id, 1, 1);
					}
				}
			}
			LOG_TRACE(_GSM_, "MCC: %d MNC: %d LAC: %d CI: %d", gsm_info.cell_info.mobile_country_code, gsm_info.cell_info.mobile_network_code,
					gsm_info.cell_info.location_area_code, gsm_info.cell_info.cell_id);
		}
	}else if(at_res > 0){
		at_res = GSM_RESPONSE_OK;
	}
	return at_res;
}

int32_t gsm_CMGR_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	if(at_res == GSM_RESPONSE_OK) /* <\r><\n>+CMGR: "REC UNREAD","+905365107600","","19/01/21,11:51:31+12"<\r><\n>Cccccccc<\r><\n>OK<\r><\n><\r> */
	{
		if(rx.len == 6 && memcmp(rx.buff, "\r\nOK\r\n", 6) == 0)
		{
			at_res = GSM_SMS_NO_SMS;
		}
		else
		{
			int32_t index = str_index_of((char *)rx.buff, "REC UNREAD");
			if(index > 0)
			{
				int32_t index1 = str_index_of_th((char *)rx.buff, "\r\n", 2);
				int32_t index2 = str_index_of_th((char *)rx.buff, "\r\n", 3);

				if(index1 > 0 && index2 > 0 && (index2 - index1 >= 2))
				{
					uint8_t phone_num[17] = {};
					str_substr((char *)&rx.buff[index + 11], (char *)phone_num, ",\"", "\",", 16);
					//sms_receive_sms(&rx.buff[index1 + 2], index2 - index1 - 2, phone_num);
				}

				at_res = GSM_SMS_REC_UNREAD;
			}
			else
			{
				at_res = GSM_SMS_REC_READ; /* REC UNREAD haricindekiler burada degerlendiriliyor */
			}
		}
	}

	return at_res;
}

int32_t gsm_COPS_search_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			int32_t index1 = str_index_of((char *)rx.buff, "REC UNREAD");
			if(index1 > -1)
			{
				LOG(DEBUG, "OK");
			}
		}

		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_COPS_state_cb(void)
{
	/* +COPS: 0,0,"TURKCELL",0 */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			int32_t index = str_index_of((char *)rx.buff, "+COPS: ");
			if(index > -1)
			{
				uint32_t state = rx.buff[index + 7];
				switch(state)
				{
				case '0':
					at_res = GSM_COPS_AUTOMOATIC_MODE;
					break;
				case '1':
					at_res = GSM_COPS_MANUEL_UNLOCKED_MODE;
					break;
				case '2':
					at_res = GSM_COPS_DEREGISTER_MODE;
					break;
				case '3':
					at_res = GSM_COPS_3_MODE;
					break;
				case '4':
					at_res = GSM_COPS_MANUEL_AUTOMATIC_MODE;
					break;
				case '5':
					at_res = GSM_COPS_MANUEL_LOCKED_MODE;
					break;
				default:
					at_res = GSM_COPS_AUTOMOATIC_MODE;
				}
			}

			index = str_index_of_th((char *)rx.buff, ",", 3); /* AcT parametresi 0->2G, 2->3G */
			if(index > -1)
			{
				uint32_t state = rx.buff[index + 1] - 48;

				const char *access_tech_str = "";

				if(state == 0){
					access_tech_str = "2G";
					state = GSM_2G;
				}
				else if(state == 2){
					access_tech_str = "3G";
					state = GSM_3G;
				}
				else if(state == 7){
					access_tech_str = "4G";
					state = GSM_4G;
				}
				else{
					access_tech_str = "Unknow";
				}

				LOG_TRACE(_GSM_, "Access Technology: %s", access_tech_str);

				gsm_set_access_technology(state);
				//led_set_mode(LED_GSM, gsm_get_access_technology());
			}
		}

		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CIMI_cb(void)
{
	/* \r\n286015326824554\r\n\r\nOK\r\n */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			/*
			 * Mobile Country Code (MCC): 3 hanedir.
			 * Mobile Network Code (MNC): Genellikle 2 hanedir fakat 3 hanede olabilir. 3 hane durumu MCC'den anlasilabilir ?? (TR de cogu yerde 2 hane)
			 *
			 */
			char imsi[17] = {}; /* mcc + mnc + msin = 15 hane */
			char *ptr = str_substr((char *)rx.buff, imsi, "\r\n", "\r\n\r\n", 16);
			if(ptr > 0)
			{
				imsi[15] = 0;
				gsm_info_set_imsi(imsi);
				LOG(_GSM_, "Sim Kart imsi: %s", imsi);
				imsi[5] = 0;
				memcpy(simcard_mcc_mnc, imsi, 5);

				str_to_int((uint8_t *)imsi, 3, &gsm_info.cell_info.mobile_country_code, 1, 0);
				str_to_int((uint8_t *)(imsi+3), 2, &gsm_info.cell_info.mobile_network_code, 1, 0);

				/* Sync cell info to gsm_info module */
				gsm_info_cell_t cell = {
					.mcc = gsm_info.cell_info.mobile_country_code,
					.mnc = gsm_info.cell_info.mobile_network_code,
					.lac = gsm_info.cell_info.location_area_code,
					.ci  = gsm_info.cell_info.cell_id,
				};
				gsm_info_set_cell(&cell);

				if(str_index_of((char *)rx.buff, "28601") > 0){
					LOG(_GSM_, "Operator: TR TURKCELL");
				} else if(str_index_of((char *)rx.buff, "28602") > 0){
					LOG(_GSM_, "Operator: VODAFONE TR");
				} else if(str_index_of((char *)rx.buff, "28603") > 0){
					LOG(_GSM_, "Operator: AVEA");
				}else{
					LOG(_GSM_, "Operator: Bilinmeyen Simkart !");
				}
			}
		}

		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CGSN_cb(void)
{
	/* \r\n358021081917381\r\n\r\nOK\r\n */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			char imei[17] = {};
			char *ptr = str_substr((char *)rx.buff, imei, "\r\n", "\r\n\r\n", 16);
			if(ptr > 0)
			{
				imei[15] = 0;
				memcpy(gsm_info.imei, imei, 16);
				gsm_info_set_imei(imei);
				modem_config_set_imei(imei);
				LOG(_GSM_, "IMEI: %s", gsm_info_get_imei());
			}
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CGMM_cb(void)
{
	/* \r\nGL865-DUAL-V3.1\r\n\r\nOK\r\n */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			char model[17] = {};
			char *ptr = str_substr((char *)rx.buff, model, "\r\n", "\r\n\r\n", 16);
			if(ptr > 0)
			{
				model[15] = 0;
				LOG(_GSM_, "Module Name: %s", model);

				if(str_index_of((char *)rx.buff, "LE910R1") > -1){
					gsm_info.module_model = MODULE_LE910R1;
					gsm_info_set_module_model(GSM_MODULE_LE910R1);
				}else{
					gsm_info.module_model = MODULE_UNDEFINED;
					gsm_info_set_module_model(GSM_MODULE_UNDEFINED);
				}
			}
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_MONI_cb(void)
{
	/* #MONI: TR TURKCELL BSIC:73 RxQual:0 LAC:6B2A Id:F26C ARFCN:60 PWR:-70dbm TA:0 */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			uint8_t temp[8] = {};
			char *ptr = str_substr((char *)rx.buff, (char *)temp, "LAC:", " ", 6);
			if(ptr > 0)
			{
				str_to_int(temp, str_len((const char *)temp, 6), &gsm_info.cell_info.location_area_code, 1, 1);
				ptr = str_substr(ptr, (char *)temp, "Id:", " ", 6);
				if(ptr > 0)
				{
					str_to_int(temp, str_len((const char *)temp, 6), &gsm_info.cell_info.cell_id, 1, 1);
				}
 			}
			LOG_TRACE(_GSM_, "MCC: %d MNC: %d LAC: %d CI: %d", gsm_info.cell_info.mobile_country_code, gsm_info.cell_info.mobile_network_code,
					gsm_info.cell_info.location_area_code, gsm_info.cell_info.cell_id);
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CEER_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			char temp[16] = {0};
			char *ptr = str_substr((char *)rx.buff, temp, "#CEER:", "\r\n", sizeof(temp)-1);
			if(ptr > 0)
			{
				LOG(_GSM_, "Call End Reason: %s", temp);
			}
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CCLK_cb(void)
{
	/* **+CCLK: "19/05/14,09:41:12+12"****OK** */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			/* CCLK komutu ayarlandiktan sonra periyodik olarak rtc bilgisini gondermekte,
			 * Bu durumda surekli guncellenmesini istemiyoruz. Ayrica serverdan guncelleme yapilir ise gsm'den geleni gormezden geliyoruz ediyoruz. */
			//if(rtc_get_last_update_source() != RTC_UPDATE_SOURCE_NONE)
			{
				//break;
			}
			int32_t index = str_index_of((char *)rx.buff, "\"");
			if(index > 0)
			{
				date_time_t dt = {};
				uint8_t *ptr = (uint8_t *)(rx.buff + index + 1);

				if(str_to_int(ptr, 2, &dt.year, 2, 0))
				{
					ptr += 3;
					if(str_to_int(ptr, 2, &dt.month, 2, 0))
					{
						ptr += 3;
						if(str_to_int(ptr, 2, &dt.day, 2, 0))
						{
							ptr += 3;
							if(str_to_int(ptr, 2, &dt.hour, 2, 0))
							{
								ptr += 3;
								if(str_to_int(ptr, 2, &dt.minute, 2, 0))
								{
									ptr += 3;
									if(str_to_int(ptr, 2, &dt.second, 2, 0))
									{
										bsp_set_rtc(dt.second, dt.minute, dt.hour, dt.day, dt.month, dt.year);

										/* Sync epoch counter */
										datetime_t epoch_dt = {0};
										epoch_dt.date.year   = 2000U + (uint16_t)dt.year;
										epoch_dt.date.month  = dt.month;
										epoch_dt.date.day    = dt.day;
										epoch_dt.time.hour   = dt.hour;
										epoch_dt.time.minute = dt.minute;
										epoch_dt.time.second = dt.second;
										bsp_set_epoch_time(dt_conv_to_epoch(&epoch_dt));

										//if(rtc_set_datatime(&dt))
										{
											//rtc_set_update_source(RTC_UPDATE_SOURCE_GSM);
										}
									}
								}
							}
						}
					}
				}
			}
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_CCID_cb(void)
{
	 /* <\r><\n>+CCID: 8990011400040345442<\r><\n><\r><\n>OK<\r><\n> */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
			char current_ccid[21] = {0};
			char *ptr = str_substr((char *)rx.buff, current_ccid, "+CCID: ", "\r\n", 20);
			if(ptr > 0)
			{
				const char *prev_iccid = gsm_info_get_iccid();
				if((prev_iccid[0] != '\0') && (memcmp(prev_iccid, current_ccid, 19) != 0))
				{
					LOG(_GSM_, "Yeni Sim kart algilandi !");
				}
				gsm_info_set_iccid(current_ccid);
				LOG(_GSM_, "ICCID: %s", gsm_info_get_iccid());
 			}
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_RFSTS_cb(void)
{
	/* #RFSTS: "286 01",60,-65,6B2A,01,5,19,10,2,F26C,"286015326824554","TURKCELL",3,2 */
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
		{
//			char temp[12];
//			char *ptr = str_substr((char *)rx.buff, temp, "\"", "\"", 10);
//			if(ptr > 0)
//			{
//				ptr = str_substr(ptr, temp, "Id:", " ", 5);
//				int32_t index = str_index_of
//				if(ptr > 0)
//				{
//					str_to_int(ptr, str_len(temp, 6), &sram.ci, 1, 1);
//				}
// 			}
//			LOG(_GSM_, "MCC: %d MNC: %d LAC: %d CI: %d", sram.mcc, sram.mnc, sram.lac, sram.ci);
		}
		case GSM_ERROR:
		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

int32_t gsm_cgdcont_cb(void)
{
	int32_t at_res = gsm_at_response_ready();
	switch (at_res) {
		case GSM_RESPONSE_OK:
			break;
		case GSM_ERROR:
			if(strstr((char *)rx.buff, "CME ERROR: 553"))
			{
				LOG_TRACE(_GSM_, "+CME 553, context already activated\r\n");
				at_res = GSM_CONTEXT_ALREADY_ACTIVATED;
			}

		case GSM_TIMEOUT:
			break;
		default:
			break;
	}
	return at_res;
}

uint32_t gsm_engine_send_at_cmd(uint8_t *cmd, uint16_t cmd_len, uint8_t *res, uint8_t res_len, uint8_t try, uint32_t timeout, int32_t  (*res_callback)())
{
	if(at_engine_is_busy() || gsm_is_busy()){
		return 0;
	}

	gsm_set_busy();
	gsm.at_callback = res_callback;
	at_engine_send_at_command(cmd, cmd_len, res, res_len, try, timeout);

	gsm.query_state = GSM_WAITING_RESPONSE; /* AT sorgusu modeme gonderildi, simdi cevap bekleniyor */
	return 1;
}

/*
 *     TODO: timeout 16 bit yapildi, at engine icinde de 16 bit yapilsin ve cozunurluk yukseltilsin. 50 msec e
 * */
uint32_t gsm_engine_send_query(uint8_t query)
{
	if(at_engine_is_busy() || gsm_is_busy()){
		return 0;
	}

	if(query >= ATQUERY_LIST_LEN)
	{
		GSM_LOG_ERR("_GSM_ ATQUERY_LIST_LEN !!!\r\n");
		return 0;
	}

	uint32_t timeout = 2000U;
	ip_addr_t ip = {0};
	const apn_t *apn = NULL;
	const char * char_ptr = NULL;
	const char *char_ptr2 = NULL;
	url_t url = {};
	uint16_t port = 0;
	uint8_t at_buff[128] = {'A', 'T'};
	uint8_t at_len = 2;
	uint8_t try = 3;
	uint8_t const *res = GSM_OK_STR;
	uint8_t res_len    = GSM_OK_STR_LEN;

	char *buff_ptr =  (char *)(at_buff + 2);

    gsm.at_callback = gsm_default_at_cb; /* Geri donus cevabi olarak \r\nOK cevabi beklenir. */
	gsm_set_busy();

	switch (query)
	{
		case ATQUERY_AT:
			try = 10;
			break;
		case ATQUERY_DISABLE_ECHO:
			at_len += xsprintf(buff_ptr, "E0");
			break;
		case ATQUERY_FACTORY_RESET:
			at_len += xsprintf(buff_ptr, "&F1");
			break;
		case ATQUERY_SET_FLOW_CONTROL:
			at_len += xsprintf(buff_ptr, "&K0");
			break;
		case ATQUERY_SET_BAUDRATE:
			at_len += xsprintf(buff_ptr, "+IPR=115200");
			break;
		case ATQUERY_SET_LOW_BAUDRATE:
			at_len += xsprintf(buff_ptr, "+IPR=4800");
			break;
		case ATQUERY_SET_CMEE:
			at_len += xsprintf(buff_ptr, "+CMEE=1");
			break;
		case ATQUERY_SET_SIMDETECT_PIN_CFG:
			at_len += xsprintf(buff_ptr, "#SIMINCFG=6,0");
			break;
		case ATQUERY_SET_SIMDETECT_MODE:
			at_len += xsprintf(buff_ptr, "#SIMDET=2");
			break;
		case ATQUERY_SET_STATUS_LED:
			at_len += xsprintf(buff_ptr, "#SLED=2");
			break;
		case ATQUERY_GET_MODULE_FW_VERSION:
			gsm.at_callback = gsm_get_module_fw_verison;
			at_len += xsprintf(buff_ptr, "#SWPKGV"); //"+CGMR"
			break;
		case ATQUERY_GET_SIMCARD_STATUS:
			gsm.at_callback = gsm_cpin_get_cb;
			at_len += xsprintf(buff_ptr, "+CPIN?");
			break;
		case ATQUERY_SET_PIN_NO:
			gsm.at_callback = gsm_cpin_set_cb;
			//at_len += xsprintf(buff_ptr, "+CPIN=%4d", flash_get_sim_pin_no());
			break;
		case ATQUERY_SET_SERVICE_CLASS:
			at_len += xsprintf(buff_ptr, "+FCLASS=0");
			break;
		case ATQUERY_SET_GPRS_CLASS:
			timeout = 16*1000;
			at_len += xsprintf(buff_ptr, "#MSCLASS=1,1");
			break;
		case ATQUERY_SET_NETWORK_STATE_RES:
			at_len += xsprintf(buff_ptr, "+CREG=2");
			break;
		case ATQUERY_GET_VOICE_SMS_NETWORK_STATE:
			gsm.at_callback = gsm_creg_get_cb;
			at_len += xsprintf(buff_ptr, "+CREG?");
			break;
		case ATQUERY_GET_GPRS_NETWORK_STATE:
			gsm.at_callback = gsm_cgreg_get_cb;
			at_len += xsprintf(buff_ptr, "+CGREG?");
			break;
		case ATQUERY_GET_LTE_NETWORK_STATE:
			gsm.at_callback = gsm_cereg_get_cb;
			at_len += xsprintf(buff_ptr, "+CEREG?");
			break;
		case ATQUERY_GET_PHONE_NUMBER:
			timeout = 13000;
			try = 1;
			gsm.at_callback = gsm_cusd_cb;
		    res     = (const uint8_t *)&"15\r\n";
		    res_len = 4;
			at_len += xsprintf(buff_ptr, "+CUSD=1,*101#");
			break;
		case ATQUERY_SET_APN:
			/* APN yoksa "" gonderilir */
			gsm.at_callback = gsm_cgdcont_cb;
			char_ptr = modem_config_get_sim_apn();
			at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len,  "+CGDCONT=1,\"IP\",\"%s\"", char_ptr);
			break;

		case ATQUERY_SOCKET_1_CONFIG_SCFG:
			at_len += xsprintf(buff_ptr, "#SCFG=1,1,0,60,600,50");
			break;
		case ATQUERY_SOCKET_2_CONFIG_SCFG:
			at_len += xsprintf(buff_ptr, "#SCFG=2,1,0,60,600,50");
			break;
		case ATQUERY_SOCKET_3_CONFIG_SCFG:
			at_len += xsprintf(buff_ptr, "#SCFG=3,1,0,60,600,50");
			break;

		case ATQUERY_SOCKET_1_CONFIG_SCFGEXT:
			gsm.at_callback = gsm_scfgext1_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT=1,1,0,20,0,0");
			break;
		case ATQUERY_SOCKET_2_CONFIG_SCFGEXT:
			gsm.at_callback = gsm_scfgext1_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT=2,1,0,0,0,0");
			break;
		case ATQUERY_SOCKET_3_CONFIG_SCFGEXT:
			gsm.at_callback = gsm_scfgext1_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT=3,1,0,20,0,0");
			break;

		case ATQUERY_SOCKET_1_CONFIG_SCFGEXT2:
			gsm.at_callback = gsm_scfgext2_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT2=1,1,1,0,0,2");
			break;
		case ATQUERY_SOCKET_2_CONFIG_SCFGEXT2:
			gsm.at_callback = gsm_scfgext2_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT2=2,0,0,0,0,2");
			break;
		case ATQUERY_SOCKET_3_CONFIG_SCFGEXT2:
			gsm.at_callback = gsm_scfgext2_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT2=3,0,0,0,0,2");
			break;


		case ATQUERY_SOCKET_1_CONFIG_SCFGEXT3:
			gsm.at_callback = gsm_scfgext3_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT3=1,0,0,0,0,0,150");
			break;
		case ATQUERY_SOCKET_2_CONFIG_SCFGEXT3:
			gsm.at_callback = gsm_scfgext3_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT3=2,0,0,0,0,0,150");
			break;
		case ATQUERY_SOCKET_3_CONFIG_SCFGEXT3:
			gsm.at_callback = gsm_scfgext3_set_cb;
			at_len += xsprintf(buff_ptr, "#SCFGEXT3=3,0,0,0,0,0,150");
			break;			




		case ATQUERY_SET_TCPWINDOW:
			at_len += xsprintf(buff_ptr, "#TCPMAXWIN=0");
			break;
		case ATQUERY_SET_FIREWALL:
			at_len += xsprintf(buff_ptr, "#FRWL=1,\"1.1.1.1\",\"0.0.0.0\"");
			break;
		case ATQUERY_ATTACH_GPRS:
			timeout = 16000UL; /* 10 sec */
			gsm.at_callback = gsm_cgatt_cb;
			at_len += xsprintf(buff_ptr, "+CGATT=1");
			break;
		case ATQUERY_ACTIVATE_GPRS:
			timeout = 160*1000UL;
			gsm.at_callback = gsm_sgact_set_cb;
			at_len += xsprintf(buff_ptr, "#SGACT=1,1");
			char_ptr = modem_config_get_sim_apn_username();
			char_ptr2 = modem_config_get_sim_apn_password();
			if(char_ptr && char_ptr[0] > 0 && char_ptr2[0] > 0)
			{
				buff_ptr += at_len - 2; /* AT cmd zaten eklenmis idi */
				at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len, ",\"%s\",\"%s\"", char_ptr, char_ptr2);
			}
			break;
		case ATQUERY_GET_GPRS_STATUS:
			gsm.at_callback = gsm_sgact_get_cb;
			at_len += xsprintf(buff_ptr, "#SGACT?");
			break;
		case ATQUERY_GET_SIGNAL_QUALITY:
			gsm.at_callback = gsm_csq_cb;
			at_len += xsprintf(buff_ptr, "+CSQ");
			break;
		case ATQUERY_GET_EXT_SIGNAL_QUALITY:
			gsm.at_callback = gsm_cesq_cb;
			at_len += xsprintf(buff_ptr, "+CESQ");
			break;
		case ATQUERY_GET_NTP_DATETIME:
			gsm.at_callback = gsm_ntp_cb;
			timeout = 13*1000UL;
			char_ptr = modem_config_get_ntp_server();
			port = modem_config_get_ntp_port();
			at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len, "#NTP=\"%s\",%u,0,7,0", char_ptr, port);
			break;

		case ATQUERY_GET_ALL_SOCKET_STATUS: /* Listener soket durumunu sorgula. */
			gsm.at_callback = gsm_ss_all_cb;
			at_len += xsprintf(buff_ptr, "#SS");
			break;
		case ATQUERY_GET_ALL_SOCKET_INFO: /* Tum soketlerin #SI bilgisini sorgula */
			gsm.at_callback = gsm_si_all_cb;
			at_len += xsprintf(buff_ptr, "#SI");
			break;

#ifdef REMOTE_TRACE_ON
			/**** TRACE SOCKET ****/
			/**** TRACE SOCKET ****/
		case ATQUERY_OPEN_TRACE_SOCKET:		/* Soketi dinlemeye basla */
			gsm.at_callback = gsm_sl_trace_cb;
			timeout = 10*1000UL;
			at_len += xsprintf(buff_ptr, "#SL=3,1,%d,0", flash_get_trace_port());
			break;
		case ATQUERY_CLOSE_TRACE_SOCKET:     /* Soketi kapat */
			timeout = 3500;
			at_len += xsprintf(buff_ptr, "#SH=3");
			break;
		case ATQUERY_ACCEPT_TRACE_CONN:		  /* Trace sokete gelen baglanti istegini kabul et. */
			at_len += xsprintf(buff_ptr, "#SA=3,1");
			break;
		case ATQUERY_GET_TRACE_SOCKET_STATUS: /* Trace soket durumunu sorgula. */
			gsm.at_callback = gsm_ss_trace_cb;
			at_len += xsprintf(buff_ptr, "#SS=3");
			break;
		case ATQUERY_GET_TRACE_SOCKET_DATA:   /* Trace sokete gelen datayi oku */
			gsm.at_callback = gsm_srcev_trace_cb;
			at_len += xsprintf(buff_ptr, "#SRECV=3,1024");
			break;
		case ATQUERY_SET_TRACE_SOCKET_DATA_MODE: /* Listener soketi data moduna gecir. Sokete data gondermeden once bu cmd gondrilmeli */
			timeout = 2000;
		    res     = GSM_REC_READY;
		    res_len = GSM_REC_READY_LEN;
			at_len  += xsprintf(buff_ptr, "#SSENDEXT=3,%d", gsm.trace_index);
			gsm_info.tx_counter += gsm.trace_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.trace_index);
			flash_add_gsm_tx_counter(gsm.trace_index);
			break;
		case ATQUERY_SEND_DATA_TO_TRACE_SOCKET:   /* Data moduna gecen module soket datasini yolla */
			gsm_info.tx_counter += gsm.trace_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.trace_index);
			flash_add_gsm_tx_counter(gsm.trace_index);
			try = 1;
			timeout = 3500;
			gsm.at_callback = gsm_send_data_to_listener_cb;
			break;
#endif


			/* MULTI LISTENET SOCKET */
		case ATQUERY_GET_IEC104_LISTENER_SOCKET_INFO:
			gsm.at_callback = gsm_iec104_listener_si_cb;
			at_len += xsprintf(buff_ptr, "#SI=3");
			break;
		case ATQUERY_OPEN_IEC104_LISTENER_SOCKET:		/* Soketi dinlemeye basla */
			gsm.at_callback = gsm_sl_listener_cb;
			timeout = 10*1000UL;
			at_len += xsprintf(buff_ptr, "#SL=3,1,%d,0", iec104_config_get_scada_port());
			break;
		case ATQUERY_CLOSE_IEC104_LISTENER_SOCKET:    /* Soketi kapat */
			timeout = 3500;
			at_len += xsprintf(buff_ptr, "#SH=3");
			break;
		case ATQUERY_ACCEPT_IEC104_LISTENER_CONN:		  /* Listener sokete gelen baglanti istegini kabul et. */
			at_len += xsprintf(buff_ptr, "#SA=3,1");
			break;
		case ATQUERY_GET_IEC104_LISTENER_SOCKET_STATUS: /* Listener soket durumunu sorgula. */
			gsm.at_callback = gsm_ss_iec104_listener_cb;
			at_len += xsprintf(buff_ptr, "#SS=3");
			break;
		case ATQUERY_GET_IEC104_LISTENER_SOCKET_DATA:   /* Listener sokete gelen datayi oku */
			gsm.at_callback = gsm_srcev_iec104_listener_cb;
			at_len += xsprintf(buff_ptr, "#SRECV=3,1024");
			break;
		case ATQUERY_SET_IEC104_LISTENER_SOCKET_DATA_MODE: /* Listener soketi data moduna gecir. Sokete data gondermeden once bu cmd gondrilmeli */
			timeout = 2000;
		    res     = GSM_REC_READY;
		    res_len = GSM_REC_READY_LEN;
			at_len  += xsprintf(buff_ptr, "#SSENDEXT=3,%d", gsm.tx_index);
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			gsm_socket_touch_activity(IEC104_LISTENER_SOCKET);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			break;
		case ATQUERY_SEND_DATA_TO_IEC104_LISTENER_SOCKET:   /* Data moduna gecen module soket datasini yolla */
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			try = 1;
			timeout = 20000;
			gsm.at_callback = gsm_send_data_to_listener_cb;
			break;







			/**** TRACE    SOCKET ****/
			/**** LISTENER SOCKET ****/
		case ATQUERY_OPEN_LISTENER_SOCKET:		/* Soketi dinlemeye basla */
			gsm.at_callback = gsm_sl_listener_cb;
			timeout = 10*1000UL;
			at_len += xsprintf(buff_ptr, "#SL=1,1,%d,0", modem_config_get_web_port());
			break;
		case ATQUERY_CLOSE_LISTENER_SOCKET:    /* Soketi kapat */
			timeout = 3500;
			at_len += xsprintf(buff_ptr, "#SH=1");
			break;
		case ATQUERY_ACCEPT_LISTENER_CONN:		  /* Listener sokete gelen baglanti istegini kabul et. */
			at_len += xsprintf(buff_ptr, "#SA=1,1");
			break;
		case ATQUERY_GET_LISTENER_SOCKET_STATUS: /* Listener soket durumunu sorgula. */
			gsm.at_callback = gsm_ss_listener_cb;
			at_len += xsprintf(buff_ptr, "#SS=1");
			break;
		case ATQUERY_GET_LISTENER_SOCKET_DATA:   /* Listener sokete gelen datayi oku */
			gsm.at_callback = gsm_srcev_listener_cb;
			at_len += xsprintf(buff_ptr, "#SRECV=1,1024");
			break;
		case ATQUERY_SET_LISTENER_SOCKET_DATA_MODE: /* Listener soketi data moduna gecir. Sokete data gondermeden once bu cmd gondrilmeli */
			timeout = 2000;
		    res     = GSM_REC_READY;
		    res_len = GSM_REC_READY_LEN;
			at_len  += xsprintf(buff_ptr, "#SSENDEXT=1,%d", gsm.tx_index);
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			gsm_socket_touch_activity(LISTENER_SOCKET);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			break;
		case ATQUERY_SEND_DATA_TO_LISTENER_SOCKET:   /* Data moduna gecen module soket datasini yolla */
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			try = 2;
			timeout = 20000;
			gsm.at_callback = gsm_send_data_to_listener_cb;
			break;
 			/**** LISTENER SOCKET ****/
			/**** DIALER   SOCKET ****/
		case ATQUERY_CONN_TO_SERVER:           /* Bilinen Ip adresi ve porta soket ac. */
			gsm.at_callback = gsm_sd_dialer_cb;
			timeout = 145*1000UL;
			//ip.ip = nvram_get_scada_ip_address();
			//port = nvram_get_scada_port();
			if(ip.ip == 0 || port == 0)
			{
				LOG_TRACE(_GSM_, "SCADA server ip/port hatali !");
				gsm_set_free();
				gsm.query_state = GSM_FREE;
				return 0;
			}
			at_len += xsprintf(buff_ptr, "#SD=2,0,%d,\"%d.%d.%d.%d\",0,1,1", port, ip.a, ip.b, ip.c, ip.d);
			break;
		case ATQUERY_CLOSE_DIALER_SOCKET:
			timeout = 3500;
			at_len += xsprintf(buff_ptr, "#SH=2");
			break;
		case ATQUERY_GET_DIALER_SOCKET_STATUS:
			gsm.at_callback = gsm_ss_dialer_cb;
			at_len += xsprintf(buff_ptr, "#SS=2");
			break;
		case ATQUERY_SET_DIALER_SOCKET_DATA_MODE:
			timeout = 2000;
		    res     = GSM_REC_READY;
		    res_len = GSM_REC_READY_LEN;
			at_len += xsprintf(buff_ptr, "#SSENDEXT=2,%d", gsm.tx_index);
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			gsm_socket_touch_activity(DIALER_SOCKET);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			break;
		case ATQUERY_GET_DIALER_SOCKET_DATA:
			gsm.at_callback = gsm_srcev_dialer_cb;
			at_len += xsprintf(buff_ptr, "#SRECV=2,1024");
			break;
		case ATQUERY_SEND_DATA_TO_DIALER_SOCKET:
			gsm_info.tx_counter += gsm.tx_index;
			gsm_info_add_tx_bytes((uint32_t)gsm.tx_index);
			//flash_add_gsm_tx_counter(gsm.tx_index);
			try = 1;
			timeout = 20000;
			gsm.at_callback = gsm_send_data_to_dialer_cb;
			break;
			/**** DIALER SOCKET ****/
		case ATQUERY_SET_HTTP_CFG:
			//flash_get_fw_url(&url);
			if(url.port > 0){
				at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len, "#HTTPCFG=0,\"%s\",%u,0,\"\",\"\",0,90", (char *)url.domain, url.port);
			}else{
				at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len, "#HTTPCFG=0,\"%s\",80,0,\"\",\"\",0,90", (char *)url.domain);
			}
			break;
		case ATQUERY_SET_HTTP_QRY:
			timeout = 60000;
			//flash_get_fw_url(&url);
			at_len += xsnprintf(buff_ptr,sizeof(at_buff) - at_len, "#HTTPQRY=0,0,\"%s\"", (char *)url.path);
			break;
		case ATWUERY_GET_HTTP_DATA:
			timeout = 1000 * 60 * 4;
			try = 1;
			res = GSM_OK_F_STR;
			res_len = 6;
			gsm.at_callback = gsm_httprcv_cb;
			at_len += xsprintf(buff_ptr, "#HTTPRCV=0,1024");
			break;
		case ATQUERY_GET_TRACE_SOCKET_INFO:
			gsm.at_callback = gsm_trace_si_cb;
			at_len += xsprintf(buff_ptr, "#SI=3");
			break;
		case ATQUERY_GET_LISTENER_SOCKET_INFO:
			gsm.at_callback = gsm_listener_si_cb;
			at_len += xsprintf(buff_ptr, "#SI=1");
			break;
		case ATQUERY_GET_DIALER_SOCKET_INFO:
			gsm.at_callback = gsm_dialer_si_cb;
			at_len += xsprintf(buff_ptr, "#SI=2");
			break;
		case ATQUERY_GET_CCED:
			gsm.at_callback = gsm_CCED_cb;
			at_len += xsprintf(buff_ptr, "+CCED=0");
			break;
		case ATQUERY_CFUN:
			at_len += xsprintf(buff_ptr, "+CFUN=0");
			break;
		case ATQUERY_SYSHALT:
			at_len += xsprintf(buff_ptr, "#SYSHALT");
			break;
		case ATQUERY_ICMP:
			at_len += xsprintf(buff_ptr, "#ICMP=1");
			break;
		case ATQUERY_SHDN:
			at_len += xsprintf(buff_ptr, "#SHDN");
			break;
		case ATQUERY_CMGF:
			at_len += xsprintf(buff_ptr, "+CMGF=1"); /* set text mode */
			break;
		case ATQUERY_CMGR:
			gsm.at_callback = gsm_CMGR_cb;
			at_len += xsprintf(buff_ptr, "+CMGR=1"); /* ilk indexteki sms'i oku */
			break;
		case ATQUERY_CMGD:
			at_len += xsprintf(buff_ptr, "+CMGD=1"); /* ilk indexteki sms'i sil, bu sirada yeni sms gelir ise 1. indexe kayit edilir. */
			break;
		case ATQUERY_COPS_STATE:
			gsm.at_callback = gsm_COPS_state_cb;
			at_len += xsprintf(buff_ptr, "+COPS?");
			break;
		case ATQUERY_COPS_SEARCH:
			timeout = 60000;
			gsm.at_callback = gsm_COPS_search_cb;
			at_len += xsprintf(buff_ptr, "+COPS=?");
			break;
		case ATQUERY_COPS_AUTOREGISTER:
			at_len += xsprintf(buff_ptr, "+COPS=0");
			break;
		case ATQUERY_COPS_DEREGEISTER:
			at_len += xsprintf(buff_ptr, "+COPS=2");
			break;
		case ATQUERY_COPS_MANUEL_REGISTER:
			//at_len += xsprintf(buff_ptr, "+COPS=1,2,\"28601\",2"); /* AT+COPS=1,2,"50501",2  en son parametre 0->2g, 2->3G*/
			at_len += xsprintf(buff_ptr, "+COPS=1,2,\"%s\",0", (char *)simcard_mcc_mnc); /* AT+COPS=1,2,"28601",2  en son parametre 0->2g, 2->3G*/
			break;
		case ATQUERY_WS46:
			at_len += xsprintf(buff_ptr, "+WS46=25"); /*12 -> 2G, 22->3G, 25->3G/2G */
			break;
		case ATQUERY_CIMI:
			gsm.at_callback = gsm_CIMI_cb;
			at_len += xsprintf(buff_ptr, "+CIMI");
			break;
		case ATQUERY_CGSN:
			gsm.at_callback = gsm_CGSN_cb;
			at_len += xsprintf(buff_ptr, "+CGSN");
			break;
		case ATQUERY_CGMM:
			gsm.at_callback = gsm_CGMM_cb;
			at_len += xsprintf(buff_ptr, "+CGMM");
			break;
		case ATQUERY_MONI:
			gsm.at_callback = gsm_MONI_cb;
			at_len += xsprintf(buff_ptr, "#MONI");
			break;
		case ATQUERY_ENHRST:
			timeout = 5000;
			try = 1;
			at_len += xsprintf(buff_ptr, "#ENHRST=1,0");
			break;
		case  ATQUERY_RFSTS:
			/* #RFSTS: "286 01",60,-64,6B2A,01,5,19,10,2,F26C,"286015326824554","TURKCELL",3,2 */
			at_len += xsprintf(buff_ptr, "#RFSTS");
			break;
		case  ATQUERY_CEERNET:
			at_len += xsprintf(buff_ptr, "#CEERNET");
			break;
		case  ATQUERY_CEER:
			gsm.at_callback = gsm_CEER_cb;
			at_len += xsprintf(buff_ptr, "#CEER");
			break;
		case  ATQUERY_CGEREP:
			at_len += xsprintf(buff_ptr, "+CGEREP=2,0");
			break;
		case ATQUERY_SEARCHLIM:
			at_len += xsprintf(buff_ptr, "#SEARCHLIM=100,100");
			break;
		case ATQUERY_FPLMN:
			at_len += xsprintf(buff_ptr, "#FPLMN=3");
			break;
		case ATQUERY_CCLK:
			at_len += xsprintf(buff_ptr, "+CCLK?");
			gsm.at_callback = gsm_CCLK_cb;
			break;
		case ATQUERY_NTIZ:
			at_len += xsprintf(buff_ptr, "#NITZ=15,1");
			break;
		case ATQUERY_CCID:
			at_len += xsprintf(buff_ptr, "+CCID");
			gsm.at_callback = gsm_CCID_cb;
			break;
		case ATQUERY_TELIT_LOG_1_ENABLE:
			at_len += xsprintf(buff_ptr, "+ZLOGSET=16,529");
			break;
		case ATQUERY_TELIT_LOG_2_ENABLE:
			at_len += xsprintf(buff_ptr, "+VLOG=5");
			break;
		default:
			LOG(_GSM_, "At komutu tanimli degil !");
			return 0;
			break;
	}

	buff_ptr[at_len - 2] = '\r'; /* at_len 2 den basliyor, en sona \r ekle */
	at_len++;
	gsm.prev_at_cmd[1] =  gsm.prev_at_cmd[0]; /* 0-> Guncel gonderilen, 1-> bir onceki gonderilen */
	gsm.prev_at_cmd[0] =  query;
	//LOG(_GSM_, " Gonderilen at cmd: %d Onceki at cmd: %d", gsm.prev_at_cmd[0], gsm.prev_at_cmd[1]);

	at_engine_send_at_command(at_buff, at_len, res, res_len, try, timeout);

	gsm.query_id    = query;
	gsm.query_state = GSM_WAITING_RESPONSE; /* AT sorgusu modeme gonderildi, simdi cevap bekleniyor */
	return 1;
}

uint32_t gsm_engine_get_query_res(void)
{
	uint32_t res = 0;
	if(gsm.query_state == GSM_WAITING_RESPONSE)
	{
		res = gsm_at_response_ready();
		if(res)
		{
			gsm.query_state = GSM_RESPONSE_READY; /* at komutuna cevap alinmis ise gsm_engine isi bitmistir. */
			if(gsm.at_callback != NULL)
				res = gsm.at_callback(); /* Cevabi callback parse eder ve gerekli bilgiler alir. Son olarak ust katman bu cevabi degerlendirir. */

			at_engine_clear_buff();
			at_engine_reset();
		}
	}
	else
	{
		LOG_TRACE(_GSM_, "ERR gsm_engine_get_query_res, query_state=%d (old 0xFF)!", gsm.query_state);
		res = 0U; /* query_state uyumsuzlugu durumunda 0 don: caller GET_RES state'inde beklesin, 0xFF donerse caller yanlis state gecisi yapar */
	}

//	if(gsm.is_module_busy && ) //TODO: Module busy timer
//	{
//
//	}

	return res;
}



// SMS Islemleri
uint32_t gsm_set_sms_text_mode(void)
{
	uint32_t at_res = 0;
	static uint8_t state = 0;
	switch (state) {
		case 0:
		{
			if(at_engine_is_busy() || gsm_is_busy())
				break;
			gsm_set_busy();
			at_engine_send_at_command("AT+CMGF=1", 9, GSM_OK_STR, GSM_OK_STR_LEN, 3, 1500);
			state = 1;
		}
			break;
		case 1:
			at_res = gsm_at_response_ready();
			if(at_res != 0)
			{
				at_engine_clear_buff();
			at_engine_reset();
				state  = 0;
			}
			break;
	}
	return at_res;
}

#if 0
uint32_t gsm_delete_all_sms(void)
{
	uint32_t at_res = 0;
	static uint8_t state = 0;
	switch (state) {
		case 0:
		{
			if(at_engine_is_busy() || gsm_is_busy())
				break;
			gsm_set_busy();
			at_engine_send_at_command("AT+CMGD=1,4", 11, GSM_OK_STR, GSM_OK_STR_LEN, 3, 1500);
			state = 1;
		}
			break;
		case 1:
			at_res = gsm_at_response_ready();
			if(at_res != 0)
			{
				at_engine_clear_buff();
			at_engine_reset();
				state  = 0;
			}
			break;
	}
	return at_res;
}

uint32_t gsm_read_sms(void)
{
	uint32_t at_res = 0;
	static uint8_t state = 0;
	switch (state) {
		case 0:
		{
			if(at_engine_is_busy() || gsm_is_busy())
				break;
			gsm_set_busy();
			at_engine_send_at_command("AT+CMGR=1", 9, GSM_OK_STR, GSM_OK_STR_LEN, 3, 1500);
			state = 1;
		}
			break;
		case 1:
			at_res = gsm_at_response_ready();
			if(at_res != 0)
			{
				at_engine_clear_buff();
				at_engine_reset();
				state  = 0;
			}
			break;
	}
	return at_res;
}
#endif
