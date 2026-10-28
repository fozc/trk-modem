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

static bool incomplete_extension(void *context, const iec104_asdu_t *request,
                                 iec104_builder_t *reply)
{
    (void)context;
    (void)request;
    (void)reply;
    return true;
}

static bool set_clock(void *context, const uint8_t time[7])
{
    (void)context;
    TEST_ASSERT_EQUAL_UINT8(26U, time[6]);
    clock_calls++;
    return clock_success;
}

static void reset(void *context)
{
    (void)context;
    reset_calls++;
}

static iec104_point_result_t point(void *context, uint8_t qoi,
                                  size_t index, iec104_point_t *out)
{
    (void)context;
    if (point_error)
    {
        return IEC104_POINT_ERROR;
    }
    if (index >= point_count)
    {
        return IEC104_POINT_END;
    }
    *out = (iec104_point_t){.ioa = (uint32_t)index + 1U,
        .type = qoi == 21U ? 36U : 1U,
        .value_size = qoi == 21U ? 12U : 1U};
    out->value[0] = (uint8_t)(index & 1U);
    return IEC104_POINT_VALUE;
}

static bool extension(void *context, const iec104_asdu_t *request,
                       iec104_builder_t *reply)
{
    (void)context;
    extension_calls++;
    iec104_asdu_t response = *request;
    response.cause = 7U;
    return iec104_builder_begin(reply, &response, 1U) &&
           iec104_builder_add(reply,
               iec104_read_ioa(request->payload), &request->payload[3]);
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
                     cause, 0x3CU, 0U, 0U, 0U, 0U, 0U, qualifier};
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
    iec104_core_io_t io = {.write = capture, .receive = receive,
                           .delivered = delivered};
    TEST_ASSERT_TRUE(iec104_core_init(&core, &config, &io));
    iec104_core_open(&core, 0U);
    static const iec104_handler_t handlers[] = {{110U, 1U, extension}};
    iec104_server_config_t app_config = {.common_address = 0x1234U,
        .originator = 0x5AU, .reset_ioa = 10001U, .point_limit = 2000U,
        .handlers = handlers, .handler_count = 1U};
    iec104_server_io_t app_io = {.write = capture, .read_point = point,
        .set_clock = set_clock, .request_reset = reset,
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
    size_t cursor = server.query_index;
    writable = false;
    ack(2U, true, 4U);
    TEST_ASSERT_EQUAL_size_t(cursor, server.query_index);
    writable = true;
    iec104_server_poll(&server, 5U);
    TEST_ASSERT_EQUAL_size_t(2U, frame_count);
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

void test_stack_query_provider_failure_closes_without_success_term(void)
{
    open_server();
    point_error = true;
    command(100U, 23U, 0U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL(IEC104_CORE_CLOSED, server.core.state);
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
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

void test_stack_registered_handler_invoked_once_on_retry(void)
{
    open_server();
    writable = false;
    command(110U, 4U, 1U, 6U, 0x1234U);
    iec104_server_poll(&server, 3U);
    TEST_ASSERT_EQUAL_UINT32(1U, extension_calls);
    writable = true;
    iec104_server_poll(&server, 4U);
    TEST_ASSERT_EQUAL_UINT32(1U, extension_calls);
    TEST_ASSERT_EQUAL_UINT8(110U, frames[0][6]);
}

void test_stack_incomplete_extension_does_not_reuse_previous_response(void)
{
    open_server();
    command(110U, 1U, 1U, 6U, 0x1234U);
    static const iec104_handler_t handlers[] =
        {{110U, 1U, incomplete_extension}};
    server.config.handlers = handlers;
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
    server.config.point_limit = 1U;
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
