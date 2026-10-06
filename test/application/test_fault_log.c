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
#include "crc32.h"

TEST_SOURCE_FILE("fault_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")

#define main fault_log_integration_main
#include "../integration/fault_log/test_fault_log.c"
#undef main
#define xcprintf fault_log_test_ignored_color_printf
#include "../integration/fault_log/mock_platform.c"
#undef xcprintf

static char shell_output[8192U];
static size_t shell_output_len;

void shell_putchr(int ch)
{
    TEST_ASSERT_TRUE(sizeof(shell_output) > shell_output_len + 1U);
    shell_output[shell_output_len] = (char)ch;
    shell_output_len++;
    shell_output[shell_output_len] = '\0';
}

void setUp(void)
{
    passed = 0;
    failed = 0;
    shell_output_len = 0U;
    shell_output[0] = '\0';
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

void test_old_inverted_load_schema_is_not_interpreted_as_load_present(void)
{
    boot_virgin();
    TEST_ASSERT_TRUE(fault_log_add(12.0F, 100U, 1U, 1U, 0U, 0U, 0U));
    TEST_ASSERT_EQUAL_INT(0, fault_log_sync());
    TEST_ASSERT_EQUAL_UINT32(3U, img_u32(false, 0U, 4U));
    for (uint8_t copy = 0U; 2U > copy; copy++)
    {
        const bool backup = (0U != copy);
        uint8_t *image = mock_slot_image_rw(backup, 0U);
        const uint32_t length = img_u32(backup, 0U, 8U);
        const uint32_t old_version = 2U;
        (void)memcpy(image + 4U, &old_version, sizeof(old_version));
        const uint32_t crc = (uint32_t)crc32_finalize(crc32_update(crc32_init(),
            image, length - 4U));
        (void)memcpy(image + length - 4U, &crc, sizeof(crc));
    }
    fault_log_init();
    TEST_ASSERT_EQUAL_UINT8(0U, fault_log_get_temp_count(0U, 0U));
}

void test_dump_displays_load_present_bit_without_old_nominal_meaning(void)
{
    boot_virgin();
    TEST_ASSERT_TRUE(fault_log_add(12.0F, 100U, 1U, 0U, 0U, 0U, 0U));
    TEST_ASSERT_TRUE(fault_log_add(13.0F, 200U, 0U, 1U, 1U, 0U, 0U));
    TEST_ASSERT_EQUAL_INT(0, fault_log_sync());
    fault_log_dump();
    TEST_ASSERT_NOT_NULL(strstr(shell_output, "Load=1  Power=Off"));
    TEST_ASSERT_NOT_NULL(strstr(shell_output, "Load=0  Power=On"));
    TEST_ASSERT_NULL(strstr(shell_output, "Nominal="));
    TEST_ASSERT_NULL(strstr(shell_output, "Below"));
}

/*** end of file ***/
