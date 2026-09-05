/*
 * rf_comm.c
 *
 *  Created on: 23 Aug 2026
 *      Author: fatih
 *
 * RF hub communication: Contiki process + UART transport + SCP dispatch
 * + single-outstanding command mechanism.
 *
 * Katmanlar:
 *   rf_scp.c/h   -> saf codec (paket kurma / cozumle, durumsuz)
 *   rf_comm.c/h  -> tasima + komut mekanizmasi (bu dosya)
 *   libscp       -> cerceveleme (COBS + CRC + SOF/EOF)
 */

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
#include "rf_inventory.h"
#include "rf_discovery.h"
#include "rf_log.h"
#include "rtc.h"
#include "cp56time2a.h"

/* ======================================================================
 * Configuration
 * ====================================================================== */

#define RF_UART_PORT              UART_3
#define RF_RX_BUFF_SIZE           1024U
#define RF_SCP_TIMEOUT_MS         100U

/** GET_STATUS periyodu (saniye) */
#define RF_LIVENESS_PERIOD_S      10U

/** Komut mekanizmasi: varsayilan timeout ve deneme sayilari */
#define RF_CMD_TIMEOUT_MS         500U
#define RF_CMD_RETRIES            2U

/** TIME_SYNC: yazma sinifi, daha uzun bekleme (R1 zamanlama tablosu) */
#define RF_CMD_TIMEOUT_WRITE_MS   1000U
#define RF_CMD_RETRIES_WRITE      2U

/** BOOT_NOTIFY scp_major beklenen deger */
#define RF_SCP_MAJOR_EXPECTED     1U

/** TIME_SYNC deneme limiti (tek BOOT bildirisi basina) */
#define RF_TIME_SYNC_MAX_TRIES    5U

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
    scp_packet_t      last_req;      /* retry'de AYNI paket gonderilir    */
    uint8_t           retries_left;  /* kalan ek deneme hakki             */
    uint32_t          deadline_ms;   /* bu denemenin son beklenme ani      */
    uint32_t          timeout_ms;    /* deneme basina bekleme suresi       */
    scp_cmd_done_fn_t done;          /* bitince cagrilacak                */
} scp_cmd_ctx_t;

static scp_cmd_ctx_t cmd_ctx = {0};
static uint8_t       cmd_next_seq = 1;  /* 1'den baslar, 0xFF -> 0x00 sarar */

/* BOOT_NOTIFY -> TIME_SYNC pending (komut mekanizmasi mesgulse bekler) */
static bool          time_sync_pending = false;
static uint8_t       time_sync_tries = 0;

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
    /* Ham frame dokumu (COBS kodlu wire baytlari) - yalniz VERBOSE.
     * modbus TX dokumu ile ayni desen. */
    RF_LOG_NODT("\r\nRF TX[%u]: ", (unsigned)frame_len);
    for (size_t i = 0U; i < frame_len; i++)
    {
        RF_LOG_NODT("%02X ", frame[i]);
    }
    RF_LOG_NODT("\r\n");

#ifdef RF_SIMULATOR
    bms_send_buff(frame, frame_len);
#else
    uart_send_buffer(RF_UART_PORT, (const char *)frame, (int)frame_len);
#endif
}

/* ======================================================================
 * Command mechanism implementation
 * ====================================================================== */

bool scp_is_free(void)
{
    return !cmd_ctx.busy;
}

bool scp_send_command(uint8_t type, uint8_t cmd,
                      const uint8_t *body, uint8_t body_len,
                      uint32_t timeout_ms, uint8_t retries,
                      scp_cmd_done_fn_t done)
{
    if (cmd_ctx.busy)
    {
        return false;   /* mesgul - tek aktif komut kurali */
    }

    if (body_len > SCP_MAX_DATA_SIZE)
    {
        return false;   /* gecersiz govde boyutu */
    }

    /* Paketi kur - retry'de bu ayni paket yeniden gonderilir */
    cmd_ctx.last_req.dst      = RF_SCP_ADDR_HUB;
    cmd_ctx.last_req.src      = RF_SCP_ADDR_RTU;
    cmd_ctx.last_req.type     = type;
    cmd_ctx.last_req.cmd      = cmd;
    cmd_ctx.last_req.seq      = cmd_next_seq++;
    cmd_ctx.last_req.data_len = body_len;

    if ((body != NULL) && (body_len > 0U))
    {
        (void)memcpy(cmd_ctx.last_req.data, body, body_len);
    }

    /* Ilk gonderim */
    (void)scp_send(&scp_ctx, &cmd_ctx.last_req);

    /* Durumu isaretle */
    cmd_ctx.busy         = true;
    cmd_ctx.retries_left = retries;
    cmd_ctx.timeout_ms   = timeout_ms;
    cmd_ctx.done         = done;
    cmd_ctx.deadline_ms  = HAL_GetTick() + timeout_ms;

    RF_LOG_INF("[RF ST->RF] cmd=0x%02X seq=%u gonderildi (timeout=%ums, retries=%u)\r\n",
		  cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
		  (unsigned)cmd_ctx.timeout_ms, (unsigned)cmd_ctx.retries_left);

    return true;
}

void scp_process(uint32_t now_ms)
{
    scp_cmd_done_fn_t  done;
    scp_cmd_result_t   result;

    if (!cmd_ctx.busy)
    {
        return;
    }

    /* Henuz sure dolmadi mi? (isaretli fark = wraparound guvenli) */
    if ((int32_t)(now_ms - cmd_ctx.deadline_ms) < 0)
    {
        return;
    }

    /* Sure doldu - retry hakki var mi? */
    if (cmd_ctx.retries_left > 0U)
    {
        cmd_ctx.retries_left--;

        /* AYNI paket, AYNI SEQ - idempotentlik (R1 2.3d) */
        (void)scp_send(&scp_ctx, &cmd_ctx.last_req);
        cmd_ctx.deadline_ms = now_ms + cmd_ctx.timeout_ms;

        RF_LOG_INF("[RF] retry cmd=0x%02X seq=%u (kalan=%u)\r\n",
              cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
              cmd_ctx.retries_left);
    }
    else
    {
        /* Tum denemeler tukendi - TIMEOUT bildir */
        done   = cmd_ctx.done;
        result = SCP_CMD_TIMEOUT;

        /* Once serbest birak - callback icinde yeni komut
         * baslatilabilir */
        cmd_ctx.busy = false;

        RF_LOG_WRN("[RF] cmd=0x%02X seq=%u TIMEOUT\r\n",
                   cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq);

        if (done != NULL)
        {
            done(result, NULL);
        }
    }
}

void scp_on_response(const scp_packet_t *pkt)
{
    scp_cmd_done_fn_t  done;
    scp_cmd_result_t   result;

    if (!cmd_ctx.busy)
    {
    	RF_LOG_WRN("[RF] Yanit beklenmiyor cmd=0x%02X seq=%u - atla\r\n",
				   pkt->cmd, pkt->seq);
        return;             /* bekleyen komut yok - bayat yanit */
    }

    /* Eslesme: CMD ve SEQ ayni olmali */
    if ((pkt->cmd != cmd_ctx.last_req.cmd) ||
        (pkt->seq != cmd_ctx.last_req.seq))
    {
    	RF_LOG_WRN("[RF] Belenen CMD/SEQ cmd=0x%02X seq=%u yanit cmd=0x%02X seq=%u - atla\r\n",
				   cmd_ctx.last_req.cmd, cmd_ctx.last_req.seq,
				   pkt->cmd, pkt->seq);
        return;             /* baska istegin yanitina benziyor - atla */
    }

    if (pkt->type == SCP_TYPE_ACK)
    {
        result = SCP_CMD_OK;
    }
    else if (pkt->type == SCP_TYPE_ERROR)
    {
        result = SCP_CMD_ERR;
    }
    else
    {
    	RF_LOG_WRN("[RF] Gecersiz yanit tip=%u cmd=0x%02X seq=%u - atla\r\n",
    							   pkt->type, pkt->cmd, pkt->seq);
        return;             /* ACK/ERROR disi - gecersiz yanit */
    }

    /* Once serbest birak - callback icinde yeni komut baslatilabilir */
    done         = cmd_ctx.done;
    cmd_ctx.busy = false;

    if (done != NULL)
    {
        done(result, pkt);  /* pkt yalnizca callback suresince gecerli */
    }
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
    RF_LOG_INF("[RF RF->ST] %s %s seq=%u len=%u\r\n",
          scp_type_to_string(pkt->type),
          scp_cmd_to_string(pkt->cmd),
          pkt->seq, (unsigned)pkt->data_len);
}

/** PING'e bostan ACK don (broadcast haric - R1 2.4) */
static void reply_ping(const scp_packet_t *pkt)
{
    scp_packet_t ack;

    if (RF_SCP_ADDR_BROADCAST == pkt->dst)
    {
        return;
    }

    ack.dst      = pkt->src;
    ack.src      = RF_SCP_ADDR_RTU;
    ack.type     = SCP_TYPE_ACK;
    ack.cmd      = pkt->cmd;
    ack.seq      = pkt->seq;
    ack.data_len = 0U;

    (void)scp_send(&scp_ctx, &ack);
}

/* ======================================================================
 * Proactive handlers
 * ====================================================================== */

/** 0x07 TIME_SYNC gonderildikten sonra cagrilir */
static void on_time_sync_done(scp_cmd_result_t result,
                              const scp_packet_t *rsp)
{
    (void)rsp;

    if (SCP_CMD_OK == result)
    {
        RF_LOG_INF("[RF] hub saati senkronize (TIME_SYNC ACK)\r\n");

        /* Devreye alma zincirinin 3. adimi: saat tamam -> envanter push */
        rf_inventory_start();
    }
    else
    {
        time_sync_tries++;
        if (time_sync_tries < RF_TIME_SYNC_MAX_TRIES)
        {
            time_sync_pending = true;    /* bosken tekrar dene */
        }
        else
        {
            RF_LOG_WRN("[RF] TIME_SYNC %u denemede basarisiz\r\n",
                       time_sync_tries);
        }
    }
}

/** RTC'den CP56Time2a govdesi kur (G-4: RTC gecersizse IV=1) */
static void build_time_sync_body(uint8_t out[RF_SCP_TIME_SYNC_BODY_LEN])
{
    bsp_rtc_t    now_rtc = bsp_get_datetime();
    cp56time2a_t ts      = cp56time2a_from_rtc(&now_rtc);

    if (!rtc_hw_is_valid())
    {
        ts.iv_bit = 1U;   /* RTC guvensiz -> damza gecersiz isaretle */
    }

    out[0] = (uint8_t)(ts.milliseconds & 0xFFU);
    out[1] = (uint8_t)((ts.milliseconds >> 8) & 0xFFU);
    out[2] = (uint8_t)((ts.minute & 0x3FU) | (uint8_t)(ts.iv_bit << 7));
    out[3] = (uint8_t)((ts.hour & 0x1FU) | (uint8_t)(ts.su_bit << 7));
    out[4] = (uint8_t)((ts.day & 0x1FU) | (uint8_t)(ts.dow << 5));
    out[5] = ts.month;
    out[6] = ts.year;
}

/** 0x13 BOOT_NOTIFY isle: surum kontrolu + TIME_SYNC tetikle (G-5, G-4) */
static void handle_boot_notify(const scp_packet_t *pkt)
{
    uint8_t major;

    if (pkt->data_len < 1U)
    {
        RF_LOG_WRN("[RF] BOOT_NOTIFY gecersiz (bos govde)\r\n");
        return;
    }

    major = pkt->data[0];
    if (major != RF_SCP_MAJOR_EXPECTED)
    {
        RF_LOG_WRN("[RF] BOOT_NOTIFY scp_major=%u (beklenen %u) - uyari, "
                   "devam\r\n",
                   (unsigned)major, (unsigned)RF_SCP_MAJOR_EXPECTED);
    }

    RF_LOG_INF("[RF] BOOT_NOTIFY (scp_major=%u) -> TIME_SYNC hazirlaniyor\r\n",
          (unsigned)major);
    time_sync_pending = true;
    time_sync_tries   = 0;
}

/** Proaktik SET'ler - komut mekanizmasindan bagimsiz, her zaman islenir */
static void handle_proactive(const scp_packet_t *pkt)
{
    switch (pkt->cmd)
    {
        case RF_SCP_CMD_BOOT_NOTIFY:
            handle_boot_notify(pkt);
            break;

        case RF_SCP_CMD_DISCOVERY_REPORT:
            if ((pkt->data_len >= 8U) &&
                (rf_discovery_add(pkt->data)))
            {
                RF_LOG_INF("[RF] kesif: yeni cihaz "
                      "EUI=%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                      pkt->data[0], pkt->data[1], pkt->data[2],
                      pkt->data[3], pkt->data[4], pkt->data[5],
                      pkt->data[6], pkt->data[7]);
            }
            break;

        default:    /* MISRA 16.4 - S3-S5'te yeni case'ler gelecek */
            RF_LOG_INF("[RF] proactive (henuz islenmiyor)\r\n");
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
    switch (pkt->type)
    {
        case SCP_TYPE_PING:
            reply_ping(pkt);
            break;

        case SCP_TYPE_ACK:        // Requset cevaplari (GET, SET)
        case SCP_TYPE_ERROR:
        	scp_on_response(pkt);
            break;

        case SCP_TYPE_SET:        // proaktif SET'ler (BOOT_NOTIFY, TRIP_NOTIFY, ...)
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
            RF_LOG_NODT("\r\nRF RX[%u]: ", (unsigned)scp_ctx.rx_idx);
            for (size_t i = 0U; i < scp_ctx.rx_idx; i++)
            {
                RF_LOG_NODT("%02X ", scp_ctx.rx_buf[i]);
            }
            RF_LOG_NODT("\r\n");

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
            RF_LOG_INF("[RF] hub up=%us fw=%s sched=%u cyc=%u\r\n",
                  (unsigned)status.uptime_sec, status.fw_version,
                  (unsigned)status.sched_active,
                  (unsigned)status.sched_cycle_count);
        }
    }
    /* TIMEOUT: bir sonraki periyot zaten tekrar deneyecek */
}

static void rf_comm_periodic_jobs(void)
{
    static struct timer liveness_timer;
    static bool         timer_started = false;

    /* TIME_SYNC pending: BOOT geldi ama komut mekanizmasi mesguldu;
     * simdi bos mu? */
    if (time_sync_pending && scp_is_free())
    {
        uint8_t cp56_body[RF_SCP_TIME_SYNC_BODY_LEN];

        build_time_sync_body(cp56_body);
        if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_TIME_SYNC,
                             cp56_body, RF_SCP_TIME_SYNC_BODY_LEN,
                             RF_CMD_TIMEOUT_WRITE_MS,
                             RF_CMD_RETRIES_WRITE,
                             on_time_sync_done))
        {
            time_sync_pending = false;
        }
    }

    /* Envanter siralayici: aktif ama komut mekanizmasi mesgulse bekle */
    rf_inventory_continue();

    /* Periyodik GET_STATUS (canlilik) */
    if (!timer_started)
    {
        timer_set(&liveness_timer, CLOCK_SECOND * RF_LIVENESS_PERIOD_S);
        timer_started = true;
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
    if (!rbuff_init(&rx_ring, rx_buff, sizeof(rx_buff)))
    {
        RF_LOG_ERR("[RF] Failed to initialize RX ring buffer!\r\n");
        return;
    }

    if (scp_init(&scp_ctx, device_address, rf_comm_transmit,
                 HAL_GetTick, RF_SCP_TIMEOUT_MS) != SCP_STATUS_OK)
    {
        RF_LOG_ERR("[RF] Failed to initialize SCP context!\r\n");
        return;
    }

    uart_set_rx_interrupt(RF_UART_PORT, UART_RX_INT_ENABLE);

    process_start(&rf_comm_process, NULL);
}

/*** end of file ***/
