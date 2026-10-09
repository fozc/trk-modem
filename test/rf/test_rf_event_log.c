/*
 * test_rf_event_log.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify complete RF event storage with a NOR flash hardware model.
 */

#include "unity.h"
#include "rf_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "mock_w25qxx.h"
#include "mock_bsp.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("spi_flash_log.c")

static uint8_t flash[8192U];
static uint8_t sample[RF_SCP_EVENT_SIZE];
static uint32_t program_calls;
static uint32_t erase_calls;
static uint32_t failed_program;
static bool drop_crc;
static bool failed_erase;
static bool hide_latest;
static uint32_t hidden_address;

static size_t flash_offset(uint32_t address, uint32_t length)
{
    TEST_ASSERT_TRUE(RF_EVENT_LOG_ADDR <= address);
    const size_t offset = (size_t)(address - RF_EVENT_LOG_ADDR);

    TEST_ASSERT_TRUE(sizeof(flash) >= offset);
    TEST_ASSERT_TRUE(sizeof(flash) - offset >= length);
    return offset;
}

static void read_flash(uint32_t address, void *buffer, uint32_t length,
                        int call_count)
{
    const size_t offset = flash_offset(address, length);

    (void)call_count;
    if (hide_latest && (hidden_address == address))
    {
        (void)memset(buffer, 0xFF, length);
    }
    else
    {
        (void)memcpy(buffer, &flash[offset], length);
    }
}

static int program_flash(uint32_t address, const void *buffer,
                          uint32_t length, int call_count)
{
    const size_t offset = flash_offset(address, length);
    const uint8_t *data = buffer;

    (void)call_count;
    program_calls++;
    TEST_ASSERT_TRUE(256U >= (address % 256U) + length);
    if (drop_crc && (2U == length))
    {
        return W25QXX_RES_OK;
    }
    if (failed_program == program_calls)
    {
        /* Hardware may partially program even when it reports failure. */
        flash[offset] &= data[0];
        return W25QXX_RES_ERROR;
    }
    for (size_t index = 0U; index < length; index++)
    {
        flash[offset + index] &= data[index];
    }
    if (hide_latest && (62U == length))
    {
        hidden_address = address;
    }
    return W25QXX_RES_OK;
}

static int erase_flash(uint32_t address, int call_count)
{
    const size_t offset = flash_offset(address, 4096U);

    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(0U, address % 4096U);
    erase_calls++;
    if (failed_erase)
    {
        return W25QXX_RES_ERROR;
    }
    (void)memset(&flash[offset], 0xFF, 4096U);
    return W25QXX_RES_OK;
}

static void update_crc(void)
{
    const uint16_t crc = log_calculate_crc16(sample, 58U);

    sample[58] = (uint8_t)(crc & 0xFFU);
    sample[59] = (uint8_t)(crc >> 8U);
}

static void append_many(size_t count)
{
    for (size_t index = 0U; index < count; index++)
    {
        sample[51] = (uint8_t)index;
        update_crc();
        TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    }
}

void setUp(void)
{
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);
    bool found = false;

    (void)memset(flash, 0xFF, sizeof(flash));
    program_calls = 0U;
    erase_calls = 0U;
    failed_program = 0U;
    drop_crc = false;
    failed_erase = false;
    hide_latest = false;
    hidden_address = 0U;
    for (size_t index = 0U; index < count; index++)
    {
        const uint8_t *data = rf_scp_vectors[index].logical;

        if ((RF_SCP_CMD_LOG_READ_RANGE == data[3]) &&
            (SCP_TYPE_ACK == data[2]))
        {
            (void)memcpy(sample, &data[7], sizeof(sample));
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
    w25qxx_read_buff_StubWithCallback(read_flash);
    w25qxx_page_write_StubWithCallback(program_flash);
    w25qxx_erase_sector_StubWithCallback(erase_flash);
    bsp_kick_wdt_Ignore();
    TEST_ASSERT_TRUE(rf_event_log_init());
}

void tearDown(void)
{
}

void test_existing_fault_and_replay_regions_keep_their_addresses(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x218000U, FAULT_LOG_ADDRESS);
    TEST_ASSERT_EQUAL_HEX32(0x220000U, FAULT_LOG_BACKUP_ADDRESS);
    TEST_ASSERT_EQUAL_HEX32(0x228000U, IEC104_EVTLOG_ADDR);
    TEST_ASSERT_EQUAL_HEX32(0x230000U, IEC104_LOGAREA_ADDRESS);
    TEST_ASSERT_EQUAL_HEX32(0x232000U, RF_EVENT_LOG_ADDR);
    TEST_ASSERT_EQUAL_UINT32(8192U, RF_EVENT_LOG_SIZE);
}

void test_captured_packet_survives_log_reinitialization_byte_for_byte(void)
{
    rf_event_record_t record;
    size_t count;

    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_EQUAL_UINT32(2U, program_calls);
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_UINT32(1U, count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample, record.data, sizeof(sample));
    TEST_ASSERT_EQUAL_UINT32(0U, erase_calls);
}

void test_invalid_record_preserves_existing_valid_record(void)
{
    rf_event_record_t record;
    uint8_t previous[60];
    size_t count;

    (void)memcpy(previous, sample, sizeof(previous));
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    sample[0] ^= 1U;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_FALSE(rf_event_log_append(sample, 59U));
    TEST_ASSERT_FALSE(rf_event_log_append(NULL, 60U));
    TEST_ASSERT_EQUAL_UINT32(2U, program_calls);
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(previous, record.data, sizeof(previous));
}

void test_unknown_event_invalid_clock_and_diagnostic_nan_are_preserved(void)
{
    rf_event_record_t record;
    size_t count;

    sample[7] = 0xFEU;
    sample[55] = 0U;
    sample[15] = 0U;
    sample[16] = 0U;
    sample[17] = 0xC0U;
    sample[18] = 0x7FU;
    sample[56] = 0x12U;
    sample[57] = 0x34U;
    update_crc();
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample, record.data, sizeof(sample));
}

void test_payload_program_failure_keeps_old_record_and_allows_next_write(void)
{
    rf_event_record_t records[2];
    size_t count;

    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    failed_program = program_calls + 1U;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    failed_program = 0U;
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(2U, count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample, records[0].data, sizeof(sample));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample, records[1].data, sizeof(sample));
}

void test_crc_program_failure_is_not_reported_as_success(void)
{
    rf_event_record_t record;
    size_t count;

    failed_program = 2U;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    failed_program = 0U;
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_UINT32(1U, count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sample, record.data, sizeof(sample));
}

void test_silent_crc_loss_cannot_use_identical_old_record_as_readback(void)
{
    rf_event_record_t records[2];
    size_t count;

    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    drop_crc = true;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    drop_crc = false;
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(1U, count);
}

void test_missing_readback_rejects_identical_previous_record(void)
{
    rf_event_record_t records[2];
    size_t count;

    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    hide_latest = true;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    hide_latest = false;
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(2U, count);
}

void test_two_sector_ring_wraps_only_inside_the_new_eight_kb_region(void)
{
    rf_event_record_t records[128];
    size_t count;

    append_many(128U);
    TEST_ASSERT_EQUAL_UINT32(1U, erase_calls);
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 128U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(128U, count);
    TEST_ASSERT_EQUAL_UINT8(127U, records[0].data[51]);
    TEST_ASSERT_EQUAL_UINT8(0U, records[127].data[51]);
    sample[51] = 128U;
    update_crc();
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_EQUAL_UINT32(2U, erase_calls);
    TEST_ASSERT_TRUE(rf_event_log_init());
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 128U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(65U, count);
    TEST_ASSERT_EQUAL_UINT8(128U, records[0].data[51]);
    TEST_ASSERT_EQUAL_UINT8(64U, records[64].data[51]);
}

void test_failed_sector_erase_preserves_full_ring_and_can_be_retried(void)
{
    rf_event_record_t records[128];
    size_t count;

    append_many(128U);
    failed_erase = true;
    TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 128U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(128U, count);
    failed_erase = false;
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 128U, records, &count));
    TEST_ASSERT_EQUAL_UINT32(65U, count);
}

void test_recent_read_bounds_and_skip_preserve_output_buffer(void)
{
    rf_event_record_t record;
    rf_event_record_t previous;
    size_t count = 99U;

    append_many(3U);
    TEST_ASSERT_TRUE(rf_event_log_read_recent(1U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_UINT32(1U, count);
    TEST_ASSERT_EQUAL_UINT8(1U, record.data[51]);
    previous = record;
    TEST_ASSERT_FALSE(rf_event_log_read_recent(SIZE_MAX, 1U,
                                               &record, &count));
    TEST_ASSERT_EQUAL_UINT32(0U, count);
    TEST_ASSERT_EQUAL_MEMORY(&previous, &record, sizeof(record));
    TEST_ASSERT_FALSE(rf_event_log_read_recent(0U, 0U, &record, &count));
    TEST_ASSERT_FALSE(rf_event_log_read_recent(0U, 1U, NULL, &count));
    TEST_ASSERT_FALSE(rf_event_log_read_recent(0U, 1U, &record, NULL));
    TEST_ASSERT_TRUE(rf_event_log_read_recent(3U, 1U, &record, &count));
    TEST_ASSERT_EQUAL_UINT32(0U, count);
}

void test_reboot_after_torn_program_keeps_confirmed_raw_packets(void)
{
    for (uint32_t step = 1U; 2U >= step; step++)
    {
        setUp();
        sample[51] = 1U;
        update_crc();
        TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
        failed_program = program_calls + step;
        sample[51] = 2U;
        update_crc();
        TEST_ASSERT_FALSE(rf_event_log_append(sample, sizeof(sample)));
        failed_program = 0U;
        TEST_ASSERT_TRUE(rf_event_log_init());
        rf_event_record_t records[2];
        size_t count;
        TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
        TEST_ASSERT_EQUAL_size_t(1U, count);
        TEST_ASSERT_EQUAL_UINT8(1U, records[0].data[51]);
        sample[51] = 3U;
        update_crc();
        TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
        TEST_ASSERT_TRUE(rf_event_log_init());
        TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
        TEST_ASSERT_EQUAL_size_t(2U, count);
        TEST_ASSERT_EQUAL_UINT8(3U, records[0].data[51]);
        TEST_ASSERT_EQUAL_UINT8(1U, records[1].data[51]);
    }
}

void test_corrupt_newest_outer_crc_does_not_hide_an_older_raw_packet(void)
{
    sample[51] = 1U;
    update_crc();
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    sample[51] = 2U;
    update_crc();
    TEST_ASSERT_TRUE(rf_event_log_append(sample, sizeof(sample)));
    flash[64U + LOG_SEQ_SIZE + 20U] ^= 1U;
    TEST_ASSERT_TRUE(rf_event_log_init());
    rf_event_record_t records[2];
    size_t count;
    TEST_ASSERT_TRUE(rf_event_log_read_recent(0U, 2U, records, &count));
    TEST_ASSERT_EQUAL_size_t(1U, count);
    TEST_ASSERT_EQUAL_UINT8(1U, records[0].data[51]);
}

/*** end of file ***/
