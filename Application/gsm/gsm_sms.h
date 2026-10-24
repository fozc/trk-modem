/*
 * gsm_sms.h
 *
 *  Created on: Oct 24, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Received SMS parsing and callback interface.
 */

#ifndef GSM_SMS_H
#define GSM_SMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a complete received CMGR response and invoke its callback.
 * @return true for REC READ/UNREAD; false preserves is_unread.
 */
bool gsm_sms_handle_response(const uint8_t *response, size_t response_len,
                             bool *is_unread);

/** @brief Log the sender and received text; lengths exclude terminators. */
void gsm_sms_received_callback(const uint8_t *sender, size_t sender_len,
                               const uint8_t *message, size_t message_len);

#ifdef __cplusplus
}
#endif

#endif /* GSM_SMS_H */

/*** end of file ***/
