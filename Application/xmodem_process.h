/*
 * xmodem_process.h
 *
 *  Created on: Apr 28, 2026
 *      Author: fatih.ozcan
 */

#ifndef XMODEM_PROCESS_H_
#define XMODEM_PROCESS_H_

#include <stdbool.h>

void xmodem_process(void);
void xmodem_app_init(void);
bool xmodem_is_active(void);

#endif /* XMODEM_PROCESS_H_ */
