/*
 * gsm_dialer_process.c
 *
 * Dialer (outgoing) socket state machine.
 * Handles TCP connection to a remote SCADA / headend server,
 * data transfer, socket status monitoring, and reconnection logic.
 */

#include "gsm_dialer_process.h"
#include "gsm_process.h"
#include "gsm_engine.h"
#include "at_engine2.h"
#include "bsp.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 *  External state
 * ------------------------------------------------------------------------- */
extern gsm_t gsm;

/* ---------------------------------------------------------------------------
 *  Private constants
 * ------------------------------------------------------------------------- */
#define DS_SOCKET_CHECK_TIMER  (10U * 1000U)

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */
void gsm_dialer_socket_process(void)
{
	uint32_t res;

	if(!gsm_is_busy())
	{
		if(gsm.dialer_socket_rx_available && gsm.dialer_socket_state == GSM_HES_WAIT_TX_DATA)
		{
			gsm.dialer_socket_rx_available = 0;
			gsm.dialer_socket_state = GSM_HES_GET_SOCKET_DATA;
		}
		if(gsm.dialer_socket_no_carrier)
		{
			gsm.dialer_socket_no_carrier = 0;
			gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
			//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
			gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
		}
	}

	//gsm.join_timer = 0xFFFFFFF;

	if (gsm.join_timer > gsm_get_tick()){ // 0xFFFFFFF
		return;
	}

	switch(gsm.dialer_socket_state)
	{
		case GSM_HES_IDLE:
			if((gsm.connect_to_headend == 2 || (gsm_get_tx_state() == GSM_TX_FIRED && gsm_get_tx_direction() == GSM_TX_DIR_DIALER_SOCKET)) && !at_engine_is_busy() && !gsm_is_busy())
			{
				if(/*nvram_get_scada_port() == 0 || nvram_get_scada_ip_address() == 0*/0)
				{
					LOG(WARNING, "SCADA server ayarlari yapilmamis !");
					gsm_set_tx_state(GSM_TX_READY);
					gsm_set_free();
				}
				else
				{
					gsm.join_counter = 0;
					gsm.dialer_socket_state = GSM_HES_CONNECT_HEADEND;
					LOG_TRACE(_GSM_, "Server'a socket aciliyor...");
				}
			}
			else
			{
				if(gsm_get_socket_state(DIALER_SOCKET) == SOCKET_OPENED || gsm_get_socket_state(DIALER_SOCKET) == SOCKET_DATA_MODE)
				{
					gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
				}
			}
			break;
		case GSM_HES_WAIT_TX_DATA:
			if(gsm_get_socket_state(DIALER_SOCKET) == SOCKET_CLOSED || gsm_get_socket_state(DIALER_SOCKET) == SOCKET_FAIL)
			{
				gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
				gsm.dialer_socket_state = GSM_HES_IDLE;
				break;
			}
			if (gsm_get_tx_state() == GSM_TX_FIRED && gsm_get_tx_direction() == GSM_TX_DIR_DIALER_SOCKET && !gsm_is_busy()) /* Gonderilecek mesaji bekle */
			{
				//gsm_set_busy();
				gsm.dialer_tcp_ack_timer = 0;
				gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET_INFO;
			}
			else if (gsm.connect_to_headend == 1) /* Mesaj beklerken soket kapatirilsa soketi kapat */
			{
				LOG_TRACE(_GSM_, "Dialer socket kapama istegi geldi...");
				gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
			} else if (gsm.dialer_socket_timer < gsm_get_tick())
			{
				gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
			}

			if (strstr((char *) at_engine_get_response(NULL), "NO CARRIER\r\n") != NULL) /* Baglanti sonlandirildi. */
			{
				LOG_TRACE(_GSM_, "DIALER Soket ile baglanti sonlandirildi\r\n");
				//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
				gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
				at_engine_clear_buff();
			}
			break;

		case GSM_HES_CONNECT_HEADEND:
			if(gsm_engine_send_query(ATQUERY_CONN_TO_SERVER)){
				//led_set_mode(LED_DIALER, LED_DIALER_CONNECTING);
				gsm.dialer_socket_state = GSM_HES_GET_RES_CONNECT_HEADEND;
			}
			break;
		case GSM_HES_CHECK_SOCKET_INFO:
			if(gsm_engine_send_query(ATQUERY_GET_DIALER_SOCKET_INFO)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_CHECK_SOCKET_INFO;
			}
			break;
		case GSM_HES_DATA_MODE:
			if(gsm_engine_send_query(ATQUERY_SET_DIALER_SOCKET_DATA_MODE)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_DATA_MODE;
			}
			break;
		case GSM_HES_CHECK_SOCKET:
			if(gsm_engine_send_query(ATQUERY_GET_DIALER_SOCKET_STATUS)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_CHECK_SOCKET;
			}
			break;
		case GSM_HES_SEND_DATA:
			if(gsm_engine_socket_send(ATQUERY_SEND_DATA_TO_DIALER_SOCKET)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_SEND_DATA;
			}
			break;
		case GSM_HES_CLOSE_SOCKET:
			if(gsm_engine_send_query(ATQUERY_CLOSE_DIALER_SOCKET)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_CLOSE_SOCKET;
			}
			break;
		case GSM_HES_GET_SOCKET_DATA:
			if(gsm_engine_send_query(ATQUERY_GET_DIALER_SOCKET_DATA)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_GET_SOCKET_DATA;
			}
			break;


		case GSM_HES_CHECK_NET:
			if(gsm_engine_send_query(ATQUERY_GET_GPRS_STATUS)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_CHECK_NET;
			}
			break;
		case GSM_HES_RECONNECT_TO_NET:
			if(gsm_engine_send_query(ATQUERY_ACTIVATE_GPRS)){
				gsm.dialer_socket_state = GSM_HES_GET_RES_RECONNECT_TO_NET;
			}
			break;

		case GSM_HES_GET_RES_CHECK_NET:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						if(gsm_get_gprs_state())
						{
							gsm.dialer_socket_state = GSM_HES_IDLE;
						}
						else
						{
							LOG(WARNING, "GPRS baglantisi kopmus !");
							gsm_internet_connection_faild_cd();
							gsm.ds_temp_counter = 0;
							gsm.dialer_socket_state = GSM_HES_RECONNECT_TO_NET;
						}
						break;
					case GSM_ERROR:
					case GSM_TIMEOUT:
						LOG_TRACE(_GSM_, "GSM_HES_GET_RES_CHECK_NET reset!");
						gsm_reset_process_old();
						break;
					default:
						break;
				}
				gsm_set_free();
			}
			break;
		case GSM_HES_GET_RES_RECONNECT_TO_NET:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						gsm.dialer_socket_state = GSM_HES_IDLE;
 						break;
					case GSM_ERROR:
						if(gsm.ds_temp_counter++ < 2) /* 3 defa baglanmaya calis */
						{
							gsm.dialer_socket_state = GSM_HES_RECONNECT_TO_NET;
						}
						else
						{
							LOG(WARNING, "GPRS baglantisi tekrar kurulamadi, reset !");
							gsm_reset_process_old();
						}
						break;
					case GSM_TIMEOUT:
						LOG_TRACE(_GSM_, "GSM_HES_GET_RES_RECONNECT_TO_NET timeout reset!");
						gsm_reset_process_old();
						break;
					default:
						break;
				}
				gsm_set_free();
			}
			break;

		case GSM_HES_GET_RES_CONNECT_HEADEND:
			if (gsm.join_counter < 3) {
				res = gsm_engine_get_query_res();
				if(res){
					switch (res) {
						case GSM_SOCKET_OPEN:
							//led_set_mode(LED_DIALER, LED_DIALER_CONNECTED);
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
							gsm.dialer_socket_state = GSM_HES_WAIT_TX_DATA;
							gsm.dialer_socket_timer = gsm_get_tick() + DS_SOCKET_CHECK_TIMER;
							LOG_TRACE(_GSM_, "DIALER Socket acildi...!");
							break;
						case GSM_SOCKET_NO_CARRIER:
							gsm.dialer_socket_state = GSM_HES_IDLE;
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_FAIL);
							gsm.join_timer = gsm_get_tick() + 1000 * 2;
							break;
						case GSM_SOCKET_ERR:
							//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
							gsm.dialer_socket_state = GSM_HES_IDLE;
							LOG_TRACE(_GSM_, "Server'a baglanti saglanamadi. ERROR: x, Timeout !");
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_FAIL);
							gsm.join_timer = gsm_get_tick() + 1000;
							break;
						case GSM_SOCKET_ERROR_ALREADY_OPEN:
							LOG_TRACE(_GSM_, "DIALER soket zaten acik ! ERROR: 4, Timeout !");
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
							gsm.dialer_socket_state = GSM_HES_WAIT_TX_DATA;
							break;
						case GSM_CONTEXT_NOT_OPENED:
							gsm.dialer_socket_state = GSM_HES_CHECK_NET; //GSM_HES_IDLE;
							LOG_TRACE(_GSM_, "GPRS baglantisi yok!\r\n");
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_FAIL);
							//gsm_reset_process_old();
							gsm.join_timer = gsm_get_tick() + 1000;
							break;
						case GSM_SOCKET_ERROR_TIMEOUT:
							//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
							gsm.dialer_socket_state = GSM_HES_IDLE;
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_FAIL);
							LOG_TRACE(_GSM_, "Server'a baglanti saglanamadi. ERROR: 559, Timeout !");
							gsm.join_timer = gsm_get_tick() + 1000;
							break;
						case GSM_ERROR:
						case GSM_TIMEOUT:
							gsm.dialer_socket_state = GSM_HES_IDLE;
							gsm_set_socket_state(DIALER_SOCKET, SOCKET_FAIL);
							gsm.join_timer = gsm_get_tick() + 1000;
							break;
						default:
							break;
					}
				if(res != GSM_SOCKET_OPEN && res != GSM_SOCKET_ERROR_ALREADY_OPEN)
				{
					gsm_set_tx_state(GSM_TX_READY);
				}
				gsm.connect_to_headend = 0;
				gsm_set_free(); //TODO: gsm.dialer_socket_state = GSM_HES_IDLE; yukaridakielr duzenle
				}
			}
			break;

		case GSM_HES_GET_RES_CHECK_SOCKET_INFO:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						if(gsm.dialer_ack_waiting <= 1024){
							gsm.dialer_tcp_ack_timer = 0;
							gsm.dialer_socket_state = GSM_HES_DATA_MODE;
						}else{
							if(gsm.dialer_tcp_ack_timer++ < 40){
							   gsm_set_delay(1500);
							   gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET_INFO;
							}else{
								LOG_TRACE(_GSM_, "Dialer TCp ACK FAIL");
								gsm.dialer_tcp_ack_timer = 0;
								gsm_set_tx_error();
								gsm.dialer_socket_state = GSM_HES_IDLE;
							}
						}
						break;
					case GSM_NO_CARRIER:
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
						gsm_set_tx_error();
						break;
					case GSM_ERROR:
						if(gsm.gsm_network_timer < 10){
							gsm_set_delay(1500);
							gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET_INFO;
						}else{
							gsm_set_delay(50);
							gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						}
						gsm.join_timer = gsm_get_tick() + 1000 * 2;
						break;
					case GSM_TIMEOUT:
						gsm_set_delay(50);
						gsm.join_timer = gsm_get_tick() + 1000 * 2;
						gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						gsm_set_tx_error();
						break;
					default:
						break;
				}
				gsm_set_free();
			}
			break;

		case GSM_HES_GET_RES_DATA_MODE:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_DATA_MODE);
						gsm.dialer_socket_state = GSM_HES_SEND_DATA;
						break;
					case GSM_NO_CARRIER:
						gsm_set_free();
						gsm_set_delay(50);
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
						gsm_set_tx_error();
						break;
					case GSM_TIMEOUT:
					case GSM_ERROR:
						gsm_set_free();
						gsm_set_tx_state(GSM_TX_READY); /* Soket data moduna gecemez ise TX'i serbeset birak */
						gsm_set_delay(50);
						gsm.join_timer = gsm_get_tick() + 1000 * 2;
						gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						gsm_set_tx_error();
						break;
					default:
						break;
				}
			}
			break;
		case GSM_HES_GET_RES_CHECK_SOCKET:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						/*
						 * * 0-> Socket kapali
						 * * 1-> Socket acik, ...
						 * * 2-> Socket beklemede (suspend), sokete bagli cihaz var !
						 * * 3-> Socket beklemede ve bufferda okunmayi bekleyen data var
						 * * 4-> Listening
						 * * 5-> Socket ba?lant?n?n onaylanmsi bekleniyor, istek var
						 * * 6-> Socket acik, ...
						 * * 7-> Baglanti kuruluyor
						 * *  Bu durumda 0 haricindekiler de sorun yok. 0 ise tektar socket acilmali.
						 */
						LOG(_GSM_, "Dialer socket state: %c", gsm_get_raw_socket_state(DIALER_SOCKET));
						gsm.dialer_socket_state = GSM_HES_WAIT_TX_DATA;
						switch (gsm_get_raw_socket_state(DIALER_SOCKET))
						{
							case '0':
								gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
								break;
								//Tekrar soket ac
							case '1':
							case '2':
								gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
								break;
							case '3':
								//Gelen datayi oku
								gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
								gsm.dialer_socket_state = GSM_HES_GET_SOCKET_DATA;
								break;
							case '4':
							case '5':
							case '6':
								gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
								break;
							case '7':
								break;
								default:
									LOG(_GSM_, "Bilinmeyen soket durumu: %c", gsm_get_raw_socket_state(DIALER_SOCKET));
									gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
								break;
						}

						if(gsm_get_raw_socket_state(DIALER_SOCKET) != '3' && gsm.dialer_socket_rx_available){ /* Eger sokette veri yoksa ve veri geldi flagi set ise flasgi sifirla */
							gsm.dialer_socket_rx_available = 0;
						}
						gsm_set_delay(50);
						gsm.dialer_socket_timer = gsm_get_tick() + DS_SOCKET_CHECK_TIMER;
						break;

					case GSM_NO_CARRIER:
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						break;
					case GSM_ERROR:
					case GSM_TIMEOUT:
						LOG_TRACE(_GSM_, "Dialer soket durumu okunamadi !\r\n");
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
						gsm_reset_process_old();
						break;
					default:
						break;
				}
				gsm_set_free();
			}

			if(gsm_get_socket_state(DIALER_SOCKET) == SOCKET_CLOSED){
				//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
			}

			break;
		case GSM_HES_GET_RES_SEND_DATA:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
					case GSM_TIMEOUT:
						gsm_set_delay(50);
						LOG(_GSM_, "Scadaya paket gonderildi. [%d bytes]", gsm.tx_index);
//						if(gsm.tx_index < 512)
//						{
//							gsm.tx_buff[gsm.tx_index] = 0;
//							LOG(_GSM_, "Dialer--> %s", gsm.tx_buff);
//						}

						gsm_set_tx_state(GSM_TX_READY);
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_OPENED);
						gsm.dialer_socket_timer = gsm_get_tick() + DS_SOCKET_CHECK_TIMER;

						if(gsm.tx_close_socket_after_tx){
							gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						}else{
							gsm.dialer_socket_state = GSM_HES_WAIT_TX_DATA;
						}

						break;
					case GSM_NO_CARRIER:
						gsm.join_timer = gsm_get_tick() + 1000;
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						gsm_set_tx_error();
						//hes_set_gsm_error();
						break;
					case GSM_ERROR:
						gsm.join_timer = gsm_get_tick() + 1000;
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET;
						gsm_set_tx_error();
						break;
					case GSM_SOCKET_DISCONNECTED_TIMEOUT:
						gsm.dialer_socket_state = GSM_HES_CLOSE_SOCKET; /* TODO: timeout durumu ele alinmali */
						gsm.join_timer = gsm_get_tick() + 1000;
						gsm_set_tx_state(GSM_TX_READY);
						gsm_set_tx_error();
						break;
					default:
						break;
				}
			gsm_set_delay(50);
			gsm_set_free();
			}
			break;
		case GSM_HES_GET_RES_CLOSE_SOCKET:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
					case GSM_ERROR:
					case GSM_TIMEOUT:
					case GSM_NO_CARRIER:
						//led_set_mode(LED_DIALER, LED_DIALER_IDLE);
						gsm_set_socket_state(DIALER_SOCKET, SOCKET_CLOSED);
						gsm.connect_to_headend = 0;								/* TODO: URC ile soket kapatilirsa ve bu esnada joniden connet emri gelirse burada siliniyor.
																					Buda diger tarfin hatali calimasine neden oluyor*/
						gsm.dialer_socket_no_carrier = 0;
						gsm.dialer_socket_state = GSM_HES_IDLE;
						LOG(_GSM_, "Dialer soket kapandi.\r\n");
						break;
					default:
						break;
				}
				gsm_set_tx_state(GSM_TX_READY);
				gsm_set_free();
			}
			break;
		case GSM_HES_GET_RES_GET_SOCKET_DATA:
			res = gsm_engine_get_query_res();
			if(res){
				switch (res) {
					case GSM_RESPONSE_OK:
						gsm.dialer_socket_timer = gsm_get_tick() + DS_SOCKET_CHECK_TIMER;
						gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						break;
					case GSM_SOCKET_NO_CARRIER:
						gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						break;
					case GSM_ERROR:
					case GSM_TIMEOUT:
						gsm.dialer_socket_state = GSM_HES_CHECK_SOCKET;
						break;
					default:
						break;
				}

			gsm_set_free();
			}
			break;
		default:
			break;
	}
}
