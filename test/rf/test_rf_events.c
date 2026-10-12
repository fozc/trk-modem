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
#include "rf.h"
#include "rf_faults.h"
#include "rf_alarm.h"
#include "mock_iec104_replay.h"
#include "cp56time2a.h"
#include "rf_types.h"
#include "mock_rf_event_log.h"
#include "mock_fault_log.h"
#include "mock_iec104_event_log.h"
#include "mock_bsp.h"
#include "spi_flash_log.h"
#include "scp_endian.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>
#include <math.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("rf.c")
TEST_SOURCE_FILE("rf_faults.c")
TEST_SOURCE_FILE("rf_alarm.c")
TEST_SOURCE_FILE("cp56time2a.c")

static uint32_t tick;
static scp_packet_t request;
static scp_packet_t read_head;
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
static bool link_active;
static bool emit_success;
static size_t emit_calls;
static size_t replay_writes;
static bool replay_success;
static bool alarm_success;
static bool hash_collision;
static rf_inventory_entry_t binding_entry_storage;
static size_t alarm_writes;
static size_t alarm_emit_calls;
static iec104_alarm_record_t written_alarm;

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

bool iec104_is_link_active(void);
bool iec104_is_link_active(void)
{
    return link_active;
}

bool iec104_emit_evtlog_record(const fault_log_t *record);
bool iec104_emit_evtlog_record(const fault_log_t *record)
{
    TEST_ASSERT_EQUAL_MEMORY(&written_fault.tm, &record->tm,
                             sizeof(record->tm));
    TEST_ASSERT_EQUAL_UINT8(written_raw[13],
                            record->info.nominal_current_status);
    emit_calls++;
    return emit_success;
}

bool iec104_emit_event_record(const iec104_event_record_t *record);
bool iec104_emit_event_record(const iec104_event_record_t *record)
{
    TEST_ASSERT_EQUAL_UINT8(IEC104_EVENT_TRIP_FAILURE, record->kind);
    alarm_emit_calls++;
    return emit_success;
}

static bool send_replay(const iec104_event_record_t *record,
                        uint16_t seq, int call_count)
{
    (void)call_count;
    if (IEC104_EVENT_FAULT == record->kind)
    {
        TEST_ASSERT_EQUAL_UINT16(42U, seq);
        return iec104_emit_evtlog_record(&record->payload.fault);
    }
    TEST_ASSERT_EQUAL_UINT16(43U, seq);
    return iec104_emit_event_record(record);
}

static bool save_alarm(const iec104_alarm_record_t *record, uint16_t *seq,
                       int call_count)
{
    (void)call_count;
    written_alarm = *record;
    alarm_writes++;
    *seq = 43U;
    return alarm_success;
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
    TEST_ASSERT_EQUAL_UINT8(written_raw[13],
                            record->info.nominal_current_status);
    written_fault = *record;
    fault_writes++;
    return fault_success;
}

static int sync_fault(int call_count)
{
    (void)call_count;
    return sync_result;
}

static bool save_replay(const fault_log_t *record, uint16_t *seq,
                         int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_MEMORY(&written_fault.tm, &record->tm,
                             sizeof(record->tm));
    TEST_ASSERT_EQUAL_UINT8(written_raw[13],
                            record->info.nominal_current_status);
    *seq = 42U;
    replay_writes++;
    return replay_success;
}

const rf_feeder_t *rf_store_get(feeder_id_t index);

/* Local pure helper from rf_config.c; avoids linking the whole store. */
bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN])
{
    bool zero = true;
    for (size_t i = 0U; i < RF_EUI64_LEN; i++)
    {
        if (0U != eui[i])
        {
            zero = false;
        }
    }
    return zero;
}
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
        const uint8_t lookup = (RF_SCP_CMD_LOG_CONSUME_IF == cmd) ?
            RF_SCP_CMD_LOG_CONSUME_TO : cmd;

        if ((0 == strcmp("AY_05b_olay_kaydi_cekme.csv", vector->source)) &&
            (lookup == data[3]) && (type == data[2]))
        {
            scp_packet_t packet =
            {
                .dst = data[0], .src = data[1], .type = data[2],
                .cmd = data[3], .seq = data[4], .data_len = data[5]
            };

            (void)memcpy(packet.data, &data[7], packet.data_len);
            if (RF_SCP_CMD_LOG_READ_RANGE == cmd)
            {
                scp_pack_u16(&packet.data[56],
                    rf_scp_eui_hash(binding_entry_storage.eui64));
                scp_pack_u16(&packet.data[58],
                    log_calculate_crc16(packet.data, 58U));
            }
            if (RF_SCP_CMD_LOG_CONSUME_IF == cmd)
            {
                packet.cmd = cmd;
                packet.data_len = 8U;
                (void)memcpy(&packet.data[4], request.data, 4U);
            }
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

static void put_u32(uint8_t *data, uint32_t value)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        data[index] = (uint8_t)(value >> (8U * index));
    }
}

static void start_read(uint16_t tail, uint16_t head)
{
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, head);
    scp_pack_u16(&packet.data[2], 1U);
    put_u32(&packet.data[4], 100U + head);
    scp_pack_u16(&packet.data[8], tail);
    read_head = packet;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(tail, scp_unpack_u16(request.data));
}

static void confirm_consumption(void)
{
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = read_head;

    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
}

/* Keep ordinal and slot metadata consistent for the fresh HEAD check. */
static bool second_binding_active;
static rf_inventory_entry_t second_binding_storage;

static bool binding_entry(uint8_t source, rf_inventory_entry_t *entry,
                          int call_count)
{
    (void)call_count;
    if (second_binding_active &&
        (source == (uint8_t)((second_binding_storage.feeder << 2U) |
                             second_binding_storage.phase)))
    {
        if (rf_eui64_is_zero(second_binding_storage.eui64))
        {
            return false;
        }
        *entry = second_binding_storage;
        return true;
    }
    if (source != (uint8_t)((binding_entry_storage.feeder << 2U) |
                            binding_entry_storage.phase))
    {
        if (hash_collision && (6U == source))
        {
            *entry = binding_entry_storage;
            entry->phase = 2U;
            return true;
        }
        return false;
    }
    if (rf_eui64_is_zero(binding_entry_storage.eui64))
    {
        return false;
    }
    *entry = binding_entry_storage;
    return true;
}

static void verify_head_then_expect_consume(uint16_t batch_tail)
{
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = read_head;

    scp_pack_u16(&packet.data[8], batch_tail);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
}

void setUp(void)
{
    rf_init();
    rf_faults_init();
    rf_alarm_init();
    binding_entry_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U},
        .line_index = 2U
    };
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    tick = 0U;
    requests = 0U;
    raw_writes = 0U;
    fault_writes = 0U;
    raw_success = true;
    fault_success = true;
    sync_result = 0;
    response_handler = NULL;
    link_active = false;
    emit_success = true;
    emit_calls = 0U;
    replay_writes = 0U;
    replay_success = true;
    alarm_success = true;
    hash_collision = false;
    alarm_writes = 0U;
    alarm_emit_calls = 0U;
    second_binding_active = false;
    (void)memset(&second_binding_storage, 0, sizeof(second_binding_storage));
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
    iec104_event_log_add_StubWithCallback(save_replay);
    iec104_replay_send_record_StubWithCallback(send_replay);
    iec104_event_log_add_alarm_StubWithCallback(save_alarm);
    iec104_event_log_sync_IgnoreAndReturn(0);
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
    TEST_ASSERT_EQUAL_UINT8(0U, written_fault.info.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT16(37U, (uint16_t)(read_u32(request.data) % 100U));
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
}

void test_temporary_and_rf_requested_opening_keep_full_32_bit_duration(void)
{
    const rf_scp_event_t opened = {.zone = 1U, .feeder = 1U, .phase = 1U,
        .event = 6U, .boot_counter = 3060U, .uptime_sec = 6350U};
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&opened));
    start_read(36U, 38U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 142U;
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
    TEST_ASSERT_EQUAL_UINT8(1U, written_fault.info.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT8(1U, written_fault.info.power_status);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOG_TYPE_PERMANENT,
                            written_fault.info.type);
    TEST_ASSERT_EQUAL_UINT32(2U, fault_writes);
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT16(38U, (uint16_t)(read_u32(request.data) % 100U));
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
    TEST_ASSERT_EQUAL_STRING("storage", rf_events_drain_reason());
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
    verify_head_then_expect_consume(99U);
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(read_u32(request.data) % 100U));
    TEST_ASSERT_EQUAL_UINT32(0U, raw_writes);
}

void test_valid_record_at_slot_99_consumes_to_zero(void)
{
    start_read(99U, 0U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT16(0U, (uint16_t)(read_u32(request.data) % 100U));
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
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT16(37U, (uint16_t)(read_u32(request.data) % 100U));
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
    verify_head_then_expect_consume(36U);
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
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT16(37U, (uint16_t)(read_u32(request.data) % 100U));
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
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT16(40U, (uint16_t)(read_u32(request.data) % 100U));
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
    confirm_consumption();
    packet.type = SCP_TYPE_ERROR;
    packet.data_len = 1U;
    packet.data[0] = RF_SCP_ERR_INVALID_PARAM;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(5U, requests);
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

/* BOLATeX BQ-02: the ring keeps at most 99 unconsumed records, so an
 * equal head and tail with a pending=100 hint is the EMPTY ring; the
 * cycle stops without issuing a range read. */
void test_equal_head_and_tail_with_full_hint_means_empty_ring(void)
{
    rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 100U, .head = 37U}
    };

    rf_events_notify(&message);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 37U);       /* head = 37 */
    scp_pack_u16(&packet.data[8], 37U);   /* tail = 37 */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(SCP_TYPE_GET, request.type);
    /* No 0x44 follows: the machine went idle (next 0x40 would be a new
     * cycle). Feeding one tick must not produce a range read. */
    rf_events_process(tick + 60001U);
    TEST_ASSERT_NOT_EQUAL(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
}


/* ------------------ BOLATeX R0 answer coverage ------------------ */

/* BQ-01: a stored 101 record opens the latched trip-failure alarm for
 * the bound feeder/phase; it is not added to the fault list. */
void test_stored_101_opens_latched_trip_failure_alarm(void)
{
    rf_inventory_entry_t entry =
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .line_index = 2U,
        .eui64 = {0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9, 0xF1, 0x1A}
    };
    rf_phase_data_t data;

    rf_scp_live_t live = {.source = (1U << 2) | 1U, .seq = 1U,
                          .uptime_sec = 10U, .state = 1U};

    rf_init();
    binding_entry_storage = entry;
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, NULL));
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;               /* event id 101 */
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    TEST_ASSERT_TRUE(rf_get_source_data((1U << 2) | 1U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    TEST_ASSERT_FALSE(data.trip_failure_latched);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_EQUAL_UINT8(1U, written_alarm.active);
    TEST_ASSERT_EQUAL_UINT8(2U, written_alarm.feeder);
    verify_head_then_expect_consume(36U);
}

/* BQ-01: the same applies to a stored 105 record. */
void test_stored_105_opens_latched_trip_failure_alarm(void)
{
    rf_inventory_entry_t entry =
    {
        .zone = 1U, .feeder = 1U, .phase = 1U,
        .eui64 = {0x00, 0x12, 0x4B, 0x00, 0x38, 0xC9, 0xF1, 0x1A}
    };
    rf_phase_data_t data;

    rf_scp_live_t live = {.source = (1U << 2) | 1U, .seq = 1U,
                          .uptime_sec = 10U, .state = 1U};

    rf_init();
    binding_entry_storage = entry;
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, NULL));
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 105U;               /* event id 105 */
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(0U, fault_writes);
    TEST_ASSERT_TRUE(rf_get_source_data((1U << 2) | 1U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failure_latched);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
}

/* BQ-03: the boot protection reads the head, then sends a no-op consume
 * at the reported tail; on completion it hands over to the inventory. */
void test_boot_protect_consumes_reported_tail_then_starts_inventory(void)
{
    inventory_status = RF_INVENTORY_IDLE;
    rf_events_init();
    TEST_ASSERT_TRUE(rf_events_boot_protect());
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 12U);       /* head */
    scp_pack_u16(&packet.data[8], 9U);    /* tail */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(4234U, read_u32(request.data));
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 9U);        /* new tail */
    scp_pack_u16(&packet.data[2], 3U);    /* No-op preserves pending records. */
    rf_inventory_start_ExpectAndReturn(true);
    respond(&packet);
    rf_events_process(tick);
}

/* Failed protection must not enable AY writes by uploading inventory. */
void test_boot_protect_failure_keeps_inventory_paused(void)
{
    inventory_status = RF_INVENTORY_IDLE;
    rf_events_init();
    TEST_ASSERT_TRUE(rf_events_boot_protect());
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 150U);      /* invalid head */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, requests);
    rf_events_process(tick + 60000U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
}

void test_empty_boot_ring_still_sends_noop_consume_before_inventory(void)
{
    inventory_status = RF_INVENTORY_IDLE;
    TEST_ASSERT_TRUE(rf_events_boot_protect());
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 9U);
    scp_pack_u16(&packet.data[8], 9U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(4237U, read_u32(request.data));
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 9U);
    scp_pack_u16(&packet.data[2], 0U);
    rf_inventory_start_ExpectAndReturn(true);
    respond(&packet);
}

/* BQ-05: a decreasing total between two 0x40 replies is only reported;
 * pulling still resumes from the current tail. */
void test_smaller_total_reports_store_reset_and_continues(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    respond(&packet);
    /* New cycle: head reports a SMALLER total (store cleared). */
    rf_events_process(tick + 60001U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    packet.data[4] = 1U;                    /* total dropped (LE) */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
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
    TEST_ASSERT_NOT_EQUAL(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
}

void test_online_fault_queue_acceptance_does_not_mark_persistent_record_sent(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    link_active = true;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, emit_calls);
    verify_head_then_expect_consume(36U);
}

void test_failed_online_send_remains_in_replay_without_blocking_consumption(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    link_active = true;
    emit_success = false;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, emit_calls);
    verify_head_then_expect_consume(36U);
}

void test_replay_sync_failure_retries_without_appending_or_sending_again(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    iec104_event_log_sync_StopIgnore();
    iec104_event_log_sync_ExpectAndReturn(-1);
    iec104_event_log_sync_ExpectAndReturn(0);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, replay_writes);
    confirm_consumption();
}

void test_replay_write_failure_holds_tail_and_keeps_both_previous_writes(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    replay_success = false;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(2U, requests);
    replay_success = true;
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT32(1U, raw_writes);
    TEST_ASSERT_EQUAL_UINT32(1U, fault_writes);
    TEST_ASSERT_EQUAL_UINT32(2U, replay_writes);
    confirm_consumption();
}

void test_advanced_tail_inside_saved_prefix_consumes_only_remaining_prefix(void)
{
    start_read(36U, 38U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data_len = 120U;
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    verify_head_then_expect_consume(37U);
    TEST_ASSERT_EQUAL_UINT16(38U, (uint16_t)(read_u32(request.data) % 100U));
}

void test_tail_already_at_saved_end_does_not_consume(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    packet = read_head;
    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    const size_t before = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
}

void test_error06_at_current_head_does_not_skip_or_consume(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = {.type = SCP_TYPE_ERROR, .data_len = 1U,
        .data = {RF_SCP_ERR_RECORD_INVALID}};
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = read_head;
    scp_pack_u16(packet.data, 36U);
    scp_pack_u16(&packet.data[8], 36U);
    respond(&packet);
    const size_t before = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
    TEST_ASSERT_EQUAL_size_t(0U, raw_writes);
}

void test_reset_during_saved_batch_reanchors_without_consuming_old_cursor(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    packet = read_head;
    put_u32(&packet.data[4], 136U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(36U, scp_unpack_u16(request.data));
}

void test_event_only_alarm_can_be_acknowledged_without_live_data(void)
{
    binding_entry_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U}
    };
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    rf_init();
    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.has_live);
    TEST_ASSERT_TRUE(data.trip_failed);
    TEST_ASSERT_TRUE(rf_ack_trip_failure(5U));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    TEST_ASSERT_FALSE(rf_ack_trip_failure(5U));
}

void test_event_latch_clears_on_observed_live_one_to_zero_transition(void)
{
    binding_entry_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U}
    };
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    rf_init();
    const rf_scp_live_t initial = {.source = 5U, .uptime_sec = 10U};
    TEST_ASSERT_TRUE(rf_handle_live(&initial, tick, NULL));
    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    rf_scp_live_t sample = {.source = 5U, .uptime_sec = 20U};
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failed); /* Last zero alone is insufficient. */
    sample.trip_failed = true;
    sample.uptime_sec++;
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    sample.trip_failed = false;
    sample.uptime_sec++;
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    TEST_ASSERT_FALSE(data.trip_failure_latched);
}

static scp_packet_t alarm_packet(void)
{
    binding_entry_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U},
        .line_index = 2U
    };
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 105U;
    update_crc(&packet);
    return packet;
}

void test_failed_alarm_queue_holds_tail_and_does_not_acknowledge_receipt(void)
{
    scp_packet_t packet = alarm_packet();
    start_read(36U, 37U);
    respond(&packet);
    alarm_success = false;
    rf_events_process(tick);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failure_latched);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    alarm_success = true;
    tick = 59999U;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    tick++;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failure_latched);
    confirm_consumption();
}

void test_alarm_sync_failure_retries_without_adding_or_sending_twice(void)
{
    scp_packet_t packet = alarm_packet();
    link_active = true;
    iec104_event_log_sync_StopIgnore();
    iec104_event_log_sync_ExpectAndReturn(-1);
    iec104_event_log_sync_ExpectAndReturn(0);
    start_read(36U, 37U);
    respond(&packet);
    rf_events_process(tick);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failure_latched);
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_emit_calls);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failure_latched);
}

void test_acknowledged_duplicate_alarm_does_not_reopen_or_enqueue_again(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, packet.data_len, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    rf_hub_restarted();
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    put_u32(&packet.data[51], event.uptime_sec + 1U);
    update_crc(&packet);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, packet.data_len, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
}

void test_alarm_identity_cache_keeps_128_entries_and_resets_only_with_rtu(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t first;
    rf_scp_event_t last;
    for (uint32_t index = 0U; 129U > index; index++)
    {
        put_u32(&packet.data[51], 1000U + index);
        update_crc(&packet);
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
            rf_scp_decode_event(packet.data, packet.data_len, &last));
        if (0U == index)
        {
            first = last;
        }
        TEST_ASSERT_TRUE(rf_handle_alarm_event(&last));
        TEST_ASSERT_TRUE(rf_ack_stored_alarm(binding_entry_storage.eui64));
    }
    TEST_ASSERT_FALSE(rf_handle_alarm_event(&last));
    TEST_ASSERT_TRUE(rf_handle_alarm_event(&first)); /* Oldest was evicted. */
    rf_init();
    TEST_ASSERT_TRUE(rf_handle_alarm_event(&last));
}

void test_live_alarm_is_acknowledged_only_after_storage_and_remains_ongoing(void)
{
    (void)alarm_packet();
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 10U,
                          .trip_failed = true};
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(5U, &time);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failure_latched);
    rf_alarm_process(tick);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failed);
    TEST_ASSERT_FALSE(data.trip_failure_latched);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    live.uptime_sec++;
    live.trip_failed = false;
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(5U, &time);
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_EQUAL_UINT8(0U, written_alarm.active);
}

void test_inventory_drain_records_a_fresh_empty_head_after_consumption(void)
{
    rf_events_request_drain();
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    confirm_consumption();
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = read_head;
    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    uint32_t total = 0U;
    TEST_ASSERT_TRUE(rf_events_drain_complete(&total));
    TEST_ASSERT_EQUAL_UINT32(137U, total);
    const rf_scp_message_t bell = {.cmd = RF_SCP_CMD_LOG_AVAILABLE,
        .type = SCP_TYPE_SET, .body.log_available = {.pending = 1U, .head = 38U}};
    rf_events_notify(&bell);
    TEST_ASSERT_FALSE(rf_events_drain_complete(&total));
}

void test_r2_unknown_source_preserves_raw_without_fault_or_alarm(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    scp_pack_u16(&packet.data[56], 0x1787U);
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
    verify_head_then_expect_consume(36U);
    TEST_ASSERT_EQUAL_UINT32(137U, read_u32(request.data));
}

void test_r2_142_without_previous_sequence_is_a_temporary_fault(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 142U;
    put_u32(&packet.data[23], 0U);
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, fault_writes);
    TEST_ASSERT_EQUAL_UINT8(FAULT_LOG_TYPE_TEMPORARY, written_fault.info.type);
    TEST_ASSERT_EQUAL_UINT32(0U, written_fault.fault_duration_ms);
}

void test_r2_wrap_rollover_preserves_saved_batch_consume_target(void)
{
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 99U);
    scp_pack_u16(&packet.data[2], UINT16_MAX);
    put_u32(&packet.data[4], 6553599U);
    scp_pack_u16(&packet.data[8], 98U);
    respond(&packet);
    rf_events_process(tick);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 0U);
    scp_pack_u16(&packet.data[2], 0U);
    put_u32(&packet.data[4], 6553600U);
    scp_pack_u16(&packet.data[8], 98U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
    TEST_ASSERT_EQUAL_UINT32(6553599U, read_u32(request.data));
}

void test_r2_new_live_boot_does_not_resolve_a_previous_alarm(void)
{
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 10U,
        .boot_counter = 1U, .trip_failed = true};
    TEST_ASSERT_TRUE(rf_handle_live(&live, 0U, NULL));
    live.boot_counter = 2U;
    live.uptime_sec = 11U;
    live.trip_failed = false;
    TEST_ASSERT_TRUE(rf_handle_live(&live, 1000U, NULL));
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, 1000U, &data));
    TEST_ASSERT_TRUE(data.trip_failed);
    TEST_ASSERT_TRUE(data.trip_failure_latched);
}

void test_r2_source_collision_and_unknown_identity_cannot_reopen_alarm(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_event_source_matches(&event));
    TEST_ASSERT_TRUE(rf_handle_alarm_event(&event));
    TEST_ASSERT_TRUE(rf_ack_stored_alarm(binding_entry_storage.eui64));
    hash_collision = true;
    TEST_ASSERT_FALSE(rf_event_source_matches(&event));
    TEST_ASSERT_FALSE(rf_handle_alarm_event(&event));
    hash_collision = false;
    event.src_eui_hash = 0U;
    TEST_ASSERT_FALSE(rf_event_source_matches(&event));
    TEST_ASSERT_FALSE(rf_handle_alarm_event(&event));
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, 0U, &data));
    TEST_ASSERT_FALSE(data.trip_failure_latched);
}

void test_r2_service_records_keep_reset_and_epoch_observations_separate(void)
{
    rf_scp_event_t event = {.event = 138U, .fault_count = 3U};
    rf_record_service_event(&event, false);
    event = (rf_scp_event_t){.event = 201U, .feeder = 2U,
        .permanent_faults = 7U, .duration_ms = 19600U};
    rf_record_service_event(&event, false);
    event = (rf_scp_event_t){.event = 143U, .fault_count = 4U};
    rf_record_service_event(&event, false);
    rf_service_status_t status;
    rf_get_service_status(&status);
    TEST_ASSERT_EQUAL_UINT8(3U, status.store_reset_reason);
    TEST_ASSERT_EQUAL_UINT8(4U, status.counter_status);
    TEST_ASSERT_EQUAL_UINT8(2U, status.epoch_feeder);
    TEST_ASSERT_EQUAL_UINT8(7U, status.epoch_phases);
    TEST_ASSERT_EQUAL_UINT32(19600U, status.epoch_duration_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, status.source_mismatches);
}

void test_r2_verified_unassigned_117_keeps_separate_count_without_mapping(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 117U;
    packet.data[9] = 0U;
    packet.data[10] = 0U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    rf_fault_stats_t stats;
    TEST_ASSERT_TRUE(rf_faults_get_stats(1U, 0U, &stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.permanent);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
}

void test_alarm_boot_zero_is_not_deduplicated_after_receipt_ack(void)
{
    scp_packet_t packet = alarm_packet();
    scp_pack_u16(&packet.data[49], 0U);
    update_crc(&packet);
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
}

void test_alarm_retry_deadline_preserves_pending_record_across_tick_wrap(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    tick = UINT32_MAX - 1000U;
    alarm_success = false;
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    const uint32_t failed_at = tick;
    alarm_success = true;
    tick = failed_at + 59999U;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    tick++;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_alarm_unrelated_or_null_event_never_writes_or_opens_a_latch(void)
{
    rf_scp_event_t event = {.event = 142U, .zone = 1U,
                            .feeder = 1U, .phase = 1U};
    TEST_ASSERT_FALSE(rf_alarm_record(NULL));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    rf_alarm_live(5U, NULL);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
}

void test_live_invalid_numbers_do_not_make_fresh_rf_communication_stale(void)
{
    const float values[] = {NAN, INFINITY, -INFINITY, -1.0F};
    for (size_t index = 0U; 4U > index; index++)
    {
        const rf_scp_live_t live = {.source = 5U,
            .uptime_sec = (uint32_t)index + 1U, .boot_counter = 1U,
            .current_amps = values[index], .flags = 3U};
        TEST_ASSERT_TRUE(rf_handle_live(&live, tick, NULL));
        rf_phase_data_t data;
        TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
        TEST_ASSERT_TRUE(data.is_online);
        TEST_ASSERT_FALSE(data.current_valid);
        TEST_ASSERT_EQUAL_UINT8(3U, data.live.flags);
    }
}

void test_live_stall_invalidates_measurement_then_next_uptime_recovers(void)
{
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 7U,
        .boot_counter = 1U, .current_amps = 10.0F};
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, NULL));
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick + 1U, NULL));
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick + 1U, &data));
    TEST_ASSERT_TRUE(data.uptime_stalled);
    TEST_ASSERT_FALSE(data.current_valid);
    live.uptime_sec++;
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick + 2U, NULL));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick + 2U, &data));
    TEST_ASSERT_FALSE(data.uptime_stalled);
    TEST_ASSERT_TRUE(data.current_valid);
}

void test_live_invalid_input_preserves_previous_measurements(void)
{
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 7U,
        .boot_counter = 1U, .current_amps = 10.0F};
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, NULL));
    rf_phase_data_t before;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &before));
    TEST_ASSERT_FALSE(rf_handle_live(NULL, tick, NULL));
    live.state = 7U;
    TEST_ASSERT_FALSE(rf_handle_live(&live, tick, NULL));
    live.state = 0U;
    live.log_pending = 101U;
    TEST_ASSERT_FALSE(rf_handle_live(&live, tick, NULL));
    rf_phase_data_t after;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(after));
    TEST_ASSERT_FALSE(rf_get_source_data(5U, tick, NULL));
    TEST_ASSERT_FALSE(rf_get_phase_data(0U, PHASE_L1, tick, NULL));
}

void test_anomaly_global_reset_clears_every_phase_and_both_paths(void)
{
    for (uint8_t feeder_id = 1U; 4U >= feeder_id; feeder_id++)
    {
        for (uint8_t phase = 1U; 3U >= phase; phase++)
        {
            for (uint8_t path = 0U; 2U > path; path++)
            {
                const rf_scp_anomaly_t report = {.source =
                    (uint8_t)((feeder_id << 2U) | phase),
                    .state = 1U, .path = path, .window = 55U, .total = 99U};
                TEST_ASSERT_TRUE(rf_handle_anomaly(&report));
            }
        }
    }
    const rf_scp_anomaly_t global = {.source = 0xFFU, .state = 1U};
    TEST_ASSERT_TRUE(rf_handle_anomaly(&global));
    const rf_scp_anomaly_t reset = {.source = 0xFFU, .state = 2U};
    TEST_ASSERT_TRUE(rf_handle_anomaly(&reset));
    for (uint8_t source = 5U; 20U > source; source++)
    {
        if (0U == (source & 3U))
        {
            continue;
        }
        for (uint8_t path = 0U; 2U > path; path++)
        {
            rf_anomaly_data_t data;
            TEST_ASSERT_TRUE(rf_get_anomaly(source, path, &data));
            TEST_ASSERT_FALSE(data.has_report);
            TEST_ASSERT_FALSE(data.is_active);
            TEST_ASSERT_EQUAL_UINT32(0U, data.total);
        }
    }
    rf_anomaly_data_t data;
    TEST_ASSERT_TRUE(rf_get_anomaly(0xFFU, 0U, &data));
    TEST_ASSERT_FALSE(data.has_report);
}

void test_anomaly_invalid_inputs_preserve_last_valid_report_and_output(void)
{
    rf_scp_anomaly_t report = {.source = 5U, .state = 1U,
        .path = 1U, .window = 55U, .total = 99U};
    TEST_ASSERT_TRUE(rf_handle_anomaly(&report));
    report.state = 3U;
    TEST_ASSERT_FALSE(rf_handle_anomaly(&report));
    report.state = 1U;
    report.path = 2U;
    TEST_ASSERT_FALSE(rf_handle_anomaly(&report));
    report.path = 1U;
    report.source = 0U;
    TEST_ASSERT_FALSE(rf_handle_anomaly(&report));
    TEST_ASSERT_FALSE(rf_handle_anomaly(NULL));
    rf_anomaly_data_t data;
    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 1U, &data));
    TEST_ASSERT_TRUE(data.is_active);
    TEST_ASSERT_EQUAL_UINT32(99U, data.total);
    const rf_anomaly_data_t before = data;
    TEST_ASSERT_FALSE(rf_get_anomaly(5U, 2U, &data));
    TEST_ASSERT_FALSE(rf_get_anomaly(0U, 1U, &data));
    TEST_ASSERT_FALSE(rf_get_anomaly(5U, 1U, NULL));
    TEST_ASSERT_EQUAL_MEMORY(&before, &data, sizeof(data));
}

void test_wrong_ordinal_consume_ack_requires_fresh_head_before_continuing(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    confirm_consumption();
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    put_u32(&packet.data[4], 138U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
}

void test_event_drain_lifecycle_rejects_null_and_finish_clears_ready(void)
{
    TEST_ASSERT_FALSE(rf_events_drain_complete(NULL));
    rf_events_request_drain();
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 36U);
    scp_pack_u16(&packet.data[8], 36U);
    respond(&packet);
    uint32_t total = 0U;
    TEST_ASSERT_TRUE(rf_events_drain_complete(&total));
    rf_events_finish_drain();
    total = 123U;
    TEST_ASSERT_FALSE(rf_events_drain_complete(&total));
    TEST_ASSERT_EQUAL_UINT32(123U, total);
}

void test_alarm_without_cached_line_index_uses_matching_active_store_line(void)
{
    scp_packet_t packet = alarm_packet();
    binding_entry_storage.line_index = UINT8_MAX;
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_UINT8(2U, written_alarm.feeder);
    TEST_ASSERT_EQUAL_UINT8(0U, written_alarm.phase);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
}

void test_alarm_without_matching_active_store_line_is_not_acknowledged(void)
{
    scp_packet_t packet = alarm_packet();
    binding_entry_storage.line_index = UINT8_MAX;
    feeder.in_use = false;
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, tick, &data));
    TEST_ASSERT_TRUE(data.trip_failure_latched);
}

void test_live_alarm_activation_and_resolution_both_reach_persistent_queue(void)
{
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 1U,
        .boot_counter = 1U, .trip_failed = true};
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(5U, &time);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    rf_alarm_process(tick);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    TEST_ASSERT_EQUAL_UINT8(1U, written_alarm.active);
    live.trip_failed = false;
    live.uptime_sec++;
    TEST_ASSERT_TRUE(rf_handle_live(&live, ++tick, &time));
    rf_alarm_process(tick);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    rf_alarm_process(tick);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    TEST_ASSERT_EQUAL_UINT8(0U, written_alarm.active);
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
}

/* ------------------ R2 edge regressions, 12.10.2026 ------------------ */

void test_boot_protect_rejects_busy_restart_and_gates_inventory_upload(void)
{
    inventory_status = RF_INVENTORY_LOADING;
    rf_events_init();
    TEST_ASSERT_FALSE(rf_events_inventory_allowed());
    TEST_ASSERT_TRUE(rf_events_boot_protect());
    TEST_ASSERT_FALSE(rf_events_boot_protect());
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 12U);
    scp_pack_u16(&packet.data[8], 9U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 9U);
    scp_pack_u16(&packet.data[2], 3U);
    rf_inventory_start_ExpectAndReturn(true);
    respond(&packet);
    TEST_ASSERT_TRUE(rf_events_inventory_allowed());
}

void test_drain_reason_reports_storage_while_persisting(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    raw_success = false;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_STRING("storage", rf_events_drain_reason());
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    TEST_ASSERT_EQUAL_STRING("storage", rf_events_drain_reason());
}

void test_drain_reason_reports_degraded_hub_storage(void)
{
    rf_events_process(tick);
    scp_packet_t busy = {.type = SCP_TYPE_ERROR, .data_len = 2U,
                         .data = {RF_SCP_ERR_BUSY, 0U}};

    respond(&busy);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_GET_FRAM_STATS, request.cmd);
    scp_packet_t packet = {.cmd = RF_SCP_CMD_GET_FRAM_STATS,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    packet.data[6] = 1U;
    respond(&packet);
    TEST_ASSERT_EQUAL_STRING("mh_degraded", rf_events_drain_reason());
    rf_events_process(tick + 60001U);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
}

void test_late_response_after_idle_does_not_restart_a_cycle(void)
{
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 37U);
    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    TEST_ASSERT_EQUAL_STRING("mh_records", rf_events_drain_reason());
    const size_t before = requests;
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
}

void test_fault_line_falls_back_to_matching_store_feeder_when_zone_differs(void)
{
    binding_entry_storage.line_index = UINT8_MAX;
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, fault_writes);
    /* No cached line index: the matching store feeder line 2 must win. */
    TEST_ASSERT_EQUAL_UINT8(2U, written_fault.info.feeder);
}

void test_fault_without_any_line_match_is_kept_raw_only(void)
{
    binding_entry_storage.line_index = UINT8_MAX;
    feeder.in_use = false;
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    /* Without a mapped alarm line the record is not acknowledged, so the
     * MH tail is held until a line mapping exists. */
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
}

void test_fault_with_invalid_load_flag_is_kept_raw_only(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;
    packet.data[13] = 2U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
}

void test_fault_with_nan_current_is_kept_raw_only(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;
    /* IEEE-754 NaN (0x7FC00000) in the little-endian current field. */
    packet.data[15] = 0x00U;
    packet.data[16] = 0x00U;
    packet.data[17] = 0xC0U;
    packet.data[18] = 0x7FU;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
}

void test_clock_quality_zero_uses_local_time_with_invalid_marker(void)
{
    const bsp_rtc_t rtc = {.millisec = 1000U, .second = 12U, .minute = 10U,
        .hour = 7U, .day = 3U, .month = 10U, .year = 26U};
    bsp_get_datetime_IgnoreAndReturn(rtc);
    scp_packet_t packet = alarm_packet();

    packet.data[7] = 101U;
    packet.data[55] = 0U;
    update_crc(&packet);
    start_read(36U, 37U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, fault_writes);
    TEST_ASSERT_TRUE(written_fault.tm.iv_bit);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_TRUE(written_alarm.time.iv_bit);
}

void test_alarm_record_with_invalid_or_unbound_source_stays_raw_only(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;

    packet.data[9] = 0U;               /* feeder 0 */
    update_crc(&packet);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    packet.data[9] = 5U;               /* feeder above the supported range */
    update_crc(&packet);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    packet.data[9] = 2U;               /* valid feeder, no binding */
    update_crc(&packet);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    packet.data[9] = 1U;
    packet.data[10] = 0U;              /* phase zero */
    update_crc(&packet);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_alarm_backoff_and_binding_change_keep_pending_delivery(void)
{
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    alarm_success = false;
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    /* Inside the retry window no new storage attempt is made. */
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    /* After the window a still-failing store retries the pending record. */
    tick = 60000U;
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    /* After the window a changed EUI on the same slot is not the same job. */
    alarm_success = true;
    tick += 60000U;
    binding_entry_storage.eui64[7] = 9U;
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    /* The original pending record still delivers for its own EUI. */
    binding_entry_storage.eui64[7] = 0U;
    tick += 60000U;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(3U, alarm_writes);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_alarm_process_stops_retrying_at_the_first_failing_source(void)
{
    (void)alarm_packet();
    second_binding_active = true;
    second_binding_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 2U, .phase = 1U, .eui64 = {3U},
        .line_index = 2U
    };
    rf_init();
    scp_packet_t packet = alarm_packet();
    rf_scp_event_t event;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, 60U, &event));
    alarm_success = false;
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    rf_scp_live_t live = {.source = 9U, .uptime_sec = 10U,
                          .boot_counter = 1U, .trip_failed = true};
    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(9U, &time);
    tick = 60000U;
    rf_alarm_process(tick);
    /* Only the first failing source is retried in this pass. */
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    alarm_success = true;
    tick += 60000U;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(4U, alarm_writes);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_alarm_process_without_live_timestamp_still_queues_local_time(void)
{
    const bsp_rtc_t rtc = {.millisec = 1000U, .second = 12U, .minute = 10U,
        .hour = 7U, .day = 3U, .month = 10U, .year = 26U};
    bsp_get_datetime_IgnoreAndReturn(rtc);
    binding_entry_storage = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U}
    };
    rf_inventory_get_binding_StubWithCallback(binding_entry);
    rf_init();
    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    rf_alarm_process(tick);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_EQUAL_UINT8(1U, written_alarm.active);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_alarm_live_repeat_with_unchanged_trip_state_does_not_requeue(void)
{
    (void)alarm_packet();
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 10U,
                          .trip_failed = true};

    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(5U, &time);
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    rf_alarm_live(5U, &time);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
}

void test_notify_rejects_out_of_range_pending_or_head_values(void)
{
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 37U);
    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    tick = 59999U;
    const rf_scp_message_t bad_pending =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 100U, .head = 37U}
    };
    rf_events_notify(&bad_pending);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, requests);
    const rf_scp_message_t good =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 1U, .head = 38U}
    };
    rf_events_notify(&good);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 38U);
    scp_pack_u16(&packet.data[8], 38U);
    respond(&packet);
    const rf_scp_message_t bad_head =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 1U, .head = 100U}
    };
    rf_events_notify(&bad_head);
    tick = 60000U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
}

/* --------- R2 dead-branch regressions, 12.10.2026 tur 2 --------- */

void test_notify_ignores_null_and_foreign_messages(void)
{
    rf_events_notify(NULL);
    const rf_scp_message_t wrong_type =
    {
        .cmd = RF_SCP_CMD_LOG_AVAILABLE, .type = SCP_TYPE_ACK,
        .body.log_available = {.pending = 1U, .head = 38U}
    };
    rf_events_notify(&wrong_type);
    const rf_scp_message_t wrong_cmd =
    {
        .cmd = RF_SCP_CMD_LIVE_DATA, .type = SCP_TYPE_SET,
        .body.log_available = {.pending = 1U, .head = 38U}
    };
    rf_events_notify(&wrong_cmd);
    tick = 59999U;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, requests);
}

void test_oversized_batch_stops_cycle_without_storing_or_consuming(void)
{
    start_read(36U, 37U);   /* One pending record is requested. */
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data_len = 120U;   /* The hub returned two records. */
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, raw_writes);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
}

void test_verify_head_reanchors_when_total_advances_a_full_wrap(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);      /* Store the record. */
    rf_events_process(tick);      /* Fresh HEAD before consume. */
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = read_head;
    /* Same modular position, but the lifetime total jumped one modulus. */
    put_u32(&packet.data[4], 137U + 6553600U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_RANGE, request.cmd);
    TEST_ASSERT_EQUAL_UINT16(36U, scp_unpack_u16(request.data));
}

void test_ack_without_packet_stops_the_cycle(void)
{
    rf_events_process(tick);      /* HEAD request in flight. */
    TEST_ASSERT_NOT_NULL(response_handler);
    response_handler(SCP_CMD_OK, NULL);
    const size_t before = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
}

void test_undecodable_ok_reply_stops_the_cycle(void)
{
    rf_events_process(tick);      /* HEAD request in flight. */
    scp_packet_t garbage = {.dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = 0x29U, .data_len = 0U};

    TEST_ASSERT_NOT_NULL(response_handler);
    response_handler(SCP_CMD_OK, &garbage);
    const size_t before = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
}

void test_consume_error_replies_without_body_or_decodable_ok_stop(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    scp_packet_t empty = {.type = SCP_TYPE_ERROR, .data_len = 0U};
    respond(&empty);
    const size_t after_empty = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(after_empty, requests);

    rf_events_init();
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    scp_packet_t garbage = {.dst = RF_SCP_ADDR_RTU, .src = RF_SCP_ADDR_HUB,
        .type = SCP_TYPE_ACK, .cmd = 0x29U, .data_len = 0U};
    respond(&garbage);            /* OK result that cannot be decoded. */
    const size_t after_garbage = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(after_garbage, requests);
}

void test_busy_reply_while_probing_fram_stops_the_cycle(void)
{
    rf_events_process(tick);      /* HEAD request. */
    scp_packet_t busy = {.type = SCP_TYPE_ERROR, .data_len = 2U,
                         .data = {RF_SCP_ERR_BUSY, 0U}};

    respond(&busy);               /* -> FRAM probe. */
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_GET_FRAM_STATS, request.cmd);
    scp_packet_t again = busy;
    respond(&again);              /* Degenerate hub: still busy. */
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    /* The normal poll window retries with a fresh head. */
    rf_events_process(tick + 60001U);
    TEST_ASSERT_EQUAL_size_t(3U, requests);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
}

void test_healthy_fram_stats_reply_stops_the_cycle_cleanly(void)
{
    rf_events_process(tick);      /* HEAD request. */
    scp_packet_t busy = {.type = SCP_TYPE_ERROR, .data_len = 2U,
                         .data = {RF_SCP_ERR_BUSY, 0U}};

    respond(&busy);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_GET_FRAM_STATS, request.cmd);
    scp_packet_t healthy = {.cmd = RF_SCP_CMD_GET_FRAM_STATS,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    scp_pack_u16(healthy.data, 10U);
    scp_pack_u16(&healthy.data[4], 90U);
    respond(&healthy);
    TEST_ASSERT_EQUAL_STRING("mh_records", rf_events_drain_reason());
    rf_events_process(tick + 60001U);
    TEST_ASSERT_EQUAL_size_t(3U, requests);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
}

void test_error_replies_without_matching_code_stop_the_cycle(void)
{
    start_read(36U, 37U);
    scp_packet_t error = {.type = SCP_TYPE_ERROR, .data_len = 1U,
                          .data = {RF_SCP_ERR_NOT_AVAILABLE}};

    respond(&error);              /* Not RECORD_INVALID during a read. */
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, requests);
    TEST_ASSERT_EQUAL_size_t(0U, raw_writes);
    /* Empty error body is equally unmatched. */
    rf_events_init();
    start_read(36U, 37U);
    scp_packet_t empty = {.type = SCP_TYPE_ERROR, .data_len = 0U};
    respond(&empty);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(4U, requests);
}

void test_consume_invalid_param_error_requires_fresh_head(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    scp_packet_t error = {.type = SCP_TYPE_ERROR, .data_len = 1U,
                          .data = {RF_SCP_ERR_INVALID_PARAM}};
    respond(&error);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    TEST_ASSERT_EQUAL_size_t(5U, requests);
}

void test_consume_unmatched_error_stops_the_cycle(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    scp_packet_t error = {.type = SCP_TYPE_ERROR, .data_len = 1U,
                          .data = {RF_SCP_ERR_NOT_AVAILABLE}};
    respond(&error);
    const size_t before = requests;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(before, requests);
}

void test_protect_consume_with_moved_tail_fails_protection(void)
{
    inventory_status = RF_INVENTORY_IDLE;
    rf_events_init();
    TEST_ASSERT_TRUE(rf_events_boot_protect());
    rf_events_process(tick);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);

    scp_pack_u16(packet.data, 12U);
    scp_pack_u16(&packet.data[8], 9U);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_CONSUME_IF, request.cmd);
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 10U);      /* Tail moved elsewhere. */
    scp_pack_u16(&packet.data[2], 2U);
    respond(&packet);
    TEST_ASSERT_FALSE(rf_events_inventory_allowed());
    rf_events_process(tick + 60001U);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
}

void test_drain_continues_while_records_remain_even_at_left_zero(void)
{
    rf_events_request_drain();
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    respond(&packet);
    rf_events_process(tick);
    verify_head_then_expect_consume(36U);
    packet = capture(RF_SCP_CMD_LOG_CONSUME_IF, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 37U);
    scp_pack_u16(&packet.data[2], 0U);   /* Nothing left, keep draining. */
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
    packet = read_head;
    scp_pack_u16(&packet.data[8], 37U);
    respond(&packet);
    uint32_t total = 0U;
    TEST_ASSERT_TRUE(rf_events_drain_complete(&total));
    TEST_ASSERT_EQUAL_UINT32(137U, total);
}

void test_fault_line_scan_skips_store_feeder_with_other_zone_or_feeder(void)
{
    binding_entry_storage.line_index = UINT8_MAX;
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    feeder.config.zone_id = 9U;          /* Same feeder number, other zone. */
    packet.data[7] = 101U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(1U, raw_writes);
    /* Zone matches again but the feeder number differs. */
    rf_events_init();
    feeder.config.zone_id = 1U;
    feeder.config.fider_id = 5U;
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    /* Restoring the identity lets the fallback find line 2 again. */
    rf_events_init();
    feeder.config.fider_id = 1U;
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, fault_writes);
    TEST_ASSERT_EQUAL_UINT8(2U, written_fault.info.feeder);
}

void test_fault_with_negative_or_huge_current_or_bad_energy_is_raw_only(void)
{
    const float negative = -1.0F;
    const float huge = 1.0e38F;

    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    (void)memcpy(&packet.data[15], &negative, 4U);
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);

    rf_events_init();
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    (void)memcpy(&packet.data[15], &huge, 4U);
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);

    rf_events_init();
    start_read(36U, 37U);
    packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    packet.data[7] = 101U;
    packet.data[14] = 2U;   /* Energy status outside {0,1}. */
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, fault_writes);
    TEST_ASSERT_EQUAL_size_t(3U, raw_writes);
}

void test_quality_one_event_with_invalid_timestamp_is_marked_invalid(void)
{
    start_read(36U, 37U);
    scp_packet_t packet = capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);

    packet.data[7] = 101U;
    (void)memset(packet.data, 0U, 7U);   /* Calendar fields all zero. */
    packet.data[55] = 1U;                /* Clock claims to be reliable. */
    update_crc(&packet);
    respond(&packet);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, fault_writes);
    TEST_ASSERT_TRUE(written_fault.tm.iv_bit);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_TRUE(written_alarm.time.iv_bit);
}

void test_events_process_waits_for_comm_and_inventory_states(void)
{
    rf_comm_can_load_inventory_StopIgnore();
    rf_comm_can_load_inventory_ExpectAndReturn(false);
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, requests);
    rf_comm_can_load_inventory_IgnoreAndReturn(true);

    inventory_status = RF_INVENTORY_IDLE;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, requests);
    inventory_status = RF_INVENTORY_ERROR;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(0U, requests);
    inventory_status = RF_INVENTORY_EMPTY;
    rf_events_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, requests);
    TEST_ASSERT_EQUAL_HEX8(RF_SCP_CMD_LOG_READ_HEAD, request.cmd);
}

void test_alarm_live_without_source_data_is_ignored(void)
{
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    rf_alarm_live(5U, &time);     /* Bound source, no live sample yet. */
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    TEST_ASSERT_EQUAL_size_t(0U, alarm_writes);
}

void test_alarm_relatch_after_live_repeat_requeues_the_alarm(void)
{
    (void)alarm_packet();
    const cp56time2a_t time = cp56time2a_make(10000U, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    rf_scp_live_t live = {.source = 5U, .uptime_sec = 10U,
                          .trip_failed = true};

    TEST_ASSERT_TRUE(rf_handle_live(&live, tick, &time));
    rf_alarm_live(5U, &time);
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, alarm_writes);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
    /* Operator latches the still-failed phase; the queue re-opens. */
    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    rf_alarm_live(5U, &time);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    /* A repeat while the record is already pending changes nothing. */
    rf_alarm_live(5U, &time);
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, alarm_writes);
    TEST_ASSERT_EQUAL_UINT8(1U, written_alarm.active);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

/*** end of file ***/
