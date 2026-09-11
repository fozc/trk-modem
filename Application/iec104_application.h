/*
 * iec104_application.h
 *
 *  Created on: Mar 13, 2026
 *      Author: fatih
 */

#ifndef IEC104_APPLICATION_H_
#define IEC104_APPLICATION_H_

#include "types.h"
#include "iec104.h"
#include "sys/pt-sem.h"

void iec104_application_event_handler(iec104_event_t evt);
void iec104_application_init(void);

/* Uygulama katmani yayicilarini (GI grup 3/4 surecleri ve replay)
 * serilestiren semafor; iec104_replay.c paylasir. */
extern struct pt_sem iec104_tx_sem;

#endif /* IEC104_APPLICATION_H_ */
