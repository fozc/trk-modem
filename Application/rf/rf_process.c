/*
 * rf_process.c
 *
 *  Created on: 12 Haz 2026
 *      Author: fatih
 */
#include "rf_process.h"
#include "contiki.h"
#include "timer.h"
#include "bsp.h"
#include "utils.h"
#include "uart.h"
#include "ring_buff.h"
#include "scp.h"
#include "rf_scp_cmd.h"
#include "rf_scp_engine.h"
#include "stm32u3xx_hal.h"

/* ======================================================================
 *  Configuration
 * ====================================================================== */

/** UART port the RF module is connected to (USART3). */
#define RF_UART_PORT            UART_3

/** RX ring buffer size (must be a power of two). */
#define RF_RX_BUFF_SIZE         1024U

/** SCP inter-byte timeout in ms; stale partial frames are discarded. */
#define RF_SCP_TIMEOUT_MS       100U

/** RF process polling period (Contiki ticks). */
#define RF_PROCESS_POLL_TICKS   ((clock_time_t)10)

/** SCP master engine: per-attempt response timeout (ms). */
#define RF_ENGINE_TIMEOUT_MS    500U

/** SCP master engine: retransmit attempts after the first send. */
#define RF_ENGINE_MAX_RETRY     3U

/** SCP master engine: consecutive timeouts before link is marked DOWN. */
#define RF_ENGINE_LINK_FAIL_N   3U

/** Periodic liveness poll period (seconds). */
#define RF_LIVENESS_PERIOD_S    10U

/* ======================================================================
 *  Module state
 * ====================================================================== */
static uint8_t     s_rf_rx_buff[RF_RX_BUFF_SIZE];
static rbuff_t     s_rf_rb_ctx;
static scp_t       s_rf_scp_ctx;
static rf_engine_t s_rf_engine;

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

/**
 * @brief Engine send callback — hand a request packet to libscp for framing.
 */
static void rf_engine_send(const scp_packet_t *req)
{
    (void)scp_send(&s_rf_scp_ctx, req);
}

/**
 * @brief Engine completion callback — consume a finished job's result.
 */
static void rf_engine_on_done(rf_job_kind_t kind, rf_result_t result,
                              const scp_packet_t *rsp)
{
    if ((RF_JOB_GET_STATUS == kind) && (RF_RESULT_OK == result))
    {
        rf_hub_status_t status;

        if (RF_CMD_OK == rf_scp_decode_status(rsp, &status))
        {
            CSLOG("[RF] hub up=%us fw=%s sched=%u cyc=%u\r\n",
                  (unsigned)status.uptime_sec, status.fw_version,
                  (unsigned)status.sched_active,
                  (unsigned)status.sched_cycle_count);
            /* TODO: publish status to store / web layer */
        }
    }
    else if (RF_RESULT_TIMEOUT == result)
    {
        CSLOG_WARN("[RF] job=%u timeout (link=%u)\r\n",
                   (unsigned)kind,
                   (unsigned)rf_engine_link_state(&s_rf_engine));
    }
    else
    {
        /* MISRA 15.7: ERROR result / other jobs - nothing to do yet. */
    }
}

/* ======================================================================
 *  RX path
 * ====================================================================== */

void rf_rx_interrupt_handler(uint8_t data)
{
    (void)rbuff_write_byte(&s_rf_rb_ctx, data);
}

/**
 * @brief Reply to a PING with an empty ACK (broadcast PING is not answered).
 */
static void rf_reply_ping(const scp_packet_t *pkt)
{
    scp_packet_t ack;

    if (RF_SCP_ADDR_BROADCAST == pkt->dst)
    {
        return;   /* R0 2.4: broadcast PING gets no reply */
    }

    ack.dst      = pkt->src;
    ack.src      = s_rf_scp_ctx.device_address;
    ack.type     = SCP_TYPE_ACK;
    ack.cmd      = pkt->cmd;
    ack.seq      = pkt->seq;
    ack.data_len = 0U;

    (void)scp_send(&s_rf_scp_ctx, &ack);
}

/**
 * @brief Handle an unsolicited (proactive) SET from the hub.
 *
 * Proactive packets are processed independently of the master engine. S1
 * logs them; later phases route by CMD (0x13 boot, 0x14 discovery,
 * 0x11 live, 0x10 trip, 0x47 log-available).
 */
static void rf_proactive_dispatch(const scp_packet_t *pkt)
{
    CSLOG("[RF] proactive cmd=0x%02X from 0x%02X (len=%u)\r\n",
          (unsigned)pkt->cmd, (unsigned)pkt->src, (unsigned)pkt->data_len);
}

/**
 * @brief Dispatch one fully decoded SCP packet by TYPE.
 *
 * PING -> in-place ACK; ACK/ERROR -> master engine (solicited response);
 * SET -> proactive path. The engine and the proactive path are independent.
 *
 * @param[in] p_pkt  Decoded packet (never NULL).
 */
static void rf_handle_packet(const scp_packet_t *p_pkt)
{
    switch (p_pkt->type)
    {
        case SCP_TYPE_PING:
            rf_reply_ping(p_pkt);
            break;

        case SCP_TYPE_ACK:      /* fall-through: both are solicited responses */
        case SCP_TYPE_ERROR:
            rf_engine_on_response(&s_rf_engine, p_pkt);
            break;

        case SCP_TYPE_SET:
            rf_proactive_dispatch(p_pkt);
            break;

        default:                /* MISRA 16.4 */
            CSLOG("[RF] unexpected type=%u cmd=%u from 0x%02X\r\n",
                  (unsigned)p_pkt->type, (unsigned)p_pkt->cmd,
                  (unsigned)p_pkt->src);
            break;
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

PROCESS(rf_process, "rf-process");
PROCESS_THREAD(rf_process, ev, data)
{
    static struct etimer poll_timer;
    static struct timer  liveness_timer;

    (void)data;

    PROCESS_BEGIN();

    etimer_set(&poll_timer, RF_PROCESS_POLL_TICKS);
    timer_set(&liveness_timer, CLOCK_SECOND * RF_LIVENESS_PERIOD_S);

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&poll_timer));
        etimer_restart(&poll_timer);

        rf_process_rx();
        rf_engine_tick(&s_rf_engine, HAL_GetTick());

        if (timer_expired(&liveness_timer))
        {
            /* Addressed GET_STATUS (not broadcast PING); busy is rejected
             * silently and retried on the next period. */
            (void)rf_engine_start(&s_rf_engine, RF_JOB_GET_STATUS,
                                  HAL_GetTick());
            timer_set(&liveness_timer, CLOCK_SECOND * RF_LIVENESS_PERIOD_S);
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

    rf_engine_init(&s_rf_engine, rf_engine_send, rf_engine_on_done,
                   RF_ENGINE_TIMEOUT_MS, RF_ENGINE_MAX_RETRY,
                   RF_ENGINE_LINK_FAIL_N);

    uart_set_rx_interrupt(RF_UART_PORT, UART_RX_INT_ENABLE);

    process_start(&rf_process, NULL);
}
