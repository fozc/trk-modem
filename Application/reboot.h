/*
 * reboot.h
 *
 *  Created on: 28 Haz 2026
 *      Author: fatih
 */

#ifndef REBOOT_H_
#define REBOOT_H_

#include <stdint.h>

void reboot_system(void);
void reboot_system_delayed(uint32_t delay_ms);

#endif /* REBOOT_H_ */
