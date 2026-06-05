/**
 * @file  ring_buff.c
 * @brief Lock-free SPSC ring buffer using C11 atomics.
 *
 * Memory ordering contract (ARM Cortex-M single-core):
 *   - Producer: store data, then store head with memory_order_release.
 *   - Consumer: load head with memory_order_acquire, then read data.
 *   - Symmetric for tail (consumer stores, producer loads).
 *
 * This guarantees that data writes are visible before the index update,
 * even without disabling interrupts.
 */
#include "ring_buff.h"
#include <string.h>

static inline bool is_power_of_two(uint32_t x)
{
    return (x != 0U) && ((x & (x - 1U)) == 0U);
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t mask(const rbuff_t *p_rb)
{
    return p_rb->buff_size - 1U;
}

/* ------------------------------------------------------------------ */
/*  Init / Clear                                                       */
/* ------------------------------------------------------------------ */

bool rbuff_init(rbuff_t *p_rb, uint8_t *p_buff, uint32_t size)
{
    if ((p_buff == NULL) || (size == 0U) || !is_power_of_two(size))
    {
        return false;
    }

    p_rb->buff      = p_buff;
    p_rb->buff_size = size;
    atomic_store_explicit(&p_rb->head, 0U, memory_order_relaxed);
    atomic_store_explicit(&p_rb->tail, 0U, memory_order_relaxed);

    return true;
}

void rbuff_clear(rbuff_t *p_rb)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_acquire);
    atomic_store_explicit(&p_rb->tail, h, memory_order_release);
}

/* ------------------------------------------------------------------ */
/*  Query                                                              */
/* ------------------------------------------------------------------ */

uint32_t rbuff_available(rbuff_t *p_rb)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    return (h - t) & mask(p_rb);
}

uint32_t rbuff_available_for_write(rbuff_t *p_rb)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&p_rb->tail, memory_order_acquire);

    return (t - h - 1U) & mask(p_rb);
}

/* ------------------------------------------------------------------ */
/*  Producer (ISR-safe single writer)                                  */
/* ------------------------------------------------------------------ */

uint32_t rbuff_write_byte(rbuff_t *p_rb, uint8_t c)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t next_h = (h + 1U) & mask(p_rb);

    /* Full check: load tail with acquire to see latest consumer progress */
    if (next_h == atomic_load_explicit(&p_rb->tail, memory_order_acquire))
    {
        return 1U; /* Buffer full */
    }

    p_rb->buff[h] = c;
    atomic_store_explicit(&p_rb->head, next_h, memory_order_release);

    return 0U;
}

uint32_t rbuff_write_buff(rbuff_t *p_rb, const void *p_data, uint32_t len)
{
    if ((p_rb == NULL) || (p_rb->buff == NULL) || (p_data == NULL))
    {
        return 1U;
    }

    if (len == 0U)
    {
        return 0U;
    }

    /* SPSC invariant: consumer can only advance tail (increase free space),
     * so free_space is a lower bound — actual free space >= free_space.
     * This makes the check-then-write pattern safe without locking. */
    uint32_t free_space = rbuff_available_for_write(p_rb);
    if (free_space < len)
    {
        return 1U;
    }

    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t first_chunk = p_rb->buff_size - h;

    if (first_chunk > len)
    {
        first_chunk = len;
    }

    memcpy(&p_rb->buff[h], p_data, first_chunk);

    uint32_t remaining = len - first_chunk;
    if (remaining > 0U)
    {
        memcpy(&p_rb->buff[0], (const uint8_t *)p_data + first_chunk, remaining);
    }

    atomic_store_explicit(&p_rb->head, (h + len) & mask(p_rb), memory_order_release);

    return 0U;
}

/* ------------------------------------------------------------------ */
/*  Consumer (main-context single reader)                              */
/* ------------------------------------------------------------------ */

bool rbuff_peek(rbuff_t *p_rb, uint8_t *p_out)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    if (h == t)
    {
        return false;
    }

    *p_out = p_rb->buff[t];
    return true;
}

bool rbuff_read_safe(rbuff_t *p_rb, uint8_t *p_out)
{
    uint32_t h = atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    if (h == t)
    {
        return false;
    }

    *p_out = p_rb->buff[t];
    atomic_store_explicit(&p_rb->tail, (t + 1U) & mask(p_rb), memory_order_release);

    return true;
}

uint32_t rbuff_read_buff(rbuff_t *p_rb, void *p_out, uint32_t len)
{
    if ((p_rb == NULL) || (p_out == NULL) || (len == 0U))
    {
        return 0U;
    }

    uint32_t avail = rbuff_available(p_rb);
    if (avail == 0U)
    {
        return 0U;
    }

    uint32_t to_read = (len > avail) ? avail : len;

    uint32_t t = atomic_load_explicit(&p_rb->tail, memory_order_relaxed);
    uint32_t first_chunk = p_rb->buff_size - t;

    if (first_chunk > to_read)
    {
        first_chunk = to_read;
    }

    memcpy(p_out, &p_rb->buff[t], first_chunk);

    uint32_t remaining = to_read - first_chunk;
    if (remaining > 0U)
    {
        memcpy((uint8_t *)p_out + first_chunk, &p_rb->buff[0], remaining);
    }

    atomic_store_explicit(&p_rb->tail, (t + to_read) & mask(p_rb), memory_order_release);

    return to_read;
}
