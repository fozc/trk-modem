/*
 * test_power_board_control.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify production CFG2/command service with the real codec and captures.
 */

#include "unity.h"
#include "power_board_control.h"
#include "scp_endian.h"
#include "mock_rf_comm.h"
#include "mock_power_board_scp.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("power_board_control_shell.c")

static uint32_t tick;
static bool free_transport;
static bool ready;
static bool accept_request;
static size_t request_count;
static scp_packet_t requests[12];
static scp_cmd_done_fn_t callback;
static power_board_snapshot_t power;

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

static bool is_free(int call_count)
{
    (void)call_count;
    return free_transport;
}

static bool is_ready(int call_count)
{
    (void)call_count;
    return ready;
}

static bool get_power(uint32_t now_ms, power_board_snapshot_t *out,
                       int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(tick, now_ms);
    *out = power;
    return true;
}

static bool send_request(const scp_packet_t *request,
                         scp_cmd_done_fn_t done, int call_count)
{
    scp_packet_t packet;

    (void)call_count;
    if (!accept_request)
    {
        return false;
    }
    TEST_ASSERT_TRUE(12U > request_count);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_build_packet(request, &packet));
    requests[request_count++] = packet;
    callback = done;
    free_transport = false;
    return true;
}

static power_board_control_status_t control_status(void)
{
    power_board_control_status_t out;

    TEST_ASSERT_TRUE(power_board_control_get_status(&out));
    return out;
}

static void reply(scp_cmd_result_t result, const scp_packet_t *packet)
{
    TEST_ASSERT_NOT_NULL(callback);
    free_transport = true;
    callback(result, packet);
}

static void config_reply(uint8_t gen, bool echo, uint8_t mask1, uint8_t mask2)
{
    scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_CFG2, .type = SCP_TYPE_ACK, .data_len = 23U
    };

    packet.data[0] = 1U;
    packet.data[1] = 1U;
    packet.data[2] = gen;
    packet.data[10] = 22U;
    packet.data[14] = 12U;
    packet.data[15] = 0x8AU;
    packet.data[17] = echo ? 1U : 0U;
    packet.data[18] = gen;
    packet.data[19] = mask1;
    packet.data[20] = mask2;
    packet.data[21] = 0x00U;
    packet.data[22] = 0x20U;
    if (8U == gen)
    {
        for (size_t index = request_count; 0U < index; index--)
        {
            const scp_packet_t *request = &requests[index - 1U];
            if ((RF_SCP_CMD_PWR_CFG2 == request->cmd) &&
                (SCP_TYPE_SET == request->type))
            {
                const uint16_t mask = scp_unpack_u16(&request->data[1]);
                for (size_t byte = 2U; byte < 15U; byte++)
                {
                    if (0U != (mask & (1U << byte)))
                    {
                        packet.data[byte + 1U] = request->data[byte + 1U];
                    }
                }
                break;
            }
        }
    }
    reply(SCP_CMD_OK, &packet);
}

static void write_ack(uint8_t gen)
{
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_CFG2, .type = SCP_TYPE_ACK,
        .data_len = 1U, .data = {gen}
    };

    reply(SCP_CMD_OK, &packet);
}

static void command_ack(uint8_t command, uint8_t sequence)
{
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_COMMAND, .type = SCP_TYPE_ACK,
        .data_len = 2U, .data = {command, sequence}
    };

    reply(SCP_CMD_OK, &packet);
}

static void result_report(uint8_t sequence, uint8_t result, uint8_t flags)
{
    const rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_PWR_RESULT, .type = SCP_TYPE_SET,
        .body.command_result = {.command = 5U, .seq = sequence,
            .result = result, .transmissions = 2U, .flags = flags}
    };

    TEST_ASSERT_TRUE(power_board_handle_command_result(&message));
}

static void start_capacity(void)
{
    const size_t first = request_count;
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_CAPACITY, .capacity_ah = 24U
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    config_reply(7U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT8(SCP_TYPE_SET, requests[first + 1U].type);
    TEST_ASSERT_EQUAL_UINT8(7U, requests[first + 1U].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x20U, requests[first + 1U].data[2]);
    TEST_ASSERT_EQUAL_UINT8(24U, requests[first + 1U].data[14]);
    write_ack(8U);
}

void setUp(void)
{
    tick = 0U;
    request_count = 0U;
    callback = NULL;
    free_transport = true;
    ready = true;
    accept_request = true;
    (void)memset(&power, 0, sizeof(power));
    power.valid_fields = POWER_VALID_SUMMARY;
    power.summary.capacity_ah = 24U;
    power.summary.charge_rate_permille = 22U;
    power_board_control_init();
    scp_is_free_StubWithCallback(is_free);
    rf_comm_can_load_inventory_StubWithCallback(is_ready);
    scp_send_request_StubWithCallback(send_request);
    power_board_get_snapshot_StubWithCallback(get_power);
}

void tearDown(void)
{
}

void test_period_write_preserves_hidden_bit_and_changes_only_customer_byte(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_PERIOD, .period_sec = 2U
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    TEST_ASSERT_EQUAL_UINT8(SCP_TYPE_GET, requests[0].type);
    config_reply(7U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_HEX16(POWER_SETTING_PERIOD,
                           scp_unpack_u16(&requests[1].data[1]));
    TEST_ASSERT_EQUAL_HEX8(0x82U, requests[1].data[15]);
    for (size_t index = 3U; index < 15U; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0U, requests[1].data[index]);
    }
    write_ack(8U);
    tick = 19999U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    tick = 20000U;
    power_board_control_process(tick);
    config_reply(8U, true, 0x20U, 0x80U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
    TEST_ASSERT_EQUAL_HEX16(POWER_SETTING_CAPACITY,
                           control_status().last_rejected);
}

void test_capacity_waits_for_36_seconds_echo_and_new_matching_summary(void)
{
    start_capacity();
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_ECHO,
                          control_status().settings_state);
    tick = 35999U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    tick = 36000U;
    power.summary.flags2 = 0x40U;
    power_board_control_process(tick);
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    power.summary.flags2 = 0x20U; /* Current limiting is not rejection. */
    power.summary.capacity_ah = 12U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    power.summary.capacity_ah = 24U;
    power.received_age_ms = 1U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    power.received_age_ms = 0U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
}

void test_missing_echo_needs_explicit_read_and_never_polls_each_tick(void)
{
    start_capacity();
    tick = 36000U;
    power_board_control_process(tick);
    config_reply(8U, false, 0U, 0U);
    for (uint8_t index = 0U; index < 10U; index++)
    {
        power_board_control_process(++tick);
    }
    TEST_ASSERT_EQUAL_size_t(3U, request_count);
    TEST_ASSERT_TRUE(power_board_read_settings());
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
}

void test_echo_rejection_and_changed_gen_are_not_success(void)
{
    start_capacity();
    tick = 36000U;
    power_board_control_process(tick);
    config_reply(8U, true, 0U, 0x20U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_REJECTED,
                          control_status().settings_state);
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    start_capacity();
    tick += 36000U;
    power_board_control_process(tick);
    config_reply(9U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_CONFLICT,
                          control_status().settings_state);
}

void test_first_write_requires_explicit_real_capacity(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_RATE, .rate_permille = 22U
    };
    scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_CFG2, .type = SCP_TYPE_ACK, .data_len = 23U,
        .data = {1U}
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_REJECTED,
                          control_status().settings_state);
    TEST_ASSERT_EQUAL_HEX16(POWER_SETTING_CAPACITY,
                           control_status().rejected_fields);
}

void test_gen_conflict_does_not_blindly_resend_set(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_CAPACITY, .capacity_ah = 24U
    };
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_CFG2, .type = SCP_TYPE_ERROR,
        .data_len = 2U, .data = {RF_SCP_ERR_BUSY, 9U}
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    config_reply(7U, true, 0U, 0U);
    reply(SCP_CMD_ERR, &packet);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_ERR_BUSY, control_status().error_code);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_CONFLICT,
                          control_status().settings_state);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    TEST_ASSERT_EQUAL_UINT8(0U, control_status().error_code);
    config_reply(9U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT8(9U, requests[3].data[0]);
}

void test_failed_verification_read_preserves_stored_state_and_allows_query(void)
{
    start_capacity();
    tick = 36000U;
    power_board_control_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    TEST_ASSERT_TRUE(power_board_read_settings());
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
}

void test_write_timeout_is_unknown_and_does_not_send_battery_command(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_CAPACITY, .capacity_ah = 24U
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    config_reply(7U, true, 0U, 0U);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_UNCERTAIN,
                          control_status().settings_state);
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
}

void test_unset_rate_is_echo_accepted_without_applied_claim(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_RATE, .rate_permille = 0U
    };

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    config_reply(7U, true, 0U, 0U);
    write_ack(8U);
    tick = 36000U;
    power_board_control_process(tick);
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_ECHO_ACCEPTED,
                          control_status().settings_state);
}

void test_battery_replacement_waits_for_matching_result_not_ack(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    TEST_ASSERT_EQUAL_UINT8(5U, requests[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5U, requests[0].data[1]);
    command_ack(5U, 42U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_ACCEPTED,
                          control_status().command_state);
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    result_report(41U, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_APPLIED,
                          control_status().command_state);
    TEST_ASSERT_EQUAL_UINT8(41U, control_status().result_seq);
    result_report(42U, 0U, 8U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_APPLIED,
                          control_status().command_state);
}

void test_result_before_ack_is_not_completion_evidence(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    result_report(42U, 0U, 8U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_SENDING,
                          control_status().command_state);
    command_ack(5U, 42U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_ACCEPTED,
                          control_status().command_state);
    TEST_ASSERT_FALSE(control_status().has_result);
    result_report(43U, 0U, 8U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_APPLIED,
                          control_status().command_state);
}

void test_cancel_ack_does_not_claim_that_battery_counters_were_preserved(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 42U);
    TEST_ASSERT_TRUE(power_board_cancel_command());
    TEST_ASSERT_EQUAL_UINT8(0U, requests[1].data[0]);
    command_ack(0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_UNCERTAIN,
                          control_status().command_state);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_PWR_RESULT, requests[2].cmd);
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_RESULT, .type = SCP_TYPE_ACK,
        .data_len = 5U, .data = {5U, 42U, 255U, 0U, 2U}
    };
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_MONITORING,
                          control_status().command_state);
    result_report(42U, 255U, 4U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_CANCELLED,
                          control_status().command_state);
    result_report(42U, 255U, 8U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_APPLIED,
                          control_status().command_state);
}

void test_command_rejection_and_no_reply_are_not_success(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 42U);
    result_report(42U, 4U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_REJECTED,
                          control_status().command_state);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 43U);
    result_report(43U, 255U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_NO_RESPONSE,
                          control_status().command_state);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
}

void test_restart_clears_reports_and_ends_pending_jobs(void)
{
    TEST_ASSERT_TRUE(power_board_read_settings());
    config_reply(7U, true, 0U, 0U);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    reply(SCP_CMD_RESTARTED, NULL);
    TEST_ASSERT_FALSE(control_status().has_config);
    TEST_ASSERT_FALSE(control_status().has_result);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_RESTARTED,
                          control_status().command_state);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
}

void test_captured_command_reports_show_pending_then_parameter_rejection(void)
{
    uint32_t result_count = 0U;

    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 1U);
    for (size_t index = 0U;
         index < sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]); index++)
    {
        const uint8_t *logical = rf_scp_vectors[index].logical;
        if (RF_SCP_CMD_PWR_RESULT == logical[3])
        {
            scp_packet_t packet = {.src = logical[1], .dst = logical[0],
                .type = logical[2], .cmd = logical[3],
                .seq = logical[4], .data_len = logical[5]};
            rf_scp_message_t message;

            (void)memcpy(packet.data, &logical[7], packet.data_len);
            if ((255U == packet.data[2]) && (1U == packet.data[3]))
            {
                packet.data[4] |= 0x10U; /* R2 running command. */
            }
            if (RF_CMD_OK != rf_scp_decode_message(&packet, &message))
            {
                continue;
            }
            TEST_ASSERT_TRUE(power_board_handle_command_result(&message));
            TEST_ASSERT_EQUAL_UINT8(packet.data[4],
                                   control_status().command_flags);
            TEST_ASSERT_EQUAL_INT((255U == packet.data[2]) ?
                POWER_COMMAND_ACCEPTED : POWER_COMMAND_REJECTED,
                control_status().command_state);
            result_count++;
        }
    }
    TEST_ASSERT_TRUE(0U < result_count);
}

void test_raw_read_and_unsolicited_blocks_preserve_exact_96_bytes(void)
{
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_PWR_TELEMETRY,
        .data_len = 96U};

    for (size_t index = 0U; index < 96U; index++)
    {
        packet.data[index] = (uint8_t)index;
    }
    TEST_ASSERT_TRUE(power_board_read_raw());
    reply(SCP_CMD_OK, &packet);
    power_board_control_status_t out = control_status();
    TEST_ASSERT_EQUAL_MEMORY(packet.data, out.raw, 96U);
    packet.type = SCP_TYPE_SET;
    packet.data[95] = 0xA5U;
    rf_scp_message_t message;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_TRUE(power_board_handle_raw(&message));
    out = control_status();
    TEST_ASSERT_EQUAL_MEMORY(packet.data, out.raw, 96U);
    TEST_ASSERT_TRUE(power_board_read_raw());
    reply(SCP_CMD_ERR, NULL);
    TEST_ASSERT_TRUE(control_status().has_raw);
}

void test_captured_cfg2_get_set_echo_flow_keeps_customer_mask(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_PERIOD, .period_sec = 2U
    };
    uint32_t replies = 0U;

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    for (size_t index = 0U;
         index < sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]); index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        const uint8_t *logical = vector->logical;

        if ((0 == strcmp("PWRB_03_ayar_yazma.csv", vector->source)) &&
            (SCP_TYPE_ACK == logical[2]))
        {
            scp_packet_t packet = {.src = logical[1], .dst = logical[0],
                .type = logical[2], .cmd = logical[3],
                .seq = logical[4], .data_len = logical[5]};
            (void)memcpy(packet.data, &logical[7], packet.data_len);
            if ((255U == packet.data[2]) && (1U == packet.data[3]))
            {
                packet.data[4] |= 0x10U; /* R2 running command. */
            }
            if (2U == replies)
            {
                tick = 20000U;
                power_board_control_process(tick);
            }
            reply(SCP_CMD_OK, &packet);
            replies++;
            if (1U == replies)
            {
                TEST_ASSERT_EQUAL_UINT8(10U, requests[1].data[0]);
                TEST_ASSERT_EQUAL_HEX16(POWER_SETTING_PERIOD,
                    scp_unpack_u16(&requests[1].data[1]));
                TEST_ASSERT_EQUAL_UINT8(2U, requests[1].data[15]);
            }
            if (3U == replies)
            {
                break;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3U, replies);
    TEST_ASSERT_EQUAL_UINT8(11U, control_status().written_gen);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
}

void test_verification_delay_crosses_tick_wrap_and_waits_for_transport(void)
{
    tick = UINT32_MAX - 1000U;
    start_capacity();
    tick += 35999U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    tick++;
    free_transport = false;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    free_transport = true;
    power_board_control_process(tick);
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
}

void test_result_get_recovers_notify_and_restart_invalidates_raw_block(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 42U);
    TEST_ASSERT_TRUE(power_board_read_command_result());
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_RESULT, .type = SCP_TYPE_ACK, .data_len = 5U,
        .data = {5U, 42U, 0U, 2U, 8U}
    };

    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_APPLIED,
                          control_status().command_state);
    rf_scp_message_t raw = {.cmd = RF_SCP_CMD_PWR_TELEMETRY,
                            .type = SCP_TYPE_SET};
    TEST_ASSERT_TRUE(power_board_handle_raw(&raw));
    power_board_control_hub_restarted();
    TEST_ASSERT_FALSE(control_status().has_raw);
    TEST_ASSERT_FALSE(control_status().has_result);
}

void test_invalid_input_busy_transport_and_unknown_hub_start_no_request(void)
{
    power_settings_request_t request =
    {
        .mask = POWER_SETTING_CAPACITY, .capacity_ah = 55U
    };

    TEST_ASSERT_FALSE(power_board_write_settings(NULL));
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    request.capacity_ah = 0U;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    request.capacity_ah = 255U;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    request.capacity_ah = 24U;
    request.mask = 0x0004U;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    request.mask = POWER_SETTING_PERIOD;
    request.period_sec = 0U;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    request.period_sec = 255U;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    TEST_ASSERT_FALSE(power_board_handle_raw(NULL));
    TEST_ASSERT_FALSE(power_board_handle_command_result(NULL));
    TEST_ASSERT_FALSE(power_board_control_get_status(NULL));
    free_transport = false;
    TEST_ASSERT_FALSE(power_board_read_settings());
    free_transport = true;
    ready = false;
    TEST_ASSERT_FALSE(power_board_battery_replaced());
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_combined_capacity_and_rate_require_matching_fresh_summary(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_CAPACITY | POWER_SETTING_RATE,
        .capacity_ah = 24U, .rate_permille = 50U
    };
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_CFG2, .type = SCP_TYPE_ACK, .data_len = 23U,
        .data = {1U}};

    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_HEX16(0x2200U, scp_unpack_u16(&requests[1].data[1]));
    TEST_ASSERT_EQUAL_UINT8(50U, requests[1].data[10]);
    TEST_ASSERT_EQUAL_UINT8(24U, requests[1].data[14]);
    write_ack(1U);
    tick = 20000U;
    power_board_control_process(tick);
    packet.data[1] = 2U;
    packet.data[2] = 1U;
    packet.data[10] = 50U;
    packet.data[14] = 24U;
    packet.data[15] = 1U;
    packet.data[17] = 1U;
    packet.data[18] = 1U;
    power.valid_fields = 0U;
    power.summary.charge_rate_permille = 50U;
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    power.valid_fields = POWER_VALID_SUMMARY;
    power.summary.charge_rate_permille = 22U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    power.summary.charge_rate_permille = 50U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_APPLIED,
                          control_status().settings_state);
}

void test_shell_rejects_invalid_values_and_exposes_only_customer_commands(void)
{
    char *valid[] = {"pwrboard", "cfg-set", "capacity", "24"};
    char *negative[] = {"pwrboard", "cfg-set", "rate", "-1"};
    char *overflow[] = {"pwrboard", "cfg-set", "rate", "256"};
    char *hidden[] = {"pwrboard", "cfg-set", "tcal", "123"};
    char *unknown[] = {"pwrboard", "service-command", "1"};

    TEST_ASSERT_EQUAL_INT(-1, power_board_control_shell(4, negative));
    TEST_ASSERT_EQUAL_INT(-1, power_board_control_shell(4, overflow));
    TEST_ASSERT_EQUAL_INT(-1, power_board_control_shell(4, hidden));
    TEST_ASSERT_EQUAL_INT(-1, power_board_control_shell(3, unknown));
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_EQUAL_INT(0, power_board_control_shell(4, valid));
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

void test_unset_rate_still_verifies_real_capacity(void)
{
    const power_settings_request_t desired_settings =
    {
        .mask = POWER_SETTING_CAPACITY | POWER_SETTING_RATE,
        .capacity_ah = 24U, .rate_permille = 255U
    };
    TEST_ASSERT_TRUE(power_board_write_settings(&desired_settings));
    config_reply(7U, true, 0U, 0U);
    write_ack(8U);
    tick = 36000U;
    power.summary.capacity_ah = 12U;
    power_board_control_process(tick);
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WAIT_APPLY,
                          control_status().settings_state);
    power.summary.capacity_ah = 24U;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_ECHO_ACCEPTED,
                          control_status().settings_state);
}

void test_get_ff_with_two_transmissions_waits_until_five_minutes(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 42U);
    scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_RESULT, .type = SCP_TYPE_ACK,
        .data_len = 5U, .data = {5U, 43U, 255U, 2U, 0x10U}
    };
    tick = 299999U;
    TEST_ASSERT_TRUE(power_board_read_command_result());
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_ACCEPTED,
                          control_status().command_state);
    tick++;
    TEST_ASSERT_TRUE(power_board_read_command_result());
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_NO_RESPONSE,
                          control_status().command_state);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
}

void test_cancel_get_reports_ineffective_cancel_without_waiting_for_outcome(void)
{
    TEST_ASSERT_TRUE(power_board_cancel_command());
    command_ack(0U, 0U);
    TEST_ASSERT_FALSE(power_board_read_settings());
    power_board_control_process(tick);
    const scp_packet_t packet =
    {
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_PWR_RESULT, .type = SCP_TYPE_ACK,
        .data_len = 5U, .data = {5U, 42U, 0U, 1U, 0U}
    };
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_CANCEL_NO_EFFECT,
                          control_status().command_state);
    const size_t before = request_count;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
}

void test_r2_b4_distinguishes_running_from_unanswered_terminal_report(void)
{
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 1U);
    rf_scp_message_t message = {.cmd = RF_SCP_CMD_PWR_RESULT,
        .type = SCP_TYPE_SET, .body.command_result =
        {.command = 5U, .seq = 1U, .result = 255U,
         .transmissions = 2U, .flags = 0x10U}};
    TEST_ASSERT_TRUE(power_board_handle_command_result(&message));
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_ACCEPTED, control_status().command_state);
    message.body.command_result.flags = 0U;
    message.body.command_result.transmissions = 1U;
    TEST_ASSERT_TRUE(power_board_handle_command_result(&message));
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_NO_RESPONSE,
                          control_status().command_state);
    TEST_ASSERT_TRUE(power_board_battery_replaced());
    command_ack(5U, 2U);
    message.type = SCP_TYPE_ACK;
    message.body.command_result.seq = 2U;
    TEST_ASSERT_TRUE(power_board_handle_command_result(&message));
    TEST_ASSERT_EQUAL_INT(POWER_COMMAND_NO_RESPONSE,
                          control_status().command_state);
}

static void check_setting_request(const power_settings_request_t *request,
                                  bool accepted)
{
    power_board_control_init();
    request_count = 0U;
    free_transport = true;
    TEST_ASSERT_EQUAL(accepted, power_board_write_settings(request));
    TEST_ASSERT_EQUAL_size_t(accepted ? 1U : 0U, request_count);
    TEST_ASSERT_EQUAL_INT(accepted ? POWER_SETTINGS_READING :
                         POWER_SETTINGS_IDLE, control_status().settings_state);
}

void test_rate_acceptance_covers_all_byte_values_including_unset_sentinels(void)
{
    for (uint16_t value = 0U; UINT8_MAX >= value; value++)
    {
        const power_settings_request_t request =
        {
            .mask = POWER_SETTING_RATE, .rate_permille = (uint8_t)value
        };
        check_setting_request(&request, (0U == value) || (255U == value) ||
                              ((20U <= value) && (200U >= value)));
    }
}

void test_capacity_acceptance_covers_all_byte_values(void)
{
    for (uint16_t value = 0U; UINT8_MAX >= value; value++)
    {
        const power_settings_request_t request =
        {
            .mask = POWER_SETTING_CAPACITY, .capacity_ah = (uint8_t)value
        };
        check_setting_request(&request, (7U <= value) && (54U >= value));
    }
}

void test_period_acceptance_covers_all_byte_values(void)
{
    for (uint16_t value = 0U; UINT8_MAX >= value; value++)
    {
        const power_settings_request_t request =
        {
            .mask = POWER_SETTING_PERIOD, .period_sec = (uint8_t)value
        };
        check_setting_request(&request, (1U <= value) && (10U >= value));
    }
}

void test_transport_refusal_preserves_idle_and_delayed_set_retries_once(void)
{
    const power_settings_request_t request =
    {
        .mask = POWER_SETTING_PERIOD, .period_sec = 2U
    };
    accept_request = false;
    TEST_ASSERT_FALSE(power_board_write_settings(&request));
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_IDLE, control_status().settings_state);
    accept_request = true;
    TEST_ASSERT_TRUE(power_board_write_settings(&request));
    accept_request = false;
    config_reply(7U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_READING,
                         control_status().settings_state);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    accept_request = true;
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_WRITING,
                         control_status().settings_state);
    power_board_control_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
}

void test_invalid_unsolicited_blocks_preserve_last_valid_reports(void)
{
    rf_scp_message_t raw = {.cmd = RF_SCP_CMD_PWR_TELEMETRY,
                            .type = SCP_TYPE_SET};
    (void)memset(raw.body.telemetry.bytes, 0xA5,
                 sizeof(raw.body.telemetry.bytes));
    TEST_ASSERT_TRUE(power_board_handle_raw(&raw));
    const power_board_control_status_t before = control_status();
    raw.type = SCP_TYPE_GET;
    TEST_ASSERT_FALSE(power_board_handle_raw(&raw));
    raw.type = SCP_TYPE_SET;
    raw.cmd = RF_SCP_CMD_PWR_SUMMARY;
    TEST_ASSERT_FALSE(power_board_handle_raw(&raw));
    TEST_ASSERT_FALSE(power_board_handle_command_result(&raw));
    raw.cmd = RF_SCP_CMD_PWR_RESULT;
    raw.type = SCP_TYPE_GET;
    TEST_ASSERT_FALSE(power_board_handle_command_result(&raw));
    const power_board_control_status_t after = control_status();
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
}

void test_read_timeout_preserves_cached_config_and_next_read_recovers(void)
{
    TEST_ASSERT_TRUE(power_board_read_settings());
    config_reply(7U, true, 0U, 0U);
    const power_board_control_status_t before = control_status();
    TEST_ASSERT_TRUE(power_board_read_settings());
    reply(SCP_CMD_TIMEOUT, NULL);
    const power_board_control_status_t after = control_status();
    TEST_ASSERT_EQUAL_INT(POWER_SETTINGS_REJECTED, after.settings_state);
    TEST_ASSERT_EQUAL_MEMORY(before.config, after.config, sizeof(after.config));
    TEST_ASSERT_TRUE(after.has_config);
    TEST_ASSERT_TRUE(power_board_read_settings());
    config_reply(8U, true, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT8(8U, control_status().config[1]);
}

/*** end of file ***/
