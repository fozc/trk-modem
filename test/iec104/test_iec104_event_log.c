/*
 * test_iec104_event_log.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify unsent state and stored payloads with shell logging disabled.
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

void test_newest_unsent_records_keep_payload_and_survive_reinitialization(void)
{
    fault_log_t record = {0};
    fault_log_t stored;
    uint16_t first_seq;
    uint16_t second_seq;
    uint16_t seq;
    record.info.nominal_current_status = 1U;
    record.fault_duration_ms = 65536U;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &first_seq));
    record.info.nominal_current_status = 0U;
    record.fault_duration_ms = UINT32_MAX;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &second_seq));
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(second_seq, seq);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stored.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(0U, stored.info.nominal_current_status);
    iec104_event_log_mark_sent(first_seq);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    iec104_event_log_mark_sent(second_seq);
    TEST_ASSERT_TRUE(iec104_event_log_init());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(first_seq, seq);
    TEST_ASSERT_EQUAL_UINT32(65536U, stored.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(1U, stored.info.nominal_current_status);
    iec104_event_log_mark_sent(first_seq);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    TEST_ASSERT_FALSE(iec104_event_log_read_newest_unsent(&stored, &seq));
    iec104_event_log_dump();
    TEST_ASSERT_EQUAL_size_t(0U, output_len);
}

void test_invalid_input_and_failed_write_preserve_unsent_state_and_sequence(void)
{
    fault_log_t record = {0};
    uint16_t seq = 123U;
    TEST_ASSERT_FALSE(iec104_event_log_add(NULL, &seq));
    TEST_ASSERT_FALSE(iec104_event_log_read_newest_unsent(NULL, &seq));
    TEST_ASSERT_EQUAL_UINT16(123U, seq);
    program_fails = true;
    TEST_ASSERT_FALSE(iec104_event_log_add(&record, &seq));
    TEST_ASSERT_EQUAL_UINT16(123U, seq);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    TEST_ASSERT_EQUAL_UINT8(0U, replay_state.has_unsent);
}

/*** end of file ***/
