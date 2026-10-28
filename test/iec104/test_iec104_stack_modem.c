/**
 * @file test_iec104_stack_modem.c
 * @brief Real modem mapping, legacy address helpers and ACK-driven replay.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "iec104_modem_fixture.h"

#include <string.h>

TEST_SOURCE_FILE("iec104_asdu.c")
TEST_SOURCE_FILE("iec104_core.c")
TEST_SOURCE_FILE("iec104_server.c")
TEST_SOURCE_FILE("iec104_modem.c")
TEST_SOURCE_FILE("iec104_config.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("iec104_modem_fixture.c")

static uint8_t frame[255];
static uint32_t current_time;
static const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};

static void acknowledge_all(uint32_t time)
{
    uint8_t ack[] = {0x68U, 4U, 1U, 0U, 0U, 0U};
    iec104_write_u16(&ack[4], (uint16_t)(
        iec104_modem_fixture.modem.server.core.send_sequence << 1U));
    (void)iec104_fixture_feed(ack, sizeof(ack), time);
}

static void clear_frames(void)
{
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
    }
}

static void connect_station(void)
{
    (void)iec104_fixture_feed(start, sizeof(start), 0U);
    for (uint32_t time = 1U; time < 12U; time++)
    {
        acknowledge_all(time);
        iec104_fixture_poll(time);
    }
    acknowledge_all(12U);
    current_time = 12U;
    clear_frames();
}

static void query(uint8_t qoi)
{
    uint8_t command[] = {0x68U, 14U, 0U, 0U, 0U, 0U,
        100U, 1U, 6U, 0x3CU, 1U, 0U, 0U, 0U, 0U, qoi};
    iec104_core_t *core = &iec104_modem_fixture.modem.server.core;
    iec104_write_u16(&command[2],
                           (uint16_t)(core->receive_sequence << 1U));
    iec104_write_u16(&command[4],
                           (uint16_t)(core->send_sequence << 1U));
    current_time++;
    (void)iec104_fixture_feed(command, sizeof(command), current_time);
    for (size_t step = 0U; step < 200U; step++)
    {
        current_time++;
        acknowledge_all(current_time);
        iec104_fixture_poll(current_time);
        if (iec104_modem_fixture.modem.server.query_state == IEC104_QUERY_IDLE)
        {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Query did not terminate");
}

void setUp(void)
{
    current_time = 0U;
    TEST_ASSERT_TRUE(iec104_fixture_start(12U));
}

void tearDown(void)
{
}

void test_stack_modem_gi20_has_106_points_with_untimed_types(void)
{
    connect_station();
    query(20U);
    size_t count = 0U;
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
        if ((frame[2] & 1U) == 0U && frame[8] == 20U)
        {
            TEST_ASSERT_TRUE(frame[6] == 13U || frame[6] == 1U);
            TEST_ASSERT_EQUAL_UINT8(0x3CU, frame[9]);
            count += frame[7];
        }
    }
    TEST_ASSERT_EQUAL_size_t(106U, count);
}

void test_stack_modem_unsupported_gi_group_does_not_start_data_query(void)
{
    connect_station();
    query(25U);
    TEST_ASSERT_EQUAL_size_t(16U, iec104_fixture_pop(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(100U, frame[6]);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frame[8]);
    TEST_ASSERT_EQUAL_UINT8(25U, frame[15]);
    TEST_ASSERT_EQUAL_size_t(0U, iec104_fixture_pop(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE,
        iec104_modem_fixture.modem.server.query_state);
}

void test_stack_modem_gi21_float_and_original_time_full_value_vector(void)
{
    connect_station();
    query(21U);
    const uint8_t expected[] = {0U, 0U, 0x20U, 0xC0U, 0U,
        0x6BU, 0xE6U, 57U, 23U, 0xF9U, 10U, 26U};
    size_t count = 0U;
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
        if ((frame[2] & 1U) == 0U && frame[8] == 21U)
        {
            TEST_ASSERT_EQUAL_UINT8(36U, frame[6]);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &frame[15],
                sizeof(expected));
            count += frame[7];
        }
    }
    TEST_ASSERT_EQUAL_size_t(21U, count);
}

void test_stack_modem_gi22_battery_is_untimed_and_alarm_mask_is_specific(void)
{
    connect_station();
    iec104_modem_fixture.power.active_alarms = 1UL << 20U;
    query(22U);
    bool found = false;
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
        if ((frame[2] & 1U) == 0U && frame[8] == 22U && frame[6] == 1U)
        {
            found = true;
            TEST_ASSERT_EQUAL_UINT32(10000U, iec104_read_ioa(&frame[12]));
            TEST_ASSERT_EQUAL_UINT8(1U, frame[15]);
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_stack_modem_missing_rf_is_invalid_and_time_is_invalid(void)
{
    connect_station();
    iec104_modem_fixture.has_live = false;
    query(21U);
    bool found = false;
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
        if ((frame[2] & 1U) == 0U && frame[8] == 21U)
        {
            found = true;
            TEST_ASSERT_EQUAL_UINT8(0x80U, frame[19]);
            TEST_ASSERT_EQUAL_UINT8(0x80U, frame[22]);
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_stack_modem_stale_current_sets_iv_nt_preserves_source_time(void)
{
    connect_station();
    iec104_modem_fixture.phase.is_online = false;
    query(21U);
    bool found = false;
    while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
    {
        if ((frame[2] & 1U) == 0U && frame[8] == 21U)
        {
            found = true;
            TEST_ASSERT_EQUAL_UINT8(0xC0U, frame[19]);
            TEST_ASSERT_EQUAL_UINT8(57U, frame[22]);
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_stack_modem_fault_lists_use_existing_ioa_helpers_and_units(void)
{
    connect_station();
    for (uint8_t qoi = 23U; qoi <= 24U; qoi++)
    {
        query(qoi);
        size_t count = 0U;
        while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
        {
            if (((frame[2] & 1U) == 0U) && (frame[8] == qoi))
            {
                count += frame[7];
                if (frame[6] == 36U)
                {
                    uint32_t base = qoi == 23U ? 2000U : 5000U;
                    TEST_ASSERT_EQUAL_UINT32(base,
                        iec104_read_ioa(&frame[12]));
                    const uint8_t amps[] = {0U, 0U, 0x48U, 0x41U};
                    TEST_ASSERT_EQUAL_UINT8_ARRAY(amps, &frame[15],
                        sizeof(amps));
                    const uint8_t duration[] = {0U, 0x20U, 0xF1U, 0x47U};
                    TEST_ASSERT_EQUAL_UINT8_ARRAY(duration, &frame[30], 4U);
                }
            }
        }
        TEST_ASSERT_EQUAL_size_t(4U, count);
    }
}

void test_stack_modem_fault_storage_failure_closes_without_act_term(void)
{
    connect_station();
    iec104_modem_fixture.storage_failure = true;
    uint8_t command[] = {0x68U, 14U, 0U, 0U, 0U, 0U,
        100U, 1U, 6U, 0U, 1U, 0U, 0U, 0U, 0U, 23U};
    iec104_write_u16(&command[4], (uint16_t)(
        iec104_modem_fixture.modem.server.core.send_sequence << 1U));
    (void)iec104_fixture_feed(command, sizeof(command), 20U);
    iec104_fixture_poll(21U);
    TEST_ASSERT_EQUAL_UINT32(IEC104_CORE_CLOSED, iec104_fixture_state());
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_modem_fixture.close_count);
}

void test_stack_modem_live_event_bypasses_replay_fallback(void)
{
    connect_station();
    iec104_fixture_event(IEC104_EVENT_TRIP_FAILURE);
    iec104_modem_notify_event(&iec104_modem_fixture.modem);
    iec104_fixture_poll(30U);
    TEST_ASSERT_TRUE(iec104_modem_fixture.modem.replay_waiting);
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_fixture_marked());
    acknowledge_all(31U);
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_fixture_marked());
}

void test_stack_modem_periodic_covers_four_categories_with_k_one(void)
{
    TEST_ASSERT_TRUE(iec104_fixture_start(1U));
    iec104_modem_fixture.modem.period_ms = 1000U;
    connect_station();
    size_t count = 0U;
    for (uint32_t time = 1001U; time < 1030U; time++)
    {
        acknowledge_all(time);
        iec104_fixture_poll(time);
        while (iec104_fixture_pop(frame, sizeof(frame)) != 0U)
        {
            if ((frame[2] & 1U) == 0U && frame[8] == 3U)
            {
                count += frame[7];
            }
        }
    }
    TEST_ASSERT_EQUAL_size_t(84U, count);
    TEST_ASSERT_FALSE(iec104_modem_fixture.modem.periodic_pending);
}

void test_stack_modem_disabled_ioas_do_not_fake_journal_delivery(void)
{
    connect_station();
    iec104_fixture_event(IEC104_EVENT_TRIP_FAILURE);
    iec104_modem_fixture.breaker.line[0].iec104.in_use = 0U;
    iec104_modem_notify_event(&iec104_modem_fixture.modem);
    iec104_fixture_poll(30U);
    acknowledge_all(31U);
    TEST_ASSERT_TRUE(iec104_modem_fixture.has_event);
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_fixture_marked());
    TEST_ASSERT_FALSE(iec104_modem_fixture.modem.replay_waiting);
}

void test_stack_modem_replay_k_one_two_frames_only_final_ack_marks_sent(void)
{
    TEST_ASSERT_TRUE(iec104_fixture_start(1U));
    connect_station();
    iec104_fixture_event(IEC104_EVENT_FAULT);
    iec104_fixture_poll(15001U);
    TEST_ASSERT_EQUAL_UINT8(1U, iec104_modem_fixture.modem.replay_stage);
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_fixture_marked());
    iec104_fixture_poll(15002U);
    TEST_ASSERT_EQUAL_UINT8(1U, iec104_modem_fixture.modem.replay_stage);
    acknowledge_all(15003U);
    iec104_fixture_poll(15003U);
    TEST_ASSERT_TRUE(iec104_modem_fixture.modem.replay_waiting);
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_fixture_marked());
    acknowledge_all(15004U);
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_fixture_marked());
}

void test_stack_modem_disconnect_between_replay_frames_preserves_journal(void)
{
    TEST_ASSERT_TRUE(iec104_fixture_start(1U));
    connect_station();
    iec104_fixture_event(IEC104_EVENT_FAULT);
    iec104_fixture_poll(15001U);
    iec104_fixture_close();
    TEST_ASSERT_TRUE(iec104_modem_fixture.has_event);
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_fixture_marked());
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_modem_fixture.sync_count);
}

void test_stack_modem_alarm_replay_uses_single_frame_and_ack(void)
{
    connect_station();
    iec104_fixture_event(IEC104_EVENT_TRIP_FAILURE);
    iec104_fixture_poll(15001U);
    TEST_ASSERT_TRUE(iec104_modem_fixture.modem.replay_waiting);
    TEST_ASSERT_EQUAL_size_t(23U, iec104_fixture_pop(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(30U, frame[6]);
    TEST_ASSERT_EQUAL_UINT32(500U, iec104_read_ioa(&frame[12]));
    acknowledge_all(15002U);
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_fixture_marked());
}

void test_stack_modem_battery_change_only_and_stale_alarm_iv(void)
{
    connect_station();
    iec104_modem_fixture.power.active_alarms = 1UL << 6U;
    iec104_fixture_poll(30U);
    TEST_ASSERT_EQUAL_size_t(0U, iec104_fixture_pop(frame, sizeof(frame)));
    iec104_modem_fixture.power.valid_fields = 0U;
    iec104_fixture_poll(31U);
    TEST_ASSERT_EQUAL_size_t(16U, iec104_fixture_pop(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT8(0x80U, frame[15]);
    iec104_fixture_poll(32U);
    TEST_ASSERT_EQUAL_size_t(0U, iec104_fixture_pop(frame, sizeof(frame)));
}

void test_stack_modem_clock_uses_existing_rtc_sync_service(void)
{
    connect_station();
    uint8_t command[] = {0x68U, 20U, 0U, 0U, 0U, 0U,
        103U, 1U, 6U, 0U, 1U, 0U, 0U, 0U, 0U,
        0x6BU, 0xE6U, 57U, 23U, 0xF9U, 10U, 26U};
    iec104_write_u16(&command[4], (uint16_t)(
        iec104_modem_fixture.modem.server.core.send_sequence << 1U));
    (void)iec104_fixture_feed(command, sizeof(command), 20U);
    TEST_ASSERT_EQUAL_UINT32(1U, iec104_fixture_clock_count());
    TEST_ASSERT_EQUAL_UINT16(987U, iec104_modem_fixture.clock.millisec);
    TEST_ASSERT_EQUAL_UINT8(58U, iec104_modem_fixture.clock.second);
}
/*** end of file ***/
