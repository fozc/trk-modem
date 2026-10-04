/*
 * stm32u3xx_hal_rtc.h
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host RTC boundary declarations, without MCU register access.
 */
#ifndef TEST_HAL_RTC_H
#define TEST_HAL_RTC_H
#include "stm32u3xx_hal_rng.h"
typedef struct { uint32_t unused; } RTC_HandleTypeDef;
typedef struct
{
    uint8_t Hours, Minutes, Seconds, TimeFormat;
    uint32_t DayLightSaving, StoreOperation, SecondFraction, SubSeconds;
} RTC_TimeTypeDef;
typedef struct { uint8_t WeekDay, Month, Date, Year; } RTC_DateTypeDef;
typedef struct
{
    volatile uint32_t BKP1R, BKP2R, BKP3R, BKP4R, BKP5R, BKP6R;
} test_tamp_t;
extern test_tamp_t test_tamp;
#define TAMP_NS (&test_tamp)
#define RTC_BKP_DR0 0U
#define RTC_FORMAT_BIN 0U
#define RTC_HOURFORMAT12_AM 0U
#define RTC_DAYLIGHTSAVING_NONE 0U
#define RTC_STOREOPERATION_RESET 0U
#define RTC_WEEKDAY_MONDAY 1U
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *handle, uint32_t reg);
void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *handle, uint32_t reg,
                        uint32_t value);
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *handle,
                                 RTC_TimeTypeDef *value, uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *handle,
                                 RTC_DateTypeDef *value, uint32_t format);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *handle,
                                 RTC_TimeTypeDef *value, uint32_t format);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *handle,
                                 RTC_DateTypeDef *value, uint32_t format);
#endif
/*** end of file ***/
