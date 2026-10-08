/*
 * test_iec104_replay_scenario.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real Contiki replay process, mocked storage and transport boundaries.
 */
#include "unity.h"
#include "stm32u3xx_hal.h"
#include "iec104_replay.h"
#include "contiki.h"
#include "sys/pt-sem.h"
#include "mock_iec104.h"
#include "mock_iec104_event_log.h"
#include "mock_fault_log.h"

TEST_SOURCE_FILE("contiki_replay_scheduler.c")

struct pt_sem iec104_tx_sem;
PROCESS(iec104_send_temporary_faults, "GI temporary boundary");
PROCESS(iec104_send_permanent_faults, "GI permanent boundary");

PROCESS_THREAD(iec104_send_temporary_faults, ev, data)
{
    (void)ev;
    (void)data;
    PROCESS_BEGIN();
    PROCESS_END();
}

PROCESS_THREAD(iec104_send_permanent_faults, ev, data)
{
    (void)ev;
    (void)data;
    PROCESS_BEGIN();
    PROCESS_END();
}

static clock_time_t now;
static uint16_t unsent;
static uint8_t delivered[2];
static size_t delivered_count;
static bool reject_record;
static bool reject_snapshot;
static size_t snapshots;

clock_time_t clock_time(void)
{
    return now;
}

static uint16_t get_unsent(int call_count)
{
    (void)call_count;
    return unsent;
}

static bool read_record(iec104_event_record_t *record, uint16_t *seq,
                        int call_count)
{
    (void)call_count;
    *record = (iec104_event_record_t)
    {
        .kind = (2U == unsent) ? IEC104_EVENT_TRIP_FAILURE : IEC104_EVENT_FAULT
    };
    *seq = unsent;
    return (0U != unsent);
}

static bool send_record(const iec104_event_record_t *record, int call_count)
{
    (void)call_count;
    if (reject_record)
    {
        reject_record = false;
        return false;
    }
    TEST_ASSERT_TRUE(2U > delivered_count);
    delivered[delivered_count++] = record->kind;
    return true;
}

static void mark_sent(uint16_t seq, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT16(unsent, seq);
    unsent--;
}

static bool send_snapshot(cause_of_transmission_t cause, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_INT(COT_SPONTANEOUS, cause);
    TEST_ASSERT_EQUAL_UINT16(0U, unsent);
    snapshots++;
    if (reject_snapshot)
    {
        reject_snapshot = false;
        return false;
    }
    return true;
}

static void advance(clock_time_t ticks)
{
    now += ticks;
    etimer_request_poll();
    for (size_t step = 0U; 100U > step; step++)
    {
        if (0 == process_run())
        {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Replay scheduler did not settle");
}

void setUp(void)
{
    now = 0U;
    unsent = 2U;
    delivered_count = 0U;
    snapshots = 0U;
    reject_record = true;
    reject_snapshot = true;
    process_init();
    process_start(&etimer_process, NULL);
    PT_SEM_INIT(&iec104_tx_sem, 1U);
    iec104_is_link_active_IgnoreAndReturn(true);
    iec104_event_log_get_unsent_count_StubWithCallback(get_unsent);
    iec104_event_log_read_newest_unsent_StubWithCallback(read_record);
    iec104_event_log_mark_sent_StubWithCallback(mark_sent);
    iec104_event_log_sync_IgnoreAndReturn(0);
    iec104_emit_event_record_StubWithCallback(send_record);
    iec104_send_rf_communication_states_StubWithCallback(send_snapshot);
}

void tearDown(void)
{
}

void test_replay_retries_unsent_alarm_then_fault_and_restores_current_states(void)
{
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    TEST_ASSERT_EQUAL_UINT16(2U, unsent);
    advance(100U);
    TEST_ASSERT_EQUAL_UINT16(1U, unsent);
    advance(100U);
    TEST_ASSERT_EQUAL_UINT16(0U, unsent);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(1U, snapshots);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(2U, snapshots);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, delivered[0]);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_FAULT, delivered[1]);
}

/*** end of file ***/
