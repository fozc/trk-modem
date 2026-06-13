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

#if (MODBUS_USE_HW_RTO == 1)
/**
 * @brief Signal a hardware frame-end - call from the UART RX-timeout ISR.
 * @note ISR-safe. Only available when MODBUS_USE_HW_RTO is 1.
 */
void modbus_process_isr_rx_timeout(void);
#endif

#endif /* MODBUS_PROCESS_H_ */
