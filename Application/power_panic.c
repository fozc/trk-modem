/*
 * power_panic.c
 *
 *  Created on: 11 Eyl 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "power_panic.h"
#include "bsp.h"
#include "console_logger.h"
#include "elog.h"
#include "gpio.h"
#include "gpio_defs.h"

/* EXTI15 owns the active flag and rearms the existing log latch on a
 * new falling edge. Main reads/sets the latch only with that IRQ excluded;
 * init uses the same BSP guard. No other ISR or DMA accesses this state.
 * CMSIS NVIC disable includes DSB/ISB; logging stays outside the guard. */
/* Zero means inactive; active values match elog_power_panic_reason_t. */
static volatile uint8_t power_panic_reason;
static volatile uint8_t panic_logged;

void power_panic_isr_falling_edge(void)
{
    power_panic_reason = ELOG_POWER_PANIC_FALLING_EDGE;
    panic_logged = 0U;
}

void power_panic_isr_rising_edge(void)
{
    if (gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN) != 0U)
    {
        power_panic_reason = ELOG_POWER_PANIC_UNKNOWN;
    }
}

void power_panic_check(void)
{
    const uint32_t was_enabled = bsp_power_panic_irq_disable();
    const uint8_t reason = power_panic_reason;
    const bool should_log = (reason != ELOG_POWER_PANIC_UNKNOWN) && (panic_logged == 0U) &&
                            (gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN) == 0U);
    if (should_log)
    {
        panic_logged = 1U;
    }
    bsp_power_panic_irq_restore(was_enabled);
    if (should_log)
    {
        CSLOG_ERR("[POWER_PANIC] Power panic detected! %s\r\n", (reason == ELOG_POWER_PANIC_STARTUP_LOW) ?
                  "Input LOW at startup" : "Falling edge detected");
        elog_log_power_panic((elog_power_panic_reason_t)reason);
    }
}

void power_panic_init(void)
{
    const uint32_t was_enabled = bsp_power_panic_irq_disable();
    /* Aynayi acilista esitle (MX_GPIO_Init PA7'yi LOW baslatir); hat LOW
     * ise bolum acik dogar - kenar uretmeyen boot-dusuk hat loglanabilsin. */
    uint8_t panic_level = gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN);

    power_panic_reason = (panic_level == 0U) ? ELOG_POWER_PANIC_STARTUP_LOW : ELOG_POWER_PANIC_UNKNOWN;
    panic_logged = 0U;
    gpio_set_pin(PWR_PANIC_MIRROR_GPIO, PWR_PANIC_MIRROR_PIN, panic_level);
    bsp_power_panic_irq_restore(was_enabled);
}

/*** end of file ***/
