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

void iec104_application_event_handler(iec104_event_t evt);
void iec104_application_init(void);

#endif /* IEC104_APPLICATION_H_ */
