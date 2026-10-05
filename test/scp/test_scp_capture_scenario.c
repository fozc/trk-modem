/*
 * test_scp_capture_scenario.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify production SCP framing against captured BOLATeX R1 bytes.
 */

#include "unity.h"
#include "scp.h"
#include "cobs.h"
#include "../fixtures/rf_scp_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("cobs.c")

#define CAPTURE_VECTOR_COUNT (sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]))

static uint8_t transmitted[SCP_FRAME_MAX_SIZE];
static size_t transmitted_len;
static uint32_t fake_tick;

static uint32_t get_tick(void)
{
    return fake_tick;
}

static void capture_transmit(const uint8_t *frame, size_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(transmitted), length);
    (void)memcpy(transmitted, frame, length);
    transmitted_len = length;
}

static void feed_bytes(scp_t *receiver, const uint8_t *bytes, size_t length)
{
    for (size_t index = 0U; index < length; index++)
    {
        scp_process_byte(receiver, bytes[index]);
    }
}

static void assert_packet(const scp_t *receiver,
                          const rf_scp_vector_t *vector)
{
    const scp_packet_t *packet = scp_get_packet(receiver);
    const uint8_t *logical = vector->logical;

    TEST_ASSERT_NOT_NULL_MESSAGE(packet, vector->source);
    TEST_ASSERT_EQUAL_HEX8(logical[0], packet->dst);
    TEST_ASSERT_EQUAL_HEX8(logical[1], packet->src);
    TEST_ASSERT_EQUAL_HEX8(logical[2], packet->type);
    TEST_ASSERT_EQUAL_HEX8(logical[3], packet->cmd);
    TEST_ASSERT_EQUAL_HEX8(logical[4], packet->seq);
    TEST_ASSERT_EQUAL_UINT8(logical[5], packet->data_len);
    TEST_ASSERT_EQUAL_size_t((size_t)packet->data_len + SCP_OVERHEAD_SIZE,
                            vector->logical_len);
    if (0U < packet->data_len)
    {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&logical[7], packet->data,
                                    packet->data_len);
    }
}

static void init_receiver(scp_t *receiver, uint8_t address)
{
    TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK,
                          scp_init(receiver, address, capture_transmit,
                                   get_tick, 100U));
}

void setUp(void)
{
    fake_tick = 0U;
    transmitted_len = 0U;
    (void)memset(transmitted, 0, sizeof(transmitted));
}

void tearDown(void)
{
}

void test_all_captured_frames_decode_with_production_parser(void)
{
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        scp_t receiver;

        init_receiver(&receiver, vector->logical[0]);
        feed_bytes(&receiver, vector->wire, vector->wire_len);
        assert_packet(&receiver, vector);
    }
}

void test_encoder_matches_captured_wire_without_building_expectations(void)
{
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        const uint8_t *logical = vector->logical;
        scp_packet_t packet = {0};
        scp_t sender;

        packet.dst = logical[0];
        packet.src = logical[1];
        packet.type = logical[2];
        packet.cmd = logical[3];
        packet.seq = logical[4];
        packet.data_len = logical[5];
        (void)memcpy(packet.data, &logical[7], packet.data_len);
        init_receiver(&sender, packet.src);
        TEST_ASSERT_EQUAL_INT(SCP_STATUS_OK, scp_send(&sender, &packet));
        TEST_ASSERT_EQUAL_size_t(vector->wire_len, transmitted_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(vector->wire, transmitted,
                                            vector->wire_len,
                                            vector->source);
    }
}

void test_frames_decode_across_every_uart_chunk_boundary(void)
{
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];

        for (size_t split = 1U; split < vector->wire_len; split++)
        {
            scp_t receiver;

            init_receiver(&receiver, vector->logical[0]);
            feed_bytes(&receiver, vector->wire, split);
            TEST_ASSERT_FALSE(scp_packet_ready(&receiver));
            fake_tick += 99U;
            feed_bytes(&receiver, &vector->wire[split],
                       vector->wire_len - split);
            assert_packet(&receiver, vector);
        }
    }
}

void test_incoming_frames_survive_console_text_and_back_to_back_frames(void)
{
    static const uint8_t console_text[] = "MH boot\r\nRF cycle complete\r\n";
    scp_t receiver;

    init_receiver(&receiver, 0x02U);
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];

        if (0x02U == vector->logical[0])
        {
            feed_bytes(&receiver, console_text, sizeof(console_text) - 1U);
            feed_bytes(&receiver, vector->wire, vector->wire_len);
            assert_packet(&receiver, vector);
            scp_packet_done(&receiver);
            TEST_ASSERT_FALSE(scp_packet_ready(&receiver));
        }
    }
}

void test_bad_crc_is_rejected_and_following_capture_is_received(void)
{
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        uint8_t logical[SCP_PACKET_MAX_SIZE];
        uint8_t frame[SCP_FRAME_MAX_SIZE];
        size_t encoded_len;
        scp_t receiver;

        (void)memcpy(logical, vector->logical, vector->logical_len);
        logical[vector->logical_len - 1U] ^= 0x01U;
        frame[0] = SCP_DELIMITER;
        TEST_ASSERT_TRUE(cobs_encode(logical, vector->logical_len, &frame[1],
                                     SCP_COBS_MAX_SIZE, &encoded_len));
        TEST_ASSERT_GREATER_THAN_size_t(0U, encoded_len);
        frame[encoded_len + 1U] = SCP_DELIMITER;
        init_receiver(&receiver, vector->logical[0]);
        feed_bytes(&receiver, frame, encoded_len + 2U);
        TEST_ASSERT_FALSE_MESSAGE(scp_packet_ready(&receiver), vector->source);
        feed_bytes(&receiver, vector->wire, vector->wire_len);
        assert_packet(&receiver, vector);
    }
}

void test_partial_capture_times_out_and_next_capture_is_received(void)
{
    for (size_t index = 0U; index < CAPTURE_VECTOR_COUNT; index++)
    {
        const rf_scp_vector_t *vector = &rf_scp_vectors[index];
        scp_t receiver;

        init_receiver(&receiver, vector->logical[0]);
        feed_bytes(&receiver, vector->wire, vector->wire_len - 1U);
        TEST_ASSERT_FALSE(scp_packet_ready(&receiver));
        fake_tick += 100U;
        feed_bytes(&receiver, vector->wire, vector->wire_len);
        assert_packet(&receiver, vector);
    }
}

/*** end of file ***/
