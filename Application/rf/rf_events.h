/*
 * rf_events.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Retrieve and persist the MH event queue before consumption.
 */

#ifndef RF_EVENTS_H
#define RF_EVENTS_H

#include "rf_scp_codec.h"

void rf_events_init(void);
void rf_events_notify(const rf_scp_message_t *message);
void rf_events_process(uint32_t now_ms);

#endif /* RF_EVENTS_H */

/*** end of file ***/
