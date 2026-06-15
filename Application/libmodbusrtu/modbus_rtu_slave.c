/**
 * @file modbus_rtu_slave.c
 * @brief Modbus RTU Slave Implementation
 */

#include "modbus_rtu_slave.h"
#include <string.h>

/* Frame geometry (bytes) */
#define MODBUS_FRAME_MIN_LEN        4U   /* addr + fc + crc(2)                  */
#define MODBUS_FC03_REQ_LEN         8U   /* addr+fc+start(2)+count(2)+crc(2)    */
#define MODBUS_FC06_REQ_LEN         8U   /* addr+fc+addr(2)+value(2)+crc(2)     */
#define MODBUS_CRC_LEN              2U   /* CRC16 trailer length                */
#define MODBUS_FC03_HEADER_LEN      3U   /* addr + fc + byte-count              */
#define MODBUS_EXCEPTION_RESP_LEN   5U   /* addr+fc+exception+crc(2)            */
#define MODBUS_EXCEPTION_FLAG       0x80U/* OR'd into FC to flag an exception   */

/* Frame field offsets */
#define MODBUS_OFFSET_ADDR          0U
#define MODBUS_OFFSET_FC            1U
#define MODBUS_OFFSET_DATA          2U

/*
 * The slave instance (modbus_slave_t) is defined in the public header so the
 * application can allocate it statically and run several independent buses.
 * This core keeps no global state: every function operates on the instance it
 * is given.
 */


/* CRC16 Table for Modbus */
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

/**
 * @brief Calculate CRC16 for Modbus RTU
 * @param buffer Data buffer
 * @param length Data length
 * @return CRC16 value
 */
static uint16_t modbus_crc16(const uint8_t *buffer, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; i++) {
        uint8_t index = (uint8_t)(crc ^ buffer[i]);
        crc = (uint16_t)((crc >> 8) ^ crc16_table[index]);
    }

    return crc;
}

/**
 * @brief Resolve a wire (PDU) register address to a logical register reference.
 * @param[in]  base_addr  Reference base (e.g. MODBUS_HOLDING_REG_BASE).
 * @param[in]  wire_addr  Address from the request (0-based PDU address).
 * @param[out] p_logical  Destination for the resolved logical reference.
 * @return true if the resolved reference fits in 16 bits, false on overflow.
 */
static bool modbus_resolve_reg_address(uint16_t base_addr, uint32_t wire_addr, uint16_t *p_logical)
{
    uint32_t logical = (uint32_t)base_addr + wire_addr;

    if (logical > (uint32_t)UINT16_MAX) {
        return false;
    }

    *p_logical = (uint16_t)logical;
    return true;
}

/**
 * @brief Send Modbus exception response
 * @param function_code Original function code
 * @param exception_code Exception code
 */
static void modbus_send_exception(modbus_slave_t *p_ctx, uint8_t function_code, uint8_t exception_code)
{
    uint8_t response[MODBUS_EXCEPTION_RESP_LEN];
    uint16_t crc;

    response[0] = p_ctx->slave_id;
    response[1] = (uint8_t)(function_code | MODBUS_EXCEPTION_FLAG);
    response[2] = exception_code;

    /* Record the error so the application can surface the last fault. */
    p_ctx->last_exception_code = exception_code;

    crc = modbus_crc16(response, 3U);
    response[3] = (uint8_t)(crc & 0xFFU);
    response[4] = (uint8_t)(crc >> 8);

    if (p_ctx->uart_write != NULL) {
        p_ctx->uart_write(response, MODBUS_EXCEPTION_RESP_LEN);
    }
}

/**
 * @brief Process Function Code 03 - Read Holding Registers
 * @param[in] p_ctx Slave instance.
 * @param[in] frame Pointer to the validated request frame.
 * @param[in] len   Request frame length (including CRC).
 */
static void modbus_process_fc03(modbus_slave_t *p_ctx, const uint8_t *frame, uint16_t len)
{
    if (len != MODBUS_FC03_REQ_LEN) {
        return;
    }

    uint16_t wire_addr = (uint16_t)(((uint16_t)frame[2] << 8) | (uint16_t)frame[3]);
    uint16_t count     = (uint16_t)(((uint16_t)frame[4] << 8) | (uint16_t)frame[5]);

    if ((count == 0U) || (count > MODBUS_MAX_READ_REGISTERS)) {
        modbus_send_exception(p_ctx, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        return;
    }

    if (p_ctx->read_callback == NULL) {
        modbus_send_exception(p_ctx, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
        return;
    }

    uint16_t byte_count   = (uint16_t)(count * 2U);
    uint16_t response_len = (uint16_t)(MODBUS_FC03_HEADER_LEN + byte_count + MODBUS_CRC_LEN);

    if (response_len > MODBUS_BUFFER_SIZE) {
        modbus_send_exception(p_ctx, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        return;
    }

    uint8_t response[MODBUS_BUFFER_SIZE];
    uint16_t idx = 0U;

    response[idx++] = p_ctx->slave_id;
    response[idx++] = MODBUS_FC_READ_HOLDING_REGISTERS;
    response[idx++] = (uint8_t)byte_count;

    for (uint16_t i = 0U; i < count; i++) {
        uint16_t logical_addr = 0U;

        if (!modbus_resolve_reg_address(MODBUS_HOLDING_REG_BASE, (uint32_t)wire_addr + i, &logical_addr)) {
            modbus_send_exception(p_ctx, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
            return;
        }

        uint16_t value = 0U;
        modbus_reg_status_t status = p_ctx->read_callback(logical_addr, &value);

        if (status != MODBUS_REG_OK) {
            modbus_send_exception(p_ctx, MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
            return;
        }

        response[idx++] = (uint8_t)(value >> 8);
        response[idx++] = (uint8_t)(value & 0xFFU);
    }

    uint16_t crc = modbus_crc16(response, idx);
    response[idx++] = (uint8_t)(crc & 0xFFU);
    response[idx++] = (uint8_t)(crc >> 8);

    if (p_ctx->uart_write != NULL) {
        p_ctx->uart_write(response, idx);
    }
}

/**
 * @brief Process Function Code 06 - Write Single Register
 * @param[in] p_ctx        Slave instance.
 * @param[in] frame        Pointer to the validated request frame.
 * @param[in] len          Request frame length (including CRC).
 * @param[in] is_broadcast true if the request was addressed to the broadcast
 *                         address (no response shall be sent).
 */
static void modbus_process_fc06(modbus_slave_t *p_ctx, const uint8_t *frame, uint16_t len, bool is_broadcast)
{
    if (len != MODBUS_FC06_REQ_LEN) {
        return;
    }

    uint16_t wire_addr = (uint16_t)(((uint16_t)frame[2] << 8) | (uint16_t)frame[3]);
    uint16_t value     = (uint16_t)(((uint16_t)frame[4] << 8) | (uint16_t)frame[5]);

    if (p_ctx->write_callback == NULL) {
        if (!is_broadcast) {
            modbus_send_exception(p_ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
        }
        return;
    }

    uint16_t logical_addr = 0U;
    if (!modbus_resolve_reg_address(MODBUS_HOLDING_REG_BASE, (uint32_t)wire_addr, &logical_addr)) {
        if (!is_broadcast) {
            modbus_send_exception(p_ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        }
        return;
    }

    modbus_reg_status_t status = p_ctx->write_callback(logical_addr, value);

    /* A broadcast write is never acknowledged. */
    if (is_broadcast) {
        return;
    }

    switch (status) {
        case MODBUS_REG_OK:
            /* Standard FC06 response echoes the request. */
            if (p_ctx->uart_write != NULL) {
                p_ctx->uart_write(frame, len);
            }
            break;

        case MODBUS_REG_ERR_ADDRESS:
            modbus_send_exception(p_ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
            break;

        case MODBUS_REG_ERR_VALUE:
        default:
            modbus_send_exception(p_ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
            break;
    }
}

/**
 * @brief Process a received Modbus frame.
 * @param[in] p_ctx Slave instance.
 * @param[in] frame Pointer to the received frame buffer.
 * @param[in] len   Number of bytes in the frame (including CRC).
 */
static void modbus_process_frame(modbus_slave_t *p_ctx, const uint8_t *frame, uint16_t len)
{
    if (len < MODBUS_FRAME_MIN_LEN) {
        return;
    }

    /* Validate CRC (transmitted low byte first). */
    uint16_t received_crc   = (uint16_t)(((uint16_t)frame[len - 1U] << 8) | (uint16_t)frame[len - 2U]);
    uint16_t calculated_crc = modbus_crc16(frame, (uint16_t)(len - MODBUS_CRC_LEN));

    if (received_crc != calculated_crc) {
        return;
    }

    uint8_t addr         = frame[MODBUS_OFFSET_ADDR];
    bool    is_broadcast = (addr == MODBUS_BROADCAST_ADDR);

    if (!is_broadcast && (addr != p_ctx->slave_id)) {
        return;
    }

    uint8_t function_code = frame[MODBUS_OFFSET_FC];

    /*
     * Clear the per-frame exception record before dispatching. After this call
     * a non-zero last_exception_code means this frame raised that exception,
     * which lets the caller detect and log each occurrence.
     */
    p_ctx->last_exception_code = 0U;

    switch (function_code) {
        case MODBUS_FC_READ_HOLDING_REGISTERS:
            /* Reads cannot be issued to the broadcast address. */
            if (!is_broadcast) {
                modbus_process_fc03(p_ctx, frame, len);
            }
            break;

        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            modbus_process_fc06(p_ctx, frame, len, is_broadcast);
            break;

        default:
            if (!is_broadcast) {
                modbus_send_exception(p_ctx, function_code, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
            }
            break;
    }
}

/* Public API Implementation */

void libmodbusrtu_slave_init(modbus_slave_t *p_ctx,
                             uint8_t slave_id,
                             modbus_get_tick_ms_t get_tick_ms_func,
                             modbus_uart_write_t uart_write_func)
{
    memset(p_ctx, 0, sizeof(*p_ctx));

    if ((slave_id < MODBUS_SLAVE_ID_MIN) || (slave_id > MODBUS_SLAVE_ID_MAX)) {
        slave_id = MODBUS_DEFAULT_SLAVE_ID;
    }

    p_ctx->slave_id    = slave_id;
    p_ctx->get_tick_ms = get_tick_ms_func;
    p_ctx->uart_write  = uart_write_func;
}

void libmodbusrtu_register_read_callback(modbus_slave_t *p_ctx, modbus_read_reg_callback_t callback)
{
    p_ctx->read_callback = callback;
}

void libmodbusrtu_register_write_callback(modbus_slave_t *p_ctx, modbus_write_reg_callback_t callback)
{
    p_ctx->write_callback = callback;
}

void libmodbusrtu_modbus_set_slave_id(modbus_slave_t *p_ctx, uint8_t id)
{
    if ((id >= MODBUS_SLAVE_ID_MIN) && (id <= MODBUS_SLAVE_ID_MAX)) {
        p_ctx->slave_id = id;
    }
}

uint8_t libmodbusrtu_modbus_get_last_exception(const modbus_slave_t *p_ctx)
{
    return p_ctx->last_exception_code;
}

/**
 * @brief Clear the receive buffer and arm it for the next frame.
 */
static void modbus_rx_reset(modbus_slave_t *p_ctx)
{
    p_ctx->rx_count    = 0U;
    p_ctx->frame_ready = false;
    p_ctx->busy        = false;
}

void libmodbusrtu_modbus_rx_byte(modbus_slave_t *p_ctx, uint8_t byte)
{
    /* The main loop owns the buffer while parsing - drop bytes until it is done. */
    if (p_ctx->busy) {
        return;
    }

    if (p_ctx->rx_count >= MODBUS_BUFFER_SIZE) {
        /* Overflow: no valid frame is this long - discard and resynchronise. */
        p_ctx->rx_count = 0U;
    }

    p_ctx->rx_buffer[p_ctx->rx_count] = byte;
    p_ctx->rx_count++;

#if (MODBUS_USE_HW_RTO == 0)
    /* Timestamp the byte so the main loop can measure the inter-frame gap. */
    if (p_ctx->get_tick_ms != NULL) {
        p_ctx->last_rx_time = p_ctx->get_tick_ms();
    }
#endif
}

#if (MODBUS_USE_HW_RTO == 1)
void libmodbusrtu_modbus_rx_timeout(modbus_slave_t *p_ctx)
{
    /* The RX line went idle for >= T3.5: any buffered frame is complete. */
    if (p_ctx->rx_count > 0U) {
        p_ctx->frame_ready = true;
    }
}
#endif

modbus_poll_result_t libmodbusrtu_modbus_process(modbus_slave_t *p_ctx)
{
    uint16_t count = p_ctx->rx_count;

    if (count == 0U) {
        return MODBUS_POLL_IDLE;
    }

    /*
     * Frame delimiting is purely timeout based (PetitModbus style): once the
     * receive line has been idle for >= T3.5 the buffered bytes form a frame.
     * CRC validation (in modbus_process_frame) has the final say on validity.
     */
#if (MODBUS_USE_HW_RTO == 1)
    /* Hardware mode: the RX-timeout ISR flags a finished frame. */
    bool complete = p_ctx->frame_ready;
#else
    /* Software mode: measure the inter-frame (T3.5) idle gap in the main loop. */
    bool complete = ((p_ctx->get_tick_ms != NULL) &&
                     ((p_ctx->get_tick_ms() - p_ctx->last_rx_time) >= MODBUS_TIMEOUT_MS));
#endif

    if (!complete) {
        return MODBUS_POLL_RECEIVING;
    }

    /* Take ownership of the buffer so the RX ISR stops writing to it. */
    p_ctx->busy = true;
    modbus_process_frame(p_ctx, p_ctx->rx_buffer, count);
    modbus_rx_reset(p_ctx);

    return MODBUS_POLL_HANDLED;
}

