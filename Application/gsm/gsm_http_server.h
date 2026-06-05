/*
 * gsm_http_server.h
 *
 *  Created on: Dec 20, 2025
 *      Author: fatih
 */

#ifndef GSM_HTTP_SERVER_H_
#define GSM_HTTP_SERVER_H_

#include <gsm_types.h>
#include <stdbool.h>



void gsm_http_server_reset(void);
void gsm_http_server_init(void);
void gsm_http_server_process(void);
void gsm_http_server_client_data_received(const uint8_t* data, uint16_t length);

#endif /* GSM_HTTP_SERVER_H_ */
