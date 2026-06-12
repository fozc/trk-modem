/**
 * @file scp.h
 * @brief SCP v1.0 — full-duplex protocol handler with single context.
 * @version 1.0.0
 *
 * Provides packet encoding (TX) and byte-by-byte decoding (RX) through
 * a unified scp_t context.  Decoded packets are held in the context until
 * the application retrieves them via scp_get_packet() and releases the slot
 * with scp_packet_done().
 */

#ifndef SCP_H
#define SCP_H

/* ---------------------------------------------------------------------------
 * Library Version
 * --------------------------------------------------------------------------- */

#define SCP_VERSION_MAJOR     1U
#define SCP_VERSION_MINOR     0U
#define SCP_VERSION_PATCH     0U
#define SCP_VERSION_STRING    "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Protocol Constants
 * --------------------------------------------------------------------------- */

#define SCP_MAX_DATA_SIZE     244U   /**< Maximum payload per packet */
#define SCP_HEADER_SIZE       7U     /**< DST+SRC+TYPE+CMD+SEQ+LEN+~LEN */
#define SCP_CRC_SIZE          2U     /**< CRC-16 little-endian */
#define SCP_PACKET_MAX_SIZE   253U   /**< Header + max data + CRC */
#define SCP_COBS_MAX_SIZE     254U   /**< Max COBS-encoded payload */
#define SCP_FRAME_MAX_SIZE    256U   /**< SOF + COBS + EOF */
#define SCP_OVERHEAD_SIZE     9U     /**< Header(7) + CRC(2) */

#define SCP_DELIMITER         0x00U
#define SCP_BROADCAST_ADDR    0x00U

/* TYPE field */
#define SCP_TYPE_GET          0x01U
#define SCP_TYPE_SET          0x02U
#define SCP_TYPE_ACK          0x03U
#define SCP_TYPE_ERROR        0x04U
#define SCP_TYPE_PING         0x05U  /**< Heartbeat request — reply with ACK */

/* Standard error codes (DATA[0] in ERROR packets) */
#define SCP_ERR_UNKNOWN_CMD   0x01U
#define SCP_ERR_INVALID_PARAM 0x02U
#define SCP_ERR_BUSY          0x03U
#define SCP_ERR_NOT_SUPPORTED 0x04U

/* ---------------------------------------------------------------------------
 * Status
 * --------------------------------------------------------------------------- */

typedef enum {
    SCP_STATUS_OK = 0,
    SCP_STATUS_ERROR_NULL_PTR,
    SCP_STATUS_ERROR_INVALID_PARAM,
    SCP_STATUS_ERROR_ENCODE_FAIL
} scp_status_t;

/* ---------------------------------------------------------------------------
 * Packet
 * --------------------------------------------------------------------------- */

typedef struct {
    uint8_t dst;
    uint8_t src;
    uint8_t type;
    uint8_t cmd;
    uint8_t seq;
    uint8_t data_len;
    uint8_t data[SCP_MAX_DATA_SIZE];
} scp_packet_t;

/* ---------------------------------------------------------------------------
 * Callbacks
 * --------------------------------------------------------------------------- */

/** HAL dispatch: write the encoded frame bytes to the physical link (e.g. UART TX). */
typedef void (*scp_transmit_fn_t)(const uint8_t *p_frame, size_t frame_len);

/** Return system uptime in milliseconds (e.g. HAL_GetTick). Must be ISR-safe. */
typedef uint32_t (*scp_get_tick_fn_t)(void);

/* ---------------------------------------------------------------------------
 * Context
 * --------------------------------------------------------------------------- */

typedef enum {
    SCP_STATE_WAIT_SOF = 0,
    SCP_STATE_COLLECT,
    SCP_STATE_PACKET_READY   /**< Valid packet decoded — waiting for app to consume */
} scp_state_t;

typedef struct {
    uint8_t         device_address;
    scp_state_t     rx_state;
    uint8_t         rx_buf[SCP_COBS_MAX_SIZE];
    size_t          rx_idx;
    scp_packet_t    rx_packet;     /**< Decoded packet */
    uint32_t        timeout_ms;    /**< Inter-byte timeout; 0 = disabled */
    uint32_t        last_rx_tick;  /**< Tick of last received byte */
    scp_transmit_fn_t transmit;
    scp_get_tick_fn_t get_tick;    /**< System tick source (may be NULL) */
} scp_t;

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialise the SCP context.
 *
 * @param[out] p_ctx           Context to initialise.
 * @param[in]  device_address  This node's address (0x01–0xFE).
 * @param[in]  transmit        Called with encoded frame to transmit (may be NULL).
 * @param[in]  get_tick        Returns system uptime in ms (may be NULL to disable timeout).
 * @param[in]  timeout_ms      Inter-byte timeout in ms; 0 = disabled.
 *
 * @return SCP_STATUS_OK on success.
 */
scp_status_t scp_init(scp_t *p_ctx,
                     uint8_t device_address,
                     scp_transmit_fn_t transmit,
                     scp_get_tick_fn_t get_tick,
                     uint32_t timeout_ms);

/**
 * @brief Feed one received byte into the parser.
 *
 * Call from UART RX ISR or polling loop.  When a complete and valid packet
 * whose DST matches device_address (or broadcast) is decoded, the state
 * transitions to SCP_STATE_PACKET_READY.  In that state all incoming bytes
 * are silently discarded until scp_packet_done() is called.
 *
 * @param[in,out] p_ctx  Context.
 * @param[in]     byte   Received byte.
 */
void scp_process_byte(scp_t *p_ctx, uint8_t byte);

/**
 * @brief Check whether a decoded packet is waiting.
 *
 * @param[in] p_ctx  Context.
 * @return true if a packet is available (state == PACKET_READY).
 */
bool scp_packet_ready(const scp_t *p_ctx);

/**
 * @brief Get a pointer to the pending received packet.
 *
 * Returns NULL when no packet is pending.  The pointer remains valid until
 * scp_packet_done() is called.
 *
 * @param[in] p_ctx  Context.
 * @return Pointer to the packet, or NULL.
 */
const scp_packet_t *scp_get_packet(const scp_t *p_ctx);

/**
 * @brief Release the current packet and resume receiving.
 *
 * Transitions the parser from PACKET_READY back to WAIT_SOF so that
 * scp_process_byte() can accept new frames.
 *
 * Typical main-loop usage:
 * @code
 *   if (scp_packet_ready(&ctx))
 *   {
 *       const scp_packet_t *p = scp_get_packet(&ctx);
 *       app_handle(p);
 *       scp_packet_done(&ctx);
 *   }
 * @endcode
 *
 * @param[in,out] p_ctx  Context.
 */
void scp_packet_done(scp_t *p_ctx);

/**
 * @brief Encode and send a packet.
 *
 * Builds the logical packet, computes CRC-16, applies COBS encoding, wraps
 * in SOF/EOF, writes to the internal tx_buf, then calls transmit.
 *
 * @param[in,out] p_ctx    Context.
 * @param[in]     p_packet Packet to send (all fields must be set by caller).
 *
 * @return SCP_STATUS_OK on success.
 */
scp_status_t scp_send(scp_t *p_ctx, const scp_packet_t *p_packet);

/**
 * @brief Send a PING (heartbeat) packet to the specified destination.
 *
 * Builds and sends a zero-payload PING packet. The remote node is expected
 * to reply with an ACK carrying the same CMD and SEQ values.
 *
 * @param[in,out] p_ctx  Context.
 * @param[in]     dst    Destination address (0x01–0xFE).
 * @param[in]     seq    Sequence number for request-response matching.
 *
 * @return SCP_STATUS_OK on success.
 */
scp_status_t scp_send_ping(scp_t *p_ctx, uint8_t dst, uint8_t seq);

#ifdef __cplusplus
}
#endif

#endif /* SCP_H */
