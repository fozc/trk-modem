/*
 * gsm_process.c
 *
 *  Created on: 2 Nis 2018
 *      Author: fozcan
 */

#include "gsm_types.h"
#include <string.h>
#include "gsm_process.h"
#include "gsm_init.h"
#include "gsm_periodical.h"
#include "at_engine2.h"
#include "gsm_engine.h"
#include "utils.h"
#include "bsp.h"
#include "web_server.h"
#include "modem_config.h"
#include "gsm_listener_process.h"
#include "gsm_dialer_process.h"
#include "iec104_application.h"
#include "iec104_process.h"
#include "gsm_log.h"
#include "gsm_wtd.h"

extern gsm_t gsm;

const uint8_t GSM_ERROR_STR[]      = "ERROR\r\n";
const uint8_t GSM_BUSY_STR[]       = "BUSY\r\n";
const uint8_t GSM_NO_CARRIER_STR[] = "NO CARRIER:";
const uint8_t GSM_RING_STR[] 	  = "RING\r\n";
const uint8_t GSM_SRING_STR[] 	  = "SRING: ";
const uint8_t GSM_CRLF_STR[]       = "\r\n";
const uint8_t GSM_XCRLF_STR[]      = "$\r\n";
const uint8_t GSM_CONNET_STR[]     = "CONNECT\r\n";
const uint8_t GSM_CSQ_STR[]        = "+CSQ: ";

void gsm_internet_connection_faild_cd(void);

void gsm_URC_callback(uint8_t *msg, uint16_t len)
{
	if(msg == NULL) { return; }

	char *ptr = strstr((char *)msg, "SRING: 1");
	if(ptr != NULL)
	{
		if (*(ptr + 8) == ',') /* Soketten data geldi*/
		{
			gsm_listener_set_rx_available(GSM_LISTENER_WEB, 1);
			GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: WEB Soketten veri geldi.\r\n");
		}
		else /* Baglanti istegi*/
		{
			gsm_listener_set_new_conn_req(GSM_LISTENER_WEB, 1);
			GSM_LOG_WRN("URC: WEB Soketten baglanti istegi geldi.\r\n");
		}
		return;
	}

	ptr = strstr((char *)msg, "NO CARRIER:1");
	if(ptr == NULL){
		ptr = strstr((char *)msg, "NO CARRIER: 1");
	}
	if(ptr != NULL)
	{
		gsm_listener_set_no_carrier(GSM_LISTENER_WEB, 1);
		//hes_event_listener_socket_close();
		gsm_set_socket_state(LISTENER_SOCKET, SOCKET_CLOSED);
		GSM_LOG_WRN("URC: NO CARRIER: LISTENER Soket ile baglanti sonlandirildi.\r\n");
		return;
	}

	ptr = strstr((char *)msg, "SRING: 2"); /* \r\nSRING: 2\r\n */
	if(ptr != NULL)
	{
		gsm.dialer_socket_rx_available = 1;
		GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: DIALER Soketten veri geldi.\r\n");
		return;
	}

	ptr = strstr((char *)msg, "NO CARRIER:2");
	if(ptr == NULL){
		ptr = strstr((char *)msg, "NO CARRIER: 2");
	}
	if(ptr != NULL)
	{
		GSM_LOG_WRN("URC: NO CARRIER: DIALER Soket ile baglanti sonlandirildi.\r\n");
		gsm.dialer_socket_no_carrier = 1;
		return;
	}
	//"\r\nSRING: 3,6\r\n"
	ptr = strstr((char *)msg, "SRING: 3");
	if(ptr != NULL)
	{
		if (*(ptr + 8) == ',') /* Soketten data geldi*/
		{
			//trace_gsm_callback(TR_EVENT_RX_DATA_AVAILABLE, NULL, 0);
			GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: IEC104 Soketten veri geldi.\r\n");
			gsm_listener_set_rx_available(GSM_LISTENER_IEC104, 1);
		}
		else /* Baglanti istegi*/
		{
			//trace_gsm_callback(TR_EVENT_NEW_CONN_REQ, NULL, 0);
			GSM_LOG_WRN("URC: IEC104 Soketten baglanti istegi geldi.\r\n");
			gsm_listener_set_new_conn_req(GSM_LISTENER_IEC104, 1);
		}
		return;
	}

	ptr = strstr((char *)msg, "NO CARRIER:3");
	if(ptr == NULL){
		ptr = strstr((char *)msg, "NO CARRIER: 3");
	}
	if(ptr != NULL)
	{
		GSM_LOG_WRN("URC: NO CARRIER: IEC104 Soket ile baglanti sonlandirildi.\r\n");
		//trace_gsm_callback(TR_EVENT_NO_CARRIER, NULL, 0);
		gsm_set_socket_state(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
		gsm_listener_set_no_carrier(GSM_LISTENER_IEC104, 1);
		return;
	}

	ptr = strstr((char *)msg, "#SL: ABORTED"); /* Listener soket network tarafindan kapatildi ! */
	if(ptr != NULL)
	{
		GSM_LOG_WRN("URC: SL: ABORTED [%s]\r\n", ptr);
		if(gsm_get_main_state() == GSM_NORMAL_MODE)
		{
			gsm_listener_set_no_carrier(GSM_LISTENER_WEB, 1);
			gsm_listener_set_no_carrier(GSM_LISTENER_IEC104, 1);
			//hes_event_listener_socket_close();

		    if(!gsm.modul_event_state)
		    {
		    	gsm.module_event_timer = gsm_get_tick() + 3000;
		    	gsm.modul_event_state  = GSM_GET_CEERNET;
		    	gsm.module_event_flag  = 1;
		    }
		}
		return;
	}

	ptr = strstr((char *)msg, "#HTTPRING:");
	if(ptr != NULL)
	{
		GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: HTTP data received.\r\n");

		/* Format: #HTTPRING: <prof>,<status_code>,<content_len>\r\n */
		/* Find first comma (after prof) */
		const char *comma1 = strchr(ptr + 10, ',');
		if(comma1 == NULL)
		{
			GSM_LOG_WRN("URC: HTTPRING format error !\r\n");
			return;
		}
		comma1++; /* skip comma, now points to status_code */

		/* Parse 3-digit status code */
		if((comma1[0] < '0') || (comma1[0] > '9') ||
		   (comma1[1] < '0') || (comma1[1] > '9') ||
		   (comma1[2] < '0') || (comma1[2] > '9'))
		{
			GSM_LOG_WRN("URC: HTTPRING format error !\r\n");
			return;
		}
		uint16_t s_code = (uint16_t)((comma1[0] - '0') * 100 +
		                             (comma1[1] - '0') * 10  +
		                             (comma1[2] - '0'));

		/* Find second comma (after status_code), then parse content_len backwards from \r\n */
		uint32_t c_len = 0, mul = 1;

		char *crlf = strstr((char *)(comma1 + 3), "\r\n");
		if(crlf == NULL) { 
			GSM_LOG_WRN("URC: HTTPRING format error !\r\n");
			return; 
		}
		const char *p_digit = crlf - 1;
		const char *lower_bound = comma1 + 3;
		while(p_digit > lower_bound && *p_digit != ',')
		{
			if((*p_digit >= '0') && (*p_digit <= '9'))
			{
				uint32_t digit = (uint32_t)(*p_digit - '0');
				c_len += digit * mul;
				if(mul > 100000000UL) /* max 9 digits (~999 MB), prevent overflow */
				{
					c_len = 0;
					break;
				}
				mul *= 10U;
			}
			else
			{
				c_len = 0;
				break;
			}

			p_digit--;
		}
		GSM_LOG_INF_C(XCOLOR_YELLOW, "Status Code: %d Content Len: %d\r\n", s_code, c_len);
		//gsm_http_status_code_content_len(s_code, c_len);
		return;
	}

	ptr = strstr((char *)msg, "+CGEV:");
	if(ptr != NULL)
	{
		/* Sebeke gprs baglantisini kestiginde veya koptugunda, **+CGEV: NW DETACH** raporu geliyor. */
		GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: - GPRS Event Reporting\r\n");

		ptr = strstr((char *)msg, "DETACH");
		if(ptr != NULL)
		{
			GSM_LOG_WRN("URC: - GPRS Baglantisi koptu !\r\n");
			//log_save_system(LOG_SYSTEM_GPRS_ERROR, 0x10, 0, 0); /* GPRS baglantisi koptu */
			//gsm_internet_connection_faild_cd();
		}

		ptr = strstr((char *)msg, "ME CLASS");
		if(ptr != NULL)
		{
			GSM_LOG_WRN("URC: Sebeke class degisimi istiyor !\r\n");
			//log_save_system(LOG_SYSTEM_GSM_ERROR, LOG_GSM_ERROR_CLASS_ERR, 0, 0);
		}


		//log_write_hw_serial_port((char*)msg, len);
		return;
	}

	ptr = strstr((char *)msg, "#NITZ");
	if(ptr != NULL)
	{
		GSM_LOG_INF_C(XCOLOR_YELLOW, "URC: Tarih Saat\r\n");
		//log_write_hw_serial_port((char*)msg, len);
		return;
	}

	GSM_LOG_INF_C(XCOLOR_YELLOW, "URC not handled [%s]\r\n", msg);
}

/*
 * Internet baglantisi kurulduktan sonra bir defa cagrilir !
 */
void gsm_internet_connection_on_cb(void)
{
	gsm_set_gprs_state(1);
}

/* Internet baglantisi koparsa bu fonk. cagrilir */
void gsm_internet_connection_faild_cd(void)
{
	gsm_reset_web_session_info();
	gsm_set_gprs_state(0);
	gsm_set_socket_state(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
	gsm_set_socket_state(LISTENER_SOCKET, SOCKET_CLOSED);
	gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);

	/* Reset HTTP server when connection closes */
	webserver_on_connection_closed();

	iec104_process_socket_closed_cb();
}

void gsm_reset_process_old(void)
{
	LOG(_GSM_, "Reset GSM Process...");

	/* Clear AT engine and GSM busy flag first — callers may have left the
	 * engine in busy state (e.g. periodical check that timed out).
	 * Without this, gsm_engine_send_query() would refuse all subsequent
	 * commands and the init state machine would be stuck forever. */
	at_engine_clear_buff();
	at_engine_reset();
	gsm_set_free();

	gsm_load_common_init_vector();
	gsm_set_init_state(GSM_RESET_MODULE);
	gsm_set_main_state(GSM_COMMON_INIT_MODE);
	gsm.listener[GSM_LISTENER_WEB].state = GSM_LS_IDLE;
	gsm.dialer_socket_state = 0;
	gsm.tx_error   = 0;
	gsm.tx_flag    = GSM_TX_NOT_AVAILABLE;

	gsm.listener[GSM_LISTENER_WEB].socket_timer = 0;
	gsm.join_timer = 0;
	gsm.disconnect_from_headend = 0;

	gsm.periodical_event_state = 0;
	gsm.periodical_phase       = GSM_INIT_PHASE_SEND;
	gsm.sms_check_timer 	 = gsm_get_tick() + 30 * 1000;
	gsm.signal_quality_timer = gsm_get_tick() + 30 * 1000;
	gsm.simcard_state_timer  = gsm_get_tick() + 30 * 1000;

	gsm_internet_connection_faild_cd();
	//hes_set_gsm_error();
	//gsm_http_downloader_init();
}

void gsm_clear_tx_error(void)
{
	gsm.tx_error = 0;
}

void gsm_set_tx_error(void)
{
	gsm.tx_error = 1;
}

uint32_t gsm_get_tx_error(void)
{
	return gsm.tx_error;
}

uint32_t gsm_is_ready(void)
{
	return gsm_get_main_state() == GSM_NORMAL_MODE;
}

void gsm_set_tx_state(uint8_t state)
{
	gsm.tx_flag = state;
	if(state == GSM_TX_READY)
	{
		gsm.tx_direction = GSM_TX_DIR_IDLE;
	}
	LOG(_GSM_, "New Tx State: %d", state);
}

void gsm_webserver_listener_reset_gprs_check_timer(void)
{
	gsm.gprs_check_timer = bsp_get_tick() + GSM_LS_GPRS_CHECK_TIMER_MS;
}


int32_t gsm_check_internet_connection(void)
{
	static uint8_t state = GSM_CHECK_GPRS_NEWTWORK_STATE;
	int32_t res;
	int32_t network_state = 0;

	switch(state)
	{
		case GSM_CHECK_GPRS_NEWTWORK_STATE:
			if(gsm_engine_send_query(ATQUERY_GET_GPRS_NETWORK_STATE)){
				gsm_set_init_state(GSM_GET_RES_CHECK_GPRS_NEWTWORK_STATE);
			}
			break;
		case GSM_CHECK_LTE_NEWTWORK_STATE:
			if(gsm_engine_send_query(ATQUERY_GET_LTE_NETWORK_STATE)){
				gsm_set_init_state(GSM_GET_RES_CHECK_LTE_NEWTWORK_STATE);
			}
			break;

		case GSM_GET_RES_CHECK_GPRS_NEWTWORK_STATE:
				res = gsm_engine_get_query_res();
				switch (res) {
					case GSM_GPRS_NETWORK_REGISTERED:
					case GSM_GPRS_NETWORK_ROAMING:
						network_state = GSM_GPRS_NETWORK_REGISTERED;
						break;
					case GSM_GPRS_NETWORK_SEARCHING:
					case GSM_GPRS_NETWORK_DENIED:
					case GSM_GPRS_NETWORK_UNKNOW:
					case GSM_TIMEOUT:
						gsm_set_delay(500);
						state  = GSM_GET_RES_CHECK_LTE_NEWTWORK_STATE; /* LTE sorgula */
						break;
				}
				if (res != 0){
					gsm_set_free();
				}
			break;

		case GSM_GET_RES_CHECK_LTE_NEWTWORK_STATE:
				res = gsm_engine_get_query_res();
				switch (res) {
					case GSM_LTE_NETWORK_REGISTERED:
					case GSM_LTE_NETWORK_ROAMING:
						network_state = GSM_LTE_NETWORK_REGISTERED;
						break;
					case GSM_LTE_NETWORK_SEARCHING:
					case GSM_LTE_NETWORK_DENIED:
					case GSM_LTE_NETWORK_UNKNOW:
					case GSM_TIMEOUT:
						gsm_set_delay(500);
						state  = GSM_CHECK_GPRS_NEWTWORK_STATE; /* 2G sorgula */
						break;
				}
				if (res != 0){
					gsm_set_free();
				}
				break;
	}

	if (gsm_get_tick() - gsm.gsm_network_timer > 300 * 1000) {
		gsm_set_init_state(GSM_GET_CEER); /* Sebekeye baglandiktan sonra 3-5 saniye icinde gprs aktif olmali */
		//log_save_system(LOG_SYSTEM_GPRS_ERROR, 0x01, 0, 0); /* GPRS baglantisi sebekede aktif degil */
	}

	return network_state;
}


void gsm_module_event_process(void)
{
	if(/*get_power_state() == POWER_LOW && */!gsm_is_busy() && gsm.module_event_timer > gsm_get_tick())
	{
		return;
	}
	uint32_t res;
	switch(gsm.modul_event_state)
	{
	case GSM_GET_CEERNET:
		if(gsm_engine_send_query(ATQUERY_CEERNET)){
			gsm.module_event_flag = 2;
			gsm.modul_event_state = GSM_GET_RES_GET_CEERNET;
		}
		break;
	case GSM_GET_RES_GET_CEERNET:
		res = gsm_engine_get_query_res();
		if(res)
		{
			gsm.modul_event_state = GSM_GET_CEER;
			gsm.module_event_timer = gsm_get_tick() + 200;
			gsm_set_free();
		}
		break;
	case GSM_GET_CEER:
		if(gsm_engine_send_query(ATQUERY_CEER)){
			gsm.module_event_flag = 2;
			gsm.modul_event_state = GSM_GET_RES_GET_CEER;
		}
		break;
	case GSM_GET_RES_GET_CEER:
		res = gsm_engine_get_query_res();
		if(res)
		{
			gsm.module_event_flag = 0;
			gsm.modul_event_state = 0;
			gsm.module_event_timer = gsm_get_tick() + 200;
			gsm_set_delay(50);
			gsm_set_free();
		}
		break;
	}
}


/**
 * @brief SIM kart cikarilmasi durumunda calisir (init sonrasi).
 *
 * Init sirasinda SIM yoklugu zaten GSM_CHECK_SIMCARD_AND_PIN
 * tarafindan handle edilir. Bu fonksiyon sadece normal mode'dan
 * gecis yapildiginda cagirilir.
 *
 * TODO: Ileride SIM bekleme + recovery eklenebilir.
 */
void gsm_sim_error_mode(void)
{
	GSM_LOG_ERR("SIM error mode - resetting modem.\r\n");
	bsp_system_reset();
}

void gsm_normal_mode(void)
{
	if(gsm.module_event_flag)
	{
		gsm_module_event_process();
		if(gsm.module_event_flag == 2) /* module_event_flag == 2 ise yani  gsm_module_event_process modulu almis ise sadece gsm_module_event_process  calismali ! */
		{
			return;
		}
	}

	gsm_listener_iec104_process();
	gsm_listener_web_process();
	//gsm_dialer_socket_process();

	gsm_periodical_event_process();

	// Burada power saving mode a gecis kosullari kontrol edilecek
//	if(get_power_state() == POWER_LOW)
//	{
//		if((gsm.periodical_event_state == 0 && /* GSM periyodik sorgu yoksa */
//			gsm.listener[GSM_LISTENER_WEB].state == GSM_LS_IDLE  &&    /* listener soket islemleri bitmis ise  */
//			(gsm_get_tx_state() == GSM_TX_DIR_IDLE && gsm.dialer_socket_state == GSM_HES_WAIT_TX_DATA)) /* && !hes_is_alarm_set()*/) // get_is_free ise direk low power ???
//		{
//			uint32_t res;
//			static uint8_t socket_state = 0;
//			switch(socket_state) /* Bildirim gonderildikten sonra tum bufferin gitmesini bekle */
//			{
//			case 0:
//				if(gsm_engine_send_query(ATQUERY_GET_DIALER_SOCKET_INFO)){
//					socket_state = 1;
//				}
//				break;
//			case 1:
//				res = gsm_engine_get_query_res();
//				if(res){
//					switch (res) {
//						case GSM_RESPONSE_OK:
//							if(gsm.dialer_ack_waiting == 0) /* Buffdaki veri iletilmis ise */
//							{
//								LOG(_GSM_, "GSM -> power saving mode");
//								gsm_set_main_state(GSM_POWER_SAVING_MODE);
//								gsm.power_saving_mode_state  = GSM_SYSHLAT;
//							}
//							else
//							{
//								socket_state = 0;
//							}
//							break;
//						case GSM_NO_CARRIER:
//						case GSM_ERROR:
//						case GSM_TIMEOUT:
//							/* ATTENTION: ! Bildirim gonderilememis olabilir ! */
//							LOG(_GSM_, "GSM -> power saving mode");
//							gsm_set_main_state(GSM_POWER_SAVING_MODE);
//							gsm.power_saving_mode_state  = GSM_SYSHLAT;
//							break;
//					}
//					gsm_set_free();
//				}
//				break;
//			}
//		}
//	}
}

void gsm_power_saving_mode(void)
{
	uint32_t res = 0;
	switch(gsm.power_saving_mode_state)
	{
	case GSM_SYSHLAT:
		if(gsm_engine_send_query(ATQUERY_SYSHALT)){
			gsm.power_saving_mode_state = GSM_GET_RES_SYSHALT;
		}
		break;
	case GSM_GET_RES_SYSHALT:
		res = gsm_engine_get_query_res();
		if(res)
		{
			if(res == GSM_RESPONSE_OK)
			{
				LOG(_GSM_, "GSM is turned off !");
				//gsm_set_uart_tx_pin_low_power_mode(1);
				//system_go_low_power();
				gsm.power_saving_mode_state = GSM_WAIT_FOR_EVENT;
			}
			else
			{
				gsm_set_delay(50);
				gsm.power_saving_mode_state = GSM_SYSHLAT; /* Tekrar sorgu gonder */
			}
		}
		break;
	case GSM_WAIT_FOR_EVENT:
		if(gsm_get_tx_state() == GSM_TX_FIRED) /* Enerji geldiginde sistem reset yer. */
		{
			LOG(_GSM_, "Event received.");
			//digital_io_process();                /* Modulu uyandirmadan once flash islemlerini hallet */
			//tamper_process();
			//gsm_set_uart_tx_pin_low_power_mode(0);
			gsm.power_saving_mode_state  = GSM_PIN_RESET;
		}
		break;
	case GSM_PIN_RESET:
		res = gsm_pin_reset_module();
		if(res == GSM_OK){
			gsm_set_signal_quality(99);
			gsm_set_delay(1000);
			gsm.power_saving_mode_state  = GSM_SYSHLAT;
			gsm_load_LE910R1_init_vector();

			gsm_set_main_state(GSM_COMMON_INIT_MODE);
		}
		break;
	}
}

void gsm_power_outage_mode(void)
{
	/*
	 * gsm_free ise baud degistir.
	 * */
	gsm_dialer_socket_process();
	if(gsm_get_tx_state() == GSM_TX_DIR_IDLE /* && !hes_is_alarm_set()*/) /*join state de eklenebilir.*/
	{
		LOG(_GSM_, "Gonderim tamamlandi, low power mode");
		gsm_set_main_state(GSM_POWER_SAVING_MODE);
		gsm.power_saving_mode_state = GSM_SYSHLAT;
	}
}

void gsm_process_init_old(void)
{
	memset(&gsm, 0x00, sizeof(gsm_t));
	gsm_load_common_init_vector();
	gsm_set_main_state(GSM_COMMON_INIT_MODE);
	gsm.module_state = 1;


	gsm.sms_check_timer              = 60U * 1000U;  /* bsp_get_tick bazli: init suresince artacak */
	gsm.signal_quality_timer         = 60U * 1000U;
	gsm.network_type_timer           = 90U * 1000U;
	gsm.gprs_check_timer             = bsp_get_tick() + 60U * 1000U; /* bsp_get_tick bazli */

	/* Stagger IEC104 socket timer to avoid simultaneous AT command contention */
	gsm.listener[GSM_LISTENER_IEC104].socket_timer = GSM_LS_STAGGER_OFFSET_MS;
}

void gsm_process_old(void)
{
	gsm_wtd_check();

	if (gsm.delay_flag)
	{
		if (gsm.delay_timer < gsm_get_tick())
		{
			gsm.delay_flag = 0;
		}
		else
		{
			return;
		}
	}

	switch(gsm_get_main_state())
	{
		case GSM_COMMON_INIT_MODE: /* Ilk(ortak) module init islemleri */
			gsm_init_old();
			break;
		case GSM_MODULE_INIT_MODE: /* Modul modeline ozgu init islemleri */
			gsm_init_old();
			break;
		case GSM_SIM_ERROR_MODE:
			gsm_sim_error_mode();
			break;
		case GSM_NORMAL_MODE:
			gsm_normal_mode();
			break;
		case GSM_POWER_OUTAGE_MODE:  /* Enerji yokken modul islemleri buradan yapilir. */
			gsm_power_outage_mode();
			break;
		case GSM_POWER_SAVING_MODE:  /* Module SYSHALT'a alinir ve event beklenir */
			gsm_power_saving_mode();
			break;
	}
}

