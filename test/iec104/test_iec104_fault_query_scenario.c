/*
 * test_iec104_fault_query_scenario.c
 *
 *  Created on: Oct 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real application GI producers, protocol and cooperative scheduler.
 */
#include "unity.h"
#include "iec104.h"
#include "iec104_application.h"
#include "iec104_platform_fake.h"
#include "iec104_config.h"
#include "mock_iec104_replay.h"
#include "contiki.h"
#include "contiki_process.h"
#include "mock_gsm_listener_process.h"
#include "mock_reboot.h"
#include <string.h>

TEST_SOURCE_FILE("iec104.c")
TEST_SOURCE_FILE("iec104_application.c")
TEST_SOURCE_FILE("iec104_platform_fake.c")
TEST_SOURCE_FILE("iec104_config.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("contiki_replay_scheduler.c")

PROCESS(iec104_replay_process, "replay boundary");
PROCESS(iec104_replay_fallback_timer, "replay timer boundary");
PROCESS_THREAD(iec104_replay_process, ev, data)
{
    (void)ev;
    (void)data;
    PROCESS_BEGIN();
    PROCESS_END();
}
PROCESS_THREAD(iec104_replay_fallback_timer, ev, data)
{
    (void)ev;
    (void)data;
    PROCESS_BEGIN();
    PROCESS_END();
}

static clock_time_t now;
static uint8_t frames[32][255];
static size_t frame_count;
static bool reject;

clock_time_t clock_time(void)
{
    return now;
}

static int send_frame(const uint8_t *data, uint16_t length)
{
    if (reject)
    {
        return -1;
    }
    TEST_ASSERT_LESS_THAN_UINT32(32U, frame_count);
    (void)memcpy(frames[frame_count++], data, length);
    return 0;
}

static void feed(const uint8_t *frame, size_t length)
{
    iec104_data_received(frame, (uint16_t)length);
    libiec104_poll();
}

static void acknowledge(void)
{
    const uint16_t sn = iec104_get_send_sn();
    const uint8_t frame[] = {0x68U, 4U, 1U, 0U,
        (uint8_t)(sn << 1U), (uint8_t)(sn >> 7U)};
    feed(frame, sizeof(frame));
}

static void query(uint8_t qoi, uint8_t originator)
{
    const uint16_t ns = iec104_get_receive_sn();
    const uint16_t nr = iec104_get_send_sn();
    const uint8_t frame[] = {0x68U, 14U,
        (uint8_t)(ns << 1U), (uint8_t)(ns >> 7U),
        (uint8_t)(nr << 1U), (uint8_t)(nr >> 7U),
        100U, 1U, 6U, originator, 1U, 0U, 0U, 0U, 0U, qoi};
    feed(frame, sizeof(frame));
}

static void advance(void)
{
    now += 100U;
    etimer_request_poll();
    for (size_t step = 0U; step < 100U; step++)
    {
        if (0 == process_run())
        {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Application scheduler did not settle");
}

void setUp(void)
{
    iec104_replay_link_established_Ignore();
    now = 0U;
    frame_count = 0U;
    reject = false;
    mock_platform_reset();
    process_init();
    process_start(&etimer_process, NULL);
    iec104_application_init();
    const iec104_config_t config = {.common_address = 1U,
        .k_max = 1U, .w_max = 32U, .t1_max = 15U,
        .t2_max = 10U, .t3_max = 60U};
    const iec104_io_t io = {.send = send_frame,
        .on_event = iec104_application_event_handler};
    iec104_init(&io, &config);
    const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    feed(start, sizeof(start));
    acknowledge();
    frame_count = 0U;
}

void tearDown(void)
{
    iec104_reset();
    iec104_application_event_handler(IEC104_EVT_SOCKET_CLOSED);
}

static void assert_query_frames(uint8_t qoi, size_t count)
{
    TEST_ASSERT_EQUAL_UINT32(count, frame_count);
    for (size_t index = 0U; index < frame_count; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0x3CU, frames[index][9]);
        TEST_ASSERT_EQUAL_UINT8((0U == index) ? 7U :
            ((index + 1U == frame_count) ? 10U : qoi), frames[index][8]);
    }
}

void test_fault_queries_retry_confirmation_data_and_term_with_window_one(void)
{
    for (uint8_t qoi = 23U; qoi <= 24U; qoi++)
    {
        mock_breaker_set_line_in_use(0U, true);
        mock_fault_log_fill(0U, 0U, (23U == qoi) ?
            FAULT_LOG_TYPE_TEMPORARY : FAULT_LOG_TYPE_PERMANENT, 1U);
        reject = true;
        query(qoi, 0x3CU);
        advance();
        TEST_ASSERT_EQUAL_UINT32(0U, frame_count);
        reject = false;
        advance();
        TEST_ASSERT_EQUAL_UINT32(1U, frame_count);
        for (size_t step = 0U; step < 8U; step++)
        {
            acknowledge();
            advance();
        }
        assert_query_frames(qoi, 6U);
        frame_count = 0U;
    }
}

void test_other_command_cannot_replace_fault_query_originator(void)
{
    reject = true;
    query(23U, 0x3CU);
    const uint8_t reset[] = {0x68U, 14U, 2U, 0U, 2U, 0U,
        105U, 1U, 6U, 0x63U, 1U, 0U, 0U, 0U, 0U, 0U};
    feed(reset, sizeof(reset));
    reject = false;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(105U, frames[0][6]);
    TEST_ASSERT_EQUAL_UINT8(0x63U, frames[0][9]);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
    acknowledge();
    advance();
    acknowledge();
    advance();
    TEST_ASSERT_EQUAL_UINT32(3U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(0x3CU, frames[1][9]);
    TEST_ASSERT_EQUAL_UINT8(7U, frames[1][8]);
    TEST_ASSERT_EQUAL_UINT8(0x3CU, frames[2][9]);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
}

void test_socket_close_cancels_waiting_query_and_allows_new_originator(void)
{
    reject = true;
    query(23U, 0x11U);
    iec104_reset();
    iec104_application_event_handler(IEC104_EVT_SOCKET_CLOSED);
    TEST_ASSERT_FALSE(process_is_running(&iec104_send_temporary_faults));
    reject = false;
    const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    feed(start, sizeof(start));
    acknowledge();
    frame_count = 0U;
    query(24U, 0x3CU);
    acknowledge();
    advance();
    assert_query_frames(24U, 2U);
}

void test_second_query_is_rejected_without_replacing_pending_query(void)
{
    query(23U, 0x3CU);
    acknowledge();
    query(24U, 0x63U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[1][8]);
    TEST_ASSERT_EQUAL_UINT8(0x63U, frames[1][9]);
    acknowledge();
    advance();
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
    TEST_ASSERT_EQUAL_UINT8(0x3CU, frames[2][9]);
    TEST_ASSERT_FALSE(process_is_running(&iec104_send_permanent_faults));
}

void test_stop_start_pauses_fault_query_until_link_is_active(void)
{
    query(23U, 0x3CU);
    acknowledge();
    const uint8_t stop[] = {0x68U, 4U, 0x13U, 0U, 0U, 0U};
    feed(stop, sizeof(stop));
    const size_t stopped = frame_count;
    advance();
    TEST_ASSERT_EQUAL_UINT32(stopped, frame_count);
    const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    feed(start, sizeof(start));
    acknowledge();
    advance();
    TEST_ASSERT_EQUAL_UINT8(10U, frames[frame_count - 1U][8]);
    TEST_ASSERT_FALSE(process_is_running(&iec104_send_temporary_faults));
}

/*** end of file ***/
