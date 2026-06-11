/*
 * bsp.c
 *
 *  Created on: Sep 16, 2025
 *      Author: fatih
 */
#include "bsp.h"
#include "main.h"
#include "contiki.h"
#include "gpio.h"
#include "uart.h"
#include "led_driver.h"
#include "gsm_signal_led.h"

#include <stdbool.h>

static const char *banner =  "\r\n+=================TROIKA======================+\r\n";


#define SYSTICK_US_DELAY
#define AHB_FREQ 96000000UL

#if ((AHB_FREQ / 1000000UL) > SysTick_LOAD_RELOAD_Msk)
	#error  "For usec delay, Reload value impossible"
#endif

static volatile uint32_t life_timer = 0;
static uint32_t base_epoch_time = 0;

void bsp_tick_handler(void)
{
	life_timer++;
	if(etimer_pending()) {
		etimer_request_poll();
	}

	clock_tick();

	void bsp_rtc_process(void);
	bsp_rtc_process();
}

static bsp_rtc_t rtc = {0, 0, 0, 0, 1, 1, 26};

void bsp_rtc_process(void)
{
	const int days_lookup[2][13] ={
			{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
			{ 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
	};

	if (rtc.millisec++ >= 1000)
	{
		rtc.millisec = 0;
		if (rtc.second++ == 60)
		{
			rtc.second = 0;
			rtc.minute++;
			if (rtc.minute == 60)
			{
				rtc.minute = 0;
				rtc.hour++;
				if (rtc.hour == 24)
				{
					rtc.hour = 0;
					rtc.day++;

					// days_lookup[leap_year ? 1 : 0][month]
					if (rtc.day > days_lookup[ ((rtc.year % 4 == 0 && rtc.year % 100 != 0) || (rtc.year % 400 == 0)) ? 1 : 0][rtc.month])
					{
						rtc.day = 1;
						rtc.month++;

						if (rtc.month == 13)
						{
							rtc.month = 1;
							rtc.year++;
						}
					}
				}
			}
		}
	}
}

void bsp_set_rtc_milisec(uint16_t milliseconds)
{
	rtc.millisec = milliseconds;
}

void bsp_set_rtc(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year)
{
    rtc.second = second;
    rtc.minute = minute;
    rtc.hour = hour;
    rtc.day = day;
    rtc.month = month;
    rtc.year = year;
}

bsp_rtc_t bsp_get_datetime(void)
{
	return rtc;
}

const char * bsp_get_rtc_str(void)
{
	static char buff[24] = {0};

	xsprintf(buff, "%02d/%02d/%02d %02d:%02d:%02d:%03d", rtc.year, rtc.month,
			rtc.day, rtc.hour, rtc.minute, rtc.second, rtc.millisec);

	return buff;
}
void bsp_print_datetime(void)
{
	xprintf("%02d/%02d/%02d %02d:%02d:%02d:%03d ", rtc.year, rtc.month,
			rtc.day, rtc.hour, rtc.minute, rtc.second, rtc.millisec);
}

uint32_t bsp_get_run_time(void)
{
	return life_timer;
}

uint32_t bsp_get_epoch_time(void)
{
	return (life_timer / 1000U) + base_epoch_time;
}

pdate_t bsp_get_pdate(void)
{
	return (pdate_t){
		.year = rtc.year,
		.month = rtc.month,
		.day = rtc.day,
		.hour = rtc.hour,
		.min = rtc.minute,
		.sec = rtc.second
	};
}

void bsp_set_epoch_time(uint32_t epoch_time)
{
	base_epoch_time = epoch_time - (life_timer / 1000U);
}

uint32_t bsp_get_tick(void)
{
	return life_timer;
}

void bsp_delay_us(uint32_t us)
{
#if defined(SYSTICK_US_DELAY)
	volatile uint32_t tmp = SysTick->CTRL; /* Clear the COUNTFLAG first */
	((void) tmp);

	SysTick->VAL   = 0UL;
	SysTick->LOAD  = (uint32_t)((AHB_FREQ / 1000000U)*us - 1UL);  /* set reload register */
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; //* Enable Systick */
	while((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U);

	SysTick->CTRL = 0; // Disable Systick
	SysTick->VAL   = 0UL;
#else
	bsp_delay(us);
#endif
}

uint32_t bsp_time_passes(uint32_t since)
{
	uint32_t now = life_timer;

	if(now >= since){
		return now - since;
	}

	return now + (1 + UINT32_MAX - since);
}

void bsp_delay_ms(uint32_t ms)
{
	uint32_t now = life_timer;
    while (bsp_time_passes(now) < ms){
    	//bsp_kick_wdt();
    }
}

void __attribute__ ((optimize("-O0"))) bsp_delay(uint32_t val)
{
	while(val--)
	{
		for(volatile uint8_t i = 0; i < 10; i++)
		{
			__NOP();
			__NOP();
			__NOP();
		}
	}
}

void bsp_putchr(int chr)
{
	//uart_send_byte(UART_1, chr);
	while (!LL_LPUART_IsActiveFlag_TXE_TXFNF(LPUART1));
	LL_LPUART_TransmitData8(LPUART1, (uint8_t)chr);
}


void bsp_kick_wdt(void)
{
#ifdef RELEASE
	gpio_toogle_pin(EXT_WDT_KICK_GPIO, EXT_WDT_KICK_PIN);
#endif
}

void bsp_init()
{
	bsp_kick_wdt();
	xdev_out(bsp_io_putchr);
}

void bsp_system_reset(void)
{
	CSLOG("BSP System Reset!\r\n");
	NVIC_SystemReset();
	while(1);
}

const char * bsp_get_banner(void)
{
	return banner;
}

/* -----------------------------------------------------------------------
 *  GSM signal LED — BSP override of weak hook
 *
 *  Translates (tech, level) from gsm_signal_led module into
 *  led_driver GSM mode commands for RGB2.
 * --------------------------------------------------------------------- */

void gsm_signal_led_apply(gsm_signal_tech_t  tech,
                           gsm_signal_level_t level)
{
    bool is_weak = (level == GSM_SIGNAL_LEVEL_WEAK);

    switch (tech)
    {
        case GSM_SIGNAL_TECH_2G:
            led_driver_set_gsm_mode(is_weak ? LED_GSM_2G_WEAK
                                            : LED_GSM_2G);
            break;

        case GSM_SIGNAL_TECH_3G:
            led_driver_set_gsm_mode(is_weak ? LED_GSM_3G_WEAK
                                            : LED_GSM_3G);
            break;

        case GSM_SIGNAL_TECH_4G:
            led_driver_set_gsm_mode(is_weak ? LED_GSM_4G_WEAK
                                            : LED_GSM_4G);
            break;

        case GSM_SIGNAL_TECH_NONE:
        default:
            led_driver_set_gsm_mode(LED_GSM_OFF);
            break;
    }
}



