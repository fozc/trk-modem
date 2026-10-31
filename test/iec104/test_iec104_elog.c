/**
 * @file test_iec104_elog.c
 * @brief Diagnostic records, close reason decoding and repeat suppression.
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "iec104_elog.h"
#include "mock_spi_flash_log.h"
#include "mock_w25qxx.h"
#include "mock_rtc.h"
#include "datetime.h"
#include "mock_bsp.h"
#include "mock_shell.h"
#include <string.h>

TEST_SOURCE_FILE("iec104_elog.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("datetime.c")
TEST_SOURCE_FILE("utils.c")

static uint32_t now;
static uint32_t writes;
static log_status_t write_result;
static iec104_elog_entry_t last_entry;

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return now;
}

static log_status_t capture_record(log_ctx_t *ctx, const void *payload, int call_count)
{
    (void)ctx;
    (void)call_count;
    memcpy(&last_entry, payload, sizeof(last_entry));
    writes++;
    return write_result;
}

void setUp(void)
{
    now = 0U;
    writes = 0U;
    write_result = LOG_OK;
    last_entry = (iec104_elog_entry_t){0};
    log_init_IgnoreAndReturn(LOG_OK);
    log_is_initialized_IgnoreAndReturn(true);
    log_get_next_seq_IgnoreAndReturn(0U);
    log_write_Stub(capture_record);
    bsp_get_tick_Stub(get_tick);
    rtc_now_IgnoreAndReturn(((rtc_t){.day = 31U, .month = 10U, .year = 26U}));
    iec104_elog_init();
}

void tearDown(void)
{
}

void test_disconnect_repeat_window_includes_tick_zero_and_exact_five_seconds(void)
{
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    now = 1U;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    now = 4999U;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    now = 5000U;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
}

void test_different_disconnect_reason_is_recorded_immediately(void)
{
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    iec104_elog_disconnected(IEC104_ELOG_DISC_TESTFR_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    TEST_ASSERT_EQUAL_UINT8(IEC104_ELOG_DISC, last_entry.code);
    TEST_ASSERT_EQUAL_UINT8(IEC104_ELOG_LEVEL_WARN, last_entry.level);
    TEST_ASSERT_EQUAL_UINT8(IEC104_ELOG_DISC_TESTFR_TIMEOUT, last_entry.info[1]);
    TEST_ASSERT_EQUAL_UINT32(1793404800U, last_entry.timestamp);
    TEST_ASSERT_EQUAL_STRING("disconnected (local: TESTFR_CON T1 timeout)",
                             iec104_elog_info_to_text(&last_entry));
    iec104_elog_disconnected(IEC104_ELOG_DISC_TESTFR_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
}

void test_connect_and_disconnect_have_independent_repeat_windows(void)
{
    iec104_elog_connected(0xC0000201U);
    TEST_ASSERT_EQUAL_UINT8(IEC104_ELOG_CONN, last_entry.code);
    TEST_ASSERT_EQUAL_STRING("connected 192.0.2.1", iec104_elog_info_to_text(&last_entry));
    iec104_elog_disconnected(IEC104_ELOG_DISC_NO_CARRIER);
    iec104_elog_connected(0xC0000201U);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    now = 4999U;
    iec104_elog_connected(0xC0000201U);
    now = 5000U;
    iec104_elog_connected(0xC0000201U);
    TEST_ASSERT_EQUAL_UINT32(3U, writes);
}

void test_repeat_window_survives_tick_wrap(void)
{
    now = UINT32_MAX - 1000U;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    now += 4999U;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    now++;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
}

void test_failed_write_does_not_suppress_the_next_record(void)
{
    write_result = LOG_ERR_FLASH_PROGRAM;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    write_result = LOG_OK;
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    iec104_elog_disconnected(IEC104_ELOG_DISC_ACK_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(2U, writes);
    write_result = LOG_ERR_FLASH_PROGRAM;
    iec104_elog_connected(0U);
    write_result = LOG_OK;
    iec104_elog_connected(0U);
    TEST_ASSERT_EQUAL_UINT32(4U, writes);
}

void test_new_close_reasons_and_historical_records_decode_without_layout_change(void)
{
    const char *const expected[] =
    {
        "disconnected (closed by remote)", "disconnected (no carrier)",
        "disconnected (connection timeout)", "disconnected (local close)",
        "disconnected (socket closed)", "disconnected (unknown)",
        "disconnected (local: I-frame ACK T1 timeout)", "disconnected (local: TESTFR_CON T1 timeout)",
        "disconnected (local: command reply TX timeout)", "disconnected (local: initialization TX timeout)",
        "disconnected (local: invalid N(R))", "disconnected (local: unexpected N(S))",
        "disconnected (local: invalid frame)", "disconnected (local: RX overflow)",
        "disconnected (local: I/S frame while stopped)", "disconnected (local: command reply overload)"
    };
    TEST_ASSERT_EQUAL_UINT32(24U, sizeof(iec104_elog_entry_t));
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]); index++)
    {
        iec104_elog_disconnected((iec104_elog_disc_reason_t)index);
        TEST_ASSERT_EQUAL_STRING(expected[index], iec104_elog_info_to_text(&last_entry));
    }
    iec104_elog_disconnected((iec104_elog_disc_reason_t)255U);
    TEST_ASSERT_EQUAL_STRING("disconnected (unknown)", iec104_elog_info_to_text(&last_entry));
}

/*** end of file ***/
