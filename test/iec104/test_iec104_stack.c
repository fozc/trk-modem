/**
 * @file test_iec104_stack.c
 * @brief Actual core/server entry points against literal wire vectors.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "iec104_server.h"

#include <string.h>

TEST_SOURCE_FILE("iec104_asdu.c")
TEST_SOURCE_FILE("iec104_core.c")
TEST_SOURCE_FILE("iec104_server.c")

static iec104_server_t server;
static iec104_core_t core;
static uint8_t frames[256][255];
static size_t lengths[256];
static size_t frame_count;
static bool writable;
static bool receive_ready;
static uint32_t delivery_tag;
static uint32_t delivery_count;
static uint32_t clock_calls;
static uint32_t reset_calls;
static uint32_t extension_calls;
static uint32_t receive_calls;
static uint32_t event_counts[6];
static size_t point_count;
static bool point_error;
static bool clock_success;
static bool queries_enabled;
static uint8_t extra_qoi;
static uint32_t point_reads;
static uint32_t ioa_base;
static uint32_t last_read_ioa;
static bool wrong_point_ioa;
static const uint8_t start[] = {0x68U, 4U, 7U, 0U, 0U, 0U};
static const uint8_t stop[] = {0x68U, 4U, 0x13U, 0U, 0U, 0U};
static const uint8_t single_asdu[] =
    {1U, 1U, 3U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U, 1U};
static const iec104_core_config_t config = {12U, 8U, 15000U, 10000U, 20000U};

static bool capture(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    if (!writable)
    {
        return false;
    }
    TEST_ASSERT_LESS_THAN(256U, frame_count);
    TEST_ASSERT_LESS_OR_EQUAL(255U, length);
    memcpy(frames[frame_count], data, length);
    lengths[frame_count++] = length;
    return true;
}

static bool receive(void *context, const iec104_asdu_t *asdu)
{
    (void)context;
    TEST_ASSERT_EQUAL_UINT8(1U, asdu->type);
    receive_calls++;
    return receive_ready;
}

static void delivered(void *context, uint32_t tag)
{
    (void)context;
    delivery_tag = tag;
    delivery_count++;
}

static void event(void *context, iec104_core_event_t code)
{
    (void)context;
    TEST_ASSERT_LESS_THAN(6U, (uint32_t)code);
    event_counts[code]++;
}

static size_t query_index;
static size_t query_limit;
static bool malformed_response;
static uint32_t request_calls;
static uint32_t continuation_calls;
static bool yield_query;
static uint32_t last_command_ioa;
static bool prepare_initial;
static bool single_command_supported;
static uint8_t command_originator;

static void reset(void *context)
{
    (void)context;
    reset_calls++;
}

static iec104_request_result_t handle_request(void *context, const iec104_request_t *request,
                                            iec104_response_t *response)
{
    (void)context;
    request_calls++;
    if ((request->asdu.type == 45U) && single_command_supported)
    {
        if (request->ioa != server.config.reset_ioa)
        {
            return IEC104_REQUEST_UNKNOWN_IOA;
        }
        return request->single_command.state && (request->single_command.qualifier == 0U) ?
                   IEC104_REQUEST_ACCEPTED : IEC104_REQUEST_REJECTED;
    }
    if (request->asdu.type == 103U)
    {
        TEST_ASSERT_EQUAL_UINT8(26U, request->time.year);
        clock_calls++;
        return clock_success ? IEC104_REQUEST_ACCEPTED : IEC104_REQUEST_REJECTED;
    }
    if (request->asdu.type == 105U)
    {
        return IEC104_REQUEST_ACCEPTED;
    }
    if (request->asdu.type == 110U)
    {
        extension_calls++;
        last_command_ioa = request->ioa;
        if (malformed_response)
        {
            response->asdu.length = 7U;
            return IEC104_REQUEST_ACCEPTED;
        }
        if (!iec104_asdu_objects_valid(&request->asdu, 1U))
        {
            return IEC104_REQUEST_REJECTED;
        }
        iec104_point_t point;
        return iec104_point_set_encoded(&point, request->ioa, 110U, &request->asdu.payload[3], 1U) &&
               iec104_response_add(response, &point) ? IEC104_REQUEST_ACCEPTED : IEC104_REQUEST_ERROR;
    }
    if ((request->asdu.type != 100U) || !queries_enabled ||
        !(((request->qualifier >= 20U) && (request->qualifier <= 24U)) || (request->qualifier == extra_qoi)))
    {
        return IEC104_REQUEST_UNSUPPORTED;
    }
    response->more = true;
    if (!request->continuation)
    {
        query_index = 0U;
        if (!prepare_initial)
        {
            return IEC104_REQUEST_ACCEPTED;
        }
    }
    else
    {
        continuation_calls++;
    }
    if (yield_query)
    {
        return IEC104_REQUEST_ACCEPTED;
    }
    while (query_index < point_count)
    {
        if (query_index >= query_limit)
        {
            return IEC104_REQUEST_ERROR;
        }
        uint32_t ioa = (uint32_t)query_index + ioa_base;
        if (ioa > 0xFFFFFFU)
        {
            return IEC104_REQUEST_ERROR;
        }
        point_reads++;
        last_read_ioa = ioa;
        if (point_error)
        {
            return IEC104_REQUEST_ERROR;
        }
        iec104_point_t point = { .ioa = ioa, .type = request->qualifier == 21U ? 36U : 1U,
                                .value_size = request->qualifier == 21U ? 12U : 1U };
        point.value[0] = (uint8_t)((ioa - 1U) & 1U);
        if (wrong_point_ioa)
        {
            point.value_size = 0U;
        }
        if (!iec104_response_add(response, &point))
        {
            return response->asdu.length == 6U ? IEC104_REQUEST_ERROR : IEC104_REQUEST_ACCEPTED;
        }
        query_index++;
    }
    response->more = false;
    return IEC104_REQUEST_ACCEPTED;
}

static void core_feed(const uint8_t *data, size_t length, uint32_t time)
{
    TEST_ASSERT_EQUAL_size_t(length,
        iec104_core_process_data(&core, data, length, time));
}

static void ack(uint16_t sequence, bool application, uint32_t time)
{
    uint8_t data[] = {0x68U, 4U, 1U, 0U, 0U, 0U};
    iec104_write_u16(&data[4], (uint16_t)(sequence << 1U));
    if (application)
    {
        TEST_ASSERT_EQUAL_size_t(sizeof(data),
            iec104_server_process_data(&server, data, sizeof(data), time));
    }
    else
    {
        core_feed(data, sizeof(data), time);
    }
}

static void open_server(void)
{
    iec104_server_open(&server, 0U);
    TEST_ASSERT_EQUAL_size_t(sizeof(start),
        iec104_server_process_data(&server, start, sizeof(start), 0U));
    ack(server.core.send_sequence, true, 1U);
    frame_count = 0U;
}

static void command(uint8_t type, uint8_t qualifier, uint32_t ioa,
                     uint8_t cause, uint16_t address)
{
    uint8_t data[] = {0x68U, 14U, 0U, 0U, 0U, 0U, type, 1U,
                     cause, command_originator, 0U, 0U, 0U, 0U, 0U, qualifier};
    iec104_write_u16(&data[2],
        (uint16_t)(server.core.receive_sequence << 1U));
    iec104_write_u16(&data[4],
        (uint16_t)(server.core.acknowledged << 1U));
    iec104_write_u16(&data[10], address);
    iec104_write_ioa(&data[12], ioa);
    (void)iec104_server_process_data(&server, data, sizeof(data),
                            server.core.now_ms + 1U);
}

static void clock_command(uint8_t minute, uint8_t cause)
{
    uint8_t data[] = {0x68U, 20U, 0U, 0U, 2U, 0U, 103U, 1U,
                     cause, 0x3CU, 0x34U, 0x12U, 0U, 0U, 0U,
                     0x6BU, 0xE6U, minute, 23U, 0xF9U, 10U, 26U};
    (void)iec104_server_process_data(&server, data, sizeof(data), 2U);
}

void setUp(void)
{
    frame_count = 0U;
    writable = true;
    receive_ready = true;
    delivery_tag = 0U;
    delivery_count = 0U;
    clock_calls = 0U;
    reset_calls = 0U;
    extension_calls = 0U;
    receive_calls = 0U;
    memset(event_counts, 0, sizeof(event_counts));
    point_count = 3U;
    point_error = false;
    clock_success = true;
    queries_enabled = true;
    extra_qoi = 0U;
    point_reads = 0U;
    ioa_base = 1U;
    last_read_ioa = 0U;
    wrong_point_ioa = false;
    query_index = 0U;
    query_limit = 2000U;
    malformed_response = false;
    request_calls = 0U;
    continuation_calls = 0U;
    yield_query = false;
    last_command_ioa = 0U;
    prepare_initial = false;
    single_command_supported = false;
    command_originator = 0x3CU;
    iec104_core_io_t io = {.write = capture, .receive = receive,
                           .delivered = delivered};
    TEST_ASSERT_TRUE(iec104_core_init(&core, &config, &io));
    iec104_core_open(&core, 0U);
    iec104_server_config_t app_config = {.common_address = 0x1234U,
        .originator = 0x5AU, .reset_ioa = 10001U};
    iec104_server_io_t app_io = {.write = capture, .handle_request = handle_request, .request_reset = reset,
        .delivered = delivered};
    TEST_ASSERT_TRUE(iec104_server_init(&server, &config, &app_config,
        &app_io));
}

void tearDown(void)
{
}

void test_stack_start_vector_and_duplicate_preserve_sequence(void)
{
    static const uint8_t expected[] = {0x68U, 4U, 0x0BU, 0U, 0U, 0U};
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    core_feed(start, sizeof(start), 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, core.send_sequence);
    TEST_ASSERT_EQUAL_size_t(1U, core.pending_count);
}

void test_stack_every_start_split_is_accepted(void)
{
    for (size_t split = 0U; split <= sizeof(start); split++)
    {
        iec104_core_open(&core, 0U);
        core_feed(start, split, 0U);
        core_feed(&start[split], sizeof(start) - split, 1U);
        TEST_ASSERT_EQUAL(IEC104_CORE_STARTED, core.state);
    }
}

void test_stack_rejected_start_retries_before_activation(void)
{
    writable = false;
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STOPPED, core.state);
    writable = true;
    iec104_core_poll(&core, 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STARTED, core.state);
}

void test_stack_send_literal_single_point_full_frame(void)
{
    static const uint8_t expected[] = {0x68U, 14U, 0U, 0U, 0U, 0U,
        1U, 1U, 3U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U, 1U};
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[1], sizeof(expected));
}

void test_stack_tx_rejection_preserves_window_tag_and_timer(void)
{
    core_feed(start, sizeof(start), 0U);
    writable = false;
    TEST_ASSERT_FALSE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    TEST_ASSERT_EQUAL_UINT16(0U, core.send_sequence);
    TEST_ASSERT_EQUAL_size_t(0U, core.pending_count);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
}

void test_stack_k_one_delivery_only_after_covering_ack(void)
{
    core.config.k = 1U;
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    TEST_ASSERT_FALSE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 8U));
    ack(0U, false, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
    ack(1U, false, 2U);
    TEST_ASSERT_EQUAL_UINT32(7U, delivery_tag);
    ack(1U, false, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, delivery_count);
}

void test_stack_old_ack_is_harmless_future_ack_closes(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    ack(1U, false, 1U);
    ack(0U, false, 2U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STARTED, core.state);
    ack(2U, false, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_sequence_wrap_delivery(void)
{
    core_feed(start, sizeof(start), 0U);
    /* Seed near wrap; exercise the same send/receive paths as normal. */
    core.send_sequence = 32767U;
    core.acknowledged = 32767U;
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 77U));
    TEST_ASSERT_EQUAL_UINT16(0U, core.send_sequence);
    ack(0U, false, 1U);
    TEST_ASSERT_EQUAL_UINT32(77U, delivery_tag);
}

void test_stack_partial_ack_keeps_original_oldest_send_time(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 1U));
    iec104_core_poll(&core, 100U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 2U));
    ack(1U, false, 14000U);
    iec104_core_poll(&core, 15099U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STARTED, core.state);
    iec104_core_poll(&core, 15100U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    TEST_ASSERT_EQUAL_UINT32(1U, delivery_count);
}

void test_stack_late_ack_does_not_mark_durable_delivery(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 77U));
    const uint8_t late_ack[] = {0x68U, 4U, 1U, 0U, 2U, 0U};
    TEST_ASSERT_EQUAL_size_t(0U,
        iec104_core_process_data(&core, late_ack, sizeof(late_ack), 15000U));
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
}

void test_stack_every_i_frame_split_keeps_one_delivery_to_application(void)
{
    uint8_t data[16] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    for (size_t split = 0U; split <= sizeof(data); split++)
    {
        frame_count = 0U;
        receive_calls = 0U;
        iec104_core_open(&core, 0U);
        core_feed(start, sizeof(start), 0U);
        core_feed(data, split, 1U);
        core_feed(&data[split], sizeof(data) - split, 2U);
        TEST_ASSERT_EQUAL_UINT32(1U, receive_calls);
        TEST_ASSERT_EQUAL_UINT16(1U, core.receive_sequence);
    }
}

void test_stack_stop_waits_for_ack_and_start_resumes_sequence(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 1U));
    core_feed(stop, sizeof(stop), 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STOPPING, core.state);
    TEST_ASSERT_FALSE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 2U));
    ack(1U, false, 2U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STOPPED, core.state);
    TEST_ASSERT_EQUAL_UINT8(0x23U, frames[frame_count - 1U][2]);
    core_feed(start, sizeof(start), 3U);
    TEST_ASSERT_EQUAL_UINT16(1U, core.send_sequence);
}

void test_stack_stop_does_not_disable_t1(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 1U));
    core_feed(stop, sizeof(stop), 1U);
    iec104_core_poll(&core, 15000U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
}

void test_stack_t3_test_confirmation_and_timeout(void)
{
    iec104_core_poll(&core, 20000U);
    TEST_ASSERT_EQUAL_UINT8(0x43U, frames[0][2]);
    const uint8_t reply[] = {0x68U, 4U, 0x83U, 0U, 0U, 0U};
    core_feed(reply, sizeof(reply), 20001U);
    iec104_core_poll(&core, 40001U);
    TEST_ASSERT_EQUAL_UINT8(0x43U, frames[1][2]);
    iec104_core_poll(&core, 55001U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_timer_wrap_is_unsigned(void)
{
    iec104_core_open(&core, UINT32_MAX - 99U);
    core_feed(start, sizeof(start), UINT32_MAX - 99U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 1U));
    iec104_core_poll(&core, 14900U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_rx_t2_and_w_ack_vectors(void)
{
    core_feed(start, sizeof(start), 0U);
    uint8_t data[16] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    core_feed(data, sizeof(data), 1U);
    iec104_core_poll(&core, 10000U);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    iec104_core_poll(&core, 10001U);
    const uint8_t expected[] = {0x68U, 4U, 1U, 0U, 2U, 0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[1], sizeof(expected));
    core.config.w = 1U;
    data[2] = 2U;
    core_feed(data, sizeof(data), 10002U);
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
    iec104_core_poll(&core, 10002U);
    TEST_ASSERT_EQUAL_UINT8(4U, frames[2][4]);
}

void test_stack_deferred_receive_retains_frame_and_suffix(void)
{
    core_feed(start, sizeof(start), 0U);
    uint8_t data[32] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    memcpy(&data[16], data, 16U);
    data[18] = 2U;
    receive_ready = false;
    TEST_ASSERT_EQUAL_size_t(16U, iec104_core_process_data(&core, data,
        sizeof(data), 1U));
    TEST_ASSERT_EQUAL_UINT16(1U, core.receive_sequence);
    receive_ready = true;
    iec104_core_poll(&core, 2U);
    core_feed(&data[16], 16U, 3U);
    TEST_ASSERT_EQUAL_UINT16(2U, core.receive_sequence);
    TEST_ASSERT_EQUAL_UINT32(3U, receive_calls);
}

void test_stack_invalid_apci_lengths_and_reserved_fields_close(void)
{
    const uint8_t bad[][6] = {
        {0x67U, 4U, 7U, 0U, 0U, 0U},
        {0x68U, 3U, 7U, 0U, 0U, 0U},
        {0x68U, 254U, 7U, 0U, 0U, 0U},
        {0x68U, 4U, 7U, 1U, 0U, 0U},
        {0x68U, 4U, 1U, 0U, 1U, 0U},
        {0x68U, 4U, 0xFFU, 0U, 0U, 0U}};
    for (size_t index = 0U; index < sizeof(bad) / sizeof(bad[0]); index++)
    {
        iec104_core_open(&core, 0U);
        (void)iec104_core_process_data(&core, bad[index],
            sizeof(bad[index]), 1U);
        TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    }
}

void test_stack_i_frame_before_start_and_wrong_sequence_close(void)
{
    uint8_t data[16] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    core_feed(data, sizeof(data), 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    iec104_core_open(&core, 0U);
    core_feed(start, sizeof(start), 0U);
    data[2] = 2U;
    core_feed(data, sizeof(data), 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_closed_session_does_not_deliver_pending_tags(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 9U));
    iec104_core_close(&core);
    iec104_core_open(&core, 1U);
    TEST_ASSERT_EQUAL_size_t(0U, core.pending_count);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
    TEST_ASSERT_EQUAL_UINT16(0U, core.send_sequence);
}

void test_stack_invalid_configuration_leaves_live_state_unchanged(void)
{
    core_feed(start, sizeof(start), 0U);
    iec104_core_config_t invalid = config;
    invalid.k = 0U;
    iec104_core_io_t io = core.io;
    TEST_ASSERT_FALSE(iec104_core_init(&core, &invalid, &io));
    TEST_ASSERT_EQUAL(IEC104_CORE_STARTED, core.state);
    invalid = config;
    invalid.t2_ms = invalid.t1_ms;
    TEST_ASSERT_FALSE(iec104_core_init(&core, &invalid, &io));
}

void test_stack_initialization_full_vector_and_once_per_tcp(void)
{
    iec104_server_open(&server, 0U);
    (void)iec104_server_process_data(&server, start, sizeof(start), 0U);
    const uint8_t expected[] = {0x68U, 14U, 0U, 0U, 0U, 0U,
        70U, 1U, 4U, 0x5AU, 0x34U, 0x12U, 0U, 0U, 0U, 0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[1], sizeof(expected));
    (void)iec104_server_process_data(&server, start, sizeof(start), 1U);
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
}

void test_stack_gi_full_multi_object_vector_and_oa(void)
{
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    const uint8_t confirm[] = {0x68U, 14U, 2U, 0U, 2U, 0U,
        100U, 1U, 7U, 0x3CU, 0x34U, 0x12U, 0U, 0U, 0U, 20U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(confirm, frames[0], sizeof(confirm));
    iec104_server_poll(&server, 3U);
    const uint8_t expected[] = {0x68U, 22U, 4U, 0U, 2U, 0U,
        1U, 3U, 20U, 0x3CU, 0x34U, 0x12U,
        1U, 0U, 0U, 0U, 2U, 0U, 0U, 1U, 3U, 0U, 0U, 0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[1], sizeof(expected));
    iec104_server_poll(&server, 4U);
    const uint8_t terminate[] = {0x68U, 14U, 6U, 0U, 2U, 0U,
        100U, 1U, 10U, 0x3CU, 0x34U, 0x12U, 0U, 0U, 0U, 20U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(terminate, frames[2], sizeof(terminate));
    TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE, server.query_state);
}

void test_stack_gi_k_one_retries_without_resampling_frame(void)
{
    server.core.config.k = 1U;
    open_server();
    command(100U, 21U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    size_t cursor = query_index;
    uint32_t calls = continuation_calls;
    writable = false;
    ack(2U, true, 4U);
    TEST_ASSERT_EQUAL_size_t(cursor, query_index);
    TEST_ASSERT_EQUAL_UINT32(calls, continuation_calls);
    writable = true;
    iec104_server_poll(&server, 5U);
    TEST_ASSERT_EQUAL_size_t(2U, frame_count);
    TEST_ASSERT_EQUAL_UINT32(calls, continuation_calls);
    TEST_ASSERT_EQUAL_UINT8(3U, frames[1][7]);
    ack(3U, true, 6U);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
}

void test_stack_second_gi_rejected_without_replacing_query(void)
{
    open_server();
    command(100U, 21U, 0U, 6U, 0x1234U);
    command(100U, 24U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(21U, server.query_qoi);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[frame_count - 1U][8]);
}

void test_stack_all_gi_groups_empty_end_cleanly(void)
{
    point_count = 0U;
    open_server();
    for (uint8_t qoi = 20U; qoi <= 24U; qoi++)
    {
        command(100U, qoi, 0U, 6U, 0x1234U);
        iec104_server_poll(&server, server.core.now_ms + 1U);
        iec104_server_poll(&server, server.core.now_ms + 1U);
        TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE, server.query_state);
        ack(server.core.send_sequence, true, server.core.now_ms + 1U);
    }
}

void test_stack_application_can_reject_station_query_before_reading_data(void)
{
    open_server();
    queries_enabled = false;
    command(100U, 20U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, point_reads);
    TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE, server.query_state);
}

void test_stack_another_application_can_serve_group16_without_library_changes(void)
{
    extra_qoi = 36U;
    point_count = 1U;
    open_server();
    command(100U, 36U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    iec104_server_poll(&server, 4U);
    const uint8_t expected_data[] = { 0x68U, 14U, 4U, 0U, 2U, 0U, 1U, 1U,
                                      36U, 0x3CU, 0x34U, 0x12U, 1U, 0U, 0U, 0U };
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected_data), lengths[1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_data, frames[1], sizeof(expected_data));
    TEST_ASSERT_EQUAL_UINT8(7U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
    TEST_ASSERT_EQUAL_UINT8(36U, frames[2][15]);
    TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE, server.query_state);
}

void test_stack_reserved_qoi_rejected_even_if_application_accepts_it(void)
{
    extra_qoi = 37U;
    open_server();
    command(100U, 37U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, point_reads);
}

void test_stack_invalid_transport_rejects_init_without_changing_instance(void)
{
    iec104_server_t before = server;
    iec104_server_io_t io = server.io;
    io.write = NULL;
    TEST_ASSERT_FALSE(iec104_server_init(&server, &config, &server.config, &io));
    TEST_ASSERT_EQUAL_MEMORY(&before, &server, sizeof(server));
}

void test_stack_missing_request_handler_initializes_and_defaults_to_negative_confirmation(void)
{
    iec104_server_io_t io = server.io;
    io.handle_request = NULL;
    TEST_ASSERT_TRUE(iec104_server_init(&server, &config, &server.config, &io));
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, point_reads);
}

void test_stack_application_selects_response_ioas_without_library_enumeration(void)
{
    ioa_base = 0x010203U;
    point_count = 1U;
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(0x010203U, last_read_ioa);
    TEST_ASSERT_EQUAL_UINT32(0x010203U, iec104_read_ioa(&frames[1][12]));
}

void test_stack_iterator_ioa_overflow_aborts_before_value_read(void)
{
    ioa_base = 0x1000000U;
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, point_reads);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
}

void test_stack_application_cannot_send_invalid_point_or_report_success_term_on_failure(void)
{
    wrong_point_ioa = true;
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
}

void test_stack_query_provider_failure_closes_without_success_term(void)
{
    open_server();
    point_error = true;
    command(100U, 23U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
}

void test_stack_first_callback_can_return_complete_gi_data_without_an_extra_continuation(void)
{
    prepare_initial = true;
    point_count = 1U;
    open_server();
    writable = false;
    command(100U, 20U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT32(1U, request_calls);
    TEST_ASSERT_EQUAL_size_t(0U, frame_count);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, request_calls);
    writable = true;
    iec104_server_poll(&server, 4U);
    iec104_server_poll(&server, 5U);
    iec104_server_poll(&server, 6U);
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(7U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT8(20U, frames[1][8]);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, continuation_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, request_calls);
}

void test_stack_empty_unfinished_response_yields_until_next_poll_instead_of_busy_looping(void)
{
    yield_query = true;
    open_server();
    command(100U, 20U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, continuation_calls);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    iec104_server_poll(&server, 4U);
    TEST_ASSERT_EQUAL_UINT32(2U, continuation_calls);
    yield_query = false;
    iec104_server_poll(&server, 5U);
    iec104_server_poll(&server, 6U);
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
    TEST_ASSERT_EQUAL(IEC104_QUERY_IDLE, server.query_state);
}

void test_stack_response_add_invalid_and_mixed_type_inputs_leave_prepared_chunk_unchanged(void)
{
    iec104_asdu_t header = { .type = 100U, .cause = 20U, .address = 1U };
    iec104_response_t response = { 0 };
    TEST_ASSERT_TRUE(iec104_builder_begin(&response.asdu, &header, 0U));
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single(&point, 100U, true, 0U, NULL));
    TEST_ASSERT_TRUE(iec104_response_add(&response, &point));
    iec104_response_t before = response;
    TEST_ASSERT_TRUE(iec104_point_set_float(&point, 101U, 1.0F, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_response_add(&response, &point));
    point.ioa = 0x1000000U;
    TEST_ASSERT_FALSE(iec104_response_add(&response, &point));
    TEST_ASSERT_FALSE(iec104_response_add(&response, NULL));
    TEST_ASSERT_FALSE(iec104_response_add(NULL, &point));
    TEST_ASSERT_EQUAL_MEMORY(&before, &response, sizeof(response));
}

void test_stack_unknown_type_wrong_cause_ca_and_ioa_responses(void)
{
    open_server();
    command(45U, 1U, 1U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x6CU, frames[frame_count - 1U][8]);
    command(100U, 20U, 0U, 3U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x6DU, frames[frame_count - 1U][8]);
    command(100U, 20U, 0U, 6U, 2U);
    TEST_ASSERT_EQUAL_UINT8(0x6EU, frames[frame_count - 1U][8]);
    command(100U, 20U, 9U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x6FU, frames[frame_count - 1U][8]);
}

void test_stack_clock_valid_invalid_and_callback_failure(void)
{
    open_server();
    clock_command(57U, 6U);
    TEST_ASSERT_EQUAL_UINT32(1U, clock_calls);
    TEST_ASSERT_EQUAL_UINT8(7U, frames[frame_count - 1U][8]);
    open_server();
    clock_command(0x80U, 6U);
    TEST_ASSERT_EQUAL_UINT32(1U, clock_calls);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[frame_count - 1U][8]);
    open_server();
    clock_success = false;
    clock_command(57U, 6U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[frame_count - 1U][8]);
}

void test_stack_clock_positive_reply_full_frame(void)
{
    open_server();
    clock_command(57U, 6U);
    const uint8_t expected[] = {0x68U, 20U, 2U, 0U, 2U, 0U,
        103U, 1U, 7U, 0x3CU, 0x34U, 0x12U, 0U, 0U, 0U,
        0x6BU, 0xE6U, 57U, 23U, 0xF9U, 10U, 26U};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
}

void test_stack_reset_positive_reply_full_frame(void)
{
    open_server();
    command(105U, 1U, 10001U, 6U, 0x1234U);
    const uint8_t expected[] = {0x68U, 14U, 2U, 0U, 2U, 0U,
        105U, 1U, 7U, 0x3CU, 0x34U, 0x12U, 0x11U, 0x27U, 0U, 1U};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
}

void test_stack_receive_sequence_wrap_and_window_storage_wrap(void)
{
    core_feed(start, sizeof(start), 0U);
    core.receive_sequence = 32767U;
    uint8_t data[16] = {0x68U, 14U, 0xFEU, 0xFFU};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    core_feed(data, sizeof(data), 1U);
    data[2] = 0U;
    data[3] = 0U;
    core_feed(data, sizeof(data), 2U);
    TEST_ASSERT_EQUAL_UINT16(1U, core.receive_sequence);
    for (uint32_t tag = 1U; tag <= 100U; tag++)
    {
        TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
            sizeof(single_asdu), tag));
        ack(core.send_sequence, false, 2U + tag);
    }
    TEST_ASSERT_EQUAL_UINT32(100U, delivery_count);
    TEST_ASSERT_EQUAL_UINT32(100U, delivery_tag);
    TEST_ASSERT_EQUAL_size_t(0U, core.pending_count);
}

void test_stack_test_marked_clock_and_reset_have_no_side_effect(void)
{
    open_server();
    clock_command(57U, 0x86U);
    command(105U, 1U, 0U, 0x86U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT32(0U, clock_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT8(0xC7U, frames[frame_count - 1U][8]);
}

void test_stack_reset_runs_only_after_positive_response_accepted(void)
{
    open_server();
    writable = false;
    command(105U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    writable = true;
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
    iec104_server_poll(&server, 4U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
}

void test_stack_reset_qrp_two_does_not_clear_storage(void)
{
    open_server();
    command(105U, 2U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[frame_count - 1U][8]);
}

static void enable_reset_sbo(void)
{
    single_command_supported = true;
    server.config.reset_sbo = true;
    server.config.sbo_timeout_ms = 1000U;
}

void test_stack_sbo_select_execute_full_frames_and_selection_consumed_once(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    const uint8_t select[] = {0x68U, 14U, 2U, 0U, 2U, 0U, 45U, 1U,
        7U, 0x3CU, 0x34U, 0x12U, 0x11U, 0x27U, 0U, 0x81U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(select, frames[0], sizeof(select));
    TEST_ASSERT_TRUE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    const uint8_t execute[] = {0x68U, 14U, 4U, 0U, 4U, 0U, 45U, 1U,
        7U, 0x3CU, 0x34U, 0x12U, 0x11U, 0x27U, 0U, 1U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(execute, frames[1], sizeof(execute));
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    iec104_server_poll(&server, server.core.now_ms + 1U);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
    TEST_ASSERT_FALSE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[3][8]);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
}

void test_stack_sbo_blocks_execute_without_select_and_both_direct_reset_addresses(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 1U, 10001U, 6U, 0x1234U);
    command(105U, 1U, 0U, 6U, 0x1234U);
    command(105U, 1U, 10001U, 6U, 0x1234U);
    for (size_t index = 0U; index < 3U; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0x47U, frames[index][8]);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, request_calls);
}

void test_stack_sbo_originator_and_value_mismatch_cannot_reuse_selection(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    command_originator = 0x3DU;
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[1][8]);
    command_originator = 0x3CU;
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[2][8]);
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    command(45U, 0U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_FALSE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[4][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
}

void test_stack_sbo_unknown_ioa_does_not_consume_the_selected_reset(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    command(45U, 1U, 999U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x6FU, frames[1][8]);
    TEST_ASSERT_TRUE(server.reset_selected);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    iec104_server_poll(&server, server.core.now_ms + 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
}

void test_stack_sbo_timeout_boundary_expires_without_extending_on_poll(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    uint32_t selected_at = server.reset_selected_since;
    iec104_server_poll(&server, selected_at + 999U);
    TEST_ASSERT_TRUE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT32(selected_at, server.reset_selected_since);
    iec104_server_poll(&server, selected_at + 1000U);
    TEST_ASSERT_FALSE(server.reset_selected);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[frame_count - 1U][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
}

void test_stack_sbo_execute_inside_deadline_survives_millisecond_wrap(void)
{
    enable_reset_sbo();
    uint32_t now = UINT32_MAX - 500U;
    iec104_server_open(&server, now);
    (void)iec104_server_process_data(&server, start, sizeof(start), now);
    ack(server.core.send_sequence, true, now + 1U);
    frame_count = 0U;
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    uint32_t selected_at = server.reset_selected_since;
    iec104_server_poll(&server, selected_at + 998U);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    iec104_server_poll(&server, server.core.now_ms + 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
}

void test_stack_sbo_cancel_confirms_without_reset_and_execute_is_then_rejected(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    command(45U, 0x81U, 10001U, 8U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(9U, frames[1][8]);
    TEST_ASSERT_EQUAL_UINT8(0x81U, frames[1][15]);
    TEST_ASSERT_FALSE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[2][8]);
}

void test_stack_sbo_selection_is_cleared_by_stop_and_new_tcp_session(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    (void)iec104_server_process_data(&server, stop, sizeof(stop), 3U);
    TEST_ASSERT_FALSE(server.reset_selected);
    iec104_server_close(&server);
    open_server();
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
}

void test_stack_sbo_reset_runs_only_after_execute_reply_is_accepted(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    writable = false;
    command(45U, 1U, 10001U, 6U, 0x1234U);
    uint32_t calls = request_calls;
    iec104_server_poll(&server, 5U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT32(calls, request_calls);
    writable = true;
    iec104_server_poll(&server, 6U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    iec104_server_poll(&server, 7U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
    TEST_ASSERT_EQUAL_UINT32(calls, request_calls);
}

void test_stack_sbo_k_one_waits_for_select_ack_before_accepting_execute_reply(void)
{
    enable_reset_sbo();
    server.core.config.k = 1U;
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, request_calls);
    ack(2U, true, 4U);
    TEST_ASSERT_EQUAL_size_t(2U, frame_count);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    ack(3U, true, 5U);
    TEST_ASSERT_EQUAL_size_t(3U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(10U, frames[2][8]);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, request_calls);
}

void test_stack_direct_single_reset_works_when_sbo_disabled_but_select_does_not_execute(void)
{
    single_command_supported = true;
    open_server();
    command(45U, 0x81U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
    command(45U, 1U, 10001U, 6U, 0x1234U);
    iec104_server_poll(&server, server.core.now_ms + 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_calls);
}

void test_stack_sbo_test_and_reserved_sco_bits_cannot_select_or_execute(void)
{
    enable_reset_sbo();
    open_server();
    command(45U, 0x81U, 10001U, 0x86U, 0x1234U);
    TEST_ASSERT_FALSE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT8(0xC7U, frames[0][8]);
    command(45U, 0x83U, 10001U, 6U, 0x1234U);
    TEST_ASSERT_FALSE(server.reset_selected);
    TEST_ASSERT_EQUAL_UINT8(0x47U, frames[1][8]);
    TEST_ASSERT_EQUAL_UINT32(0U, request_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
}

void test_stack_sbo_invalid_timeout_rejects_init_and_preserves_existing_server(void)
{
    iec104_server_t before = server;
    iec104_server_config_t settings = server.config;
    settings.reset_sbo = true;
    settings.sbo_timeout_ms = 0U;
    TEST_ASSERT_FALSE(iec104_server_init(&server, &config, &settings, &server.io));
    settings.sbo_timeout_ms = UINT32_MAX;
    TEST_ASSERT_FALSE(iec104_server_init(&server, &config, &settings, &server.io));
    TEST_ASSERT_EQUAL_MEMORY(&before, &server, sizeof(server));
}

void test_stack_periodic_full_frame_uses_cot_one_and_backpressure_preserves_sequence(void)
{
    open_server();
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single(&point, 0x010203U, true, 0U, NULL));
    writable = false;
    TEST_ASSERT_FALSE(iec104_server_publish_periodic(&server, &point, 1U));
    TEST_ASSERT_EQUAL_UINT16(1U, server.core.send_sequence);
    writable = true;
    TEST_ASSERT_TRUE(iec104_server_publish_periodic(&server, &point, 1U));
    const uint8_t expected[] = {0x68U, 14U, 2U, 0U, 0U, 0U, 1U, 1U,
        1U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U, 1U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
    command(100U, 20U, 0U, 6U, 0x1234U);
    TEST_ASSERT_FALSE(iec104_server_publish_periodic(&server, &point, 1U));
}

void test_stack_prepared_publication_retries_without_mutation_and_delivers_ack_tag(void)
{
    open_server();
    server.core.config.k = 1U;
    iec104_builder_t builder;
    const iec104_asdu_t header = { .type = 1U, .cause = 3U, .originator = 0x5AU, .address = 0x1234U };
    const uint8_t value = 1U;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &header, 1U));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0x010203U, &value));
    iec104_builder_t original = builder;
    writable = false;
    iec104_server_t before = server;
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 77U));
    TEST_ASSERT_EQUAL_MEMORY(&before, &server, sizeof(server));
    writable = true;
    TEST_ASSERT_TRUE(iec104_server_publish_asdu(&server, &builder, 77U));
    const uint8_t expected[] = {0x68U, 14U, 2U, 0U, 0U, 0U, 1U, 1U,
        3U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U, 1U};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
    before = server;
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 78U));
    TEST_ASSERT_EQUAL_MEMORY(&before, &server, sizeof(server));
    TEST_ASSERT_EQUAL_MEMORY(&original, &builder, sizeof(builder));
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
    ack(server.core.send_sequence, true, 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, delivery_count);
    TEST_ASSERT_EQUAL_UINT32(77U, delivery_tag);
    TEST_ASSERT_TRUE(iec104_server_publish_asdu(&server, &builder, 78U));
}

void test_stack_prepared_publication_supports_one_maximum_size_extension_object(void)
{
    open_server();
    iec104_builder_t builder;
    const iec104_asdu_t header = { .type = 200U, .cause = 1U, .originator = 0x5AU, .address = 0x1234U };
    uint8_t value[240];
    memset(value, 0xA5, sizeof(value));
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &header, sizeof(value)));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0x010203U, value));
    TEST_ASSERT_FALSE(iec104_builder_add(&builder, 0x010204U, value));
    TEST_ASSERT_TRUE(iec104_server_publish_asdu(&server, &builder, 0U));
    const uint8_t prefix[] = {0x68U, 253U, 2U, 0U, 0U, 0U, 200U, 1U,
        1U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U};
    TEST_ASSERT_EQUAL_size_t(255U, lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(prefix, frames[0], sizeof(prefix));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(value, &frames[0][sizeof(prefix)], sizeof(value));
}

void test_stack_prepared_publication_rejects_invalid_header_and_extent_without_side_effects(void)
{
    open_server();
    iec104_builder_t builder;
    const iec104_asdu_t header = { .type = 1U, .cause = 3U, .originator = 0x5AU, .address = 0x1234U };
    const uint8_t value = 1U;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &header, 1U));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, 100U, &value));
    const iec104_server_t before = server;
    const uint8_t offsets[] = {0U, 1U, 2U, 2U, 2U, 3U, 4U};
    const uint8_t invalid[] = {0U, 0U, 6U, 0x43U, 0x83U, 0U, 0U};
    for (size_t index = 0U; index < sizeof(offsets); index++)
    {
        uint8_t saved = builder.data[offsets[index]];
        builder.data[offsets[index]] = invalid[index];
        TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 0U));
        builder.data[offsets[index]] = saved;
    }
    builder.length--;
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 0U));
    builder.length++;
    builder.value_size = 0U;
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 0U));
    builder.value_size = IEC104_CORE_ASDU_MAX;
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, &builder, 0U));
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(&server, NULL, 0U));
    TEST_ASSERT_FALSE(iec104_server_publish_asdu(NULL, &builder, 0U));
    TEST_ASSERT_EQUAL_MEMORY(&before, &server, sizeof(server));
    TEST_ASSERT_EQUAL_size_t(0U, frame_count);
}

void test_stack_single_request_handler_invoked_once_on_retry(void)
{
    open_server();
    writable = false;
    command(110U, 4U, 0x010203U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, extension_calls);
    TEST_ASSERT_EQUAL_UINT32(0x010203U, last_command_ioa);
    writable = true;
    iec104_server_poll(&server, 4U);
    TEST_ASSERT_EQUAL_UINT32(1U, extension_calls);
    TEST_ASSERT_EQUAL_UINT8(110U, frames[0][6]);
}

void test_stack_incomplete_extension_does_not_reuse_previous_response(void)
{
    open_server();
    command(110U, 1U, 1U, 6U, 0x1234U);
    malformed_response = true;
    size_t before = frame_count;
    command(110U, 2U, 1U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_size_t(before, frame_count);
}

void test_stack_link_event_callbacks_follow_real_transitions_once(void)
{
    core.io.event = event;
    core_feed(start, sizeof(start), 0U);
    core_feed(start, sizeof(start), 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, event_counts[IEC104_CORE_LINK_STARTED]);
    core_feed(stop, sizeof(stop), 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, event_counts[IEC104_CORE_LINK_STOPPED]);
    iec104_core_close(&core);
    iec104_core_close(&core);
    TEST_ASSERT_EQUAL_UINT32(1U, event_counts[IEC104_CORE_DISCONNECTED]);
}

void test_stack_start_during_pending_stop_closes_without_delivering(void)
{
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, single_asdu,
        sizeof(single_asdu), 7U));
    core_feed(stop, sizeof(stop), 1U);
    core_feed(start, sizeof(start), 2U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
}

void test_stack_pipeline_overload_closes_without_second_side_effect(void)
{
    open_server();
    writable = false;
    command(105U, 1U, 0U, 6U, 0x1234U);
    command(105U, 1U, 0U, 6U, 0x1234U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
}

void test_stack_spontaneous_uses_own_oa_and_ack_tag(void)
{
    open_server();
    iec104_point_t points[] = {
        {.ioa = 10U, .type = 30U, .value_size = 8U, .value = {1U}},
        {.ioa = 20U, .type = 30U, .value_size = 8U, .value = {0x80U}}};
    TEST_ASSERT_TRUE(iec104_server_publish(&server, points, 2U, 99U));
    TEST_ASSERT_EQUAL_UINT8(0x5AU, frames[0][9]);
    TEST_ASSERT_EQUAL_UINT8(2U, frames[0][7]);
    TEST_ASSERT_EQUAL_size_t(34U, lengths[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, delivery_count);
    ack(2U, true, 3U);
    TEST_ASSERT_EQUAL_UINT32(99U, delivery_tag);
}

void test_stack_builder_sequence_capacity_and_overflow_are_atomic(void)
{
    iec104_builder_t builder;
    iec104_asdu_t asdu = {.type = 1U, .cause = 3U, .sequence = true};
    uint8_t value = 1U;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &asdu, 1U));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0xFFFFFEU, &value));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0xFFFFFFU, &value));
    size_t before = builder.length;
    TEST_ASSERT_FALSE(iec104_builder_add(&builder, 0U, &value));
    TEST_ASSERT_EQUAL_size_t(before, builder.length);
    TEST_ASSERT_TRUE(iec104_asdu_parse(&asdu, builder.data, builder.length));
    TEST_ASSERT_TRUE(iec104_asdu_objects_valid(&asdu, 1U));
    builder.data[1] = 0x83U;
    builder.data[builder.length++] = 1U;
    TEST_ASSERT_TRUE(iec104_asdu_parse(&asdu, builder.data, builder.length));
    TEST_ASSERT_FALSE(iec104_asdu_objects_valid(&asdu, 1U));
}

void test_stack_max_frame_and_failed_add_preserves_data(void)
{
    iec104_asdu_t asdu = {.type = 1U, .cause = 3U, .address = 1U};
    iec104_builder_t builder;
    uint8_t value = 1U;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &asdu, 1U));
    for (uint32_t ioa = 0U; ioa < 60U; ioa++)
    {
        TEST_ASSERT_TRUE(iec104_builder_add(&builder, ioa, &value));
    }
    TEST_ASSERT_EQUAL_size_t(246U, builder.length);
    iec104_builder_t before = builder;
    TEST_ASSERT_FALSE(iec104_builder_add(&builder, 60U, &value));
    TEST_ASSERT_EQUAL_MEMORY(&before, &builder, sizeof(builder));
    core_feed(start, sizeof(start), 0U);
    TEST_ASSERT_TRUE(iec104_core_send(&core, builder.data, builder.length, 0U));
    TEST_ASSERT_EQUAL_size_t(252U, lengths[1]);
    iec104_core_open(&core, 0U);
    core_feed(start, sizeof(start), 0U);
    /* Largest legal opaque ASDU reaches exactly 255 octets on wire. */
    uint8_t raw[249] = {127U, 1U, 3U, 0U, 1U, 0U};
    TEST_ASSERT_TRUE(iec104_core_send(&core, raw, sizeof(raw), 0U));
    TEST_ASSERT_EQUAL_size_t(255U, lengths[frame_count - 1U]);
}

void test_stack_testfr_response_retries_and_control_timeout_closes(void)
{
    const uint8_t test[] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    writable = false;
    core_feed(test, sizeof(test), 0U);
    writable = true;
    iec104_core_poll(&core, 1U);
    TEST_ASSERT_EQUAL_UINT8(0x83U, frames[0][2]);
    writable = false;
    core_feed(start, sizeof(start), 2U);
    iec104_core_poll(&core, 15002U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_failed_s_ack_retries_then_times_out(void)
{
    core_feed(start, sizeof(start), 0U);
    core.config.w = 1U;
    uint8_t data[16] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    writable = false;
    core_feed(data, sizeof(data), 1U);
    iec104_core_poll(&core, 2U);
    TEST_ASSERT_EQUAL_UINT16(1U, core.received_pending);
    iec104_core_poll(&core, 15001U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_idle_test_tx_blocked_cannot_wait_forever(void)
{
    writable = false;
    iec104_core_poll(&core, 20000U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STOPPED, core.state);
    iec104_core_poll(&core, 35000U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_malformed_asdu_and_nonzero_reserved_receive_bit_close(void)
{
    uint8_t data[16] = {0x68U, 14U};
    memcpy(&data[6], single_asdu, sizeof(single_asdu));
    core_feed(start, sizeof(start), 0U);
    data[7] = 0U;
    core_feed(data, sizeof(data), 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
    iec104_core_open(&core, 0U);
    core_feed(start, sizeof(start), 0U);
    data[7] = 1U;
    data[4] = 1U;
    core_feed(data, sizeof(data), 1U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, core.state);
}

void test_stack_null_input_is_rejected_without_changing_state(void)
{
    TEST_ASSERT_EQUAL_size_t(0U, iec104_core_process_data(&core, NULL, 1U, 0U));
    TEST_ASSERT_EQUAL_size_t(0U, iec104_server_process_data(&server, NULL,
        1U, 0U));
    TEST_ASSERT_FALSE(iec104_core_send(&core, NULL, 1U, 0U));
    TEST_ASSERT_FALSE(iec104_builder_begin(NULL, NULL, 1U));
    TEST_ASSERT_FALSE(iec104_builder_add(NULL, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_asdu_parse(NULL, NULL, 1U));
    TEST_ASSERT_FALSE(iec104_asdu_objects_valid(NULL, 1U));
    TEST_ASSERT_FALSE(iec104_cp56_time_valid(NULL));
    iec104_core_poll(NULL, 0U);
    iec104_core_open(NULL, 0U);
    iec104_core_close(NULL);
    iec104_core_abort(NULL);
    iec104_server_poll(NULL, 0U);
    iec104_server_close(NULL);
    iec104_server_open(NULL, 0U);
    TEST_ASSERT_EQUAL(IEC104_CORE_STOPPED, core.state);
}

void test_stack_query_limit_closes_without_claiming_completion(void)
{
    open_server();
    query_limit = 1U;
    point_count = 3U;
    command(100U, 20U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
}

void test_stack_malformed_command_count_has_no_reset_side_effect(void)
{
    open_server();
    uint8_t data[] = {0x68U, 14U, 0U, 0U, 2U, 0U,
        105U, 2U, 6U, 0U, 0x34U, 0x12U, 0U, 0U, 0U, 1U};
    (void)iec104_server_process_data(&server, data, sizeof(data), 2U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_calls);
}

void test_stack_float_vector_and_calendar_validation(void)
{
    uint8_t data[4];
    iec104_write_float(data, -2.5F);
    const uint8_t expected[] = {0U, 0U, 0x20U, 0xC0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, sizeof(data));
    uint8_t time[] = {0U, 0U, 0U, 0U, 29U, 2U, 24U};
    TEST_ASSERT_TRUE(iec104_cp56_time_valid(time));
    time[6] = 26U;
    TEST_ASSERT_FALSE(iec104_cp56_time_valid(time));
    time[4] = 28U;
    time[2] = 0x40U;
    TEST_ASSERT_FALSE(iec104_cp56_time_valid(time));
}

void test_stack_builder_full_single_and_multiple_frame_golden_vectors(void)
{
    /* Literal wire octets: independent of codec output and host layout.
     * CA=1234, OA=5A; IOAs=010203/0A0B0C; -2.5F; IV=1;
     * time=2026-10-25 23:57:58.987 (Sunday).
     */
    static const uint8_t single[][27] = {
        {0x68U,14U,0U,0U,0U,0U,1U,1U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x81U},
        {0x68U,18U,0U,0U,0U,0U,13U,1U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0U,0U,0x20U,0xC0U,0x80U},
        {0x68U,21U,0U,0U,0U,0U,30U,1U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x81U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U},
        {0x68U,21U,0U,0U,0U,0U,31U,1U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x82U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U},
        {0x68U,25U,0U,0U,0U,0U,36U,1U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0U,0U,0x20U,0xC0U,0x80U,
         0x6BU,0xE6U,57U,23U,0xF9U,10U,26U}};
    static const uint8_t multiple[][42] = {
        {0x68U,18U,0U,0U,0U,0U,1U,2U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x81U,12U,11U,10U,0x81U},
        {0x68U,26U,0U,0U,0U,0U,13U,2U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0U,0U,0x20U,0xC0U,0x80U,
         12U,11U,10U,0U,0U,0x20U,0xC0U,0x80U},
        {0x68U,32U,0U,0U,0U,0U,30U,2U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x81U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U,
         12U,11U,10U,0x81U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U},
        {0x68U,32U,0U,0U,0U,0U,31U,2U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0x82U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U,
         12U,11U,10U,0x82U,0x6BU,0xE6U,57U,23U,0xF9U,10U,26U},
        {0x68U,40U,0U,0U,0U,0U,36U,2U,3U,0x5AU,0x34U,0x12U,
         3U,2U,1U,0U,0U,0x20U,0xC0U,0x80U,
         0x6BU,0xE6U,57U,23U,0xF9U,10U,26U,
         12U,11U,10U,0U,0U,0x20U,0xC0U,0x80U,
         0x6BU,0xE6U,57U,23U,0xF9U,10U,26U}};
    const uint8_t types[] = {1U, 13U, 30U, 31U, 36U};
    const size_t sizes[] = {1U, 5U, 8U, 8U, 12U};
    for (size_t index = 0U; index < 5U; index++)
    {
        uint8_t value[12] = {0};
        bool measured = (types[index] == 13U) || (types[index] == 36U);
        size_t time_index = measured ? 5U : 1U;
        if (measured)
        {
            iec104_write_float(value, -2.5F);
            value[4] = 0x80U;
        }
        else
        {
            value[0] = types[index] == 31U ? 0x82U : 0x81U;
        }
        if (types[index] >= 30U)
        {
            const uint8_t time[] = {0x6BU,0xE6U,57U,23U,0xF9U,10U,26U};
            memcpy(&value[time_index], time, sizeof(time));
        }
        iec104_asdu_t asdu = {.type = types[index], .cause = 3U,
            .originator = 0x5AU, .address = 0x1234U};
        iec104_builder_t builder;
        TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &asdu, sizes[index]));
        TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0x010203U, value));
        iec104_core_open(&core, 0U);
        core_feed(start, sizeof(start), 0U);
        TEST_ASSERT_TRUE(iec104_core_send(&core, builder.data,
            builder.length, 0U));
        TEST_ASSERT_EQUAL_size_t((size_t)single[index][1] + 2U,
                                 lengths[frame_count - 1U]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(single[index], frames[frame_count - 1U],
                                      lengths[frame_count - 1U]);
        TEST_ASSERT_TRUE(iec104_builder_add(&builder, 0x0A0B0CU, value));
        iec104_core_open(&core, 0U);
        core_feed(start, sizeof(start), 0U);
        TEST_ASSERT_TRUE(iec104_core_send(&core, builder.data,
            builder.length, 0U));
        TEST_ASSERT_EQUAL_size_t((size_t)multiple[index][1] + 2U,
                                 lengths[frame_count - 1U]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(multiple[index], frames[frame_count - 1U],
                                      lengths[frame_count - 1U]);
    }
}
/*** end of file ***/
