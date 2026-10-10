/*
 * stm32u3xx.h
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host reset boundary for the isolated application IPC scenario.
 */
#ifndef TEST_APP_IPC_STM32U3XX_H
#define TEST_APP_IPC_STM32U3XX_H

void __DSB(void);
void NVIC_SystemReset(void);

#endif
/*** end of file ***/
