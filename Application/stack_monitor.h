/*
 * stack_monitor.h
 *
 *  Created on: Mar 11, 2026
 *      Author: Fatih Ozcan
 *
 *  Stack high-water mark monitor using a paint-and-scan watermark technique.
 *
 *  Usage:
 *    1. Call stack_monitor_init() once early in main, before any deep call chains.
 *    2. Periodically call stack_monitor_get_stats() to read watermark statistics.
 *    3. Optionally call stack_monitor_reset() to repaint and restart measurements.
 *
 *  Note: Not interrupt-safe. Suitable for cooperative schedulers (e.g. Contiki).
 */

#ifndef STACK_MONITOR_H_
#define STACK_MONITOR_H_

#include <stdint.h>

/* ---- Configuration ---- */

/* Override this macro to plug in your own assert handler (e.g. configASSERT,
 * a breakpoint, or a fault logger). Default: no-op in release, __BKPT in debug. */
#ifndef STACK_MONITOR_ASSERT
#  ifdef DEBUG
#    define STACK_MONITOR_ASSERT(expr)  do { if (!(expr)) { __asm volatile ("bkpt #0"); } } while (0)
#  else
#    define STACK_MONITOR_ASSERT(expr)  ((void)(expr))
#  endif
#endif

/* ---- Public types ---- */

typedef struct
{
    uint32_t total_bytes;  /* Configured stack size (_Min_Stack_Size)         */
    uint32_t used_bytes;   /* High-water mark: maximum bytes ever used        */
    uint32_t free_bytes;   /* Bytes that have never been touched              */
    uint8_t  usage_pct;    /* used_bytes * 100 / total_bytes  (0..100)        */
} stack_monitor_stats_t;

/* ---- Public API ---- */

/* Paint the unused stack region with the watermark pattern and reset cache.
 * Must be called once at startup before any deep call chains. */
void stack_monitor_init(void);

/* Re-paint the currently free region and reset cache.
 * Useful in test environments where the high-water mark must be re-measured. */
void stack_monitor_reset(void);

/* Return all watermark statistics in a single traversal. */
stack_monitor_stats_t stack_monitor_get_stats(void);

/* Convenience getters — each delegates to stack_monitor_get_stats(). */
uint32_t stack_monitor_get_free(void);
uint32_t stack_monitor_get_used(void);
uint32_t stack_monitor_get_total(void);
uint8_t  stack_monitor_get_usage_pct(void);

#endif /* STACK_MONITOR_H_ */
