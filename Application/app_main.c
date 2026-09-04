/*
 * app_main.c
 *
 *  Created on: Sep 16, 2025
 *      Author: Fatih Özcan
 *              fatihozcan@gmail.com
 */
#include "app_main.h"
#include "main.h"
#include "bsp.h"
#include "rtc.h"
#include "gpio.h"
#include "w25qxx.h"
#include "contiki.h"
#include "contiki_process.h"
#include "version.h"
#include "shell.h"
#include "nvram.h"
#include "rf_config.h"
#include "rf_scp.h"
#include "rf_comm.h"
#include "rf_shell.h"
#include "elog.h"
#include "iec104_elog.h"
#include "hardfault_handler.h"
#include "xmodem_process.h"
#include "modbus_process.h"
#include "breaker.h"
#include "iec104_process.h"
#include "iec104_event_log.h"
#include "web_server.h"
#include "gsm_process_contiki.h"
#include "modem_config.h"
#include "periodic_reset.h"
#include "app_ipc.h"
#include "boot.h"

#include "fault_log.h"
#include "stack_monitor.h"
#include "web_shell.h"
#include "led_driver.h"
#include "adc.h"
#include "digital_input.h"
#include "relay.h"

#include "rf_dummy.h"


#include "relay.h"
#include "power_board.h"
#include "system_status.h"
#include "reset_source.h"
#include "bms_reader.h"

PROCESS(heart_beat_process, "heart-beat");
PROCESS(rtc_resync_process, "rtc-resync");
PROCESS(fw_approve_process, "fw_approve_process");
AUTOSTART_PROCESSES(&heart_beat_process, &rtc_resync_process, &fw_approve_process);


PROCESS_THREAD(heart_beat_process, ev, data)
{
	(void)ev;
	(void)data;

	static struct etimer timer;
	static struct timer led_timer;
	static struct timer life_time_timer;
	static struct timer stack_timer;
	static struct timer rf_dummy_timer;

	PROCESS_BEGIN();

	etimer_set(&timer, CLOCK_SECOND / 100); // 10 Hz
	timer_set(&led_timer, CLOCK_SECOND / 2); // 0.5 second
	timer_set(&life_time_timer, CLOCK_SECOND); // 1 second
	timer_set(&stack_timer, CLOCK_SECOND * 60); // 10 seconds
	timer_set(&rf_dummy_timer, CLOCK_SECOND * 5); // 2 second RF dummy update

	while (1)
	{
		bsp_kick_wdt();

		if(timer_expired(&led_timer))
		{
			timer_reset(&led_timer);
			gpio_toggle_pin(LED1_GPIO, LED1_PIN);
		}

		led_driver_tick();

		if(timer_expired(&life_time_timer)){
			timer_reset(&life_time_timer);
			//nvram_lifetime_increment(1);
			modem_config_set_lifetime(modem_config_get_lifetime() + 1);

		}

		if(timer_expired(&stack_timer))
		{
			stack_monitor_stats_t s = stack_monitor_get_stats();
			const char *color = (s.free_bytes < 256U) ? XCOLOR_RED : XCOLOR_GREEN;
			CCSLOG(color, "[Stack] [%lu/%lu] bytes  (used/total, %lu free, %u%%)\r\n",
					s.used_bytes, s.total_bytes, s.free_bytes, s.usage_pct);

			timer_reset(&stack_timer);
		}

		if(timer_expired(&rf_dummy_timer))
		{
			timer_reset(&rf_dummy_timer);
			rf_dummy_tick();
		}

		periodic_reset_tick();

 		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
 		etimer_restart(&timer);
	}

	PROCESS_END();
}


PROCESS_THREAD(fw_approve_process, ev, data)
{
	(void)ev;
	(void)data;

	static struct etimer approve_timer;

	PROCESS_BEGIN();

	etimer_set(&approve_timer, (clock_time_t)(CLOCK_SECOND * 5));
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&approve_timer));

	if(app_ipc_is_firmware_approved())
	{
		CSLOG_WARN("Firmware already approved.\r\n");
	}
	else
	{
		int res = app_ipc_approve_firmware(false);

		if(res == APP_IPC_OK)
		{
			CSLOG_WARN("Firmware approved successfully!\r\n");
		}

		if(res < APP_IPC_OK)
		{
			CSLOG_ERR("Failed to approve firmware! Error code: %d\r\n", res);
		}
	}
	PROCESS_END();
}



/* Periodic anti-drift resync: reload the software RTC from the persistent
 * hardware RTC every 6 hours to compensate for SysTick accumulation error. */
#define RTC_RESYNC_INTERVAL_SEC  (6UL * 60UL * 60UL)

PROCESS_THREAD(rtc_resync_process, ev, data)
{
	(void)ev;
	(void)data;

	static struct etimer resync_timer;

	PROCESS_BEGIN();

	etimer_set(&resync_timer, (clock_time_t)(CLOCK_SECOND * RTC_RESYNC_INTERVAL_SEC));

	while (1)
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&resync_timer));

		if (rtc_hw_is_valid())
		{
			rtc_resync_sw_from_hw();
		}

		etimer_restart(&resync_timer);
	}

	PROCESS_END();
}

/* Persist the boot reset cause and any fault trace the previous run left
 * in the TAMP backup registers (hardfault handler stash). */
static void elog_log_boot_events(void)
{
	if (rtc_bkpr_read(HF_BKPR_DR_MAGIC) == HF_BKPR_MAGIC)
	{
		uint32_t pc = rtc_bkpr_read(HF_BKPR_DR_PC);
		uint32_t lr = rtc_bkpr_read(HF_BKPR_DR_LR);
		uint32_t cfsr = rtc_bkpr_read(HF_BKPR_DR_CFSR);
		uint32_t hfsr = rtc_bkpr_read(HF_BKPR_DR_HFSR);

		elog_log_hardfault(pc, lr, cfsr, hfsr);
		rtc_bkpr_write(HF_BKPR_DR_MAGIC, 0U);

		CSLOG_ERR("[ELOG] Previous run ended in HARDFAULT (pc=0x%08lX cfsr=0x%08lX)\r\n",
		          (unsigned long)pc, (unsigned long)cfsr);
	}

	elog_log_reset_cause((uint32_t)reset_source_get_flags(),
	                     reset_source_get_raw(),
	                     reset_source_is_abnormal());
}

__attribute__ ((noreturn)) void app_main(void)
{
	stack_monitor_init();
	bsp_init();
	led_driver_init();

	/* Seed the software RTC from the persistent hardware RTC (if valid). */
	rtc_boot_sync();

	w25qxx_init();
	//w25qxx_test();

	/* Resolves the A/B download target; must run before any update path. */
	boot_init();

	/* elog must be up before any module that logs to it (nvram recovery,
	 * power board, ...). */
	elog_init();
	elog_log_boot_events();
	iec104_elog_init();

	process_init();
	process_start(&etimer_process, NULL);
	autostart_start(autostart_processes);

	nvram_init();
	rf_store_init();   /* RF fider store'unu NVRAM last-known aynasindan yukle */
	//rf_dummy_init();
	shell_init("Troika >\r\n", NULL, bsp_putchr);

	reset_source_print();

	CSLOG("-Troika Smart Breaker Modem Started\r\n");
	CSLOG("Board started...Compile Time [%s %s]\r\n", __TIME__, __DATE__);
	CSLOG("Product Type: [%s]\r\n", PRODUCT_TYPE);
	CSLOG("Device Type: [%d] Dev Model: [%d] App Type: [%d]\r\n", DEVICE_TYPE, DEVICE_MODEL, APP_TYPE);
	CSLOG("Soft Ver: [%d.%d.%d.%d]\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_EXTRA);
	boot_log_installed_fw();

#ifndef DEBUG
	CSLOG("\r\nRunning in Release Mode\r\n");
	CSLOG("\r\n\tRunning in Release Mode\r\n");
	CSLOG("\r\n\t\tRunning in Release Mode\r\n");
#endif

	CSLOG("sizeof(nvram_t)        = %d bytes\r\n", sizeof(nvram_t));
	CSLOG("sizeof(breaker_t)      = %d bytes\r\n", sizeof(breaker_t));
	CSLOG("sizeof(iec104_config_t)= %d bytes\r\n", sizeof(iec104_config_t));
	CSLOG("sizeof(modem_config_t) = %d bytes\r\n", sizeof(modem_config_t));
	CSLOG("sizeof(iec104_line_config_t) = %d bytes\r\n", sizeof(iec104_line_config_t));
	CSLOG("sizeof(modbus_line_config_t) = %d bytes\r\n", sizeof(modbus_line_config_t));

	CSLOG("sizeof(nvram_t)        = %08d bytes\r\n", sizeof(nvram_t));
	CSLOG("sizeof(breaker_t)      = 0x%08X bytes\r\n", sizeof(breaker_t));

	elog_shell_init();
	iec104_elog_shell_init();
	xmodem_app_init();
	app_ipc_init();
	modbus_process_init();

	breaker_init();
	iec104_process_init();
	iec104_event_log_init(&(iec104_evtlog_cfg_t){
		.io_if = {
			.read = w25qxx_read_buff,
			.write = w25qxx_write_buff
		},
		.superblock_addr = IEC104_EVTLOG_SUPERBLOCK_ADDR,
		.superblock_backup_addr = IEC104_EVTLOG_SUPERBLOCK_BACKUP_ADDR,
		.data_addr = IEC104_EVTLOG_DATA_ADDR,
		.data_backup_addr = IEC104_EVTLOG_DATA_BACKUP_ADDR
	});

	gsm_process_contiki_init();

	digital_input_init();
	relay_init();

	/* RTU SCP adresi sabittir (R0 2.3b): 0x02. Hub 0x01, broadcast 0x00. */
	rf_comm_init(RF_SCP_ADDR_RTU);
	rf_shell_init();
	fault_log_init();
	web_shell_init(NULL);

	adc_init();
	power_board_init();

	LL_LPUART_EnableIT_RXNE_RXFNE(LPUART1);  /* Console RX */
	gpio_set_pin(MODBUS_EN_FLT_BSP_GPIO, MODBUS_EN_FLT_BSP_PIN, 1); // Modbus RS485 power enable

#ifdef LED_TEST
	led_test_all();
#endif

	system_status_init();
	//bms_reader_init();
	LL_USART_EnableIT_RXNE_RXFNE(UART5);  /* BMS RX */


	CSLOG("Board Initialization Completed.\r\n");

	while(1)
	{
		if(process_nevents())
		{
			while(process_run()); //consume all events
		}
	}
}

