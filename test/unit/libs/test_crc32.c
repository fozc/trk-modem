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

/*** end of file ***/
