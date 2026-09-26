/*
 * test_efw_crc.c
 *
 *  Created on: Sep 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies the embedded framework CRC implementation on the host.
 */

#include "unity.h"

#include <stddef.h>
#include <stdint.h>

#include "efw_crc.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_efw_crc_matches_firmware_update_wire_value(void)
{
    static const uint8_t input[] = "123456789";
    efw_crc_t crc = efw_crc_init();

    crc = efw_crc_update(crc, input, sizeof(input) - 1U);

    TEST_ASSERT_EQUAL_HEX32(0x340BC6D9U, efw_crc_finalize(crc));
}

void test_efw_crc_incremental_update_is_stable(void)
{
    static const uint8_t first[] = "1234";
    static const uint8_t second[] = "56789";
    static const uint8_t complete[] = "123456789";
    efw_crc_t incremental = efw_crc_init();
    efw_crc_t single = efw_crc_init();

    incremental = efw_crc_update(incremental, first, sizeof(first) - 1U);
    incremental = efw_crc_update(incremental, second, sizeof(second) - 1U);
    single = efw_crc_update(single, complete, sizeof(complete) - 1U);

    TEST_ASSERT_EQUAL_HEX32(efw_crc_finalize(single),
                            efw_crc_finalize(incremental));
}

/*** end of file ***/
