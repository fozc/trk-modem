/**
 * @file  ring_buff.h
 * @brief Lock-free SPSC ring buffer for ISR/main communication.
 *
 * Buffer size must be a power of two. Uses C11 atomics for the
 * head/tail indices so that one context can write while another reads
 * without disabling interrupts.
 *
 * SPSC contract (single producer, single consumer):
 *   - Exactly ONE context produces (an ISR or a thread). Only the
 *     producer may call: rbuff_write_byte, rbuff_write_buff,
 *     rbuff_get_write_block, rbuff_advance, rbuff_available_for_write.
 *   - Exactly ONE context consumes (thread or ISR; never the producer
 *     side functions). Only the consumer may call: rbuff_read_safe,
 *     rbuff_read_buff, rbuff_peek, rbuff_peek_buff, rbuff_available,
 *     rbuff_get_read_block, rbuff_skip, rbuff_clear.
 *   - head is written only by the producer, tail only by the consumer.
 *     Calling a producer function from a second context corrupts the
 *     buffer silently.
 *
 * Ordering convention:
 *   - Producer writes data, then updates head (release).
 *   - Consumer reads data, then updates tail (release).
 *   - Each side loads the other's index with acquire ordering.
 *
 * Robustness: every function may be called on a zero-initialized
 * (never successfully initialized) instance - it returns the "empty"
 * or "full" result instead of dereferencing a NULL backing store.
 */
#ifndef RING_BUFF_H_
#define RING_BUFF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct
{
	uint8_t              *buff;
	uint32_t              buff_size; /**< Must be power of two */
	atomic_uint_fast32_t  head;      /**< Written by producer only */
	atomic_uint_fast32_t  tail;      /**< Written by consumer only */
} rbuff_t;

/**
 * Initialize the ring buffer. Must fully complete before the buffer is
 * shared between contexts.
 *
 * @param p_rb    Ring buffer instance
 * @param p_buff  Backing store; must stay valid for the instance lifetime
 * @param size    Size in bytes; must be a power of two
 *
 * @return true on success; false on NULL arguments or non-power-of-two size
 */
bool     rbuff_init                (rbuff_t *p_rb, uint8_t *p_buff, uint32_t size);

/**
 * Discard all pending data (consumer context only).
 *
 * Safe to call while the producer is writing: the buffer is emptied up
 * to a snapshot of head. A byte produced concurrently may be dropped or
 * kept depending on timing.
 *
 * @param p_rb Ring buffer instance
 */
void     rbuff_clear               (rbuff_t *p_rb);

/* ------------------------------------------------------------------ */
/*  Producer side (single writer context: ISR or thread)              */
/* ------------------------------------------------------------------ */

/**
 * Free capacity in bytes (producer context).
 * Lower bound if the consumer drains concurrently.
 *
 * @param p_rb Ring buffer instance
 *
 * @return Number of bytes currently writable (always <= size - 1)
 */
uint32_t rbuff_available_for_write (rbuff_t *p_rb);

/**
 * Write a single byte (producer context, ISR-safe).
 *
 * @param p_rb Ring buffer instance
 * @param c    Byte to write
 *
 * @return 0 on success; 1 when the buffer is full (or instance invalid)
 */
uint32_t rbuff_write_byte          (rbuff_t *p_rb, uint8_t c);

/**
 * Write a block (producer context). All-or-nothing policy: if the free
 * space is less than len, nothing is written and no state changes.
 *
 * @param p_rb   Ring buffer instance
 * @param p_data Source data
 * @param len    Number of bytes to write
 *
 * @return 0 when all len bytes were written; 1 on full/invalid arguments
 */
uint32_t rbuff_write_buff          (rbuff_t *p_rb, const void *p_data, uint32_t len);

/**
 * Get the contiguous free block for zero-copy writes (producer context).
 * Write into the returned block, then publish it with rbuff_advance().
 * Do NOT call rbuff_write_* between this call and rbuff_advance().
 *
 * @param p_rb     Ring buffer instance
 * @param pp_data  Out: block start, NULL when no space
 * @param p_len    Out: block length in bytes, 0 when no space
 *
 * @return true when a non-empty block is returned
 */
bool     rbuff_get_write_block     (rbuff_t *p_rb, uint8_t **pp_data, uint32_t *p_len);

/**
 * Publish len bytes previously written into the block returned by
 * rbuff_get_write_block() (producer context).
 *
 * @param p_rb Ring buffer instance
 * @param len  Number of bytes actually written by the producer
 *
 * @return Number of bytes published (clamped to free space)
 */
uint32_t rbuff_advance             (rbuff_t *p_rb, uint32_t len);

/* ------------------------------------------------------------------ */
/*  Consumer side (single reader context)                              */
/* ------------------------------------------------------------------ */

/**
 * Number of bytes pending (consumer context).
 * Lower bound if the producer fills concurrently.
 *
 * @param p_rb Ring buffer instance
 *
 * @return Number of bytes currently readable
 */
uint32_t rbuff_available           (rbuff_t *p_rb);

/**
 * Read one byte without consuming it (consumer context).
 *
 * @param p_rb  Ring buffer instance
 * @param p_out Oldest pending byte
 *
 * @return true when a byte was peeked; false when empty
 */
bool     rbuff_peek                (rbuff_t *p_rb, uint8_t *p_out);

/**
 * Read up to len bytes without consuming them (consumer context).
 * Partial: returns fewer bytes when less is buffered.
 *
 * @param p_rb  Ring buffer instance
 * @param skip  Bytes to skip before copying (0 = oldest byte)
 * @param p_out Destination buffer
 * @param len   Number of bytes to peek
 *
 * @return Number of bytes copied (0 when empty or skip >= available)
 */
uint32_t rbuff_peek_buff           (rbuff_t *p_rb, uint32_t skip, void *p_out, uint32_t len);

/**
 * Consume one byte (consumer context).
 *
 * @param p_rb  Ring buffer instance
 * @param p_out Oldest pending byte
 *
 * @return true when a byte was read; false when empty
 */
bool     rbuff_read_safe           (rbuff_t *p_rb, uint8_t *p_out);

/**
 * Consume up to len bytes (consumer context). Partial read: returns
 * fewer bytes when less is buffered.
 *
 * @param p_rb  Ring buffer instance
 * @param p_out Destination buffer
 * @param len   Number of bytes to read
 *
 * @return Number of bytes actually read and consumed
 */
uint32_t rbuff_read_buff           (rbuff_t *p_rb, void *p_out, uint32_t len);

/**
 * Get the contiguous pending block for zero-copy reads (consumer
 * context). Process the block, then consume it with rbuff_skip().
 * Do NOT call the rbuff_read or rbuff_peek functions between this call
 * and rbuff_skip().
 *
 * @param p_rb     Ring buffer instance
 * @param pp_data  Out: block start, NULL when empty
 * @param p_len    Out: block length in bytes, 0 when empty
 *
 * @return true when a non-empty block is returned
 */
bool     rbuff_get_read_block     (rbuff_t *p_rb, uint8_t **pp_data, uint32_t *p_len);

/**
 * Consume len bytes previously obtained from rbuff_get_read_block()
 * (consumer context).
 *
 * @param p_rb Ring buffer instance
 * @param len  Number of bytes processed by the consumer
 *
 * @return Number of bytes consumed (clamped to available)
 */
uint32_t rbuff_skip                (rbuff_t *p_rb, uint32_t len);

#endif /* RING_BUFF_H_ */
