/*
 * test_cobs.c
 *
 *  Created on: Sep 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies COBS encoding, decoding, and boundary rejection behavior.
 */

#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cobs.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_cobs_encodes_and_decodes_embedded_zero(void)
{
    static const uint8_t input[] = {0x11U, 0x00U, 0x22U};
    static const uint8_t expected[] = {0x02U, 0x11U, 0x02U, 0x22U};
    uint8_t encoded[sizeof(expected)] = {0U};
    uint8_t decoded[sizeof(input)] = {0U};
    size_t encoded_len = 0U;
    size_t decoded_len = 0U;

    TEST_ASSERT_TRUE(cobs_encode(input, sizeof(input), encoded,
                                 sizeof(encoded), &encoded_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), encoded_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
    TEST_ASSERT_TRUE(cobs_decode(encoded, encoded_len, decoded,
                                 sizeof(decoded), &decoded_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(input), decoded_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, sizeof(input));
}

void test_cobs_empty_payload_encodes_as_single_code_byte(void)
{
    uint8_t encoded[1] = {0U};
    size_t encoded_len = 0U;

    TEST_ASSERT_TRUE(cobs_encode(NULL, 0U, encoded, sizeof(encoded),
                                 &encoded_len));
    TEST_ASSERT_EQUAL_size_t(1U, encoded_len);
    TEST_ASSERT_EQUAL_HEX8(0x01U, encoded[0]);
}

void test_cobs_decode_inplace_restores_original_payload(void)
{
    uint8_t data[] = {0x02U, 0x11U, 0x02U, 0x22U};
    static const uint8_t expected[] = {0x11U, 0x00U, 0x22U};
    size_t decoded_len = 0U;

    TEST_ASSERT_TRUE(cobs_decode_inplace(data, sizeof(data), &decoded_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), decoded_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, sizeof(expected));
}

void test_cobs_rejects_invalid_code_and_small_output(void)
{
    static const uint8_t invalid[] = {0x00U};
    static const uint8_t input[] = {0x11U, 0x22U};
    uint8_t output[1] = {0U};
    size_t output_len = 0U;

    TEST_ASSERT_FALSE(cobs_decode(invalid, sizeof(invalid), output,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_encode(input, sizeof(input), output,
                                  sizeof(output), &output_len));
}

/*** end of file ***/
