/*
 * rtc.h
 *
 *  Created on: Mar 31, 2026
 *      Author: fatih.ozcan
 */

#ifndef BSP_RTC_H_
#define BSP_RTC_H_

#include <stdint.h>

typedef struct
{
	uint16_t millisec;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
}rtc_t;

void rtc_set(uint8_t second, uint8_t minute, uint8_t hour, uint8_t day, uint8_t month, uint8_t year);
rtc_t rtc_now(void);
const char * rtc_get_now_str(void);
void rtc_print_now(void);




#endif /* BSP_RTC_H_ */
