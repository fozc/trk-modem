/*
 * gsm_process.c
 *
 *  Created on: Dec 25, 2025
 *      Author: fatih
 */
#include "gsm_process_contiki.h"
#include "at_engine2.h"
#include "gsm_engine.h"
#include "types.h"
#include "utils.h"
#include "bsp.h"
#include "contiki.h"
#include "uart.h"
#include "gsm_http_server.h"
#include "gsm_process.h"
#include "gsm_firmware_update.h"
#include "gsm_shell.h"
#include "gpio.h"
#include "led_driver.h"

PROCESS(gsm_power_on, "gsm_power_on");
PROCESS_THREAD(gsm_power_on, ev, data)
{
	static struct etimer timer;
	static struct timer sw_rdy_timer;
	static bool sw_rdy_asserted = false;
	static bool recovery_mode = false;

	PROCESS_BEGIN();

	/* process_start'in data parametresi kullanim amacini tasir:
	 * NULL = acilis - bugunku sinirsiz retry davranisi korunur,
	 * NULL degil = kurtarma - tek atimlik guc dizisi, tekrar yok. */
	if (ev == PROCESS_EVENT_INIT)
	{
		recovery_mode = (data != NULL);
	}
	else
	{
		/* Sonraki olaylarda INIT'te yakalanan deger gecerli kalir. */
	}

	while(1)
	{
		sw_rdy_asserted = false;

		led_driver_set_modem_mode(LED_MODEM_POWER_ON);

		gpio_set_pin(GSM_POWER_GPIO, GSM_POWER_PIN, GPIO_LOW);
		gpio_set_pin(GSM_ONOFF_BSP_GPIO, GSM_ONOFF_BSP_PIN, GPIO_LOW);

		etimer_set(&timer, 200);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

		CSLOG("Powering on GSM module...\r\n");
		gpio_set_pin(GSM_POWER_GPIO, GSM_POWER_PIN, GPIO_HIGH);
		etimer_set(&timer, 500);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

		CSLOG("Toggle GSM_ONOFF pulse (4.5s)\r\n");
		gpio_set_pin(GSM_ONOFF_BSP_GPIO, GSM_ONOFF_BSP_PIN, GPIO_HIGH);
		etimer_set(&timer, 4500);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
		gpio_set_pin(GSM_ONOFF_BSP_GPIO, GSM_ONOFF_BSP_PIN, GPIO_LOW);

		CSLOG("Waiting for GSM_SW_RDY (max 15s)...\r\n");

		timer_set(&sw_rdy_timer, 15000);

		while(1)
		{
			etimer_set(&timer, 50);
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

		    if (!gpio_read_pin(GSM_SW_RDY_BSP_GPIO, GSM_SW_RDY_BSP_PIN)) {
		    	sw_rdy_asserted = true;
		    	break;
		    }

		    if(timer_expired(&sw_rdy_timer))
		    {
		    	CSLOG_ERR("GSM_SW_RDY not asserted within 15s\r\n");
		    	break;
		    }

		  }

		if(sw_rdy_asserted){
			CSLOG("GSM_SW_RDY = HIGH (%lu ms)\r\n", timer_remaining(&sw_rdy_timer));
			break;
		}else if(recovery_mode){
			/* Kurtarma tek atimlik: tekrar deneme yok. Sonraki karar
			 * merdivenin ust kademelerinde (init -> ENHRST -> pin reset
			 * -> 2. deneme -> EWDT) verilir. */
			CSLOG_ERR("GSM power-on recovery failed\r\n");
			break;
		}else{
			CSLOG_WARN("Retrying GSM power on sequence...\r\n");
		}
	}

	PROCESS_END();
}


PROCESS(gsm_process_contiki, "gsm_process_contiki");
PROCESS_THREAD(gsm_process_contiki, ev, data)
{
	static struct etimer timer;
	static bool first_start = true;
	static const uint8_t recovery_marker = 1U;
	process_data_t power_on_data = NULL;

	PROCESS_BEGIN();

	while(1)
	{
		/* Guc dizisi oncesi RX kesmesini kapat ve bayragi temizle;
		 * boylece kurtarma yeniden baslatmasi acilis yoluyla birebir
		 * ayni kosullarda kosar (acilista kesme zaten kapalidir). */
		uart_set_rx_interrupt(UART_1, UART_RX_INT_DISABLE);
		gsm_clear_module_restart();

		if (!first_start)
		{
			power_on_data = (process_data_t)&recovery_marker;
		}
		first_start = false;

		process_start(&gsm_power_on, power_on_data);
		PROCESS_WAIT_EVENT_UNTIL((ev == PROCESS_EVENT_EXITED) &&
		                         (data == &gsm_power_on));

		at_engine_init();
		at_engine_reset();
		gsm_process_init_old();
		gsm_http_server_init();
		gsm_firmware_update_init();
		gsm_firmware_update_rfwu_init();
		gsm_shell_init();

		uart_set_rx_interrupt(UART_1, UART_RX_INT_ENABLE);

		etimer_set(&timer, 50);
		while (!gsm_module_restart_requested())
		{
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
			etimer_restart(&timer);

			gsm_process_old();
			(void)at_engine_process();
			gsm_http_server_process();

		}

		/* Modem kurtarma merdiveni tam guc cevrimi istedi: dongu
		 * basina donulur - gsm_power_on dizisi + tam yeniden init.
		 * Zombi LC riski yoktur: ayni protothread'in duz C akisinda
		 * ilerlenir, paylasilan pt uzerine baska yazma olmaz. */
	}

	PROCESS_END();
}


void gsm_process_contiki_init(void)
{
	process_start(&gsm_process_contiki, NULL);
}



