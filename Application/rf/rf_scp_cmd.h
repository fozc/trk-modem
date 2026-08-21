/*
 * rf_scp_cmd.h
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * SCP v1.0 application-layer command constants and body codecs
 * (struct <-> DATA[], little-endian). Pure and hardware-independent so the
 * layer is host-testable. S1 scope: 0x01 GET_STATUS.
 */

#ifndef RF_RF_SCP_CMD_H_
#define RF_RF_SCP_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "scp.h"                 /* scp_packet_t */

/* ---------------------------------------------------------------------------
 * Addresses (R0 section 2.3b)
 * --------------------------------------------------------------------------- */

#define RF_SCP_ADDR_BROADCAST   0x00U   /* Only valid in DST; no reply     */
#define RF_SCP_ADDR_HUB         0x01U   /* Modem_RF_Hub (peer)             */
#define RF_SCP_ADDR_RTU         0x02U   /* STM32 RTU (this node)           */

/* ---------------------------------------------------------------------------
 * Command codes (R0 section 3) - S1 subset; grow phase by phase
 * --------------------------------------------------------------------------- */

#define RF_SCP_CMD_GET_STATUS   0x01U   /* GET -> ACK + 25 B (R0 3.3.1)    */

/* ---------------------------------------------------------------------------
 * Standard error codes carried in DATA[0] of ERROR packets (R0 section 9.1)
 * --------------------------------------------------------------------------- */

#define RF_SCP_ERR_UNKNOWN_CMD    0x01U
#define RF_SCP_ERR_INVALID_PARAM  0x02U
#define RF_SCP_ERR_BUSY           0x03U
#define RF_SCP_ERR_NOT_SUPPORTED  0x04U
#define RF_SCP_ERR_NOT_AVAILABLE  0x05U /* "try again later" - not permanent */

/* ---------------------------------------------------------------------------
 * 0x01 GET_STATUS ACK body layout (R0 section 3.3.1) - 25 bytes
 * --------------------------------------------------------------------------- */

#define RF_SCP_STATUS_BODY_LEN    25U
#define RF_SCP_FW_LABEL_LEN       16U   /* char[16]; source [19] always NUL */

typedef struct
{
    uint32_t uptime_sec;                    /* Offset 0-3   */
    char     fw_version[RF_SCP_FW_LABEL_LEN]; /* Offset 4-19; [15] always NUL */
    uint8_t  sched_active;                  /* Offset 20    */
    uint32_t sched_cycle_count;             /* Offset 21-24 */
} rf_hub_status_t;

typedef enum
{
    RF_CMD_OK = 0,
    RF_CMD_ERR_NULL,
    RF_CMD_ERR_LEN                          /* body not the expected length */
} rf_cmd_status_t;

/**
 * @brief Decode a 0x01 GET_STATUS ACK body (25 B) into a status struct.
 *
 * @param[in]  packet  Decoded ACK packet (DATA holds the 25-byte body).
 * @param[out] out     Destination status structure.
 *
 * @return RF_CMD_OK on success, RF_CMD_ERR_NULL on NULL argument,
 *         RF_CMD_ERR_LEN if data_len != 25.
 *
 * @note Thread-safe: yes (no shared state; operates only on arguments).
 */
rf_cmd_status_t rf_scp_decode_status(const scp_packet_t *packet,
                                     rf_hub_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_SCP_CMD_H_ */

/*** end of file ***/
