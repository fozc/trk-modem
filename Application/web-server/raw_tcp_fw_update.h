/*
 * raw_tcp_fw_update.h
 *
 * RFWU: Raw Firmware Update Protocol over TCP port 80.
 *
 * Packet layout (all multi-byte fields little-endian):
 *
 *   [0..3]               magic     uint32_t  0x55574652  ("RFWU" on wire)
 *   [4]                  cmd       uint8_t   rfwu_cmd_t / rfwu_resp_t
 *   [5]                  flags     uint8_t   reserved, must be 0
 *   [6..7]               data_len  uint16_t  byte count of payload
 *   [8..11]              seq       uint32_t  sequence number (informational)
 *   [12..12+data_len-1]  data               payload
 *   [12+data_len..15+data_len]  crc32       CRC-32 of header + data
 *
 * CMD_DATA payload:     offset(4 LE) + firmware_bytes(1..1024)
 * CMD_HELLO payload:    total_size(4 LE) + file_hash(4 LE) + auth_token(4 LE)
 * RESP_STATUS payload:  active(1) + write_head(4 LE) + nv_total(4 LE)
 *                       + nv_received(4 LE) + nv_hash(4 LE)  [17 bytes total]
 */

#ifndef APPLICATION_WEB_SERVER_RAW_TCP_FW_UPDATE_H_
#define APPLICATION_WEB_SERVER_RAW_TCP_FW_UPDATE_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol constants ─────────────────────────────────────────── */

/*
 * Wire bytes: 0x52 0x46 0x57 0x55 ("RFWU").
 * Stored as LE uint32: 0x55574652.
 * Does not match the start of any HTTP method ("GET ", "POST", etc.).
 */
#define RFWU_MAGIC             0x55574652U

#define RFWU_HEADER_SIZE       12U
#define RFWU_CRC_SIZE           4U
#define RFWU_MAX_CHUNK_SIZE  1024U

/* CMD_DATA payload = offset(4) + firmware_data(max 1024) */
#define RFWU_MAX_DATA_LEN    (4U + RFWU_MAX_CHUNK_SIZE)

/* Maximum complete packet byte count */
#define RFWU_MAX_PACKET_SIZE (RFWU_HEADER_SIZE + RFWU_MAX_DATA_LEN + RFWU_CRC_SIZE)

/* Marks a valid session in NVRAM */
#define RFWU_SESSION_MAGIC   0xFEEDFACEU

/* Default shared key — change in production */
#define RFWU_DEFAULT_SHARED_KEY 0x534D4152U  /* "SMAR" */

/* ── Command / response codes ───────────────────────────────────── */

/** Commands sent by the client to the MCU */
typedef enum {
    RFWU_CMD_HELLO  = 0x01U,   /**< Start or resume a transfer      */
    RFWU_CMD_DATA   = 0x02U,   /**< One firmware chunk              */
    RFWU_CMD_FINISH = 0x03U,   /**< All chunks sent, finalise       */
    RFWU_CMD_QUERY  = 0x04U,   /**< Query current session state     */
    RFWU_CMD_REBOOT = 0x05U,   /**< Request device reboot           */
    RFWU_CMD_ABORT  = 0x06U,   /**< Abort transfer, clear session   */
} rfwu_cmd_t;

/** Responses sent by the MCU to the client */
typedef enum {
    RFWU_RESP_ACK    = 0x81U,  /**< Command accepted                */
    RFWU_RESP_NACK   = 0x82U,  /**< Command rejected (with reason)  */
    RFWU_RESP_STATUS = 0x83U,  /**< Reply to CMD_QUERY              */
} rfwu_resp_t;

/** Error codes carried in NACK.data[0] */
typedef enum {
    RFWU_ERR_AUTH          = 0x01U, /**< Auth token mismatch         */
    RFWU_ERR_CRC           = 0x02U, /**< Packet CRC mismatch         */
    RFWU_ERR_GAP           = 0x03U, /**< Offset gap (not expected)   */
    RFWU_ERR_OVERFLOW      = 0x04U, /**< Data beyond declared size   */
    RFWU_ERR_FLASH         = 0x05U, /**< Flash write failure         */
    RFWU_ERR_NO_SESSION    = 0x06U, /**< No active session           */
    RFWU_ERR_SIZE_MISMATCH = 0x07U, /**< FINISH size != expected     */
} rfwu_error_t;

/* ── Packet header struct ───────────────────────────────────────── */

/** 12-byte packet header; all fields little-endian */
typedef struct {
    uint32_t magic;    /**< Must equal RFWU_MAGIC           */
    uint8_t  cmd;      /**< rfwu_cmd_t or rfwu_resp_t       */
    uint8_t  flags;    /**< Reserved, must be 0             */
    uint16_t data_len; /**< Byte count of following payload */
    uint32_t seq;      /**< Sequence number (informational) */
} __attribute__((packed)) rfwu_header_t;

_Static_assert(sizeof(rfwu_header_t) == RFWU_HEADER_SIZE,
               "rfwu_header_t size mismatch");

/* ── Flash operation callbacks ──────────────────────────────────── */

/**
 * @brief Flash callbacks provided by the BSP/application layer.
 *
 * fw_init is called once per transfer:
 *   - resume_offset == 0  : fresh start (erase target region if needed)
 *   - resume_offset  > 0  : resume; always a multiple of 4096
 *
 * fw_write is called sequentially from resume_offset onward.
 * The implementation must buffer writes to 4 KB sector boundaries internally.
 */
typedef struct {
    int  (*fw_init)  (uint32_t total_size, uint32_t resume_offset);
    int  (*fw_write) (const uint8_t *p_data, uint32_t size);
    int  (*fw_finish)(uint32_t total_size);
    void (*fw_reboot)(void);
} rfwu_fw_ops_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Initialise the RFWU module with the TCP send callback.
 *
 * Must be called once before the first rfwu_on_receive().
 * Flash callbacks are registered separately via rfwu_register_fw_ops().
 *
 * @param[in] p_send  TCP transmit function (must not be NULL).
 */
void rfwu_init(int (*p_send)(const void *p_data, int len));

/**
 * @brief Register (or replace) flash operation callbacks.
 *
 * @param[in] p_ops  Flash callbacks struct (must not be NULL).
 */
void rfwu_register_fw_ops(const rfwu_fw_ops_t *p_ops);

/**
 * @brief Feed received TCP bytes into the RFWU parser.
 *
 * Call this whenever bytes arrive on the TCP socket and the connection
 * is in RFWU mode.  Handles fragmented TCP delivery transparently.
 *
 * @param[in] p_data  Pointer to received bytes.
 * @param[in] len     Number of bytes (must be > 0).
 */
void rfwu_on_receive(const uint8_t *p_data, int len);

/**
 * @brief Notify the RFWU module that the TCP connection closed.
 *
 * Resets the packet parser.  Session state is preserved in NVRAM so
 * that the next connection can resume.
 */
void rfwu_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_WEB_SERVER_RAW_TCP_FW_UPDATE_H_ */
