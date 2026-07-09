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
#include "led_driver.h"
#include "adc.h"
#include "digital_input.h"
#include "relay.h"

#include "rf_dummy.h"
#include "rf_process.h"


#include "relay.h"

/* -----------------------------------------------------------------------
 *  LED hardware test — compile with -DLED_TEST to enable.
 *
 *  Blocking test that runs before Contiki starts.
 *  Cycles through every LED pin and every driver mode so each
 *  state can be visually verified on the board.
 * --------------------------------------------------------------------- */
//#define LED_TEST
#ifdef LED_TEST

#define LED_TEST_STEP_DELAY_MS   3000U
#define LED_TEST_BLINK_DELAY_MS  5000U

/**
 * @brief Blocking tick helper — runs led_driver_tick() in a busy loop
 *        for the given duration so blink patterns are visible.
 */
static void led_test_run_ticks(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < duration_ms)
    {
        led_driver_tick();
        HAL_Delay(50);
    }
}

static void led_test_all(void)
{
    CSLOG("\r\n========== LED TEST START ==========\r\n");

    /* ----------------------------------------------------------
     *  Phase 1: Raw GPIO — toggle each pin directly (bypass driver)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 1: Raw GPIO direct pin toggle ---\r\n");

    led_driver_init(); /* All OFF */

    CSLOG("[GPIO] LED1 (PE7, active-LOW) ON\r\n");
    gpio_set_pin(LED1_GPIO, LED1_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED1_GPIO, LED1_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED1 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] LED2 (PB2, active-LOW) ON\r\n");
    gpio_set_pin(LED2_GPIO, LED2_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED2_GPIO, LED2_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED2 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] LED3 (PB1, active-LOW) ON\r\n");
    gpio_set_pin(LED3_GPIO, LED3_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED3_GPIO, LED3_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED3 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 RED (PE11) ON\r\n");
    gpio_set_pin(LED_RGB1_R_GPIO, LED_RGB1_R_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_R_GPIO, LED_RGB1_R_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 RED OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 GREEN (PE12) ON\r\n");
    gpio_set_pin(LED_RGB1_G_GPIO, LED_RGB1_G_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_G_GPIO, LED_RGB1_G_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 GREEN OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 BLUE (PE13) ON\r\n");
    gpio_set_pin(LED_RGB1_B_GPIO, LED_RGB1_B_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_B_GPIO, LED_RGB1_B_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 BLUE OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 RED (PE8) ON\r\n");
    gpio_set_pin(LED_RGB2_R_GPIO, LED_RGB2_R_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_R_GPIO, LED_RGB2_R_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 RED OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 GREEN (PE9) ON\r\n");
    gpio_set_pin(LED_RGB2_G_GPIO, LED_RGB2_G_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_G_GPIO, LED_RGB2_G_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 GREEN OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 BLUE (PE10) ON\r\n");
    gpio_set_pin(LED_RGB2_B_GPIO, LED_RGB2_B_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_B_GPIO, LED_RGB2_B_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 BLUE OFF\r\n");
    HAL_Delay(500);

    CSLOG("\r\n--- Phase 1 complete ---\r\n\r\n");

    /* ----------------------------------------------------------
     *  Phase 2: LED driver modes — modem status (RGB1)
     * ---------------------------------------------------------- */
    CSLOG("--- Phase 2: Modem Status LED modes (RGB1) ---\r\n");
    led_driver_init();

    CSLOG("[MODEM] LED_MODEM_POWER_ON — Yellow 1000/1000\r\n");
    led_driver_set_modem_mode(LED_MODEM_POWER_ON);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_INIT — Yellow 250/250\r\n");
    led_driver_set_modem_mode(LED_MODEM_INIT);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_SEARCHING — Blue 1000/1000\r\n");
    led_driver_set_modem_mode(LED_MODEM_SEARCHING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_READY — Green 250/2750\r\n");
    led_driver_set_modem_mode(LED_MODEM_READY);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_NO_SIM — Red double pulse\r\n");
    led_driver_set_modem_mode(LED_MODEM_NO_SIM);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_ERROR — Red triple pulse\r\n");
    led_driver_set_modem_mode(LED_MODEM_ERROR);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_OFF\r\n");
    led_driver_set_modem_mode(LED_MODEM_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 3: LED driver modes — GSM network (RGB2)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 3: GSM Network LED modes (RGB2) ---\r\n");

    CSLOG("[GSM] LED_GSM_2G — Red 250/2750\r\n");
    led_driver_set_gsm_mode(LED_GSM_2G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_2G_WEAK — Red 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_2G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_3G — Blue 250/2750\r\n");
    led_driver_set_gsm_mode(LED_GSM_3G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_3G_WEAK — Blue 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_3G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_4G — Green solid\r\n");
    led_driver_set_gsm_mode(LED_GSM_4G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_4G_WEAK — Green 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_4G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_OFF\r\n");
    led_driver_set_gsm_mode(LED_GSM_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 4: LED driver modes — IEC104 listener (LED2)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 4: IEC104 Listener LED (LED2) ---\r\n");

    CSLOG("[IEC104] LED_LISTENER_LISTENING — Solid ON\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_LISTENING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[IEC104] LED_LISTENER_CONNECTED — Blink 250/250\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_CONNECTED);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[IEC104] LED_LISTENER_OFF\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 5: LED driver modes — Web listener (LED1)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 5: Web Listener LED (LED1) ---\r\n");

    CSLOG("[WEB] LED_LISTENER_LISTENING — Solid ON\r\n");
    led_driver_set_web_mode(LED_LISTENER_LISTENING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[WEB] LED_LISTENER_CONNECTED — Blink 250/250\r\n");
    led_driver_set_web_mode(LED_LISTENER_CONNECTED);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[WEB] LED_LISTENER_OFF\r\n");
    led_driver_set_web_mode(LED_LISTENER_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Done — reset all LEDs and continue boot
     * ---------------------------------------------------------- */
    led_driver_init();
    CSLOG("\r\n========== LED TEST COMPLETE ==========\r\n\r\n");
}

#endif /* LED_TEST */

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

	static struct timer three_second_timer;

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

__attribute__ ((noreturn)) void app_main(void)
{
	stack_monitor_init();
	bsp_init();
	led_driver_init();

	/* Seed the software RTC from the persistent hardware RTC (if valid). */
	rtc_boot_sync();

	w25qxx_init();
	//w25qxx_test();

	process_init();
	process_start(&etimer_process, NULL);
	autostart_start(autostart_processes);

	nvram_init();
	//rf_dummy_init();

	relay_init();


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

	digital_input_init();

	relay_init();

	/* TODO: source the RF SCP device address from configuration. */
	rf_process_init(1U);


	fault_log_init();

	web_shell_init(NULL);

	CSLOG("Board Initialization Completed.\r\n");

	adc_init();

#ifdef LED_TEST
	led_test_all();
#endif

	LL_LPUART_EnableIT_RXNE_RXFNE(LPUART1);  /* Console RX */

	gpio_set_pin(MODBUS_EN_FLT_BSP_GPIO, MODBUS_EN_FLT_BSP_PIN, 1); // Modbus RS485 power enable

	while(1)
	{
		if(process_nevents())
		{
			while(process_run()); //consume all events
		}
	}
}

