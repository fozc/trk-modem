/*
 * test_rf_group.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify configuration groups with real store/codec and captured reports.
 */

#include "unity.h"
#include "rf_group.h"
#include "rf_config.h"
#include "rf_group_web.h"
#include "rf_nvram_fake.h"
#include "mock_rf_comm.h"
#include "mock_rf_inventory.h"
#include "../fixtures/rf_scp_vectors.h"
#include <math.h>
#include <string.h>

TEST_SOURCE_FILE("rf_config.c")
TEST_SOURCE_FILE("rf_nvram_fake.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("rf_group_web.c")
TEST_SOURCE_FILE("xprintf.c")

static uint32_t tick;
static bool transport_free;
static bool inventory_loaded;
static bool binding_valid;
static bool accept_request;
static bool epoch_ready;
static size_t request_count;
static scp_packet_t requests[12];
static scp_cmd_done_fn_t callback;
static rf_feeder_t feeder;
static rf_inventory_entry_t bindings[3];

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

static bool is_free(int call_count)
{
    (void)call_count;
    return transport_free;
}

static bool is_loaded(int call_count)
{
    (void)call_count;
    return inventory_loaded;
}

static bool is_epoch_ready(uint8_t fider, int call_count)
{
    (void)fider;
    (void)call_count;
    return epoch_ready;
}

static bool get_binding(uint8_t source, rf_inventory_entry_t *out,
                        int call_count)
{
    const uint8_t phase = source & 0x03U;
    const uint8_t fider = (source >> 2U) & 0x07U;

    (void)call_count;
    if (!binding_valid || (0U == phase) ||
        (fider != bindings[0].feeder))
    {
        return false;
    }
    *out = bindings[(size_t)phase - 1U];
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
    TEST_ASSERT_TRUE(sizeof(requests) / sizeof(requests[0]) > request_count);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_build_packet(request, &packet));
    requests[request_count++] = packet;
    callback = done;
    transport_free = false;
    return true;
}

static rf_group_status_t status(void)
{
    rf_group_status_t out;

    TEST_ASSERT_TRUE(rf_group_get_status(&out));
    return out;
}

static void reply(scp_cmd_result_t result, scp_packet_t *packet)
{
    TEST_ASSERT_NOT_NULL(callback);
    transport_free = true;
    callback(result, packet);
}

static void ack(void)
{
    scp_packet_t packet =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = requests[request_count - 1U].cmd
    };

    reply(SCP_CMD_OK, &packet);
}

static void unused_group(void)
{
    scp_packet_t packet = {.data_len = 1U,
                           .data = {RF_SCP_ERR_INVALID_PARAM}};

    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[0].cmd);
    reply(SCP_CMD_ERR, &packet);
}

static void write_three_and_commit(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    for (size_t index = 0U; index < 3U; index++)
    {
        rf_group_process(tick);
        TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_WRITE,
                               requests[request_count - 1U].cmd);
        ack();
    }
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_COMMIT,
                           requests[request_count - 1U].cmd);
}

static rf_scp_message_t captured_report(uint8_t state)
{
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);

    for (size_t index = 0U; index < count; index++)
    {
        const uint8_t *frame = rf_scp_vectors[index].logical;

        if ((0 == strcmp("AY_06_yapilandirma_yazma.csv",
                         rf_scp_vectors[index].source)) &&
            (RF_SCP_CMD_CFG_STATUS_NOTIFY == frame[3]) &&
            (state == frame[8]))
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
            return message;
        }
    }
    TEST_FAIL_MESSAGE("Required configuration report is missing");
    return (rf_scp_message_t){0};
}

void setUp(void)
{
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);
    size_t members = 0U;

    rf_nvram_fake_reset();
    rf_store_stage_abort();
    rf_group_init();
    rf_config_defaults(&feeder);
    feeder.in_use = true;
    for (size_t index = 0U; index < count; index++)
    {
        const uint8_t *frame = rf_scp_vectors[index].logical;

        if ((0 == strcmp("AY_06_yapilandirma_yazma.csv",
                         rf_scp_vectors[index].source)) &&
            (RF_SCP_CMD_CFG_WRITE == frame[3]) &&
            (SCP_TYPE_SET == frame[2]))
        {
            TEST_ASSERT_TRUE(3U > members);
            (void)memcpy(&feeder.config, &frame[15], sizeof(feeder.config));
            bindings[members].zone = feeder.config.zone_id;
            bindings[members].feeder = feeder.config.fider_id;
            bindings[members].phase = (uint8_t)(members + 1U);
            (void)memcpy(bindings[members].eui64, &frame[7], 8U);
            members++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3U, members);
    (void)memcpy(feeder.r_eui64, bindings[0].eui64, 8U);
    (void)memcpy(feeder.s_eui64, bindings[1].eui64, 8U);
    (void)memcpy(feeder.t_eui64, bindings[2].eui64, 8U);
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    tick = 0U;
    transport_free = true;
    inventory_loaded = true;
    binding_valid = true;
    accept_request = true;
    epoch_ready = true;
    request_count = 0U;
    callback = NULL;
    rf_comm_can_load_inventory_IgnoreAndReturn(true);
    scp_is_free_StubWithCallback(is_free);
    scp_send_request_StubWithCallback(send_request);
    rf_inventory_is_loaded_StubWithCallback(is_loaded);
    rf_inventory_epoch_ready_StubWithCallback(is_epoch_ready);
    rf_inventory_get_binding_StubWithCallback(get_binding);
}

void tearDown(void)
{
}

void test_captured_group_is_written_three_times_and_verified_after_commit(void)
{
    write_three_and_commit();
    TEST_ASSERT_EQUAL_UINT32(5U, request_count);
    for (size_t index = 1U; index < 4U; index++)
    {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(bindings[index - 1U].eui64,
                                     requests[index].data, 8U);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&requests[1].data[8],
                                     &requests[index].data[8], 96U);
        TEST_ASSERT_EQUAL_UINT8(1U, requests[index].data[9]);
        TEST_ASSERT_EQUAL_UINT8(0U, requests[index].data[10]);
        TEST_ASSERT_EQUAL_UINT8(0U, requests[index].data[102]);
        TEST_ASSERT_EQUAL_UINT8(0U, requests[index].data[103]);
    }
    TEST_ASSERT_EQUAL_UINT8(1U, requests[4].data[0]);
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_WAITING, status().state);
    rf_scp_message_t report = captured_report(3U);

    TEST_ASSERT_EQUAL_HEX16(0x096DU, status().expected_crc);
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
    TEST_ASSERT_EQUAL_INT(0, rf_nvram_fake_sync_count());
    char json[512];
    size_t length;

    TEST_ASSERT_TRUE(rf_group_status_json_build(json, sizeof(json), &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"State\":\"applied\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"MatchesDesired\":true"));
    feeder.config.nominal_current = 9.0F;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_TRUE(rf_group_status_json_build(json, sizeof(json), &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"State\":\"applied\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"MatchesDesired\":false"));
    TEST_ASSERT_EQUAL_INT(0, rf_nvram_fake_sync_count());
}

void test_existing_applied_id_is_rejected_before_any_write(void)
{
    rf_scp_message_t report = captured_report(3U);
    scp_packet_t packet =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {1U, 3U, 7U, 0U, 0x6DU, 0x09U, 9U, 0U}
    };

    TEST_ASSERT_FALSE(rf_group_handle_status(&report));
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, request_count);
}

void test_store_changes_after_start_do_not_mix_member_blocks(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    feeder.config.nominal_current = 9.0F;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    for (size_t index = 0U; index < 3U; index++)
    {
        rf_group_process(tick);
        ack();
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&requests[1].data[8],
                                 &requests[3].data[8], 96U);
    TEST_ASSERT_EQUAL_HEX16(0x096DU, status().expected_crc);
}

void test_incomplete_or_changed_ack_binding_rejects_start_and_commit(void)
{
    binding_valid = false;
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    binding_valid = true;
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    ack();
    bindings[2].eui64[0] ^= 1U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_EQUAL_UINT32(2U, request_count);
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    TEST_ASSERT_FALSE(rf_group_abort());
}

void test_start_rejects_missing_duplicate_members_and_invalid_settings(void)
{
    (void)memset(feeder.r_eui64, 0, 8U);
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    (void)memcpy(feeder.r_eui64, feeder.s_eui64, 8U);
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    (void)memcpy(feeder.r_eui64, bindings[0].eui64, 8U);
    feeder.config.ia_threshold = NAN;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    TEST_ASSERT_FALSE(rf_group_start(MAX_POWER_LINE_COUNT, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, request_count);
}

void test_ack_only_means_waiting_not_applied_and_lost_notify_is_polled(void)
{
    write_three_and_commit();
    ack();
    tick = 4999U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(5U, request_count);
    tick++;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[5].cmd);
    scp_packet_t packet =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {1U, 3U, 7U, 0U, 0x6DU, 0x09U, 9U, 0U}
    };

    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
}

void test_early_progress_and_applied_notify_do_not_get_overwritten_by_ack(void)
{
    write_three_and_commit();
    rf_scp_message_t report = captured_report(1U);

    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_COMMITTING, status().state);
    report = captured_report(3U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
    report = captured_report(2U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_UINT8(3U, status().report.state);
}

void test_wrong_crc_or_incomplete_applied_bitmap_is_not_success(void)
{
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(3U);

    report.body.config.config_crc ^= 1U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_incomplete_applied_report_requires_fresh_status_before_retry(void)
{
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(3U);

    report.body.config.member_bitmap = 3U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    report = captured_report(3U);
    report.cmd = RF_SCP_CMD_CFG_STATUS_GET;
    report.type = SCP_TYPE_ACK;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
}

void test_wrong_group_or_type_does_not_change_current_status(void)
{
    write_three_and_commit();
    ack();
    rf_group_status_t previous = status();
    rf_scp_message_t report = captured_report(3U);

    report.body.config.group_id = 2U;
    TEST_ASSERT_FALSE(rf_group_handle_status(&report));
    report.body.config.group_id = 1U;
    report.type = SCP_TYPE_ERROR;
    TEST_ASSERT_FALSE(rf_group_handle_status(&report));
    rf_group_status_t current = status();

    TEST_ASSERT_EQUAL_MEMORY(&previous, &current, sizeof(current));
}

void test_partial_failure_keeps_member_bitmap_without_auto_reapply(void)
{
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(2U);

    report.body.config.state = 4U;
    report.body.config.reason = 6U;
    report.body.config.member_bitmap = 2U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
    TEST_ASSERT_EQUAL_UINT8(2U, status().report.member_bitmap);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bindings[1].eui64, status().members[1], 8U);
    tick = 200000U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(5U, request_count);
}

void test_failed_write_locks_new_groups_without_guessing_abort_identity(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_FALSE(rf_group_abort());
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    rf_group_hub_restarted();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_accepted_commit_can_be_aborted_but_rejection_is_not_cancellation(void)
{
    write_three_and_commit();
    TEST_ASSERT_FALSE(rf_group_abort());
    ack();
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_ABORT, requests[5].cmd);
    scp_packet_t packet = {.data_len = 1U,
                           .data = {RF_SCP_ERR_INVALID_PARAM}};

    reply(SCP_CMD_ERR, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_TRUE(rf_group_abort());
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
}

void test_busy_and_not_loaded_preserve_previous_job(void)
{
    transport_free = false;
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    transport_free = true;
    inventory_loaded = false;
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    TEST_ASSERT_FALSE(rf_group_get_status(NULL));
}

void test_fider_four_uses_ack_members_and_epoch_wait_blocks_start(void)
{
    feeder.config.fider_id = 4U;
    for (size_t index = 0U; index < 3U; index++)
    {
        bindings[index].feeder = 4U;
    }
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    epoch_ready = false;
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    epoch_ready = true;
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT8(4U, requests[1].data[9]);
}

void test_early_failed_notify_keeps_exact_failure_after_commit_timeout(void)
{
    write_three_and_commit();
    rf_scp_message_t report = captured_report(1U);

    report.body.config.state = 4U;
    report.body.config.reason = 1U;
    report.body.config.member_bitmap = 4U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
    TEST_ASSERT_EQUAL_UINT8(1U, status().report.reason);
    TEST_ASSERT_EQUAL_UINT8(4U, status().report.member_bitmap);
}

void test_timeout_after_commit_can_recover_from_authoritative_notification(void)
{
    write_three_and_commit();
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    rf_scp_message_t report = captured_report(3U);

    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
}

void test_transport_refusal_keeps_preflight_pending_without_write(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    accept_request = false;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CHECKING, status().state);
    accept_request = true;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, request_count);
}

void test_user_abort_notify_is_cancellation_not_failure_even_before_ack(void)
{
    write_three_and_commit();
    ack();
    TEST_ASSERT_TRUE(rf_group_abort());
    rf_scp_message_t report = captured_report(1U);

    report.body.config.state = 4U;
    report.body.config.reason = 10U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    TEST_ASSERT_EQUAL_UINT8(10U, status().report.reason);
}

void test_read_only_probe_can_be_cancelled_without_remote_abort(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    scp_packet_t packet = {.data_len = 1U,
                           .data = {RF_SCP_ERR_INVALID_PARAM}};

    reply(SCP_CMD_ERR, &packet);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_read_only_probe_timeout_does_not_require_hub_restart_for_retry(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_pending_status_ack_can_resolve_an_early_mismatching_notify(void)
{
    write_three_and_commit();
    ack();
    tick = 5000U;
    rf_group_process(tick);
    rf_scp_message_t report = captured_report(3U);

    report.body.config.config_crc ^= 1U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    scp_packet_t packet =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {1U, 3U, 7U, 0U, 0x6DU, 0x09U, 9U, 0U}
    };

    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
}

void test_status_poll_waits_five_seconds_across_tick_wrap(void)
{
    tick = UINT32_MAX - 999U;
    write_three_and_commit();
    ack();
    tick = 3999U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(5U, request_count);
    tick = 4000U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_UINT32(6U, request_count);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[5].cmd);
}

void test_changed_desired_settings_do_not_match_the_started_snapshot(void)
{
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_TRUE(rf_group_start(2U, 7U));
    TEST_ASSERT_EQUAL_UINT8(3U, status().line);
    TEST_ASSERT_TRUE(rf_group_matches_config());
    feeder.config.nominal_current = 9.0F;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CHECKING, status().state);
}

void test_rtu_restart_resets_group_metadata_and_does_not_restore_applied(void)
{
    TEST_ASSERT_FALSE(rf_group_is_active());
    TEST_ASSERT_TRUE(rf_group_start(2U, 7U));
    TEST_ASSERT_TRUE(rf_group_is_active());
    rf_group_init();
    TEST_ASSERT_EQUAL_UINT8(0U, status().group_id);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_FALSE(rf_group_is_active());
}

void test_existing_peer_terminal_report_cannot_apply_an_unsent_job(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 7U));
    rf_group_process(tick);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {7U, 1U, 7U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    TEST_ASSERT_TRUE(rf_group_is_active());
    const rf_scp_message_t report = {.cmd = RF_SCP_CMD_CFG_STATUS_NOTIFY,
        .type = SCP_TYPE_SET, .body.config = {.group_id = 7U,
            .state = 3U, .member_bitmap = 7U,
            .config_crc = status().expected_crc}};
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    TEST_ASSERT_FALSE(rf_group_is_active());
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

/*** end of file ***/
