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
#include "crc32.h"
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
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    reboot_fixture();
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
    reboot_fixture();
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

static void write_flash_record(size_t slot, uint16_t seq,
                                const iec104_event_record_t *record)
{
    const size_t entry_size = LOG_ENTRY_OVERHEAD + sizeof(*record);
    const size_t offset = slot * entry_size;
    TEST_ASSERT_TRUE(LOG_SECTOR_SIZE >= offset + entry_size);
    (void)memcpy(&flash[offset], &seq, sizeof(seq));
    (void)memcpy(&flash[offset + LOG_SEQ_SIZE], record, sizeof(*record));
    const uint16_t crc = log_calculate_crc16(&flash[offset],
        LOG_SEQ_SIZE + sizeof(*record));
    (void)memcpy(&flash[offset + LOG_SEQ_SIZE + sizeof(*record)],
                 &crc, sizeof(crc));
}

static iec104_event_record_t fault_record(uint32_t duration)
{
    iec104_event_record_t record = {.kind = IEC104_EVENT_FAULT,
        .payload.fault = {.fault_duration_ms = duration}};
    record.crc = crc32_finalize(crc32_update(crc32_init(), &record,
        sizeof(record) - sizeof(record.crc)));
    return record;
}

void test_torn_payload_and_crc_writes_keep_old_record_after_reboot(void)
{
    for (uint32_t step = 1U; 2U >= step; step++)
    {
        reset_fixture();
        fault_log_t record = {.fault_duration_ms = 100U};
        uint16_t first;
        TEST_ASSERT_TRUE(iec104_event_log_add(&record, &first));
        TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
        const iec104_evtlog_state_t previous = replay_state;
        failed_program_call = program_calls + step;
        torn_program_bytes = (1U == step) ? 7U : 0U;
        record.fault_duration_ms = 200U;
        uint16_t rejected_seq = 123U;
        TEST_ASSERT_FALSE(iec104_event_log_add(&record, &rejected_seq));
        TEST_ASSERT_EQUAL_UINT16(123U, rejected_seq);
        TEST_ASSERT_EQUAL_MEMORY(&previous, &replay_state, sizeof(previous));
        failed_program_call = 0U;
        reboot_fixture();
        iec104_event_record_t stored;
        uint16_t seq;
        TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
        TEST_ASSERT_EQUAL_UINT16(first, seq);
        TEST_ASSERT_EQUAL_UINT32(100U, stored.payload.fault.fault_duration_ms);
        iec104_event_log_mark_sent(seq);
        record.fault_duration_ms = 300U;
        TEST_ASSERT_TRUE(iec104_event_log_add(&record, &seq));
        TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
        reboot_fixture();
        TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
        TEST_ASSERT_EQUAL_UINT32(300U, stored.payload.fault.fault_duration_ms);
    }
}

void test_silent_crc_loss_cannot_acknowledge_an_identical_older_record(void)
{
    const fault_log_t record = {.fault_duration_ms = 1234U};
    uint16_t first;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &first));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    const iec104_evtlog_state_t previous = replay_state;
    silent_program_call = program_calls + 2U;
    uint16_t rejected_seq = 42U;
    TEST_ASSERT_FALSE(iec104_event_log_add(&record, &rejected_seq));
    TEST_ASSERT_EQUAL_UINT16(42U, rejected_seq);
    TEST_ASSERT_EQUAL_MEMORY(&previous, &replay_state, sizeof(previous));
    silent_program_call = 0U;
    reboot_fixture();
    iec104_event_record_t stored;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(first, seq);
}

void test_inner_crc_and_unknown_kind_are_skipped_even_with_valid_outer_crc(void)
{
    for (uint8_t fault = 0U; 2U > fault; fault++)
    {
        reset_fixture();
        const iec104_event_record_t valid = fault_record(100U);
        iec104_event_record_t invalid = fault_record(200U);
        if (0U == fault)
        {
            invalid.crc ^= 1U;
        }
        else
        {
            invalid.kind = 255U;
            invalid.crc = crc32_finalize(crc32_update(crc32_init(), &invalid,
                sizeof(invalid) - sizeof(invalid.crc)));
        }
        write_flash_record(0U, 0U, &valid);
        write_flash_record(1U, 1U, &invalid);
        persisted_replay_state = (iec104_evtlog_state_t)
            {.has_unsent = 1U, .unsent_low = 0U, .unsent_high = 1U};
        reboot_fixture();
        iec104_event_record_t stored;
        uint16_t seq;
        TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
        TEST_ASSERT_EQUAL_UINT16(0U, seq);
        TEST_ASSERT_EQUAL_UINT32(100U, stored.payload.fault.fault_duration_ms);
        iec104_event_log_mark_sent(seq);
        TEST_ASSERT_FALSE(iec104_event_log_read_newest_unsent(&stored, &seq));
        TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    }
}

void test_new_append_invalidates_replay_cursor_without_losing_older_fault(void)
{
    fault_log_t record = {.fault_duration_ms = 100U};
    uint16_t first;
    uint16_t newest;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &first));
    record.fault_duration_ms = 200U;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &newest));
    iec104_event_record_t stored;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    iec104_event_log_mark_sent(seq);
    const iec104_alarm_record_t alarm = {.active = 1U};
    TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, &newest));
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(newest, seq);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, stored.kind);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    /* The single pending interval may replay a sent record after an append. */
    if (first != seq)
    {
        TEST_ASSERT_EQUAL_UINT32(200U, stored.payload.fault.fault_duration_ms);
        iec104_event_log_mark_sent(seq);
        TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    }
    TEST_ASSERT_EQUAL_UINT16(first, seq);
    TEST_ASSERT_EQUAL_UINT32(100U, stored.payload.fault.fault_duration_ms);
}

void test_failed_nvram_sync_preserves_previous_durable_replay_boundary(void)
{
    const fault_log_t record = {.fault_duration_ms = 100U};
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, &seq));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    iec104_event_log_mark_sent(seq);
    sync_fails = true;
    TEST_ASSERT_EQUAL_INT(-1, iec104_event_log_sync());
    sync_fails = false;
    reboot_fixture();
    iec104_event_record_t stored;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT32(100U, stored.payload.fault.fault_duration_ms);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_sequence_wrap_replays_newest_first_after_restart(void)
{
    const iec104_event_record_t first = fault_record(100U);
    const iec104_event_record_t second = fault_record(200U);
    write_flash_record(0U, 65534U, &first);
    write_flash_record(1U, 0U, &second);
    persisted_replay_state = (iec104_evtlog_state_t)
        {.has_unsent = 1U, .unsent_low = 65534U, .unsent_high = 0U};
    reboot_fixture();
    iec104_event_record_t stored;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(0U, seq);
    TEST_ASSERT_EQUAL_UINT32(200U, stored.payload.fault.fault_duration_ms);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(65534U, seq);
    TEST_ASSERT_EQUAL_UINT32(100U, stored.payload.fault.fault_duration_ms);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_read_without_mark_sent_retries_the_same_newest_pending_record(void)
{
    fault_log_t fault = {.fault_duration_ms = 100U};
    TEST_ASSERT_TRUE(iec104_event_log_add(&fault, NULL));
    fault.fault_duration_ms = 200U;
    uint16_t newest;
    TEST_ASSERT_TRUE(iec104_event_log_add(&fault, &newest));
    iec104_event_record_t stored;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(newest, seq);
    /* A rejected transport send does not call mark_sent. */
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT16(newest, seq);
    TEST_ASSERT_EQUAL_UINT32(200U, stored.payload.fault.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
}

void test_full_ring_erase_failure_preserves_history_after_restart(void)
{
    const uint32_t per_sector = LOG_SECTOR_SIZE /
        (LOG_ENTRY_OVERHEAD + sizeof(iec104_event_record_t));
    const uint32_t capacity = IEC104_EVTLOG_SECTOR_COUNT * per_sector;
    fault_log_t record = {0};
    for (uint32_t index = 0U; capacity > index; index++)
    {
        record.fault_duration_ms = index;
        TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    }
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    const iec104_evtlog_state_t previous = replay_state;
    erase_fails = true;
    uint16_t rejected_seq = 123U;
    record.fault_duration_ms = capacity;
    TEST_ASSERT_FALSE(iec104_event_log_add(&record, &rejected_seq));
    TEST_ASSERT_EQUAL_UINT16(123U, rejected_seq);
    TEST_ASSERT_EQUAL_MEMORY(&previous, &replay_state, sizeof(previous));
    erase_fails = false;
    reboot_fixture();
    iec104_event_record_t stored;
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&stored, &seq));
    TEST_ASSERT_EQUAL_UINT32(capacity - 1U,
        stored.payload.fault.fault_duration_ms);
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    reboot_fixture();
    uint32_t read_count = 0U;
    while (iec104_event_log_read_newest_unsent(&stored, &seq))
    {
        TEST_ASSERT_TRUE(capacity > read_count);
        TEST_ASSERT_EQUAL_UINT32(capacity - read_count,
            stored.payload.fault.fault_duration_ms);
        iec104_event_log_mark_sent(seq);
        read_count++;
    }
    TEST_ASSERT_EQUAL_UINT32(capacity - per_sector + 1U, read_count);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_unsynced_mark_sent_reappears_after_boot_until_sync_completes(void)
{
    const iec104_alarm_record_t alarm = {.active = 1U};
    uint16_t seq;
    TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, &seq));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    reboot_fixture();
    iec104_event_record_t record;
    TEST_ASSERT_TRUE(iec104_event_log_read_newest_unsent(&record, &seq));
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, record.kind);
    iec104_event_log_mark_sent(seq);
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    reboot_fixture();
    TEST_ASSERT_FALSE(iec104_event_log_read_newest_unsent(&record, &seq));
}

/*** end of file ***/
