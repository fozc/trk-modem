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
    clock_valid = true;
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
    rtc_get_unix_epoch_IgnoreAndReturn(1790000000U);
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
    TEST_ASSERT_EQUAL_UINT8(1U, flash[2]);
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
    for (uint32_t index = 1U; index < 64U; index++)
    {
        runtime_stats_set(RUNTIME_LIFETIME, index);
        TEST_ASSERT_TRUE(runtime_stats_save());
    }
    TEST_ASSERT_EQUAL_UINT32(0U, erases);
    fail_erase = true;
    runtime_stats_set(RUNTIME_LIFETIME, 64U);
    TEST_ASSERT_FALSE(runtime_stats_save());
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(63U, runtime_stats_get(RUNTIME_LIFETIME));
    fail_erase = false;
    for (uint32_t index = 64U; index < 300U; index++)
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
    flash[64U + 10U] ^= 1U;
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(10U, runtime_stats_get(RUNTIME_LIFETIME));
}

void test_unknown_version_does_not_reformat_or_overwrite_log(void)
{
    TEST_ASSERT_TRUE(create_snapshot(10U));
    flash[2] = 2U;
    const uint16_t crc = log_calculate_crc16(flash, 62U);
    flash[62] = (uint8_t)crc;
    flash[63] = (uint8_t)(crc >> 8U);
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
    TEST_ASSERT_FALSE(runtime_stats_read(128U, &record));
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
    const uint16_t crc = log_calculate_crc16(flash, 62U);
    flash[62] = (uint8_t)crc;
    flash[63] = (uint8_t)(crc >> 8U);
    TEST_ASSERT_TRUE(runtime_stats_init());
    runtime_stats_set(RUNTIME_LIFETIME, 20U);
    TEST_ASSERT_TRUE(runtime_stats_save());
    TEST_ASSERT_EQUAL_UINT8(0U, flash[64]);
    TEST_ASSERT_EQUAL_UINT8(0U, flash[65]);
    TEST_ASSERT_TRUE(runtime_stats_init());
    TEST_ASSERT_EQUAL_UINT32(20U, runtime_stats_get(RUNTIME_LIFETIME));
    runtime_stats_t previous;
    TEST_ASSERT_TRUE(runtime_stats_read(1U, &previous));
    TEST_ASSERT_EQUAL_UINT32(10U, previous.counters[RUNTIME_LIFETIME]);
}

/*** end of file ***/
