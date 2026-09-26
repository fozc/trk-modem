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

/* EXTI15 (power-fail sinyali, LOW = panik) kesmesi yazari, ana dongu okuyucu.
 * Kesme rising+falling tetiklenir; PA7 aynasi hiz icin dogrudan EXTI15
 * ISR'i icinde LL komutlariyla surulur (bkz. stm32u3xx_it.c) - bu modul
 * yalnizca panik bayragini yonetir. Bayragi sadece LOW seviye set eder;
 * yukselen kenar (toparlanma, HIGH) yanlis panik uretmez.
 * cortex-m-atomic-isr politikasinin tam uygulamasi ileriki versiyona
 * birakildi. */
static volatile uint8_t power_panic_flag = 0;

void power_panic_isr_handler(void)
{
    if (gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN) == 0U)
    {
        power_panic_flag = 1U;
    }
}

void power_panic_check(void)
{
    if (power_panic_flag)
    {
        power_panic_flag = 0U;
        CSLOG_ERR("[POWER_PANIC] Power panic detected! "
                  "System will shutdown immediately.\r\n");
        elog_log_power_panic();
    }
}

void power_panic_init(void)
{
    /* Acilista aynayi PE15'in anlik seviyesine esitle (MX_GPIO_Init PA7'yi
     * LOW baslatir; PE15 HIGH iken ayna yanlis kalirdi). Hat zaten LOW ise
     * (guc zaten yok) panik bayragi da set edilir. */
    uint8_t panic_level = gpio_read_pin(PWR_PANIC_BSP_GPIO, PWR_PANIC_BSP_PIN);

    power_panic_flag = (uint8_t)((panic_level == 0U) ? 1U : 0U);
    gpio_set_pin(PWR_PANIC_MIRROR_GPIO, PWR_PANIC_MIRROR_PIN, panic_level);
}
