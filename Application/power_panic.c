/*
 * power_panic.c
 *
 *  Created on: 11 Eyl 2026
 *      Author: fatih
 */
#include "power_panic.h"
#include "console_logger.h"
#include "elog.h"

/* EXTI15 (power-fail sinyali) kesmesi yazari, ana dongu okuyucu.
 * Bu modulun mevcut versiyonunda basit volatile bayrak yeterli
 * goruldu: yaninda yayinlanan ikinci bir veri yok, guc kaybi
 * hatti dustuktan sonra yeniden kenar uretmez. cortex-m-atomic-isr
 * politikasinin tam uygulamasi ileriki versiyona birakildi. */
static volatile uint8_t power_panic_flag = 0;

void power_panic_isr_handler(void)
{
    power_panic_flag = 1U;
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
    power_panic_flag = 0U;
}
