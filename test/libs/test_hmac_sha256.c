/*
 * test_hmac_sha256.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Standard HMAC-SHA256 vectors and incremental SHA verification.
 */
#include "unity.h"
#include "hmac_sha256.h"
#include "sha256.h"

#include <stdint.h>
#include <string.h>

TEST_SOURCE_FILE("sha256.c")

void setUp(void)
{
}

void tearDown(void)
{
}

void test_hmac_rfc4231_case_one(void)
{
    const uint8_t expected[32] =
    {
        0xb0U, 0x34U, 0x4cU, 0x61U, 0xd8U, 0xdbU, 0x38U, 0x53U,
        0x5cU, 0xa8U, 0xafU, 0xceU, 0xafU, 0x0bU, 0xf1U, 0x2bU,
        0x88U, 0x1dU, 0xc2U, 0x00U, 0xc9U, 0x83U, 0x3dU, 0xa7U,
        0x26U, 0xe9U, 0x37U, 0x6cU, 0x2eU, 0x32U, 0xcfU, 0xf7U
    };
    uint8_t key[20];
    uint8_t actual[32];
    const uint8_t data[] = "Hi There";
    (void)memset(key, 0x0b, sizeof(key));
    hmac_sha256(actual, key, sizeof(key), data, sizeof(data) - 1U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

void test_hmac_rfc4231_case_two_incremental(void)
{
    const uint8_t expected[32] =
    {
        0x5bU, 0xdcU, 0xc1U, 0x46U, 0xbfU, 0x60U, 0x75U, 0x4eU,
        0x6aU, 0x04U, 0x24U, 0x26U, 0x08U, 0x95U, 0x75U, 0xc7U,
        0x5aU, 0x00U, 0x3fU, 0x08U, 0x9dU, 0x27U, 0x39U, 0x83U,
        0x9dU, 0xecU, 0x58U, 0xb9U, 0x64U, 0xecU, 0x38U, 0x43U
    };
    const uint8_t key[] = "Jefe";
    const uint8_t data[] = "what do ya want for nothing?";
    uint8_t actual[32];
    hmac_sha256_ctx context;
    hmac_sha256_init(&context, key, sizeof(key) - 1U);
    hmac_sha256_update(&context, data, 7U);
    hmac_sha256_update(&context, data + 7U, sizeof(data) - 8U);
    hmac_sha256_final(&context, actual);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

void test_sha256_incremental_abc(void)
{
    const uint8_t expected[32] =
    {
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
        0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
        0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU
    };
    sha256_ctx context;
    uint8_t actual[32];
    sha256_init(&context);
    sha256_update(&context, (const uint8_t *)"a", 1U);
    sha256_update(&context, (const uint8_t *)"bc", 2U);
    sha256_final(&context, actual);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}
/*** end of file ***/
