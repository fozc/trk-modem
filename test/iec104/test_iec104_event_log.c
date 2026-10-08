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
    iec104_event_record_t stored;
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
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stored.payload.fault.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(0U, stored.payload.fault.info.nominal_current_status);
    iec104_event_log_mark_sent(first_seq);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    iec104_event_log_mark_sent(second_seq);
    TEST_ASSERT_TRUE(iec104_event_log_init());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(first_seq, seq);
    TEST_ASSERT_EQUAL_UINT32(65536U, stored.payload.fault.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(1U, stored.payload.fault.info.nominal_current_status);
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

void test_mixed_alarm_and_fault_records_survive_restart_and_replay_newest_first(void)
{
    const fault_log_t fault = {.fault_duration_ms = 70000U};
    const iec104_alarm_record_t alarm = {.feeder = 2U, .phase = 1U,
        .active = 1U, .time = {.iv_bit = 1U}};
    uint16_t fault_seq;
    uint16_t alarm_seq;
    TEST_ASSERT_TRUE(iec104_event_log_add(&fault, &fault_seq));
    TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, &alarm_seq));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    TEST_ASSERT_TRUE(iec104_event_log_init());
    iec104_event_record_t record;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&record, &seq));
    TEST_ASSERT_EQUAL_UINT16(alarm_seq, seq);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, record.kind);
    TEST_ASSERT_EQUAL_MEMORY(&alarm, &record.payload.alarm, sizeof(alarm));
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&record, &seq));
    TEST_ASSERT_EQUAL_UINT16(fault_seq, seq);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_FAULT, record.kind);
    TEST_ASSERT_EQUAL_UINT32(70000U, record.payload.fault.fault_duration_ms);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_alarm_records_cross_nor_pages_and_reject_invalid_input(void)
{
    iec104_alarm_record_t alarm = {.active = 1U};
    for (uint32_t index = 0U; 20U > index; index++)
    {
        TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, NULL));
    }
    TEST_ASSERT_EQUAL_UINT16(20U, iec104_event_log_get_unsent_count());
    alarm.active = 2U;
    uint16_t seq = 42U;
    TEST_ASSERT_FALSE(iec104_event_log_add_alarm(&alarm, &seq));
    TEST_ASSERT_FALSE(iec104_event_log_add_alarm(NULL, &seq));
    TEST_ASSERT_EQUAL_UINT16(42U, seq);
    TEST_ASSERT_EQUAL_UINT16(20U, iec104_event_log_get_unsent_count());
}

/*** end of file ***/
