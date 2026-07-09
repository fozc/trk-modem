/**
 * @file scp.c
 * @brief SCP v1.0 unified implementation — CRC, packet encode/decode, parser.
 * @version 1.0.0
 * @author Fatih Ozcan
 */

#include "scp.h"
#include "scp_endian.h"
#include "cobs.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal constants
 * --------------------------------------------------------------------------- */

#define OFFSET_DST   0U
#define OFFSET_SRC   1U
#define OFFSET_TYPE  2U
#define OFFSET_CMD   3U
#define OFFSET_SEQ   4U
#define OFFSET_LEN   5U
#define OFFSET_NLEN  6U
#define OFFSET_DATA  7U

#define CRC16_INIT   0xFFFFU
#define CRC16_POLY   0x1021U

/* ---------------------------------------------------------------------------
 * CRC-16/CCITT-FALSE (internal)
 * --------------------------------------------------------------------------- */

static uint16_t calc_crc16(const uint8_t *p_data, size_t length)
{
    uint16_t crc = CRC16_INIT;

    for (size_t i = 0U; i < length; i++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint32_t)p_data[i] << 8U));

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)(((uint32_t)crc << 1U) ^ (uint32_t)CRC16_POLY);
            }
            else
            {
                crc = (uint16_t)((uint32_t)crc << 1U);
            }
        }
    }

    return crc;
}

/* ---------------------------------------------------------------------------
 * Internal packet decode
 * --------------------------------------------------------------------------- */

static bool decode_packet(uint8_t *p_cobs,
                          size_t cobs_len,
                          uint8_t device_address,
                          scp_packet_t *p_packet)
{
    size_t decoded_len = 0U;

    /* Decode COBS in-place — safe because write_idx <= read_idx throughout */
    if (!cobs_decode_inplace(p_cobs, cobs_len, &decoded_len))
    {
        return false;
    }

    /* Minimum length: header(7) + CRC(2) = 9 */
    if (decoded_len < SCP_OVERHEAD_SIZE)
    {
        return false;
    }

    /* Early address check — skip CRC computation if not for us */
    uint8_t dst = p_cobs[OFFSET_DST];
    bool is_for_me = (dst == device_address) ||
                     (dst == SCP_BROADCAST_ADDR);

    if (!is_for_me)
    {
        return false;
    }

    uint8_t len  = p_cobs[OFFSET_LEN];
    uint8_t nlen = p_cobs[OFFSET_NLEN];

    /* Header check: LEN + ~LEN == 0xFF */
    if (((uint16_t)len + (uint16_t)nlen) != 0x00FFU)
    {
        return false;
    }

    /* Length validation: decoded_len == LEN + 9 */
    if (decoded_len != ((size_t)len + SCP_OVERHEAD_SIZE))
    {
        return false;
    }

    if (len > SCP_MAX_DATA_SIZE)
    {
        return false;
    }

    /* CRC-16 verification */
    size_t   crc_scope = (size_t)SCP_HEADER_SIZE + (size_t)len;
    uint16_t computed  = calc_crc16(p_cobs, crc_scope);
    uint16_t received  = scp_unpack_u16(&p_cobs[crc_scope]);

    if (computed != received)
    {
        return false;
    }

    /* Extract fields */
    p_packet->dst      = dst;
    p_packet->src      = p_cobs[OFFSET_SRC];
    p_packet->type     = p_cobs[OFFSET_TYPE];
    p_packet->cmd      = p_cobs[OFFSET_CMD];
    p_packet->seq      = p_cobs[OFFSET_SEQ];
    p_packet->data_len = len;

    if (len > 0U)
    {
        (void)memcpy(p_packet->data, &p_cobs[OFFSET_DATA], (size_t)len);
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

scp_status_t scp_init(scp_t *p_ctx,
                     uint8_t device_address,
                     scp_transmit_fn_t transmit,
                     scp_get_tick_fn_t get_tick,
                     uint32_t timeout_ms)
{
    if (NULL == p_ctx)
    {
        return SCP_STATUS_ERROR_NULL_PTR;
    }

    (void)memset(p_ctx, 0, sizeof(scp_t));
    p_ctx->device_address     = device_address;
    p_ctx->rx_state           = SCP_STATE_WAIT_SOF;
    p_ctx->transmit           = transmit;
    p_ctx->get_tick           = get_tick;
    p_ctx->timeout_ms         = timeout_ms;

    return SCP_STATUS_OK;
}

void scp_process_byte(scp_t *p_ctx, uint8_t byte)
{
    if (NULL == p_ctx)
    {
        return;
    }

    /* --- Inter-byte timeout check --- */
    if ((p_ctx->get_tick != NULL) && (p_ctx->timeout_ms > 0U))
    {
        uint32_t now = p_ctx->get_tick();

        if ((p_ctx->rx_state == SCP_STATE_COLLECT) && (p_ctx->rx_idx > 0U))
        {
            uint32_t elapsed = now - p_ctx->last_rx_tick;

            if (elapsed >= p_ctx->timeout_ms)
            {
                /* Stale partial frame — discard and resync */
                p_ctx->rx_idx   = 0U;
                p_ctx->rx_state = SCP_STATE_WAIT_SOF;
            }
        }

        p_ctx->last_rx_tick = now;
    }

    switch (p_ctx->rx_state)
    {
        case SCP_STATE_WAIT_SOF:
        {
            if (byte == SCP_DELIMITER)
            {
                p_ctx->rx_idx   = 0U;
                p_ctx->rx_state = SCP_STATE_COLLECT;
            }
            break;
        }

        case SCP_STATE_COLLECT:
        {
            if (byte == SCP_DELIMITER)
            {
                if (p_ctx->rx_idx > 0U)
                {
                    if (decode_packet(p_ctx->rx_buf, p_ctx->rx_idx,
                                      p_ctx->device_address,
                                      &p_ctx->rx_packet))
                    {
                        p_ctx->rx_state = SCP_STATE_PACKET_READY;
                        return;  /* Do not reset rx_idx — frozen until done */
                    }
                }

                /* Invalid or empty frame — this 0x00 is SOF for next frame */
                p_ctx->rx_idx = 0U;
            }
            else
            {
                if (p_ctx->rx_idx < SCP_COBS_MAX_SIZE)
                {
                    p_ctx->rx_buf[p_ctx->rx_idx] = byte;
                    p_ctx->rx_idx++;
                }
                else
                {
                    /* Overflow — stay in COLLECT; next 0x00 starts a new frame */
                    p_ctx->rx_idx = 0U;
                }
            }
            break;
        }

        case SCP_STATE_PACKET_READY:
        {
            /* All incoming bytes are silently discarded until
             * the application calls scp_packet_done(). */
            break;
        }

        default:
        {
            p_ctx->rx_state = SCP_STATE_WAIT_SOF;
            p_ctx->rx_idx   = 0U;
            break;
        }
    }
}

scp_status_t scp_send(scp_t *p_ctx, const scp_packet_t *p_packet)
{
    if ((NULL == p_ctx) || (NULL == p_packet))
    {
        return SCP_STATUS_ERROR_NULL_PTR;
    }

    if (p_packet->data_len > SCP_MAX_DATA_SIZE)
    {
        return SCP_STATUS_ERROR_INVALID_PARAM;
    }

    /* --- Build logical packet --- */
    uint8_t logical[SCP_PACKET_MAX_SIZE];
    size_t  logical_len = (size_t)SCP_HEADER_SIZE
                        + (size_t)p_packet->data_len
                        + (size_t)SCP_CRC_SIZE;

    logical[OFFSET_DST]  = p_packet->dst;
    logical[OFFSET_SRC]  = p_packet->src;
    logical[OFFSET_TYPE] = p_packet->type;
    logical[OFFSET_CMD]  = p_packet->cmd;
    logical[OFFSET_SEQ]  = p_packet->seq;
    logical[OFFSET_LEN]  = p_packet->data_len;
    logical[OFFSET_NLEN] = (uint8_t)(~p_packet->data_len);

    if (p_packet->data_len > 0U)
    {
        (void)memcpy(&logical[OFFSET_DATA],
                     p_packet->data,
                     (size_t)p_packet->data_len);
    }

    /* --- CRC-16 --- */
    size_t   crc_scope = (size_t)SCP_HEADER_SIZE + (size_t)p_packet->data_len;
    uint16_t crc       = calc_crc16(logical, crc_scope);

    scp_pack_u16(&logical[crc_scope], crc);

    /* --- COBS encode into local frame buffer (after SOF slot) --- */
    uint8_t frame[SCP_FRAME_MAX_SIZE];
    size_t  cobs_len = 0U;

    if (!cobs_encode(logical, logical_len,
                     &frame[1], SCP_COBS_MAX_SIZE,
                     &cobs_len))
    {
        return SCP_STATUS_ERROR_ENCODE_FAIL;
    }

    /* --- SOF + EOF --- */
    frame[0]            = SCP_DELIMITER;
    frame[1U + cobs_len] = SCP_DELIMITER;

    size_t frame_len = 1U + cobs_len + 1U;

    if (p_ctx->transmit != NULL)
    {
        p_ctx->transmit(frame, frame_len);
    }

    return SCP_STATUS_OK;
}

scp_status_t scp_send_ping(scp_t *p_ctx, uint8_t dst, uint8_t seq)
{
    if (NULL == p_ctx)
    {
        return SCP_STATUS_ERROR_NULL_PTR;
    }

    scp_packet_t ping = {0};
    ping.dst      = dst;
    ping.src      = p_ctx->device_address;
    ping.type     = SCP_TYPE_PING;
    ping.cmd      = 0x00U;
    ping.seq      = seq;
    ping.data_len = 0U;

    return scp_send(p_ctx, &ping);
}

bool scp_packet_ready(const scp_t *p_ctx)
{
    if (NULL == p_ctx)
    {
        return false;
    }

    return (p_ctx->rx_state == SCP_STATE_PACKET_READY);
}

const scp_packet_t *scp_get_packet(const scp_t *p_ctx)
{
    if (NULL == p_ctx)
    {
        return NULL;
    }

    if (p_ctx->rx_state != SCP_STATE_PACKET_READY)
    {
        return NULL;
    }

    return &p_ctx->rx_packet;
}

void scp_packet_done(scp_t *p_ctx)
{
    if (NULL != p_ctx)
    {
        p_ctx->rx_state = SCP_STATE_WAIT_SOF;
        p_ctx->rx_idx   = 0U;
    }
}
