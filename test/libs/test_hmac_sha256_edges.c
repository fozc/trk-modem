/*
 * test_hmac_sha256_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "hmac_sha256.h"
#include "../fixtures/hmac_block_vectors.h"
#include <string.h>
TEST_SOURCE_FILE("sha256.c")
static uint8_t key[131];
static uint8_t data[129];
void setUp(void)
{
    for (size_t i = 0U; i < sizeof(key); i++)
    {
        key[i] = (uint8_t)((i * 11U + 5U) & 0xFFU);
    }
    for (size_t i = 0U; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)((i * 29U + 17U) & 0xFFU);
    }
}
void tearDown(void)
{
}
void test_hmac_key_and_message_boundaries_match_independent_reference(void)
{
    uint8_t digest[32];
    for (size_t i = 0U; i < sizeof(hmac_block_vectors) /
         sizeof(hmac_block_vectors[0]); i++)
    {
        const hmac_test_vector_t *vector = &hmac_block_vectors[i];
        hmac_sha256(digest, key, vector->key_len, data, vector->data_len);
        TEST_ASSERT_EQUAL_MEMORY(vector->digest, digest, sizeof(digest));
    }
}
void test_hmac_long_key_all_split_points_match_reference(void)
{
    const hmac_test_vector_t *vector = &hmac_block_vectors[55];
    for (size_t split = 0U; split <= sizeof(data); split++)
    {
        hmac_sha256_ctx context;
        uint8_t digest[32];
        hmac_sha256_init(&context, key, sizeof(key));
        hmac_sha256_update(&context, data, split);
        hmac_sha256_update(&context, data + split, sizeof(data) - split);
        hmac_sha256_final(&context, digest);
        TEST_ASSERT_EQUAL_MEMORY(vector->digest, digest, sizeof(digest));
    }
}
void test_hmac_zero_length_update_preserves_partial_message(void)
{
    hmac_sha256_ctx context;
    uint8_t digest[32];
    hmac_sha256_init(&context, key, sizeof(key));
    hmac_sha256_update(&context, data, 55U);
    hmac_sha256_update(&context, data, 0U);
    hmac_sha256_update(&context, data + 55U, 74U);
    hmac_sha256_final(&context, digest);
    TEST_ASSERT_EQUAL_MEMORY(hmac_block_vectors[55].digest,
        digest, sizeof(digest));
}
void test_hmac_output_guards_and_input_buffers_are_preserved(void)
{
    uint8_t output[48];
    uint8_t original_key[sizeof(key)];
    uint8_t original_data[sizeof(data)];
    memcpy(original_key, key, sizeof(key));
    memcpy(original_data, data, sizeof(data));
    memset(output, 0xA5, sizeof(output));
    hmac_sha256(output + 8U, key, sizeof(key), data, sizeof(data));
    TEST_ASSERT_EQUAL_MEMORY(original_key, key, sizeof(key));
    TEST_ASSERT_EQUAL_MEMORY(original_data, data, sizeof(data));
    for (size_t i = 0U; i < 8U; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xA5U, output[i]);
        TEST_ASSERT_EQUAL_HEX8(0xA5U, output[i + 40U]);
    }
}
/*** end of file ***/
