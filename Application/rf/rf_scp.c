/*
 * rf_scp.c
 *
 *  Created on: Aug 21, 2026
 *      Author: fatih
 *
 * SCP v1.0 wire codec: request builders + response decoders. Pure and
 * hardware-independent so it is host-testable. All multi-byte wire fields
 * are little-endian.
 */

#include "rf_scp.h"
#include <string.h>

bool rf_scp_build_request(rf_operation_t operation, uint8_t step,
                          uint8_t seq, const uint8_t *body,
                          uint8_t body_len, scp_packet_t *out)
{
    if (NULL == out)
    {
        return false;
    }

    out->dst = RF_SCP_ADDR_HUB;
    out->src = RF_SCP_ADDR_RTU;
    out->seq = seq;

    switch (operation)
    {
        case RF_OP_GET_STATUS:
            if ((0U == step) && (0U == body_len))
            {
                out->type     = SCP_TYPE_GET;
                out->cmd      = RF_SCP_CMD_GET_STATUS;
                out->data_len = 0U;
                return true;
            }
            return false;   /* single step; nothing after step 0 */

        case RF_OP_TIME_SYNC:
            if ((0U == step) && (RF_SCP_TIME_SYNC_BODY_LEN == body_len) &&
                (NULL != body))
            {
                out->type     = SCP_TYPE_SET;
                out->cmd      = RF_SCP_CMD_TIME_SYNC;
                out->data_len = RF_SCP_TIME_SYNC_BODY_LEN;
                (void)memcpy(out->data, body, RF_SCP_TIME_SYNC_BODY_LEN);
                return true;
            }
            return false;   /* wrong step or body length */

        default:            /* MISRA 16.4 */
            return false;
    }
}

bool rf_scp_build_ping_reply(const scp_packet_t *ping, scp_packet_t *ack)
{
    if ((NULL == ping) || (NULL == ack))
    {
        return false;
    }

    if (RF_SCP_ADDR_BROADCAST == ping->dst)
    {
        return false;   /* R0 2.4: broadcast PING gets no reply */
    }

    ack->dst      = ping->src;
    ack->src      = RF_SCP_ADDR_RTU;
    ack->type     = SCP_TYPE_ACK;
    ack->cmd      = ping->cmd;
    ack->seq      = ping->seq;
    ack->data_len = 0U;
    return true;
}

rf_cmd_status_t rf_scp_decode_status(const scp_packet_t *packet,
                                     rf_hub_status_t *out)
{
    if ((NULL == packet) || (NULL == out))
    {
        return RF_CMD_ERR_NULL;
    }

    if (RF_SCP_STATUS_BODY_LEN != packet->data_len)
    {
        return RF_CMD_ERR_LEN;   /* validate at the boundary, trust inside */
    }

    const uint8_t *body = packet->data;

    out->uptime_sec = (uint32_t)body[0]
                    | ((uint32_t)body[1] << 8U)
                    | ((uint32_t)body[2] << 16U)
                    | ((uint32_t)body[3] << 24U);

    /* fw_version: char[16]; source byte [19] is always NUL per spec, but
     * force termination defensively so a malformed peer cannot leak. */
    (void)memcpy(out->fw_version, &body[4], RF_SCP_FW_LABEL_LEN);
    out->fw_version[RF_SCP_FW_LABEL_LEN - 1U] = '\0';

    out->sched_active = body[20];

    out->sched_cycle_count = (uint32_t)body[21]
                           | ((uint32_t)body[22] << 8U)
                           | ((uint32_t)body[23] << 16U)
                           | ((uint32_t)body[24] << 24U);

    return RF_CMD_OK;
}

/*** end of file ***/
