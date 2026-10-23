/*
 * gsm_http_server.c
 *
 *  Created on: Dec 20, 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#define CSLOG_MODULE LOG_MOD_GSM
#include "gsm_http_server.h"
#include "gsm_engine.h"
#include "gsm_listener_process.h"
#include "web_server.h"
#include "http_server.h"
#include "gsm_log.h"
#include "ring_buff.h"
#include <string.h>
#include <limits.h>
 
#define GSM_HTTP_SERVER_RX_BUFFER_SIZE 2048
/* HTTP writes and drains run in the cooperative GSM process. Reuse the
 * common SPSC queue; no additional IRQ masking or atomic state is needed.
 * One byte is reserved by the ring's empty/full representation. */
#define GSM_HTTP_SERVER_TX_BUFFER_SIZE (10U * 1024U)
#define GSM_HTTP_SERVER_HEADER_RESERVE 512U

_Static_assert(HTTP_TX_BUFFER_SIZE + GSM_HTTP_SERVER_HEADER_RESERVE <=
               GSM_HTTP_SERVER_TX_BUFFER_SIZE - 1U,
               "dynamic HTTP response exceeds the TX queue");

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

    const uint8_t *static_data;
    uint32_t static_remaining;
	bool     tx_data_ready;
	bool     close_after_response;
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
    /* A pending response owns the transport. One RX fragment can wait;
     * further pipelined input must not overwrite it or mix responses. */
    if (!g_http_server.is_rfwu && g_http_server.tx_data_ready &&
        g_http_server.rx_data_received)
    {
        g_http_server.close_after_response = true;
        return;
    }

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
			CSLOG_WARN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
			CSLOG_WARN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
			CSLOG_WARN("[GSM HTTP SERVER] -->>> Warning: Previous RX data overwritten! <<--\r\n");
		}
    }

	if(length > 0 && length <= sizeof(g_http_server.rx_buffer) - 1)
	{
		memcpy(g_http_server.rx_buffer, data, length);
		g_http_server.rx_length = length;
		g_http_server.rx_data_received = true;

		if (!g_http_server.is_rfwu) {
			CCSLOG(XCOLOR_CYAN, "[GSM HTTP SERVER] Client data received: %d bytes\r\n", length);
		}
	}
	else
	{
		if (!g_http_server.is_rfwu) {
			CSLOG_ERR("[GSM HTTP SERVER] Invalid client data length: %d bytes\r\n", length);
		}
	}
}

static int http_server_send(const void *data, uint32_t len)
{
    if (0U < g_http_server.static_remaining)
    {
        return -1;
    }
    if (SOCKET_CLOSED == gsm_get_listener_socket_state())
    {
		if (!g_http_server.is_rfwu) {
			CSLOG_ERR("[GSM HTTP SERVER] Listener socket not open, cannot send data!\r\n");
		}
		return -1;
	}

	if (0U == rbuff_write_buff(&tx_rb_ctx, data, len))
	{
		g_http_server.tx_data_ready = true;
		return (int)len;
	}
	else
	{
		if (!g_http_server.is_rfwu) {
			CSLOG_ERR("[GSM HTTP SERVER] TX buffer overflow, cannot send %d bytes! (free: %d/%d)\r\n",
			          len,
			          (int)rbuff_available_for_write(&tx_rb_ctx),
			          (int)GSM_HTTP_SERVER_TX_BUFFER_SIZE);
		}
		return -1;
	}
}

/* The caller guarantees immutable program-lifetime storage. */
static int http_server_send_static(const void *data, uint32_t length)
{
    if ((NULL == data) || (0U == length) || ((uint32_t)INT_MAX < length) ||
        (SOCKET_CLOSED == gsm_get_listener_socket_state()) ||
        (0U < g_http_server.static_remaining))
    {
        return -1;
    }
    g_http_server.static_data = data;
    g_http_server.static_remaining = length;
    g_http_server.tx_data_ready = true;
    return (int)length;
}

void gsm_http_server_reset(void)
{
	if (!g_http_server.is_rfwu) {
		CSLOG_WARN("[GSM HTTP SERVER] Reset  gsm_server\r\n");
	}
    g_http_server.static_data = NULL;
    g_http_server.static_remaining = 0U;
	g_http_server.rx_length = 0;
	g_http_server.rx_data_received = false;
	g_http_server.tx_data_ready = false;
	g_http_server.close_after_response = false;
	g_http_server.is_rfwu = false;
	g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;

	rbuff_clear(&tx_rb_ctx);
}

void gsm_http_server_init(void)
{
    g_http_server.close_after_response = false;
    web_server_io_t wio = {0};
    wio.send = http_server_send;
    wio.send_static = http_server_send_static;
    web_server_init(&wio);

    if (!rbuff_init(&tx_rb_ctx, http_tx_buff,
                    GSM_HTTP_SERVER_TX_BUFFER_SIZE))
    {
        CSLOG_ERR("[GSM HTTP SERVER] TX ring initialization failed\r\n");
    }
    else
    {
        CSLOG("[GSM HTTP SERVER] TX ring ready: %u usable bytes\r\n",
              (unsigned int)(GSM_HTTP_SERVER_TX_BUFFER_SIZE - 1U));
    }
}


void gsm_http_server_close_after_response(void)
{
    g_http_server.close_after_response = true;
    g_http_server.tx_data_ready = true;
}

void gsm_http_server_send_response(void)
{
    if (SOCKET_CLOSED == gsm_get_listener_socket_state())
    {
        gsm_http_server_reset();
        return;
    }
    if (0U != gsm_tx_is_ready())
    {
        return;
    }

    uint8_t *queued_data = NULL;
    uint32_t length = 0U;
    if (rbuff_get_read_block(&tx_rb_ctx, &queued_data, &length))
    {
        if (1500U < length)
        {
            length = 1500U;
        }
        if (0U == gsm_send_to_socket(queued_data, (uint16_t)length,
                                     GSM_TX_DIR_LISTENER_SOCKET,
                                     false, false))
        {
            (void)rbuff_skip(&tx_rb_ctx, length);
        }
        return;
    }
    if (0U < g_http_server.static_remaining)
    {
        length = g_http_server.static_remaining;
        if (1500U < length)
        {
            length = 1500U;
        }
        if (0U == gsm_send_to_socket(g_http_server.static_data,
                                     (uint16_t)length,
                                     GSM_TX_DIR_LISTENER_SOCKET,
                                     false, false))
        {
            g_http_server.static_data += length;
            g_http_server.static_remaining -= length;
            if (0U == g_http_server.static_remaining)
            {
                g_http_server.static_data = NULL;
            }
        }
        return;
    }

    /* Both sources are empty and the last modem TX has completed. */
    if (g_http_server.close_after_response)
    {
        g_http_server.close_after_response = false;
        gsm_listener_socket_event_handler(GSM_LISTENER_WEB,
                                          GSM_USER_EVENT_CLOSE_SOCKET);
    }
    g_http_server.tx_data_ready = false;
    g_http_server.state = GSM_HTTP_SERVER_STATE_IDLE;
}

void gsm_http_server_process_rx_data(void)
{
	if(g_http_server.rx_data_received)
	{
		g_http_server.rx_buffer[g_http_server.rx_length] = 0;
		if (!g_http_server.is_rfwu) {
			CCSLOG(XCOLOR_CYAN, "[GSM HTTP SERVER] Processing received data. Len[%d]\r\n", g_http_server.rx_length);
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
    if (g_http_server.tx_data_ready)
    {
        g_http_server.state = GSM_HTTP_SERVER_SEND_RESPONSE;
    }
    else if (g_http_server.rx_data_received)
    {
        g_http_server.state = GSM_HTTP_SERVER_STATE_PROCESS_RX_DATA;
    }
    else
    {
        /* No work pending. */
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

/*** end of file ***/
