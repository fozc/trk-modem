/*
 * test_rf_scp_codec_scenario.c
 *
 *  Created on: Aug 21, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host tests for rf_scp - the pure SCP wire codec:
 * request builder, PING reply builder and GET_STATUS response decoder.
 *
 * The request state machine has its own real-wire host scenario suite.
 *
 * Usage: ceedling test:test_rf_scp_codec_scenario
 */

#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "cobs.h"
#include "scp.h"
#include "rf_scp.h"

#define TEST_CHECK(condition, message) \
    TEST_ASSERT_TRUE_MESSAGE((condition), (message))

static void build_status_ack(scp_packet_t *pkt, uint8_t seq)
{
    (void)memset(pkt, 0, sizeof(*pkt));
    pkt->dst      = RF_SCP_ADDR_RTU;
    pkt->src      = RF_SCP_ADDR_HUB;
    pkt->type     = SCP_TYPE_ACK;
    pkt->cmd      = RF_SCP_CMD_GET_STATUS;
    pkt->seq      = seq;
    pkt->data_len = RF_SCP_STATUS_BODY_LEN;

    pkt->data[0] = 0x44U;   /* uptime = 0x11223344 (LE) */
    pkt->data[1] = 0x33U;
    pkt->data[2] = 0x22U;
    pkt->data[3] = 0x11U;
    (void)memcpy(&pkt->data[4], "v1.2.3", 7U);
    pkt->data[20] = 0x01U;  /* sched_active */
    pkt->data[21] = 0x01U;  /* sched_cycle_count = 0x00000101 (LE) */
    pkt->data[22] = 0x01U;
}

void test_build_request(void)
{
    scp_packet_t req;

    TEST_CHECK(rf_scp_build_request(RF_OP_GET_STATUS, 0U, 7U, NULL, 0U, &req),
               "build_request GET_STATUS step0 ok");
    TEST_CHECK((RF_SCP_ADDR_HUB == req.dst) && (RF_SCP_ADDR_RTU == req.src)
               && (SCP_TYPE_GET == req.type)
               && (RF_SCP_CMD_GET_STATUS == req.cmd)
               && (7U == req.seq) && (0U == req.data_len),
               "build_request header correct");
    TEST_CHECK(!rf_scp_build_request(RF_OP_GET_STATUS, 1U, 0U, NULL, 0U, &req),
               "build_request no step1 for GET_STATUS");
    TEST_CHECK(!rf_scp_build_request(RF_OP_NONE, 0U, 0U, NULL, 0U, &req),
               "build_request rejects unknown operation");
    TEST_CHECK(!rf_scp_build_request(RF_OP_GET_STATUS, 0U, 0U, NULL, 3U,
                                     &req),
               "build_request GET_STATUS rejects body");
}

void test_build_time_sync(void)
{
    scp_packet_t req;
    const uint8_t cp56[RF_SCP_TIME_SYNC_BODY_LEN] =
        {0x34U, 0x12U, 0x1EU, 0x0CU, 0x15U, 0x08U, 0x1AU};

    TEST_CHECK(rf_scp_build_request(RF_OP_TIME_SYNC, 0U, 9U,
                                    cp56, RF_SCP_TIME_SYNC_BODY_LEN, &req),
               "build_request TIME_SYNC step0 ok");
    TEST_CHECK((SCP_TYPE_SET == req.type)
               && (RF_SCP_CMD_TIME_SYNC == req.cmd)
               && (9U == req.seq)
               && (RF_SCP_TIME_SYNC_BODY_LEN == req.data_len),
               "build_request TIME_SYNC header correct");
    TEST_CHECK(0 == memcmp(req.data, cp56, RF_SCP_TIME_SYNC_BODY_LEN),
               "build_request TIME_SYNC body copied verbatim");

    TEST_CHECK(!rf_scp_build_request(RF_OP_TIME_SYNC, 0U, 0U, NULL, 7U, &req),
               "build_request TIME_SYNC rejects NULL body");
    TEST_CHECK(!rf_scp_build_request(RF_OP_TIME_SYNC, 0U, 0U, cp56, 6U,
                                     &req),
               "build_request TIME_SYNC rejects wrong length");
    TEST_CHECK(!rf_scp_build_request(RF_OP_TIME_SYNC, 1U, 0U, cp56, 7U,
                                     &req),
               "build_request no step1 for TIME_SYNC");
}

void test_build_ping_reply(void)
{
    scp_packet_t ping;
    scp_packet_t ack;

    (void)memset(&ping, 0, sizeof(ping));
    ping.dst  = RF_SCP_ADDR_RTU;
    ping.src  = RF_SCP_ADDR_HUB;
    ping.type = SCP_TYPE_PING;
    ping.cmd  = 0x04U;
    ping.seq  = 0x2AU;

    TEST_CHECK(rf_scp_build_ping_reply(&ping, &ack), "ping reply built");
    TEST_CHECK((SCP_TYPE_ACK == ack.type) && (RF_SCP_ADDR_HUB == ack.dst)
               && (RF_SCP_ADDR_RTU == ack.src) && (ping.cmd == ack.cmd)
               && (ping.seq == ack.seq) && (0U == ack.data_len),
               "ping reply echoes cmd/seq as empty ACK");

    ping.dst = RF_SCP_ADDR_BROADCAST;
    TEST_CHECK(!rf_scp_build_ping_reply(&ping, &ack),
               "broadcast PING gets no reply");
}

void test_decode_status(void)
{
    scp_packet_t    pkt;
    rf_hub_status_t st;

    build_status_ack(&pkt, 0x07U);

    TEST_CHECK(RF_CMD_OK == rf_scp_decode_status(&pkt, &st),
               "decode_status returns OK");
    TEST_CHECK(0x11223344U == st.uptime_sec, "decode_status uptime LE");
    TEST_CHECK(0 == strcmp(st.fw_version, "v1.2.3"),
               "decode_status fw_version NUL-terminated");
    TEST_CHECK(1U == st.sched_active, "decode_status sched_active");
    TEST_CHECK(0x00000101U == st.sched_cycle_count,
               "decode_status sched_cycle_count LE");

    pkt.data_len = 24U;
    TEST_CHECK(RF_CMD_ERR_LEN == rf_scp_decode_status(&pkt, &st),
               "decode_status rejects wrong length");
    TEST_CHECK(RF_CMD_ERR_NULL == rf_scp_decode_status(NULL, &st),
               "decode_status rejects NULL");
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_status_every_invalid_length_preserves_previous_output(void)
{
    scp_packet_t packet;
    build_status_ack(&packet, 7U);
    rf_hub_status_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (uint16_t length = 0U; 256U > length; length++)
    {
        if (RF_SCP_STATUS_BODY_LEN == length)
        {
            continue;
        }
        packet.data_len = (uint8_t)length;
        rf_hub_status_t output = previous;
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
            rf_scp_decode_status(&packet, &output));
        TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
    }
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
        rf_scp_decode_status(&packet, NULL));
}

void test_status_unterminated_firmware_label_is_bounded_and_nul_terminated(void)
{
    scp_packet_t packet;
    build_status_ack(&packet, 7U);
    (void)memset(&packet.data[4], 'A', RF_SCP_FW_LABEL_LEN);
    rf_hub_status_t output;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_status(&packet, &output));
    TEST_ASSERT_EQUAL_size_t(RF_SCP_FW_LABEL_LEN - 1U,
                             strlen(output.fw_version));
    TEST_ASSERT_EQUAL_UINT32(0x00000101U, output.sched_cycle_count);
}

void test_request_step_and_body_length_matrix_accepts_only_defined_operations(void)
{
    const uint8_t body[7] = {0};
    for (uint16_t step = 0U; 256U > step; step++)
    {
        for (uint8_t length = 0U; 9U > length; length++)
        {
            scp_packet_t output;
            TEST_ASSERT_EQUAL_INT((0U == step) && (0U == length),
                rf_scp_build_request(RF_OP_GET_STATUS, (uint8_t)step,
                                      255U, NULL, length, &output));
            TEST_ASSERT_EQUAL_INT((0U == step) && (7U == length),
                rf_scp_build_request(RF_OP_TIME_SYNC, (uint8_t)step,
                                      255U, body, length, &output));
        }
    }
    TEST_ASSERT_FALSE(rf_scp_build_request(RF_OP_NONE, 0U, 0U,
                                           body, 7U, NULL));
}

void test_ping_null_or_broadcast_does_not_overwrite_a_prepared_ack(void)
{
    scp_packet_t ping = {.dst = RF_SCP_ADDR_BROADCAST};
    scp_packet_t output;
    (void)memset(&output, 0xA5, sizeof(output));
    const scp_packet_t previous = output;
    TEST_ASSERT_FALSE(rf_scp_build_ping_reply(NULL, &output));
    TEST_ASSERT_FALSE(rf_scp_build_ping_reply(&ping, NULL));
    TEST_ASSERT_FALSE(rf_scp_build_ping_reply(&ping, &output));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
}

/*** end of file ***/
