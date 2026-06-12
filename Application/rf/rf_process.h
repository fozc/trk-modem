/*
 * rf_process.h
 *
 *  Created on: 12 Haz 2026
 *      Author: fatih
 */

#ifndef RF_RF_PROCESS_H_
#define RF_RF_PROCESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Feed one received RF byte into the RX ring buffer.
 *
 * Call from the USART3 RX interrupt handler. Keeps the ISR minimal:
 * the byte is only enqueued; SCP parsing happens in the process context.
 *
 * @param[in] data  Received byte.
 *
 * @note Thread-safe: single-writer (ISR). Do not call from multiple contexts.
 */
void rf_rx_interrupt_handler(uint8_t data);

/**
 * @brief Initialise the RF process and start it.
 *
 * Initialises the RX ring buffer and the SCP context, enables the UART RX
 * interrupt, and starts the Contiki process.
 *
 * @param[in] device_address  This node's SCP address (0x01-0xFE). Supplied
 *                            by the caller (configurable from outside).
 */
void rf_process_init(uint8_t device_address);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_PROCESS_H_ */
