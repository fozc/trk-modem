/*
 * test_ring_buff.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies the SPSC ring buffer contract on the host.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ring_buff.h"

#define TEST_BUFFER_SIZE 8U

static rbuff_t ring;
static uint8_t storage[TEST_BUFFER_SIZE];

void setUp(void)
{
    memset(storage, 0, sizeof(storage));
    TEST_ASSERT_TRUE(rbuff_init(&ring, storage, sizeof(storage)));
}

void tearDown(void)
{
}

void test_ring_buff_rejects_invalid_initialization(void)
{
    rbuff_t local_ring;
    uint8_t local_storage[8];

    TEST_ASSERT_FALSE(rbuff_init(NULL, local_storage,
                                 sizeof(local_storage)));
    TEST_ASSERT_FALSE(rbuff_init(&local_ring, NULL,
                                 sizeof(local_storage)));
    TEST_ASSERT_FALSE(rbuff_init(&local_ring, local_storage, 0U));
    TEST_ASSERT_FALSE(rbuff_init(&local_ring, local_storage, 6U));
}

void test_ring_buff_keeps_fifo_order_and_reserves_one_slot(void)
{
    uint8_t value = 0U;

    TEST_ASSERT_EQUAL_UINT32(TEST_BUFFER_SIZE - 1U,
                             rbuff_available_for_write(&ring));

    for (uint32_t i = 0U; i < (TEST_BUFFER_SIZE - 1U); i++)
    {
        TEST_ASSERT_EQUAL_UINT32(0U,
                                 rbuff_write_byte(&ring,
                                                  (uint8_t)(0x20U + i)));
    }

    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_byte(&ring, 0xFFU));
    TEST_ASSERT_EQUAL_UINT32(TEST_BUFFER_SIZE - 1U,
                             rbuff_available(&ring));

    for (uint32_t i = 0U; i < (TEST_BUFFER_SIZE - 1U); i++)
    {
        TEST_ASSERT_TRUE(rbuff_read_safe(&ring, &value));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x20U + i), value);
    }

    TEST_ASSERT_FALSE(rbuff_read_safe(&ring, &value));
}

void test_ring_buff_block_write_is_all_or_nothing(void)
{
    static const uint8_t first[] = {1U, 2U, 3U, 4U, 5U};
    static const uint8_t rejected[] = {6U, 7U, 8U};
    uint8_t output[sizeof(first)] = {0U};

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, first, sizeof(first)));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             rbuff_write_buff(&ring, rejected,
                                              sizeof(rejected)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(first), rbuff_available(&ring));
    TEST_ASSERT_EQUAL_UINT32(sizeof(output),
                             rbuff_read_buff(&ring, output,
                                             sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, output, sizeof(first));
}

void test_ring_buff_wraps_block_copy_without_reordering(void)
{
    static const uint8_t prefill[] = {1U, 2U, 3U, 4U, 5U, 6U};
    static const uint8_t wrapped[] = {0xA0U, 0xA1U, 0xA2U, 0xA3U};
    uint8_t output[sizeof(prefill)] = {0U};

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, prefill,
                                              sizeof(prefill)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(prefill),
                             rbuff_read_buff(&ring, output,
                                             sizeof(output)));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, wrapped,
                                              sizeof(wrapped)));

    memset(output, 0, sizeof(output));
    TEST_ASSERT_EQUAL_UINT32(sizeof(wrapped),
                             rbuff_read_buff(&ring, output,
                                             sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wrapped, output, sizeof(wrapped));
}

void test_ring_buff_zero_copy_publish_and_consume_are_clamped(void)
{
    uint8_t *block = NULL;
    uint32_t block_len = 0U;

    TEST_ASSERT_TRUE(rbuff_get_write_block(&ring, &block, &block_len));
    TEST_ASSERT_NOT_NULL(block);
    TEST_ASSERT_EQUAL_UINT32(TEST_BUFFER_SIZE - 1U, block_len);

    for (uint32_t i = 0U; i < block_len; i++)
    {
        block[i] = (uint8_t)(0x60U + i);
    }

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&ring));
    TEST_ASSERT_EQUAL_UINT32(block_len,
                             rbuff_advance(&ring, block_len + 5U));
    TEST_ASSERT_TRUE(rbuff_get_read_block(&ring, &block, &block_len));
    TEST_ASSERT_EQUAL_UINT32(TEST_BUFFER_SIZE - 1U, block_len);
    TEST_ASSERT_EQUAL_HEX8(0x60U, block[0]);
    TEST_ASSERT_EQUAL_HEX8(0x66U, block[6]);
    TEST_ASSERT_EQUAL_UINT32(block_len,
                             rbuff_skip(&ring, block_len + 5U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&ring));
}

void test_ring_buff_zero_initialized_instance_is_safe(void)
{
    rbuff_t zero = {0};
    uint8_t value = 0U;

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&zero));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available_for_write(&zero));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_byte(&zero, 0x11U));
    TEST_ASSERT_FALSE(rbuff_read_safe(&zero, &value));
    rbuff_clear(&zero);
}

void test_ring_buff_size_one_is_valid_but_has_no_capacity(void)
{
    rbuff_t local_ring;
    uint8_t local_storage[1] = {0U};
    uint8_t value = 0U;

    TEST_ASSERT_TRUE(rbuff_init(&local_ring, local_storage,
                                sizeof(local_storage)));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_available_for_write(&local_ring));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             rbuff_write_byte(&local_ring, 0x55U));
    TEST_ASSERT_FALSE(rbuff_read_safe(&local_ring, &value));
}

void test_ring_buff_peek_does_not_consume_data(void)
{
    static const uint8_t input[] = {0x11U, 0x22U, 0x33U};
    uint8_t value = 0U;

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, input,
                                              sizeof(input)));
    TEST_ASSERT_TRUE(rbuff_peek(&ring, &value));
    TEST_ASSERT_EQUAL_HEX8(input[0], value);
    TEST_ASSERT_EQUAL_UINT32(sizeof(input), rbuff_available(&ring));
    TEST_ASSERT_TRUE(rbuff_read_safe(&ring, &value));
    TEST_ASSERT_EQUAL_HEX8(input[0], value);
}

void test_ring_buff_peek_block_supports_skip_partial_and_wrap(void)
{
    static const uint8_t first[] = {1U, 2U, 3U, 4U, 5U, 6U};
    static const uint8_t second[] = {7U, 8U, 9U, 10U};
    static const uint8_t expected[] = {6U, 7U, 8U, 9U, 10U};
    uint8_t discard[5] = {0U};
    uint8_t output[8] = {0U};

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, first,
                                              sizeof(first)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(discard),
                             rbuff_read_buff(&ring, discard,
                                             sizeof(discard)));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, second,
                                              sizeof(second)));

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected),
                             rbuff_peek_buff(&ring, 0U, output,
                                             sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT32(2U,
                             rbuff_peek_buff(&ring, 2U, output, 2U));
    TEST_ASSERT_EQUAL_HEX8(8U, output[0]);
    TEST_ASSERT_EQUAL_HEX8(9U, output[1]);
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_peek_buff(&ring, sizeof(expected),
                                             output, sizeof(output)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), rbuff_available(&ring));
}

void test_ring_buff_clear_discards_pending_data_and_restores_capacity(void)
{
    static const uint8_t input[] = {1U, 2U, 3U};

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, input,
                                              sizeof(input)));
    rbuff_clear(&ring);

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&ring));
    TEST_ASSERT_EQUAL_UINT32(TEST_BUFFER_SIZE - 1U,
                             rbuff_available_for_write(&ring));
}

void test_ring_buff_partial_read_clamps_to_available_data(void)
{
    static const uint8_t input[] = {0xA1U, 0xA2U, 0xA3U};
    uint8_t output[6] = {0U};

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, input,
                                              sizeof(input)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(input),
                             rbuff_read_buff(&ring, output,
                                             sizeof(output)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, sizeof(input));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&ring));
}

void test_ring_buff_zero_copy_blocks_stop_at_physical_boundary(void)
{
    static const uint8_t first[] = {1U, 2U, 3U, 4U, 5U, 6U};
    uint8_t discard[5] = {0U};
    uint8_t *block = NULL;
    uint32_t block_len = 0U;

    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, first,
                                              sizeof(first)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(discard),
                             rbuff_read_buff(&ring, discard,
                                             sizeof(discard)));
    TEST_ASSERT_TRUE(rbuff_get_write_block(&ring, &block, &block_len));
    TEST_ASSERT_EQUAL_UINT32(2U, block_len);
    block[0] = 7U;
    block[1] = 8U;
    TEST_ASSERT_EQUAL_UINT32(2U, rbuff_advance(&ring, 2U));

    TEST_ASSERT_TRUE(rbuff_get_read_block(&ring, &block, &block_len));
    TEST_ASSERT_EQUAL_UINT32(3U, block_len);
    TEST_ASSERT_EQUAL_HEX8(6U, block[0]);
    TEST_ASSERT_EQUAL_HEX8(7U, block[1]);
    TEST_ASSERT_EQUAL_UINT32(2U, rbuff_skip(&ring, 2U));
    TEST_ASSERT_TRUE(rbuff_get_read_block(&ring, &block, &block_len));
    TEST_ASSERT_EQUAL_UINT32(1U, block_len);
    TEST_ASSERT_EQUAL_HEX8(8U, block[0]);
}

void test_ring_buff_rejects_null_outputs_and_zero_lengths(void)
{
    uint8_t *block = (uint8_t *)storage;
    uint32_t block_len = 123U;
    uint8_t value = 0U;

    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_buff(&ring, NULL, 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_buff(&ring, NULL, 0U));
    TEST_ASSERT_FALSE(rbuff_peek(&ring, NULL));
    TEST_ASSERT_FALSE(rbuff_read_safe(&ring, NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_peek_buff(&ring, 0U, NULL, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_peek_buff(&ring, 0U, &value, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_read_buff(&ring, NULL, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_read_buff(&ring, &value, 0U));
    TEST_ASSERT_FALSE(rbuff_get_write_block(&ring, NULL, &block_len));
    TEST_ASSERT_FALSE(rbuff_get_write_block(&ring, &block, NULL));
    TEST_ASSERT_FALSE(rbuff_get_read_block(&ring, NULL, &block_len));
    TEST_ASSERT_FALSE(rbuff_get_read_block(&ring, &block, NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_advance(&ring, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_skip(&ring, 0U));
}

void test_ring_buff_empty_and_full_zero_copy_paths_are_safe(void)
{
    uint8_t *block = (uint8_t *)storage;
    uint32_t block_len = 123U;
    uint8_t output = 0U;

    TEST_ASSERT_FALSE(rbuff_peek(&ring, &output));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_read_buff(&ring, &output, 1U));
    TEST_ASSERT_FALSE(rbuff_get_read_block(&ring, &block, &block_len));
    TEST_ASSERT_NULL(block);
    TEST_ASSERT_EQUAL_UINT32(0U, block_len);

    for (uint32_t index = 0U; index < (TEST_BUFFER_SIZE - 1U); index++)
    {
        TEST_ASSERT_EQUAL_UINT32(0U,
                                 rbuff_write_byte(&ring, (uint8_t)index));
    }
    TEST_ASSERT_FALSE(rbuff_get_write_block(&ring, &block, &block_len));
    TEST_ASSERT_NULL(block);
    TEST_ASSERT_EQUAL_UINT32(0U, block_len);
}

void test_ring_buff_invalid_instances_cover_all_guard_paths(void)
{
    rbuff_t no_storage = {0};
    rbuff_t zero_size = {
        .buff = storage,
        .buff_size = 0U
    };
    uint8_t *block = NULL;
    uint32_t block_len = 0U;
    uint8_t value = 0U;

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available_for_write(NULL));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_byte(NULL, 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuff_write_buff(NULL, &value, 1U));
    TEST_ASSERT_FALSE(rbuff_peek(NULL, &value));
    TEST_ASSERT_FALSE(rbuff_read_safe(NULL, &value));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_peek_buff(NULL, 0U, &value, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_read_buff(NULL, &value, 1U));
    TEST_ASSERT_FALSE(rbuff_get_write_block(NULL, &block, &block_len));
    TEST_ASSERT_FALSE(rbuff_get_read_block(NULL, &block, &block_len));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_advance(NULL, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_skip(NULL, 1U));
    rbuff_clear(NULL);

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&zero_size));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_available_for_write(&no_storage));
    TEST_ASSERT_FALSE(rbuff_get_write_block(&zero_size, &block,
                                             &block_len));
    TEST_ASSERT_FALSE(rbuff_get_read_block(&no_storage, &block,
                                            &block_len));
}

void test_ring_buff_zero_length_block_write_and_linear_peek(void)
{
    static const uint8_t input[] = {1U, 2U, 3U};
    uint8_t output = 0U;

    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_write_buff(&ring, input, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U,
                             rbuff_write_buff(&ring, input,
                                              sizeof(input)));
    TEST_ASSERT_EQUAL_UINT32(1U,
                             rbuff_peek_buff(&ring, 0U, &output, 1U));
    TEST_ASSERT_EQUAL_HEX8(1U, output);
}

/*** end of file ***/
