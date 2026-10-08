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

#include <stdbool.h>
#include "rf_scp_codec.h"

void rf_events_init(void);
void rf_events_notify(const rf_scp_message_t *message);
void rf_events_process(uint32_t now_ms);

/** BOLATeX BQ-03: run the pre-inventory ring protection (0x40 then a
 * no-op 0x46 at the current tail) after an MH restart; the inventory
 * upload starts only after successful protection. A failed pair uses
 * the existing event poll interval; the inventory remains paused. */
bool rf_events_boot_protect(void);
bool rf_events_inventory_allowed(void);

void rf_events_request_drain(void);
bool rf_events_drain_complete(uint32_t *total);
void rf_events_finish_drain(void);


#endif /* RF_EVENTS_H */

/*** end of file ***/
