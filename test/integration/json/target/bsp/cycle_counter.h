/*
 * cycle_counter.h
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Test-only CMSIS hardware boundary. Never part of a production image.
 */
#ifndef JSON_HW_CYCLE_COUNTER_H
#define JSON_HW_CYCLE_COUNTER_H

#include "stm32u3xx.h"

#include <stdbool.h>
#include <stdint.h>

static uint32_t saved_demcr;
static uint32_t saved_dwt_ctrl;

static bool cycle_counter_start(void)
{
    saved_demcr = DCB->DEMCR;
    saved_dwt_ctrl = DWT->CTRL;
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U)
    {
        return false;
    }
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    uint32_t before = DWT->CYCCNT;
    for (uint32_t index = 0U; index < 32U; ++index)
    {
        __NOP();
    }
    return DWT->CYCCNT != before;
}

static uint32_t cycle_counter_read(void)
{
    __DSB();
    __ISB();
    return DWT->CYCCNT;
}

static void cycle_counter_stop(void)
{
    DWT->CTRL = saved_dwt_ctrl;
    DCB->DEMCR = saved_demcr;
}

#endif
/*** end of file ***/
