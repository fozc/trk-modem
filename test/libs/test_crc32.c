/*
 * test_crc32.c
 *
 *  Created on: Sep 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies the CRC-32 public contract with host-side test vectors.
 */

#include "unity.h"

#include <stddef.h>
#include <stdint.h>

#include "crc32.h"

#define CRC_TEST_DATA_SIZE 256U

void setUp(void)
{
}

void tearDown(void)
{
}

void test_crc32_matches_standard_check_value(void)
{
    static const uint8_t input[] = "123456789";
    crc32_t crc = crc32_init();

    crc = crc32_update(crc, input, sizeof(input) - 1U);

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U,
                            (uint32_t)crc32_finalize(crc));
}

void test_crc32_incremental_update_matches_single_update(void)
{
    static const uint8_t first[] = "1234";
    static const uint8_t second[] = "56789";
    static const uint8_t complete[] = "123456789";
    crc32_t incremental = crc32_init();
    crc32_t single = crc32_init();

    incremental = crc32_update(incremental, first, sizeof(first) - 1U);
    incremental = crc32_update(incremental, second, sizeof(second) - 1U);
    single = crc32_update(single, complete, sizeof(complete) - 1U);

    TEST_ASSERT_EQUAL_HEX32((uint32_t)crc32_finalize(single),
                            (uint32_t)crc32_finalize(incremental));
}

void test_crc32_empty_input_keeps_initial_state(void)
{
    crc32_t crc = crc32_init();

    crc = crc32_update(crc, NULL, 0U);

    TEST_ASSERT_EQUAL_HEX32(0U, (uint32_t)crc32_finalize(crc));
}

void test_crc32_binary_vector_matches_known_value(void)
{
    static const uint8_t input[] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x7FU, 0x80U, 0xFEU, 0xFFU
    };
    crc32_t crc = crc32_init();

    crc = crc32_update(crc, input, sizeof(input));

    TEST_ASSERT_EQUAL_HEX32(0xBC6987F2U,
                            (uint32_t)crc32_finalize(crc));
}

void test_crc32_bytewise_update_matches_full_buffer(void)
{
    uint8_t input[CRC_TEST_DATA_SIZE];
    crc32_t bytewise = crc32_init();
    crc32_t complete = crc32_init();

    for (size_t index = 0U; index < sizeof(input); index++)
    {
        input[index] = (uint8_t)index;
        bytewise = crc32_update(bytewise, &input[index], 1U);
    }
    complete = crc32_update(complete, input, sizeof(input));

    TEST_ASSERT_EQUAL_HEX32((uint32_t)crc32_finalize(complete),
                            (uint32_t)crc32_finalize(bytewise));
}

void test_crc32_reflect_handles_boundary_widths(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000001U,
                            (uint32_t)crc32_reflect(1U, 1U));
    TEST_ASSERT_EQUAL_HEX32(0x00000080U,
                            (uint32_t)crc32_reflect(1U, 8U));
    TEST_ASSERT_EQUAL_HEX32(0xE6A2C480U,
                            (uint32_t)crc32_reflect(0x01234567U, 32U));
}

/*** end of file ***/
