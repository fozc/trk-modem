/*
 * rf_comm.c
 *
 *  Created on: 23 Aug 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * RF hub communication: Contiki process + UART transport + SCP dispatch
 * + single-outstanding command mechanism.
 *
 * Katmanlar:
 *   rf_scp_codec -> request validation and payload decoding
 *   rf_scp.c/h   -> legacy status/PING codec
 *   rf_comm.c/h  -> tasima + komut mekanizmasi (bu dosya)
 *   libscp       -> cerceveleme (COBS + CRC + SOF/EOF)
 */

#define CSLOG_MODULE LOG_MOD_RF
#include "rf_comm.h"
#include "contiki.h"
#include "timer.h"
#include "bsp.h"
#include "utils.h"
#include "uart.h"
#include "ring_buff.h"
#include "scp.h"
#include <string.h>
#include "stm32u3xx_hal.h"
#include "rf_scp.h"
#include "rf_scp_codec.h"
#include "rf.h"
#include "rf_events.h"
#include "rf_group.h"
#include "power_board_scp.h"
#include "power_board_control.h"
#include "rf_inventory.h"
#include "rf_discovery.h"
#include "rf_log.h"
#include "rf_uart_bridge.h"
#include "rtc.h"
#include "rf_hil_transport.h"
#include "gpio_defs.h"
#include "cp56time2a.h"

/* ======================================================================
 * Configuration
 * ====================================================================== */

#if RF_SCP_OVER_MODBUS_PORT
/* HIL bench build: SCP on UART4 (Modbus RS-485 connector, PC simulator
 * as MH); USART3 stays silent. See rf_hil_transport.h. */
#define RF_UART_PORT              UART_4
#define RF_UART_BAUDRATE          230400U
#else
#define RF_UART_PORT              UART_3
#endif
#define RF_RX_BUFF_SIZE           1024U
#define RF_SCP_TIMEOUT_MS         100U

/** GET_STATUS periyodu (saniye) */
#define RF_LIVENESS_PERIOD_S      10U

/** Komut mekanizmasi: varsayilan timeout ve deneme sayilari */
#define RF_CMD_TIMEOUT_MS         500U
#define RF_CMD_RETRIES            3U

/** TIME_SYNC uses the same 500 ms / 3 extra retries as inventory. */

/** BOOT_NOTIFY scp_major beklenen deger */
#define RF_SCP_MAJOR_EXPECTED     1U

#define RF_TIME_SYNC_PERIOD_S     3600U

/* ======================================================================
 * Module state
 * ====================================================================== */

static uint8_t       rx_buff[RF_RX_BUFF_SIZE];
static rbuff_t       rx_ring;
static scp_t         scp_ctx;

/* ======================================================================
 * Command mechanism (single outstanding)
 * ====================================================================== */

typedef struct
{
    bool              busy;          /* komut aktif mi                    */
    bool              retry_pending; /* ERROR retry waits for deadline    */
    scp_packet_t      last_req;      /* retry'de AYNI paket gonderilir    */
    uint8_t           retries_left;  /* kalan ek deneme hakki             */
    uint32_t          deadline_ms;   /* bu denemenin son beklenme ani      */
    uint32_t          timeout_ms;    /* deneme basina bekleme suresi       */
    scp_cmd_done_fn_t done;          /* bitince cagrilacak                */
} scp_cmd_ctx_t;

/* Owned by cooperative Contiki callers. UART ISR only writes rx_ring;
 * command and notification processing run in rf_comm_check_rx/process.
 */
static scp_cmd_ctx_t cmd_ctx = {0};
static uint8_t       cmd_next_seq = 1;  /* 1'den baslar, 0xFF -> 0x00 sarar */

/* BOOT_NOTIFY -> TIME_SYNC pending (komut mekanizmasi mesgulse bekler) */
static bool          time_sync_pending = false;
static bool          time_sync_boot = false;
static uint8_t       hub_major;
static bool          hub_boot_received;
static struct timer  time_sync_timer;
static bool          time_sync_timer_started;
static struct timer  liveness_timer;
static bool          liveness_timer_started;

/* ======================================================================
 * Process declaration + simulator block
 * ====================================================================== */

PROCESS_NAME(rf_comm_process);

//#define RF_SIMULATOR

#ifdef RF_SIMULATOR
#include "main.h"

void bms_rx_interrupt_handler(uint8_t data)
{
    /* rf_comm_init cagrilana dek ring buffer NULL'dur - veriyi at.
     * Bu, UART5 kesmesi init'ten once tetiklenirse hardfault onler. */
    if (rx_ring.buff != NULL)
    {
        (void)rbuff_write_byte(&rx_ring, data);
    }
}

static void bms_send_buff(const uint8_t *buffer, size_t length)
{
    extern UART_HandleTypeDef huart5;

    gpio_set_pin(BMS_OE_BSP_GPIO, BMS_OE_BSP_PIN, GPIO_HIGH);
    gpio_set_pin(BMS_RE_BSP_GPIO, BMS_RE_BSP_PIN, GPIO_HIGH);

    LL_USART_ClearFlag_TC(UART5);

    for (uint16_t i = 0U; i < length; i++)
    {
        LL_USART_TransmitData8(UART5, buffer[i]);
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5))
        {
            /* Wait until the data register can accept the next byte. */
        }
    }

    while (!LL_USART_IsActiveFlag_TC(UART5))
    {
        /* Wait for transmission-complete. */
    }

    gpio_set_pin(BMS_OE_BSP_GPIO, BMS_OE_BSP_PIN, GPIO_LOW);
    gpio_set_pin(BMS_RE_BSP_GPIO, BMS_RE_BSP_PIN, GPIO_LOW);
}
#endif

/* ======================================================================
 * UART transport
 * ====================================================================== */

void rf_comm_rx_interrupt_handler(uint8_t data)
{
    (void)rbuff_write_byte(&rx_ring, data);
}

static void rf_comm_transmit(const uint8_t *frame, size_t frame_len)
{
    /* Saydam kopru acikken USART3 RF modulun kendisine ayrilmistir:
     * liveness/time-sync/retry frame'leri saydam akisa karismasin.
     * Tum SCP TX yollari bu fonksiyondan gecer; reset ile kopru
     * kapanir ve SCP kendiliginden normale doner. */
    if (rf_uart_bridge_is_enabled())
    {
        return;
    }

    /* Ham frame dokumu (COBS kodlu wire baytlari) - yalniz VERBOSE.
     * modbus TX dokumu ile ayni desen. */
	CSLOG("\r\nRF TX[%u]: [", (unsigned)frame_len);
    for (size_t i = 0U; i < frame_len; i++)
    {
        CSLOG_NODT("%02X ", frame[i]);
    }
    CSLOG_NODT("]\r\n");

#ifdef RF_SIMULATOR
    bms_send_buff(frame, frame_len);
#elif RF_SCP_OVER_MODBUS_PORT
    /* RS-485 half duplex: hold MODBUS_OE during the whole frame so the
     * transceiver drives the bus, release after TC. Blocking, like the
     * Modbus TX path; frame bursts are <= 256 B (~11 ms at 230400). */
    uart_send_buffer_rs485(RF_UART_PORT, MODBUS_OE_BSP_GPIO,
                           MODBUS_OE_BSP_PIN, true, frame,
                           (uint16_t)frame_len);
#else
    uart_send_buffer(RF_UART_PORT, (const char *)frame, (int)frame_len);
#endif
}

/* ======================================================================
 * Command mechanism implementation
 * ====================================================================== */

bool scp_is_free(void)
{
    return !cmd_ctx.busy && !rf_uart_bridge_is_enabled();
}

bool rf_comm_can_load_inventory(void)
{
    return (RF_SCP_MAJOR_EXPECTED == hub_major) && !time_sync_boot;
}

uint8_t rf_comm_get_hub_major(void)
{
    return hub_major;
}

void rf_comm_sync_time(void)
{
    if (RF_SCP_MAJOR_EXPECTED == hub_major)
    {
        time_sync_pending = true;
    }
}

static void finish_command(scp_cmd_result_t result, const scp_packet_t *packet)
{
    scp_cmd_done_fn_t done = cmd_ctx.done;

    cmd_ctx.busy = false;
    cmd_ctx.retry_pending = false;
    cmd_ctx.done = NULL;
    if (NULL != done)
    {
        done(result, packet);
    }
}

bool scp_send_request(const scp_packet_t *request, scp_cmd_done_fn_t done)
{
    uint32_t timeout_ms = RF_CMD_TIMEOUT_MS;
    uint8_t retries = RF_CMD_RETRIES;

    if (NULL == request)
    {
        return false;
    }
    switch (request->cmd)
    {
        case RF_SCP_CMD_CFG_WRITE:
        case RF_SCP_CMD_CFG_COMMIT:
        case RF_SCP_CMD_CFG_ABORT:
        case RF_SCP_CMD_PWR_COMMAND:
            timeout_ms = 1000U;
            break;
        case RF_SCP_CMD_EPOCH_REFRESH:
            timeout_ms = 1000U;
            retries = 1U;
            break;
        case RF_SCP_CMD_LOG_READ_RANGE:
            retries = 2U;
            break;
        case RF_SCP_CMD_PWR_CFG2:
            if (SCP_TYPE_SET == request->type)
            {
                timeout_ms = 1000U;
            }
            break;
        default:
            break;
    }
    return scp_send_command(request->type, request->cmd, request->data,
                            request->data_len, timeout_ms, retries, done);
}

bool scp_send_command(uint8_t type, uint8_t cmd,
                      const uint8_t *body, uint8_t body_len,
                      uint32_t timeout_ms, uint8_t retries,
                      scp_cmd_done_fn_t done)
{
    scp_packet_t request = {0};

    if (!scp_is_free() || (NULL == scp_ctx.transmit))
    {
        return false;   /* mesgul - tek aktif komut kurali */
    }
    if ((hub_boot_received && (RF_SCP_MAJOR_EXPECTED != hub_major) &&
         (RF_SCP_CMD_GET_STATUS != cmd) && (0U != cmd)) ||
        (((RF_SCP_CMD_INVENTORY_SET == cmd) ||
          (RF_SCP_CMD_INVENTORY_END == cmd) ||
          (RF_SCP_CMD_INVENTORY_UPDATE == cmd)) &&
         !rf_comm_can_load_inventory()))
    {
        return false;
    }

    if ((body_len > SCP_MAX_DATA_SIZE) ||
        ((0U < body_len) && (NULL == body)) || (0U == timeout_ms) ||
        (0x7FFFFFFFU < timeout_ms))
    {
        return false;   /* gecersiz govde boyutu */
    }

    /* Paketi kur - retry'de bu ayni paket yeniden gonderilir */
    request.type     = type;
    request.cmd      = cmd;
    request.seq      = cmd_next_seq;
    request.data_len = body_len;

    if ((body != NULL) && (body_len > 0U))
    {
        (void)memcpy(request.data, body, body_len);
    }
    if (RF_CMD_OK != rf_scp_build_packet(&request, &cmd_ctx.last_req))
    {
        return false;
    }

    /* Ilk gonderim */
    if (SCP_STATUS_OK != scp_send(&scp_ctx, &cmd_ctx.last_req))
    {
        return false;
    }
    cmd_next_seq++;
    rf_inventory_request_sent(cmd);

    /* Durumu isaretle */
    cmd_ctx.busy         = true;
    cmd_ctx.retry_pending = false;
    cmd_ctx.retries_left = retries;
    cmd_ctx.timeout_ms   = timeout_ms;
    cmd_ctx.done         = done;
    cmd_ctx.deadline_ms  = HAL_GetTick() + timeout_ms;

    CSLOG("[RF ST->RF] cmd=0x%02X seq=%u "
          "gonderildi (timeout=%ums, retries=%u)\r\n",
		  cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
		  (unsigned)cmd_ctx.timeout_ms, (unsigned)cmd_ctx.retries_left);

    return true;
}

void scp_process(uint32_t now_ms)
{
    if (!cmd_ctx.busy || rf_uart_bridge_is_enabled())
    {
        return;
    }

    /* Delays are 1..INT32_MAX ms; unsigned subtraction handles tick wrap. */
    if (0x7FFFFFFFU < (now_ms - cmd_ctx.deadline_ms))
    {
        return;
    }

    /* Sure doldu - retry hakki var mi? */
    if (cmd_ctx.retry_pending || (0U < cmd_ctx.retries_left))
    {
        if (cmd_ctx.retry_pending)
        {
            cmd_ctx.last_req.seq = cmd_next_seq++;
            cmd_ctx.retry_pending = false;
        }
        else
        {
            /* Only an unanswered request is repeated with the same SEQ. */
            cmd_ctx.retries_left--;
        }
        if (SCP_STATUS_OK != scp_send(&scp_ctx, &cmd_ctx.last_req))
        {
            finish_command(SCP_CMD_TIMEOUT, NULL);
            return;
        }
        rf_inventory_request_sent(cmd_ctx.last_req.cmd);
        cmd_ctx.deadline_ms = now_ms + cmd_ctx.timeout_ms;

        CSLOG("[RF] retry cmd=0x%02X seq=%u (kalan=%u)\r\n",
              cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
              cmd_ctx.retries_left);
    }
    else
    {
        /* Tum denemeler tukendi - TIMEOUT bildir */
        CSLOG_WARN("[RF] cmd=0x%02X seq=%u TIMEOUT\r\n",
                   cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq);

        finish_command(SCP_CMD_TIMEOUT, NULL);
    }
}

static bool can_retry_error(const scp_packet_t *packet)
{
    if (RF_SCP_ERR_BUSY == packet->data[0])
    {
        /* CFG2 GEN mismatch requires a fresh GET, not a blind SET. */
        return !((RF_SCP_CMD_PWR_CFG2 == packet->cmd) &&
                 (2U == packet->data_len));
    }
    return (RF_SCP_ERR_NOT_AVAILABLE == packet->data[0]) &&
           ((RF_SCP_CMD_PWR_CFG2 == packet->cmd) ||
            (RF_SCP_CMD_PWR_TELEMETRY == packet->cmd));
}

void scp_on_response(const scp_packet_t *pkt)
{
    rf_scp_message_t message;
    scp_cmd_result_t   result;

    if ((NULL == pkt) || !cmd_ctx.busy || cmd_ctx.retry_pending ||
        rf_uart_bridge_is_enabled())
    {
        return;             /* bekleyen komut yok - bayat yanit */
    }
    /* Eslesme: CMD ve SEQ ayni olmali */
    if ((pkt->cmd != cmd_ctx.last_req.cmd) ||
        (pkt->seq != cmd_ctx.last_req.seq))
    {
        CSLOG_WARN("[RF] expected cmd=0x%02X seq=%u, "
                   "received cmd=0x%02X seq=%u\r\n",
				   cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
				   pkt->cmd, pkt->seq);
        return;             /* baska istegin yanitina benziyor - atla */
    }
    if (RF_CMD_OK != rf_scp_decode_message(pkt, &message))
    {
        return;
    }

    if (pkt->type == SCP_TYPE_ACK)
    {
        if ((RF_SCP_CMD_PWR_CFG2 == pkt->cmd) &&
            (((SCP_TYPE_GET == cmd_ctx.last_req.type) &&
              (23U != pkt->data_len)) ||
             ((SCP_TYPE_SET == cmd_ctx.last_req.type) &&
              (1U != pkt->data_len))))
        {
            return;
        }
        result = SCP_CMD_OK;
    }
    else if (pkt->type == SCP_TYPE_ERROR)
    {
        if (can_retry_error(pkt) &&
            (0U < cmd_ctx.retries_left))
        {
            cmd_ctx.retries_left--;
            cmd_ctx.retry_pending = true;
            cmd_ctx.deadline_ms = HAL_GetTick() + cmd_ctx.timeout_ms;
            return;
        }
        result = SCP_CMD_ERR;
    }
    else
    {
    	CSLOG_WARN("[RF] Gecersiz yanit tip=%u cmd=0x%02X seq=%u - atla\r\n",
    							   pkt->type, pkt->cmd, pkt->seq);
        return;             /* ACK/ERROR disi - gecersiz yanit */
    }

    /* Once serbest birak - callback icinde yeni komut baslatilabilir */
    if (SCP_CMD_OK == result)
    {
        rf_inventory_record_ack(&cmd_ctx.last_req);
    }
    finish_command(result, pkt);
}

/* ======================================================================
 * Packet dispatch (TYPE-based routing)
 * ====================================================================== */

static const char *scp_type_to_string(uint8_t type)
{
    switch (type)
    {
        case SCP_TYPE_PING:  return "PING";
        case SCP_TYPE_GET:   return "GET";
        case SCP_TYPE_SET:   return "SET";
        case SCP_TYPE_ACK:   return "ACK";
        case SCP_TYPE_ERROR: return "ERROR";
        default:             return "UNKNOWN";
    }
}

static const char *scp_cmd_to_string(uint8_t cmd)
{
    switch (cmd)
    {
        case RF_SCP_CMD_GET_STATUS:        return "GET_STATUS";
        case RF_SCP_CMD_TIME_SYNC:         return "TIME_SYNC";
        case RF_SCP_CMD_INVENTORY_SET:     return "INVENTORY_SET";
        case RF_SCP_CMD_INVENTORY_END:     return "INVENTORY_END";
        case RF_SCP_CMD_TRIP_NOTIFY:       return "TRIP_NOTIFY";
        case RF_SCP_CMD_LIVE_DATA:         return "LIVE_DATA";
        case RF_SCP_CMD_ANOMALY_REPORT:    return "ANOMALY_REPORT";
        case RF_SCP_CMD_BOOT_NOTIFY:       return "BOOT_NOTIFY";
        case RF_SCP_CMD_DISCOVERY_REPORT:  return "DISCOVERY_REPORT";
        case RF_SCP_CMD_CFG_STATUS_NOTIFY: return "CFG_STATUS_NOTIFY";
        case RF_SCP_CMD_LOG_AVAILABLE:     return "LOG_AVAILABLE";
        default:                           return "UNKNOWN";
    }
}

static void print_scp_packet(const scp_packet_t *pkt)
{
    CSLOG("[RF RF->ST] %s %s seq=%u len=%u\r\n",
          scp_type_to_string(pkt->type),
          scp_cmd_to_string(pkt->cmd),
          pkt->seq, (unsigned)pkt->data_len);
}

/** PING'e bostan ACK don (broadcast haric - R1 2.4) */
static void reply_ping(const scp_packet_t *pkt)
{
    scp_packet_t ack;

    if ((NULL == pkt) || (RF_SCP_ADDR_HUB != pkt->src) ||
        (RF_SCP_ADDR_RTU != pkt->dst) || (SCP_TYPE_PING != pkt->type) ||
        (0U != pkt->cmd) || (0U != pkt->data_len) ||
        rf_uart_bridge_is_enabled())
    {
        return;
    }
    if (!rf_scp_build_ping_reply(pkt, &ack))
    {
        return;
    }

    if (SCP_STATUS_OK != scp_send(&scp_ctx, &ack))
    {
        CSLOG_WARN("[RF] PING ACK encode failed\r\n");
    }
}

/* ======================================================================
 * Proactive handlers
 * ====================================================================== */

/** 0x07 TIME_SYNC gonderildikten sonra cagrilir */
static void on_time_sync_done(scp_cmd_result_t result,
                              const scp_packet_t *rsp)
{
    (void)rsp;

    if (SCP_CMD_RESTARTED == result)
    {
        return;
    }
    timer_set(&time_sync_timer, CLOCK_SECOND * RF_TIME_SYNC_PERIOD_S);
    time_sync_timer_started = true;

    if (SCP_CMD_OK == result)
    {
        CSLOG("[RF] hub saati senkronize (TIME_SYNC ACK)\r\n");

        /* Devreye alma zincirinin 3. adimi: saat tamam -> envanter push */
        if (time_sync_boot)
        {
            time_sync_boot = false;
            rf_inventory_start();
        }
    }
    else
    {
        CSLOG_WARN("[RF] TIME_SYNC failed; next BOOT/hour may retry\r\n");
    }
}

/** Use the validated local RTC; an invalid clock is never sent. */
static bool build_time_sync_body(uint8_t out[RF_SCP_TIME_SYNC_BODY_LEN])
{
    if (!rtc_hw_is_valid())
    {
        return false;
    }
    bsp_rtc_t    now_rtc = bsp_get_datetime();
    cp56time2a_t ts      = cp56time2a_from_rtc(&now_rtc);

    if (0U != ts.iv_bit)
    {
        return false;
    }

    out[0] = (uint8_t)(ts.milliseconds & 0xFFU);
    out[1] = (uint8_t)((ts.milliseconds >> 8) & 0xFFU);
    out[2] = (uint8_t)((ts.minute & 0x3FU) | (uint8_t)(ts.iv_bit << 7));
    out[3] = (uint8_t)((ts.hour & 0x1FU) | (uint8_t)(ts.su_bit << 7));
    out[4] = (uint8_t)((ts.day & 0x1FU) | (uint8_t)(ts.dow << 5));
    out[5] = ts.month;
    out[6] = ts.year;
    return true;
}

/** 0x13 BOOT_NOTIFY isle: surum kontrolu + TIME_SYNC tetikle (G-5, G-4) */
static void handle_boot_notify(const scp_packet_t *pkt)
{
    uint8_t major;

    if (pkt->data_len != 1U)
    {
        CSLOG_WARN("[RF] BOOT_NOTIFY gecersiz (bos govde)\r\n");
        return;
    }

    major = pkt->data[0];
    hub_major = major;
    hub_boot_received = true;
    time_sync_boot = true;
    time_sync_pending = (RF_SCP_MAJOR_EXPECTED == major);
    time_sync_timer_started = false;
    rf_inventory_reset();
    rf_hub_restarted();
    if (cmd_ctx.busy)
    {
        finish_command(SCP_CMD_RESTARTED, NULL);
    }
    rf_events_init();
    rf_group_hub_restarted();
    power_board_scp_hub_restarted();
    power_board_control_hub_restarted();
    if (major != RF_SCP_MAJOR_EXPECTED)
    {
        CSLOG_WARN("[RF] BOOT_NOTIFY scp_major=%u (beklenen %u) - uyari, "
                   "inventory stopped\r\n",
                   (unsigned)major, (unsigned)RF_SCP_MAJOR_EXPECTED);
        return;
    }

    CSLOG("[RF] BOOT_NOTIFY (scp_major=%u) -> TIME_SYNC hazirlaniyor\r\n",
          (unsigned)major);
}

/** Proaktik SET'ler - komut mekanizmasindan bagimsiz, her zaman islenir */
static void handle_proactive(const scp_packet_t *pkt)
{
    rf_scp_message_t message;

    if (RF_CMD_OK != rf_scp_decode_message(pkt, &message))
    {
        return;
    }
    switch (pkt->cmd)
    {
        case RF_SCP_CMD_BOOT_NOTIFY:
            handle_boot_notify(pkt);
            break;

        case RF_SCP_CMD_DISCOVERY_REPORT:
            if (rf_discovery_report(message.body.discovery.eui64,
                                    message.body.discovery.rssi))
            {
                CSLOG("[RF] kesif: yeni cihaz "
                      "EUI=%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                      pkt->data[0], pkt->data[1], pkt->data[2],
                      pkt->data[3], pkt->data[4], pkt->data[5],
                      pkt->data[6], pkt->data[7]);
            }
            break;

        case RF_SCP_CMD_LIVE_DATA:
        {
            cp56time2a_t received_time = {.iv_bit = 1U};

            if (rtc_hw_is_valid())
            {
                const bsp_rtc_t rtc = bsp_get_datetime();
                received_time = cp56time2a_from_rtc(&rtc);
            }
            (void)rf_handle_live(&message.body.live, HAL_GetTick(),
                                &received_time);
            break;
        }

        case RF_SCP_CMD_TRIP_NOTIFY:
            (void)rf_handle_trip(&message.body.trip, HAL_GetTick());
            break;

        case RF_SCP_CMD_ANOMALY_REPORT:
            (void)rf_handle_anomaly(&message.body.anomaly);
            break;

        case RF_SCP_CMD_LOG_AVAILABLE:
            rf_events_notify(&message);
            break;

        case RF_SCP_CMD_CFG_STATUS_NOTIFY:
            (void)rf_group_handle_status(&message);
            break;

        case RF_SCP_CMD_PWR_SUMMARY:
            (void)power_board_handle_summary(&message, HAL_GetTick());
            break;

        case RF_SCP_CMD_PWR_ALARM:
            (void)power_board_handle_alarm(&message, HAL_GetTick());
            break;

        case RF_SCP_CMD_PWR_RESULT:
            (void)power_board_handle_command_result(&message);
            break;

        case RF_SCP_CMD_PWR_TELEMETRY:
            (void)power_board_handle_raw(&message);
            break;

        default:    /* MISRA 16.4 - S3-S5'te yeni case'ler gelecek */
            CSLOG("[RF] proactive (henuz islenmiyor)\r\n");
            break;
    }
}

/* ======================================================================
 * RX path
 * ====================================================================== */

/**
 * @brief Gelen paketi TYPE'a gore ayir.
 *
 * Uc bagimsiz yol - biri digerini bloklamaz:
 *   PING  -> reply_ping (aninda ACK)
 *   ACK/ERROR -> scp_on_response (komut mekanizmasi)
 *   SET   -> handle_proactive (bagimsiz, mesguliyete bakmaz)
 */
static void rf_comm_on_data_received(const scp_packet_t *pkt)
{
    if ((NULL == pkt) || (RF_SCP_ADDR_HUB != pkt->src) ||
        (0xF0U <= pkt->cmd) || rf_uart_bridge_is_enabled())
    {
        return;
    }
    switch (pkt->type)
    {
        case SCP_TYPE_PING:
            reply_ping(pkt);
            break;

        case SCP_TYPE_ACK:        // Requset cevaplari (GET, SET)
        case SCP_TYPE_ERROR:
        	scp_on_response(pkt);
            break;

        case SCP_TYPE_SET:        /* Unsolicited notifications. */
            handle_proactive(pkt);
            break;

        default:                /* MISRA 16.4 */
            break;
    }
}

static void rf_comm_check_rx(void)
{
    uint8_t byte;

    while (rbuff_read_safe(&rx_ring, &byte))
    {
        scp_process_byte(&scp_ctx, byte);

        if (scp_packet_ready(&scp_ctx))
        {
            const scp_packet_t *pkt = scp_get_packet(&scp_ctx);

            /* Ham frame dokumu: PACKET_READY'de rx_buf/rx_idx scp'nin
             * decode edilmis wire baytlarini scp_packet_done'a kadar
             * dondurur (scp.c "frozen until done"). Yalniz VERBOSE. */
            CSLOG("RF RX[%u]: [", (unsigned)scp_ctx.rx_idx);
            for (size_t i = 0U; i < scp_ctx.rx_idx; i++)
            {
                CSLOG_NODT("%02X ", scp_ctx.rx_buf[i]);
            }
            CSLOG_NODT("]\r\n");

            print_scp_packet(pkt);
            rf_comm_on_data_received(pkt);
            scp_packet_done(&scp_ctx);
        }
    }
}

/* ======================================================================
 * Periodic jobs
 * ====================================================================== */

/** GET_STATUS bittiginde cagrilir */
static void on_status_done(scp_cmd_result_t result,
                           const scp_packet_t *rsp)
{
    if (SCP_CMD_OK == result)
    {
        rf_hub_status_t status;

        if (RF_CMD_OK == rf_scp_decode_status(rsp, &status))
        {
            CSLOG("[RF] hub up=%us fw=%s sched=%u cyc=%u\r\n",
                  (unsigned)status.uptime_sec, status.fw_version,
                  (unsigned)status.sched_active,
                  (unsigned)status.sched_cycle_count);
        }
    }
    /* TIMEOUT: bir sonraki periyot zaten tekrar deneyecek */
}

static void rf_comm_periodic_jobs(void)
{
    if (time_sync_timer_started && timer_expired(&time_sync_timer) &&
        (RF_SCP_MAJOR_EXPECTED == hub_major))
    {
        time_sync_pending = true;
        time_sync_timer_started = false;
    }

    /* TIME_SYNC pending: BOOT geldi ama komut mekanizmasi mesguldu;
     * simdi bos mu? */
    if (time_sync_pending && scp_is_free() &&
        (RF_SCP_MAJOR_EXPECTED == hub_major))
    {
        uint8_t cp56_body[RF_SCP_TIME_SYNC_BODY_LEN];
        bool has_valid_time = build_time_sync_body(cp56_body);

        if (has_valid_time &&
            scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_TIME_SYNC,
                             cp56_body, RF_SCP_TIME_SYNC_BODY_LEN,
                             RF_CMD_TIMEOUT_MS,
                             RF_CMD_RETRIES,
                             on_time_sync_done))
        {
            time_sync_pending = false;
        }
        else if (!has_valid_time && time_sync_boot)
        {
            /* Invalid RTC does not block inventory. Keep the time request
             * pending; its later ACK must not restart the upload.
             */
            time_sync_boot = false;
            rf_inventory_start();
        }
        else
        {
            /* Busy/invalid periodic time refresh waits for a valid clock. */
        }
    }

    /* Envanter siralayici: aktif ama komut mekanizmasi mesgulse bekle */
    rf_inventory_continue();
    rf_group_process(HAL_GetTick());
    rf_events_process(HAL_GetTick());
    power_board_scp_process(HAL_GetTick());
    power_board_control_process(HAL_GetTick());

    /* Periyodik GET_STATUS (canlilik) */
    if (!liveness_timer_started)
    {
        timer_set(&liveness_timer, CLOCK_SECOND * RF_LIVENESS_PERIOD_S);
        liveness_timer_started = true;
    }

    if (timer_expired(&liveness_timer) && scp_is_free())
    {
        (void)scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
                               NULL, 0,
                               RF_CMD_TIMEOUT_MS, RF_CMD_RETRIES,
                               on_status_done);
        timer_set(&liveness_timer, CLOCK_SECOND * RF_LIVENESS_PERIOD_S);
    }
}

/* ======================================================================
 * Contiki process
 * ====================================================================== */

PROCESS(rf_comm_process, "rf_comm_process");
PROCESS_THREAD(rf_comm_process, ev, data)
{
    (void)ev;

    static struct etimer poll_timer;
    (void)data;

    PROCESS_BEGIN();

    etimer_set(&poll_timer, 10);

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&poll_timer));
        etimer_restart(&poll_timer);

        rf_comm_check_rx();              /* RX -> dispatch              */
        scp_process(HAL_GetTick());      /* timeout / retry / callback  */
        rf_comm_periodic_jobs();         /* zamanlayici isleri           */
    }

    PROCESS_END();
}

void rf_comm_init(uint8_t device_address)
{
    (void)memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    cmd_next_seq = 1U;
    hub_major = 0U;
    hub_boot_received = false;
    time_sync_pending = false;
    time_sync_boot = false;
    time_sync_timer_started = false;
    liveness_timer_started = false;
    rf_inventory_reset();
    rf_init();
    rf_events_init();
    rf_group_init();
    if (!rbuff_init(&rx_ring, rx_buff, sizeof(rx_buff)))
    {
        CSLOG_ERR("[RF] Failed to initialize RX ring buffer!\r\n");
        return;
    }

    if (scp_init(&scp_ctx, device_address, rf_comm_transmit,
                 HAL_GetTick, RF_SCP_TIMEOUT_MS) != SCP_STATUS_OK)
    {
        CSLOG_ERR("[RF] Failed to initialize SCP context!\r\n");
        return;
    }

#if RF_SCP_OVER_MODBUS_PORT
    /* UART4 comes up at the CubeMX Modbus default (115200); SCP line
     * rate is 230400. Reprogram while idle, before RX starts. */
    uart_set_baudrate(RF_UART_PORT, RF_UART_BAUDRATE);
#endif
    uart_set_rx_interrupt(RF_UART_PORT, UART_RX_INT_ENABLE);

    process_start(&rf_comm_process, NULL);
}

/*** end of file ***/
