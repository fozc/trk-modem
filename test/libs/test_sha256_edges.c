/*
 * test_sha256_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "sha256.h"
#include "../fixtures/sha256_block_vectors.h"
#include <string.h>

static uint8_t data[129];
void setUp(void)
{
    for (size_t i = 0U; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)((i * 29U + 17U) & 0xFFU);
    }
}
void tearDown(void)
{
}

void test_sha256_padding_and_block_boundaries_match_independent_vectors(void)
{
    uint8_t digest[32];
    for (size_t i = 0U; i < sizeof(sha256_block_vectors) /
         sizeof(sha256_block_vectors[0]); i++)
    {
        const sha256_test_vector_t *vector = &sha256_block_vectors[i];
        sha256(data, vector->length, digest);
        TEST_ASSERT_EQUAL_MEMORY(vector->digest, digest, sizeof(digest));
    }
}

void test_sha256_all_split_points_match_reference_digest(void)
{
    const sha256_test_vector_t *vector = &sha256_block_vectors[9];
    for (size_t split = 0U; split <= sizeof(data); split++)
    {
        sha256_ctx context;
        uint8_t digest[32];
        sha256_init(&context);
        sha256_update(&context, data, split);
        sha256_update(&context, data + split, sizeof(data) - split);
        sha256_final(&context, digest);
        TEST_ASSERT_EQUAL_MEMORY(vector->digest, digest, sizeof(digest));
    }
}

void test_sha256_bytewise_updates_and_zero_length_updates(void)
{
    sha256_ctx context;
    uint8_t digest[32];
    sha256_init(&context);
    for (size_t i = 0U; i < sizeof(data); i++)
    {
        sha256_update(&context, data, 0U);
        sha256_update(&context, data + i, 1U);
    }
    sha256_final(&context, digest);
    TEST_ASSERT_EQUAL_MEMORY(sha256_block_vectors[9].digest,
        digest, sizeof(digest));
}

void test_sha256_final_clears_context_and_respects_digest_bounds(void)
{
    struct
    {
        uint8_t before[8];
        uint8_t digest[32];
        uint8_t after[8];
    } output;
    sha256_ctx context;
    const uint8_t zero[sizeof(sha256_ctx)] = {0U};
    memset(&output, 0xA5, sizeof(output));
    sha256_init(&context);
    sha256_update(&context, data, sizeof(data));
    sha256_final(&context, output.digest);
    TEST_ASSERT_EQUAL_MEMORY(zero, &context, sizeof(context));
    for (size_t i = 0U; i < sizeof(output.before); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xA5U, output.before[i]);
        TEST_ASSERT_EQUAL_HEX8(0xA5U, output.after[i]);
    }
}

void test_sha256_contexts_can_be_interleaved_without_shared_state(void)
{
    sha256_ctx first;
    sha256_ctx second;
    uint8_t digest[32];
    sha256_init(&first);
    sha256_init(&second);
    sha256_update(&first, data, 1U);
    sha256_update(&second, data, 55U);
    sha256_update(&first, data + 1U, 63U);
    sha256_update(&second, data + 55U, 74U);
    sha256_final(&first, digest);
    TEST_ASSERT_EQUAL_MEMORY(sha256_block_vectors[5].digest,
        digest, sizeof(digest));
    sha256_final(&second, digest);
    TEST_ASSERT_EQUAL_MEMORY(sha256_block_vectors[9].digest,
        digest, sizeof(digest));
}
/*** end of file ***/
