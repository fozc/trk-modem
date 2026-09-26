/*
 * test_rf_scp.c
 *
 *  Created on: Aug 21, 2026
 *      Author: fatih
 *
 * Host tests for rf_scp - the pure SCP wire codec:
 * request builder, PING reply builder and GET_STATUS response decoder.
 *
 * (The request/response state machine now lives in rf_comm.c, which is
 * exercised on target; it depends on Contiki/HAL and is not built here.)
 *
 * Usage: make -f Makefile.scp run  (test/integration/rf)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "scp.h"
#include "rf_scp.h"

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                          \
    do                                                                  \
    {                                                                   \
        if ((cond) != 0)                                                \
        {                                                               \
            test_pass++;                                                \
            printf("PASS: %s\r\n", (name));                             \
        }                                                               \
        else                                                           \
        {                                                               \
            test_fail++;                                                \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__); \
        }                                                               \
    } while (0)

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

static void test_build_request(void)
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

static void test_build_time_sync(void)
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

static void test_build_ping_reply(void)
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

static void test_decode_status(void)
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

int main(void)
{
    printf("=== rf_scp codec host tests ===\r\n");

    test_build_request();
    test_build_time_sync();
    test_build_ping_reply();
    test_decode_status();

    printf("\r\n=== %u passed, %u failed ===\r\n", test_pass, test_fail);
    return (0U == test_fail) ? 0 : 1;
}

/*** end of file ***/
