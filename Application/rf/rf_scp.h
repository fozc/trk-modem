/*
 * rf_scp.h
 *
 *  Created on: Aug 21, 2026
 *      Author: fatih
 *
 * SCP v1.0 wire codec for the RF hub link: constants, request builders and
 * response decoders. Pure and hardware-independent (no state, no callbacks,
 * no time) so it is trivially host-testable and reusable.
 */

#ifndef RF_RF_SCP_H_
#define RF_RF_SCP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "scp.h"                 /* scp_packet_t, SCP_TYPE_* */

/* ---------------------------------------------------------------------------
 * Addresses (R0 section 2.3b)
 * --------------------------------------------------------------------------- */

#define RF_SCP_ADDR_BROADCAST   0x00U   /* Only valid in DST; no reply     */
#define RF_SCP_ADDR_HUB         0x01U   /* Modem_RF_Hub (peer)             */
#define RF_SCP_ADDR_RTU         0x02U   /* STM32 RTU (this node)           */

/* ---------------------------------------------------------------------------
 * Command codes (R0 section 3) - grow phase by phase
 * --------------------------------------------------------------------------- */

/* Requests we send (GET/SET) - reply is ACK/ERROR */
#define RF_SCP_CMD_GET_STATUS        0x01U  /* GET -> ACK + 25 B (R0 3.3.1) */
#define RF_SCP_CMD_TIME_SYNC         0x07U  /* SET <- CP56Time2a 7 B (R0 8.1) */
#define RF_SCP_CMD_INVENTORY_SET     0x04U  /* SET <- 12 B (R0 3.2) */
#define RF_SCP_CMD_INVENTORY_END     0x05U  /* SET <- 0 B (R0 3.2) */

/** CP56Time2a body length for 0x07 TIME_SYNC (R0 section 8.1). */
#define RF_SCP_TIME_SYNC_BODY_LEN    7U

/** Body length for 0x04 INVENTORY_SET (R0 section 3.2). */
#define RF_SCP_INV_SET_BODY_LEN     12U

/* Proactive SETs the hub sends unsolicited (R0 table C) */
#define RF_SCP_CMD_TRIP_NOTIFY       0x10U
#define RF_SCP_CMD_LIVE_DATA         0x11U
#define RF_SCP_CMD_ANOMALY_REPORT    0x12U
#define RF_SCP_CMD_BOOT_NOTIFY       0x13U
#define RF_SCP_CMD_DISCOVERY_REPORT  0x14U
#define RF_SCP_CMD_CFG_STATUS_NOTIFY 0x21U
#define RF_SCP_CMD_LOG_AVAILABLE     0x47U

/* ---------------------------------------------------------------------------
 * Standard error codes carried in DATA[0] of ERROR packets (R0 section 9.1)
 * --------------------------------------------------------------------------- */

#define RF_SCP_ERR_UNKNOWN_CMD    0x01U   /* permanent */
#define RF_SCP_ERR_INVALID_PARAM  0x02U   /* permanent */
#define RF_SCP_ERR_BUSY           0x03U
#define RF_SCP_ERR_NOT_SUPPORTED  0x04U   /* permanent */
#define RF_SCP_ERR_NOT_AVAILABLE  0x05U   /* transient - "try again later"  */

/* ---------------------------------------------------------------------------
 * Logical operations (a request/response exchange, possibly multi-step)
 * --------------------------------------------------------------------------- */

typedef enum
{
    RF_OP_NONE = 0,
    RF_OP_GET_STATUS,
    RF_OP_TIME_SYNC
} rf_operation_t;

/* ---------------------------------------------------------------------------
 * 0x01 GET_STATUS ACK body layout (R0 section 3.3.1) - 25 bytes
 * --------------------------------------------------------------------------- */

#define RF_SCP_STATUS_BODY_LEN    25U
#define RF_SCP_FW_LABEL_LEN       16U   /* char[16]; source [19] always NUL */

typedef struct
{
    uint32_t uptime_sec;                      /* Offset 0-3   */
    char     fw_version[RF_SCP_FW_LABEL_LEN]; /* Offset 4-19; [15] always NUL */
    uint8_t  sched_active;                    /* Offset 20    */
    uint32_t sched_cycle_count;               /* Offset 21-24 */
} rf_hub_status_t;

typedef enum
{
    RF_CMD_OK = 0,
    RF_CMD_ERR_NULL,
    RF_CMD_ERR_LEN                            /* body not the expected length */
} rf_cmd_status_t;

/* ---------------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------------- */

/**
 * @brief Build the request packet for one step of an operation.
 *
 * @param[in]  operation  Operation to encode.
 * @param[in]  step       Step index within the operation (0-based).
 * @param[in]  seq        Sequence number to stamp.
 * @param[in]  body       Request body (NULL for bodyless requests).
 * @param[in]  body_len   Body length; must match the operation's expected
 *                        length (0 for bodyless).
 * @param[out] out        Destination packet.
 *
 * @return true if (operation, step) maps to a request and the body length
 *         is correct; false otherwise (no such step, unknown operation,
 *         body length mismatch).
 */
bool rf_scp_build_request(rf_operation_t operation, uint8_t step,
                          uint8_t seq, const uint8_t *body,
                          uint8_t body_len, scp_packet_t *out);

/**
 * @brief Build the ACK reply for a received PING.
 *
 * @param[in]  ping  Received PING packet.
 * @param[out] ack   Destination ACK packet.
 *
 * @return true if a reply must be sent; false for a broadcast PING
 *         (R0 2.4: broadcast PING is not answered).
 */
bool rf_scp_build_ping_reply(const scp_packet_t *ping, scp_packet_t *ack);

/**
 * @brief Decode a 0x01 GET_STATUS ACK body (25 B) into a status struct.
 *
 * @param[in]  packet  Decoded ACK packet (DATA holds the 25-byte body).
 * @param[out] out     Destination status structure.
 *
 * @return RF_CMD_OK on success, RF_CMD_ERR_NULL on NULL argument,
 *         RF_CMD_ERR_LEN if data_len != 25.
 */
rf_cmd_status_t rf_scp_decode_status(const scp_packet_t *packet,
                                     rf_hub_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_SCP_H_ */

/*** end of file ***/
