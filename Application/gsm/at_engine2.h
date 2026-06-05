/*
 * at_engine2.h
 *
 *  Created on: Apr 11, 2026
 *      Author: fatih
 */

#ifndef GSM_AT_ENGINE2_H_
#define GSM_AT_ENGINE2_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	AT_ENGINE_RESULT_NONE,
	AT_ENGINE_RESULT_OK,
	AT_ENGINE_RESULT_ERROR,
	AT_ENGINE_RESULT_NO_SIM,
	AT_ENGINE_RESULT_TIMEOUT
} at_engine_result_t;

/* ---------- Debug ---------- */
void at_engine_set_log(bool is_enabled);
bool at_engine_get_log(void);

/* ---------- Lifecycle ---------- */
void at_engine_init(void);
void at_engine_reset(void);

/* ---------- Command API ---------- */
bool at_engine_is_busy(void);
bool at_engine_send_at_command(const void *p_cmd, uint16_t cmd_len,
                               const void *p_expected_response,
                               uint8_t expected_response_len,
                               uint8_t retry_count, uint32_t timeout);
bool at_engine_send_data(const uint8_t *p_data, uint16_t data_len, uint32_t timeout);
at_engine_result_t at_engine_get_result(void);
int32_t at_engine_process(void);

/* ---------- Response Access ---------- */

/**
 * @brief  Get pointer to the AT response buffer.
 *
 * @param[out] p_length  Receives the number of valid bytes in the buffer.
 *                       May be NULL if the caller only needs the pointer.
 *
 * @return Pointer to the internal response buffer (null-terminated).
 */
const uint8_t *at_engine_get_response(uint16_t *p_length);

/* ---------- Buffer Management ---------- */

/**
 * @brief Clear the RX ring buffer and the response accumulation buffer.
 */
void at_engine_clear_buff(void);

/**
 * @brief Cancel the running AT command and return to idle.
 */
void at_engine_cancel(void);

/* ---------- Diagnostics ---------- */

/**
 * @brief Get human-readable AT engine state string for diagnostic logging.
 */
const char *at_engine_get_state_str(void);

/* ---------- ISR Integration ---------- */

/**
 * @brief UART RX byte handler. Call from the USART1 RX interrupt.
 */
void at_engine_rx_byte(uint8_t byte);

/**
 * @brief Blocking send of raw bytes via the GSM UART.
 */
void at_engine_send_raw(const uint8_t *p_data, uint16_t len);

/**
 * @brief Non-blocking (interrupt-driven) send via the GSM UART.
 */
void at_engine_send_raw_async(uint8_t *p_data, uint16_t len);

/**
 * @brief Check whether a non-blocking transfer has completed.
 *
 * @return true when the previous at_engine_send_raw_async() has finished.
 */
bool at_engine_is_tx_done(void);

/**
 * @brief UART TXE (TX empty) ISR handler. Call from USART1 TXE interrupt.
 */
void at_engine_tx_empty_callback(void);

/**
 * @brief UART TC (transfer complete) ISR handler. Call from USART1 TC interrupt.
 */
void at_engine_tx_complete_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_AT_ENGINE2_H_ */
