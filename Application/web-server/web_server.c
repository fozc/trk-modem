/*
 * web_server.c
 *
 *  Created on: 12 Eki 2025
 *      Author: fatih
 */
#include "web_server.h"
#include "http_server.h"
#include "raw_tcp_fw_update.h"
#include <stdio.h>
#include "gsm_http_server.h"

/* ── Connection-mode multiplexing ───────────────────────────────── */

typedef enum {
    CONN_MODE_UNKNOWN = 0, /**< First bytes not yet received */
    CONN_MODE_HTTP,        /**< Standard HTTP traffic        */
    CONN_MODE_RFWU,        /**< Raw TCP firmware update      */
} conn_mode_t;

static conn_mode_t s_conn_mode  = CONN_MODE_UNKNOWN;
static uint8_t     s_magic_buf[4];
static uint8_t     s_magic_fill = 0U;

/* ── ─────────────────────────────────────────────────────────────── */

web_server_io_t io = {0};

static int web_server_send(const void *data, int len)
{
	if(io.send){
		/* Send data without logging to avoid blocking */
		return io.send(data, len);
	}
	return 0;
}

void webserver_on_received(const uint8_t *data, int len)
{
    const uint8_t *p         = data;
    int            remaining  = len;

    /*
     * On the first call after a connection is opened we inspect the initial
     * 4 bytes to decide whether this is an HTTP or an RFWU connection.
     */
    if (s_conn_mode == CONN_MODE_UNKNOWN) {
        while ((s_magic_fill < 4U) && (remaining > 0)) {
            s_magic_buf[s_magic_fill] = *p;
            s_magic_fill++;
            p++;
            remaining--;
        }

        if (s_magic_fill < 4U) {
            return; /* Wait for more bytes before deciding */
        }

        uint32_t magic = (uint32_t)s_magic_buf[0]
                       | ((uint32_t)s_magic_buf[1] << 8U)
                       | ((uint32_t)s_magic_buf[2] << 16U)
                       | ((uint32_t)s_magic_buf[3] << 24U);

        s_conn_mode = (magic == RFWU_MAGIC) ? CONN_MODE_RFWU : CONN_MODE_HTTP;

        /* Feed the buffered first 4 bytes into the correct handler */
        if (s_conn_mode == CONN_MODE_RFWU) {
            rfwu_on_receive(s_magic_buf, 4);
        } else {
            http_server_on_receive(s_magic_buf, 4);
        }
    }

    /* Forward any bytes that arrived after (or together with) the magic */
    if (remaining > 0) {
        if (s_conn_mode == CONN_MODE_RFWU) {
            rfwu_on_receive(p, remaining);
        } else {
            http_server_on_receive(p, remaining);
        }
    }
}

void webserver_on_connection_closed(void)
{
    conn_mode_t prev_mode = s_conn_mode;
    s_conn_mode  = CONN_MODE_UNKNOWN;
    s_magic_fill = 0U;

    /*
     * Only notify the module that actually handled this connection.
     * Avoids spurious HTTP-layer log output when the connection was
     * an RFWU firmware-update session, and vice versa.
     * CONN_MODE_UNKNOWN means fewer than 4 bytes were received so no
     * handler processed any data — reset both to be safe.
     */
    if (prev_mode != CONN_MODE_HTTP) {
        rfwu_on_disconnect();
    }
    if (prev_mode != CONN_MODE_RFWU) {
        gsm_http_server_reset();
        http_server_reset();
    }
}


void web_server_init(web_server_io_t *io_)
{
	if(io_){
		io = *io_;
	}

    http_server_init(web_server_send);
    rfwu_init(web_server_send);
}
