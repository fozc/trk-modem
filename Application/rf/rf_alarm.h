/*
 * rf_alarm.h
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Durable trip-failure delivery and receipt acknowledgement.
 */
#ifndef RF_ALARM_H
#define RF_ALARM_H

#include "rf_scp_codec.h"
#include "iec104_types.h"

void rf_alarm_init(void);
void rf_alarm_live(uint8_t source, const cp56time2a_t *time);
bool rf_alarm_record(const rf_scp_event_t *event);
void rf_alarm_process(uint32_t now_ms);
bool rf_alarm_is_idle(void);

#endif /* RF_ALARM_H */

/*** end of file ***/
