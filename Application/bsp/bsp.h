/*
 * bsp.h
 *
 *  Created on: Sep 16, 2025
 *      Author: fatih
 */
#ifndef BSP_BSP_H_
#define BSP_BSP_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include "xprintf.h"
#include "console_logger.h"


typedef union
{
    uint32_t raw;
    struct
	{
        uint32_t sec   : 6;  // 0-59
        uint32_t min   : 6;  // 0-59
        uint32_t hour  : 5;  // 0-23
        uint32_t day   : 5;  // 1-31
        uint32_t month : 4;  // 1-12
        uint32_t year  : 6;  // 0-63 (2025 + year)
    };
} pdate_t;

typedef struct
{
	uint16_t millisec;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
}bsp_rtc_t;

void bsp_set_rtc_milisec(uint16_t milliseconds);
void bsp_set_rtc(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year);
bsp_rtc_t bsp_get_datetime(void);
const char * bsp_get_rtc_str(void);
void bsp_print_datetime(void);


#define bsp_io_putchr  bsp_putchr //bsp_putchr // Console/Debug output
#define millis      bsp_get_tick

void bsp_tick_handler(void);
void bsp_init(void);
void bsp_delay(uint32_t);
void bsp_delay_us(uint32_t);
void bsp_delay_ms(uint32_t);
void bsp_kick_wdt(void);
void bsp_putchr(int chr);

void bsp_system_reset(void);
uint32_t bsp_get_tick(void);
uint32_t bsp_get_run_time(void);
uint32_t bsp_get_epoch_time(void);
void bsp_set_epoch_time(uint32_t epoch_time);
pdate_t bsp_get_pdate(void);
const char * bsp_get_banner(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BSP_H_ */
