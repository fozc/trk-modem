/*
 * test_fault_log.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Run the existing NOR fixture and source-time/32-bit regressions.
 */

#include "unity.h"
#include "fault_log.h"

TEST_SOURCE_FILE("fault_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")

#define main fault_log_integration_main
#include "../integration/fault_log/test_fault_log.c"
#undef main
#include "../integration/fault_log/mock_platform.c"

void setUp(void)
{
    passed = 0;
    failed = 0;
}

void tearDown(void)
{
}

void test_existing_fault_log_dual_copy_regressions(void)
{
    TEST_ASSERT_EQUAL_INT(0, fault_log_integration_main());
}

void test_source_time_and_maximum_duration_survive_flash_reload(void)
{
    fault_log_t record = {0};
    fault_log_t saved;

    boot_virgin();
    record.tm.milliseconds = 12345U;
    record.tm.minute = 12U;
    record.tm.hour = 3U;
    record.tm.day = 5U;
    record.tm.month = 10U;
    record.tm.year = 26U;
    record.tm.iv_bit = 1U;
    record.fault_duration_ms = UINT32_MAX;
    record.info.feeder = 2U;
    record.info.phase = 1U;
    record.info.type = FAULT_LOG_TYPE_PERMANENT;
    fault_log_set_current_amps(&record, 12.5F);
    TEST_ASSERT_TRUE(fault_log_append(&record));
    TEST_ASSERT_EQUAL_INT(0, fault_log_sync());
    fault_log_init();
    TEST_ASSERT_TRUE(fault_log_read_nth(2U, 1U,
        FAULT_LOG_TYPE_PERMANENT, 0U, &saved));
    TEST_ASSERT_EQUAL_MEMORY(&record.tm, &saved.tm, sizeof(record.tm));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, saved.fault_duration_ms);
    TEST_ASSERT_EQUAL_FLOAT(12.5F, fault_log_current_amps(&saved));
    TEST_ASSERT_EQUAL_UINT32(20U, sizeof(saved));
    TEST_ASSERT_FALSE(fault_log_append(NULL));
}

/*** end of file ***/
