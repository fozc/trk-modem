/*
 * time_service.c
 *
 *  Created on: 14 Eyl 2025
 *      Author: fatih
 */
#include "time_service.h"
#include <stdatomic.h>

/* TIM17 ISR increments this independent counter; readers only need an
 * indivisible current value and do not publish other state. */
static _Atomic uint32_t system_ticks = 0U;

#if defined(__arm__) || defined(__thumb__)
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
               "system tick atomic must be lock-free");
#else
_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "system tick atomic must be lock-free");
#endif


#if defined(TEST) || defined(UNIT_TEST)
void time_service_reset(void)
{
    atomic_store_explicit(&system_ticks, 0U, memory_order_relaxed);
}

void time_service_set_ticks(uint32_t ticks)
{
    atomic_store_explicit(&system_ticks, ticks, memory_order_relaxed);
}
#endif


void time_service_tick(void)
{
    (void)atomic_fetch_add_explicit(&system_ticks, 1U,
                                    memory_order_relaxed);
}

uint32_t get_system_uptime(void)
{
    return atomic_load_explicit(&system_ticks, memory_order_relaxed);
}

uint32_t time_get_elapsed(uint32_t start_time)
{
    uint32_t current_time =
        atomic_load_explicit(&system_ticks, memory_order_relaxed);
    if (current_time >= start_time) {
        return current_time - start_time;
    } else {
        // Handle wrap-around
        return (UINT32_MAX - start_time + 1) + current_time;
    }
}

bool time_has_elapsed(uint32_t start_time, uint32_t duration)
{
    return time_get_elapsed(start_time) >= duration;
}




