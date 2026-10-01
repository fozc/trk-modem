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

#define COBS_TEST_MAX_PAYLOAD 253U

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

void test_cobs_round_trip_covers_lengths_and_zero_positions(void)
{
    uint8_t input[COBS_TEST_MAX_PAYLOAD] = {0U};
    uint8_t encoded[COBS_TEST_MAX_PAYLOAD + 1U] = {0U};
    uint8_t decoded[COBS_TEST_MAX_PAYLOAD] = {0U};

    for (size_t input_len = 0U;
         input_len <= COBS_TEST_MAX_PAYLOAD;
         input_len++)
    {
        size_t encoded_len = 0U;
        size_t decoded_len = 0U;

        for (size_t index = 0U; index < input_len; index++)
        {
            input[index] = ((index % 17U) == 0U)
                         ? 0U
                         : (uint8_t)(index + input_len);
        }

        TEST_ASSERT_TRUE(cobs_encode(input, input_len, encoded,
                                     sizeof(encoded), &encoded_len));
        TEST_ASSERT_TRUE(cobs_decode(encoded, encoded_len, decoded,
                                     sizeof(decoded), &decoded_len));
        TEST_ASSERT_EQUAL_size_t(input_len, decoded_len);
        if (input_len > 0U)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, input_len);
        }
    }
}

void test_cobs_handles_leading_consecutive_and_trailing_zeroes(void)
{
    static const uint8_t input[] = {
        0U, 0U, 0x11U, 0U, 0x22U, 0U, 0U
    };
    uint8_t encoded[sizeof(input) + 1U] = {0U};
    uint8_t decoded[sizeof(input)] = {0U};
    size_t encoded_len = 0U;
    size_t decoded_len = 0U;

    TEST_ASSERT_TRUE(cobs_encode(input, sizeof(input), encoded,
                                 sizeof(encoded), &encoded_len));
    TEST_ASSERT_TRUE(cobs_decode(encoded, encoded_len, decoded,
                                 sizeof(decoded), &decoded_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(input), decoded_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, sizeof(input));
}

void test_cobs_uses_exact_capacity_for_long_nonzero_block(void)
{
    uint8_t input[COBS_TEST_MAX_PAYLOAD];
    uint8_t encoded[COBS_TEST_MAX_PAYLOAD + 1U] = {0U};
    size_t encoded_len = 0U;

    memset(input, 0xA5, sizeof(input));

    TEST_ASSERT_FALSE(cobs_encode(input, sizeof(input), encoded,
                                  sizeof(encoded) - 1U, &encoded_len));
    TEST_ASSERT_TRUE(cobs_encode(input, sizeof(input), encoded,
                                 sizeof(encoded), &encoded_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(encoded), encoded_len);
    TEST_ASSERT_EQUAL_HEX8(0xFEU, encoded[0]);
}

void test_cobs_rejects_null_arguments(void)
{
    static const uint8_t input[] = {0x11U};
    uint8_t output[2] = {0U};
    size_t output_len = 0U;

    TEST_ASSERT_FALSE(cobs_encode(input, sizeof(input), NULL,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_encode(input, sizeof(input), output,
                                  sizeof(output), NULL));
    TEST_ASSERT_FALSE(cobs_encode(NULL, sizeof(input), output,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_encode(input, sizeof(input), output,
                                  0U, &output_len));
    TEST_ASSERT_FALSE(cobs_decode(input, sizeof(input), NULL,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode(input, sizeof(input), output,
                                  sizeof(output), NULL));
    TEST_ASSERT_FALSE(cobs_decode(NULL, sizeof(input), output,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode_inplace(NULL, sizeof(input),
                                          &output_len));
    TEST_ASSERT_FALSE(cobs_decode_inplace(output, sizeof(input), NULL));
}

void test_cobs_rejects_truncated_blocks_and_encoded_zeroes(void)
{
    static const uint8_t truncated[] = {0x03U, 0x11U};
    static const uint8_t encoded_zero[] = {0x03U, 0x11U, 0x00U};
    uint8_t output[4] = {0U};
    uint8_t inplace[sizeof(truncated)] = {0x03U, 0x11U};
    size_t output_len = 0U;

    TEST_ASSERT_FALSE(cobs_decode(truncated, sizeof(truncated), output,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode(encoded_zero, sizeof(encoded_zero),
                                  output, sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode_inplace(inplace, sizeof(inplace),
                                          &output_len));
}

void test_cobs_rejects_overflow_at_zero_and_decode_copy_boundaries(void)
{
    static const uint8_t zero[] = {0U};
    static const uint8_t encoded_data[] = {0x03U, 0x11U, 0x22U};
    static const uint8_t encoded_separator[] = {0x01U, 0x01U};
    uint8_t output[1] = {0U};
    size_t output_len = 0U;

    TEST_ASSERT_FALSE(cobs_encode(zero, sizeof(zero), output,
                                  sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode(encoded_data, sizeof(encoded_data),
                                  output, sizeof(output), &output_len));
    TEST_ASSERT_FALSE(cobs_decode(encoded_separator,
                                  sizeof(encoded_separator), output, 0U,
                                  &output_len));
}

void test_cobs_decodes_full_length_code_without_inserting_zero(void)
{
    uint8_t encoded[255U] = {0U};
    uint8_t inplace[sizeof(encoded)] = {0U};
    uint8_t output[254U] = {0U};
    size_t output_len = 0U;

    encoded[0] = 0xFFU;
    memset(&encoded[1], 0xA5, sizeof(encoded) - 1U);
    memcpy(inplace, encoded, sizeof(encoded));

    TEST_ASSERT_TRUE(cobs_decode(encoded, sizeof(encoded), output,
                                 sizeof(output), &output_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(output), output_len);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5U, output, sizeof(output));
    TEST_ASSERT_TRUE(cobs_decode_inplace(inplace, sizeof(inplace),
                                         &output_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(output), output_len);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5U, inplace, sizeof(output));
}

void test_cobs_inplace_rejects_zero_code_and_zero_inside_block(void)
{
    uint8_t zero_code[] = {0U};
    uint8_t zero_in_block[] = {0x03U, 0x11U, 0U};
    size_t output_len = 0U;

    TEST_ASSERT_FALSE(cobs_decode_inplace(zero_code, sizeof(zero_code),
                                          &output_len));
    TEST_ASSERT_FALSE(cobs_decode_inplace(zero_in_block,
                                          sizeof(zero_in_block),
                                          &output_len));
}

/*** end of file ***/
