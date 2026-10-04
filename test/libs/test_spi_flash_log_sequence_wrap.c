/*
 * test_spi_flash_log_sequence_wrap.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Reuse the integration NOR fixture for production sequence-wrap checks.
 */
#include "unity.h"
#include "spi_flash_log.h"

TEST_SOURCE_FILE("spi_flash_log.c")

#define main spi_flash_log_integration_main
#include "../integration/libs/test_spi_flash_log.c"
#undef main

void setUp(void)
{
    passed = 0;
    failed = 0;
}

void tearDown(void)
{
}

void test_last_records_remain_readable_when_sequence_wraps_to_zero(void)
{
    test_sequence_wrap_read_last();
    TEST_ASSERT_EQUAL_INT(0, failed);
}

void test_boot_scan_restores_sequence_and_record_order_across_wrap(void)
{
    test_sequence_wrap_boot_scan();
    TEST_ASSERT_EQUAL_INT(0, failed);
}

/*** end of file ***/
