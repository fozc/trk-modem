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
#include "mock_elog.h"
TEST_SOURCE_FILE("json.c")
#include "rf_group.h"
#include "rf_apply.h"
#include "rf_config.h"
#include "rf_group_web.h"
#include "rf_nvram_fake.h"
#include "mock_rf_comm.h"
#include "mock_rf_inventory.h"
#include "../fixtures/rf_scp_vectors.h"
#include "../fixtures/rf_scp_r23_vectors.h"
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
static bool relocation;
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

static bool has_relocation(int call_count)
{
    (void)call_count;
    return relocation;
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

static bool verify_commit_before_tx(uint8_t line, uint8_t fider, uint8_t group_id, uint16_t crc, int call_count)
{
    (void)line;
    (void)fider;
    (void)group_id;
    (void)crc;
    (void)call_count;
    TEST_ASSERT_EQUAL_size_t(4U, request_count);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_CFG_WRITE, requests[request_count - 1U].cmd);
    TEST_ASSERT_EQUAL_UINT8(3U, status().writes_acked);
    return true;
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

static void recover_idle_group(void)
{
    rf_group_process(tick);
    scp_packet_t initial = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK, .data_len = 8U};
    reply(SCP_CMD_OK, &initial);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    request_count = 0U;
    callback = NULL;
}

void setUp(void)
{
    elog_log_rf_commit_IgnoreAndReturn(true);
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
    rf_inventory_get_status_IgnoreAndReturn(RF_INVENTORY_READY);
    rf_inventory_prepare_save_IgnoreAndReturn(true);
    relocation = false;
    rf_inventory_has_relocation_StubWithCallback(has_relocation);
    rf_inventory_has_pending_IgnoreAndReturn(false);
    rf_inventory_service_pending_IgnoreAndReturn(false);
    recover_idle_group();
}

void tearDown(void)
{
}

void test_captured_group_is_written_three_times_and_verified_after_commit(void)
{
    elog_log_rf_commit_StopIgnore();
    elog_log_rf_commit_ExpectAndReturn(3U, feeder.config.fider_id, 1U,
                                      rf_config_writable_crc(&feeder.config), true);
    elog_log_rf_commit_AddCallback(verify_commit_before_tx);
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
        recover_idle_group();
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
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CHECKING, status().state);
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_TRUE(rf_group_is_active());
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

void test_failed_commit_audit_sends_no_commit_does_not_retry_and_keeps_abort_available(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    for (size_t index = 0U; index < 3U; index++)
    {
        rf_group_process(tick);
        ack();
    }
    TEST_ASSERT_EQUAL_INT(RF_GROUP_COMMITTING, status().state);
    const size_t before = request_count;
    elog_log_rf_commit_StopIgnore();
    elog_log_rf_commit_ExpectAndReturn(3U, feeder.config.fider_id, 1U,
                                      rf_config_writable_crc(&feeder.config), false);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
    rf_group_process(tick + 10000U);
    rf_group_process(tick + 20000U);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
    TEST_ASSERT_TRUE(rf_group_can_abort());
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_CFG_ABORT, requests[request_count - 1U].cmd);
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
}

void test_durable_service_move_blocks_normal_save_before_storage_or_rf(void)
{
    rf_inventory_service_pending_StopIgnore();
    rf_inventory_service_pending_IgnoreAndReturn(true);
    TEST_ASSERT_FALSE(rf_apply_can_save());
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_BUSY, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(0, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
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

/* ------------------ R2 edge regressions, 12.10.2026 ------------------ */

static rf_scp_message_t status_message(uint8_t cmd, uint8_t type,
                                       uint8_t group_id, uint8_t state,
                                       uint8_t bitmap, uint8_t reason)
{
    rf_scp_message_t message = {.cmd = cmd, .type = type};

    message.body.config.group_id = group_id;
    message.body.config.state = state;
    message.body.config.member_bitmap = bitmap;
    message.body.config.reason = reason;
    return message;
}

void test_restarted_state_accepts_only_terminal_status_query(void)
{
    write_three_and_commit();
    rf_group_hub_restarted();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    const rf_scp_message_t notify = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 4U, 7U, 8U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&notify));
    const rf_scp_message_t wrong_state = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_ACK, 1U, 3U, 7U, 8U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&wrong_state));
    const rf_scp_message_t wrong_reason = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_ACK, 1U, 4U, 7U, 9U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&wrong_reason));
    const rf_scp_message_t wrong_type = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_SET, 1U, 4U, 7U, 8U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&wrong_type));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_FALSE(status().has_report);
    const rf_scp_message_t terminal = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_ACK, 1U, 4U, 7U, 8U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&terminal));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_TRUE(status().has_report);
}

void test_delayed_progress_notify_moves_uncertain_to_waiting_not_aborting(void)
{
    write_three_and_commit();
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    const rf_scp_message_t progress = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 1U, 0U, 0U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&progress));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_WAITING, status().state);
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ABORTING, status().state);
    TEST_ASSERT_TRUE(rf_group_handle_status(&progress));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ABORTING, status().state);
}

void test_status_report_with_state_zero_is_uncertain(void)
{
    write_three_and_commit();
    const rf_scp_message_t report = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_ACK, 1U, 0U, 0U, 0U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    /* An unresolved commit never allows a fresh start. */
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
}

void test_restarted_command_result_marks_hub_restart(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_RESTARTED, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_FALSE(status().has_report);
}

void test_status_ack_for_another_group_is_uncertain_not_reported(void)
{
    write_three_and_commit();
    ack();
    rf_group_process(tick + 5000U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .type = SCP_TYPE_ACK, .data_len = 8U,
        .data = {9U, 1U, 0U, 0U, 0U, 0U, 0U, 0U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_FALSE(status().has_report);
}

void test_abort_when_transport_refuses_keeps_waiting_state(void)
{
    write_three_and_commit();
    ack();
    accept_request = false;
    TEST_ASSERT_FALSE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_WAITING, status().state);
}

void test_group_process_is_idle_while_a_command_is_pending(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    rf_group_process(tick + 6000U);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

void test_uncertain_read_only_probe_can_be_cancelled_without_remote_abort(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
}

/* --------- R2 dead-branch regressions, 12.10.2026 tur 2 --------- */

void test_matches_config_before_and_after_applied_with_store_drift(void)
{
    const rf_feeder_t saved = feeder;
    rf_scp_message_t report = captured_report(3U);

    TEST_ASSERT_FALSE(rf_group_matches_config());
    write_three_and_commit();
    ack();
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_APPLIED, status().state);
    TEST_ASSERT_TRUE(rf_group_matches_config());
    feeder.config.fider_id = 5U;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    feeder = saved;
    feeder.config.zone_id = 9U;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    feeder = saved;
    feeder.s_eui64[0] ^= 0xA5U;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    feeder = saved;
    feeder.r_eui64[7] ^= 0x5AU;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    feeder = saved;
    feeder.t_eui64[0] ^= 0x3CU;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    feeder = saved;
    feeder.in_use = false;
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_TRUE(rf_store_set(2U, &saved));
    TEST_ASSERT_TRUE(rf_group_matches_config());
}

void test_can_start_and_is_active_matrix_for_observed_peer(void)
{
    scp_packet_t in_use =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {1U, 3U, 7U, 0U, 0x6DU, 0x09U, 0U, 0U}
    };

    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_OK, &in_use);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    /* Without a report the peer job is not provably active. */
    TEST_ASSERT_FALSE(rf_group_is_active());
    /* Progress keeps it active and blocks a fresh job. */
    const rf_scp_message_t progress = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 2U, 3U, 0U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&progress));
    TEST_ASSERT_TRUE(rf_group_is_active());
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    /* A terminal observation releases the lock for a fresh job. */
    const rf_scp_message_t terminal = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_ACK, 1U, 3U, 0U, 0U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&terminal));
    TEST_ASSERT_FALSE(rf_group_is_active());
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
}

void test_mismatch_with_full_bitmap_restarts_and_locked_mismatch_polls(void)
{
    rf_scp_message_t report = captured_report(3U);

    /* Incomplete bitmap locks the job. A mismatch notify that lands while
     * the status poll is pending keeps MISMATCH on the error reply; the
     * explicit abort still works. */
    report.body.config.member_bitmap = 3U;
    write_three_and_commit();
    ack();
    rf_group_process(tick + 5000U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    scp_packet_t error = {.data_len = 1U,
                           .data = {RF_SCP_ERR_NOT_AVAILABLE}};
    reply(SCP_CMD_ERR, &error);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    /* The incomplete bitmap also refuses a fresh start. */
    TEST_ASSERT_FALSE(rf_group_start(2U, 2U));
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ABORTING, status().state);
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);

    /* CRC-only mismatch keeps the full bitmap: a retry is allowed. */
    report = captured_report(3U);
    report.body.config.config_crc = 0x1234U;
    write_three_and_commit();
    ack();
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    TEST_ASSERT_EQUAL_UINT8(7U, status().report.member_bitmap);
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
}

void test_start_rejects_invalid_line_unconfigured_store_and_busy_paths(void)
{
    TEST_ASSERT_FALSE(rf_group_start(MAX_POWER_LINE_COUNT, 1U));
    TEST_ASSERT_FALSE(rf_group_start(3U, 1U));   /* Store line unused. */
    transport_free = false;
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    transport_free = true;
    rf_comm_can_load_inventory_StopIgnore();
    rf_comm_can_load_inventory_ExpectAndReturn(false);
    TEST_ASSERT_FALSE(rf_group_start(2U, 1U));
    rf_comm_can_load_inventory_IgnoreAndReturn(true);
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
}

void test_handle_status_guard_rejects_foreign_forms_and_terminals(void)
{
    TEST_ASSERT_FALSE(rf_group_handle_status(NULL));
    write_three_and_commit();
    ack();                             /* WAITING: reports are acceptable. */
    const rf_scp_message_t notify_with_ack = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_ACK, 1U, 1U, 0U, 0U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&notify_with_ack));
    const rf_scp_message_t get_with_set = status_message(
        RF_SCP_CMD_CFG_STATUS_GET, SCP_TYPE_SET, 1U, 1U, 0U, 0U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&get_with_set));
    const rf_scp_message_t state_five = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 5U, 0U, 0U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&state_five));
    const rf_scp_message_t bitmap_eight = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 1U, 8U, 0U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&bitmap_eight));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_WAITING, status().state);
    /* A cancelled job refuses every further report. */
    TEST_ASSERT_TRUE(rf_group_abort());
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    const rf_scp_message_t valid = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 1U, 0U, 0U);
    TEST_ASSERT_FALSE(rf_group_handle_status(&valid));
    /* MISMATCH accepts only the ACK form. */
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(3U);
    report.body.config.member_bitmap = 3U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_MISMATCH, status().state);
    TEST_ASSERT_FALSE(rf_group_handle_status(&valid));
}

void test_checking_reply_matrix_bodyless_busy_and_foreign_group(void)
{
    scp_packet_t empty = {.data_len = 0U};

    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_ERR, &empty);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    /* An OK result without a packet is equally uncertain. */
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
    rf_group_process(tick);
    reply(SCP_CMD_OK, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    /* So is an OK result that cannot be decoded. */
    TEST_ASSERT_TRUE(rf_group_start(2U, 5U));
    rf_group_process(tick);
    scp_packet_t garbage = {.dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = 0x29U, .data_len = 0U};
    reply(SCP_CMD_OK, &garbage);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    scp_packet_t busy = {.data_len = 1U, .data = {RF_SCP_ERR_BUSY}};
    TEST_ASSERT_TRUE(rf_group_start(2U, 6U));
    rf_group_process(tick);
    reply(SCP_CMD_ERR, &busy);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    scp_packet_t foreign =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {9U, 1U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
    TEST_ASSERT_TRUE(rf_group_start(2U, 3U));
    rf_group_process(tick);
    reply(SCP_CMD_OK, &foreign);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    scp_packet_t free_id =
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 8U, .data = {4U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
    TEST_ASSERT_TRUE(rf_group_start(2U, 4U));
    rf_group_process(tick);
    reply(SCP_CMD_OK, &free_id);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_WRITING, status().state);
}

void test_waiting_poll_survives_null_and_undecodable_replies(void)
{
    write_three_and_commit();
    ack();
    rf_group_process(tick + 5000U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
    reply(SCP_CMD_OK, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    /* Resolve the uncertain commit before starting the next job. */
    const rf_scp_message_t done = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 4U, 7U, 10U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&done));
    write_three_and_commit();
    ack();
    rf_group_process(tick + 5000U);
    scp_packet_t garbage = {.dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = 0x29U, .data_len = 0U};
    reply(SCP_CMD_OK, &garbage);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
}

void test_restarted_without_commit_does_not_poll_and_errors_keep_state(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    rf_group_process(tick);
    reply(SCP_CMD_RESTARTED, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    const size_t before = request_count;
    rf_group_process(tick + 6000U);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
    /* A known commit is re-queried; an error reply keeps RESTARTED. */
    write_three_and_commit();
    rf_group_hub_restarted();
    transport_free = true;   /* The commit send left the transport busy. */
    const size_t after_commit = request_count;
    rf_comm_can_load_inventory_StopIgnore();
    rf_comm_can_load_inventory_ExpectAndReturn(false);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(after_commit, request_count);
    rf_comm_can_load_inventory_IgnoreAndReturn(true);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_RESTARTED, status().state);
    TEST_ASSERT_FALSE(status().has_report);
}

void test_writing_process_guards_lose_epoch_and_inventory(void)
{
    /* Busy transport blocks even the initial probe. */
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    transport_free = false;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    transport_free = true;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    /* Epoch loss during WRITING degrades to UNCERTAIN without a write. */
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_TRUE(rf_group_start(2U, 2U));
    unused_group();
    epoch_ready = false;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    /* Losing the inventory mid-job has the same effect. */
    epoch_ready = true;
    TEST_ASSERT_TRUE(rf_group_start(2U, 3U));
    unused_group();
    inventory_loaded = false;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    TEST_ASSERT_EQUAL_size_t(3U, request_count);
    /* An idle WRITING state after an acked write can be aborted. */
    inventory_loaded = true;
    TEST_ASSERT_TRUE(rf_group_start(2U, 4U));
    unused_group();
    rf_group_process(tick);         /* First WRITE goes out. */
    ack();                          /* The hub now knows the group. */
    TEST_ASSERT_TRUE(rf_group_abort());
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ABORTING, status().state);
    ack();
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
}

void test_waiting_poll_send_refusal_keeps_the_poll_deadline(void)
{
    write_three_and_commit();
    ack();
    accept_request = false;
    rf_group_process(tick + 5000U);
    TEST_ASSERT_EQUAL_size_t(5U, request_count);
    accept_request = true;
    rf_group_process(tick + 5000U);
    TEST_ASSERT_EQUAL_size_t(6U, request_count);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET,
                           requests[request_count - 1U].cmd);
}

void test_abort_rejections_for_idle_pending_and_terminal_states(void)
{
    TEST_ASSERT_FALSE(rf_group_abort());
    /* A pending status poll of a known job cannot be aborted. */
    write_three_and_commit();
    ack();
    rf_group_process(tick + 5000U);    /* STATUS_GET pending. */
    transport_free = true;
    TEST_ASSERT_FALSE(rf_group_abort());
    transport_free = false;            /* Busy transport also refuses. */
    TEST_ASSERT_FALSE(rf_group_abort());
    transport_free = true;
    const rf_scp_message_t cancelled = status_message(
        RF_SCP_CMD_CFG_STATUS_NOTIFY, SCP_TYPE_SET, 1U, 4U, 7U, 10U);
    TEST_ASSERT_TRUE(rf_group_handle_status(&cancelled));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_CANCELLED, status().state);
    TEST_ASSERT_FALSE(rf_group_abort());
}

void test_incomplete_feeder_reports_partial_without_three_member_write(void)
{
    (void)memset(feeder.s_eui64, 0, 8U);
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_PARTIAL, batch_status().state);
    TEST_ASSERT_EQUAL_HEX8(4U, batch_status().skipped);
    TEST_ASSERT_EQUAL_HEX8(0U, batch_status().applied);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_TRUE(rf_apply_can_save());
    char json[1024];
    size_t length;
    TEST_ASSERT_TRUE(rf_group_status_json_build(json, sizeof(json), &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"BatchState\":\"partial\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Skipped\":4"));
}

void test_incomplete_feeder_does_not_block_a_complete_other_feeder(void)
{
    add_second_feeder();
    (void)memset(feeder.s_eui64, 0, 8U);
    TEST_ASSERT_TRUE(rf_store_set(2U, &feeder));
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_OK, rf_apply_save());
    rf_apply_process();
    TEST_ASSERT_EQUAL_UINT8(7U, status().line);
    finish_batch_feeder(3U);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_PARTIAL, batch_status().state);
    TEST_ASSERT_EQUAL_HEX8(0x40U, batch_status().applied);
    TEST_ASSERT_EQUAL_HEX8(4U, batch_status().skipped);
}

void test_abort_capability_is_read_only_and_matches_transport_constraints(void)
{
    TEST_ASSERT_FALSE(rf_group_can_abort());
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    const rf_group_status_t original = status();
    TEST_ASSERT_TRUE(rf_group_can_abort());
    const rf_group_status_t unchanged = status();
    TEST_ASSERT_EQUAL_MEMORY(&original, &unchanged, sizeof(original));
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    unused_group();
    rf_group_process(tick);
    TEST_ASSERT_FALSE(rf_group_can_abort());
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_TRUE(rf_group_can_abort());
    transport_free = false;
    TEST_ASSERT_FALSE(rf_group_can_abort());
    TEST_ASSERT_FALSE(rf_group_abort());
    transport_free = true;
    TEST_ASSERT_TRUE(rf_group_can_abort());
    TEST_ASSERT_TRUE(rf_group_abort());
}

static bool stage_pending_inventory(int call_count)
{
    (void)call_count;
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    journal->pending = 1U;
    journal->count = 1U;
    journal->entries[0].eui64[0] = 0x11U;
    journal->entries[0].phase = 1U;
    return true;
}

void test_failed_target_save_restores_journal_and_never_starts_rf(void)
{
    TEST_ASSERT_TRUE(rf_store_stage_begin());
    rf_store_get_mutable(2U)->r_eui64[0] ^= 1U;
    rf_inventory_prepare_save_StubWithCallback(stage_pending_inventory);
    rf_nvram_fake_set_save_result(NVRAM_SAVE_FAILED);
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_ERROR, rf_apply_save());
    TEST_ASSERT_EQUAL_UINT8(0U, nvram_get_rf_inventory_journal()->pending);
    TEST_ASSERT_EQUAL_UINT8(0U, nvram_get_rf_inventory_journal()->count);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_EQUAL_MEMORY(&feeder, rf_store_get(2U), sizeof(feeder));
}

void test_missing_failed_reasons_one_and_nine_never_become_applied(void)
{
    const uint8_t reasons[] = {1U, 9U};
    for (size_t index = 0U; index < sizeof(reasons); index++)
    {
        rf_group_init();
        transport_free = true;
        recover_idle_group();
        write_three_and_commit();
        ack();
        rf_scp_message_t report = captured_report(1U);
        report.body.config.state = 4U;
        report.body.config.reason = reasons[index];
        report.body.config.member_bitmap = 7U;
        TEST_ASSERT_TRUE(rf_group_handle_status(&report));
        TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
        TEST_ASSERT_EQUAL_UINT8(reasons[index], status().report.reason);
        char json[1024];
        size_t length;
        TEST_ASSERT_TRUE(rf_group_status_json_build(json, sizeof(json), &length));
        TEST_ASSERT_NOT_NULL(strstr(json, "\"State\":\"failed\""));
        report.body.config.state = 2U;
        report.body.config.reason = 0U;
        (void)rf_group_handle_status(&report);
        TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
    }
}

void test_r23_range_failure_reason_3_preserves_all_member_bitmap(void)
{
    write_three_and_commit();
    ack();
    rf_scp_message_t report = captured_report(1U);
    report.body.config.state = 4U;
    report.body.config.reason = 3U;
    report.body.config.member_bitmap = 7U;
    TEST_ASSERT_TRUE(rf_group_handle_status(&report));
    TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
    TEST_ASSERT_EQUAL_UINT8(3U, status().report.reason);
    TEST_ASSERT_EQUAL_UINT8(7U, status().report.member_bitmap);
}

void test_r23_write_error_02_is_uncertain_without_applied_claim_or_blind_retry(void)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, 1U));
    unused_group();
    rf_group_process(tick);
    const size_t before = request_count;
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .cmd = RF_SCP_CMD_CFG_WRITE,
        .type = SCP_TYPE_ERROR, .data_len = 1U,
        .data = {RF_SCP_ERR_INVALID_PARAM}};
    reply(SCP_CMD_ERR, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
}

static void check_r23_status_capture(const char *source, uint8_t group_id, uint8_t reason, uint8_t bitmap)
{
    TEST_ASSERT_TRUE(rf_group_start(2U, group_id));
    unused_group();
    for (size_t member = 0U; member < 3U; member++)
    {
        rf_group_process(tick);
        TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_WRITE, requests[request_count - 1U].cmd);
        ack();
    }
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_COMMIT, requests[request_count - 1U].cmd);
    ack();
    size_t reports = 0U;
    for (size_t index = 0U; index < sizeof(rf_scp_r23_vectors) / sizeof(rf_scp_r23_vectors[0]); index++)
    {
        const rf_scp_r23_vector_t *vector = &rf_scp_r23_vectors[index];
        const uint8_t *frame = vector->logical;
        if ((strcmp(source, vector->source) != 0) || (RF_SCP_ADDR_RTU != frame[0]) ||
            ((RF_SCP_CMD_CFG_STATUS_GET != frame[3]) && (RF_SCP_CMD_CFG_STATUS_NOTIFY != frame[3])) ||
            (SCP_TYPE_ERROR == frame[2]) || (group_id != frame[7]))
        {
            continue;
        }
        scp_packet_t packet = {.dst = frame[0], .src = frame[1], .type = frame[2],
            .cmd = frame[3], .seq = frame[4], .data_len = frame[5]};
        (void)memcpy(packet.data, &frame[7], packet.data_len);
        rf_scp_message_t report;
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &report));
        (void)rf_group_handle_status(&report);
        TEST_ASSERT_NOT_EQUAL(RF_GROUP_APPLIED, status().state);
        reports++;
    }
    TEST_ASSERT_GREATER_THAN_size_t(1U, reports);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_FAILED, status().state);
    TEST_ASSERT_EQUAL_UINT8(reason, status().report.reason);
    TEST_ASSERT_EQUAL_UINT8(bitmap, status().report.member_bitmap);
    const size_t before = request_count;
    rf_group_process(tick + 90000U);
    TEST_ASSERT_EQUAL_size_t(before, request_count);
}

void test_r23_captured_staged_progress_timeout_and_late_queries_remain_failed(void)
{
    check_r23_status_capture("AY_R23_05c_yapilandirma_zaman_asimi_durum_sorgusu.csv", 50U, 5U, 1U);
    TEST_ASSERT_EQUAL_UINT8(13U, status().report.attempts);
}

void test_r23_captured_inventory_rejection_never_claims_application(void)
{
    check_r23_status_capture("AY_R23_05a_yapilandirma_ret_envanter_disi.csv", 64U, 2U, 4U);
}

void test_relocation_save_stops_before_journal_storage_or_rf_activity(void)
{
    TEST_ASSERT_TRUE(rf_store_stage_begin());
    rf_feeder_t *candidate = rf_store_get_mutable(2U);
    (void)memcpy(candidate->s_eui64, candidate->r_eui64, 8U);
    (void)memset(candidate->r_eui64, 0, 8U);
    relocation = true;
    const int before = rf_nvram_fake_sync_count();
    TEST_ASSERT_EQUAL_INT(RF_APPLY_SAVE_MAINTENANCE, rf_apply_save());
    TEST_ASSERT_EQUAL_INT(before, rf_nvram_fake_sync_count());
    TEST_ASSERT_EQUAL_UINT8(0U, nvram_get_rf_inventory_journal()->pending);
    TEST_ASSERT_EQUAL_MEMORY(&feeder, rf_store_get(2U), sizeof(feeder));
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    TEST_ASSERT_FALSE(rf_apply_is_running());
    rf_store_stage_abort();
}

void test_cold_start_queries_zero_only_after_inventory_and_never_writes(void)
{
    rf_group_init();
    inventory_loaded = false;
    rf_inventory_get_status_IgnoreAndReturn(RF_INVENTORY_IDLE);
    TEST_ASSERT_FALSE(rf_group_is_active());
    TEST_ASSERT_FALSE(rf_group_start(2U, 7U));
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    rf_inventory_get_status_IgnoreAndReturn(RF_INVENTORY_READY);
    inventory_active = true;
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, request_count);
    inventory_active = false;
    inventory_loaded = true;
    rf_group_hub_restarted();
    TEST_ASSERT_FALSE(rf_apply_can_save());
    rf_group_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_CFG_STATUS_GET, requests[0].cmd);
    TEST_ASSERT_EQUAL_UINT8(0U, requests[0].data[0]);
    TEST_ASSERT_EQUAL_INT(SCP_TYPE_GET, requests[0].type);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK, .data_len = 8U};
    reply(SCP_CMD_OK, &packet);
    rf_group_process(300000U);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_IDLE, status().state);
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_TRUE(rf_apply_can_save());
}

void test_cold_start_active_peer_blocks_new_group_without_attributing_applied(void)
{
    rf_group_init();
    rf_group_process(tick);
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .cmd = RF_SCP_CMD_CFG_STATUS_GET, .type = SCP_TYPE_ACK,
        .data_len = 8U, .data = {9U, 2U, 7U}};
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_TRUE(rf_group_is_active());
    TEST_ASSERT_FALSE(rf_group_start(2U, 7U));
    TEST_ASSERT_EQUAL_UINT8(0U, status().line);
    TEST_ASSERT_EQUAL_UINT8(0U, status().feeder);
    rf_group_process(5000U);
    TEST_ASSERT_EQUAL_UINT8(9U, requests[1].data[0]);
    packet.data[1] = 3U;
    packet.data[4] = 0x6DU;
    packet.data[5] = 0x09U;
    reply(SCP_CMD_OK, &packet);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_ID_IN_USE, status().state);
    TEST_ASSERT_FALSE(rf_group_matches_config());
    TEST_ASSERT_FALSE(rf_group_is_active());
    TEST_ASSERT_EQUAL_size_t(2U, request_count);
    TEST_ASSERT_TRUE(rf_group_start(2U, 7U));
}

void test_cold_start_timeout_stays_unknown_until_explicit_status_and_does_not_repeat(void)
{
    rf_group_init();
    rf_group_process(tick);
    reply(SCP_CMD_TIMEOUT, NULL);
    TEST_ASSERT_EQUAL_INT(RF_GROUP_UNCERTAIN, status().state);
    rf_group_process(300000U);
    TEST_ASSERT_EQUAL_size_t(1U, request_count);
    rf_scp_message_t message = {.cmd = RF_SCP_CMD_CFG_STATUS_NOTIFY, .type = SCP_TYPE_SET,
        .body.config = {.group_id = 11U, .state = 4U, .reason = 8U}};
    TEST_ASSERT_FALSE(rf_group_handle_status(&message));
    message.cmd = RF_SCP_CMD_CFG_STATUS_GET;
    message.type = SCP_TYPE_ACK;
    TEST_ASSERT_TRUE(rf_group_handle_status(&message));
    TEST_ASSERT_TRUE(status().has_report);
    TEST_ASSERT_EQUAL_UINT8(8U, status().report.reason);
    TEST_ASSERT_EQUAL_UINT8(0U, status().line);
    TEST_ASSERT_FALSE(rf_group_matches_config());
}

/*** end of file ***/
