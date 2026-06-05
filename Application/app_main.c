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
#include "gpio.h"
#include "w25qxx.h"
#include "contiki.h"
#include "contiki_process.h"
#include "version.h"
#include "shell.h"
#include "nvram.h"
#include "elog.h"
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

#include "fault_log.h"
#include "stack_monitor.h"
#include "web_shell.h"

#include "rf_dummy.h"

PROCESS(heart_beat_process, "heart-beat");
AUTOSTART_PROCESSES(&heart_beat_process);


static uint16_t __attribute__((aligned(32))) adc_buffer[4] = {0};
static uint16_t hADCxConvertedData_Temperature_DegreeCelsius = 0;

#define ADC_VREF_MV 2500 /* VREFBUF at SCALE3 = 2.5V */
#define ADC_MAX_VAL 4095

/* Voltage divider: 16.2K + 10K, ADC reads across 10K

V_rail = V_adc * (R1 + R2) / R2 = V_adc * 262 / 100 */
#define DIVIDER_NUM 262
#define DIVIDER_DEN 100

static inline uint32_t adc_raw_to_mv(uint16_t raw)
{
    return ((uint32_t)raw * ADC_VREF_MV * DIVIDER_NUM) / ((uint32_t)ADC_MAX_VAL * DIVIDER_DEN);
}

void start_adc_conversion(void)
{
	extern ADC_HandleTypeDef hadc1;
	HAL_ADC_Start_DMA(&hadc1, (const uint32_t *)adc_buffer, 4);
}

uint16_t get_tdie(void)
{
	hADCxConvertedData_Temperature_DegreeCelsius = __LL_ADC_CALC_TEMPERATURE(2500, adc_buffer[4], LL_ADC_RESOLUTION_12B);
	return hADCxConvertedData_Temperature_DegreeCelsius;
}


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
		if(timer_expired(&led_timer))
		{
			timer_reset(&led_timer);
			gpio_toggle_pin(LED1_GPIO, LED1_PIN);
		}

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

__attribute__ ((noreturn)) void app_main(void)
{
	stack_monitor_init();
	bsp_init();
	w25qxx_init();
	//w25qxx_test();

	process_init();
	process_start(&etimer_process, NULL);
	autostart_start(autostart_processes);

	nvram_init();
	rf_dummy_init();

	CSLOG("Troika Smart Breaker Modem Started\r\n");
	CSLOG("Board started...Compile Time [%s %s]\r\n", __TIME__, __DATE__);
	CSLOG("Product Type: [%s]\r\n", PRODUCT_TYPE);
	CSLOG("Device Type: [%d] Dev Model: [%d] App Type: [%d]\r\n", DEVICE_TYPE, DEVICE_MODEL, APP_TYPE);
	CSLOG("Soft Ver: [%d.%d.%d.%d]\r\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_EXTRA);
	CSLOG("Git Commit: [%s]\r\n", GIT_COMMIT_HASH);



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


	elog_init(&(elog_io_cfg_t){
		.storage_if = (elog_storage_if_t){
			.read = w25qxx_read_buff,
			.write = w25qxx_write_buff
		},
		.super_block_addr = ELOG_SUPERBLOCK_ADDRESS,
		.log_block_addr = ELOG_LOGAREA_ADDRESS,
		.super_block_backup_paddr = ELOG_SUPERBLOCK_BACKUP_ADDRESS,
		.log_block_backup_addr = ELOG_LOGAREA_BACKUP_ADDRESS,
		.size = ELOG_LOG_AREA_SIZE
	});
	shell_init("Troika >\r\n", NULL, bsp_putchr);
	elog_shell_init();
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


	fault_log_init();

	web_shell_init(NULL);

	CSLOG("Board Initialization Completed.\r\n");

	LL_LPUART_EnableIT_RXNE_RXFNE(LPUART1);  /* Console RX */

	start_adc_conversion();

	while(1)
	{
		if(process_nevents())
		{
			while(process_run()); //consume all events
		}
	}
}

