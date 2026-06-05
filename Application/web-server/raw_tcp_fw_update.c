/*
 * raw_tcp_fw_update.c
 *
 * RFWU — Raw Firmware Update Protocol over TCP port 80.
 *
 * Parser state machine:
 *   WAIT_HEADER (12 bytes) → WAIT_DATA (data_len bytes) → WAIT_CRC (4 bytes)
 *   → validate CRC → dispatch command → WAIT_HEADER
 *
 * Received-bytes tracking:
 *   write_head        — in-memory offset of next expected byte (advances per chunk).
 *   session.received_bytes — NVRAM-persisted progress; only advances at 4 KB sector
 *                           boundaries so resume always starts on an aligned offset.
 */

#include "raw_tcp_fw_update.h"

#include "nvram.h"
#include "efw_crc.h"
#include "console_logger.h"

#include <string.h>
#include <stddef.h>

/* ── Parser states ──────────────────────────────────────────────── */

typedef enum {
    PARSE_WAIT_HEADER = 0,
    PARSE_WAIT_DATA,
    PARSE_WAIT_CRC,
} parse_state_t;

/* ── Module state ───────────────────────────────────────────────── */

static struct {
    /* Packet parser */
    parse_state_t parse_state;
    uint8_t       buf[RFWU_MAX_PACKET_SIZE];
    uint16_t      buf_fill;
    uint16_t      need;         /* bytes still required for current phase */
    rfwu_header_t hdr;          /* decoded header of the current packet  */

    /* Transfer state */
    bool     session_active;
    uint32_t write_head;        /* next byte offset expected from client */

    /* Callbacks */
    const rfwu_fw_ops_t *p_ops;
    int (*p_send)(const void *p_data, int len);
} s;

/* ── CRC-32 helper ──────────────────────────────────────────────── */

static uint32_t crc32_calc(const uint8_t *p_data, uint16_t len)
{
    efw_crc_t crc = efw_crc_init();
    crc = efw_crc_update(crc, p_data, (size_t)len);
    return (uint32_t)efw_crc_finalize(crc);
}

/* ── Little-endian read helpers ─────────────────────────────────── */

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8U)
         | ((uint32_t)p[2] << 16U)
         | ((uint32_t)p[3] << 24U);
}

/* ── Response builders ──────────────────────────────────────────── */

/*
 * Build and send an RFWU response packet.
 * Largest response payload is RESP_STATUS at 17 bytes.
 * Buffer: header(12) + data(max 17) + crc(4) = 33 bytes.
 */
static void send_response(uint8_t resp_cmd, const uint8_t *p_data, uint8_t data_len)
{
    if (s.p_send == NULL) { return; }

    /* 12-byte header + up to 17 data bytes + 4-byte CRC = 33 bytes max */
    uint8_t  pkt[RFWU_HEADER_SIZE + 17U + RFWU_CRC_SIZE];
    uint16_t total = (uint16_t)(RFWU_HEADER_SIZE + (uint16_t)data_len + RFWU_CRC_SIZE);

    pkt[0]  = (uint8_t)(RFWU_MAGIC         & 0xFFU);
    pkt[1]  = (uint8_t)((RFWU_MAGIC >> 8U)  & 0xFFU);
    pkt[2]  = (uint8_t)((RFWU_MAGIC >> 16U) & 0xFFU);
    pkt[3]  = (uint8_t)((RFWU_MAGIC >> 24U) & 0xFFU);
    pkt[4]  = resp_cmd;
    pkt[5]  = 0x00U;
    pkt[6]  = data_len;
    pkt[7]  = 0x00U;
    pkt[8]  = 0x00U;
    pkt[9]  = 0x00U;
    pkt[10] = 0x00U;
    pkt[11] = 0x00U;

    if (data_len > 0U) {
        (void)memcpy(&pkt[RFWU_HEADER_SIZE], p_data, (size_t)data_len);
    }

    /* CRC covers header + data */
    uint32_t crc     = crc32_calc(pkt, (uint16_t)(RFWU_HEADER_SIZE + (uint16_t)data_len));
    uint8_t  crc_off = (uint8_t)(RFWU_HEADER_SIZE + data_len);
    pkt[crc_off + 0U] = (uint8_t)(crc         & 0xFFU);
    pkt[crc_off + 1U] = (uint8_t)((crc >> 8U)  & 0xFFU);
    pkt[crc_off + 2U] = (uint8_t)((crc >> 16U) & 0xFFU);
    pkt[crc_off + 3U] = (uint8_t)((crc >> 24U) & 0xFFU);

    (void)s.p_send(pkt, (int)total);
}

/* ACK payload: received_bytes(4 LE) + chunk_size(2 LE) */
static void send_ack(uint32_t received)
{
    uint8_t data[6];
    data[0] = (uint8_t)(received             & 0xFFU);
    data[1] = (uint8_t)((received >> 8U)     & 0xFFU);
    data[2] = (uint8_t)((received >> 16U)    & 0xFFU);
    data[3] = (uint8_t)((received >> 24U)    & 0xFFU);
    data[4] = (uint8_t)(RFWU_MAX_CHUNK_SIZE        & 0xFFU);
    data[5] = (uint8_t)((RFWU_MAX_CHUNK_SIZE >> 8U) & 0xFFU);
    send_response((uint8_t)RFWU_RESP_ACK, data, (uint8_t)sizeof(data));
}

/* NACK payload: error_code(1) + expected_offset(4 LE) */
static void send_nack(rfwu_error_t err, uint32_t expected_offset)
{
    uint8_t data[5];
    data[0] = (uint8_t)err;
    data[1] = (uint8_t)(expected_offset         & 0xFFU);
    data[2] = (uint8_t)((expected_offset >> 8U)  & 0xFFU);
    data[3] = (uint8_t)((expected_offset >> 16U) & 0xFFU);
    data[4] = (uint8_t)((expected_offset >> 24U) & 0xFFU);
    send_response((uint8_t)RFWU_RESP_NACK, data, (uint8_t)sizeof(data));
}

/* STATUS payload: active(1) + received_bytes(4 LE) + total_size(4 LE) */
static void send_status(void)
{
    const rfwu_nvram_t *p_nv     = nvram_get_rfwu();
    bool               active   = s.session_active;
    uint32_t           whead    = active ? s.write_head : 0U;
    bool               has_nv   = (p_nv->magic == RFWU_SESSION_MAGIC);
    uint32_t           nv_total = has_nv ? p_nv->total_size     : 0U;
    uint32_t           nv_rx    = has_nv ? p_nv->received_bytes : 0U;
    uint32_t           nv_hash  = has_nv ? p_nv->file_hash      : 0U;

    /*
     * RESP_STATUS payload (17 bytes, all LE):
     *   [0]      active       uint8   1 = transfer currently in progress
     *   [1..4]   write_head   uint32  in-RAM offset (0 if not active)
     *   [5..8]   nv_total     uint32  NVRAM total_size  (0 if no session)
     *   [9..12]  nv_received  uint32  NVRAM received_bytes
     *   [13..16] nv_hash      uint32  NVRAM file_hash
     */
    uint8_t data[17];
    data[0]  = active ? 1U : 0U;
    data[1]  = (uint8_t)(whead         & 0xFFU);
    data[2]  = (uint8_t)((whead >>  8U) & 0xFFU);
    data[3]  = (uint8_t)((whead >> 16U) & 0xFFU);
    data[4]  = (uint8_t)((whead >> 24U) & 0xFFU);
    data[5]  = (uint8_t)(nv_total         & 0xFFU);
    data[6]  = (uint8_t)((nv_total >>  8U) & 0xFFU);
    data[7]  = (uint8_t)((nv_total >> 16U) & 0xFFU);
    data[8]  = (uint8_t)((nv_total >> 24U) & 0xFFU);
    data[9]  = (uint8_t)(nv_rx         & 0xFFU);
    data[10] = (uint8_t)((nv_rx >>  8U) & 0xFFU);
    data[11] = (uint8_t)((nv_rx >> 16U) & 0xFFU);
    data[12] = (uint8_t)((nv_rx >> 24U) & 0xFFU);
    data[13] = (uint8_t)(nv_hash         & 0xFFU);
    data[14] = (uint8_t)((nv_hash >>  8U) & 0xFFU);
    data[15] = (uint8_t)((nv_hash >> 16U) & 0xFFU);
    data[16] = (uint8_t)((nv_hash >> 24U) & 0xFFU);
    send_response((uint8_t)RFWU_RESP_STATUS, data, (uint8_t)sizeof(data));
}

/* ── NVRAM session helpers ──────────────────────────────────────── */

/* Persist sector-aligned progress; preserves all other NVRAM fields. */
static void session_persist(uint32_t received_bytes)
{
    const rfwu_nvram_t *p_cur = nvram_get_rfwu();
    rfwu_nvram_t        upd;

    upd.magic          = RFWU_SESSION_MAGIC;
    upd.file_hash      = p_cur->file_hash;
    upd.total_size     = p_cur->total_size;
    upd.received_bytes = received_bytes;
    upd.shared_key     = p_cur->shared_key;

    nvram_set_rfwu(&upd);
}

/* Clear the session record; preserve the shared key. */
static void session_clear(void)
{
    const rfwu_nvram_t *p_cur = nvram_get_rfwu();
    rfwu_nvram_t        upd;

    upd.magic          = 0U;
    upd.file_hash      = 0U;
    upd.total_size     = 0U;
    upd.received_bytes = 0U;
    upd.shared_key     = p_cur->shared_key;

    nvram_set_rfwu(&upd);
    s.session_active = false;
    s.write_head     = 0U;
}

/* ── Command handlers ───────────────────────────────────────────── */

static void handle_cmd_hello(void)
{
    /* Payload must be exactly: total_size(4) + file_hash(4) + auth_token(4) */
    if (s.hdr.data_len != 12U) {
        send_nack(RFWU_ERR_CRC, 0U);
        return;
    }

    const uint8_t *p = &s.buf[RFWU_HEADER_SIZE];
    uint32_t total_size  = read_u32_le(p);
    uint32_t file_hash   = read_u32_le(p + 4U);
    uint32_t auth_token  = read_u32_le(p + 8U);

    /* Auth: token = CRC32(shared_key_bytes) XOR total_size */
    const rfwu_nvram_t *p_nv = nvram_get_rfwu();
    uint32_t shared_key = (p_nv->shared_key != 0U) ? p_nv->shared_key
                                                    : RFWU_DEFAULT_SHARED_KEY;
    uint32_t expected_token = crc32_calc((const uint8_t *)&shared_key, 4U) ^ total_size;

    if (auth_token != expected_token) {
        CCSLOG(XCOLOR_RED, "[RFWU] Auth failed\r\n");
        send_nack(RFWU_ERR_AUTH, 0U);
        return;
    }

    /* Resume check: same file if magic + size + hash all match */
    uint32_t resume_offset = 0U;
    if ((p_nv->magic      == RFWU_SESSION_MAGIC) &&
        (p_nv->total_size == total_size)          &&
        (p_nv->file_hash  == file_hash))
    {
        resume_offset = p_nv->received_bytes;
        CCSLOG(XCOLOR_CYAN, "[RFWU] Resume from %lu bytes\r\n", resume_offset);
    }
    else
    {
        /* New transfer — write session record first */
        rfwu_nvram_t upd;
        upd.magic          = RFWU_SESSION_MAGIC;
        upd.file_hash      = file_hash;
        upd.total_size     = total_size;
        upd.received_bytes = 0U;
        upd.shared_key     = shared_key;
        nvram_set_rfwu(&upd);

        CCSLOG(XCOLOR_CYAN, "[RFWU] New transfer, size=%lu\r\n", total_size);
    }

    /* Initialise flash layer */
    if (s.p_ops->fw_init(total_size, resume_offset) != 0) {
        send_nack(RFWU_ERR_FLASH, 0U);
        return;
    }

    s.session_active = true;
    s.write_head     = resume_offset;
    send_ack(s.write_head);
}

static void handle_cmd_data(void)
{
    if (!s.session_active) {
        send_nack(RFWU_ERR_NO_SESSION, 0U);
        return;
    }

    /* Payload: offset(4) + at least 1 firmware byte */
    if (s.hdr.data_len < 5U) {
        send_nack(RFWU_ERR_CRC, s.write_head);
        return;
    }

    const uint8_t *p        = &s.buf[RFWU_HEADER_SIZE];
    uint32_t       offset     = read_u32_le(p);
    uint32_t       chunk_size = (uint32_t)s.hdr.data_len - 4U;

    /* Retry: we already have this data */
    if (offset < s.write_head) {
        CCSLOG(XCOLOR_YELLOW, "[RFWU] Retry: offset=%lu < write_head=%lu, skipping\r\n",
               (unsigned long)offset, (unsigned long)s.write_head);
        send_ack(s.write_head);
        return;
    }

    /* Gap: client jumped ahead */
    if (offset > s.write_head) {
        CCSLOG(XCOLOR_RED, "[RFWU] Gap: offset=%lu, expected=%lu\r\n",
               (unsigned long)offset, (unsigned long)s.write_head);
        send_nack(RFWU_ERR_GAP, s.write_head);
        return;
    }

    /* Overflow: would write past declared file size */
    const rfwu_nvram_t *p_nv = nvram_get_rfwu();
    if ((offset + chunk_size) > p_nv->total_size) {
        CCSLOG(XCOLOR_RED, "[RFWU] Overflow: offset=%lu + chunk=%lu > total=%lu\r\n",
               (unsigned long)offset, (unsigned long)chunk_size,
               (unsigned long)p_nv->total_size);
        send_nack(RFWU_ERR_OVERFLOW, s.write_head);
        return;
    }

    /* Write to flash */
    if (s.p_ops->fw_write(p + 4U, chunk_size) != 0) {
        send_nack(RFWU_ERR_FLASH, s.write_head);
        return;
    }

    /* Advance write_head; persist to NVRAM at each 4 KB sector boundary */
    uint32_t prev_sector = s.write_head / 4096U;
    s.write_head += chunk_size;
    uint32_t new_sector = s.write_head / 4096U;

    if (new_sector > prev_sector) {
        session_persist(new_sector * 4096U);
    }

    send_ack(s.write_head);
}

static void handle_cmd_finish(void)
{
    if (!s.session_active) {
        send_nack(RFWU_ERR_NO_SESSION, 0U);
        return;
    }

    if (s.hdr.data_len != 4U) {
        send_nack(RFWU_ERR_CRC, s.write_head);
        return;
    }

    uint32_t total_size = read_u32_le(&s.buf[RFWU_HEADER_SIZE]);

    const rfwu_nvram_t *p_nv = nvram_get_rfwu();
    if (total_size != p_nv->total_size) {
        send_nack(RFWU_ERR_SIZE_MISMATCH, s.write_head);
        return;
    }

    if (s.write_head != total_size) {
        send_nack(RFWU_ERR_SIZE_MISMATCH, s.write_head);
        return;
    }

    /* Flush any partial sector remaining in the flash buffer */
    if (s.p_ops->fw_finish(total_size) != 0) {
        send_nack(RFWU_ERR_FLASH, s.write_head);
        return;
    }

    /* Mark complete in NVRAM */
    session_persist(total_size);
    s.session_active = false;

    CCSLOG(XCOLOR_GREEN, "[RFWU] Transfer complete: %lu bytes\r\n", total_size);
    send_ack(s.write_head);
}

static void handle_cmd_query(void)
{
    send_status();
}

static void handle_cmd_reboot(void)
{
    CCSLOG(XCOLOR_RED, "[RFWU] Reboot requested\r\n");
    send_ack(s.write_head);

    if ((s.p_ops != NULL) && (s.p_ops->fw_reboot != NULL)) {
        s.p_ops->fw_reboot();
    }
}

static void handle_cmd_abort(void)
{
    CCSLOG(XCOLOR_YELLOW, "[RFWU] Transfer aborted\r\n");
    session_clear();
    send_ack(0U);
}

/* ── Packet parser ──────────────────────────────────────────────── */

static void reset_parser(void)
{
    s.parse_state = PARSE_WAIT_HEADER;
    s.buf_fill    = 0U;
    s.need        = RFWU_HEADER_SIZE;
}

static void decode_header(void)
{
    s.hdr.magic    = read_u32_le(&s.buf[0]);
    s.hdr.cmd      = s.buf[4];
    s.hdr.flags    = s.buf[5];
    s.hdr.data_len = read_u16_le(&s.buf[6]);
    s.hdr.seq      = read_u32_le(&s.buf[8]);
}

/* Called when a complete packet has been received; validates CRC and dispatches. */
static void process_packet(void)
{
    uint16_t payload_end  = (uint16_t)(RFWU_HEADER_SIZE + s.hdr.data_len);
    uint32_t expected_crc = read_u32_le(&s.buf[payload_end]);
    uint32_t computed_crc = crc32_calc(s.buf, payload_end);

    if (computed_crc != expected_crc) {
        CCSLOG(XCOLOR_RED, "[RFWU] CRC error\r\n");
        send_nack(RFWU_ERR_CRC, s.write_head);
        return;
    }

    if (s.p_ops == NULL) {
        send_nack(RFWU_ERR_FLASH, 0U);
        return;
    }

    switch ((rfwu_cmd_t)s.hdr.cmd) {
        case RFWU_CMD_HELLO:  handle_cmd_hello();  break;
        case RFWU_CMD_DATA:   handle_cmd_data();   break;
        case RFWU_CMD_FINISH: handle_cmd_finish(); break;
        case RFWU_CMD_QUERY:  handle_cmd_query();  break;
        case RFWU_CMD_REBOOT: handle_cmd_reboot(); break;
        case RFWU_CMD_ABORT:  handle_cmd_abort();  break;
        default:
            CCSLOG(XCOLOR_RED, "[RFWU] Unknown cmd: 0x%02X\r\n", s.hdr.cmd);
            send_nack(RFWU_ERR_CRC, s.write_head);
            break;
    }
}

/* Advance the parser state after accumulating the required bytes. */
static void advance_state(void)
{
    switch (s.parse_state) {
        case PARSE_WAIT_HEADER:
            decode_header();
            if (s.hdr.magic != RFWU_MAGIC) {
                /* Not an RFWU packet — discard and re-sync */
                reset_parser();
            } else if (s.hdr.data_len > RFWU_MAX_DATA_LEN) {
                /* Oversized payload — reject */
                reset_parser();
            } else if (s.hdr.data_len == 0U) {
                s.parse_state = PARSE_WAIT_CRC;
                s.need        = RFWU_CRC_SIZE;
            } else {
                s.parse_state = PARSE_WAIT_DATA;
                s.need        = s.hdr.data_len;
            }
            break;

        case PARSE_WAIT_DATA:
            s.parse_state = PARSE_WAIT_CRC;
            s.need        = RFWU_CRC_SIZE;
            break;

        case PARSE_WAIT_CRC:
            process_packet();
            reset_parser();
            break;

        default:
            reset_parser();
            break;
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void rfwu_init(int (*p_send)(const void *p_data, int len))
{
    (void)memset(&s, 0, sizeof(s));
    s.p_send = p_send;
    reset_parser();
}

void rfwu_register_fw_ops(const rfwu_fw_ops_t *p_ops)
{
    s.p_ops = p_ops;
}

void rfwu_on_receive(const uint8_t *p_data, int len)
{
    if ((p_data == NULL) || (len <= 0)) { return; }

    const uint8_t *p         = p_data;
    int            remaining  = len;

    while (remaining > 0) {
        /* Bytes to copy: the smaller of what we need and what is available */
        uint16_t can_copy = s.need;
        if ((uint32_t)can_copy > (uint32_t)remaining) {
            can_copy = (uint16_t)(uint32_t)remaining;
        }

        /* Bounds guard: should never trigger given the data_len check in advance_state */
        if (((uint32_t)s.buf_fill + (uint32_t)can_copy) > RFWU_MAX_PACKET_SIZE) {
            reset_parser();
            break;
        }

        (void)memcpy(&s.buf[s.buf_fill], p, (size_t)can_copy);
        s.buf_fill = (uint16_t)((uint32_t)s.buf_fill + (uint32_t)can_copy);
        s.need     = (uint16_t)((uint32_t)s.need     - (uint32_t)can_copy);
        p         += can_copy;
        remaining -= (int)can_copy;

        if (s.need == 0U) {
            advance_state();
        }
    }
}

void rfwu_on_disconnect(void)
{
    /* Reset parser; NVRAM session is preserved for resume on next connection */
    reset_parser();
    s.session_active = false;
}
