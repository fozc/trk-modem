/*
 * libiec104_process.h
 *
 *  Created on: Oct 26, 2025
 *      Author: fatih
 */

#ifndef IEC104_PROCESS_H_
#define IEC104_PROCESS_H_

#include <stdint.h>

void iec104_process_socket_closed_cb(void);
void iec104_on_data(const uint8_t *data, uint16_t len, void *user);
void iec104_process_init(void);


void iec104_process_tx_buffer_dump(void);

#endif /* IEC104_PROCESS_H_ */
