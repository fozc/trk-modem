/*
 * test_elog_uart_recovery.c
 *
 *  Created on: Oct 19, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 *  Verify UART recovery records through the production elog writer.
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_rtc.h"
TEST_SOURCE_FILE("xprintf.c")
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
/* Existing elog shell/64-bit host formatting exceptions only. */
#include "../../Application/elog.c"
#pragma GCC diagnostic pop

static uint32_t fake_tick;
static uint32_t writes;
static elog_entry_t last_entry;
static elog_entry_t stored_entry;
static bool storage_ready;
static bool drop_write;
static bool omit_readback;
static log_status_t write_result;
static log_status_t read_result;

time32_t dt_conv_to_epoch(const datetime_t *date)
{
    TEST_ASSERT_NOT_NULL(date);
    return 1000U;
}

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return fake_tick;
}

bool log_is_initialized(const log_ctx_t *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&elog_ctx, ctx);
    return storage_ready;
}

uint32_t log_get_next_seq(const log_ctx_t *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&elog_ctx, ctx);
    return writes;
}

log_status_t log_write(log_ctx_t *ctx, const void *payload)
{
    TEST_ASSERT_EQUAL_PTR(&elog_ctx, ctx);
    last_entry = *(const elog_entry_t *)payload;
    writes++;
    if ((write_result == LOG_OK) && !drop_write)
    {
        stored_entry = last_entry;
    }
    return write_result;
}

log_status_t log_read_last(log_ctx_t *ctx, uint32_t count, log_visit_fn visit, void *user_ctx,
                            log_page_ctx_t *page)
{
    TEST_ASSERT_EQUAL_PTR(&elog_ctx, ctx);
    TEST_ASSERT_EQUAL_UINT32(1U, count);
    page->page_count = 0U;
    if ((read_result == LOG_OK) && !omit_readback)
    {
        visit(&stored_entry, sizeof(stored_entry), stored_entry.entry_id, user_ctx);
        page->page_count = 1U;
    }
    return read_result;
}

void setUp(void)
{
    fake_tick = 0U;
    writes = 0U;
    last_entry = (elog_entry_t){0};
    stored_entry = (elog_entry_t){0};
    storage_ready = true;
    drop_write = false;
    omit_readback = false;
    write_result = LOG_OK;
    read_result = LOG_OK;
    bsp_get_tick_Stub(get_tick);
    rtc_now_IgnoreAndReturn((rtc_t){0});
    rtc_hw_is_valid_IgnoreAndReturn(true);
}

void tearDown(void)
{
}

void test_uart_recovery_payload_outcome_changes_and_per_port_rate_limit(void)
{
    elog_log_uart_recovery(1U, false, 0x10203040U, 0x50607080U);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    TEST_ASSERT_EQUAL_UINT8(ELOG_UART_TX_RECOVERY, last_entry.code);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_ERROR, last_entry.level);
    TEST_ASSERT_EQUAL_UINT32(1000U, last_entry.timestamp);
    static const uint8_t expected[] =
    {
        1U, 0U, 0x10U, 0x20U, 0x30U, 0x40U,
        0x50U, 0x60U, 0x70U, 0x80U
    };
    TEST_ASSERT_EQUAL_MEMORY(expected, last_entry.info, sizeof(expected));
    elog_log_uart_recovery(1U, false, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    fake_tick = 10U;
    elog_log_uart_recovery(1U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_WARN, last_entry.level);
    TEST_ASSERT_EQUAL_UINT8(1U, last_entry.info[1]);
    fake_tick = 60009U;
    elog_log_uart_recovery(1U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    fake_tick++;
    elog_log_uart_recovery(1U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(3U, writes);
    fake_tick = UINT32_MAX - 20U;
    elog_log_uart_recovery(4U, false, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(4U, writes);
    fake_tick = 10U;
    elog_log_uart_recovery(4U, false, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(4U, writes);
    fake_tick = 59980U;
    elog_log_uart_recovery(4U, false, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(5U, writes);
}

void test_uart_recovery_rejects_unknown_ports_without_writing(void)
{
    elog_log_uart_recovery(0U, false, 0U, 0U);
    elog_log_uart_recovery(3U, false, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, writes);
}

void test_rf_trip_failure_edges_use_real_elog_writer_and_decoder(void)
{
    elog_log_rf_trip_failure(1U, 2U, true);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    TEST_ASSERT_EQUAL_UINT8(ELOG_RF_TRIP_FAILURE, last_entry.code);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_ERROR, last_entry.level);
    TEST_ASSERT_EQUAL_UINT32(1000U, last_entry.timestamp);
    const uint8_t expected[3] = {1U, 2U, 1U};
    TEST_ASSERT_EQUAL_MEMORY(expected, last_entry.info, sizeof(expected));
    TEST_ASSERT_EQUAL_STRING("RF feeder=1 phase=2 active", elog_info_to_text(&last_entry));
    elog_log_rf_trip_failure(1U, 2U, false);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_INFO, last_entry.level);
    TEST_ASSERT_EQUAL_STRING("RF feeder=1 phase=2 cleared", elog_info_to_text(&last_entry));
    elog_log_rf_trip_failure(0U, 2U, true);
    elog_log_rf_trip_failure(5U, 2U, true);
    elog_log_rf_trip_failure(1U, 0U, true);
    elog_log_rf_trip_failure(1U, 4U, true);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
}

void test_operator_ack_uses_real_elog_writer_and_decodes_identity_and_ongoing_state(void)
{
    uint8_t info[16] = {1U, 2U, 1U, 1U, 105U, 3U, 123U, 0U, 0x40U, 0xE2U, 1U, 0U};
    elog_add(ELOG_RF_ALARM_ACK, ELOG_LEVEL_INFO, info, sizeof(info));
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    TEST_ASSERT_EQUAL_UINT8(ELOG_RF_ALARM_ACK, last_entry.code);
    TEST_ASSERT_EQUAL_UINT32(1000U, last_entry.timestamp);
    TEST_ASSERT_EQUAL_MEMORY(info, last_entry.info, sizeof(info));
    TEST_ASSERT_EQUAL_STRING("RF F1 Ph2 ack web/admin event=105 boot=123 up=123456 ongoing",
                              elog_info_to_text(&last_entry));
    info[5] = 1U;
    elog_add(ELOG_RF_ALARM_ACK, ELOG_LEVEL_INFO, info, sizeof(info));
    TEST_ASSERT_EQUAL_STRING("RF F1 Ph2 ack web/admin event=105 boot=123 up=123456 closed",
                              elog_info_to_text(&last_entry));
}

void test_rf_commit_audit_contains_time_identity_crc_and_requires_readback(void)
{
    TEST_ASSERT_TRUE(elog_log_rf_commit(3U, 1U, 17U, 0xABCDU));
    TEST_ASSERT_EQUAL_UINT8(ELOG_RF_CFG_COMMIT, last_entry.code);
    TEST_ASSERT_EQUAL_UINT32(1000U, last_entry.timestamp);
    const uint8_t expected[] = {3U, 1U, 17U, 0xCDU, 0xABU, 1U};
    TEST_ASSERT_EQUAL_MEMORY(expected, last_entry.info, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(&last_entry, &stored_entry, sizeof(last_entry));
    TEST_ASSERT_EQUAL_STRING("RF COMMIT intent line=3 feeder=1 group=17 cfg_crc=ABCD clock=valid",
                              elog_info_to_text(&last_entry));
    rtc_hw_is_valid_IgnoreAndReturn(false);
    TEST_ASSERT_TRUE(elog_log_rf_commit(3U, 1U, 18U, 0x1234U));
    TEST_ASSERT_EQUAL_UINT32(0U, last_entry.timestamp);
    TEST_ASSERT_EQUAL_UINT8(0U, last_entry.info[5]);
}

void test_rf_commit_audit_rejects_invalid_identity_and_storage_failures(void)
{
    TEST_ASSERT_FALSE(elog_log_rf_commit(0U, 1U, 1U, 0U));
    TEST_ASSERT_FALSE(elog_log_rf_commit(8U, 1U, 1U, 0U));
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 0U, 1U, 0U));
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 5U, 1U, 0U));
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 0U, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, writes);
    storage_ready = false;
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 1U, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, writes);
    storage_ready = true;
    write_result = LOG_ERR_FLASH_PROGRAM;
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 1U, 0U));
    write_result = LOG_OK;
    read_result = LOG_ERR_FLASH_READ;
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 1U, 0U));
    read_result = LOG_OK;
    omit_readback = true;
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 1U, 0U));
}

void test_rf_commit_audit_does_not_accept_an_identical_older_entry_after_silent_write_loss(void)
{
    TEST_ASSERT_TRUE(elog_log_rf_commit(1U, 1U, 1U, 0x1234U));
    drop_write = true;
    TEST_ASSERT_FALSE(elog_log_rf_commit(1U, 1U, 1U, 0x1234U));
    TEST_ASSERT_EQUAL_UINT16(0U, stored_entry.entry_id);
    TEST_ASSERT_EQUAL_UINT16(1U, last_entry.entry_id);
}

void test_rf_maintenance_uses_real_writer_and_distinguishes_hub_and_ay(void)
{
    elog_log_rf_maintenance(ELOG_RF_MAINTENANCE_AY, 1U, 3U, 6U, 0xABCDU);
    TEST_ASSERT_EQUAL_UINT8(ELOG_RF_MAINTENANCE, last_entry.code);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_ERROR, last_entry.level);
    const uint8_t expected[] = {2U, 1U, 3U, 6U, 0xCDU, 0xABU};
    TEST_ASSERT_EQUAL_MEMORY(expected, last_entry.info, sizeof(expected));
    TEST_ASSERT_EQUAL_STRING("RF maintenance AY feeder=1 phase=3 code=6 hash=ABCD; contact Bolatex",
                              elog_info_to_text(&last_entry));
    elog_log_rf_maintenance(ELOG_RF_MAINTENANCE_MH, 0U, 0U, 4U, 0U);
    TEST_ASSERT_NOT_NULL(strstr(elog_info_to_text(&last_entry), "maintenance MH"));
    elog_log_rf_maintenance((elog_rf_maintenance_kind_t)3U, 0U, 0U, 4U, 0U);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
}

/*** end of file ***/
