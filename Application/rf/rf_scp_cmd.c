/*
 * rf_scp_cmd.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * SCP v1.0 application-layer body codecs. Pure, hardware-independent so the
 * layer is host-testable. All multi-byte wire fields are little-endian.
 */

#include "rf_scp_cmd.h"
#include <string.h>

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
