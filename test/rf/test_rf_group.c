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
#include "rf_apply.h"
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
TEST_SOURCE_FILE("rf_apply.c")
TEST_SOURCE_FILE("xprintf.c")

static uint32_t tick;
static bool transport_free;
static bool inventory_loaded;
static bool inventory_active;
static bool inventory_matches;
static bool binding_valid;
static bool accept_request;
static bool epoch_ready;
static size_t request_count;
static scp_packet_t requests[300];
static scp_cmd_done_fn_t callback;
static rf_feeder_t feeder;
static rf_inventory_entry_t bindings[3];
static rf_inventory_entry_t second_bindings[3];

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

static bool is_inventory_active(int call_count)
{
    (void)call_count;
    return inventory_active;
}

static bool matches_inventory(int call_count)
{
    (void)call_count;
    return inventory_matches;
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
    if (binding_valid && (0U != phase) &&
        (0U != second_bindings[0].feeder) &&
        (fider == second_bindings[0].feeder))
    {
        *out = second_bindings[(size_t)phase - 1U];
        return true;
    }
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

static void inventory_wait(rf_inventory_wait_t *out, int call_count)
{
    (void)call_count;
    *out = (rf_inventory_wait_t){.reason = "none"};
}

void setUp(void)
{
    rf_inventory_get_wait_StubWithCallback(inventory_wait);
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);
    size_t members = 0U;

    rf_nvram_fake_reset();
    rf_store_stage_abort();
    rf_store_init();
    rf_group_init();
    rf_apply_init();
    (void)memset(second_bindings, 0, sizeof(second_bindings));
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
    inventory_active = false;
    inventory_matches = true;
    binding_valid = true;
    accept_request = true;
    epoch_ready = true;
    request_count = 0U;
    callback = NULL;
    rf_comm_can_load_inventory_IgnoreAndReturn(true);
    scp_is_free_StubWithCallback(is_free);
    scp_send_request_StubWithCallback(send_request);
    rf_inventory_is_loaded_StubWithCallback(is_loaded);
    rf_inventory_is_active_StubWithCallback(is_inventory_active);
    rf_inventory_matches_config_StubWithCallback(matches_inventory);
    rf_inventory_epoch_ready_StubWithCallback(is_epoch_ready);
    rf_inventory_get_binding_StubWithCallback(get_binding);
    rf_inventory_config_finished_Ignore();
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
    char json[768];
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
    TEST_ASSERT_TRUE(rf_group_abort());
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

void test_failed_write_allows_explicit_abort_of_the_transmitted_group(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_ABORT,
                           requests[request_count - 1U].cmd);
    ack();
    rf_group_hub_restarted();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_uncertain_write_explicit_retry_rewrites_all_frozen_members(void)
{
    for (size_t failed_member = 0U; failed_member < 3U; failed_member++)
    {
        rf_group_init();
        request_count = 0U;
        TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
        unused_group();
        for (size_t index = 0U; index < failed_member; index++)
        {
            rf_group_process(tick);
            ack();
        }
        rf_group_process(tick);
        reply(SCP_CMD_TIMEOUT, NULL);
        const size_t retry_first = request_count;
        const scp_packet_t frozen = requests[1];

        rf_group_process(tick + 10000U);
        TEST_ASSERT_EQUAL_UINT32(retry_first, request_count);
        TEST_ASSERT_FALSE(rf_apply_can_save());
        TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
        TEST_ASSERT_EQUAL_UINT8(0U, status().writes_acked);
        for (size_t index = 0U; index < 3U; index++)
        {
            rf_group_process(tick);
            TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_WRITE,
                                  requests[retry_first + index].cmd);
            TEST_ASSERT_EQUAL_MEMORY(bindings[index].eui64,
                                     requests[retry_first + index].data, 8U);
            TEST_ASSERT_EQUAL_MEMORY(&frozen.data[8],
                &requests[retry_first + index].data[8], 96U);
            ack();
        }
        rf_group_process(tick);
        TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_COMMIT,
                              requests[request_count - 1U].cmd);
        ack();
        rf_scp_message_t report = captured_report(3U);
        TEST_ASSERT_TRUE(rf_group_handle_status(&report));
        TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
    }
}

void test_uncertain_write_retry_rejects_changed_settings_and_keeps_job(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    const rf_group_status_t previous = status();

    TEST_ASSERT_TRUE(rf_store_set(3U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(3U, 1U));
    feeder.config.ia_threshold += 1.0F;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    feeder.config.ia_threshold -= 1.0F;
    feeder.config.fider_id = 2U;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    feeder.config.fider_id = previous.feeder;
    feeder.config.zone_id++;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    feeder.config.zone_id--;
    feeder.r_eui64[7] ^= 1U;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    rf_group_status_t current = status();
    TEST_ASSERT_EQUAL_MEMORY(&previous, &current, sizeof(current));
    TEST_ASSERT_EQUAL_UINT32(2U, request_count);
}

void test_uncertain_commit_cannot_use_precommit_write_retry(void)
{
    write_three_and_commit();
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    TEST_ASSERT_EQUAL_UINT32(5U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
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

static rf_apply_status_t batch_status(void)
{
    rf_apply_status_t out;

    rf_apply_get_status(&out);
    return out;
}

static void add_second_feeder(void)
{
    rf_feeder_t second = feeder;

    second.config.fider_id = 2U;
    for (size_t index = 0U; index < 3U; index++)
    {
        second_bindings[index] = bindings[index];
        second_bindings[index].feeder = 2U;
        second_bindings[index].eui64[0] ^= 0x80U;
    }
    (void)memcpy(second.r_eui64, second_bindings[0].eui64, 8U);
    (void)memcpy(second.s_eui64, second_bindings[1].eui64, 8U);
    (void)memcpy(second.t_eui64, second_bindings[2].eui64, 8U);
    TEST_ASSERT_TRUE(rf_store_set(6U, &second));
}

static void finish_batch_feeder(uint8_t result)
{
    scp_packet_t missing = {.data_len = 1U,
        .data = {RF_SCP_ERR_INVALID_PARAM}};

    rf_group_process(tick);
    reply(SCP_CMD_ERR, &missing);
    for (size_t index = 0U; index < 3U; index++)
    {
        rf_group_process(tick);
        ack();
    }
    rf_group_process(tick);
    ack();
    rf_scp_message_t report = {.cmd = RF_SCP_CMD_CFG_STATUS_NOTIFY,
        .type = SCP_TYPE_SET, .body.config = {
            .group_id = status().group_id, .state = result,
            .member_bitmap = 7U, .config_crc = status().expected_crc}};

    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    rf_apply_process();
}

void test_save_applies_every_active_feeder_in_order_even_when_unchanged(void)
{
    add_second_feeder();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(1, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_HEX8(0x44U, batch_status().targets);
    TEST_ASSERT_FALSE(rf_apply_can_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_UINT8(3U, status().line);
    TEST_ASSERT_EQUAL_UINT8(1U, status().group_id);
    finish_batch_feeder(3U);
    TEST_ASSERT_EQUAL_HEX8(0x04U, batch_status().applied);
    TEST_ASSERT_EQUAL_UINT8(7U, status().line);
    TEST_ASSERT_EQUAL_UINT8(2U, status().group_id);
    finish_batch_feeder(3U);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_COMPLETE, batch_status().state);
    TEST_ASSERT_EQUAL_HEX8(0x44U, batch_status().applied);
    TEST_ASSERT_EQUAL_size_t(10U, request_count);
    TEST_ASSERT_TRUE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_UINT8(3U, status().line);
    TEST_ASSERT_EQUAL_UINT8(3U, status().group_id);
}

void test_nvram_failure_never_starts_rf_or_replaces_previous_batch(void)
{
    rf_apply_status_t previous = batch_status();

    rf_nvram_fake_set_sync_result(-1);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_ERROR, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(previous.state, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    rf_nvram_fake_set_sync_result(0);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
}

void test_busy_save_does_not_sync_or_replace_targets(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_BUSY, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(1, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_HEX8(0x04U, batch_status().targets);
}

void test_empty_active_set_saves_without_rf_commands(void)
{
    feeder.in_use = false;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_COMPLETE, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_EQUAL_INT(1, rf_nvram_fake_sync_count());
}

void test_failed_feeder_stops_queue_and_explicit_save_can_retry(void)
{
    add_second_feeder();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    finish_batch_feeder(4U);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_HEX8(0U, batch_status().applied);
    rf_apply_process();
    TEST_ASSERT_EQUAL_size_t(5U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_UINT8(3U, status().line);
    TEST_ASSERT_EQUAL_UINT8(2U, status().group_id);
}

void test_later_feeder_failure_preserves_completed_feeders(void)
{
    add_second_feeder();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    finish_batch_feeder(3U);
    finish_batch_feeder(4U);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_UINT8(7U, batch_status().line);
    TEST_ASSERT_EQUAL_HEX8(0x04U, batch_status().applied);
    TEST_ASSERT_EQUAL_size_t(10U, request_count);
}

void test_uncertain_write_stops_batch_and_blocks_save(void)
{
    add_second_feeder();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    unused_group();
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_FALSE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_BUSY, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(1, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
}

void test_terminal_used_ids_are_skipped_before_write(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    rf_group_process(tick);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {1U, 3U, 7U}};

    reply(SCP_CMD_OK, &packet);
    rf_apply_process();
    TEST_ASSERT_EQUAL_UINT8(2U, status().group_id);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[1].cmd);
}

void test_used_active_peer_stops_batch_without_writes(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    rf_group_process(tick);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {1U, 1U, 7U}};

    reply(SCP_CMD_OK, &packet);
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_FALSE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

void test_all_255_used_ids_stop_without_zero_or_wrap_reprobe(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    for (uint16_t id = 1U; id <= UINT8_MAX; id++)
    {
        rf_group_process(tick);
        TEST_ASSERT_EQUAL_UINT8(id, requests[request_count - 1U].data[0]);
        scp_packet_t packet = {.src = RF_SCP_ADDR_HUB,
            .dst = RF_SCP_ADDR_RTU, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
            .type = SCP_TYPE_ACK, .data_len = 8U,
            .data = {(uint8_t)id, 3U, 7U}};

        reply(SCP_CMD_OK, &packet);
        rf_apply_process();
    }
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(255U, request_count);
}

void test_abort_before_first_group_and_restart_never_auto_resume(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_TRUE(rf_apply_abort());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_group_hub_restarted();
    rf_apply_hub_restarted();
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    rf_apply_init();
    rf_group_init();
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_IDLE, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_missing_binding_stops_before_first_rf_request(void)
{
    binding_valid = false;
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_FALSE(batch_status().group_started);
    TEST_ASSERT_EQUAL_UINT8(3U, batch_status().line);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_late_applied_after_uncertain_does_not_resume_remaining_feeders(void)
{
    add_second_feeder();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    scp_packet_t missing = {.data_len = 1U,
        .data = {RF_SCP_ERR_INVALID_PARAM}};

    rf_group_process(tick);
    reply(SCP_CMD_ERR, &missing);
    for (size_t index = 0U; index < 3U; index++)
    {
        rf_group_process(tick);
        ack();
    }
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    rf_scp_message_t report = {.cmd = RF_SCP_CMD_CFG_STATUS_NOTIFY,
        .type = SCP_TYPE_SET, .body.config = {
            .group_id = status().group_id, .state = 3U,
            .member_bitmap = 7U, .config_crc = status().expected_crc}};

    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(5U, request_count);
    TEST_ASSERT_TRUE(rf_apply_can_save());
}

void test_transport_busy_waits_without_starting_or_losing_targets(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    transport_free = false;
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_RUNNING, batch_status().state);
    TEST_ASSERT_FALSE(batch_status().group_started);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    transport_free = true;
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CHECKING, status().state);
    TEST_ASSERT_EQUAL_UINT8(1U, status().group_id);
}

void test_inventory_upload_or_drain_blocks_save_before_nvram(void)
{
    inventory_active = true;
    TEST_ASSERT_FALSE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_BUSY, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(0, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    inventory_active = false;
    TEST_ASSERT_TRUE(rf_apply_can_save());
}

static void start_active_peer(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 7U));
    rf_group_process(tick);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {7U, 1U, 7U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_TRUE(rf_group_is_active());
}

void test_lost_peer_terminal_notify_is_recovered_by_status_poll(void)
{
    start_active_peer();
    TEST_ASSERT_FALSE(rf_group_start(2U, 8U));
    tick = 4999U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    tick = 5000U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[1].cmd);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {7U, 3U, 7U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_FALSE(rf_group_is_active());
    TEST_ASSERT_TRUE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
}

void test_only_a_matched_local_terminal_report_notifies_epoch_maintenance(void)
{
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(1U);
    report.body.config.state = 4U;
    report.body.config.reason = 5U;
    rf_inventory_config_finished_Expect(status().feeder, 5U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_FALSE(rf_group_handle_status(&report));
}

void test_failed_peer_poll_keeps_the_lock_and_retries_later(void)
{
    start_active_peer();
    tick = 5000U;
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_TRUE(rf_group_is_active());
    TEST_ASSERT_FALSE(rf_apply_can_save());
    tick = 10000U;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(3U, request_count);
    scp_packet_t wrong = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {8U, 3U, 7U}};
    reply(SCP_CMD_OK, &wrong);
    TEST_ASSERT_TRUE(rf_group_is_active());
    TEST_ASSERT_EQUAL_UINT8(7U, status().report.group_id);
}

void test_batch_reports_start_rejection_without_losing_saved_settings(void)
{
    char json[768];
    size_t length = 0U;
    inventory_loaded = false;
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_START_REJECTED,
                           batch_status().stop_reason);
    TEST_ASSERT_TRUE(rf_group_status_json_build(json, sizeof(json), &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"StopReason\":\"start_rejected\""));
    TEST_ASSERT_EQUAL_INT(1, rf_nvram_fake_sync_count());
}

void test_batch_reports_peer_and_restart_reasons(void)
{
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    rf_group_process(tick);
    scp_packet_t peer = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {1U, 1U, 7U}};
    reply(SCP_CMD_OK, &peer);
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_PEER_ACTIVE, batch_status().stop_reason);
    rf_group_hub_restarted();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_hub_restarted();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_HUB_RESTARTED,
                           batch_status().stop_reason);
}

void test_primary_only_save_never_starts_inventory_or_rf(void)
{
    rf_nvram_fake_set_save_result(NVRAM_SAVE_PRIMARY_ONLY);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_PRIMARY_ONLY, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOPPED, batch_status().state);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_STORAGE, batch_status().stop_reason);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_assignment_save_waits_for_complete_inventory_before_group_write(void)
{
    inventory_matches = false;
    rf_inventory_start_ExpectAndReturn(true);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_TRUE(batch_status().inventory_pending);
    TEST_ASSERT_FALSE(rf_apply_abort());
    inventory_active = true;
    rf_apply_process();
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_FALSE(rf_apply_can_save());
    inventory_active = false;
    inventory_matches = true;
    rf_apply_process();
    TEST_ASSERT_FALSE(batch_status().inventory_pending);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CHECKING, status().state);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

void test_partial_inventory_stops_and_next_save_retries_inventory(void)
{
    inventory_matches = false;
    rf_inventory_start_ExpectAndReturn(true);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_INVENTORY,
                          batch_status().stop_reason);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    rf_inventory_start_ExpectAndReturn(true);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_TRUE(batch_status().inventory_pending);
}

void test_inventory_start_rejection_keeps_saved_settings_without_rf(void)
{
    inventory_matches = false;
    rf_inventory_start_ExpectAndReturn(false);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_INVENTORY,
                          batch_status().stop_reason);
    rf_apply_process();
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_removing_all_assignments_waits_for_empty_inventory(void)
{
    rf_store_get_mutable(2U)->in_use = false;
    inventory_matches = false;
    rf_inventory_start_ExpectAndReturn(true);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    TEST_ASSERT_EQUAL_UINT8(0U, batch_status().targets);
    TEST_ASSERT_TRUE(batch_status().inventory_pending);
    inventory_active = true;
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_RUNNING, batch_status().state);
    inventory_active = false;
    inventory_matches = true;
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_COMPLETE, batch_status().state);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_mh_restart_stops_the_pending_inventory_sequence(void)
{
    inventory_matches = false;
    rf_inventory_start_ExpectAndReturn(true);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_hub_restarted();
    TEST_ASSERT_FALSE(batch_status().inventory_pending);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_STOP_HUB_RESTARTED,
                          batch_status().stop_reason);
    inventory_matches = true;
    rf_apply_process();
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
}

void test_r2_mh_restart_queries_known_commit_once_without_reapplying(void)
{
    write_three_and_commit();
    ack();
    rf_group_hub_restarted();
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .type = SCP_TYPE_ACK, .data_len = 8U,
        .data = {1U, 4U, 0U, 8U, 0U, 0U, 0U, 0U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_TRUE(status().has_report);
    TEST_ASSERT_EQUAL_UINT8(8U, status().report.reason);
    const size_t sent = request_count;
    rf_group_process(200000U);
    TEST_ASSERT_EQUAL_size_t(sent, request_count);
}

/*** end of file ***/
