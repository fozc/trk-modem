/*
 * rf_event_log.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Persistent storage of complete 60-byte RF event packets.
 */

#ifndef RF_EVENT_LOG_H
#define RF_EVENT_LOG_H

#include "rf_scp_codec.h"
#include <stddef.h>

typedef struct
{
    uint8_t data[RF_SCP_EVENT_SIZE];
} rf_event_record_t;

bool rf_event_log_init(void);

/* True only after valid input, successful write and verified readback.
 * Cooperative process context only; never call these APIs from an ISR.
 * Repeating an append can create a duplicate, preserving at-least-once
 * delivery when a consumer acknowledgment is lost across an RTU reboot.
 */
bool rf_event_log_append(const uint8_t *data, size_t length);

/* Newest first, skipping skip newest records. Count is buffer capacity.
 * Invalid input or read failure reports zero through out_count.
 */
bool rf_event_log_read_recent(size_t skip, size_t count,
                              rf_event_record_t *records,
                              size_t *out_count);

#endif /* RF_EVENT_LOG_H */

/*** end of file ***/
