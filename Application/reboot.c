/*
 * reboot.c
 *
 *  Created on: 28 Haz 2026
 *      Author: fatih
 */
#include "reboot.h"
#include "bsp.h"
#include "contiki.h"
#include "stm32u3xx.h"

void reboot_system(void)
{
	CCSLOG(XCOLOR_RED, "Rebooting system...\r\n");
	NVIC_SystemReset();
	while(1)
	{
		/* Wait for reset */
	}
}


PROCESS(reboot_process, "reboot_process");
PROCESS_THREAD(reboot_process, ev, data)
{
    static struct etimer timer;

    PROCESS_BEGIN();

    uint32_t delay_ms = *((uint32_t *)data);
    etimer_set(&timer, delay_ms);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    etimer_restart(&timer);

    reboot_system();

    PROCESS_END();
}


void reboot_system_delayed(uint32_t delay_ms)
{
	CCSLOG(XCOLOR_YELLOW, "Rebooting system in %d ms...\r\n", delay_ms);

	process_start(&reboot_process, &delay_ms);
}

