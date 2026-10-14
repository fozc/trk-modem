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
#include <string.h>

TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("utils.c")

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

static shell_cmd_t test_command;

static int capture_command(const shell_cmd_t *command, int call_count)
{
    (void)call_count;
    test_command = *command;
    return 0;
}

static void register_test_command(void)
{
    shell_register_command_StubWithCallback(capture_command);
    iec104_event_log_shell_init();
    TEST_ASSERT_NOT_NULL(test_command.func);
}

void test_test_command_rejects_invalid_counts_without_flash_writes(void)
{
    const char *invalid[] = {"-1", "99999999999", "65536", "abc", ""};
    register_test_command();
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         index++)
    {
        char *args[] = {"iec104evtlog", "test", (char *)invalid[index]};
        TEST_ASSERT_EQUAL_INT(0, test_command.func(3, args));
        TEST_ASSERT_EQUAL_UINT32(0U, program_calls);
        TEST_ASSERT_EQUAL_UINT32(0U, sync_calls);
    }
    TEST_ASSERT_NOT_NULL(strstr(output, "Kullanim:"));
}

void test_test_command_zero_keeps_existing_records(void)
{
    fault_log_t record = {0};
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    const uint32_t before = program_calls;
    register_test_command();
    char *args[] = {"iec104evtlog", "test", "0"};
    TEST_ASSERT_EQUAL_INT(0, test_command.func(3, args));
    TEST_ASSERT_EQUAL_UINT32(before, program_calls);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
}

void test_test_command_valid_count_adds_requested_records(void)
{
    register_test_command();
    breaker_get_power_line_by_idx_IgnoreAndReturn(NULL);
    const bsp_rtc_t datetime = {.day = 1U, .month = 1U};
    bsp_get_datetime_IgnoreAndReturn(datetime);
    char *args[] = {"iec104evtlog", "test", "2"};
    TEST_ASSERT_EQUAL_INT(0, test_command.func(3, args));
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
}

/*** end of file ***/
