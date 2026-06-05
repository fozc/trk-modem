/**
 * @file modbus_rtu_slave.c
 * @brief Modbus RTU Slave Implementation
 */

#include "modbus_rtu_slave.h"
#include <string.h>

/* Modbus RTU State Machine */
typedef enum {
    MODBUS_STATE_IDLE,
    MODBUS_STATE_RECEIVING,
    MODBUS_STATE_PROCESSING
} modbus_state_t;

/* Modbus Context Structure */
typedef struct {
    uint8_t slave_id;
    uint8_t rx_buffer[MODBUS_BUFFER_SIZE];
    uint16_t rx_index;
    uint32_t last_rx_time;
    modbus_state_t state;
    
    /* Platform-specific functions */
    modbus_get_tick_ms_t get_tick_ms;
    modbus_uart_write_t uart_write;
    
    /* User callbacks */
    modbus_fc03_callback_t fc03_callback;
    modbus_fc06_callback_t fc06_callback;
} modbus_context_t;

/* Global Modbus context */
static modbus_context_t g_modbus;

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
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        uint8_t index = (uint8_t)(crc ^ buffer[i]);
        crc = (crc >> 8) ^ crc16_table[index];
    }
    
    return crc;
}

/**
 * @brief Send Modbus exception response
 * @param function_code Original function code
 * @param exception_code Exception code
 */
static void modbus_send_exception(uint8_t function_code, uint8_t exception_code)
{
    uint8_t response[5];
    uint16_t crc;
    
    response[0] = g_modbus.slave_id;
    response[1] = function_code | 0x80;  // Set MSB for exception
    response[2] = exception_code;
    
    crc = modbus_crc16(response, 3);
    response[3] = (uint8_t)(crc & 0xFF);
    response[4] = (uint8_t)(crc >> 8);
    
    if (g_modbus.uart_write) {
        g_modbus.uart_write(response, 5);
    }
}

/**
 * @brief Process Function Code 03 - Read Holding Registers
 * @param request Pointer to request buffer
 * @param request_len Request length
 */
static void modbus_process_fc03(const uint8_t *request, uint16_t request_len)
{
    if (request_len != 8) {  // Slave ID + FC + Start Addr(2) + Count(2) + CRC(2)
        return;
    }
    
    uint16_t start_addr = (uint16_t)(request[2] << 8) | request[3];
    uint16_t count = (request[4] << 8) | request[5];
    
    // Validate request
    if (count == 0 || count > 125) {
        modbus_send_exception(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        return;
    }
    
    // Address validation will be done by user callback
    // Just check if count exceeds our max register limit
    if (count > MODBUS_MAX_REGISTERS) {
        modbus_send_exception(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        return;
    }

    if (3 + count * 2 + 2 > MODBUS_BUFFER_SIZE) 
    { 
        modbus_send_exception(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        return; 
    }
    
    // Call user callback
    if (!g_modbus.fc03_callback) {
        modbus_send_exception(MODBUS_FC_READ_HOLDING_REGISTERS, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
        return;
    }
  
    uint16_t register_data[MODBUS_MAX_REGISTERS];
    g_modbus.fc03_callback(start_addr, count, register_data);
    
    // Build response
    uint8_t response[MODBUS_BUFFER_SIZE];
    uint16_t response_len = 0;
    
    response[response_len++] = g_modbus.slave_id;
    response[response_len++] = MODBUS_FC_READ_HOLDING_REGISTERS;
    response[response_len++] = count * 2;  // Byte count
    
    for (uint16_t i = 0; i < count; i++) {
        response[response_len++] = (uint8_t)(register_data[i] >> 8);
        response[response_len++] = (uint8_t)(register_data[i] & 0xFF);
    }
    
    uint16_t crc = modbus_crc16(response, response_len);
    response[response_len++] = (uint8_t)(crc & 0xFF);
    response[response_len++] = (uint8_t)(crc >> 8);
    
    if (g_modbus.uart_write) {
        g_modbus.uart_write(response, response_len);
    }
}

/**
 * @brief Process Function Code 06 - Write Single Register
 * @param request Pointer to request buffer
 * @param request_len Request length
 */
static void modbus_process_fc06(const uint8_t *request, uint16_t request_len)
{
    if (request_len != 8) {  // Slave ID + FC + Addr(2) + Value(2) + CRC(2)
        return;
    }
    
    uint16_t addr = (uint16_t)(request[2] << 8) | request[3];
    uint16_t value = (uint16_t)(request[4] << 8) | request[5];
    
    // Address validation will be done by user callback
    // Callback can return 0 (fail) if address is invalid
    
    // Call user callback
    if (!g_modbus.fc06_callback) {
        modbus_send_exception(MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
        return;
    }
    
    uint8_t result = g_modbus.fc06_callback(addr, value);
    
    if (!result) {
        modbus_send_exception(MODBUS_FC_WRITE_SINGLE_REGISTER, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        return;
    }
    
    // Echo back the request as response (standard for FC06)
    if (g_modbus.uart_write) {
        g_modbus.uart_write(request, request_len);
    }
}

/**
 * @brief Process received Modbus frame
 */
static void modbus_process_frame(void)
{
    if (g_modbus.rx_index < 4) {  // Minimum frame: Slave ID + FC + CRC(2)
        return;
    }
    
    // Check CRC
    uint16_t received_crc = (uint16_t)(g_modbus.rx_buffer[g_modbus.rx_index - 1] << 8) |
                            g_modbus.rx_buffer[g_modbus.rx_index - 2];
    uint16_t calculated_crc = modbus_crc16(g_modbus.rx_buffer, g_modbus.rx_index - 2);
    
    if (received_crc != calculated_crc) {
        // CRC error - ignore frame
        return;
    }
    
    // Check slave ID
    if (g_modbus.rx_buffer[0] != g_modbus.slave_id) {
        return;
    }
    
    // Process based on function code
    uint8_t function_code = g_modbus.rx_buffer[1];
    
    switch (function_code) {
        case MODBUS_FC_READ_HOLDING_REGISTERS:
            modbus_process_fc03(g_modbus.rx_buffer, g_modbus.rx_index);
            break;
            
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
            modbus_process_fc06(g_modbus.rx_buffer, g_modbus.rx_index);
            break;
            
        default:
            modbus_send_exception(function_code, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
            break;
    }
}

/* Public API Implementation */

void libmodbusrtu_slave_init(uint8_t slave_id,
                       modbus_get_tick_ms_t get_tick_ms_func,
                       modbus_uart_write_t uart_write_func)
{
    memset(&g_modbus, 0, sizeof(modbus_context_t));
    
    g_modbus.slave_id = slave_id;
    g_modbus.get_tick_ms = get_tick_ms_func;
    g_modbus.uart_write = uart_write_func;
    g_modbus.state = MODBUS_STATE_IDLE;
}

void libmodbusrtu_register_fc03_callback(modbus_fc03_callback_t callback)
{
    g_modbus.fc03_callback = callback;
}

void libmodbusrtu_register_fc06_callback(modbus_fc06_callback_t callback)
{
    g_modbus.fc06_callback = callback;
}

void libmodbusrtu_modbus_set_slave_id(uint8_t id)
{
    g_modbus.slave_id = id;
}

void libmodbusrtu_modbus_rx_handler(uint8_t byte)
{
    if (!g_modbus.get_tick_ms) {
        // Cannot process without tick function
        return;
    }
    
    // Protect frame being processed - ignore new data
    if (g_modbus.state == MODBUS_STATE_PROCESSING) {
        return;
    }
    
    uint32_t current_time = g_modbus.get_tick_ms();
    
    // Check for timeout (new frame)
    if (g_modbus.state == MODBUS_STATE_RECEIVING) {
        if ((current_time - g_modbus.last_rx_time) >= MODBUS_TIMEOUT_MS) {
            // Timeout occurred - reset buffer
            g_modbus.rx_index = 0;
            g_modbus.state = MODBUS_STATE_IDLE;
        }
    }
    
    // Store byte
    if (g_modbus.rx_index < MODBUS_BUFFER_SIZE) {
        g_modbus.rx_buffer[g_modbus.rx_index++] = byte;
        g_modbus.last_rx_time = current_time;
        g_modbus.state = MODBUS_STATE_RECEIVING;
    } else {
        // Buffer overflow - reset
        g_modbus.rx_index = 0;
        g_modbus.state = MODBUS_STATE_IDLE;
    }
}

void libmodbusrtu_modbus_process(void)
{
    if (!g_modbus.get_tick_ms) {
        // Cannot process without tick function
        return;
    }
    
    if (g_modbus.state == MODBUS_STATE_RECEIVING) {
        uint32_t current_time = g_modbus.get_tick_ms();
        
        // Check if frame is complete (timeout occurred)
        if ((current_time - g_modbus.last_rx_time) >= MODBUS_TIMEOUT_MS) {
            g_modbus.state = MODBUS_STATE_PROCESSING;
            modbus_process_frame();
            
            // Reset for next frame
            g_modbus.rx_index = 0;
            g_modbus.state = MODBUS_STATE_IDLE;
        }
    }
}
