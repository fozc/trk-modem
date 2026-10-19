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
    return true;
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
    return LOG_OK;
}

void setUp(void)
{
    fake_tick = 0U;
    writes = 0U;
    last_entry = (elog_entry_t){0};
    bsp_get_tick_Stub(get_tick);
    rtc_now_IgnoreAndReturn((rtc_t){0});
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
/*** end of file ***/
