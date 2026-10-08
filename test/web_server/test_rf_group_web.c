/*
 * test_rf_group_web.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify web apply parsing and bounded status JSON at the service boundary.
 */

#include "unity.h"
#include "rf_group_web.h"
#include "mock_rf_group.h"
#include "mock_rf_apply.h"
#include "mock_rf_inventory.h"
#include <string.h>

TEST_SOURCE_FILE("xprintf.c")

static void batch_status(rf_apply_status_t *out, int call_count)
{
    (void)call_count;
    *out = (rf_apply_status_t){.state = RF_APPLY_RUNNING,
        .targets = 0x44U, .applied = 0x04U, .line = 7U,
        .group_started = true};
}

static void inventory_wait(rf_inventory_wait_t *out, int call_count)
{
    (void)call_count;
    *out = (rf_inventory_wait_t){.reason = "storage",
        .total_ms = 125000U, .reason_ms = 7000U};
}

void setUp(void)
{
    rf_inventory_get_wait_StubWithCallback(inventory_wait);
    rf_apply_get_status_StubWithCallback(batch_status);
    rf_apply_can_save_IgnoreAndReturn(false);
}

void tearDown(void)
{
}

static bool get_status(rf_group_status_t *out, int call_count)
{
    (void)call_count;
    *out = (rf_group_status_t){.state = RF_GROUP_APPLIED, .line = 2U,
        .feeder = 4U, .group_id = 255U, .expected_crc = 0x096DU,
        .writes_acked = 3U, .has_report = true,
        .report = {.state = 3U, .member_bitmap = 7U, .config_crc = 0x096DU}};
    return true;
}

void test_applied_snapshot_is_distinct_from_changed_desired_settings(void)
{
    char buffer[768];
    size_t length = 0U;

    rf_group_get_status_StubWithCallback(get_status);
    rf_group_matches_config_ExpectAndReturn(false);
    TEST_ASSERT_TRUE(rf_group_status_json_build(buffer, sizeof(buffer),
                                                &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"State\":\"applied\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"Line\":2,\"Feeder\":4"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"MatchesDesired\":false"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"HasReport\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"BatchState\":\"running\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"Targets\":68,\"Applied\":4"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"SaveBlocked\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"WaitReason\":\"storage\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"WaitTotalMs\":125000"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"WaitReasonMs\":7000"));
    TEST_ASSERT_EQUAL_size_t(strlen(buffer), length);
}

void test_small_status_buffer_is_cleared_and_guards_are_preserved(void)
{
    uint8_t buffer[18];
    size_t length = 123U;

    (void)memset(buffer, 0xA5, sizeof(buffer));
    rf_group_get_status_StubWithCallback(get_status);
    rf_group_matches_config_ExpectAndReturn(true);
    TEST_ASSERT_FALSE(rf_group_status_json_build((char *)&buffer[1], 16U,
                                                 &length));
    TEST_ASSERT_EQUAL_UINT8(0U, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, buffer[17]);
    TEST_ASSERT_EQUAL_size_t(0U, length);
    TEST_ASSERT_FALSE(rf_group_status_json_build(NULL, 16U, &length));
    TEST_ASSERT_FALSE(rf_group_status_json_build((char *)buffer, 0U, &length));
    TEST_ASSERT_FALSE(rf_group_status_json_build((char *)buffer, 16U, NULL));
}

/*** end of file ***/
