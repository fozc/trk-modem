/*
 * iec104_application.h
 *
 *  Created on: Mar 13, 2026
 *      Author: fatih
 */

#ifndef IEC104_APPLICATION_H_
#define IEC104_APPLICATION_H_

#include "types.h"

typedef enum {
    IEC104_APP_EVT_SEND_TEMP_FAULTS = 1,
	IEC104_APP_EVT_SEND_PERM_FAULTS = 2,
	IEC104_APP_EVT_SOCKET_CLOSED = 3
} iec104_app_event_t;


void iec104_application_event_handler(iec104_app_event_t evt);

#endif /* IEC104_APPLICATION_H_ */
