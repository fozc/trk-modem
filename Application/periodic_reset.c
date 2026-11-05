/*
 * periodic_reset.c
 *
 *  Created on: Apr 27, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Periodic system reset scheduler.
 */

#include "periodic_reset.h"

#include "modem_config.h"
#include "runtime_stats.h"
#include "rtc.h"
#include "bsp.h"
#include "contiki.h"

#include <stdint.h>
#include <stdbool.h>

#define UPDATE_RESET_DELAY_MS (10U * 60U * 1000U)

static struct timer s_timer = {0};
static uint32_t     s_period_s = 0UL;
static uint32_t update_start_tick;
static bool update_reset_deferred;

/* Called by update start paths in cooperative main context. */
void periodic_reset_defer_for_update(void)
{
    update_start_tick = bsp_get_tick();
    update_reset_deferred = true;
}

void periodic_reset_tick(void)
{
    uint32_t cfg_period = modem_config_get_reset_period();

    /* Eski NVRAM degerlerinde kalmis dev periyotlar da kiskalanir */
    if (cfg_period > PERIODIC_RESET_PERIOD_MAX_S)
    {
        cfg_period = PERIODIC_RESET_PERIOD_MAX_S;
    }

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

    if (update_reset_deferred)
    {
        if ((uint32_t)(bsp_get_tick() - update_start_tick) < UPDATE_RESET_DELAY_MS)
        {
            return;
        }
        update_reset_deferred = false;
    }

    if (timer_expired(&s_timer))
    {
        CCSLOG(XCOLOR_RED,
               "[PeriodicReset] Period elapsed (%lu s) - resetting device.\r\n",
               s_period_s);
        if (!runtime_stats_save())
        {
            CSLOG_ERR("[PeriodicReset] Runtime stats save failed; resetting\r\n");
        }
        rtc_bkpr_write(RTC_BKPR_PERIODIC_RESET_DR, RTC_BKPR_PERIODIC_RESET_MAGIC);
        bsp_system_reset();
    }
}

/*** end of file ***/
