/*
 * bsp_delay_test_platform.h
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only register boundary for the real BSP microsecond delay.
 */
#ifndef BSP_DELAY_TEST_PLATFORM_H
#define BSP_DELAY_TEST_PLATFORM_H
#include <stdint.h>
typedef struct
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
} bsp_delay_test_systick_t;
bsp_delay_test_systick_t *bsp_delay_test_systick(void);
#define SysTick (bsp_delay_test_systick())
#define SysTick_LOAD_RELOAD_Msk 0xFFFFFFUL
#define SysTick_CTRL_COUNTFLAG_Msk (1UL << 16U)
#define SysTick_CTRL_CLKSOURCE_Msk (1UL << 2U)
#define SysTick_CTRL_ENABLE_Msk 1UL
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t state);
void NVIC_SystemReset(void);
#define LPUART1 ((void *)0)
uint32_t LL_LPUART_IsActiveFlag_TXE_TXFNF(void *uart);
void LL_LPUART_TransmitData8(void *uart, uint8_t value);
#endif
/*** end of file ***/
