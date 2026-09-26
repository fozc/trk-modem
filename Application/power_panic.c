/*
 * power_panic.c
 *
 *  Created on: 11 Eyl 2026
 *      Author: fatih
 */
#include "power_panic.h"
#include "console_logger.h"
#include "elog.h"
#include "gpio.h"
#include "gpio_defs.h"

/* PE15 (LOW = panik) panik bolumu izleyici.
 * Dusen kenar bolumu acar, yukselen kenar kapatir; hat reset olmadan
 * HIGH'a donebildiginden her LOW dususu taze bolum acar. Bayraga yalniz
 * EXTI15 kesmesi yazar, ana dongu okur -> volatile yeterli. Ana dongu:
 * bolum acik ve hat LOW -> bolum basina bir kez log + elog; hat HIGH ->
 * gecici darbe, log yok. PA7 aynasi ISR icinde (bkz. stm32u3xx_it.c). */
static volatile uint8_t power_panic_flag = 1;

/* Ana dongu ozel mandal: bolum basina tek log. Bayraga yazmaz. */
static uint8_t panic_logged = 0;

void power_panic_isr_falling_edge(void)
{
	power_panic_flag = 1U;
}

void power_panic_isr_rising_edge(void)
{
	/* Yalniz hat gercekten HIGH ise kapat: rise+fall ayni kesmede
	 * birlesirse LOW bitmisse bolum acik kalmali. */
	if (gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN) != 0U)
	{
		power_panic_flag = 0U;
	}
}

void power_panic_check(void)
{
	uint8_t flag = power_panic_flag;

	if (flag == 0U)
	{
		panic_logged = 0U;
		return;
	}

	if ((panic_logged == 0U) &&
	    (gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN) == 0U))
	{
		panic_logged = 1U;
		CSLOG_ERR("[POWER_PANIC] Power panic detected! "
		          "System will shutdown immediately.\r\n");
		elog_log_power_panic();
	}

	/* Bolum acik ama hat HIGH: gecici darbe, loglanmaz. */
}

void power_panic_init(void)
{
	/* Aynayi acilista esitle (MX_GPIO_Init PA7'yi LOW baslatir); hat LOW
	 * ise bolum acik dogar - kenar uretmeyen boot-dusuk hat loglanabilsin. */
	uint8_t panic_level = gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN);

	power_panic_flag = (panic_level == 0U) ? 1U : 0U;
	gpio_set_pin(PWR_PANIC_MIRROR_GPIO, PWR_PANIC_MIRROR_PIN, panic_level);
}
