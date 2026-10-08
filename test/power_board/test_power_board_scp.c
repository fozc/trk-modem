/*
 * test_power_board_scp.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify SCP power quality, alarm correction and session deduplication.
 */
#include "unity.h"
#include "power_board_scp.h"
#include "mock_power_board_control.h"
#include "mock_elog.h"
#include "mock_shell.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")

static size_t log_calls;
static uint8_t log_info[16];
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return 0U;
}

static void capture_log(elog_code_t code, elog_level_t level,
                         const void *data, size_t length, int call_count)
{
    (void)level;
    (void)call_count;
    TEST_ASSERT_EQUAL_INT(ELOG_PWR_SCP_ALARM, code);
    TEST_ASSERT_EQUAL_UINT32(16U, length);
    (void)memcpy(log_info, data, length);
    log_calls++;
}

static power_board_snapshot_t read_snapshot(uint32_t tick)
{
    power_board_snapshot_t out;

    TEST_ASSERT_TRUE(power_board_get_snapshot(tick, &out));
    return out;
}

static rf_scp_message_t summary(void)
{
    return (rf_scp_message_t)
    {
        .cmd = RF_SCP_CMD_PWR_SUMMARY, .type = SCP_TYPE_SET,
        .body.power =
        {
            .flags = 0x83U, .flags2 = 5U, .soc_flags = 3U,
            .source = 1U, .session = 7U, .charge_phase = 2U,
            .pv_mv = 24000U, .battery_mv = 12400U, .battery_ma = -321,
            .soc_tenths = -125, .soh_percent = 96U,
            .battery_temperature = 24, .board_temperature = 31,
            .capacity_ah = 12U, .charge_rate_permille = 100U
        }
    };
}

static rf_scp_message_t alarm(uint8_t seq, uint8_t code,
                              uint8_t state, uint8_t value)
{
    rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_PWR_ALARM, .type = SCP_TYPE_SET
    };

    message.body.alarm.seq = seq;
    message.body.alarm.code = code;
    message.body.alarm.state = state;
    message.body.alarm.value1 = value;
    return message;
}

void setUp(void)
{
    power_board_control_init_Ignore();
    shell_register_command_IgnoreAndReturn(0);
    power_board_scp_init();
    log_calls = 0U;
    elog_add_StubWithCallback(capture_log);
}

void tearDown(void)
{
}

void test_capture_summaries_and_alarms_enter_real_model(void)
{
    const size_t count = sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]);
    size_t checked = 0U;

    for (size_t index = 0U; index < count; index++)
    {
        const uint8_t *frame = rf_scp_vectors[index].logical;

        if ((RF_SCP_ADDR_RTU == frame[0]) &&
            ((RF_SCP_CMD_PWR_SUMMARY == frame[3]) ||
             (RF_SCP_CMD_PWR_ALARM == frame[3])))
        {
            scp_packet_t packet =
            {
                .dst = frame[0], .src = frame[1], .type = frame[2],
                .cmd = frame[3], .seq = frame[4], .data_len = frame[5]
            };
            rf_scp_message_t message;

            (void)memcpy(packet.data, &frame[7], packet.data_len);
            TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                                  rf_scp_decode_message(&packet, &message));
            if (RF_SCP_CMD_PWR_SUMMARY == packet.cmd)
            {
                TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
                power_board_snapshot_t out = read_snapshot(0U);

                TEST_ASSERT_EQUAL_INT16(message.body.power.battery_ma,
                                        out.summary.battery_ma);
                TEST_ASSERT_EQUAL_INT16(message.body.power.soc_tenths,
                                        out.summary.soc_tenths);
            }
            else
            {
                TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
            }
            checked++;
        }
    }
    TEST_ASSERT_TRUE(60U < checked);
}

void test_stale_charger_keeps_adc_but_rejects_charge_power_and_soc(void)
{
    rf_scp_message_t message = summary();

    message.body.power.flags |= 4U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_snapshot_t out = read_snapshot(0U);

    TEST_ASSERT_TRUE(0U != (out.valid_fields & POWER_VALID_ADC));
    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields &
        (POWER_VALID_CHARGER | POWER_VALID_POWER | POWER_VALID_SOC));
    TEST_ASSERT_EQUAL_INT16(-321, out.summary.battery_ma);
}


void test_stale_board_adc_rejects_voltages_and_temperature(void)
{
    rf_scp_message_t message = summary();

    message.body.power.flags2 |= 0x80U;
    message.body.power.source = 0xFFU;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_snapshot_t out = read_snapshot(0U);

    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields &
        (POWER_VALID_ADC | POWER_VALID_BOARD_TEMP | POWER_VALID_SOURCE));
    TEST_ASSERT_TRUE(0U != (out.valid_fields & POWER_VALID_CHARGER));
}

void test_relative_negative_soc_is_valid_and_not_absolute(void)
{
    rf_scp_message_t message = summary();

    message.body.power.soc_flags = 2U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_snapshot_t out = read_snapshot(0U);

    TEST_ASSERT_EQUAL_INT16(-125, out.summary.soc_tenths);
    TEST_ASSERT_TRUE(0U != (out.valid_fields & POWER_VALID_SOC));
    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields & POWER_SOC_ABSOLUTE);
}

void test_summary_expiry_and_tick_wrap_do_not_refresh_on_alarm_frames(void)
{
    rf_scp_message_t message = summary();

    TEST_ASSERT_TRUE(power_board_handle_summary(&message, UINT32_MAX - 999U));
    TEST_ASSERT_TRUE(0U != (read_snapshot(28999U).valid_fields &
                            POWER_VALID_SUMMARY));
    message = alarm(1U, 4U, 1U, 0U);
    message.body.alarm.active = 16U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 29000U));
    power_board_snapshot_t out = read_snapshot(29000U);

    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields & POWER_VALID_SUMMARY);
    TEST_ASSERT_TRUE(0U != (out.valid_fields & POWER_VALID_ALARMS));
}

void test_unavailable_power_and_temperature_are_independently_invalid(void)
{
    rf_scp_message_t message = summary();

    message.body.power.flags &= (uint8_t)~2U;
    message.body.power.battery_temperature = INT8_MIN;
    message.body.power.board_temperature = INT8_MIN;
    message.body.power.capacity_ah = 7U;
    message.body.power.flags2 = 1U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_snapshot_t out = read_snapshot(0U);

    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields &
        (POWER_VALID_POWER | POWER_VALID_BATTERY_TEMP |
         POWER_VALID_BOARD_TEMP | POWER_VALID_CAPACITY));
}

void test_summary_repairs_a_lost_alarm_edge_and_logs_full_32_bit_masks(void)
{
    rf_scp_message_t message = summary();

    message.body.power.active_alarms = (1UL << 18U);
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    TEST_ASSERT_EQUAL_HEX8(4U, log_info[10]);
    TEST_ASSERT_EQUAL_HEX32(1UL << 18U, read_snapshot(0U).active_alarms);
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    message.body.power.active_alarms = 0U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 2U));
    TEST_ASSERT_EQUAL_UINT32(2U, log_calls);
}

void test_same_session_last_gasp_updates_cause_without_a_second_episode(void)
{
    rf_scp_message_t message = alarm(1U, 0x20U, 2U, 7U);

    message.body.alarm.value0 = 2U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    message.body.alarm.seq = 2U;
    message.body.alarm.value0 = 1U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 2U));
    TEST_ASSERT_EQUAL_UINT8(7U, read_snapshot(2U).last_gasp_session);
    TEST_ASSERT_EQUAL_UINT8(1U, read_snapshot(2U).last_gasp_cause);
    message = alarm(3U, 0x21U, 2U, 7U);
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 22000U));
    TEST_ASSERT_TRUE(read_snapshot(22000U).last_gasp_cancelled);
}

void test_mh_restart_invalidates_measurements_but_preserves_power_session(void)
{
    rf_scp_message_t message = summary();

    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    message = alarm(1U, 0x20U, 2U, 7U);
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    power_board_scp_hub_restarted();
    power_board_snapshot_t out = read_snapshot(1U);

    TEST_ASSERT_EQUAL_UINT16(0U, out.valid_fields);
    TEST_ASSERT_EQUAL_UINT16(12400U, out.summary.battery_mv);
    TEST_ASSERT_EQUAL_UINT8(7U, out.last_gasp_session);
    TEST_ASSERT_TRUE(out.has_last_gasp);
}

void test_invalid_message_preserves_valid_state_and_null_output_is_rejected(
    void)
{
    rf_scp_message_t message = summary();

    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    message.type = SCP_TYPE_GET;
    TEST_ASSERT_FALSE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_FALSE(power_board_handle_summary(NULL, 1U));
    TEST_ASSERT_FALSE(power_board_handle_alarm(&message, 1U));
    TEST_ASSERT_FALSE(power_board_get_snapshot(0U, NULL));
    TEST_ASSERT_EQUAL_UINT16(12400U, read_snapshot(0U).summary.battery_mv);
}

void test_last_gasp_repeat_after_other_alarm_cannot_restore_old_alarm_mask(void)
{
    rf_scp_message_t gasp = alarm(1U, 0x20U, 2U, 7U);

    gasp.body.alarm.value0 = 1U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&gasp, 0U));
    rf_scp_message_t edge = alarm(2U, 4U, 1U, 0U);

    edge.body.alarm.active = 16U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&edge, 1U));
    const size_t count = log_calls;

    TEST_ASSERT_TRUE(power_board_handle_alarm(&gasp, 2U));
    TEST_ASSERT_EQUAL_UINT32(count, log_calls);
    TEST_ASSERT_EQUAL_UINT32(16U, read_snapshot(2U).active_alarms);
}

void test_reset_clears_full_mask_and_is_recorded_once(void)
{
    rf_scp_message_t edge = alarm(1U, 18U, 1U, 0U);

    edge.body.alarm.active = 1U << 18U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&edge, 0U));
    rf_scp_message_t reset = alarm(2U, 0xFFU, 3U, 0U);

    TEST_ASSERT_TRUE(power_board_handle_alarm(&reset, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, read_snapshot(1U).active_alarms);
    const size_t count = log_calls;

    TEST_ASSERT_TRUE(power_board_handle_alarm(&reset, 2U));
    TEST_ASSERT_EQUAL_UINT32(count, log_calls);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, log_info[1]);
}

void test_periodic_expiry_prevents_old_data_becoming_fresh_after_full_tick_cycle(
    void)
{
    rf_scp_message_t message = summary();

    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_scp_process(30000U);
    TEST_ASSERT_EQUAL_UINT16(0U, read_snapshot(0U).valid_fields);
}

/*** end of file ***/
