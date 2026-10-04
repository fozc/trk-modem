/*
 * main.h
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only CMSIS instruction shim for driver tests.
 */
#ifndef TEST_SUPPORT_MAIN_H
#define TEST_SUPPORT_MAIN_H

#include "stm32u3xx_hal_rtc.h"
/* Host tests check SPI transactions, not MCU delay timing. */
#define __NOP() ((void)0)

#endif
/*** end of file ***/
