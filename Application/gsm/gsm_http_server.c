/*
 * gsm_http_server.c
 *
 *  Created on: Dec 20, 2025
 *      Author: fatih
 */
#include "gsm_http_server.h"
#include "gsm_engine.h"
#include "web_server.h"
#include "ring_buff.h"
#include "gsm_log.h"
 
#define GSM_HTTP_SERVER_RX_BUFFER_SIZE 2048
#define GSM_HTTP_SERVER_TX_BUFFER_SIZE 1024*16


static uint8_t http_tx_buff[GSM_HTTP_SERVER_TX_BUFFER_SIZE] = {0};
static rbuff_t tx_rb_ctx = {0};


/* RFWU wire magic: 0x52 0x46 0x57 0x55 ("RFWU" LE uint32 = 0x55574652) */
#define GSM_HTTP_SERVER_RFWU_MAGIC_B0 0x52U
#define GSM_HTTP_SERVER_RFWU_MAGIC_B1 0x46U
#define GSM_HTTP_SERVER_RFWU_MAGIC_B2 0x57U
#define GSM_HTTP_SERVER_RFWU_MAGIC_B3 0x55U

typedef struct
{
	uint8_t  rx_buffer[GSM_HTTP_SERVER_RX_BUFFER_SIZE];
	uint16_t rx_length;
	bool     rx_data_received;

	bool     tx_data_ready;
	bool     is_rfwu;  /**< True when the connection carries RFWU firmware-update traffic */

	uint8_t state;
}gsn_http_server_t;

static gsn_http_server_t g_http_server = {0};


enum
{
	GSM_HTTP_SERVER_STATE_IDLE = 0,
	GSM_HTTP_SERVER_STATE_PROCESS_RX_DATA,
	GSM_HTTP_SERVER_SEND_RESPONSE,
	GSM_HTTP_SERVER_MAX
};


void gsm_http_server_client_data_received(const uint8_t* data, uint16_t length)
{
	/* Detect RFWU connection early by checking the 4-byte wire magic.
	 * Once set, is_rfwu suppresses HTTP-layer log output for this session. */
	if ((length >= 4U) &&
		(data[0] == GSM_HTTP_SERVER_RFWU_MAGIC_B0) &&
		(data[1] == GSM_HTTP_SERVER_RFWU_MAGIC_B1) &&
		(data[2] == GSM_HTTP_SERVER_RFWU_MAGIC_B2) &&
		(data[3] == GSM_HTTP_SERVER_RFWU_MAGIC_B3))
	{
		g_http_server.is_rfwu = true;
	}

	if(g_http_server.rx_data_received) {
		if (!g_http_server.is_rfwu) {
			GSM_LOG_WRN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
			GSM_LOG_WRN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
			GSM_LOG_WRN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
		}
    }

	if(length > 0 && length <= sizeof(g_http_server.rx_buffer) - 1)
	{
		memcpy(g_http_server.rx_buffer, data, length);
		g_http_server.rx_length = length;
		g_http_server.rx_data_received = true;

		if (!g_http_server.is_rfwu) {
			GSM_LOG_INF_C(XCOLOR_CYAN, "[GSM HTTP SERVER] Client data received: %d bytes\r\n", length);
		}
	}
	else
	{
		if (!g_http_server.is_rfwu) {
			GSM_LOG_ERR("[GSM HTTP SERVER] Invalid client data length: %d bytes\r\n", length);
		}
	}
}

static int http_server_send(const void *data, uint32_t len)
{
	if(gsm_get_listener_socket_state() == SOCKET_CLOSED){
		if (!g_http_server.is_rfwu) {
			GSM_LOG_ERR("[GSM HTTP SERVER] Listener socket not open, cannot send data!\r\n");
		}
		return -1;
	}

	if(data && rbuff_available_for_write(&tx_rb_ctx) >= len)
	{
		rbuff_write_buff(&tx_rb_ctx, (uint8_t *)data, len);
		g_http_server.tx_data_ready = true;
		return len;
	}
	else
	{
		if (!g_http_server.is_rfwu) {
			GSM_LOG_ERR("[GSM HTTP SERVER] TX buffer overflow, cannot send %d bytes!\r\n", len);
		}
		return -1;
	}
}

void gsm_http_server_reset(void)
{
	if (!g_http_server.is_rfwu) {
		GSM_LOG_WRN("[GSM HTTP SERVER] Reset  gsm_server\r\n");
	}
	g_http_server.rx_length = 0;
	g_http_server.rx_data_received = false;
	g_http_server.tx_data_ready = false;
	g_http_server.is_rfwu = false;
	g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;

	rbuff_clear(&tx_rb_ctx);
}

void gsm_http_server_init(void)
{
	  web_server_io_t wio = {0};
	  wio.send = http_server_send; //(int (*)(const uint8_t*, int))http_server_send;
	  web_server_init(&wio);

	  if(!rbuff_init(&tx_rb_ctx, http_tx_buff, GSM_HTTP_SERVER_TX_BUFFER_SIZE)){
		  GSM_LOG_ERR("[GSM HTTP SERVER] Failed to initialize TX ring buffer!\r\n");
	  }
}


void gsm_http_server_send_response(void)
{
	if(gsm_tx_is_ready() != 0){
		return;
	}

	if(gsm_get_listener_socket_state() == SOCKET_CLOSED)
	{
		if (!g_http_server.is_rfwu) {
			GSM_LOG_ERR("[GSM HTTP SERVER] Listener socket not open, cannot send response!\r\n");
		}
		g_http_server.tx_data_ready = false;
		rbuff_clear(&tx_rb_ctx);
		g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;
		return;
	}

    if(rbuff_available(&tx_rb_ctx) > 0)
    {
		static uint8_t temp_buffer[1500];
        uint32_t bytes_to_send = rbuff_read_buff(&tx_rb_ctx, temp_buffer, sizeof(temp_buffer));
        
        if(bytes_to_send > 0) {
            gsm_send_to_socket(temp_buffer, bytes_to_send, GSM_TX_DIR_LISTENER_SOCKET, false, false);
        }
    }

    if(rbuff_available(&tx_rb_ctx) == 0)
    {
		if (!g_http_server.is_rfwu) {
			GSM_LOG_INF_C(XCOLOR_GREEN, "[GSM HTTP SERVER] Response sent completely\r\n");
		}
        g_http_server.tx_data_ready = false;
        g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;
    }
}

void gsm_http_server_process_rx_data(void)
{
	if(g_http_server.rx_data_received)
	{
		g_http_server.rx_buffer[g_http_server.rx_length] = 0;
		if (!g_http_server.is_rfwu) {
			GSM_LOG_INF_C(XCOLOR_CYAN, "[GSM HTTP SERVER] Processing received data. Len[%d]\r\n", g_http_server.rx_length);
		}
#if 0
		CSLOG("[GSM HTTP SERVER] [%s]\r\n", g_http_server.rx_buffer);
#endif
		webserver_on_received(g_http_server.rx_buffer, g_http_server.rx_length);
		g_http_server.rx_data_received = false;
		g_http_server.rx_length = 0;


//		if(http_server_send(http_html, sizeof(http_html) - 1))
//		{
//			g_http_server.state = GSM_HTTP_SERVER_SEND_RESPONSE;
//		}
//		else
//		{
//			CSLOG("[GSM HTTP SERVER] Failed to prepare response!\r\n");
//		}
	}

	if(g_http_server.tx_data_ready){
			g_http_server.state = GSM_HTTP_SERVER_SEND_RESPONSE;
	}else{
		g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;
	}
}

void gsm_http_server_idle(void)
{
	if(g_http_server.rx_data_received)
	{
		g_http_server.state = GSM_HTTP_SERVER_STATE_PROCESS_RX_DATA;
	}
	else if(g_http_server.tx_data_ready)
	{
		g_http_server.state = GSM_HTTP_SERVER_SEND_RESPONSE;
	}
}

void gsm_http_server_process(void)
{
	switch(g_http_server.state)
	{
		case GSM_HTTP_SERVER_STATE_IDLE:
			gsm_http_server_idle();
			break;

		case GSM_HTTP_SERVER_STATE_PROCESS_RX_DATA:
			gsm_http_server_process_rx_data();
			break;

		case GSM_HTTP_SERVER_SEND_RESPONSE:
			gsm_http_server_send_response();
			break;

		default:
			g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;
			break;
	}
}
