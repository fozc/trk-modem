/*
 * power_panic.h
 *
 *  Created on: 11 Eyl 2026
 *      Author: fatih
 */

#ifndef POWER_PANIC_H_
#define POWER_PANIC_H_

/* EXTI15 (PE15, LOW = panik) kenar kesmeleri icin: dusen kenar panik
 * bolumunu acar, yukselen kenar (toparlanma) kapatir. */
void power_panic_isr_falling_edge(void);
void power_panic_isr_rising_edge(void);
void power_panic_check(void);
void power_panic_init(void);


#endif /* POWER_PANIC_H_ */
