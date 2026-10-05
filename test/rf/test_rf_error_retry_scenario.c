/*
 * test_rf_error_retry_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify RF dispatch, bounded retries and startup/inventory flows.
 */
#include "unity.h"
#include "mock_bsp.h"
#include "scp.h"
#include "cobs.h"
#include "rf_scp.h"
#include "rf_scp_codec.h"
#include "rf.h"
#include "rf_dummy.h"
#include "rf_monitor_json.h"
#include "mock_rf_events.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_discovery.c")
TEST_SOURCE_FILE("ring_buff.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("rf.c")
TEST_SOURCE_FILE("rf_dummy.c")
TEST_SOURCE_FILE("rf_monitor_json.c")
TEST_SOURCE_FILE("xprintf.c")

static uint32_t fake_tick;
static bool bridge_enabled;
static bool rtc_valid;
static bsp_rtc_t fake_rtc;
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

#include "../../Application/rf/rf_comm.c"
#include "../../Application/rf/rf_inventory.c"
#include "../../Application/rf/rf_shell.c"
#include "../../contiki-kernel/sys/timer.c"

clock_time_t clock_time(void)
{
    return fake_tick;
}

bool rtc_hw_is_valid(void)
{
    return rtc_valid;
}

static bsp_rtc_t read_rtc(int call_count)
{
    (void)call_count;
    return fake_rtc;
}

bool rf_uart_bridge_is_enabled(void)
{
    return bridge_enabled;
}

static scp_t hub_receiver;
static scp_packet_t transmitted;
static uint32_t transmit_calls;
static uint32_t done_calls;
static scp_cmd_result_t done_result;
static scp_packet_t done_packet;
static bool has_done_packet;
static rf_feeder_t feeder;
static rf_feeder_t second_feeder;
static rf_feeder_t extra_feeders[MAX_POWER_LINE_COUNT - 2U];
static uint32_t event_notify_calls;
static rf_scp_message_t event_notification;

static void capture_event_notification(const rf_scp_message_t *message,
                                        int call_count)
{
    (void)call_count;
    event_notification = *message;
    event_notify_calls++;
}

static void capture_frame(const uint8_t *frame, size_t length)
{
    for (size_t index = 0U; index < length; index++)
    {
        scp_process_byte(&hub_receiver, frame[index]);
    }
    const scp_packet_t *packet = scp_get_packet(&hub_receiver);
    TEST_ASSERT_NOT_NULL(packet);
    transmitted = *packet;
    transmit_calls++;
    scp_packet_done(&hub_receiver);
}

static void command_done(scp_cmd_result_t result, const scp_packet_t *packet)
{
    has_done_packet = (NULL != packet);
    if (NULL != packet)
    {
        done_packet = *packet;
    }
    done_calls++;
    done_result = result;
}

const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    switch (index)
    {
        case 0U: return &feeder;
        case 1U: return &second_feeder;
        default:
            if (MAX_POWER_LINE_COUNT > index)
            {
                return &extra_feeders[(size_t)index - 2U];
            }
            return NULL;
    }
}

bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN])
{
    for (size_t index = 0U; index < RF_EUI64_LEN; index++)
    {
        if (0U != eui[index])
        {
            return false;
        }
    }
    return true;
}

static void deliver_response(uint8_t type, uint8_t error)
{
    scp_packet_t packet = {0};
    packet.dst = RF_SCP_ADDR_RTU;
    packet.src = RF_SCP_ADDR_HUB;
    packet.type = type;
    packet.cmd = cmd_ctx.last_req.cmd;
    packet.seq = cmd_ctx.last_req.seq;
    if (SCP_TYPE_ERROR == type)
    {
        packet.data_len = 1U;
        packet.data[0] = error;
    }
    else if (RF_SCP_CMD_GET_STATUS == packet.cmd)
    {
        packet.data_len = RF_SCP_STATUS_BODY_LEN;
    }
    else if (RF_SCP_CMD_PWR_TELEMETRY == packet.cmd)
    {
        packet.data_len = 96U;
    }
    else
    {
        /* Remaining ACKs used here have no body. */
    }
    scp_on_response(&packet);
}

void setUp(void)
{
    rf_events_init_Ignore();
    rf_events_notify_StubWithCallback(capture_event_notification);
    rf_events_process_Ignore();
    fake_tick = 0U;
    event_notify_calls = 0U;
    bridge_enabled = false;
    transmit_calls = 0U;
    done_calls = 0U;
    done_result = SCP_CMD_OK;
    has_done_packet = false;
    memset(&done_packet, 0, sizeof(done_packet));
    memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    cmd_next_seq = 1U;
    rf_inventory_reset();
    rf_init();
    memset(&feeder, 0, sizeof(feeder));
    memset(&second_feeder, 0, sizeof(second_feeder));
    memset(extra_feeders, 0, sizeof(extra_feeders));
    feeder.in_use = true;
    feeder.config.fider_id = 1U;
    feeder.r_eui64[0] = 0x11U;
    time_sync_pending = false;
    time_sync_boot = false;
    time_sync_timer_started = false;
    liveness_timer_started = false;
    hub_major = RF_SCP_MAJOR_EXPECTED;
    hub_boot_received = true;
    rtc_valid = true;
    fake_rtc = (bsp_rtc_t)
    {
        .millisec = 123U, .second = 59U, .minute = 59U, .hour = 23U,
        .day = 5U, .month = 10U, .year = 26U
    };
    bsp_get_datetime_StubWithCallback(read_rtc);
    rf_discovery_reset();
    TEST_ASSERT_TRUE(rbuff_init(&rx_ring, rx_buff, sizeof(rx_buff)));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
        scp_init(&hub_receiver, RF_SCP_ADDR_HUB, NULL, HAL_GetTick, 100U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
        scp_init(&scp_ctx, RF_SCP_ADDR_RTU, capture_frame, HAL_GetTick, 100U));
}

void tearDown(void)
{
}

void test_not_available_waits_then_uses_new_seq_with_shared_budget(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_PWR_TELEMETRY,
        NULL, 0U, 500U, 2U, command_done));
    const scp_packet_t first = transmitted;
    for (uint8_t retry = 0U; retry < 2U; retry++)
    {
        fake_tick += 100U;
        deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
        TEST_ASSERT_TRUE(cmd_ctx.retry_pending);
        TEST_ASSERT_EQUAL_UINT32(1U + retry, transmit_calls);
        fake_tick += 500U;
        scp_process(fake_tick);
        TEST_ASSERT_EQUAL_UINT32(2U + retry, transmit_calls);
        TEST_ASSERT_EQUAL_UINT8(first.seq + 1U + retry, transmitted.seq);
        TEST_ASSERT_EQUAL_UINT8(first.cmd, transmitted.cmd);
        TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
        TEST_ASSERT_FALSE(scp_is_free());
        TEST_ASSERT_EQUAL_UINT8(1U - retry, cmd_ctx.retries_left);
        TEST_ASSERT_EQUAL_UINT32(fake_tick + 500U, cmd_ctx.deadline_ms);
    }
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
    TEST_ASSERT_TRUE(scp_is_free());
    TEST_ASSERT_EQUAL_UINT8(0U, cmd_ctx.retries_left);
    scp_process(5000U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
}

void test_timeout_already_retries_identical_packet_and_sequence(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
    const scp_packet_t first = transmitted;
    scp_process(499U);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    scp_process(500U);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_ctx.retries_left);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
}

void test_inventory_keeps_device_until_retry_ack_then_sends_end(void)
{
    rf_inventory_start();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    const scp_packet_t first = transmitted;
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_BUSY);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    fake_tick = 500U;
    scp_process(fake_tick);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(first.seq + 1U, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT8(0U, skipped_count);
    TEST_ASSERT_EQUAL_UINT8(0U, sent_count);
    TEST_ASSERT_EQUAL_UINT8(2U, cmd_ctx.retries_left);
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, sent_count);
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
    TEST_ASSERT_FALSE(rf_inventory_is_active());
    TEST_ASSERT_EQUAL_UINT8(0U, skipped_count);
    TEST_ASSERT_EQUAL_UINT8(1U, sent_count);
}

void test_permanent_errors_finish_without_retry(void)
{
    const uint8_t errors[] =
    {
        RF_SCP_ERR_UNKNOWN_CMD, RF_SCP_ERR_INVALID_PARAM,
        RF_SCP_ERR_NOT_SUPPORTED, RF_SCP_ERR_RECORD_INVALID
    };
    for (size_t index = 0U; index < sizeof(errors); index++)
    {
        transmit_calls = 0U;
        done_calls = 0U;
        TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET,
            RF_SCP_CMD_GET_STATUS, NULL, 0U, 500U, 2U, command_done));
        deliver_response(SCP_TYPE_ERROR, errors[index]);
        TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
        TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
        TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
        TEST_ASSERT_TRUE(scp_is_free());
    }
}

void test_inventory_end_retries_busy_then_accepts_ack(void)
{
    feeder.in_use = false;
    rf_inventory_start();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    const scp_packet_t first = transmitted;
    for (uint8_t retry = 0U; retry < 2U; retry++)
    {
        deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_BUSY);
        fake_tick += 500U;
        scp_process(fake_tick);
        TEST_ASSERT_EQUAL_UINT32(2U + retry, transmit_calls);
        TEST_ASSERT_EQUAL_UINT8(first.seq + 1U + retry, transmitted.seq);
        TEST_ASSERT_TRUE(rf_inventory_is_active());
        TEST_ASSERT_FALSE(rf_inventory_is_loaded());
    }
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_FALSE(rf_inventory_is_loaded());
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_EMPTY, rf_inventory_get_status());
    TEST_ASSERT_FALSE(rf_inventory_is_active());
}

void test_empty_error_body_is_ignored_without_reading_error_code(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
    scp_packet_t packet = {0};
    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.cmd = transmitted.cmd;
    packet.seq = transmitted.seq;
    packet.type = SCP_TYPE_ERROR;
    packet.data[0] = RF_SCP_ERR_NOT_AVAILABLE;
    scp_on_response(&packet);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_FALSE(scp_is_free());
}

void test_not_available_and_timeout_share_existing_retry_budget(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_PWR_TELEMETRY,
        NULL, 0U, 500U, 2U, command_done));
    const scp_packet_t first = transmitted;
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    fake_tick = 500U;
    scp_process(fake_tick);
    fake_tick = 1000U;
    scp_process(fake_tick);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(first.seq + 1U, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT8(first.cmd, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(0U, cmd_ctx.retries_left);
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
}

void test_production_ping_reply_sends_empty_ack_with_matching_command_seq(void)
{
    scp_packet_t ping = {0};
    ping.dst = RF_SCP_ADDR_RTU;
    ping.src = RF_SCP_ADDR_HUB;
    ping.type = SCP_TYPE_PING;
    ping.cmd = 0x00U;
    ping.seq = 0x2AU;
    reply_ping(&ping);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_ADDR_HUB, transmitted.dst);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_ADDR_RTU, transmitted.src);
    TEST_ASSERT_EQUAL_UINT8(SCP_TYPE_ACK, transmitted.type);
    TEST_ASSERT_EQUAL_UINT8(ping.cmd, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(ping.seq, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT8(0U, transmitted.data_len);
    TEST_ASSERT_TRUE(scp_is_free());
}

void test_production_broadcast_ping_sends_nothing(void)
{
    scp_packet_t ping = {0};
    ping.dst = RF_SCP_ADDR_BROADCAST;
    ping.src = RF_SCP_ADDR_HUB;
    ping.type = SCP_TYPE_PING;
    reply_ping(&ping);
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
    TEST_ASSERT_TRUE(scp_is_free());
}

static scp_packet_t make_response(uint8_t type, uint8_t length)
{
    scp_packet_t packet = {0};

    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.cmd = cmd_ctx.last_req.cmd;
    packet.seq = cmd_ctx.last_req.seq;
    packet.type = type;
    packet.data_len = length;
    return packet;
}

static void receive_frame(const uint8_t *frame, size_t length)
{
    for (size_t index = 0U; index < length; index++)
    {
        rf_comm_rx_interrupt_handler(frame[index]);
    }
}

static void inject_packet(const scp_packet_t *packet)
{
    scp_t hub_sender;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
        scp_init(&hub_sender, RF_SCP_ADDR_HUB, receive_frame,
                 HAL_GetTick, 100U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&hub_sender, packet));
    rf_comm_check_rx();
}

static void start_status_command(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
}

void test_rx_path_accepts_only_matching_hub_ack_with_valid_payload(void)
{
    start_status_command();
    scp_packet_t packet = make_response(SCP_TYPE_ACK, 25U);

    packet.src = 3U;
    inject_packet(&packet);
    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_BROADCAST;
    inject_packet(&packet);
    packet.dst = RF_SCP_ADDR_RTU;
    packet.seq++;
    inject_packet(&packet);
    packet.seq--;
    packet.cmd = RF_SCP_CMD_GET_FRAM_STATS;
    inject_packet(&packet);
    packet.cmd = RF_SCP_CMD_GET_STATUS;
    packet.data_len = 24U;
    inject_packet(&packet);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_FALSE(scp_is_free());
    TEST_ASSERT_EQUAL_UINT8(2U, cmd_ctx.retries_left);
    packet.data_len = 25U;
    inject_packet(&packet);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
    TEST_ASSERT_TRUE(scp_is_free());
}

void test_rx_notifications_do_not_ack_or_complete_an_outstanding_request(void)
{
    start_status_command();
    for (size_t index = 0U;
         index < sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]); index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];

        if ((RF_SCP_ADDR_RTU == vector->logical[0]) &&
            (SCP_TYPE_SET == vector->logical[2]) &&
            (RF_SCP_CMD_BOOT_NOTIFY != vector->logical[3]))
        {
            receive_frame(vector->wire, vector->wire_len);
            rf_comm_check_rx();
            TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
            TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
            TEST_ASSERT_FALSE(scp_is_free());
        }
    }
    TEST_ASSERT_FALSE(time_sync_pending);
    TEST_ASSERT_GREATER_THAN_UINT8(0U, rf_discovery_get_count());
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
}

void test_short_or_wrong_source_discovery_does_not_change_existing_list(void)
{
    scp_packet_t packet = {0};

    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.type = SCP_TYPE_SET;
    packet.cmd = RF_SCP_CMD_DISCOVERY_REPORT;
    packet.data_len = 9U;
    packet.data[0] = 0x11U;
    inject_packet(&packet);
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
    packet.data[0] = 0x22U;
    packet.data_len = 8U;
    inject_packet(&packet);
    packet.data_len = 9U;
    packet.src = 0xFFU;
    inject_packet(&packet);
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
}

void test_busy_retry_ignores_late_ack_until_new_sequence_is_sent(void)
{
    start_status_command();
    scp_packet_t late_ack = make_response(SCP_TYPE_ACK, 25U);

    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_BUSY);
    inject_packet(&late_ack);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    scp_process(499U);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    fake_tick = 500U;
    scp_process(fake_tick);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_NOT_EQUAL(late_ack.seq, transmitted.seq);
    inject_packet(&late_ack);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
}

void test_cfg_read_not_available_is_terminal_in_this_hub_firmware(void)
{
    const uint8_t eui[8] = {0x11U};

    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_CFG_READ_ALL,
        eui, sizeof(eui), 500U, 3U, command_done));
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
    scp_process(5000U);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
}

void test_cfg2_gen_conflict_returns_error_without_blind_set_retry(void)
{
    uint8_t body[16] = {0};

    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_PWR_CFG2,
        body, sizeof(body), 1000U, 3U, command_done));
    scp_packet_t response = make_response(SCP_TYPE_ERROR, 2U);
    response.data[0] = RF_SCP_ERR_BUSY;
    response.data[1] = 42U;
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
    TEST_ASSERT_TRUE(scp_is_free());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_TRUE(has_done_packet);
    TEST_ASSERT_EQUAL_UINT8(2U, done_packet.data_len);
    TEST_ASSERT_EQUAL_UINT8(42U, done_packet.data[1]);
}

void test_cfg2_ack_length_must_match_get_or_set_request(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_PWR_CFG2,
        NULL, 0U, 500U, 3U, command_done));
    scp_packet_t response = make_response(SCP_TYPE_ACK, 1U);
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    response.data_len = 23U;
    response.data[0] = 1U;
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    uint8_t body[16] = {0};
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_PWR_CFG2,
        body, sizeof(body), 1000U, 3U, command_done));
    response = make_response(SCP_TYPE_ACK, 23U);
    response.data[0] = 1U;
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    response.data_len = 1U;
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(2U, done_calls);
}

void test_invalid_request_or_transport_does_not_consume_sequence(void)
{
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_TIME_SYNC,
        NULL, 7U, 500U, 3U, command_done));
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 0U, 3U, command_done));
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 0x80000000U, 3U, command_done));
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 3U, command_done));
    TEST_ASSERT_FALSE(scp_send_request(NULL, command_done));
    scp_ctx.transmit = NULL;
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 3U, command_done));
    TEST_ASSERT_TRUE(scp_is_free());
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_next_seq);
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    scp_on_response(NULL);
    rf_comm_on_data_received(NULL);
}

void test_bridge_mode_blocks_requests_replies_and_retry_budget_changes(void)
{
    start_status_command();
    bridge_enabled = true;
    TEST_ASSERT_FALSE(scp_is_free());
    TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 3U, command_done));
    scp_process(5000U);
    deliver_response(SCP_TYPE_ACK, 0U);
    scp_packet_t ping = make_response(SCP_TYPE_PING, 0U);
    ping.cmd = 0U;
    rf_comm_on_data_received(&ping);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_EQUAL_UINT8(2U, cmd_ctx.retries_left);
    TEST_ASSERT_EQUAL_UINT32(500U, cmd_ctx.deadline_ms);
}

void test_timer_wrap_and_sequence_wrap_keep_timeout_retries_identical(void)
{
    fake_tick = UINT32_MAX - 250U;
    cmd_next_seq = 255U;
    start_status_command();
    TEST_ASSERT_EQUAL_UINT8(255U, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT32(249U, cmd_ctx.deadline_ms);
    scp_process(248U);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    scp_process(249U);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(255U, transmitted.seq);
    deliver_response(SCP_TYPE_ACK, 0U);
    start_status_command();
    TEST_ASSERT_EQUAL_UINT8(0U, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_next_seq);
}

static void start_next_command(scp_cmd_result_t result,
                                const scp_packet_t *packet)
{
    command_done(result, packet);
    start_status_command();
}

void test_callback_can_start_next_request_after_completion(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 3U, start_next_command));
    uint8_t first_seq = transmitted.seq;
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(first_seq + 1U, transmitted.seq);
    TEST_ASSERT_FALSE(scp_is_free());
}

void test_request_defaults_match_document_command_classes(void)
{
    static const uint16_t cases[][5] =
    {
        {0x00U, SCP_TYPE_PING, 0U, 500U, 3U},
        {0x01U, SCP_TYPE_GET, 0U, 500U, 3U},
        {0x02U, SCP_TYPE_GET, 0U, 500U, 3U},
        {0x24U, SCP_TYPE_SET, 1U, 1000U, 3U},
        {0x26U, SCP_TYPE_SET, 1U, 1000U, 3U},
        {0x28U, SCP_TYPE_GET, 1U, 500U, 3U},
        {0x2AU, SCP_TYPE_SET, 1U, 1000U, 1U},
        {0x40U, SCP_TYPE_GET, 0U, 500U, 3U},
        {0x42U, SCP_TYPE_GET, 2U, 500U, 3U},
        {0x44U, SCP_TYPE_GET, 4U, 500U, 2U},
        {0x46U, SCP_TYPE_SET, 2U, 500U, 3U},
        {0xE5U, SCP_TYPE_GET, 0U, 500U, 3U},
        {0xE5U, SCP_TYPE_SET, 16U, 1000U, 3U},
        {0xE6U, SCP_TYPE_SET, 2U, 1000U, 3U},
        {0xE7U, SCP_TYPE_GET, 0U, 500U, 3U},
        {0xE8U, SCP_TYPE_GET, 0U, 500U, 3U}
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        scp_packet_t request = {0};

        (void)memset(&cmd_ctx, 0, sizeof(cmd_ctx));
        request.cmd = (uint8_t)cases[index][0];
        request.type = (uint8_t)cases[index][1];
        request.data_len = (uint8_t)cases[index][2];
        if (RF_SCP_CMD_EPOCH_REFRESH == request.cmd)
        {
            request.data[0] = 1U;
        }
        else if (RF_SCP_CMD_LOG_READ_RANGE == request.cmd)
        {
            request.data[2] = 4U;
        }
        else
        {
            /* Zero is valid for the remaining request bodies. */
        }
        TEST_ASSERT_TRUE(scp_send_request(&request, command_done));
        TEST_ASSERT_EQUAL_UINT32(cases[index][3], cmd_ctx.timeout_ms);
        TEST_ASSERT_EQUAL_UINT8(cases[index][4], cmd_ctx.retries_left);
    }
}

void test_timeout_exhaustion_completes_once_without_a_response(void)
{
    scp_packet_t request = {0};

    request.cmd = RF_SCP_CMD_GET_STATUS;
    request.type = SCP_TYPE_GET;
    TEST_ASSERT_TRUE(scp_send_request(&request, command_done));
    const scp_packet_t first = transmitted;
    for (uint32_t attempt = 1U; attempt <= 4U; attempt++)
    {
        fake_tick = attempt * 500U;
        scp_process(fake_tick);
        TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
    }
    TEST_ASSERT_EQUAL_UINT32(4U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_TIMEOUT, done_result);
    TEST_ASSERT_FALSE(has_done_packet);
    TEST_ASSERT_TRUE(scp_is_free());
    scp_process(5000U);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(4U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
}

void test_busy_retries_are_bounded_and_finish_with_original_error(void)
{
    start_status_command();
    for (uint8_t attempt = 0U; attempt < 3U; attempt++)
    {
        deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_BUSY);
        if (2U > attempt)
        {
            fake_tick += 500U;
            scp_process(fake_tick);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
    TEST_ASSERT_TRUE(has_done_packet);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_ERR_BUSY, done_packet.data[0]);
    TEST_ASSERT_TRUE(scp_is_free());
    scp_process(5000U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
}

void test_malformed_cfg2_error_extensions_leave_request_pending(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_PWR_CFG2,
        NULL, 0U, 500U, 3U, command_done));
    scp_packet_t response = make_response(SCP_TYPE_ERROR, 3U);
    response.data[0] = RF_SCP_ERR_BUSY;
    inject_packet(&response);
    response.data_len = 2U;
    response.data[0] = RF_SCP_ERR_INVALID_PARAM;
    inject_packet(&response);
    response.data[0] = RF_SCP_ERR_NOT_AVAILABLE;
    inject_packet(&response);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_EQUAL_UINT8(3U, cmd_ctx.retries_left);
    TEST_ASSERT_FALSE(cmd_ctx.retry_pending);
}

void test_ping_works_while_request_is_busy_but_malformed_ping_is_ignored(void)
{
    start_status_command();
    scp_packet_t ping = make_response(SCP_TYPE_PING, 0U);
    ping.cmd = 0U;
    ping.seq = 42U;
    inject_packet(&ping);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(SCP_TYPE_ACK, transmitted.type);
    TEST_ASSERT_EQUAL_UINT8(42U, transmitted.seq);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_FALSE(scp_is_free());
    ping.cmd = 1U;
    inject_packet(&ping);
    ping.cmd = 0U;
    ping.data_len = 1U;
    inject_packet(&ping);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
}

static void notify_boot(uint8_t major)
{
    scp_packet_t packet = {0};

    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.type = SCP_TYPE_SET;
    packet.cmd = RF_SCP_CMD_BOOT_NOTIFY;
    packet.data_len = 1U;
    packet.data[0] = major;
    inject_packet(&packet);
}

static void complete_inventory(void)
{
    for (size_t step = 0U; step < (MAX_POWER_LINE_COUNT * 3U) + 1U; step++)
    {
        if (!rf_inventory_is_active())
        {
            return;
        }
        if (cmd_ctx.busy)
        {
            deliver_response(SCP_TYPE_ACK, 0U);
        }
        rf_inventory_continue();
    }
    TEST_ASSERT_FALSE(rf_inventory_is_active());
}

void test_boot_sends_valid_local_time_then_inventory_and_end(void)
{
    static const uint8_t expected_time[7] =
    {
        0xF3U, 0xE6U, 0x3BU, 0x17U, 0x25U, 0x0AU, 0x1AU
    };
    hub_major = 0U;
    hub_boot_received = false;
    feeder.config.rf_channel = 7U;
    notify_boot(1U);
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
    TEST_ASSERT_FALSE(rf_comm_can_load_inventory());
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_time, transmitted.data, 7U);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(7U, transmitted.data[11]);
    TEST_ASSERT_EQUAL_UINT8(1U, transmitted.data[2]);
    complete_inventory();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
}

void test_invalid_rtc_loads_inventory_then_syncs_when_clock_becomes_valid(void)
{
    rtc_valid = false;
    notify_boot(1U);
    rf_comm_periodic_jobs();
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    TEST_ASSERT_TRUE(time_sync_pending);
    TEST_ASSERT_TRUE(rf_inventory_is_active());
    complete_inventory();
    TEST_ASSERT_FALSE(rf_inventory_is_active());
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
    rtc_valid = true;
    fake_rtc.day = 31U;
    fake_rtc.month = 2U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    fake_rtc.day = 5U;
    fake_rtc.month = 10U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
}

void test_hourly_time_refresh_does_not_upload_inventory_again(void)
{
    notify_boot(1U);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ACK, 0U);
    complete_inventory();
    fake_tick = 3599999U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_GET_STATUS, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    fake_tick = 3600000U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    uint32_t before_ack = transmit_calls;
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(before_ack, transmit_calls);
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
    TEST_ASSERT_FALSE(rf_inventory_is_active());
}

void test_incompatible_major_blocks_inventory_and_write_requests(void)
{
    for (uint8_t major = 0U; major < 3U; major += 2U)
    {
        notify_boot(major);
        rf_comm_periodic_jobs();
        rf_inventory_start();
        TEST_ASSERT_EQUAL_UINT8(major, rf_comm_get_hub_major());
        TEST_ASSERT_FALSE(time_sync_pending);
        TEST_ASSERT_FALSE(rf_comm_can_load_inventory());
        TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
        TEST_ASSERT_FALSE(rf_inventory_is_active());
        const uint8_t group = 1U;
        TEST_ASSERT_FALSE(scp_send_command(SCP_TYPE_SET,
            RF_SCP_CMD_CFG_COMMIT, &group, 1U, 1000U, 3U, command_done));
    }
    notify_boot(1U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
}

void test_boot_cancels_request_and_ignores_old_response(void)
{
    start_status_command();
    scp_packet_t late_response = make_response(SCP_TYPE_ACK, 25U);
    notify_boot(1U);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_RESTARTED, done_result);
    TEST_ASSERT_FALSE(has_done_packet);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    inject_packet(&late_response);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_FALSE(scp_is_free());
}

void test_boot_restarts_inventory_from_first_entry(void)
{
    feeder.s_eui64[0] = 0x22U;
    rf_inventory_start();
    scp_packet_t old_ack = make_response(SCP_TYPE_ACK, 0U);
    deliver_response(SCP_TYPE_ACK, 0U);
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_UINT8(2U, transmitted.data[2]);
    notify_boot(1U);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_IDLE, rf_inventory_get_status());
    TEST_ASSERT_EQUAL_UINT8(0U, sent_count);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, transmitted.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0x11U, transmitted.data[3]);
    inject_packet(&old_ack);
    TEST_ASSERT_EQUAL_UINT8(0U, sent_count);
    complete_inventory();
    TEST_ASSERT_EQUAL_UINT8(2U, sent_count);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
}

void test_repeated_boot_during_time_sync_does_not_use_old_ack(void)
{
    notify_boot(1U);
    rf_comm_periodic_jobs();
    scp_packet_t old_ack = make_response(SCP_TYPE_ACK, 0U);
    notify_boot(1U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    TEST_ASSERT_NOT_EQUAL(old_ack.seq, transmitted.seq);
    inject_packet(&old_ack);
    TEST_ASSERT_FALSE(rf_inventory_is_active());
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_TRUE(rf_inventory_is_active());
}

void test_empty_inventory_end_ack_stays_empty_and_boot_can_repeat(void)
{
    feeder.in_use = false;
    notify_boot(1U);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ACK, 0U);
    complete_inventory();
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_EMPTY, rf_inventory_get_status());
    TEST_ASSERT_FALSE(rf_inventory_is_loaded());
    notify_boot(1U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
}

void test_partial_inventory_is_distinct_from_complete_and_all_failed(void)
{
    feeder.s_eui64[0] = 0x22U;
    rf_inventory_start();
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_INVALID_PARAM);
    rf_inventory_continue();
    complete_inventory();
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_PARTIAL, rf_inventory_get_status());
    TEST_ASSERT_EQUAL_UINT8(1U, sent_count);
    TEST_ASSERT_EQUAL_UINT8(1U, skipped_count);
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
    rf_inventory_reset();
    feeder.config.fider_id = 0U;
    rf_inventory_start();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    complete_inventory();
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_ERROR, rf_inventory_get_status());
    TEST_ASSERT_EQUAL_UINT8(0U, sent_count);
    TEST_ASSERT_EQUAL_UINT8(2U, skipped_count);
    TEST_ASSERT_FALSE(rf_inventory_is_loaded());
}

void test_upload_gap_limit_does_not_send_late_end_or_claim_loaded(void)
{
    rf_inventory_start();
    deliver_response(SCP_TYPE_ACK, 0U);
    fake_tick = 10001U;
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_ERROR, rf_inventory_get_status());
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_FALSE(rf_inventory_is_loaded());
}

void test_inventory_waits_for_existing_command_without_skipping_device(void)
{
    start_status_command();
    rf_inventory_start();
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_TRUE(rf_inventory_is_active());
    TEST_ASSERT_EQUAL_UINT8(0U, skipped_count);
    deliver_response(SCP_TYPE_ACK, 0U);
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(1U, transmitted.data[2]);
}

void test_update_assignment_deletion_and_failed_ack_preserve_discovery(void)
{
    rf_inventory_entry_t entry =
    {
        .zone = 0U, .feeder = 1U, .phase = 2U,
        .eui64 = {0xAAU}, .channel = 7U
    };
    TEST_ASSERT_FALSE(rf_inventory_update(&entry, command_done));
    rf_inventory_start();
    complete_inventory();
    TEST_ASSERT_TRUE(rf_discovery_report(entry.eui64, -75));
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_UPDATE, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(2U, transmitted.data[2]);
    TEST_ASSERT_EQUAL_UINT8(7U, transmitted.data[11]);
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_INVALID_PARAM);
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, rf_discovery_get_count());
    entry.feeder = 0U;
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    TEST_ASSERT_EQUAL_UINT8(0U, transmitted.data[1]);
    deliver_response(SCP_TYPE_ACK, 0U);
    entry.zone = 1U;
    TEST_ASSERT_FALSE(rf_inventory_update(&entry, command_done));
}

void test_epoch_requires_inventory_and_feeder_range(void)
{
    char *valid[] = {"epoch", "1"};
    char *invalid[] = {"epoch", "5"};

    TEST_ASSERT_FALSE(rf_inventory_refresh_epoch(1U, command_done));
    TEST_ASSERT_EQUAL_INT(-1, rf_shell_epoch(2, valid));
    rf_inventory_start();
    complete_inventory();
    TEST_ASSERT_EQUAL_INT(-1, rf_shell_epoch(2, invalid));
    TEST_ASSERT_EQUAL_INT(0, rf_shell_epoch(2, valid));
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_EPOCH_REFRESH, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(1U, transmitted.data[0]);
    TEST_ASSERT_EQUAL_UINT32(1000U, cmd_ctx.timeout_ms);
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_ctx.retries_left);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_FALSE(rf_inventory_refresh_epoch(0U, command_done));
    TEST_ASSERT_FALSE(rf_inventory_refresh_epoch(5U, command_done));
}

void test_time_shell_requests_refresh_without_reloading_inventory(void)
{
    char *args[] = {"time"};

    rf_inventory_start();
    complete_inventory();
    TEST_ASSERT_EQUAL_INT(0, rf_shell_time(1, args));
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    uint32_t before_ack = transmit_calls;
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(before_ack, transmit_calls);
    TEST_ASSERT_FALSE(rf_inventory_is_active());
}

void test_full_discovery_queue_refreshes_known_rssi(void)
{
    uint8_t eui[8] = {0};
    rf_discovery_entry_t entry;

    for (uint8_t index = 0U; index < RF_DISCOVERY_MAX; index++)
    {
        eui[0] = (uint8_t)(index + 1U);
        TEST_ASSERT_TRUE(rf_discovery_report(eui, -75));
    }
    eui[0] = 1U;
    TEST_ASSERT_TRUE(rf_discovery_report(eui, -128));
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_EQUAL_INT8(-128, entry.rssi);
    TEST_ASSERT_TRUE(entry.has_rssi);
    eui[0] = 99U;
    TEST_ASSERT_FALSE(rf_discovery_report(eui, -20));
    TEST_ASSERT_EQUAL_UINT8(RF_DISCOVERY_MAX, rf_discovery_get_count());
    eui[0] = 1U;
    TEST_ASSERT_TRUE(rf_discovery_remove(eui));
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_EQUAL_UINT8(2U, entry.eui64[0]);
    TEST_ASSERT_EQUAL_INT8(-75, entry.rssi);
    eui[0] = 99U;
    TEST_ASSERT_TRUE(rf_discovery_add(eui));
    TEST_ASSERT_TRUE(rf_discovery_get(RF_DISCOVERY_MAX - 1U, &entry));
    TEST_ASSERT_FALSE(entry.has_rssi);
    TEST_ASSERT_FALSE(rf_discovery_report(NULL, -75));
    TEST_ASSERT_FALSE(rf_discovery_remove(NULL));
    TEST_ASSERT_FALSE(rf_discovery_get(0U, NULL));
    rf_discovery_entry_t previous = entry;
    TEST_ASSERT_FALSE(rf_discovery_get(RF_DISCOVERY_MAX, &entry));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &entry, sizeof(entry));
    TEST_ASSERT_FALSE(rf_discovery_get(SIZE_MAX, &entry));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &entry, sizeof(entry));
}

void test_mixed_zone_and_invalid_assignment_produce_partial_inventory(void)
{
    second_feeder.in_use = true;
    second_feeder.config.zone_id = 1U;
    second_feeder.config.fider_id = 2U;
    second_feeder.r_eui64[0] = 0x22U;
    rf_inventory_start();
    complete_inventory();
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, sent_count);
    TEST_ASSERT_EQUAL_UINT8(1U, skipped_count);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_PARTIAL, rf_inventory_get_status());
}

void test_permanent_time_error_does_not_retry_or_load_inventory(void)
{
    notify_boot(1U);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_INVALID_PARAM);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_FALSE(time_sync_pending);
    TEST_ASSERT_FALSE(rf_inventory_is_active());
    TEST_ASSERT_FALSE(rf_comm_can_load_inventory());
    notify_boot(1U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
}

void test_hourly_timer_wrap_still_refreshes_time_without_inventory_reload(void)
{
    fake_tick = UINT32_MAX - 1800000U;
    uint32_t first_tick = fake_tick;

    notify_boot(1U);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ACK, 0U);
    complete_inventory();
    fake_tick = first_tick + 3599999U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_GET_STATUS, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    fake_tick = first_tick + 3600000U;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    uint32_t before_ack = transmit_calls;
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(before_ack, transmit_calls);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
}

void test_boot_during_update_preserves_discovery_and_reports_restart(void)
{
    rf_inventory_entry_t entry =
    {
        .zone = 0U, .feeder = 1U, .phase = 2U, .eui64 = {0xAAU}
    };
    rf_inventory_start();
    complete_inventory();
    TEST_ASSERT_TRUE(rf_discovery_report(entry.eui64, -75));
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    notify_boot(1U);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_RESTARTED, done_result);
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
    TEST_ASSERT_FALSE(rf_inventory_is_loaded());
}

void test_upload_accepts_end_at_the_exact_ten_second_boundary(void)
{
    rf_inventory_start();
    deliver_response(SCP_TYPE_ACK, 0U);
    fake_tick = 10000U;
    rf_inventory_continue();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
}

void test_clock_becoming_valid_mid_upload_preserves_inventory_cursor(void)
{
    rtc_valid = false;
    feeder.s_eui64[0] = 0x22U;
    notify_boot(1U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(1U, transmitted.data[2]);
    deliver_response(SCP_TYPE_ACK, 0U);
    rtc_valid = true;
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_TIME_SYNC, transmitted.cmd);
    deliver_response(SCP_TYPE_ACK, 0U);
    rf_comm_periodic_jobs();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_SET, transmitted.cmd);
    TEST_ASSERT_EQUAL_UINT8(2U, transmitted.data[2]);
    complete_inventory();
    TEST_ASSERT_EQUAL_UINT8(2U, sent_count);
    TEST_ASSERT_EQUAL_INT(RF_INVENTORY_READY, rf_inventory_get_status());
}

static scp_packet_t captured_notification(uint8_t cmd)
{
    for (size_t index = 0U;
         index < sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]); index++)
    {
        const uint8_t *logical = rf_scp_vectors[index].logical;

        if ((RF_SCP_ADDR_RTU == logical[0]) &&
            (SCP_TYPE_SET == logical[2]) && (cmd == logical[3]))
        {
            scp_packet_t packet = {0};

            packet.dst = logical[0];
            packet.src = logical[1];
            packet.type = logical[2];
            packet.cmd = logical[3];
            packet.seq = logical[4];
            packet.data_len = logical[5];
            (void)memcpy(packet.data, &logical[7], packet.data_len);
            return packet;
        }
    }
    TEST_FAIL_MESSAGE("Required captured notification is missing");
    scp_packet_t empty = {0};
    return empty;
}

static void load_one_phase(void)
{
    rf_inventory_start();
    complete_inventory();
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

void test_live_rx_requires_ack_binding_and_retains_exact_r1_values(void)
{
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    rf_phase_data_t data;

    inject_packet(&packet);
    TEST_ASSERT_FALSE(rf_get_source_data(5U, fake_tick, &data));
    rf_inventory_start();
    inject_packet(&packet);
    TEST_ASSERT_FALSE(rf_get_source_data(5U, fake_tick, &data));
    complete_inventory();
    uint32_t before_receive = transmit_calls;
    inject_packet(&packet);
    TEST_ASSERT_EQUAL_UINT32(before_receive, transmit_calls);
    TEST_ASSERT_TRUE(rf_get_phase_data(0U, PHASE_L1, fake_tick, &data));
    TEST_ASSERT_EQUAL_UINT8(0x11U, data.eui64[0]);
    TEST_ASSERT_TRUE(data.is_online);
    TEST_ASSERT_TRUE(data.has_live);
    TEST_ASSERT_EQUAL_UINT32(766990U, data.live.seq);
    TEST_ASSERT_EQUAL_UINT32(1336U, data.live.uptime_sec);
    TEST_ASSERT_EQUAL_INT8(-53, data.live.rssi);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 31.641413f, data.live.trip_voltage);
    TEST_ASSERT_EQUAL_UINT8(78U, data.live.log_pending);
    TEST_ASSERT_TRUE(data.current_valid);
}

void test_live_freshness_uses_arrival_time_not_sequence_gaps(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    put_u32(&packet.data[1], 123456789U);
    fake_tick = UINT32_MAX - 1000U;
    inject_packet(&packet);
    rf_phase_data_t data;
    uint32_t last_tick = fake_tick;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, last_tick + 29999U, &data));
    TEST_ASSERT_TRUE(data.is_online);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, last_tick + 30000U, &data));
    TEST_ASSERT_FALSE(data.is_online);
    fake_tick += 10000U;
    put_u32(&packet.data[1], 333333333U);
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.is_online);
    TEST_ASSERT_EQUAL_UINT32(333333333U, data.live.seq);
}

void test_fider_four_maps_to_its_assigned_store_slot_without_two_bit_mask(void)
{
    feeder.in_use = false;
    second_feeder.in_use = true;
    second_feeder.config.fider_id = 4U;
    second_feeder.r_eui64[0] = 0x44U;
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 17U;
    inject_packet(&packet);
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_phase_data(1U, PHASE_L1, fake_tick, &data));
    TEST_ASSERT_EQUAL_UINT8(0x44U, data.eui64[0]);
    TEST_ASSERT_FALSE(rf_get_phase_data(0U, PHASE_L1, fake_tick, &data));
    packet.data[0] = 21U; /* Reserved fider 5 must not alias fider 1. */
    inject_packet(&packet);
    TEST_ASSERT_FALSE(rf_get_source_data(21U, fake_tick, &data));
}

void test_desired_store_change_does_not_relabel_old_hub_measurements(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    inject_packet(&packet);
    feeder.r_eui64[0] = 0x22U;
    rf_phase_data_t data;

    TEST_ASSERT_FALSE(rf_get_phase_data(0U, PHASE_L1, fake_tick, &data));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_EQUAL_UINT8(0x11U, data.eui64[0]);
    rf_inventory_entry_t entry =
    {
        .zone = 0U, .feeder = 1U, .phase = 1U, .eui64 = {0x22U}
    };
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_FALSE(rf_get_source_data(5U, fake_tick, &data));
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_phase_data(0U, PHASE_L1, fake_tick, &data));
    TEST_ASSERT_EQUAL_UINT8(0x22U, data.eui64[0]);
}

void test_trip_instant_current_is_separate_from_live_current(void)
{
    load_one_phase();
    scp_packet_t live = captured_notification(RF_SCP_CMD_LIVE_DATA);
    live.data[0] = 5U;
    put_u32(&live.data[11], 0x41280000U); /* Independent IEEE-754: 10.5 A. */
    inject_packet(&live);
    scp_packet_t trip = captured_notification(RF_SCP_CMD_TRIP_NOTIFY);
    trip.data[2] = 0U;
    trip.data[3] = 5U;
    put_u32(&trip.data[6], 0U);
    fake_tick = 30000U;
    inject_packet(&trip);
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.has_trip);
    TEST_ASSERT_FALSE(data.is_online);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.5f, data.live.current_amps);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, data.trip.current_amps);
    TEST_ASSERT_EQUAL_UINT32(30000U, data.last_trip_ms);
    trip.data[2] = 1U;
    inject_packet(&trip);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_EQUAL_UINT8(0U, data.trip.zone);
}

void test_trip_failed_clears_only_with_advancing_uptime_in_same_boot(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    packet.data[28] = 1U;
    put_u32(&packet.data[5], 100U);
    inject_packet(&packet);
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.trip_failed);
    TEST_ASSERT_FALSE(rf_ack_trip_failure(5U));
    packet.data[28] = 0U;
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.trip_failed);
    put_u32(&packet.data[5], 110U);
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    TEST_ASSERT_FALSE(data.trip_failure_latched);
}

void test_ay_restart_keeps_trip_failure_until_operator_acknowledgement(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    packet.data[28] = 1U;
    put_u32(&packet.data[5], 100U);
    inject_packet(&packet);
    packet.data[28] = 0U;
    put_u32(&packet.data[5], 0U);
    inject_packet(&packet);
    put_u32(&packet.data[5], 10U);
    inject_packet(&packet);
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.trip_failed);
    TEST_ASSERT_TRUE(data.trip_failure_latched);
    char *args[] = {"alarm-ack", "1", "1"};
    TEST_ASSERT_EQUAL_INT(0, rf_shell_alarm_ack(3, args));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_FALSE(data.trip_failed);
    TEST_ASSERT_FALSE(data.trip_failure_latched);
}

void test_hub_restart_invalidates_freshness_but_preserves_ay_failure(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    packet.data[28] = 1U;
    put_u32(&packet.data[5], 100U);
    inject_packet(&packet);
    notify_boot(1U);
    rf_comm_periodic_jobs();
    deliver_response(SCP_TYPE_ACK, 0U);
    complete_inventory();
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_FALSE(data.is_online);
    TEST_ASSERT_TRUE(data.trip_failed);
    packet.data[28] = 0U;
    put_u32(&packet.data[5], 0U);
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.is_online);
    TEST_ASSERT_TRUE(data.trip_failure_latched);
}

void test_anomaly_reset_is_independent_of_protection_and_boot_order(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_ANOMALY_REPORT);
    packet.data[0] = 5U;
    packet.data[1] = 1U;
    packet.data[8] = 0U;
    inject_packet(&packet);
    packet.data[8] = 1U;
    inject_packet(&packet);
    rf_anomaly_data_t data;

    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 0U, &data));
    TEST_ASSERT_TRUE(data.is_active);
    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 1U, &data));
    TEST_ASSERT_TRUE(data.is_active);
    packet.data[0] = 0xFFU;
    packet.data[1] = 2U;
    inject_packet(&packet);
    notify_boot(1U);
    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 0U, &data));
    TEST_ASSERT_FALSE(data.is_active);
    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 1U, &data));
    TEST_ASSERT_FALSE(data.is_active);
    packet.data[0] = 5U;
    packet.data[1] = 1U;
    inject_packet(&packet);
    notify_boot(1U);
    packet.data[0] = 0xFFU;
    packet.data[1] = 2U;
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_anomaly(5U, 1U, &data));
    TEST_ASSERT_FALSE(data.is_active);
}

void test_nonfinite_measurement_is_flagged_without_losing_signed_metadata(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    put_u32(&packet.data[11], 0x7FC00000U);
    packet.data[23] = 0U;
    packet.data[24] = 0x80U;
    packet.data[25] = 0x80U;
    inject_packet(&packet);
    rf_phase_data_t data;

    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.is_online);
    TEST_ASSERT_FALSE(data.current_valid);
    TEST_ASSERT_TRUE(isnan(data.live.current_amps));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, data.live.temperature);
    TEST_ASSERT_EQUAL_INT8(-128, data.live.rssi);
    char *args[] = {"live", "1", "1"};
    TEST_ASSERT_EQUAL_INT(0, rf_shell_live(3, args));
}

void test_invalid_data_getters_and_packets_preserve_previous_live_state(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    inject_packet(&packet);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    rf_phase_data_t previous = data;

    packet.data_len = 32U;
    inject_packet(&packet);
    packet.data_len = 33U;
    packet.data[27] = 2U;
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &data, sizeof(data));
    TEST_ASSERT_FALSE(rf_get_phase_data(SIZE_MAX, PHASE_L1, 0U, &data));
    TEST_ASSERT_FALSE(rf_get_phase_data(0U, PHASE_ALL, 0U, &data));
    TEST_ASSERT_FALSE(rf_get_phase_data(0U, PHASE_L2, 0U, &data));
    TEST_ASSERT_FALSE(rf_get_source_data(0U, 0U, &data));
    TEST_ASSERT_FALSE(rf_get_source_data(5U, 0U, NULL));
    TEST_ASSERT_FALSE(rf_handle_live(NULL, 0U));
    TEST_ASSERT_FALSE(rf_handle_trip(NULL, 0U));
    TEST_ASSERT_FALSE(rf_handle_anomaly(NULL));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &data, sizeof(data));
}

void test_monitor_json_exposes_r1_types_without_obsolete_fields(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    packet.data[23] = 0x2CU;
    packet.data[24] = 1U; /* 300 C: no old int8 clipping. */
    inject_packet(&packet);
    char json[2048];
    size_t length = 0U;

    TEST_ASSERT_TRUE(rf_json_monitor_build(json, sizeof(json), 0,
                                          fake_tick, &length));
    TEST_ASSERT_EQUAL_size_t(strlen(json), length);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Eui64\":\"1100000000000000\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Temp\":300"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"RSSI\":-53"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Irms\":"));
    TEST_ASSERT_NULL(strstr(json, "DEVICEID"));
    TEST_ASSERT_NULL(strstr(json, "LQI"));
    TEST_ASSERT_NULL(strstr(json, "FazHataAkimi"));
    put_u32(&packet.data[11], 0x7FC00000U);
    inject_packet(&packet);
    TEST_ASSERT_TRUE(rf_json_monitor_build(json, sizeof(json), 0,
                                          fake_tick, &length));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"Irms\":null"));
    TEST_ASSERT_NULL(strstr(json, "NaN"));
}

void test_monitor_json_small_buffers_fail_without_writing_past_capacity(void)
{
    uint8_t guarded[20];
    size_t length = 123U;

    (void)memset(guarded, 0xA5, sizeof(guarded));
    TEST_ASSERT_FALSE(rf_json_monitor_build((char *)&guarded[1], 8U, -1,
                                           0U, &length));
    TEST_ASSERT_EQUAL_size_t(0U, length);
    TEST_ASSERT_EQUAL_UINT8(0U, guarded[1]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, guarded[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, guarded[9]);
    TEST_ASSERT_FALSE(rf_json_monitor_build(NULL, 8U, 0, 0U, &length));
    TEST_ASSERT_FALSE(rf_json_monitor_build((char *)guarded, 0U, 0,
                                           0U, &length));
    TEST_ASSERT_FALSE(rf_json_monitor_build((char *)guarded, sizeof(guarded),
                                           -2, 0U, &length));
}

void test_synthetic_r1_samples_never_override_live_data_or_config(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    inject_packet(&packet);
    rf_phase_data_t data;
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    rf_phase_data_t previous = data;
    rf_feeder_t previous_config = feeder;
    rf_dummy_init();
    rf_dummy_tick();
    rf_scp_live_t sample;

    TEST_ASSERT_TRUE(rf_dummy_get_live(5U, &sample));
    TEST_ASSERT_EQUAL_INT8(-75, sample.rssi);
    TEST_ASSERT_FALSE(rf_dummy_get_live(0U, &sample));
    TEST_ASSERT_FALSE(rf_dummy_get_live(5U, NULL));
    TEST_ASSERT_TRUE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &data, sizeof(data));
    TEST_ASSERT_EQUAL_MEMORY(&previous_config, &feeder, sizeof(feeder));
}

void test_full_r1_monitor_response_fits_the_existing_http_buffer(void)
{
    for (size_t line = 0U; line < 4U; line++)
    {
        rf_feeder_t *config;

        switch (line)
        {
            case 0U: config = &feeder; break;
            case 1U: config = &second_feeder; break;
            default: config = &extra_feeders[line - 2U]; break;
        }
        config->in_use = true;
        config->config.fider_id = (uint8_t)(line + 1U);
        config->r_eui64[0] = (uint8_t)(line * 3U + 1U);
        config->s_eui64[0] = (uint8_t)(line * 3U + 2U);
        config->t_eui64[0] = (uint8_t)(line * 3U + 3U);
    }
    load_one_phase();
    for (uint8_t line = 1U; line <= 4U; line++)
    {
        for (uint8_t phase = 1U; phase <= 3U; phase++)
        {
            rf_scp_live_t live = {0};
            live.source = (uint8_t)(((uint32_t)line << 2U) | phase);
            live.seq = UINT32_MAX;
            live.uptime_sec = UINT32_MAX;
            live.current_amps = 3.402823466e38f;
            live.trip_voltage = 3.402823466e38f;
            live.harvest_voltage = 3.402823466e38f;
            live.temperature = INT16_MIN;
            live.rssi = INT8_MIN;
            live.log_pending = 100U;
            live.fault_count = 255U;
            live.flags = 255U;
            live.log_wrap_low = 255U;
            TEST_ASSERT_TRUE(rf_handle_live(&live, 0U));
            rf_scp_trip_t trip = {0};
            trip.source = live.source;
            trip.event = 1U;
            trip.current_amps = live.current_amps;
            trip.fault_count = 255U;
            TEST_ASSERT_TRUE(rf_handle_trip(&trip, UINT32_MAX));
            rf_scp_anomaly_t anomaly =
            {
                .source = live.source, .state = 0U,
                .window = UINT16_MAX, .total = UINT32_MAX, .path = 0U
            };
            TEST_ASSERT_TRUE(rf_handle_anomaly(&anomaly));
            anomaly.path = 1U;
            TEST_ASSERT_TRUE(rf_handle_anomaly(&anomaly));
        }
    }
    char json[8192];
    size_t length;

    TEST_ASSERT_TRUE(rf_json_monitor_build(json, sizeof(json), -1,
                                          UINT32_MAX, &length));
    TEST_ASSERT_LESS_THAN_size_t(8192U, length);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"LineId\":7"));
    TEST_ASSERT_NULL(strstr(json, "NaN"));
    TEST_ASSERT_NULL(strstr(json, "INF"));
}

void test_ack_moves_and_deletes_the_effective_eui_binding(void)
{
    load_one_phase();
    rf_inventory_entry_t entry;
    TEST_ASSERT_TRUE(rf_inventory_get_binding(5U, &entry));
    TEST_ASSERT_EQUAL_UINT8(0x11U, entry.eui64[0]);
    rf_inventory_entry_t moved = entry;
    moved.phase = 2U;
    TEST_ASSERT_TRUE(rf_inventory_update(&moved, command_done));
    TEST_ASSERT_TRUE(rf_inventory_get_binding(5U, &entry));
    TEST_ASSERT_FALSE(rf_inventory_get_binding(6U, &entry));
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_FALSE(rf_inventory_get_binding(5U, &entry));
    TEST_ASSERT_TRUE(rf_inventory_get_binding(6U, &entry));
    moved.feeder = 0U;
    TEST_ASSERT_TRUE(rf_inventory_update(&moved, command_done));
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_FALSE(rf_inventory_get_binding(6U, &entry));
}

void test_same_eui_move_preserves_its_latched_failure(void)
{
    load_one_phase();
    scp_packet_t packet = captured_notification(RF_SCP_CMD_LIVE_DATA);
    packet.data[0] = 5U;
    put_u32(&packet.data[5], 100U);
    packet.data[28] = 1U;
    inject_packet(&packet);
    put_u32(&packet.data[5], 0U);
    packet.data[28] = 0U;
    inject_packet(&packet);
    rf_inventory_entry_t entry =
    {
        .zone = 0U, .feeder = 1U, .phase = 2U, .eui64 = {0x11U}
    };
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    deliver_response(SCP_TYPE_ACK, 0U);
    packet.data[0] = 6U;
    put_u32(&packet.data[5], 10U);
    inject_packet(&packet);
    rf_phase_data_t data;

    TEST_ASSERT_FALSE(rf_get_source_data(5U, fake_tick, &data));
    TEST_ASSERT_TRUE(rf_get_source_data(6U, fake_tick, &data));
    TEST_ASSERT_TRUE(data.trip_failure_latched);
    TEST_ASSERT_TRUE(data.trip_failed);
}

void test_failed_update_does_not_replace_the_effective_ack_identity(void)
{
    load_one_phase();
    rf_inventory_entry_t entry =
    {
        .zone = 0U, .feeder = 1U, .phase = 1U, .eui64 = {0x22U}
    };
    TEST_ASSERT_TRUE(rf_inventory_update(&entry, command_done));
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_INVALID_PARAM);
    TEST_ASSERT_TRUE(rf_inventory_get_binding(5U, &entry));
    TEST_ASSERT_EQUAL_UINT8(0x11U, entry.eui64[0]);
    rf_inventory_entry_t previous = entry;
    TEST_ASSERT_FALSE(rf_inventory_get_binding(0U, &entry));
    TEST_ASSERT_FALSE(rf_inventory_get_binding(5U, NULL));
    rf_inventory_record_ack(NULL);
    TEST_ASSERT_EQUAL_MEMORY(&previous, &entry, sizeof(entry));
}

static const rf_scp_vector_t *event_vector(uint8_t cmd, uint8_t type)
{
    const size_t count = sizeof(rf_scp_vectors) /
                         sizeof(rf_scp_vectors[0]);

    for (size_t index = 0U; index < count; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];

        if ((0 == strcmp("AY_05b_olay_kaydi_cekme.csv", vector->source)) &&
            (type == vector->logical[2]) &&
            (cmd == vector->logical[3]))
        {
            return vector;
        }
    }
    TEST_FAIL_MESSAGE("Required event capture is missing");
    return NULL;
}

static void receive_event_capture(uint8_t cmd, uint8_t type)
{
    const rf_scp_vector_t *vector = event_vector(cmd, type);

    receive_frame(vector->wire, vector->wire_len);
    rf_comm_check_rx();
}

static void send_event_capture(uint8_t cmd, uint8_t type)
{
    const rf_scp_vector_t *vector = event_vector(cmd, type);
    const uint8_t *data = vector->logical;
    scp_packet_t request = {0};

    request.type = type;
    request.cmd = cmd;
    request.data_len = data[5];
    (void)memcpy(request.data, &data[7], request.data_len);
    TEST_ASSERT_TRUE(scp_send_request(&request, command_done));
    TEST_ASSERT_EQUAL_HEX8(data[0], transmitted.dst);
    TEST_ASSERT_EQUAL_HEX8(data[1], transmitted.src);
    TEST_ASSERT_EQUAL_HEX8(data[4], transmitted.seq);
    TEST_ASSERT_EQUAL_UINT8(request.data_len, transmitted.data_len);
    if (0U < request.data_len)
    {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(request.data, transmitted.data,
                                     request.data_len);
    }
}

void test_captured_event_commands_round_trip_through_uart_and_scp(void)
{
    rf_scp_message_t message;
    rf_scp_event_t event;

    cmd_next_seq = 0U;
    receive_event_capture(RF_SCP_CMD_LOG_AVAILABLE, SCP_TYPE_SET);
    TEST_ASSERT_EQUAL_UINT32(0U, transmit_calls);
    send_event_capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_GET);
    receive_event_capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&done_packet, &message));
    TEST_ASSERT_EQUAL_UINT16(37U, message.body.log_head.head);
    TEST_ASSERT_EQUAL_UINT16(42U, message.body.log_head.wrap);
    TEST_ASSERT_EQUAL_UINT32(4237U, message.body.log_head.total);
    TEST_ASSERT_EQUAL_UINT16(36U, message.body.log_head.tail);

    send_event_capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_GET);
    receive_event_capture(RF_SCP_CMD_LOG_READ_RANGE, SCP_TYPE_ACK);
    TEST_ASSERT_EQUAL_UINT32(2U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&done_packet, &message));
    TEST_ASSERT_EQUAL_UINT8(1U, message.body.log_batch.count);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(message.body.log_batch.records, 60U, &event));
    TEST_ASSERT_EQUAL_UINT32(280U, event.duration_ms);

    /* Exercise the command transport; durable storage is a later gate. */
    send_event_capture(RF_SCP_CMD_LOG_CONSUME_TO, SCP_TYPE_SET);
    receive_event_capture(RF_SCP_CMD_LOG_CONSUME_TO, SCP_TYPE_ACK);
    TEST_ASSERT_EQUAL_UINT32(3U, done_calls);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&done_packet, &message));
    TEST_ASSERT_EQUAL_UINT16(37U, message.body.log_consume.tail);
    TEST_ASSERT_EQUAL_UINT16(0U, message.body.log_consume.left);
    TEST_ASSERT_TRUE(scp_is_free());
}

void test_event_notification_has_no_ack_and_cannot_complete_pending_read(void)
{
    cmd_next_seq = 0U;
    send_event_capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_GET);
    receive_event_capture(RF_SCP_CMD_LOG_AVAILABLE, SCP_TYPE_SET);
    TEST_ASSERT_EQUAL_UINT32(1U, event_notify_calls);
    TEST_ASSERT_EQUAL_UINT16(1U, event_notification.body.log_available.pending);
    TEST_ASSERT_EQUAL_UINT16(37U, event_notification.body.log_available.head);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, done_calls);
    TEST_ASSERT_FALSE(scp_is_free());
    receive_event_capture(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_OK, done_result);
}

/*** end of file ***/
