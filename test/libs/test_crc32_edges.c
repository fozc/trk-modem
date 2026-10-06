/*
 * test_crc32_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * Reference CRCs verified independently with Python zlib.crc32.
 */
#include "unity.h"
#include "crc32.h"
#include <string.h>

void setUp(void)
{
}
void tearDown(void)
{
}
void test_crc32_every_split_of_all_byte_values_matches_reference(void)
{
    uint8_t data[256];
    for (size_t i = 0U; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)i;
    }
    for (size_t split = 0U; split <= sizeof(data); split++)
    {
        crc32_t crc = crc32_update(crc32_init(), data, split);
        crc = crc32_update(crc, NULL, 0U);
        crc = crc32_update(crc, data + split, sizeof(data) - split);
        TEST_ASSERT_EQUAL_HEX32(0x29058C73U,
            (uint32_t)crc32_finalize(crc));
    }
}
void test_crc32_zero_and_ff_blocks_match_independent_reference(void)
{
    uint8_t data[1024];
    memset(data, 0, sizeof(data));
    TEST_ASSERT_EQUAL_HEX32(0xEFB5AF2EU, (uint32_t)crc32_finalize(
        crc32_update(crc32_init(), data, sizeof(data))));
    memset(data, 0xFF, sizeof(data));
    TEST_ASSERT_EQUAL_HEX32(0xB83AFFF4U, (uint32_t)crc32_finalize(
        crc32_update(crc32_init(), data, sizeof(data))));
}
void test_crc32_unaligned_input_keeps_guards_and_matches_reference(void)
{
    uint8_t data[258];
    data[0] = 0xA5U;
    data[257] = 0x5AU;
    for (size_t i = 0U; i < 256U; i++)
    {
        data[i + 1U] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_HEX32(0x29058C73U, (uint32_t)crc32_finalize(
        crc32_update(crc32_init(), data + 1U, 256U)));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5AU, data[257]);
    for (size_t i = 0U; i < 256U; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(i, data[i + 1U]);
    }
}
void test_crc32_interleaved_independent_accumulators_keep_separate_state(void)
{
    static const char data[] = "123456789";
    uint8_t zero[1024] = {0};
    crc32_t first = crc32_update(crc32_init(), data, 4U);
    crc32_t second = crc32_update(crc32_init(), zero, 512U);
    first = crc32_update(first, data + 4U, 5U);
    second = crc32_update(second, zero + 512U, 512U);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, (uint32_t)crc32_finalize(first));
    TEST_ASSERT_EQUAL_HEX32(0xEFB5AF2EU, (uint32_t)crc32_finalize(second));
}
void test_crc32_zero_length_keeps_partial_and_nondefault_seed(void)
{
    const crc32_t seed = 0x12345678U;
    TEST_ASSERT_EQUAL_HEX32((uint32_t)seed,
        (uint32_t)crc32_update(seed, NULL, 0U));
    static const char partial[] = "1234";
    crc32_t crc = crc32_update(crc32_init(), partial, sizeof(partial) - 1U);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)crc,
        (uint32_t)crc32_update(crc, partial, 0U));
}
/*** end of file ***/
