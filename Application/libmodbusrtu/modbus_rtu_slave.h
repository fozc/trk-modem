/**
 * @file modbus_rtu_slave.h
 * @brief Modbus RTU Slave Implementation for Cortex-M
 * @author Fatih Ozcan
 *         fatihozcan@gmail.com
 * @date 2025-10-17
 * 
 * Supports Function Codes:
 *  - FC03: Read Holding Registers
 *  - FC06: Write Single Register
 */

#ifndef MODBUS_RTU_SLAVE_H
#define MODBUS_RTU_SLAVE_H

#include <stdint.h>

/* Modbus Function Codes */
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06

/* Modbus Exception Codes */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION       0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     0x03

/* Configuration */
#define MODBUS_DEFAULT_SLAVE_ID     23
#define MODBUS_MAX_REGISTERS        11
#define MODBUS_TIMEOUT_MS           10
#define MODBUS_BUFFER_SIZE          256

/**
 * @brief Function Code 03 Callback Type
 * @param start_addr Starting register address
 * @param count Number of registers to read
 * @param data Pointer to output buffer (caller fills this)
 * @note Callback should fill data array with register values
 */
typedef void (*modbus_fc03_callback_t)(uint16_t start_addr, uint16_t count, uint16_t *data);

/**
 * @brief Function Code 06 Callback Type
 * @param addr Register address to write
 * @param value Value to write
 * @return 1 if success, 0 if failed
 */
typedef uint8_t (*modbus_fc06_callback_t)(uint16_t addr, uint16_t value);

/**
 * @brief Get system tick in milliseconds (platform-specific)
 * @return Current tick in milliseconds
 */
typedef uint32_t (*modbus_get_tick_ms_t)(void);

/**
 * @brief UART write function (platform-specific)
 * @param data Pointer to data buffer
 * @param len Length of data to send
 */
typedef void (*modbus_uart_write_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Initialize Modbus RTU Slave
 * @param slave_id Modbus slave ID (1-247)
 * @param get_tick_ms_func Function to get system tick in ms
 * @param uart_write_func Function to write data to UART
 */
void libmodbusrtu_slave_init(uint8_t slave_id,
                       modbus_get_tick_ms_t get_tick_ms_func,
                       modbus_uart_write_t uart_write_func);

/**
 * @brief Register callback for Function Code 03 (Read Holding Registers)
 * @param callback Pointer to callback function
 */
void libmodbusrtu_register_fc03_callback(modbus_fc03_callback_t callback);

/**
 * @brief Register callback for Function Code 06 (Write Single Register)
 * @param callback Pointer to callback function
 */
void libmodbusrtu_register_fc06_callback(modbus_fc06_callback_t callback);

/**
 * @brief Set Modbus slave ID
 * @param id New slave ID (1-247)
 */
void libmodbusrtu_modbus_set_slave_id(uint8_t id);

/**
 * @brief RX byte handler - call this from UART interrupt
 * @param byte Received byte
 * @note This function should be called for each received byte
 */
void libmodbusrtu_modbus_rx_handler(uint8_t byte);

/**
 * @brief Process Modbus frames - call periodically in main loop
 * @note Handles timeout detection and frame processing
 */
void libmodbusrtu_modbus_process(void);

#endif /* MODBUS_RTU_SLAVE_H */
