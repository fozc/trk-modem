/*
 * modbus_process.h
 *
 *  Created on: Oct 26, 2025
 *      Author: fatih
 */

#ifndef MODBUS_PROCESS_H_
#define MODBUS_PROCESS_H_

#include <stdint.h>
#include "modbus_rtu_slave.h"


void modbus_process_init(void);

/**
 * @brief Wake the Modbus process so it parses a completed frame.
 * @note ISR-safe; call from the UART RX-timeout interrupt.
 */
void modbus_process_poll(void);

/**
 * @brief Feed one received byte to the Modbus slave - call from the UART RX ISR.
 *
 * Buffers the byte in the bus instance and, in software-timeout mode, wakes the
 * Modbus process. Keeps the slave instance private to modbus_process.c.
 * @param[in] byte Received byte.
 * @note ISR-safe.
 */
void modbus_process_isr_rx_byte(uint8_t byte);

/**
 * @brief Release the RS-485 bus after a DMA response has been transmitted.
 *
 * Called from HAL_UART_TxCpltCallback() (UART4 branch) once the final response
 * byte has been shifted out. De-asserts the MODBUS_OE driver-enable pin and
 * frees the transmit buffer for the next response.
 * @note ISR-safe.
 */
void modbus_process_tx_complete_callback(void);

#if (MODBUS_USE_HW_RTO == 1)
/**
 * @brief Signal a hardware frame-end - call from the UART RX-timeout ISR.
 * @note ISR-safe. Only available when MODBUS_USE_HW_RTO is 1.
 */
void modbus_process_isr_rx_timeout(void);
#endif

#endif /* MODBUS_PROCESS_H_ */
