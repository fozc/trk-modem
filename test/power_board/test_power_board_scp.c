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
#include "shell_log_enabled.h"
#include "console_log_enabled.h"
#define CSLOG_MODULE LOG_MOD_RF
#include "power_board_scp.h"
#include "mock_power_board_control.h"
#include "mock_elog.h"
#include "mock_shell.h"
#include "mock_rtc.h"
#include "mock_console_logger.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("xprintf.c")

/* Compile production with the real CSLOG macros enabled. */
#include "power_board_scp.c"

static size_t log_calls;
static elog_level_t last_log_level;
static uint8_t log_info[16];
static log_lvl_t console_level;
static char console_output[4096];
static size_t console_length;

static void capture_shell(int character, int call_count)
{
    (void)call_count;
    if ((sizeof(console_output) - 1U) > console_length)
    {
        console_output[console_length++] = (char)character;
        console_output[console_length] = '\0';
    }
}

static void capture_console(int character)
{
    if ((sizeof(console_output) - 1U) > console_length)
    {
        console_output[console_length] = (char)character;
        console_length++;
        console_output[console_length] = '\0';
    }
}

static bool console_level_enabled(log_mod_t module, log_lvl_t min_level,
                                  int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_INT(LOG_MOD_RF, module);
    return console_level >= min_level;
}
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return 0U;
}

static void capture_log(elog_code_t code, elog_level_t level,
                         const void *data, size_t length, int call_count)
{
    last_log_level = level;
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
    console_level = LOG_LVL_NORMAL;
    console_length = 0U;
    console_output[0] = '\0';
    xdev_out(capture_console);
    shell_putchr_StubWithCallback(capture_shell);
    console_logger_level_enabled_StubWithCallback(console_level_enabled);
    rtc_print_now_Ignore();
    power_board_control_init_Ignore();
    shell_register_command_IgnoreAndReturn(0);
    power_board_scp_init();
    log_calls = 0U;
    elog_add_StubWithCallback(capture_log);
}

void tearDown(void)
{
    xdev_out(NULL);
}

void test_lifetime_summary_replaces_old_values_and_shell_marks_unknown_or_stale(void)
{
    char *args[] = {"pwrboard", "show"};
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, read_snapshot(0U).summary.equivalent_cycles);
    TEST_ASSERT_EQUAL_INT(0, power_board_scp_shell(2, args));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "Remaining cycles=unknown"));
    rf_scp_message_t message = summary();
    message.body.power.equivalent_cycles = 0U;
    message.body.power.remaining_cycles = 2500U;
    message.body.power.remaining_years_x10 = 120U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    console_length = 0U;
    console_output[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, power_board_scp_shell(2, args));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "EFC (cycles)=0"));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "Remaining cycles=2500"));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "Remaining life (0.1 year)=120"));
    message.body.power.equivalent_cycles = UINT16_MAX;
    message.body.power.remaining_cycles = UINT16_MAX;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, read_snapshot(0U).summary.remaining_cycles);
    TEST_ASSERT_EQUAL_UINT16(120U, read_snapshot(0U).summary.remaining_years_x10);
    power_board_scp_hub_restarted();
    console_length = 0U;
    console_output[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, power_board_scp_shell(2, args));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "Remaining life (0.1 year)=unknown"));
}

void test_summary_raw_capture_keeps_last_packet_with_reception_time(void)
{
    power_summary_raw_t out;
    uint8_t body[POWER_SUMMARY_RAW_MAX];

    for (size_t index = 0U; index < sizeof(body); index++)
    {
        body[index] = (uint8_t)index;
    }

    rtc_get_second_ExpectAndReturn(31U);
    rtc_get_minute_ExpectAndReturn(24U);
    rtc_get_hour_ExpectAndReturn(16U);
    rtc_get_day_ExpectAndReturn(5U);
    rtc_get_month_ExpectAndReturn(10U);
    rtc_get_year_ExpectAndReturn(26U);
    power_board_capture_summary_raw(body, POWER_SUMMARY_RAW_MAX, 1000U);

    TEST_ASSERT_TRUE(power_board_get_summary_raw(&out));
    TEST_ASSERT_EQUAL_UINT8(POWER_SUMMARY_RAW_MAX, out.len);
    TEST_ASSERT_EQUAL_UINT32(1000U, out.tick_ms);
    TEST_ASSERT_EQUAL_UINT8(16U, out.hour);
    TEST_ASSERT_EQUAL_UINT8(26U, out.year);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, out.raw, POWER_SUMMARY_RAW_MAX);

    /* Asiri uzun paket reddedilir; onceki kayit bozulmaz. */
    power_board_capture_summary_raw(body, POWER_SUMMARY_RAW_MAX + 1U, 2000U);
    TEST_ASSERT_TRUE(power_board_get_summary_raw(&out));
    TEST_ASSERT_EQUAL_UINT32(1000U, out.tick_ms);
    TEST_ASSERT_EQUAL_UINT8(POWER_SUMMARY_RAW_MAX, out.len);
}

void test_alarm_names_cover_spec_codes_and_reject_out_of_range(void)
{
    TEST_ASSERT_EQUAL_STRING("AKU_COK_DUSUK", power_board_alarm_name(0U));
    TEST_ASSERT_EQUAL_STRING("KART_YUVA_KAPALI", power_board_alarm_name(19U));
    TEST_ASSERT_EQUAL_STRING("AKU_YOK", power_board_alarm_name(20U));
    TEST_ASSERT_NULL(power_board_alarm_name(21U));
    TEST_ASSERT_NULL(power_board_alarm_name(255U));
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

void test_telemetry_loss_escalation_is_recorded_as_high_even_when_active_mask_is_unchanged(void)
{
    rf_scp_message_t message = alarm(1U, 13U, 1U, 0U);
    message.body.alarm.value0 = 1U;
    message.body.alarm.active = UINT32_C(1) << 13U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    TEST_ASSERT_EQUAL_INT(ELOG_LEVEL_WARN, last_log_level);
    message.body.alarm.seq = 2U;
    message.body.alarm.value0 = 2U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 10000U));
    TEST_ASSERT_EQUAL_UINT32(2U, log_calls);
    TEST_ASSERT_EQUAL_INT(ELOG_LEVEL_ERROR, last_log_level);
    TEST_ASSERT_EQUAL_UINT8(2U, log_info[6]);
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 10001U));
    TEST_ASSERT_EQUAL_UINT32(2U, log_calls);
    message.body.alarm.seq = 3U;
    message.body.alarm.state = 0U;
    message.body.alarm.value0 = 0U;
    message.body.alarm.active = 0U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 11000U));
    TEST_ASSERT_EQUAL_INT(ELOG_LEVEL_INFO, last_log_level);
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
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
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

void test_r2_unknown_soh_is_retained_with_invalid_soh_quality(void)
{
    rf_scp_message_t message = summary();
    message.body.power.soh_percent = UINT8_MAX;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    power_board_snapshot_t out = read_snapshot(0U);
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, out.summary.soh_percent);
    TEST_ASSERT_EQUAL_HEX16(0U, out.valid_fields & POWER_VALID_SOH);
    TEST_ASSERT_NOT_EQUAL(0U, out.valid_fields & POWER_VALID_SUMMARY);
}

void test_console_summary_announces_only_changed_alarm_bits(void)
{
    rf_scp_message_t message = summary();
    message.body.power.active_alarms = 16U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    TEST_ASSERT_NOT_NULL(strstr(console_output,
        "[PB] alarm basladi: AKU_DUSUK (kod=4 maske=0x00000010)"));
    const size_t previous_length = console_length;
    message.body.power.seq++;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);

    console_length = 0U;
    console_output[0] = '\0';
    message.body.power.active_alarms = 1U << 18U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 2U));
    TEST_ASSERT_NOT_NULL(strstr(console_output,
        "[PB] alarm kapandi: AKU_DUSUK (kod=4 maske=0x00040000)"));
    TEST_ASSERT_NOT_NULL(strstr(console_output,
        "[PB] alarm basladi: SOC_DUSUK (kod=18 maske=0x00040000)"));
}

void test_console_alarm_and_summary_share_last_announced_mask(void)
{
    rf_scp_message_t message = alarm(1U, 4U, 1U, 0U);
    message.body.alarm.active = 16U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm basladi: AKU_DUSUK"));
    const size_t previous_length = console_length;
    message = summary();
    message.body.power.active_alarms = 16U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    message = alarm(2U, 4U, 1U, 0U);
    message.body.alarm.active = 16U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 2U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);
    message = alarm(3U, 4U, 0U, 0U);
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 3U));
    TEST_ASSERT_NOT_NULL(strstr(console_output,
        "[PB] alarm kapandi: AKU_DUSUK (kod=4 maske=0x00000000)"));
    const size_t cleared_length = console_length;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 4U));
    message = summary();
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 5U));
    TEST_ASSERT_EQUAL_size_t(cleared_length, console_length);
}

void test_console_reset_announces_all_closures_once(void)
{
    rf_scp_message_t message = summary();
    message.body.power.active_alarms = (1U << 4U) | (1U << 18U);
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    console_length = 0U;
    console_output[0] = '\0';
    message = alarm(1U, 0xFFU, 3U, 0U);
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 1U));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm kapandi: AKU_DUSUK"));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm kapandi: SOC_DUSUK"));
    const size_t previous_length = console_length;
    message.body.alarm.seq++;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 2U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);
}

void test_console_mh_restart_does_not_repeat_unchanged_alarms(void)
{
    rf_scp_message_t message = summary();
    message.body.power.active_alarms = 16U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    const size_t previous_length = console_length;
    power_board_scp_hub_restarted();
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);
    power_board_scp_hub_restarted();
    message.body.power.active_alarms = 0U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 2U));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm kapandi: AKU_DUSUK"));
}

void test_console_off_reconciles_with_last_printed_state_when_enabled(void)
{
    rf_scp_message_t message = summary();
    message.body.power.active_alarms = 16U;
    console_level = LOG_LVL_OFF;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    TEST_ASSERT_EQUAL_STRING("", console_output);
    TEST_ASSERT_EQUAL_UINT32(16U, read_snapshot(0U).active_alarms);
    console_level = LOG_LVL_NORMAL;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm basladi: AKU_DUSUK"));
    const size_t previous_length = console_length;
    console_level = LOG_LVL_OFF;
    message.body.power.active_alarms = 0U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 2U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);
    console_level = LOG_LVL_NORMAL;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 3U));
    TEST_ASSERT_NOT_NULL(strstr(console_output, "alarm kapandi: AKU_DUSUK"));
}

void test_console_invalid_or_duplicate_alarm_preserves_announced_state(void)
{
    rf_scp_message_t message = alarm(1U, 4U, 1U, 0U);
    message.body.alarm.active = 16U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    const size_t previous_length = console_length;
    message.body.alarm.active = 0U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 1U));
    message.body.alarm.seq++;
    message.body.alarm.state = 4U;
    TEST_ASSERT_FALSE(power_board_handle_alarm(&message, 2U));
    TEST_ASSERT_EQUAL_size_t(previous_length, console_length);
    TEST_ASSERT_EQUAL_UINT32(16U, read_snapshot(2U).active_alarms);
}

void test_r23_battery_absent_code_20_is_a_level_alarm_not_last_gasp(void)
{
    rf_scp_message_t message = alarm(1U, 20U, 1U, 0U);
    message.body.alarm.active = 1UL << 20U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 0U));
    TEST_ASSERT_EQUAL_HEX32(1UL << 20U, read_snapshot(0U).active_alarms);
    TEST_ASSERT_FALSE(read_snapshot(0U).has_last_gasp);
    TEST_ASSERT_EQUAL_INT(ELOG_LEVEL_WARN, last_log_level);
    TEST_ASSERT_NOT_NULL(strstr(console_output, "AKU_YOK"));
    message.body.alarm.seq++;
    message.body.alarm.state = 0U;
    message.body.alarm.active = 0U;
    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, read_snapshot(1U).active_alarms);
}

void test_r23_soc_drop_and_code_18_are_preserved_without_smoothing(void)
{
    rf_scp_message_t message = summary();
    message.body.power.soc_tenths = 800;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 0U));
    message.body.power.soc_tenths = 100;
    message.body.power.active_alarms = 1UL << 18U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, 1U));
    TEST_ASSERT_EQUAL_INT16(100, read_snapshot(1U).summary.soc_tenths);
    TEST_ASSERT_EQUAL_HEX32(1UL << 18U, read_snapshot(1U).active_alarms);
}

/*** end of file ***/
