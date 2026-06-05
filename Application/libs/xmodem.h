/*
 * xmodem.h
 *
 *  Created on: 23 May 2018
 *      Author: fozcan
 */

#ifndef LIBXMODEM_H_
#define LIBXMODEM_H_

#include <stdint.h>

//#define XMODEM_USE_CACHE

#define XMODEM_EVT_FILE_DOWNLOAD_STARTED  0x01
#define XMODEM_EVT_FILE_DOWNLOAD_CANCELED 0x02
#define XMODEM_EVT_FILE_DOWNLOAD_COMPLETE 0x04

#define XMODEM_EVT_FILE_CHUNK_RECEIVED    0x10


typedef struct
{
	void (*event_callback)(uint8_t evt, const void *buff, uint32_t size);
	void (*write_byte)(int data);
}xmodem_config_t;


void xmodem_poll         	(void);
void xmodem_timer_process	(void);
void xmodem_rx_isr			(uint8_t rx_data);
int  xmodem_init            (xmodem_config_t cfg_);


#endif /* XMODEM_H_ */

