/*
 * test_ring_buf.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies FIFO ordering, capacity, wraparound, and bulk processing.
 */

#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ring_buf.h"

#define TEST_STORAGE_LEN 4U

static RingBuf ring;
static RingBufElement storage[TEST_STORAGE_LEN];
static RingBufElement handled[TEST_STORAGE_LEN];
static size_t handled_count;

static void capture_element(RingBufElement element)
{
    handled[handled_count] = element;
    handled_count++;
}

void setUp(void)
{
    handled_count = 0U;
    RingBuf_ctor(&ring, storage, TEST_STORAGE_LEN);
}

void tearDown(void)
{
}

void test_ring_buf_constructor_starts_empty_with_one_reserved_slot(void)
{
    RingBufElement output = 0xA5U;

    TEST_ASSERT_EQUAL_UINT16(TEST_STORAGE_LEN - 1U,
                             RingBuf_num_free(&ring));
    TEST_ASSERT_FALSE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, output);
}

void test_ring_buf_preserves_fifo_order(void)
{
    RingBufElement output;

    TEST_ASSERT_TRUE(RingBuf_put(&ring, 0x11U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 0x22U));
    TEST_ASSERT_EQUAL_UINT16(1U, RingBuf_num_free(&ring));

    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(0x11U, output);
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(0x22U, output);
    TEST_ASSERT_FALSE(RingBuf_get(&ring, &output));
}

void test_ring_buf_rejects_put_when_full(void)
{
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 1U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 2U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 3U));
    TEST_ASSERT_EQUAL_UINT16(0U, RingBuf_num_free(&ring));
    TEST_ASSERT_FALSE(RingBuf_put(&ring, 4U));
}

void test_ring_buf_wraps_head_and_tail_without_reordering(void)
{
    RingBufElement output;

    TEST_ASSERT_TRUE(RingBuf_put(&ring, 1U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 2U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 3U));
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 4U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 5U));

    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(3U, output);
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(4U, output);
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_HEX8(5U, output);
    TEST_ASSERT_EQUAL_UINT16(TEST_STORAGE_LEN - 1U,
                             RingBuf_num_free(&ring));
}

void test_ring_buf_process_all_visits_each_element_and_empties_buffer(void)
{
    RingBufElement output = 0U;

    TEST_ASSERT_TRUE(RingBuf_put(&ring, 0x31U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 0x32U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 0x33U));

    RingBuf_process_all(&ring, capture_element);

    TEST_ASSERT_EQUAL_size_t(3U, handled_count);
    TEST_ASSERT_EQUAL_HEX8(0x31U, handled[0]);
    TEST_ASSERT_EQUAL_HEX8(0x32U, handled[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33U, handled[2]);
    TEST_ASSERT_FALSE(RingBuf_get(&ring, &output));
    TEST_ASSERT_EQUAL_UINT16(TEST_STORAGE_LEN - 1U,
                             RingBuf_num_free(&ring));
}

void test_ring_buf_process_all_handles_wrapped_data(void)
{
    RingBufElement output;

    TEST_ASSERT_TRUE(RingBuf_put(&ring, 1U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 2U));
    TEST_ASSERT_TRUE(RingBuf_get(&ring, &output));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 3U));
    TEST_ASSERT_TRUE(RingBuf_put(&ring, 4U));

    RingBuf_process_all(&ring, capture_element);

    TEST_ASSERT_EQUAL_size_t(3U, handled_count);
    TEST_ASSERT_EQUAL_HEX8(2U, handled[0]);
    TEST_ASSERT_EQUAL_HEX8(3U, handled[1]);
    TEST_ASSERT_EQUAL_HEX8(4U, handled[2]);
}

void test_all_small_capacities_match_fifo_model_across_repeated_wraps(void)
{
    struct
    {
        uint8_t before[8];
        RingBufElement data[16];
        uint8_t after[8];
    } guarded;
    RingBufElement model[16];
    uint32_t random_state = 0x12345678U;

    for (uint16_t length = 1U; 16U >= length; length++)
    {
        size_t count = 0U;
        const size_t capacity = (size_t)length - 1U;
        (void)memset(&guarded, 0xA5, sizeof(guarded));
        RingBuf_ctor(&ring, guarded.data, length);
        for (uint32_t step = 0U; 4096U > step; step++)
        {
            random_state = random_state * 1664525U + 1013904223U;
            if (0U != (random_state & 0x80000000U))
            {
                const RingBufElement value = (RingBufElement)random_state;
                const bool accepted = count < capacity;
                TEST_ASSERT_EQUAL(accepted, RingBuf_put(&ring, value));
                if (accepted)
                {
                    model[count++] = value;
                }
            }
            else
            {
                RingBufElement out = 0xA5U;
                const bool available = 0U != count;
                TEST_ASSERT_EQUAL(available, RingBuf_get(&ring, &out));
                if (available)
                {
                    TEST_ASSERT_EQUAL_HEX8(model[0], out);
                    count--;
                    for (size_t index = 0U; count > index; index++)
                    {
                        model[index] = model[index + 1U];
                    }
                }
                else
                {
                    TEST_ASSERT_EQUAL_HEX8(0xA5U, out);
                }
            }
            TEST_ASSERT_EQUAL_UINT16(capacity - count, RingBuf_num_free(&ring));
            for (size_t index = 0U; sizeof(guarded.before) > index; index++)
            {
                TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.before[index]);
                TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.after[index]);
            }
        }
    }
}

/*** end of file ***/
