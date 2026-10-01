/*
 * test_iec104_util.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies IEC 104 three-byte information object address helpers.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

#include "iec104_util.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_iec104_ioa_round_trip_preserves_all_24_bits(void)
{
    static const uint32_t values[] = {
        0x000000U,
        0x000001U,
        0x00FFFFU,
        0x010000U,
        0xABCDEFU,
        0xFFFFFFU
    };

    for (size_t i = 0U; i < (sizeof(values) / sizeof(values[0])); i++)
    {
        ioa_3byte_t ioa = iec104_make_ioa_3byte(values[i]);

        TEST_ASSERT_EQUAL_HEX32(values[i],
                                iec104_ioa_3byte_to_uint32(ioa));
    }
}

void test_iec104_make_ioa_uses_little_endian_wire_order(void)
{
    ioa_3byte_t ioa = iec104_make_ioa_3byte(0x123456U);

    TEST_ASSERT_EQUAL_HEX8(0x56U, ioa.ioa_low);
    TEST_ASSERT_EQUAL_HEX8(0x34U, ioa.ioa_mid);
    TEST_ASSERT_EQUAL_HEX8(0x12U, ioa.ioa_high);
}

void test_iec104_ioa_equality_checks_every_byte(void)
{
    ioa_3byte_t reference = iec104_make_ioa_3byte(0x123456U);
    ioa_3byte_t different_low = reference;
    ioa_3byte_t different_mid = reference;
    ioa_3byte_t different_high = reference;

    different_low.ioa_low ^= 0x01U;
    different_mid.ioa_mid ^= 0x01U;
    different_high.ioa_high ^= 0x01U;

    TEST_ASSERT_TRUE(iec104_ioa_3byte_equals(reference, reference));
    TEST_ASSERT_FALSE(iec104_ioa_3byte_equals(reference, different_low));
    TEST_ASSERT_FALSE(iec104_ioa_3byte_equals(reference, different_mid));
    TEST_ASSERT_FALSE(iec104_ioa_3byte_equals(reference, different_high));
}

void test_iec104_make_ioa_discards_bits_above_wire_width(void)
{
    ioa_3byte_t ioa = iec104_make_ioa_3byte(0xAB123456U);

    TEST_ASSERT_EQUAL_HEX32(0x00123456U,
                            iec104_ioa_3byte_to_uint32(ioa));
}

void test_iec104_each_wire_byte_maps_to_expected_bit_range(void)
{
    ioa_3byte_t low = {.ioa_low = 0xFFU, .ioa_mid = 0U, .ioa_high = 0U};
    ioa_3byte_t mid = {.ioa_low = 0U, .ioa_mid = 0xFFU, .ioa_high = 0U};
    ioa_3byte_t high = {.ioa_low = 0U, .ioa_mid = 0U, .ioa_high = 0xFFU};

    TEST_ASSERT_EQUAL_HEX32(0x0000FFU,
                            iec104_ioa_3byte_to_uint32(low));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00U,
                            iec104_ioa_3byte_to_uint32(mid));
    TEST_ASSERT_EQUAL_HEX32(0xFF0000U,
                            iec104_ioa_3byte_to_uint32(high));
}

/*** end of file ***/
