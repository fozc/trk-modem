/*
 * rf_process.c
 *
 *  Created on: 12 Haz 2026
 *      Author: fatih
 */
#include "rf_process.h"
#include "contiki.h"
#include "bsp.h"
#include "utils.h"
#include "uart.h"
#include "ring_buff.h"
#include "scp.h"
#include "stm32u3xx_hal.h"

/* ======================================================================
 *  Configuration
 * ====================================================================== */

/** UART port the RF module is connected to (USART3). */
#define RF_UART_PORT            UART_3

/** RX ring buffer size (must be a power of two). */
#define RF_RX_BUFF_SIZE         256U

/** SCP inter-byte timeout in ms; stale partial frames are discarded. */
#define RF_SCP_TIMEOUT_MS       100U

/** RF process polling period (Contiki ticks). */
#define RF_PROCESS_POLL_TICKS   ((clock_time_t)50)

/* ======================================================================
 *  Module state
 * ====================================================================== */

static uint8_t  s_rf_rx_buff[RF_RX_BUFF_SIZE];
static rbuff_t  s_rf_rb_ctx;
static scp_t    s_rf_scp_ctx;

/* ======================================================================
 *  SCP HAL callbacks
 * ====================================================================== */

/**
 * @brief SCP transmit callback — writes an encoded frame to the RF UART.
 */
static void rf_scp_transmit(const uint8_t *p_frame, size_t frame_len)
{
    if ((p_frame == NULL) || (frame_len == 0U))
    {
        return;
    }

    uart_send_buffer(RF_UART_PORT, (const char *)p_frame, (int)frame_len);
}

/* ======================================================================
 *  RX path
 * ====================================================================== */

void rf_rx_interrupt_handler(uint8_t data)
{
    (void)rbuff_write_byte(&s_rf_rb_ctx, data);
}

/**
 * @brief Process one fully decoded SCP packet.
 *
 * First-stage handler: replies to PING with an ACK; other packet types are
 * only logged for now.
 *
 * @param[in] p_pkt  Decoded packet (never NULL).
 */
static void rf_handle_packet(const scp_packet_t *p_pkt)
{
    switch (p_pkt->type)
    {
        case SCP_TYPE_PING:
        {
            CSLOG("[RF] PING from 0x%02X (cmd=%u seq=%u) -> ACK\r\n",
                  (unsigned)p_pkt->src, (unsigned)p_pkt->cmd,
                  (unsigned)p_pkt->seq);

            scp_packet_t ack;
            ack.dst      = p_pkt->src;
            ack.src      = s_rf_scp_ctx.device_address;
            ack.type     = SCP_TYPE_ACK;
            ack.cmd      = p_pkt->cmd;
            ack.seq      = p_pkt->seq;
            ack.data_len = 0U;

            (void)scp_send(&s_rf_scp_ctx, &ack);
            break;
        }

        default:
        {
            CSLOG("[RF] packet type=%u cmd=%u from 0x%02X (len=%u)\r\n",
                  (unsigned)p_pkt->type, (unsigned)p_pkt->cmd,
                  (unsigned)p_pkt->src, (unsigned)p_pkt->data_len);
            break;
        }
    }
}

/**
 * @brief Drain the RX ring buffer, feed the SCP parser, dispatch packets.
 */
static void rf_process_rx(void)
{
    uint8_t byte;

    while (rbuff_read_safe(&s_rf_rb_ctx, &byte))
    {
        scp_process_byte(&s_rf_scp_ctx, byte);

        if (scp_packet_ready(&s_rf_scp_ctx))
        {
            const scp_packet_t *p_pkt = scp_get_packet(&s_rf_scp_ctx);
            if (p_pkt != NULL)
            {
                rf_handle_packet(p_pkt);
            }
            scp_packet_done(&s_rf_scp_ctx);
        }
    }
}

/* ======================================================================
 *  Contiki process
 * ====================================================================== */

PROCESS(rf_process, "rf-process");

PROCESS_THREAD(rf_process, ev, data)
{
    static struct etimer timer;
    static struct timer  ping_timer;

    (void)ev;
    (void)data;

    PROCESS_BEGIN();

    etimer_set(&timer, RF_PROCESS_POLL_TICKS);
    timer_set(&ping_timer, CLOCK_SECOND * 10U);

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_restart(&timer);

        rf_process_rx();

        if (timer_expired(&ping_timer))
		{
			CSLOG("[RF] Sending periodic PING\r\n");

			scp_send_ping(&s_rf_scp_ctx, 0xFFU, 0U);  /* Broadcast PING with seq=0 */
			timer_set(&ping_timer, CLOCK_SECOND * 10U);
		}
    }

    PROCESS_END();
}

void rf_process_init(uint8_t device_address)
{
    if (!rbuff_init(&s_rf_rb_ctx, s_rf_rx_buff, sizeof(s_rf_rx_buff)))
    {
        CSLOG_ERR("[RF] Failed to initialize RX ring buffer!\r\n");
        return;
    }

    if (scp_init(&s_rf_scp_ctx, device_address, rf_scp_transmit,
                 HAL_GetTick, RF_SCP_TIMEOUT_MS) != SCP_STATUS_OK)
    {
        CSLOG_ERR("[RF] Failed to initialize SCP context!\r\n");
        return;
    }

    uart_set_rx_interrupt(RF_UART_PORT, UART_RX_INT_ENABLE);

    process_start(&rf_process, NULL);
}
