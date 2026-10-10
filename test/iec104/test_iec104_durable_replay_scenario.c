/*
 * test_iec104_durable_replay_scenario.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real replay scheduler and persistent log; fake NOR, NVRAM and transport.
 */
#include "unity.h"
#include "stm32u3xx_hal.h"
#include "iec104_replay.h"
#include "iec104_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "contiki.h"
#include "sys/pt-sem.h"
#include "mock_iec104.h"
#include "mock_fault_log.h"
#include "mock_w25qxx.h"
#include "mock_nvram.h"
#include "mock_shell.h"
#include "mock_bsp.h"
#include "mock_breaker.h"
#include "mock_utils.h"
#include <string.h>

TEST_SOURCE_FILE("contiki_replay_scheduler.c")
TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")

#include "../support/iec104_event_log_fixture.h"

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
static bool link_active;
static bool block_transport;
static bool flush_to_wire;
static bool waiting_for_ack;
static uint32_t rejections;
static uint8_t attempted[160U];
static size_t attempt_count;
static iec104_event_record_t delivered[4U];
static size_t delivered_count;
static size_t snapshots;

clock_time_t clock_time(void)
{
    return now;
}

static bool is_link_active(int call_count)
{
    (void)call_count;
    if (!link_active)
    {
        waiting_for_ack = false; /* Production core resets on socket close. */
    }
    return link_active;
}

static bool ack_pending(int call_count)
{
    (void)call_count;
    return waiting_for_ack;
}

static bool send_record(const iec104_event_record_t *record, int call_count)
{
    (void)call_count;
    TEST_ASSERT_TRUE(sizeof(attempted) > attempt_count);
    attempted[attempt_count++] = record->kind;
    if (block_transport || (0U != rejections))
    {
        if (0U != rejections)
        {
            rejections--;
        }
        return false;
    }
    if (!flush_to_wire)
    {
        /* Queue accept only: mirrors iec104_send() returning 0 when the
         * frame is copied into the RAM TX slots (iec104_process.c:146)
         * before any gsm_send_to_socket() flush (iec104_process.c:197).
         * delivered[] stays the wire-level ground truth. */
        return true;
    }
    TEST_ASSERT_TRUE(4U > delivered_count);
    delivered[delivered_count++] = *record;
    return true;
}

static bool send_tracked(const iec104_event_record_t *record, uint16_t seq,
                         iec104_event_ack_fn_t on_ack, int call_count)
{
    const bool accepted = send_record(record, call_count);
    if (accepted)
    {
        waiting_for_ack = !flush_to_wire;
        if (flush_to_wire)
        {
            on_ack(seq); /* Boundary model: successful transfer plus ACK. */
        }
    }
    return accepted;
}

static bool send_snapshot(cause_of_transmission_t cause, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_INT(COT_SPONTANEOUS, cause);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    snapshots++;
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

static void queue_fault_and_alarm(void)
{
    const fault_log_t fault = {.fault_duration_ms = UINT32_MAX};
    const iec104_alarm_record_t alarm = {.active = 1U,
        .feeder = 2U, .phase = 1U, .time = {.iv_bit = 1U}};
    TEST_ASSERT_TRUE(iec104_event_log_add(&fault, NULL));
    TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, NULL));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    reboot_fixture();
}

void setUp(void)
{
    reset_fixture();
    now = 0U;
    link_active = true;
    block_transport = false;
    flush_to_wire = true;
    waiting_for_ack = false;
    rejections = 0U;
    attempt_count = 0U;
    delivered_count = 0U;
    snapshots = 0U;
    process_init();
    process_start(&etimer_process, NULL);
    PT_SEM_INIT(&iec104_tx_sem, 1U);
    iec104_is_link_active_StubWithCallback(is_link_active);
    iec104_emit_event_record_tracked_StubWithCallback(send_tracked);
    iec104_event_ack_pending_StubWithCallback(ack_pending);
    iec104_send_rf_communication_states_StubWithCallback(send_snapshot);
}

void tearDown(void)
{
}

void test_rejected_send_retries_same_persisted_alarm_before_older_fault(void)
{
    queue_fault_and_alarm();
    rejections = 1U;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    TEST_ASSERT_EQUAL_size_t(0U, delivered_count);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(1U, delivered_count);
    advance(100U);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(2U, delivered_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, attempted[0]);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, attempted[1]);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_FAULT, attempted[2]);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
        delivered[1].payload.fault.fault_duration_ms);
    TEST_ASSERT_EQUAL_size_t(1U, snapshots);
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_persistent_transport_failure_stops_with_records_still_durable(void)
{
    queue_fault_and_alarm();
    block_transport = true;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    for (size_t step = 0U; 160U > step; step++)
    {
        advance(100U);
    }
    TEST_ASSERT_EQUAL_size_t(150U, attempt_count);
    TEST_ASSERT_EQUAL_size_t(0U, delivered_count);
    TEST_ASSERT_EQUAL_size_t(0U, snapshots);
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    block_transport = false;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    advance(100U);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(2U, delivered_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, delivered[0].kind);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_FAULT, delivered[1].kind);
}

void test_link_loss_preserves_replay_order_after_reconnect(void)
{
    queue_fault_and_alarm();
    block_transport = true;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    link_active = false;
    advance(100U);
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    link_active = true;
    block_transport = false;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    advance(100U);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(2U, delivered_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, delivered[0].kind);
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_FAULT, delivered[1].kind);
}

/* Review 2026-10-10: the old replay marked a record sent as soon
 * as the transport accepted it into the RAM TX slots. Socket close
 * discarded these slots before delivery. This boundary regression is
 * complemented by test_iec104_ack_delivery_scenario.c, which executes
 * the actual production slots, reset, ACK parser and persistent log. */
void test_queue_accept_without_wire_flush_survives_link_loss(void)
{
    queue_fault_and_alarm();
    flush_to_wire = false;   /* emit accepts into the queue only */
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    advance(100U);
    advance(100U);
    /* Only one record can await ACK; both remain durable. */
    TEST_ASSERT_EQUAL_size_t(1U, attempt_count);
    TEST_ASSERT_EQUAL_size_t(0U, delivered_count);
    /* the socket dies before any flush */
    link_active = false;
    advance(100U);
    reboot_fixture();
    /* durability contract: unflushed records must still be unsent */
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    flush_to_wire = true;
    link_active = true;
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    advance(100U);
    advance(100U);
    TEST_ASSERT_EQUAL_size_t(2U, delivered_count);
}

/*** end of file ***/
