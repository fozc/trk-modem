/**
 * @file  ring_buff.h
 * @brief Lock-free SPSC ring buffer for ISR/main communication.
 *
 * Buffer size must be a power of two. Uses C11 atomics for the
 * head/tail indices so that one context can write while another reads
 * without disabling interrupts.
 *
 * Convention:
 *   - Producer writes data, then updates head (release).
 *   - Consumer reads data, then updates tail (release).
 *   - Each side loads the other's index with acquire ordering.
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
	atomic_uint_fast32_t  head;      /**< Written by producer */
	atomic_uint_fast32_t  tail;      /**< Written by consumer */
} rbuff_t;

bool     rbuff_init                (rbuff_t *p_rb, uint8_t *p_buff, uint32_t size);
uint32_t rbuff_available_for_write (rbuff_t *p_rb);
uint32_t rbuff_write_byte          (rbuff_t *p_rb, uint8_t c);
uint32_t rbuff_write_buff          (rbuff_t *p_rb, const void *p_data, uint32_t len);
uint32_t rbuff_available           (rbuff_t *p_rb);
bool     rbuff_peek                (rbuff_t *p_rb, uint8_t *p_out);
bool     rbuff_read_safe           (rbuff_t *p_rb, uint8_t *p_out);
uint32_t rbuff_read_buff           (rbuff_t *p_rb, void *p_out, uint32_t len);
void     rbuff_clear               (rbuff_t *p_rb);

#endif /* RING_BUFF_H_ */
