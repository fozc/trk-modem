/*
 * stack_monitor.c
 *
 *  Created on: Mar 11, 2026
 *      Author: Fatih Özcan
 *              fatihozcan@gmail.com
 */
#include "stack_monitor.h"
#include <stddef.h>
#include "cmsis_compiler.h"  /* __get_MSP() */


#define STACK_WM_PATTERN      0xDEADBEEFU
#define STACK_WORDS_UNSCANNED UINT32_MAX    /* no scan performed yet */


/* Cached count of intact (free) watermark words from stack base.
 * STACK_WORDS_UNSCANNED means the next call must do a full upward scan. */
static uint32_t s_cached_free_words = STACK_WORDS_UNSCANNED;



/* Linker-provided symbols: _estack is the initial stack top (highest address),
 * _Min_Stack_Size is the configured stack size in bytes. */
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;

static inline uint32_t *stack_base(void)
{
    return (uint32_t *)((uint32_t)&_estack - (uint32_t)&_Min_Stack_Size);
}

static inline uint32_t stack_total_bytes(void)
{
    return (uint32_t)&_Min_Stack_Size;
}

/* Paint the region [base, current_sp) with the watermark pattern.
 * MUST NOT be called from an interrupt/exception handler: MSP in exception
 * context points to the interrupt frame, not the thread-mode stack top,
 * which would corrupt active stack frames. */
static void stack_paint(void)
{
    /* IPSR != 0 means CPU is executing an exception (IRQ, fault, SysTick…). */
    if (__get_IPSR() != 0U) {
        STACK_MONITOR_ASSERT(0); /* configurable hook — see stack_monitor.h */
        return;
    }

    uint32_t       *p          = stack_base();
    const uint32_t *current_sp = (const uint32_t *)__get_MSP();

    while (p < current_sp) {
        *p++ = STACK_WM_PATTERN;
    }
}

/* Scan from cached_end downward to find the new high-water boundary.
 * Uses array indexing (base[i]) — no pointer arithmetic, no OOB risk.
 * i > 0U guard prevents underflow of (i - 1U). */
static uint32_t scan_downward(const uint32_t *base)
{
    uint32_t i = s_cached_free_words;

    while (i > 0U && base[i - 1U] != STACK_WM_PATTERN) {
        i--;
    }

    return i;
}

/* ---- Public API ---- */

void stack_monitor_init(void)
{
    stack_paint();
    s_cached_free_words = STACK_WORDS_UNSCANNED;
}

void stack_monitor_reset(void)
{
    stack_paint();
    s_cached_free_words = STACK_WORDS_UNSCANNED;
}

stack_monitor_stats_t stack_monitor_get_stats(void)
{
    const uint32_t *base        = stack_base();  /* read-only after paint */
    const uint32_t  total       = stack_total_bytes();
    const uint32_t  total_words = total / 4U;

    if (s_cached_free_words == STACK_WORDS_UNSCANNED) {
        /* First call: full upward scan — find first non-watermark word */
        uint32_t i = 0U;
        while (i < total_words && base[i] == STACK_WM_PATTERN) {
            i++;
        }
        s_cached_free_words = i;
    } else {
        /* Subsequent calls: narrow downward scan from cached boundary */
        s_cached_free_words = scan_downward(base);
    }

    const uint32_t free_bytes = s_cached_free_words * 4U;
    const uint32_t used_bytes = (free_bytes <= total) ? (total - free_bytes) : total;
    const uint8_t  usage_pct  = (total > 0U) ? (uint8_t)(used_bytes * 100U / total) : 0U;

    return (stack_monitor_stats_t){
        .total_bytes = total,
        .used_bytes  = used_bytes,
        .free_bytes  = free_bytes,
        .usage_pct   = usage_pct,
    };
}

uint32_t stack_monitor_get_free(void)
{
    return stack_monitor_get_stats().free_bytes;
}

uint32_t stack_monitor_get_used(void)
{
    return stack_monitor_get_stats().used_bytes;
}

uint32_t stack_monitor_get_total(void)
{
    return stack_total_bytes();
}

uint8_t stack_monitor_get_usage_pct(void)
{
    return stack_monitor_get_stats().usage_pct;
}
