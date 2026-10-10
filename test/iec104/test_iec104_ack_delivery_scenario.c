/*
 * test_iec104_ack_delivery_scenario.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real replay, IEC104 ACK parser, TX slots and persistent log integration.
 */

#include "unity.h"
#include "stm32u3xx_hal.h"
#include "iec104.h"
#include "iec104_replay.h"
#include "iec104_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "contiki.h"
#include "sys/pt-sem.h"
#include "mock_fault_log.h"
#include "mock_w25qxx.h"
#include "mock_nvram.h"
#include "mock_shell.h"
#include "mock_bsp.h"
#include "mock_breaker.h"
#include "mock_utils.h"
#include "mock_rf.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_socket.h"
#include "mock_iec104_application.h"
#include "mock_iec104_elog.h"
#include "mock_rtc.h"
#include "mock_time_service.h"
#include <string.h>

TEST_SOURCE_FILE("contiki_replay_scheduler.c")
TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")
TEST_SOURCE_FILE("iec104.c")
TEST_SOURCE_FILE("iec104_config.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("iec104_replay.c")
TEST_SOURCE_FILE("iec104_event_log.c")

#include "../support/iec104_event_log_fixture.h"
#include "../../Application/iec104_process.c"

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
static breaker_t breaker_state;
static iec104_config_t config;
static uint32_t gsm_submissions;
static uint8_t gsm_payload[1024];
static uint16_t gsm_length;

clock_time_t clock_time(void)
{
    return now;
}

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return now;
}

static const power_line_t *get_line(uint32_t index, int call_count)
{
    (void)call_count;
    return (MAX_POWER_LINE_COUNT > index) ? &breaker_state.line[index] : NULL;
}

static uint32_t submit_gsm(const void *data, uint16_t length, uint32_t socket,
                            bool close_after, bool crypto, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(GSM_TX_DIR_IEC104_SOCKET, socket);
    TEST_ASSERT_FALSE(close_after);
    TEST_ASSERT_FALSE(crypto);
    TEST_ASSERT_TRUE(sizeof(gsm_payload) >= length);
    (void)memcpy(gsm_payload, data, length);
    gsm_length = length;
    gsm_submissions++;
    return 0U;
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
    TEST_FAIL_MESSAGE("Scheduler did not settle");
}

static void acknowledge(uint16_t nr)
{
    const uint8_t frame[6] =
    {
        0x68U, 4U, 1U, 0U, (uint8_t)((nr << 1U) & 0xFEU),
        (uint8_t)(nr >> 7U)
    };
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
}

static void start_link(void)
{
    const uint8_t frame[6] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    iec104_tx_process();
    acknowledge(iec104_get_send_sn());
    gsm_submissions = 0U;
    gsm_length = 0U;
}

static void close_link(void)
{
    iec104_process_socket_closed_cb();
    process_exit(&iec104_replay_process);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    TEST_ASSERT_EQUAL_UINT16(0U, tx.pending);
}

static void add_records(bool alarm)
{
    const fault_log_t fault = {.fault_duration_ms = 12345U};
    TEST_ASSERT_TRUE(iec104_event_log_add(&fault, NULL));
    if (alarm)
    {
        const iec104_alarm_record_t entry = {.active = 1U,
            .feeder = 2U, .phase = 1U, .time = {.iv_bit = 1U}};
        TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&entry, NULL));
    }
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
}

void setUp(void)
{
    reset_fixture();
    now = 0U;
    (void)memset(&breaker_state, 0, sizeof(breaker_state));
    config = (iec104_config_t){.k_max = 12U, .w_max = 8U,
        .t1_max = 45U, .t2_max = 10U, .t3_max = 60U, .common_address = 1U};
    breaker_state.line[0].iec104.temporary_fault =
        iec104_make_ioa_3byte(100000U);
    breaker_state.line[2].iec104.trip_failed[1] =
        iec104_make_ioa_3byte(300001U);
    nvram_get_breaker_rw_IgnoreAndReturn(&breaker_state);
    nvram_get_iec104_config_rw_IgnoreAndReturn(&config);
    breaker_get_power_line_by_idx_StubWithCallback(get_line);
    breaker_get_active_powerline_count_IgnoreAndReturn(0U);
    gsm_get_tx_state_IgnoreAndReturn(GSM_TX_READY);
    gsm_send_to_socket_StubWithCallback(submit_gsm);
    gsm_socket_get_stats_IgnoreAndReturn(NULL);
    iec104_application_event_handler_Ignore();
    iec104_elog_disconnected_Ignore();
    process_init();
    process_start(&etimer_process, NULL);
    PT_SEM_INIT(&iec104_tx_sem, 1U);
    tx_reset();
    iec104_init(&(iec104_io_t){.send = iec104_send}, &config);
    start_link();
}

void tearDown(void)
{
    process_exit(&iec104_replay_process);
    process_exit(&iec104_replay_fallback_timer);
    iec104_reset();
}

void test_real_tx_queue_loss_keeps_records_durable_until_reconnected_ack(void)
{
    add_records(true);
    advance(500U);
    TEST_ASSERT_TRUE(0U < tx.pending);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_submissions);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    PT_SEM_INIT(&iec104_tx_sem, 1U);
    start_link();
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    iec104_tx_process();
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_submissions);
    TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, gsm_payload[6]);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    acknowledge(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    advance(100U);
    iec104_tx_process();
    TEST_ASSERT_EQUAL_UINT32(2U, gsm_submissions);
    TEST_ASSERT_EQUAL_UINT8(M_ME_TF_1, gsm_payload[6]);
    const uint16_t end = iec104_get_send_sn();
    acknowledge((uint16_t)(end - 1U));
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    acknowledge(end);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_gsm_acceptance_without_master_ack_does_not_consume_record(void)
{
    add_records(true);
    iec104_tx_process();
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_submissions);
    TEST_ASSERT_TRUE(0U < gsm_length);
    TEST_ASSERT_EQUAL_UINT16(0U, tx.pending);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
}

void test_partial_fault_ack_and_socket_loss_preserve_whole_record(void)
{
    add_records(false);
    iec104_tx_process();
    acknowledge((uint16_t)(iec104_get_send_sn() - 1U));
    TEST_ASSERT_TRUE(iec104_event_ack_pending());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
}

void test_new_record_during_pending_ack_cannot_be_consumed_by_old_ack(void)
{
    add_records(false);
    iec104_tx_process();
    const uint16_t old_end = iec104_get_send_sn();
    const iec104_alarm_record_t alarm = {.feeder = 2U, .phase = 1U,
                                         .active = 1U};
    TEST_ASSERT_TRUE(iec104_event_log_add_alarm(&alarm, NULL));
    TEST_ASSERT_EQUAL_INT(0, iec104_event_log_sync());
    acknowledge(old_end);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_event_log_get_unsent_count());
    advance(100U);
    iec104_tx_process();
    TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, gsm_payload[6]);
    acknowledge(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    advance(100U);
    iec104_tx_process();
    TEST_ASSERT_EQUAL_UINT8(M_ME_TF_1, gsm_payload[6]);
    acknowledge(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_failed_ack_state_sync_replays_record_after_reboot(void)
{
    add_records(false);
    iec104_tx_process();
    sync_fails = true;
    acknowledge(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
    sync_fails = false;
    PT_SEM_INIT(&iec104_tx_sem, 1U);
    start_link();
    iec104_replay_link_established();
    advance(15U * CLOCK_SECOND);
    iec104_tx_process();
    acknowledge(iec104_get_send_sn());
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_event_log_get_unsent_count());
}

void test_invalid_future_ack_preserves_persistent_record(void)
{
    add_records(false);
    iec104_tx_process();
    acknowledge((uint16_t)(iec104_get_send_sn() + 1U));
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
}

void test_existing_t1_timeout_preserves_unacknowledged_record(void)
{
    add_records(false);
    iec104_tx_process();
    for (uint8_t second = 0U; 46U > second; second++)
    {
        iec104_tick();
    }
    libiec104_poll();
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    close_link();
    reboot_fixture();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_event_log_get_unsent_count());
}

/*** end of file ***/
