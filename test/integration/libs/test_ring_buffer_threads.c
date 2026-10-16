/**
 * @file test_ring_buffer_threads.c
 * @brief Host SPSC publication test using the production ring buffers.
 *
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "ring_buff.h"
#include "ring_buf.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define STREAM_LENGTH 200000U

static uint8_t backing[49152];
static rbuff_t ring;
static RingBuf reference;
static bool use_reference;

static uint8_t stream_byte(uint32_t position)
{
    return (uint8_t)((position ^ (position >> 8U) ^
                      (position >> 16U)) & 0xFFU);
}

static void *produce(void *argument)
{
    (void)argument;
    uint32_t position = 0U;
    uint32_t iteration = 0U;
    uint8_t data[63];
    while (STREAM_LENGTH > position)
    {
        uint32_t length = STREAM_LENGTH - position;
        const uint32_t mode = iteration++ % 3U;
        if (sizeof(data) < length)
        {
            length = sizeof(data);
        }
        if (use_reference || (0U == mode))
        {
            length = 1U;
        }
        if (!use_reference && (2U == mode))
        {
            uint8_t *block = NULL;
            uint32_t block_length = 0U;
            if (!rbuff_get_write_block(&ring, &block, &block_length))
            {
                (void)sched_yield();
                continue;
            }
            if (block_length < length)
            {
                length = block_length;
            }
            for (uint32_t index = 0U; length > index; index++)
            {
                block[index] = stream_byte(position + index);
            }
            assert(length == rbuff_advance(&ring, length));
        }
        else
        {
            if (!use_reference)
            {
                const uint32_t free = rbuff_available_for_write(&ring);
                if (free < length)
                {
                    length = free;
                }
                if (0U == length)
                {
                    (void)sched_yield();
                    continue;
                }
            }
            for (uint32_t index = 0U; length > index; index++)
            {
                data[index] = stream_byte(position + index);
            }
            const bool written = use_reference ?
                RingBuf_put(&reference, data[0]) :
                ((0U == mode) ?
                    (0U == rbuff_write_byte(&ring, data[0])) :
                    (0U == rbuff_write_buff(&ring, data, length)));
            if (!written)
            {
                (void)sched_yield();
                continue;
            }
        }
        position += length;
    }
    return NULL;
}

static void *consume(void *argument)
{
    (void)argument;
    uint32_t position = 0U;
    uint32_t iteration = 0U;
    uint8_t data[67];
    while (STREAM_LENGTH > position)
    {
        uint32_t length = 0U;
        const uint32_t mode = iteration++ % 3U;
        if (use_reference)
        {
            length = RingBuf_get(&reference, data) ? 1U : 0U;
        }
        else if (0U == mode)
        {
            length = rbuff_read_safe(&ring, data) ? 1U : 0U;
        }
        else if (1U == mode)
        {
            length = rbuff_read_buff(&ring, data, sizeof(data));
        }
        else
        {
            uint8_t *block = NULL;
            uint32_t block_length = 0U;
            if (rbuff_get_read_block(&ring, &block, &block_length))
            {
                length = (sizeof(data) < block_length) ?
                    sizeof(data) : block_length;
                memcpy(data, block, length);
                assert(length == rbuff_skip(&ring, length));
            }
        }
        if (0U == length)
        {
            (void)sched_yield();
        }
        assert(STREAM_LENGTH - position >= length);
        for (uint32_t index = 0U; length > index; index++)
        {
            assert(stream_byte(position + index) == data[index]);
        }
        position += length;
    }
    return NULL;
}

int main(void)
{
    static const uint16_t capacities[] = {2U, 3U, 6U, 8U, 17U, 257U, 49152U};
    for (size_t implementation = 0U; 2U > implementation; implementation++)
    {
        use_reference = (1U == implementation);
        for (size_t index = 0U; sizeof(capacities) / sizeof(capacities[0]) >
             index; index++)
        {
            pthread_t producer;
            pthread_t consumer;
            assert(rbuff_init(&ring, backing, capacities[index]));
            RingBuf_ctor(&reference, backing, capacities[index]);
            assert(0 == pthread_create(&consumer, NULL, consume, NULL));
            assert(0 == pthread_create(&producer, NULL, produce, NULL));
            assert(0 == pthread_join(producer, NULL));
            assert(0 == pthread_join(consumer, NULL));
            assert(0U == rbuff_available(&ring));
            assert(capacities[index] - 1U == RingBuf_num_free(&reference));
        }
    }
    puts("PASS: two threads, both queues, 7 capacities, mixed byte/block/zero-copy");
    return 0;
}

/*** end of file ***/
