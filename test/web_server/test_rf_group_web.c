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
#include <string.h>

TEST_SOURCE_FILE("xprintf.c")

void setUp(void)
{
}

void tearDown(void)
{
}

void test_apply_uses_one_based_store_line_and_accepts_full_group_id_range(void)
{
    rf_group_start_ExpectAndReturn(0U, 0U, true);
    TEST_ASSERT_EQUAL_INT(RF_WEB_APPLY_STARTED, rf_web_start_apply("1/0"));
    rf_group_start_ExpectAndReturn(6U, 255U, true);
    TEST_ASSERT_EQUAL_INT(RF_WEB_APPLY_STARTED, rf_web_start_apply("7/255"));
    rf_group_start_ExpectAndReturn(1U, 5U, false);
    TEST_ASSERT_EQUAL_INT(RF_WEB_APPLY_NOT_STARTED, rf_web_start_apply("2/5"));
}

void test_invalid_apply_paths_never_call_group_service(void)
{
    const char *invalid[] = {"", "0/1", "8/1", "1/256", "1/-1",
        "-1/2", "1/1x", "1/1/2", "1/", "1", "/2", "1.5/2"};

    TEST_ASSERT_EQUAL_INT(RF_WEB_APPLY_INVALID, rf_web_start_apply(NULL));
    for (size_t index = 0U;
         index < sizeof(invalid) / sizeof(invalid[0]); index++)
    {
        TEST_ASSERT_EQUAL_INT(RF_WEB_APPLY_INVALID,
                              rf_web_start_apply(invalid[index]));
    }
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
    char buffer[512];
    size_t length = 0U;

    rf_group_get_status_StubWithCallback(get_status);
    rf_group_matches_config_ExpectAndReturn(false);
    TEST_ASSERT_TRUE(rf_group_status_json_build(buffer, sizeof(buffer),
                                                &length));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"State\":\"applied\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"Line\":2,\"Feeder\":4"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"MatchesDesired\":false"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "\"HasReport\":true"));
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
