/*
 * test_rf_events.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify event retrieval using captured replies and storage boundaries.
 */

#include "unity.h"
#include "rf_events.h"
#include "mock_rf_comm.h"
#include "mock_rf_inventory.h"
#include "rf_types.h"
#include "mock_rf_event_log.h"
#include "mock_fault_log.h"
#include "mock_bsp.h"
#include "spi_flash_log.h"
#include "scp_endian.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("spi_flash_log.c")

static uint32_t tick;
static scp_packet_t request;
static scp_cmd_done_fn_t response_handler;
static size_t requests;
static size_t raw_writes;
static size_t fault_writes;
static bool raw_success;
static bool fault_success;
static int sync_result;
static fault_log_t written_fault;
static uint8_t written_raw[60];
static rf_feeder_t feeder;
static rf_inventory_status_t inventory_status;
static bool transport_free;

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

static rf_inventory_status_t get_inventory_status(int call_count)
{
    (void)call_count;
    return inventory_status;
}

static bool is_transport_free(int call_count)
{
    (void)call_count;
    return transport_free;
}

static bool send_request(const scp_packet_t *packet,
                          scp_cmd_done_fn_t done, int call_count)
{
    (void)call_count;
    request = *packet;
    response_handler = done;
    requests++;
    return true;
}

static bool save_raw(const uint8_t *data, size_t length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(60U, length);
    (void)memcpy(written_raw, data, 60U);
    raw_writes++;
    return raw_success;
}

static bool save_fault(const fault_log_t *record, int call_count)
{
    (void)call_count;
    written_fault = *record;
    fault_writes++;
    return fault_success;
}

static int sync_fault(int call_count)
{
    (void)call_count;
    return sync_result;
}

const rf_feeder_t *rf_store_get(feeder_id_t index);
const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    return (2U == index) ? &feeder : NULL;
}

static scp_packet_t capture(uint8_t cmd, uint8_t type)
{
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);

    for (size_t index = 0U; index < count; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        const uint8_t *data = vector->logical;

        if ((0 == strcmp("AY_05b_olay_kaydi_cekme.csv", vector->source)) &&
            (cmd == data[3]) && (type == data[2]))
        {
            scp_packet_t packet =
            {
                .dst = data[0], .src = data[1], .type = data[2],
                .cmd = data[3], .seq = data[4], .data_len = data[5]
            };

            (void)memcpy(packet.data, &data[7], packet.data_len);
            return packet;
        }
    }
    TEST_FAIL_MESSAGE("Event capture is missing");
    return (scp_packet_t){0};
}

static void respond(scp_packet_t *packet)
{
    TEST_ASSERT_NOT_NULL(response_handler);
    response_handler((SCP_TYPE_ERROR == packet->type) ? SCP_CMD_ERR :
                     SCP_CMD_OK, packet);
}

static void update_crc(scp_packet_t *packet)
{
    scp_pack_u16(&packet->data[58], log_calculate_crc16(packet->data, 58U));
}

static void start_read(uint16_t tail, uint16_t head)
{
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, head);
    scp_pack_u16(&packet.data[8], tail);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(tail, scp_unpack_u16(request.data));
}

static void confirm_consumption(void)
{
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_TO, request.cmd);
}

void setUp(void)
{
    tick = 0U;
    requests = 0U;
    raw_writes = 0U;
    fault_writes = 0U;
    raw_success = true;
    fault_success = true;
    sync_result = 0;
    response_handler = NULL;
    (void)memset(&written_fault, 0, sizeof(written_fault));
    (void)memset(&request, 0, sizeof(request));
    (void)memset(&feeder, 0, sizeof(feeder));
    feeder.in_use = true;
    feeder.config.fider_id = 1U;
    feeder.config.zone_id = 1U;
    rf_comm_can_load_inventory_IgnoreAndReturn(true);
    inventory_status = RF_INVENTORY_READY;
    transport_free = true;
    rf_inventory_get_status_StubWithCallback(get_inventory_status);
    scp_is_free_StubWithCallback(is_transport_free);
    scp_send_request_StubWithCallback(send_request);
    rf_event_log_append_StubWithCallback(save_raw);
    fault_log_append_StubWithCallback(save_fault);
    fault_log_sync_StubWithCallback(sync_fault);
    rf_events_init();
}

void tearDown(void)
{
}

void test_capture_is_saved_in_both_logs_before_consume(void)
{
    scp_packet_t notification = capture(RF_SCP_CMD_LOG_AVAILABLE,
                                         SCP_TYPE_SET);
    rf_scp_message_t message;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&notification, &message));
    rf_events_notify(&message);
    start_read(36U, 37U);
    TEST_ASSERT_EQUAL_UINT16(1U, scp_unpack_u16(&request.data[2]));
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data, written_raw, 60U);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data, &written_fault.tm, 7U);
    TEST_ASSERT_EQUAL_UINT32(280U, written_fault.fault_duration_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 15.1033916F,
                             fault_log_current_amps(&written_fault));
    TEST_ASSERT_EQUAL_UINT8(2U, written_fault.info.feeder);
    TEST_ASSERT_EQUAL_UINT8(0U, written_fault.info.phase);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOG_TYPE_PERMANENT,
                            written_fault.info.type);
    TEST_ASSERT_EQUAL_UINT8(1U, written_fault.info.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_TO, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(37U, scp_unpack_u16(request.data));
    packet = capture(RF_SCP_CMD_LOG_CONSUME_TO, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(3U, requests);
}

void test_temporary_and_rf_requested_opening_keep_full_32_bit_duration(void)
{
    start_read(36U, 38U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 3U;
    packet.data[23] = 0xFFU;
    packet.data[24] = 0xFFU;
    packet.data[25] = 0xFFU;
    packet.data[26] = 0xFFU;
    packet.data[13] = 1U;
    packet.data[14] = 1U;
    update_crc(&packet);
    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data[67] = 7U;
    scp_pack_u16(&packet.data[118],
                 log_calculate_crc16(&packet.data[60], 58U));
    packet.data_len = 120U;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOG_TYPE_TEMPORARY,
                            written_fault.info.type);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, written_fault.fault_duration_ms);
    TEST_ASSERT_EQUAL_UINT8(0U, written_fault.info.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT8(1U, written_fault.info.power_status);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOG_TYPE_PERMANENT,
                            written_fault.info.type);
    TEST_ASSERT_EQUAL_UINT32(2U, fault_writes);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT16(38U, scp_unpack_u16(request.data));
}

void test_raw_write_failure_holds_tail_and_retries_the_same_record(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    raw_success = false;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, fault_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    tick = 59999U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    raw_success = true;
    tick++;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, raw_writes);
    confirm_consumption();
}

void test_failed_fault_sync_is_retried_without_appending_either_record(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    sync_result = -1;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    tick = 60000U;
    sync_result = 0;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    confirm_consumption();
}

void test_bad_inner_crc_cannot_advance_tail(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[0] ^= 1U;
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
}

void test_invalid_slot_error_skips_exactly_one_slot_and_wraps(void)
{
    start_read(99U, 1U);
    scp_packet_t packet =
    {
        .type = SCP_TYPE_ERROR, .data_len = 1U,
        .data = {RF_SCP_ERR_RECORD_INVALID}
    };

    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_TO, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(0U, scp_unpack_u16(request.data));
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
}

void test_valid_record_at_slot_99_consumes_to_zero(void)
{
    start_read(99U, 0U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT16(0U, scp_unpack_u16(request.data));
}

void test_unknown_event_is_only_saved_as_raw(void)
{
    start_read(36U, 38U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 0xFEU;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(0U, fault_writes);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT16(37U, scp_unpack_u16(request.data));
}

void test_unassigned_fault_does_not_invent_a_feeder_mapping(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[9] = 0U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(0U, fault_writes);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_TO, request.cmd);
}

void test_bad_second_record_consumes_only_the_saved_prefix(void)
{
    start_read(36U, 38U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data_len = 120U;
    packet.data[60] ^= 1U;
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT16(37U, scp_unpack_u16(request.data));
}

void test_rejected_fault_append_holds_tail_without_rewriting_raw_packet(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    fault_success = false;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    fault_success = true;
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, fault_writes);
    confirm_consumption();
}

void test_pending_fault_sync_is_completed_even_if_configuration_changes(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    sync_result = -1;
    rf_events_process(tick);
    feeder.in_use = false;
    sync_result = -1;
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    sync_result = 0;
    tick = 120000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    confirm_consumption();
}

void test_four_record_batch_is_saved_one_record_per_poll_then_consumed(void)
{
    start_read(36U, 40U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    for (size_t offset = 60U; offset < 240U; offset += 60U)
    {
        (void)memcpy(&packet.data[offset], packet.data, 60U);
    }
    packet.data_len = 240U;
    respond(&packet);
    for (size_t index = 0U; index < 4U; index++)
    {
        rf_events_process(tick);
        TEST_ASSERT_EQUAL_UINT32(index + 1U, raw_writes);
        TEST_ASSERT_EQUAL_UINT32(2U, requests);
    }
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT16(40U, scp_unpack_u16(request.data));
}

void test_response_with_more_records_than_requested_does_not_consume(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data_len = 120U;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
}

void test_consume_rejection_refreshes_head_before_another_attempt(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    packet.type = SCP_TYPE_ERROR;
    packet.data_len = 1U;
    packet.data[0] = RF_SCP_ERR_INVALID_PARAM;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(3U, requests);
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(4U, requests);
}

void test_free_running_clock_sets_iv_without_replacing_timestamp(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[55] = 2U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(1U, written_fault.tm.iv_bit);
    TEST_ASSERT_EQUAL_UINT16(scp_unpack_u16(packet.data),
                             written_fault.tm.milliseconds);
}

void test_repeated_busy_checks_fram_and_degraded_stops_events_until_boot(void)
{
    rf_events_process(tick);
    scp_packet_t packet =
    {
        .type = SCP_TYPE_ERROR, .data_len = 1U,
        .data = {RF_SCP_ERR_BUSY}
    };

    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_GET_FRAM_STATS, request.cmd);
    packet = (scp_packet_t)
    {
        .dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = RF_SCP_CMD_GET_FRAM_STATS,
        .data_len = 8U, .data = {0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U}
    };
    respond(&packet);
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    rf_events_init();
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(3U, requests);
}

void test_empty_ring_is_polled_after_sixty_seconds_when_notify_is_lost(void)
{
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    tick = 59999U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, requests);
    tick++;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
}

void test_full_notify_disambiguates_equal_head_and_tail(void)
{
    rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 100U, .head = 37U}
    };

    rf_events_notify(&message);
    start_read(37U, 37U);
    TEST_ASSERT_EQUAL_UINT16(4U, scp_unpack_u16(&request.data[2]));
}

void test_restart_reanchors_from_mh_head_instead_of_cached_tail(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    rf_events_init();
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
}

void test_zero_records_and_out_of_range_head_do_not_start_a_range_read(void)
{
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 100U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, requests);
    rf_events_init();
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data_len = 0U;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(3U, requests);
}

void test_incomplete_inventory_and_busy_transport_do_not_start_events(void)
{
    inventory_status = RF_INVENTORY_LOADING;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, requests);
    inventory_status = RF_INVENTORY_READY;
    transport_free = false;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, requests);
}

void test_overwrite_during_local_failure_cannot_consume_reused_slots(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    raw_success = false;
    rf_events_process(tick);
    tick = 60000U;
    raw_success = true;
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    /* The index returned to its old value after the ring overwrote it. */
    scp_pack_u16(packet.data, 36U);
    packet.data[4] = 0x54U;
    packet.data[5] = 0x11U; /* 4436 = original total 4237 + 199. */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(3U, requests);
    TEST_ASSERT_NOT_EQUAL(RF_SCP_CMD_LOG_CONSUME_TO, request.cmd);
}

/*** end of file ***/
