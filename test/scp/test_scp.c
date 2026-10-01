/*
 * test_scp.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies SCP framing, parser states, addressing, CRC, and timeout recovery.
 */

#include "unity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "scp.h"

TEST_SOURCE_FILE("cobs.c")

#define TEST_DEVICE_ADDRESS 0x11U
#define TEST_REMOTE_ADDRESS 0x22U

static uint8_t captured_frame[SCP_FRAME_MAX_SIZE];
static size_t captured_len;
static uint32_t fake_tick;

static void capture_transmit(const uint8_t *frame, size_t frame_len)
{
    TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(captured_frame), frame_len);
    memcpy(captured_frame, frame, frame_len);
    captured_len = frame_len;
}

static uint32_t get_fake_tick(void)
{
    return fake_tick;
}

static void feed_frame(scp_t *context, const uint8_t *frame, size_t frame_len)
{
    for (size_t index = 0U; index < frame_len; index++)
    {
        scp_process_byte(context, frame[index]);
    }
}

static scp_packet_t make_packet(uint8_t destination)
{
    scp_packet_t packet = {0};

    packet.dst = destination;
    packet.src = TEST_REMOTE_ADDRESS;
    packet.type = SCP_TYPE_SET;
    packet.cmd = 0x37U;
    packet.seq = 0xA5U;
    packet.data_len = 5U;
    packet.data[0] = 0x00U;
    packet.data[1] = 0x11U;
    packet.data[2] = 0x00U;
    packet.data[3] = 0x22U;
    packet.data[4] = 0x33U;

    return packet;
}

void setUp(void)
{
    memset(captured_frame, 0, sizeof(captured_frame));
    captured_len = 0U;
    fake_tick = 0U;
}

void tearDown(void)
{
}

void test_scp_init_sets_context_and_rejects_null(void)
{
    scp_t context;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_ERROR_NULL_PTR,
                          scp_init(NULL, TEST_DEVICE_ADDRESS,
                                   capture_transmit, get_fake_tick, 50U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&context, TEST_DEVICE_ADDRESS,
                                   capture_transmit, get_fake_tick, 50U));
    TEST_ASSERT_EQUAL_HEX8(TEST_DEVICE_ADDRESS, context.device_address);
    TEST_ASSERT_EQUAL_INT(SCP_STATE_WAIT_SOF, context.rx_state);
    TEST_ASSERT_EQUAL_UINT32(50U, context.timeout_ms);
    TEST_ASSERT_EQUAL_size_t(0U, context.rx_idx);
}

void test_scp_send_and_receive_round_trip_preserves_packet(void)
{
    scp_t transmitter;
    scp_t receiver;
    scp_packet_t input = make_packet(TEST_DEVICE_ADDRESS);
    const scp_packet_t *output;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&transmitter, TEST_REMOTE_ADDRESS,
                                   capture_transmit, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&transmitter, &input));
    TEST_ASSERT_GREATER_THAN_size_t(2U, captured_len);
    TEST_ASSERT_EQUAL_HEX8(SCP_DELIMITER, captured_frame[0]);
    TEST_ASSERT_EQUAL_HEX8(SCP_DELIMITER,
                           captured_frame[captured_len - 1U]);

    feed_frame(&receiver, captured_frame, captured_len);
    TEST_ASSERT_TRUE(scp_packet_ready(&receiver));
    output = scp_get_packet(&receiver);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_HEX8(input.dst, output->dst);
    TEST_ASSERT_EQUAL_HEX8(input.src, output->src);
    TEST_ASSERT_EQUAL_HEX8(input.type, output->type);
    TEST_ASSERT_EQUAL_HEX8(input.cmd, output->cmd);
    TEST_ASSERT_EQUAL_HEX8(input.seq, output->seq);
    TEST_ASSERT_EQUAL_UINT8(input.data_len, output->data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data, output->data,
                                  input.data_len);
}

void test_scp_broadcast_is_received_but_other_address_is_ignored(void)
{
    scp_t transmitter;
    scp_t receiver;
    scp_packet_t packet = make_packet(0x44U);

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&transmitter, TEST_REMOTE_ADDRESS,
                                   capture_transmit, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&transmitter, &packet));
    feed_frame(&receiver, captured_frame, captured_len);
    TEST_ASSERT_FALSE(scp_packet_ready(&receiver));

    packet.dst = SCP_BROADCAST_ADDR;
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&transmitter, &packet));
    feed_frame(&receiver, captured_frame, captured_len);
    TEST_ASSERT_TRUE(scp_packet_ready(&receiver));
}

void test_scp_corrupted_frame_is_rejected_and_parser_resynchronizes(void)
{
    scp_t transmitter;
    scp_t receiver;
    scp_packet_t packet = make_packet(TEST_DEVICE_ADDRESS);

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&transmitter, TEST_REMOTE_ADDRESS,
                                   capture_transmit, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&transmitter, &packet));
    captured_frame[captured_len - 2U] ^= 0x01U;
    feed_frame(&receiver, captured_frame, captured_len);

    TEST_ASSERT_FALSE(scp_packet_ready(&receiver));
    TEST_ASSERT_EQUAL_INT(SCP_STATE_COLLECT, receiver.rx_state);
    TEST_ASSERT_EQUAL_size_t(0U, receiver.rx_idx);
}

void test_scp_packet_ready_freezes_input_until_done(void)
{
    scp_t transmitter;
    scp_t receiver;
    scp_packet_t packet = make_packet(TEST_DEVICE_ADDRESS);
    size_t frozen_index;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&transmitter, TEST_REMOTE_ADDRESS,
                                   capture_transmit, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&transmitter, &packet));
    feed_frame(&receiver, captured_frame, captured_len);
    frozen_index = receiver.rx_idx;

    scp_process_byte(&receiver, 0x55U);
    TEST_ASSERT_EQUAL_INT(SCP_STATE_PACKET_READY, receiver.rx_state);
    TEST_ASSERT_EQUAL_size_t(frozen_index, receiver.rx_idx);
    scp_packet_done(&receiver);
    TEST_ASSERT_EQUAL_INT(SCP_STATE_WAIT_SOF, receiver.rx_state);
    TEST_ASSERT_EQUAL_size_t(0U, receiver.rx_idx);
}

void test_scp_timeout_discards_partial_frame(void)
{
    scp_t receiver;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, get_fake_tick, 10U));
    scp_process_byte(&receiver, SCP_DELIMITER);
    fake_tick = 1U;
    scp_process_byte(&receiver, 0x02U);
    TEST_ASSERT_EQUAL_size_t(1U, receiver.rx_idx);
    fake_tick = 11U;
    scp_process_byte(&receiver, 0x44U);

    TEST_ASSERT_EQUAL_INT(SCP_STATE_WAIT_SOF, receiver.rx_state);
    TEST_ASSERT_EQUAL_size_t(0U, receiver.rx_idx);
}

void test_scp_receive_overflow_resets_collection_index(void)
{
    scp_t receiver;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    scp_process_byte(&receiver, SCP_DELIMITER);
    for (size_t index = 0U; index < SCP_COBS_MAX_SIZE; index++)
    {
        scp_process_byte(&receiver, 0x55U);
    }
    TEST_ASSERT_EQUAL_size_t(SCP_COBS_MAX_SIZE, receiver.rx_idx);
    scp_process_byte(&receiver, 0x55U);
    TEST_ASSERT_EQUAL_size_t(0U, receiver.rx_idx);
    TEST_ASSERT_EQUAL_INT(SCP_STATE_COLLECT, receiver.rx_state);
}

void test_scp_invalid_state_and_null_helpers_are_safe(void)
{
    scp_t context;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&context, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    context.rx_state = (scp_state_t)99;
    context.rx_idx = 7U;
    scp_process_byte(&context, 0x55U);
    TEST_ASSERT_EQUAL_INT(SCP_STATE_WAIT_SOF, context.rx_state);
    TEST_ASSERT_EQUAL_size_t(0U, context.rx_idx);

    scp_process_byte(NULL, 0U);
    TEST_ASSERT_FALSE(scp_packet_ready(NULL));
    TEST_ASSERT_NULL(scp_get_packet(NULL));
    TEST_ASSERT_NULL(scp_get_packet(&context));
    scp_packet_done(NULL);
}

void test_scp_send_validates_arguments_and_payload_length(void)
{
    scp_t context;
    scp_packet_t packet = {0};

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&context, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_ERROR_NULL_PTR,
                          scp_send(NULL, &packet));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_ERROR_NULL_PTR,
                          scp_send(&context, NULL));
    packet.data_len = SCP_MAX_DATA_SIZE + 1U;
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_ERROR_INVALID_PARAM,
                          scp_send(&context, &packet));
    packet.data_len = 0U;
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&context, &packet));
}

void test_scp_ping_builds_zero_payload_packet(void)
{
    scp_t transmitter;
    scp_t receiver;
    const scp_packet_t *packet;

    TEST_ASSERT_EQUAL_INT(SCP_STATUS_ERROR_NULL_PTR,
                          scp_send_ping(NULL, TEST_DEVICE_ADDRESS, 1U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&transmitter, TEST_REMOTE_ADDRESS,
                                   capture_transmit, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(&receiver, TEST_DEVICE_ADDRESS,
                                   NULL, NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_send_ping(&transmitter,
                                        TEST_DEVICE_ADDRESS, 0x42U));
    feed_frame(&receiver, captured_frame, captured_len);
    packet = scp_get_packet(&receiver);

    TEST_ASSERT_NOT_NULL(packet);
    TEST_ASSERT_EQUAL_HEX8(SCP_TYPE_PING, packet->type);
    TEST_ASSERT_EQUAL_HEX8(0U, packet->cmd);
    TEST_ASSERT_EQUAL_HEX8(0x42U, packet->seq);
    TEST_ASSERT_EQUAL_UINT8(0U, packet->data_len);
}

/*** end of file ***/
