/*
 * hub.c
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * Modem_RF_Hub simulator davranisi. Tum komutlar R1 paketindeki govde
 * duzenlerine gore yanitlar; ayrintili islem loglari burada basilir.
 */

#include "hub.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ======================================================================
 * Sabitler
 * ====================================================================== */

#define HUB_ADDR              0x01U
#define RTU_ADDR              0x02U

#define CMD_GET_STATUS        0x01U
#define CMD_GET_FRAM_STATS    0x02U
#define CMD_SET_CONFIG        0x03U
#define CMD_INVENTORY_SET     0x04U
#define CMD_INVENTORY_END     0x05U
#define CMD_INVENTORY_UPDATE  0x06U
#define CMD_TIME_SYNC         0x07U
#define CMD_TRIP              0x10U
#define CMD_LIVE              0x11U
#define CMD_ANOMALY           0x12U
#define CMD_BOOT              0x13U
#define CMD_DISCOVERY         0x14U
#define CMD_CFG_READ          0x20U
#define CMD_CFG_NOTIFY        0x21U
#define CMD_CFG_WRITE         0x22U
#define CMD_CFG_COMMIT        0x24U
#define CMD_CFG_ABORT         0x26U
#define CMD_CFG_STATUS        0x28U
#define CMD_EPOCH             0x2AU
#define CMD_LOG_HEAD          0x40U
#define CMD_LOG_RECORD        0x42U
#define CMD_LOG_RANGE         0x44U
#define CMD_LOG_CONSUME       0x46U
#define CMD_LOG_BELL          0x47U

#define ERR_UNKNOWN_CMD       0x01U
#define ERR_INVALID_PARAM     0x02U
#define ERR_BUSY              0x03U
#define ERR_NOT_SUPPORTED     0x04U
#define ERR_NOT_AVAILABLE     0x05U

/* cfg akisi zamanlamasi (RF dagitim simulasyonu) */
#define HUB_APPLY_DELAY_MS    800U
/* 0x2A kesif penceresi (spec 30 s; testi hizli tutmak icin 5 s) */
#define HUB_EPOCH_BUSY_MS     5000U
/* tek cercevede maksimum kayit (244 B / 60 B) */
#define HUB_RANGE_MAX         4U

/* cfg sebep kodlari (R1 3.3.5) */
#define REASON_NONE           0U
#define REASON_NOT_LIVE       1U
#define REASON_NO_INV         2U
#define REASON_RANGE          3U
#define REASON_USER_ABORT     10U

/* ======================================================================
 * Kucuk yardimcilar: LE okuma/yazma, log, CRC
 * ====================================================================== */

static uint16_t rd16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static void wr16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
    b[2] = (uint8_t)((v >> 16) & 0xFFU);
    b[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void wrf32(uint8_t *b, float f)
{
    uint32_t v;
    (void)memcpy(&v, &f, sizeof(v));
    wr32(b, v);
}

static void hub_log(const hub_t *hub, const char *fmt, ...)
{
    va_list ap;
    printf("[HUB %10u] ", hub->now_ms);
    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    uint8_t bit;

    for (i = 0U; i < len; i++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint32_t)data[i] << 8));
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)(((uint32_t)crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)((uint32_t)crc << 1);
            }
        }
    }
    return crc;
}

uint16_t hub_cfg_crc(const uint8_t block[HUB_BLOCK_LEN])
{
    /* yazilabilir-CRC: maske disi @3-56 (R1 4.4) */
    return crc16_ccitt_false(&block[3], 54U);
}

hub_device_t *hub_find_device(hub_t *hub, const uint8_t eui[8])
{
    size_t i;
    for (i = 0U; i < HUB_DEVICES_MAX; i++)
    {
        hub_device_t *d = &hub->dev[i];
        if ((d->valid != 0U) && (memcmp(d->eui, eui, 8U) == 0))
        {
            return d;
        }
    }
    return NULL;
}

/** EUI-64'u 16-hex olarak log icin bicimlendir. */
static void fmt_eui(const uint8_t eui[8], char out[17])
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0U; i < 8U; i++)
    {
        out[(i * 2U)] = hex[(eui[i] >> 4) & 0x0FU];
        out[(i * 2U) + 1U] = hex[eui[i] & 0x0FU];
    }
    out[16] = '\0';
}

/* ======================================================================
 * Yanit gonderme
 * ====================================================================== */

static void send_pkt(hub_t *hub, scp_packet_t *pkt)
{
    /* SET tekrar politikesi icin son SET yaniti saklanir (R1 2.3d) */
    if (hub->capture_reply != 0U)
    {
        hub->last_reply = *pkt;
        hub->have_last_set = 1U;
        hub->last_set_seq = hub->pending_set_seq;
        hub->capture_reply = 0U;
    }
    hub->send(pkt, hub->user);
}

static void send_ack(hub_t *hub, const scp_packet_t *req,
                     const uint8_t *body, uint8_t len)
{
    scp_packet_t ack;

    ack.dst = req->src;
    ack.src = HUB_ADDR;
    ack.type = SCP_TYPE_ACK;
    ack.cmd = req->cmd;
    ack.seq = req->seq;
    ack.data_len = len;
    if ((body != NULL) && (len > 0U))
    {
        (void)memcpy(ack.data, body, len);
    }
    send_pkt(hub, &ack);
}

static void send_error(hub_t *hub, const scp_packet_t *req, uint8_t code,
                       const char *why)
{
    scp_packet_t err;

    err.dst = req->src;
    err.src = HUB_ADDR;
    err.type = SCP_TYPE_ERROR;
    err.cmd = req->cmd;
    err.seq = req->seq;
    err.data_len = 1U;
    err.data[0] = code;
    send_pkt(hub, &err);
    hub_log(hub, "  -> ERROR cmd=0x%02X code=0x%02X (%s)", req->cmd, code, why);
}

static void send_proactive(hub_t *hub, uint8_t cmd, const uint8_t *body,
                           uint8_t len, const char *name)
{
    scp_packet_t pkt;

    pkt.dst = RTU_ADDR;
    pkt.src = HUB_ADDR;
    pkt.type = SCP_TYPE_SET;
    pkt.cmd = cmd;
    pkt.seq = hub->proactive_seq;
    hub->proactive_seq++;
    pkt.data_len = len;
    if ((body != NULL) && (len > 0U))
    {
        (void)memcpy(pkt.data, body, len);
    }
    send_pkt(hub, &pkt);
    hub_log(hub, "PROACTIVE %s (cmd=0x%02X seq=%u len=%u)", name, cmd,
            pkt.seq, len);
}

/* ======================================================================
 * Zaman: CP56Time2a yardimcilari
 * ====================================================================== */

/* Gun-sayisindan takvime (Howard Hinnant civil_from_days algoritmasi) */
static void civil_from_days(int32_t z, int32_t *y, uint8_t *m, uint8_t *d)
{
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mm = (mp < 10) ? (mp + 3) : (mp - 9);

    if (mm <= 2U)
    {
        yy++;
    }
    *y = yy;
    *m = (uint8_t)mm;
    *d = (uint8_t)dd;
}

static uint32_t hub_wall_sec(const hub_t *hub)
{
    if (hub->time_synced != 0U)
    {
        return hub->synced_epoch_sec +
               ((hub->now_ms - hub->synced_uptime_ms) / 1000U);
    }
    return 0U;
}

static void cp56_encode(uint8_t out[7], uint32_t epoch_sec, uint16_t ms,
                        uint8_t clock_quality)
{
    uint32_t days = epoch_sec / 86400U;
    uint32_t secs = epoch_sec % 86400U;
    int32_t y;
    uint8_t m;
    uint8_t d;

    civil_from_days((int32_t)days, &y, &m, &d);

    wr16(out, ms);                                  /* ms (0-59999)       */
    out[2] = (uint8_t)((secs / 60U % 60U) & 0x3FU); /* dakika + IV bit7   */
    if (clock_quality == 0U)
    {
        out[2] = (uint8_t)(out[2] | 0x80U);         /* IV=1 gecersiz      */
    }
    out[3] = (uint8_t)((secs / 3600U) & 0x1FU);     /* saat               */
    out[4] = (uint8_t)((d & 0x1FU) | 0x20U);        /* gun + dow(ozet)    */
    out[5] = m;
    out[6] = (uint8_t)(y % 100);
}

static void cp56_decode_log(const hub_t *hub, const uint8_t b[7])
{
    uint16_t ms = rd16(b);
    uint8_t iv = (uint8_t)((b[2] >> 7) & 1U);
    uint8_t min = (uint8_t)(b[2] & 0x3FU);
    uint8_t hour = (uint8_t)(b[3] & 0x1FU);
    uint8_t day = (uint8_t)(b[4] & 0x1FU);
    uint8_t mon = b[5];
    uint8_t yr = b[6];

    hub_log(hub, "  TIME_SYNC -> 20%02u-%02u-%02u %02u:%02u:%02u.%03u (IV=%u)",
            yr, mon, day, hour, min,
            (unsigned)((hub_wall_sec(hub)) % 60U), ms, iv);
}

/* CP56'dan epoch'a (kaba; saat/dk/gun yeter) */
static uint32_t cp56_to_epoch(const uint8_t b[7])
{
    /* 1970-01-01 baz; yil 2000+ varsayimi (yil byte'i 0-99) */
    int32_t y = 2000 + (int32_t)b[6];
    int32_t m = (int32_t)b[5];
    int32_t d = (int32_t)(b[4] & 0x1FU);
    int32_t days;
    /* Howard Hinnant days_from_civil */
    int32_t yy = y;
    yy -= (m <= 2) ? 1 : 0;
    {
        int32_t era = (yy >= 0 ? yy : yy - 399) / 400;
        uint32_t yoe = (uint32_t)(yy - era * 400);
        uint32_t doy = (uint32_t)((153 * ((m > 2 ? m - 3 : m + 9)) + 2) / 5 + d - 1);
        uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + (int32_t)doe - 719468;
    }
    {
        uint32_t secs_of_day = ((uint32_t)(b[3] & 0x1FU) * 3600U) +
                               ((uint32_t)(b[2] & 0x3FU) * 60U);
        return ((uint32_t)days * 86400U) + secs_of_day;
    }
}

/* ======================================================================
 * Config blogu: default + hedef cihaz kurulumu
 * ====================================================================== */

static void block_default(uint8_t block[HUB_BLOCK_LEN], const hub_device_t *d)
{
    float f;

    (void)memset(block, 0, HUB_BLOCK_LEN);
    block[0] = d->zone;
    block[1] = d->fider;
    block[2] = d->phase;

    f = 6.0f;   (void)memcpy(&block[3], &f, 4);   /* Nominal_Current   */
    f = 13.0f;  (void)memcpy(&block[7], &f, 4);   /* Ia_Threshold      */
    f = 0.30f;  (void)memcpy(&block[11], &f, 4);  /* Is_Safety         */
    f = 1000.0f;(void)memcpy(&block[15], &f, 4);  /* di_dt_Threshold   */
    f = 2.0f;   (void)memcpy(&block[19], &f, 4);  /* Line_Break        */

    block[23] = 50U;                              /* Line_Frequency    */
    wr16(&block[24], 60U);                        /* Threshold_ms      */
    wr16(&block[26], 30U);                        /* T_Reclaim_Sec     */
    wr16(&block[28], 180U);                       /* T_Mem_Dead_Sec    */
    wr16(&block[30], 60U);                        /* Inrush_Timer_ms   */
    f = 5.0f;   (void)memcpy(&block[32], &f, 4);  /* Inrush_Multiplier */
    wr16(&block[36], 200U);                       /* Dead_Line_Verify  */
    wr16(&block[38], 100U);                       /* Sync_Trip_Delay   */
    wr16(&block[40], 40U);                        /* Trip_Pulse_ms     */

    block[42] = 3U;                               /* Set_Count         */
    block[43] = 0U;                               /* Operating_Mode    */
    block[44] = 0U;                               /* Trip_Mode         */
    block[45] = 39U;                              /* Inrush_100Hz      */

    block[46] = 0U;                               /* CLP_Enabled       */
    f = 2.0f;  (void)memcpy(&block[47], &f, 4);   /* CLP_Multiplier    */
    wr16(&block[51], 5000U);                      /* CLP_Duration_MS   */

    f = 32.0f; (void)memcpy(&block[53], &f, 4);   /* Vtrip_Target      */

    block[57] = d->channel;                       /* RF_Channel (mask) */
    block[58] = 7U;                               /* RF_Atim    (mask) */
    (void)memcpy(&block[59], "HUBSIM01", 8);      /* Last_Modem_EUI    */
}

/* ======================================================================
 * Istek isleyicileri
 * ====================================================================== */

static void req_get_status(hub_t *hub, const scp_packet_t *req)
{
    uint8_t body[25];

    wr32(&body[0], (hub->now_ms - hub->start_ms) / 1000U);
    (void)memset(&body[4], 0, 16U);
    (void)memcpy(&body[4], HUB_FW_LABEL, sizeof(HUB_FW_LABEL) - 1U);
    body[20] = 1U;                                /* sched_active */
    wr32(&body[21], hub->sched_cycles);

    hub_log(hub, "  GET_STATUS -> uptime=%us fw=%s cycles=%u",
            (unsigned)((hub->now_ms - hub->start_ms) / 1000U),
            HUB_FW_LABEL, hub->sched_cycles);
    send_ack(hub, req, body, 25U);
}

static void req_get_fram(hub_t *hub, const scp_packet_t *req)
{
    uint8_t body[8];

    wr16(&body[0], hub->head);
    wr16(&body[2], hub->wrap);
    wr16(&body[4], (uint16_t)(HUB_LOG_SLOTS - hub->pending));
    body[6] = 0U;
    body[7] = 0U;

    hub_log(hub, "  FRAM_STATS -> head=%u wrap=%u free=%u",
            hub->head, hub->wrap, HUB_LOG_SLOTS - hub->pending);
    send_ack(hub, req, body, 8U);
}

static void req_cfg_read(hub_t *hub, const scp_packet_t *req)
{
    hub_device_t *d;
    char eui_hex[17];

    if (req->data_len != 8U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 8");
        return;
    }

    fmt_eui(req->data, eui_hex);
    d = hub_find_device(hub, req->data);

    if (hub->stale_0x20_next != 0U)
    {
        hub->stale_0x20_next = 0U;
        send_error(hub, req, ERR_NOT_AVAILABLE,
                   "taze veri yok (enjeksiyon) - tekrar sor");
        return;
    }

    if ((d == NULL) || (d->fresh == 0U))
    {
        send_error(hub, req, ERR_NOT_AVAILABLE,
                   (d == NULL) ? "cihaz envanterde degil" :
                                 "taze config toplanmadi");
        return;
    }

    hub_log(hub, "  CFG_READ [%s] -> 96 B block (cfg_crc=0x%04X)", eui_hex,
            hub_cfg_crc(d->block));
    send_ack(hub, req, d->block, HUB_BLOCK_LEN);
}

static void req_cfg_status(hub_t *hub, const scp_packet_t *req)
{
    uint8_t body[8];

    if (req->data_len != 1U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 1");
        return;
    }

    body[0] = hub->group_id;
    body[1] = hub->group_state;
    body[2] = hub->group_members;
    body[3] = hub->group_reason;
    wr16(&body[4], hub->group_crc);
    body[6] = 0U;
    body[7] = 0U;

    hub_log(hub, "  CFG_STATUS gid=%u -> state=%u members=0x%02X crc=0x%04X",
            req->data[0], hub->group_state, hub->group_members,
            hub->group_crc);
    send_ack(hub, req, body, 8U);
}

static void req_log_head(hub_t *hub, const scp_packet_t *req)
{
    uint8_t body[10];

    wr16(&body[0], hub->head);
    wr16(&body[2], hub->wrap);
    wr32(&body[4], hub->total_events);

    if (hub->r1_mode != 0U)
    {
        wr16(&body[8], hub->tail);
        hub_log(hub, "  LOG_HEAD (R1) -> head=%u wrap=%u total=%u tail=%u",
                hub->head, hub->wrap, hub->total_events, hub->tail);
        send_ack(hub, req, body, 10U);
    }
    else
    {
        hub_log(hub, "  LOG_HEAD (R0) -> head=%u wrap=%u total=%u (tail yok)",
                hub->head, hub->wrap, hub->total_events);
        send_ack(hub, req, body, 8U);
    }
}

static void req_log_record(hub_t *hub, const scp_packet_t *req)
{
    uint16_t idx;

    if (req->data_len != 2U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 2");
        return;
    }

    idx = rd16(req->data);
    if (idx >= HUB_LOG_SLOTS)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "index >= 100");
        return;
    }

    hub_log(hub, "  LOG_RECORD idx=%u -> 60 B (trigger=%u)", idx,
            hub->events[idx][7]);
    send_ack(hub, req, hub->events[idx], HUB_EVENT_LEN);
}

static void req_log_range(hub_t *hub, const scp_packet_t *req)
{
    uint16_t start;
    uint16_t count;

    if (req->data_len != 4U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 4");
        return;
    }

    start = rd16(&req->data[0]);
    count = rd16(&req->data[2]);

    if ((count == 0U) || (count > HUB_RANGE_MAX) ||
        ((uint32_t)start + (uint32_t)count > HUB_LOG_SLOTS))
    {
        send_error(hub, req, ERR_INVALID_PARAM,
                   "count=0 ya da >4 ya da aralik tasmasi");
        return;
    }

    hub_log(hub, "  LOG_RANGE start=%u count=%u -> %u B", start, count,
            count * HUB_EVENT_LEN);
    send_ack(hub, req, hub->events[start],
             (uint8_t)(count * HUB_EVENT_LEN));
}

static void req_inventory(hub_t *hub, const scp_packet_t *req)
{
    char eui_hex[17];

    if (req->cmd == CMD_INVENTORY_END)
    {
        if (req->data_len != 0U)
        {
            send_error(hub, req, ERR_INVALID_PARAM, "LEN != 0");
            return;
        }
        hub->inventory_loaded = 1U;
        hub_log(hub, "  INVENTORY_END -> envanter yuklendi (inventory_loaded=1)");
        send_ack(hub, req, NULL, 0U);
        return;
    }

    if (req->data_len != 12U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 12");
        return;
    }

    fmt_eui(&req->data[3], eui_hex);

    {
        hub_device_t *d = hub_find_device(hub, &req->data[3]);
        if (d == NULL)
        {
            size_t i;
            for (i = 0U; i < HUB_DEVICES_MAX; i++)
            {
                if (hub->dev[i].valid == 0U)
                {
                    d = &hub->dev[i];
                    break;
                }
            }
        }
        if (d == NULL)
        {
            send_error(hub, req, ERR_INVALID_PARAM, "envanter dolu");
            return;
        }

        if (d->valid == 0U)
        {
            (void)memset(d, 0, sizeof(*d));
            d->valid = 1U;
            (void)memcpy(d->eui, &req->data[3], 8U);
            d->zone = req->data[0];
            d->fider = req->data[1];
            d->phase = req->data[2];
            d->channel = req->data[11];
            block_default(d->block, d);
            hub_log(hub, "  INVENTORY_SET [%s] zone=%u fider=%u phase=%u "
                         "ch=%u -> yeni cihaz (taze config VAR)",
                    eui_hex, req->data[0], req->data[1], req->data[2],
                    req->data[11]);
            d->fresh = 1U; /* envantere girince cihaz bagli sayilir */
        }
        else
        {
            d->zone = req->data[0];
            d->fider = req->data[1];
            d->phase = req->data[2];
            d->channel = req->data[11];
            hub_log(hub, "  INVENTORY_SET [%s] guncellendi", eui_hex);
        }
    }
    send_ack(hub, req, NULL, 0U);
}

static void req_time_sync(hub_t *hub, const scp_packet_t *req)
{
    if (req->data_len != 7U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 7");
        return;
    }

    hub->time_synced = 1U;
    hub->synced_epoch_sec = cp56_to_epoch(req->data);
    hub->synced_uptime_ms = hub->now_ms;
    cp56_decode_log(hub, req->data);
    send_ack(hub, req, NULL, 0U);
}

static void req_cfg_write(hub_t *hub, const scp_packet_t *req)
{
    hub_device_t *d;
    char eui_hex[17];

    if (req->data_len != (8U + HUB_BLOCK_LEN))
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 104");
        return;
    }

    fmt_eui(req->data, eui_hex);
    d = hub_find_device(hub, req->data);
    if (d == NULL)
    {
        send_error(hub, req, ERR_INVALID_PARAM,
                   "EUI envanterde yok (sim basitlestirmesi)");
        return;
    }

    (void)memcpy(d->staged_block, &req->data[8], HUB_BLOCK_LEN);
    d->staged = 1U;

    hub->group_state = HUB_G_STAGED;
    hub->group_reason = REASON_NONE;
    hub->group_members = 0U;
    {
        size_t i;
        for (i = 0U; i < HUB_DEVICES_MAX; i++)
        {
            if ((hub->dev[i].valid != 0U) && (hub->dev[i].staged != 0U))
            {
                hub->group_members = (uint8_t)(hub->group_members |
                                               (uint8_t)(1U << i));
            }
        }
    }
    hub->group_crc = hub_cfg_crc(d->staged_block);

    hub_log(hub, "  CFG_WRITE [%s] -> STAGED (grup uyeleri=0x%02X "
                 "cfg_crc=0x%04X; uygulama 0x24 ile)",
            eui_hex, hub->group_members, hub->group_crc);
    send_ack(hub, req, NULL, 0U);
}

static void req_cfg_commit(hub_t *hub, const scp_packet_t *req)
{
    if (req->data_len != 1U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 1");
        return;
    }

    if (hub->group_state != HUB_G_STAGED)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "STAGED yazim yok");
        return;
    }

    hub->group_id = req->data[0];
    hub->group_state = HUB_G_DELIVERED;
    hub->apply_at_ms = hub->now_ms + HUB_APPLY_DELAY_MS;
    hub_log(hub, "  CFG_COMMIT gid=%u -> DELIVERED; %u ms sonra APPLIED "
                 "bildirilecek (0x21)", hub->group_id, HUB_APPLY_DELAY_MS);
    send_ack(hub, req, NULL, 0U);
}

static void req_cfg_abort(hub_t *hub, const scp_packet_t *req)
{
    size_t i;

    if (req->data_len != 1U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 1");
        return;
    }

    for (i = 0U; i < HUB_DEVICES_MAX; i++)
    {
        hub->dev[i].staged = 0U;
    }
    hub->group_state = HUB_G_IDLE;
    hub->group_reason = REASON_USER_ABORT;
    hub_log(hub, "  CFG_ABORT gid=%u -> staged temizlendi (kullanici iptali)",
            req->data[0]);
    send_ack(hub, req, NULL, 0U);
}

static void req_epoch(hub_t *hub, const scp_packet_t *req)
{
    if (req->data_len != 1U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 1");
        return;
    }

    if (hub->now_ms < hub->epoch_busy_until)
    {
        send_error(hub, req, ERR_BUSY, "kesif penceresi hala acik");
        return;
    }

    hub->epoch_busy_until = hub->now_ms + HUB_EPOCH_BUSY_MS;
    hub_log(hub, "  EPOCH_REFRESH fider=%u -> 3 faza yayin + %u sn kesif "
                 "penceresi (sim: %u sn)",
            req->data[0], HUB_EPOCH_BUSY_MS / 1000U, HUB_EPOCH_BUSY_MS / 1000U);
    send_ack(hub, req, NULL, 0U);
}

static void req_log_consume(hub_t *hub, const scp_packet_t *req)
{
    uint16_t x;
    uint8_t body[4];
    uint32_t written_abs;
    uint32_t tail_abs;
    uint32_t x_abs;

    if (req->data_len != 2U)
    {
        send_error(hub, req, ERR_INVALID_PARAM, "LEN != 2");
        return;
    }

    x = rd16(req->data);
    written_abs = ((uint32_t)hub->wrap * HUB_LOG_SLOTS) + hub->head;
    tail_abs = ((uint32_t)hub->wrap * HUB_LOG_SLOTS) + hub->tail;
    x_abs = ((uint32_t)hub->wrap * HUB_LOG_SLOTS) + x;

    /* wrap gecmedigi varsayimiyla mutlak karsilastirma (sim basitlestirmesi) */
    if ((x_abs < tail_abs) || (x_abs > written_abs))
    {
        send_error(hub, req, ERR_INVALID_PARAM,
                   "geriye gitme ya da head'i asma");
        return;
    }

    hub->pending = (uint16_t)(written_abs - x_abs);
    hub->tail = x;

    wr16(&body[0], hub->tail);
    wr16(&body[2], hub->pending);
    hub_log(hub, "  LOG_CONSUME x=%u -> tail=%u left=%u (taahhut islendi)",
            x, hub->tail, hub->pending);
    send_ack(hub, req, body, 4U);
}

/* ======================================================================
 * Paket dagitimi
 * ====================================================================== */

static void handle_get(hub_t *hub, const scp_packet_t *req)
{
    switch (req->cmd)
    {
        case CMD_GET_STATUS:   req_get_status(hub, req);  break;
        case CMD_GET_FRAM_STATS:req_get_fram(hub, req);   break;
        case CMD_CFG_READ:     req_cfg_read(hub, req);    break;
        case CMD_CFG_STATUS:   req_cfg_status(hub, req);  break;
        case CMD_LOG_HEAD:     req_log_head(hub, req);    break;
        case CMD_LOG_RECORD:   req_log_record(hub, req);  break;
        case CMD_LOG_RANGE:    req_log_range(hub, req);   break;
        default:
            send_error(hub, req, ERR_UNKNOWN_CMD, "bilinmeyen GET");
            break;
    }
}

static void handle_set(hub_t *hub, const scp_packet_t *req)
{
    switch (req->cmd)
    {
        case CMD_INVENTORY_SET:
        case CMD_INVENTORY_UPDATE:
        case CMD_INVENTORY_END: req_inventory(hub, req);  break;
        case CMD_TIME_SYNC:     req_time_sync(hub, req);  break;
        case CMD_CFG_WRITE:     req_cfg_write(hub, req);  break;
        case CMD_CFG_COMMIT:    req_cfg_commit(hub, req); break;
        case CMD_CFG_ABORT:     req_cfg_abort(hub, req);  break;
        case CMD_EPOCH:         req_epoch(hub, req);      break;
        case CMD_LOG_CONSUME:   req_log_consume(hub, req);break;
        case CMD_SET_CONFIG:    /* R1 3.2: stub */
            send_error(hub, req, ERR_NOT_SUPPORTED, "0x03 stub");
            break;
        default:
            send_error(hub, req, ERR_UNKNOWN_CMD, "bilinmeyen SET");
            break;
    }
}

static void handle_ping(hub_t *hub, const scp_packet_t *req)
{
    scp_packet_t ack;

    if (req->dst == SCP_BROADCAST_ADDR)
    {
        hub_log(hub, "  PING (broadcast) -> yanit YOK (R1 2.4)");
        return;
    }

    hub_log(hub, "  PING -> ACK");
    ack.dst = req->src;
    ack.src = HUB_ADDR;
    ack.type = SCP_TYPE_ACK;
    ack.cmd = req->cmd;
    ack.seq = req->seq;
    ack.data_len = 0U;
    send_pkt(hub, &ack);
}

void hub_on_packet(hub_t *hub, const scp_packet_t *pkt)
{
    /* Yanit YOK enjeksiyonu (RTU timeout/retry yolunu provaya alir) */
    if (hub->drop_next != 0U)
    {
        hub->drop_next = 0U;
        hub_log(hub, "  INJEKSIYON: istek YUTULDU (yanit yok) cmd=0x%02X "
                     "seq=%u", pkt->cmd, pkt->seq);
        return;
    }

    /* SET tekrar politikesi (R1 2.3d): ayni SEQ + araya istek girmeden */
    if ((pkt->type == SCP_TYPE_SET) && (hub->have_last_set != 0U) &&
        (pkt->seq == hub->last_set_seq) &&
        (hub->last_request_seq == pkt->seq))
    {
        hub_log(hub, "  DUP SET seq=%u -> son yanit AYNEN tekrarlanir "
                     "(idempotent)", pkt->seq);
        send_pkt(hub, &hub->last_reply);
        return;
    }

    hub_log(hub, "RX cmd=0x%02X type=0x%02X seq=%u len=%u",
            pkt->cmd, pkt->type, pkt->seq, pkt->data_len);

    /* bu SET'in yaniti tekrar icin yakalanir */
    if (pkt->type == SCP_TYPE_SET)
    {
        hub->capture_reply = 1U;
        hub->pending_set_seq = pkt->seq;
    }

    switch (pkt->type)
    {
        case SCP_TYPE_GET:  handle_get(hub, pkt); break;
        case SCP_TYPE_SET:  handle_set(hub, pkt); break;
        case SCP_TYPE_PING: handle_ping(hub, pkt);break;
        default:
            hub_log(hub, "  beklenmedik TYPE (yanit yok)");
            break;
    }

    /* tekrar-yanit kaydi: yalniz SET icin saklanir */
    hub->last_request_seq = pkt->seq;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void hub_init(hub_t *hub, hub_send_fn_t send, void *user, uint8_t r1_mode)
{
    (void)memset(hub, 0, sizeof(*hub));
    hub->send = send;
    hub->user = user;
    hub->r1_mode = r1_mode;
    hub->scp_major = 1U;
    hub->start_ms = 0U;
}

void hub_tick(hub_t *hub, uint32_t now_ms)
{
    hub->now_ms = now_ms;

    if ((hub->group_state == HUB_G_DELIVERED) &&
        ((int32_t)(now_ms - hub->apply_at_ms) >= 0))
    {
        uint8_t body[8];
        size_t i;
        uint8_t outcome_failed = hub->commit_fail;
        uint8_t reason = hub->commit_fail_reason;

        hub->commit_fail = 0U;

        if (outcome_failed == 0U)
        {
            for (i = 0U; i < HUB_DEVICES_MAX; i++)
            {
                hub_device_t *d = &hub->dev[i];
                if ((d->valid != 0U) && (d->staged != 0U))
                {
                    (void)memcpy(d->block, d->staged_block, HUB_BLOCK_LEN);
                    d->staged = 0U;
                    d->fresh = 1U;
                }
            }
            hub->group_state = HUB_G_APPLIED;
            hub->sched_cycles++;
            hub_log(hub, "APPLIED: gid=%u cfg_crc=0x%04X (0x21 gidiyor)",
                    hub->group_id, hub->group_crc);
        }
        else
        {
            size_t j;
            for (j = 0U; j < HUB_DEVICES_MAX; j++)
            {
                hub->dev[j].staged = 0U;
            }
            hub->group_state = HUB_G_FAILED;
            hub->group_reason = reason;
            hub_log(hub, "FAILED: gid=%u reason=%u (0x21 gidiyor)",
                    hub->group_id, reason);
        }

        body[0] = hub->group_id;
        body[1] = hub->group_state;
        body[2] = hub->group_members;
        body[3] = hub->group_reason;
        wr16(&body[4], hub->group_crc);
        body[6] = 0U;
        body[7] = 0U;
        send_proactive(hub, CMD_CFG_NOTIFY, body, 8U, "CFG_STATUS_NOTIFY");
    }
}

/* ======================================================================
 * Proaktif bildirimler + olay halkasi
 * ====================================================================== */

void hub_send_boot_notify(hub_t *hub)
{
    uint8_t body[1];
    body[0] = hub->scp_major;
    send_proactive(hub, CMD_BOOT, body, 1U, "BOOT_NOTIFY");
}

void hub_send_discovery(hub_t *hub)
{
    uint8_t body[9];
    static uint8_t disc_counter = 0U;

    disc_counter++;
    (void)memset(body, 0, sizeof(body));
    body[0] = 0xA4U; body[1] = 0x05U; body[2] = 0x67U; body[3] = 0x82U;
    body[4] = 0x1CU; body[5] = 0x3BU; body[6] = 0xD0U; body[7] = disc_counter;
    body[8] = (uint8_t)(200U - (uint8_t)(disc_counter * 2U)); /* rssi i8 */

    send_proactive(hub, CMD_DISCOVERY, body, 9U, "DISCOVERY_REPORT");
}

void hub_send_trip(hub_t *hub)
{
    uint8_t body[12];
    const hub_device_t *d = (hub->dev[0].valid != 0U) ? &hub->dev[0] : NULL;

    (void)memset(body, 0, sizeof(body));
    body[0] = 0x04U;                              /* msg_type (RF ici)  */
    body[1] = 4U;                                 /* OVERCURRENT        */
    body[2] = (d != NULL) ? d->zone : 1U;
    body[3] = (uint8_t)((uint8_t)(((d != NULL) ? d->fider : 1U) << 2) |
                         (uint8_t)((d != NULL) ? d->phase : 1U));
    body[4] = 1U;                                 /* fault_count        */
    body[5] = 0U;                                 /* flags              */
    wrf32(&body[6], 875.5f);                      /* max_fault_current  */
    wr16(&body[10], 250U);                        /* timestamp_ms       */

    send_proactive(hub, CMD_TRIP, body, 12U, "TRIP_NOTIFY");
}

void hub_send_live(hub_t *hub)
{
    uint8_t body[33];

    (void)memset(body, 0, sizeof(body));
    body[0] = 1U;                                 /* src (ayirici 1)    */
    wr32(&body[1], hub->now_ms);                  /* seq                */
    wr32(&body[5], (hub->now_ms - hub->start_ms) / 1000U); /* uptime     */
    body[9] = 1U;                                 /* Current_State      */
    body[10] = 0U;                                /* Fault_Count        */
    wrf32(&body[11], 6.2f);                       /* Live_Irms          */
    wrf32(&body[15], 32.1f);                      /* Live_V_Trip        */
    wrf32(&body[19], 3.4f);                       /* Live_V_Rec         */
    wr16(&body[23], 25);                           /* MCU_Temp i16       */

    body[25] = (uint8_t)(-70);                    /* RSSI i8            */
    body[26] = 0U;                                /* Status_Flags       */
    body[27] = 0U;                                /* FSM_Error           */
    body[28] = 0U;                                /* Trip_Failed         */
    body[29] = (uint8_t)hub->pending;             /* log_pending         */
    body[30] = (uint8_t)(hub->wrap & 0xFFU);      /* wrapped_low         */

    send_proactive(hub, CMD_LIVE, body, 33U, "LIVE_DATA");
}

void hub_send_anomaly(hub_t *hub)
{
    uint8_t body[10];

    (void)memset(body, 0, sizeof(body));
    body[0] = 1U;                                 /* src                 */
    body[1] = 2U;                                 /* state               */
    wr16(&body[2], 3U);                           /* win                 */
    wr32(&body[4], 12U);                          /* total               */
    body[8] = 0U;                                 /* path                */

    send_proactive(hub, CMD_ANOMALY, body, 10U, "ANOMALY_REPORT");
}

void hub_send_log_bell(hub_t *hub)
{
    uint8_t body[4];

    wr16(&body[0], hub->pending);
    wr16(&body[2], hub->head);
    send_proactive(hub, CMD_LOG_BELL, body, 4U, "LOG_AVAILABLE_NOTIFY");
}

void hub_add_events(hub_t *hub, uint8_t count, uint8_t trigger)
{
    uint8_t k;
    const hub_device_t *d = (hub->dev[0].valid != 0U) ? &hub->dev[0] : NULL;

    for (k = 0U; k < count; k++)
    {
        uint8_t *rec = hub->events[hub->head];

        (void)memset(rec, 0, HUB_EVENT_LEN);
        cp56_encode(&rec[0], hub_wall_sec(hub), (uint16_t)(k * 10U),
                    (uint8_t)((hub->time_synced != 0U) ? 1U : 2U));
        rec[7] = trigger;
        rec[8] = (d != NULL) ? d->zone : 1U;
        rec[9] = (d != NULL) ? d->fider : 1U;
        rec[10] = (d != NULL) ? d->phase : 1U;
        rec[11] = 1U;                              /* fault_count_state */
        rec[12] = 95U;                             /* soh_ratio         */
        rec[13] = 1U;                              /* nominal_status    */
        rec[14] = 1U;                              /* energy_status     */
        wrf32(&rec[15], 875.5f);                   /* max_fault_current */
        wrf32(&rec[19], 1200.0f);                  /* max_di_dt         */
        wr32(&rec[23], 45U);                       /* fault_duration_ms */
        wrf32(&rec[27], 31.9f);                    /* V_Trip            */
        wrf32(&rec[31], 3.5f);                     /* V_Harvest         */
        wrf32(&rec[35], 1.25f);                    /* Vref_Vagnd        */
        wr16(&rec[39], 250);                        /* mcu_temp i16      */
        wr16(&rec[41], 1U);                        /* permanent faults  */
        wr16(&rec[43], 2U);                        /* temporary faults  */
        wrf32(&rec[45], 95.0f);                    /* soh_idc           */
        wr16(&rec[49], 17U);                       /* boot_counter      */
        wr32(&rec[51], (hub->now_ms - hub->start_ms) / 1000U); /* uptime */
        rec[55] = (uint8_t)((hub->time_synced != 0U) ? 1U : 2U); /* clock_q */
        wr16(&rec[58], crc16_ccitt_false(rec, 58U)); /* kayit CRC'si    */

        hub->head = (uint16_t)((hub->head + 1U) % HUB_LOG_SLOTS);
        if (hub->head == 0U)
        {
            hub->wrap++;
        }
        hub->total_events++;
        hub->pending++;
    }

    hub_log(hub, "EVENT EKLENDI: %u adet (trigger=%u) -> head=%u pending=%u",
            count, trigger, hub->head, hub->pending);
}

/*** end of file ***/
