/*
 * periodic_reset.c
 *
 *  Created on: Apr 27, 2026
 *      Author: Fatih Özcan
 *              fatihozcan@gmail.com
 *
 * Periodic system reset scheduler.
 */

#include "periodic_reset.h"

#include "modem_config.h"
#include "bsp.h"
#include "contiki.h"

#include <stdint.h>


static struct timer s_timer = {0};
static uint32_t     s_period_s = 0UL;

void periodic_reset_tick(void)
{
    uint32_t cfg_period = modem_config_get_reset_period();
    if (cfg_period != s_period_s)
    {
        CCSLOG(XCOLOR_CYAN,
               "[PeriodicReset] period updated: %lu s -> %lu s\r\n",
               s_period_s, cfg_period);
        s_period_s = cfg_period;
        if (s_period_s > 0UL)
        {
            timer_set(&s_timer, (clock_time_t)(s_period_s * (uint32_t)CLOCK_SECOND));
        }
    }

    if (s_period_s == 0UL)
    {
        return;
    }

    if (timer_expired(&s_timer))
    {
        CCSLOG(XCOLOR_RED,
               "[PeriodicReset] Period elapsed (%lu s) — resetting device.\r\n",
               s_period_s);
        bsp_system_reset();
    }
}
