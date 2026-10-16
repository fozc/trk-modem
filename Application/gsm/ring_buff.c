/**
 * @file  ring_buff.c
 * @brief Lock-free SPSC ring buffer using C11 atomics.
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Memory ordering contract (ARM Cortex-M single-core):
 *   - Producer: store data, then store head with memory_order_release.
 *   - Consumer: load head with memory_order_acquire, then read data.
 *   - Symmetric for tail (consumer stores, producer loads).
 *
 * This guarantees that data writes are visible before the index update,
 * even without disabling interrupts.
 *
 * Context rules and API contracts are documented in ring_buff.h.
 * Every public entry point validates the instance first: a zeroed
 * (never initialized) rbuff_t behaves as an empty/full buffer instead
 * of dereferencing a NULL backing store.
 */
#include "ring_buff.h"
#include <string.h>

/* Indices and counts stay below size. Subtract before adding so even
 * capacities near UINT32_MAX do not overflow during a wrap. */
static inline uint32_t wrap_index(const rbuff_t *rb, uint32_t index,
                                  uint32_t count)
{
    const uint32_t remaining = rb->buff_size - index;
    return (count >= remaining) ? count - remaining : index + count;
}

static inline uint32_t used_space(const rbuff_t *rb, uint32_t head,
                                  uint32_t tail)
{
    return (head >= tail) ? head - tail : rb->buff_size - (tail - head);
}

static inline uint32_t free_space(const rbuff_t *rb, uint32_t head,
                                  uint32_t tail)
{
    return rb->buff_size - 1U - used_space(rb, head, tail);
}

static inline bool rbuff_is_valid(const rbuff_t *p_rb)
{
    return (p_rb != NULL) && (p_rb->buff != NULL) && (p_rb->buff_size != 0U);
}

/* Index loads may be wider than uint32_t on LP64 hosts. The casts below
 * are safe: initialization stores zero and every index update wraps
 * within buff_size, so indices stay within the uint32_t buffer size. */

/* ------------------------------------------------------------------ */
/*  Init / Clear                                                       */
/* ------------------------------------------------------------------ */

bool rbuff_init(rbuff_t *p_rb, uint8_t *p_buff, uint32_t size)
{
    if ((p_rb == NULL) || (p_buff == NULL) || (size == 0U))
    {
        return false;
    }

    p_rb->buff      = p_buff;
    p_rb->buff_size = size;
    atomic_init(&p_rb->head, 0U);
    atomic_init(&p_rb->tail, 0U);

    return true;
}

void rbuff_clear(rbuff_t *p_rb)
{
    if (!rbuff_is_valid(p_rb))
    {
        return;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    atomic_store_explicit(&p_rb->tail, h, memory_order_release);
}

/* ------------------------------------------------------------------ */
/*  Producer (single writer context: ISR or thread)                    */
/* ------------------------------------------------------------------ */

uint32_t rbuff_available_for_write(rbuff_t *p_rb)
{
    if (!rbuff_is_valid(p_rb))
    {
        return 0U;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_acquire);

    return free_space(p_rb, h, t);
}

uint32_t rbuff_write_byte(rbuff_t *p_rb, uint8_t c)
{
    if (!rbuff_is_valid(p_rb))
    {
        return 1U; /* Not written */
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t next_h = wrap_index(p_rb, h, 1U);

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
    if (!rbuff_is_valid(p_rb) || (p_data == NULL))
    {
        return 1U;
    }

    if (len == 0U)
    {
        return 0U;
    }

    /* SPSC invariant: consumer can only advance tail (increase free space),
     * so the snapshot is a lower bound on the actual free space.
     * This makes the check-then-write pattern safe without locking. */
    const uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    const uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_acquire);
    const uint32_t available_space = free_space(p_rb, h, t);
    if (available_space < len)
    {
        return 1U;
    }

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

    atomic_store_explicit(&p_rb->head, wrap_index(p_rb, h, len),
                          memory_order_release);

    return 0U;
}

bool rbuff_get_write_block(rbuff_t *p_rb, uint8_t **pp_data, uint32_t *p_len)
{
    if ((pp_data == NULL) || (p_len == NULL))
    {
        return false;
    }
    *pp_data = NULL;
    *p_len = 0U;

    if (!rbuff_is_valid(p_rb))
    {
        return false;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_acquire);

    /* The -1 reserve is built into the free-space formula: the block can
     * never reach the tail, so wrapping head to 0 stays unambiguous. */
    uint32_t available_space = free_space(p_rb, h, t);
    if (available_space == 0U)
    {
        return false;
    }

    uint32_t len = p_rb->buff_size - h;
    if (len > available_space)
    {
        len = available_space;
    }

    *pp_data = &p_rb->buff[h];
    *p_len = len;
    return true;
}

uint32_t rbuff_advance(rbuff_t *p_rb, uint32_t len)
{
    if (!rbuff_is_valid(p_rb) || (len == 0U))
    {
        return 0U;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_relaxed);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_acquire);

    uint32_t available_space = free_space(p_rb, h, t);
    if (len > available_space)
    {
        len = available_space;
    }

    atomic_store_explicit(&p_rb->head, wrap_index(p_rb, h, len),
                          memory_order_release);
    return len;
}

/* ------------------------------------------------------------------ */
/*  Consumer (single reader context)                                   */
/* ------------------------------------------------------------------ */

uint32_t rbuff_available(rbuff_t *p_rb)
{
    if (!rbuff_is_valid(p_rb))
    {
        return 0U;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    return used_space(p_rb, h, t);
}

bool rbuff_peek(rbuff_t *p_rb, uint8_t *p_out)
{
    if (!rbuff_is_valid(p_rb) || (p_out == NULL))
    {
        return false;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    if (h == t)
    {
        return false;
    }

    *p_out = p_rb->buff[t];
    return true;
}

uint32_t rbuff_peek_buff(rbuff_t *p_rb, uint32_t skip, void *p_out, uint32_t len)
{
    if (!rbuff_is_valid(p_rb) || (p_out == NULL) || (len == 0U))
    {
        return 0U;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    uint32_t avail = used_space(p_rb, h, t);
    if (skip >= avail)
    {
        return 0U;
    }
    avail -= skip;
    t = wrap_index(p_rb, t, skip);

    uint32_t to_peek = (len > avail) ? avail : len;

    uint32_t first_chunk = p_rb->buff_size - t;
    if (first_chunk > to_peek)
    {
        first_chunk = to_peek;
    }

    memcpy(p_out, &p_rb->buff[t], first_chunk);

    uint32_t remaining = to_peek - first_chunk;
    if (remaining > 0U)
    {
        memcpy((uint8_t *)p_out + first_chunk, &p_rb->buff[0], remaining);
    }

    return to_peek;
}

bool rbuff_read_safe(rbuff_t *p_rb, uint8_t *p_out)
{
    if (!rbuff_is_valid(p_rb) || (p_out == NULL))
    {
        return false;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    if (h == t)
    {
        return false;
    }

    *p_out = p_rb->buff[t];
    atomic_store_explicit(&p_rb->tail, wrap_index(p_rb, t, 1U),
                          memory_order_release);

    return true;
}

uint32_t rbuff_read_buff(rbuff_t *p_rb, void *p_out, uint32_t len)
{
    if (!rbuff_is_valid(p_rb) || (p_out == NULL) || (len == 0U))
    {
        return 0U;
    }

    const uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    const uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);
    const uint32_t avail = used_space(p_rb, h, t);
    if (avail == 0U)
    {
        return 0U;
    }

    uint32_t to_read = (len > avail) ? avail : len;

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

    atomic_store_explicit(&p_rb->tail, wrap_index(p_rb, t, to_read),
                          memory_order_release);

    return to_read;
}

bool rbuff_get_read_block(rbuff_t *p_rb, uint8_t **pp_data, uint32_t *p_len)
{
    if ((pp_data == NULL) || (p_len == NULL))
    {
        return false;
    }
    *pp_data = NULL;
    *p_len = 0U;

    if (!rbuff_is_valid(p_rb))
    {
        return false;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    uint32_t avail = used_space(p_rb, h, t);
    if (avail == 0U)
    {
        return false;
    }

    uint32_t len = p_rb->buff_size - t;
    if (len > avail)
    {
        len = avail;
    }

    *pp_data = &p_rb->buff[t];
    *p_len = len;
    return true;
}

uint32_t rbuff_skip(rbuff_t *p_rb, uint32_t len)
{
    if (!rbuff_is_valid(p_rb) || (len == 0U))
    {
        return 0U;
    }

    uint32_t h =
        (uint32_t)atomic_load_explicit(&p_rb->head, memory_order_acquire);
    uint32_t t =
        (uint32_t)atomic_load_explicit(&p_rb->tail, memory_order_relaxed);

    uint32_t avail = used_space(p_rb, h, t);
    if (len > avail)
    {
        len = avail;
    }

    atomic_store_explicit(&p_rb->tail, wrap_index(p_rb, t, len),
                          memory_order_release);
    return len;
}

/*** end of file ***/
