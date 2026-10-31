/*
 * test_runtime_stats.c
 *
 * Created on: Oct 24, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Real runtime statistics and SPI log over a NOR hardware boundary model.
 */
#include "unity.h"
#include "runtime_stats.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "mock_w25qxx.h"
#include "mock_spi.h"
#include "mock_rtc.h"
#include "mock_stack_monitor.h"
#include "mock_bsp.h"
#include <string.h>

TEST_SOURCE_FILE("spi_flash_log.c")

static uint8_t flash[RUNTIME_STATS_SIZE];
static uint32_t tick;
static uint32_t programs;
static uint32_t erases;
static uint32_t fail_program;
static bool fail_erase;
static bool lose_crc;
static bool clock_valid;
static rtc_sync_info_t sync_info;
static uint32_t stack_peak;
static uint32_t epoch;

static uint32_t epoch_value(int call)
{
    (void)call;
    return epoch;
}

static rtc_sync_info_t last_sync(int call)
{
    (void)call;
    return sync_info;
}

static uint32_t peak_used(int call)
{
    (void)call;
    return stack_peak;
}

static size_t offset_of(uint32_t address, uint32_t length)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(RUNTIME_STATS_ADDR, address);
    const size_t offset = address - RUNTIME_STATS_ADDR;
    TEST_ASSERT_TRUE(offset <= sizeof(flash));
    TEST_ASSERT_TRUE(length <= sizeof(flash) - offset);
    return offset;
}

static void read_flash(uint32_t address, void *out, uint32_t length, int call)
{
    (void)call;
    (void)memcpy(out, flash + offset_of(address, length), length);
}

static int program_flash(uint32_t address, const void *in,
                         uint32_t length, int call)
{
    const size_t offset = offset_of(address, length);
    const uint8_t *data = in;
    (void)call;
    programs++;
    TEST_ASSERT_TRUE((address % 256U) + length <= 256U);
    if (lose_crc && (length == 2U))
    {
        return W25QXX_RES_OK;
    }
    const uint32_t count = (programs == fail_program) ? length / 2U : length;
    for (size_t index = 0U; index < count; index++)
    {
        flash[offset + index] &= data[index];
    }
    return (programs == fail_program) ? W25QXX_RES_WRITE_FAIL : W25QXX_RES_OK;
}

static int erase_flash(uint32_t address, int call)
{
    const size_t offset = offset_of(address, LOG_SECTOR_SIZE);
    (void)call;
    erases++;
    if (fail_erase)
    {
        return W25QXX_RES_WRITE_FAIL;
    }
    (void)memset(flash + offset, 0xFF, LOG_SECTOR_SIZE);
    return W25QXX_RES_OK;
}

static uint32_t get_tick(int call)
{
    (void)call;
    return tick;
}

static bool valid_clock(int call)
{
    (void)call;
    return clock_valid;
}

void setUp(void)
{
    sync_info = (rtc_sync_info_t){0};
    stack_peak = 1536U;
    rtc_get_last_sync_Stub(last_sync);
    stack_monitor_get_used_Stub(peak_used);
    clock_valid = true;
    epoch = 1790000000U;
    (void)memset(flash, 0xFF, sizeof(flash));
    tick = 0U;
    programs = 0U;
    erases = 0U;
    fail_program = 0U;
    fail_erase = false;
    lose_crc = false;
    w25qxx_read_buff_StubWithCallback(read_flash);
    w25qxx_page_write_StubWithCallback(program_flash);
    w25qxx_erase_sector_StubWithCallback(erase_flash);
    spi_get_transfer_status_IgnoreAndReturn(SPI_TRANSFER_OK);
    bsp_get_tick_StubWithCallback(get_tick);
    rtc_hw_is_valid_StubWithCallback(valid_clock);
    rtc_get_unix_epoch_Stub(epoch_value);
}

void tearDown(void)
{
}

void test_empty_log_starts_at_zero_without_boot_time_flash_writes(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    for (uint32_t index = 0U; index < RUNTIME_BOOTS; index++)
    {
        TEST_ASSERT_EQUAL_UINT32(0U,
            runtime_stats_get((runtime_counter_t)index));
    }
    TEST_ASSERT_EQUAL_UINT32(1U, runtime_stats_get(RUNTIME_BOOTS));
    TEST_ASSERT_EQUAL_UINT32(0U, programs);
    TEST_ASSERT_EQUAL_UINT32(0U, erases);
    runtime_stats_set(RUNTIME_LIFETIME, 1234U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT8(2U, flash[2]);
    TEST_ASSERT_EQUAL_UINT8(0xD2U, flash[10]);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(1234U, runtime_stats_get(RUNTIME_LIFETIME));
    TEST_ASSERT_EQUAL_UINT32(2U, runtime_stats_get(RUNTIME_BOOTS));
    TEST_ASSERT_EQUAL_UINT32(2U, programs);
}

static bool create_snapshot(uint32_t lifetime)
{
    if (!runtime_stats_init())
    {
        return false;
    }
    runtime_stats_set(RUNTIME_LIFETIME, lifetime);
    return runtime_stats_save();
}

void test_all_counters_survive_reload_and_wrap_modulo_32_bits(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    for (uint32_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
    {
        runtime_stats_add((runtime_counter_t)index, 100U + index);
    }
    runtime_stats_add(RUNTIME_GSM_RX_BYTES, UINT32_MAX);
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_TRUE(runtime_stats_init());
    for (uint32_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
    {
        uint32_t expected = 100U + index;
        if (index == RUNTIME_GSM_RX_BYTES)
        {
            expected--;
        }
        else if (index == RUNTIME_BOOTS)
        {
            expected += 2U;
        }
        else
        {
            /* Other fields retain exactly the encoded value. */
        }
        TEST_ASSERT_EQUAL_UINT32(expected,
            runtime_stats_get((runtime_counter_t)index));
    }
}

void test_checkpoint_occurs_at_15_minutes_not_every_tick(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    runtime_stats_set(RUNTIME_LIFETIME, 10U);
    tick = 899999U;
    runtime_stats_tick(true);
    TEST_ASSERT_EQUAL_UINT32(909U, runtime_stats_get(RUNTIME_LIFETIME));
    TEST_ASSERT_EQUAL_UINT32(899U,
        runtime_stats_get(RUNTIME_IEC_ACTIVE_SECONDS));
    TEST_ASSERT_EQUAL_UINT32(0U, programs);
    tick = 900000U;
    runtime_stats_tick(false);
    TEST_ASSERT_EQUAL_UINT32(2U, programs);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(910U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_failed_checkpoint_preserves_ram_and_retries_next_interval(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    runtime_stats_set(RUNTIME_LIFETIME, 12U);
    fail_program = programs + 1U;
    tick = 900000U;
    runtime_stats_tick(true);
    const uint32_t failed_calls = programs;
    TEST_ASSERT_EQUAL_UINT32(912U, runtime_stats_get(RUNTIME_LIFETIME));
    tick += 1000U;
    runtime_stats_tick(true);
    TEST_ASSERT_EQUAL_UINT32(failed_calls, programs);
    tick = 1800000U;
    runtime_stats_tick(true);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(1812U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_torn_payload_and_crc_keep_the_previous_snapshot(void)
{
    for (uint32_t stage = 1U; stage <= 2U; stage++)
    {
        TEST_ASSERT_TRUE(create_snapshot(100U));
        const uint32_t baseline = runtime_stats_get(RUNTIME_LIFETIME);
        runtime_stats_add(RUNTIME_LIFETIME, 50U);
        fail_program = programs + stage;
        TEST_ASSERT_FALSE(runtime_stats_save());
        TEST_ASSERT_TRUE(runtime_stats_init());
        TEST_ASSERT_EQUAL_UINT32(baseline, runtime_stats_get(RUNTIME_LIFETIME));
        fail_program = 0U;
    }
}

void test_dropped_crc_cannot_pass_readback_using_identical_old_payload(void)
{
    TEST_ASSERT_TRUE(create_snapshot(100U));
    lose_crc = true;
    TEST_ASSERT_FALSE(runtime_stats_save());
    lose_crc = false;
    TEST_ASSERT_TRUE(runtime_stats_save());
}

void test_sector_rotation_and_failed_erase_preserve_last_record(void)
{
    TEST_ASSERT_TRUE(create_snapshot(0U));
    for (uint32_t index = 1U; index < 60U; index++)
    {
        runtime_stats_set(RUNTIME_LIFETIME, index);
        TEST_ASSERT_TRUE(runtime_stats_save());
    }
    TEST_ASSERT_EQUAL_UINT32(0U, erases);
    fail_erase = true;
    runtime_stats_set(RUNTIME_LIFETIME, 60U);
    TEST_ASSERT_FALSE(runtime_stats_save());
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(59U, runtime_stats_get(RUNTIME_LIFETIME));
    fail_erase = false;
    for (uint32_t index = 60U; index < 300U; index++)
    {
        runtime_stats_set(RUNTIME_LIFETIME, index);
        TEST_ASSERT_TRUE(runtime_stats_save());
    }
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(299U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_corrupt_latest_record_falls_back_to_prior_valid_record(void)
{
    TEST_ASSERT_TRUE(create_snapshot(10U));
    runtime_stats_set(RUNTIME_LIFETIME, 20U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    flash[68U + 10U] ^= 1U;
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(10U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_unknown_version_does_not_reformat_or_overwrite_log(void)
{
    TEST_ASSERT_TRUE(create_snapshot(10U));
    flash[2] = 255U;
    const uint16_t crc = log_calculate_crc16(flash, 66U);
    flash[66] = (uint8_t)crc;
    flash[67] = (uint8_t)(crc >> 8U);
    const uint32_t before = programs;
    TEST_ASSERT_FALSE(runtime_stats_init());
    TEST_ASSERT_FALSE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT32(before, programs);
    TEST_ASSERT_EQUAL_UINT32(0U, erases);
}

void test_spi_read_error_is_not_treated_as_empty_flash(void)
{
    spi_get_transfer_status_IgnoreAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_FALSE(runtime_stats_init());
    TEST_ASSERT_FALSE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT32(0U, programs);
    TEST_ASSERT_EQUAL_UINT32(0U, erases);
}

void test_invalid_clock_is_explicit_and_bad_arguments_preserve_output(void)
{
    clock_valid = false;
    TEST_ASSERT_TRUE(create_snapshot(100U));
    runtime_stats_t record;
    TEST_ASSERT_TRUE(runtime_stats_read(0U, &record));
    TEST_ASSERT_FALSE(record.clock_valid);
    TEST_ASSERT_EQUAL_UINT32(0U, record.timestamp);
    record.timestamp = 42U;
    TEST_ASSERT_FALSE(runtime_stats_read(240U, &record));
    TEST_ASSERT_EQUAL_UINT32(42U, record.timestamp);
    TEST_ASSERT_FALSE(runtime_stats_read(0U, NULL));
    runtime_stats_add((runtime_counter_t)UINT32_MAX, 99U);
    runtime_stats_set((runtime_counter_t)UINT32_MAX, 99U);
    TEST_ASSERT_EQUAL_UINT32(0U,
        runtime_stats_get((runtime_counter_t)UINT32_MAX));
    runtime_stats_snapshot(NULL);
}

void test_tick_wrap_and_restart_before_first_checkpoint(void)
{
    tick = UINT32_MAX - 499U;
    TEST_ASSERT_TRUE(runtime_stats_init());
    fail_program = 1U;
    runtime_stats_set(RUNTIME_LIFETIME, 33U);
    TEST_ASSERT_FALSE(runtime_stats_save());
    fail_program = 0U;
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(0U, runtime_stats_get(RUNTIME_LIFETIME));
    tick = 500U;
    runtime_stats_tick(true);
    TEST_ASSERT_EQUAL_UINT32(1U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_sequence_wrap_keeps_latest_snapshot_and_history_order(void)
{
    TEST_ASSERT_TRUE(create_snapshot(10U));
    /* Valid on-flash record just before the reserved 0xFFFF sequence. */
    flash[0] = 0xFEU;
    flash[1] = 0xFFU;
    const uint16_t crc = log_calculate_crc16(flash, 66U);
    flash[66] = (uint8_t)crc;
    flash[67] = (uint8_t)(crc >> 8U);
    TEST_ASSERT_TRUE(runtime_stats_init());
    runtime_stats_set(RUNTIME_LIFETIME, 20U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT8(0U, flash[68]);
    TEST_ASSERT_EQUAL_UINT8(0U, flash[69]);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(20U, runtime_stats_get(RUNTIME_LIFETIME));
    runtime_stats_t previous;
    TEST_ASSERT_TRUE(runtime_stats_read(1U, &previous));
    TEST_ASSERT_EQUAL_UINT32(10U, previous.counters[RUNTIME_LIFETIME]);
}

void test_snapshot_persists_current_rtc_source_time_and_stack_peak(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    const rtc_sync_info_t update = {.source = RTC_SOURCE_NTP, .timestamp = 1790000000U};
    sync_info = update;
    runtime_stats_t record;
    runtime_stats_snapshot(&record);
    TEST_ASSERT_EQUAL(RTC_SOURCE_NTP, record.rtc_last_sync.source);
    TEST_ASSERT_EQUAL_UINT32(update.timestamp, record.rtc_last_sync.timestamp);
    TEST_ASSERT_EQUAL_UINT32(1536U, record.stack_peak_bytes);
    sync_info = update;
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_TRUE(runtime_stats_read(0U, &record));
    TEST_ASSERT_EQUAL(RTC_SOURCE_NTP, record.rtc_last_sync.source);
    TEST_ASSERT_EQUAL_UINT32(update.timestamp, record.rtc_last_sync.timestamp);
    TEST_ASSERT_EQUAL_UINT32(1536U, record.stack_peak_bytes);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_TRUE(runtime_stats_read(0U, &record));
    TEST_ASSERT_EQUAL_UINT32(update.timestamp, record.rtc_last_sync.timestamp);
    TEST_ASSERT_EQUAL_UINT32(1536U, record.stack_peak_bytes);
    TEST_ASSERT_EQUAL_UINT8(2U, flash[2]);
}

void test_history_retains_all_240_records_and_never_crosses_four_sector_region(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    for (uint32_t index = 0U; index < RUNTIME_STATS_CAPACITY; index++)
    {
        runtime_stats_set(RUNTIME_LIFETIME, index);
        TEST_ASSERT_TRUE(runtime_stats_save());
    }
    TEST_ASSERT_TRUE(runtime_stats_init());
    runtime_stats_t record;
    for (uint32_t skip = 0U; skip < RUNTIME_STATS_CAPACITY; skip++)
    {
        TEST_ASSERT_TRUE(runtime_stats_read(skip, &record));
        TEST_ASSERT_EQUAL_UINT32(RUNTIME_STATS_CAPACITY - 1U - skip, record.counters[RUNTIME_LIFETIME]);
    }
    runtime_stats_set(RUNTIME_LIFETIME, 240U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_TRUE(runtime_stats_read(180U, &record));
    TEST_ASSERT_EQUAL_UINT32(60U, record.counters[RUNTIME_LIFETIME]);
    TEST_ASSERT_FALSE(runtime_stats_read(181U, &record));
}

void test_oversized_stack_peak_is_rejected_before_flash_write(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    stack_peak = 65536U;
    TEST_ASSERT_FALSE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT32(0U, programs);
    stack_peak = 65535U;
    TEST_ASSERT_TRUE(runtime_stats_save());
    runtime_stats_t record;
    TEST_ASSERT_TRUE(runtime_stats_read(0U, &record));
    TEST_ASSERT_EQUAL_UINT32(65535U, record.stack_peak_bytes);
}

void test_metadata_is_from_each_checkpoint_not_the_latest_ram_value(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    sync_info = (rtc_sync_info_t){.source = RTC_SOURCE_IEC104, .timestamp = 100U};
    stack_peak = 1800U;
    TEST_ASSERT_TRUE(runtime_stats_save());
    sync_info = (rtc_sync_info_t){.source = RTC_SOURCE_NTP, .timestamp = 200U};
    stack_peak = 2048U;
    TEST_ASSERT_TRUE(runtime_stats_save());
    runtime_stats_t record;
    TEST_ASSERT_TRUE(runtime_stats_read(0U, &record));
    TEST_ASSERT_EQUAL(RTC_SOURCE_NTP, record.rtc_last_sync.source);
    TEST_ASSERT_EQUAL_UINT32(200U, record.rtc_last_sync.timestamp);
    TEST_ASSERT_EQUAL_UINT32(2048U, record.stack_peak_bytes);
    TEST_ASSERT_TRUE(runtime_stats_read(1U, &record));
    TEST_ASSERT_EQUAL(RTC_SOURCE_IEC104, record.rtc_last_sync.source);
    TEST_ASSERT_EQUAL_UINT32(100U, record.rtc_last_sync.timestamp);
    TEST_ASSERT_EQUAL_UINT32(1800U, record.stack_peak_bytes);
}

void test_version_two_wire_header_has_fixed_offsets_and_little_endian_fields(void)
{
    TEST_ASSERT_TRUE(runtime_stats_init());
    epoch = 0x01020304U;
    sync_info = (rtc_sync_info_t){.source = RTC_SOURCE_WEB, .timestamp = 0x55667788U};
    stack_peak = 0x1234U;
    runtime_stats_set(RUNTIME_LIFETIME, 0x11223344U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    const uint8_t expected[] = {0U, 0U, 2U, 13U, 0x34U, 0x12U,
                                4U, 3U, 2U, 1U, 0x44U, 0x33U, 0x22U, 0x11U};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, flash, sizeof(expected));
    const uint8_t rtc_epoch[] = {0x88U, 0x77U, 0x66U, 0x55U};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rtc_epoch, flash + 62U, sizeof(rtc_epoch));
}

void test_invalid_source_or_reserved_flag_never_authorizes_overwriting_history(void)
{
    TEST_ASSERT_TRUE(create_snapshot(100U));
    for (uint32_t index = 0U; index < 2U; index++)
    {
        flash[3] = (index == 0U) ? 15U : 0x80U;
        const uint16_t crc = log_calculate_crc16(flash, 66U);
        flash[66] = (uint8_t)crc;
        flash[67] = (uint8_t)(crc >> 8U);
        const uint32_t before = programs;
        TEST_ASSERT_FALSE(runtime_stats_init());
        TEST_ASSERT_FALSE(runtime_stats_save());
        TEST_ASSERT_EQUAL_UINT32(before, programs);
        TEST_ASSERT_EQUAL_UINT32(0U, erases);
    }
}

void test_torn_writes_at_each_page_crossing_stage_preserve_the_previous_checkpoint(void)
{
    for (uint32_t stage = 1U; stage <= 3U; stage++)
    {
        (void)memset(flash, 0xFF, sizeof(flash));
        programs = 0U;
        fail_program = 0U;
        TEST_ASSERT_TRUE(runtime_stats_init());
        for (uint32_t index = 1U; index <= 3U; index++)
        {
            runtime_stats_set(RUNTIME_LIFETIME, index * 100U);
            TEST_ASSERT_TRUE(runtime_stats_save());
        }
        /* The fourth 68-byte entry starts at byte 204: two payload programs plus CRC. */
        fail_program = programs + stage;
        runtime_stats_set(RUNTIME_LIFETIME, 400U);
        TEST_ASSERT_FALSE(runtime_stats_save());
        fail_program = 0U;
        TEST_ASSERT_TRUE(runtime_stats_init());
        TEST_ASSERT_EQUAL_UINT32(300U, runtime_stats_get(RUNTIME_LIFETIME));
        runtime_stats_set(RUNTIME_LIFETIME, 500U);
        TEST_ASSERT_TRUE(runtime_stats_save());
        TEST_ASSERT_TRUE(runtime_stats_init());
        TEST_ASSERT_EQUAL_UINT32(500U, runtime_stats_get(RUNTIME_LIFETIME));
    }
}

/*** end of file ***/
