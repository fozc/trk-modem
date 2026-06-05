/*
 * web_server.h
 *
 *  Created on: 12 Eki 2025
 *      Author: fatih
 */

#ifndef WEB_SERVER_H_
#define WEB_SERVER_H_

#include <stdint.h>


typedef struct
{
    int (*send)(const void *data, uint32_t length);

}web_server_io_t;



void web_server_init(web_server_io_t *io_);
void webserver_on_received(const uint8_t *data, int len);
void webserver_on_connection_closed(void);

#endif /* WEB_SERVER_H_ */
