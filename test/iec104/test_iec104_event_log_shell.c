/*
 * test_iec104_event_log_shell.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify stored event bits through the public shell dump entry point.
 */
#include "unity.h"
#include "iec104_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "mock_w25qxx.h"
#include "mock_nvram.h"
#include "mock_shell.h"
#include "mock_bsp.h"
#include "mock_breaker.h"
#include "mock_utils.h"
#include <string.h>

TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")

#include "../support/iec104_event_log_fixture.h"

void setUp(void)
{
    reset_fixture();
}

void tearDown(void)
{
}

void test_public_dump_preserves_stored_load_present_and_absent_bits(void)
{
    fault_log_t record = {0};
    record.info.nominal_current_status = 1U;
    record.info.power_status = 0U;
    record.info.type = FAULT_LOG_TYPE_PERMANENT;
    fault_log_set_current_amps(&record, 12.5F);
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    record.info.nominal_current_status = 0U;
    record.info.power_status = 1U;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    iec104_event_log_dump();
    TEST_ASSERT_NOT_NULL(strstr(output, "P=0 Load=1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "P=1 Load=0"));
    TEST_ASSERT_NULL(strstr(output, " N="));
}

/*** end of file ***/
