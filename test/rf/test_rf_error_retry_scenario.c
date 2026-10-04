/*
 * test_rf_error_retry_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify bounded RF transient retries through real command/inventory paths.
 */
#include "unity.h"
#include "mock_bsp.h"
#include "scp.h"
#include "cobs.h"
#include "rf_scp.h"
#include <string.h>

static uint32_t fake_tick;
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

#include "../../Application/rf/rf_comm.c"
#include "../../Application/rf/rf_inventory.c"

static scp_t hub_receiver;
static scp_packet_t transmitted;
static uint32_t transmit_calls;
static uint32_t done_calls;
static scp_cmd_result_t done_result;
static rf_feeder_t feeder;

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
    (void)packet;
    done_calls++;
    done_result = result;
}

const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    return (0U == index) ? &feeder : NULL;
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
    packet.cmd = transmitted.cmd;
    packet.seq = transmitted.seq;
    if (SCP_TYPE_ERROR == type)
    {
        packet.data_len = 1U;
        packet.data[0] = error;
    }
    scp_on_response(&packet);
}

void setUp(void)
{
    fake_tick = 0U;
    transmit_calls = 0U;
    done_calls = 0U;
    done_result = SCP_CMD_OK;
    memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    cmd_next_seq = 1U;
    active = false;
    loaded = false;
    memset(&feeder, 0, sizeof(feeder));
    feeder.in_use = true;
    feeder.config.fider_id = 1U;
    feeder.r_eui64[0] = 0x11U;
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
        scp_init(&hub_receiver, RF_SCP_ADDR_HUB, NULL, HAL_GetTick, 100U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
        scp_init(&scp_ctx, RF_SCP_ADDR_RTU, capture_frame, HAL_GetTick, 100U));
}

void tearDown(void)
{
}

void test_not_available_retries_twice_then_finishes_with_error(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
    const scp_packet_t first = transmitted;
    for (uint8_t retry = 0U; retry < 2U; retry++)
    {
        fake_tick += 100U;
        deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
        TEST_ASSERT_EQUAL_UINT32(2U + retry, transmit_calls);
        TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
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
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    TEST_ASSERT_EQUAL_UINT32(2U, transmit_calls);
    TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
    TEST_ASSERT_EQUAL_UINT8(0U, skipped_count);
    TEST_ASSERT_EQUAL_UINT8(0U, sent_count);
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_ctx.retries_left);
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

void test_busy_and_permanent_errors_finish_without_retry(void)
{
    const uint8_t errors[] =
    {
        RF_SCP_ERR_BUSY, RF_SCP_ERR_UNKNOWN_CMD,
        RF_SCP_ERR_INVALID_PARAM, RF_SCP_ERR_NOT_SUPPORTED
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

void test_inventory_end_retries_not_available_twice_then_accepts_ack(void)
{
    feeder.in_use = false;
    rf_inventory_start();
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_CMD_INVENTORY_END, transmitted.cmd);
    const scp_packet_t first = transmitted;
    for (uint8_t retry = 0U; retry < 2U; retry++)
    {
        deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
        TEST_ASSERT_EQUAL_UINT32(2U + retry, transmit_calls);
        TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
        TEST_ASSERT_TRUE(rf_inventory_is_active());
        TEST_ASSERT_FALSE(rf_inventory_is_loaded());
    }
    deliver_response(SCP_TYPE_ACK, 0U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_TRUE(rf_inventory_is_loaded());
    TEST_ASSERT_FALSE(rf_inventory_is_active());
}

void test_empty_error_body_finishes_without_reading_error_code(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
    scp_packet_t packet = {0};
    packet.cmd = transmitted.cmd;
    packet.seq = transmitted.seq;
    packet.type = SCP_TYPE_ERROR;
    packet.data[0] = RF_SCP_ERR_NOT_AVAILABLE;
    scp_on_response(&packet);
    TEST_ASSERT_EQUAL_UINT32(1U, transmit_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, done_calls);
    TEST_ASSERT_EQUAL_INT(SCP_CMD_ERR, done_result);
}

void test_not_available_and_timeout_share_existing_retry_budget(void)
{
    TEST_ASSERT_TRUE(scp_send_command(SCP_TYPE_GET, RF_SCP_CMD_GET_STATUS,
        NULL, 0U, 500U, 2U, command_done));
    const scp_packet_t first = transmitted;
    deliver_response(SCP_TYPE_ERROR, RF_SCP_ERR_NOT_AVAILABLE);
    scp_process(500U);
    TEST_ASSERT_EQUAL_UINT32(3U, transmit_calls);
    TEST_ASSERT_EQUAL_MEMORY(&first, &transmitted, sizeof(first));
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
    ping.cmd = 0x04U;
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

/*** end of file ***/
