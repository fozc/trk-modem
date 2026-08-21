/*
 * test_rf_scp.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * Host tests for the SCP application layer: rf_scp_cmd body codec and the
 * rf_scp_engine single-outstanding master (SEQ, timeout, retry, link-down,
 * busy rejection, response matching). Time and packets are injected.
 *
 * Usage: make -f Makefile.scp run  (Application/rf/test)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "scp.h"
#include "rf_scp_cmd.h"
#include "rf_scp_engine.h"

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

/* ----------------------------------------------------------------------
 * Capture harness for the injected engine callbacks
 * ---------------------------------------------------------------------- */

static scp_packet_t  cap_sent[16];
static unsigned int  cap_sent_count;

static rf_job_kind_t cap_done_kind;
static rf_result_t   cap_done_result;
static unsigned int  cap_done_count;
static bool          cap_done_had_rsp;

static void cap_reset(void)
{
    cap_sent_count   = 0U;
    cap_done_count   = 0U;
    cap_done_had_rsp = false;
}

static void cap_send(const scp_packet_t *req)
{
    if (cap_sent_count < (sizeof(cap_sent) / sizeof(cap_sent[0])))
    {
        cap_sent[cap_sent_count] = *req;
        cap_sent_count++;
    }
}

static void cap_done(rf_job_kind_t kind, rf_result_t result,
                     const scp_packet_t *rsp)
{
    cap_done_kind    = kind;
    cap_done_result  = result;
    cap_done_had_rsp = (NULL != rsp);
    cap_done_count++;
}

/* ----------------------------------------------------------------------
 * rf_scp_cmd: GET_STATUS body codec
 * ---------------------------------------------------------------------- */

static void build_status_ack(scp_packet_t *pkt, uint8_t seq)
{
    (void)memset(pkt, 0, sizeof(*pkt));
    pkt->dst      = RF_SCP_ADDR_RTU;
    pkt->src      = RF_SCP_ADDR_HUB;
    pkt->type     = SCP_TYPE_ACK;
    pkt->cmd      = RF_SCP_CMD_GET_STATUS;
    pkt->seq      = seq;
    pkt->data_len = RF_SCP_STATUS_BODY_LEN;

    /* uptime_sec = 0x11223344 (LE) */
    pkt->data[0] = 0x44U;
    pkt->data[1] = 0x33U;
    pkt->data[2] = 0x22U;
    pkt->data[3] = 0x11U;

    /* fw_version "v1.2.3" then NUL padding; [19] stays NUL */
    (void)memcpy(&pkt->data[4], "v1.2.3", 7U);

    pkt->data[20] = 0x01U;   /* sched_active */

    /* sched_cycle_count = 0x00000101 (LE) */
    pkt->data[21] = 0x01U;
    pkt->data[22] = 0x01U;
    pkt->data[23] = 0x00U;
    pkt->data[24] = 0x00U;
}

static void test_decode_status_ok(void)
{
    scp_packet_t    pkt;
    rf_hub_status_t st;

    build_status_ack(&pkt, 0x07U);

    TEST_CHECK(RF_CMD_OK == rf_scp_decode_status(&pkt, &st),
               "decode_status returns OK");
    TEST_CHECK(0x11223344U == st.uptime_sec,
               "decode_status uptime LE");
    TEST_CHECK(0 == strcmp(st.fw_version, "v1.2.3"),
               "decode_status fw_version NUL-terminated");
    TEST_CHECK(1U == st.sched_active,
               "decode_status sched_active");
    TEST_CHECK(0x00000101U == st.sched_cycle_count,
               "decode_status sched_cycle_count LE");
}

static void test_decode_status_rejects(void)
{
    scp_packet_t    pkt;
    rf_hub_status_t st;

    build_status_ack(&pkt, 0x00U);
    pkt.data_len = 24U;   /* wrong length */

    TEST_CHECK(RF_CMD_ERR_LEN == rf_scp_decode_status(&pkt, &st),
               "decode_status rejects wrong length");
    TEST_CHECK(RF_CMD_ERR_NULL == rf_scp_decode_status(NULL, &st),
               "decode_status rejects NULL packet");
    TEST_CHECK(RF_CMD_ERR_NULL == rf_scp_decode_status(&pkt, NULL),
               "decode_status rejects NULL out");
}

/* ----------------------------------------------------------------------
 * rf_scp_engine
 * ---------------------------------------------------------------------- */

static void engine_setup(rf_engine_t *eng)
{
    cap_reset();
    rf_engine_init(eng, cap_send, cap_done, 500U, 3U, 3U);
}

static void test_engine_start_and_seq(void)
{
    rf_engine_t eng;
    engine_setup(&eng);

    TEST_CHECK(rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U),
               "start accepts first job");
    TEST_CHECK(rf_engine_is_busy(&eng),
               "engine busy after start");
    TEST_CHECK(1U == cap_sent_count,
               "one request emitted on start");
    TEST_CHECK((RF_SCP_ADDR_HUB == cap_sent[0].dst)
               && (RF_SCP_ADDR_RTU == cap_sent[0].src)
               && (SCP_TYPE_GET == cap_sent[0].type)
               && (RF_SCP_CMD_GET_STATUS == cap_sent[0].cmd)
               && (0U == cap_sent[0].data_len),
               "request header matches GET_STATUS");

    uint8_t first_seq = cap_sent[0].seq;

    /* Second job rejected while busy. */
    TEST_CHECK(!rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U),
               "busy rejects second job");
    TEST_CHECK(1U == cap_sent_count,
               "no extra request while busy");

    /* Complete first, then a second job must use seq+1. */
    scp_packet_t ack;
    build_status_ack(&ack, first_seq);
    rf_engine_on_response(&eng, &ack);

    TEST_CHECK(!rf_engine_is_busy(&eng),
               "engine idle after ACK");
    TEST_CHECK(rf_engine_start(&eng, RF_JOB_GET_STATUS, 2000U),
               "start accepts job after completion");
    TEST_CHECK(cap_sent[1].seq == (uint8_t)(first_seq + 1U),
               "SEQ increments per new request");
}

static void test_engine_ack_result(void)
{
    rf_engine_t eng;
    engine_setup(&eng);

    (void)rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U);

    scp_packet_t ack;
    build_status_ack(&ack, cap_sent[0].seq);
    rf_engine_on_response(&eng, &ack);

    TEST_CHECK((1U == cap_done_count)
               && (RF_RESULT_OK == cap_done_result)
               && (RF_JOB_GET_STATUS == cap_done_kind)
               && cap_done_had_rsp,
               "ACK yields OK result with response");
    TEST_CHECK(RF_LINK_UP == rf_engine_link_state(&eng),
               "link UP after ACK");
}

static void test_engine_mismatch_ignored(void)
{
    rf_engine_t eng;
    engine_setup(&eng);

    (void)rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U);

    scp_packet_t ack;
    build_status_ack(&ack, (uint8_t)(cap_sent[0].seq + 5U));  /* wrong SEQ */
    rf_engine_on_response(&eng, &ack);

    TEST_CHECK((0U == cap_done_count) && rf_engine_is_busy(&eng),
               "mismatched SEQ is ignored, job stays busy");
}

static void test_engine_error_result(void)
{
    rf_engine_t eng;
    engine_setup(&eng);

    (void)rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U);

    scp_packet_t err;
    (void)memset(&err, 0, sizeof(err));
    err.type     = SCP_TYPE_ERROR;
    err.cmd      = RF_SCP_CMD_GET_STATUS;
    err.seq      = cap_sent[0].seq;
    err.data_len = 1U;
    err.data[0]  = RF_SCP_ERR_NOT_AVAILABLE;
    rf_engine_on_response(&eng, &err);

    TEST_CHECK((1U == cap_done_count) && (RF_RESULT_ERR == cap_done_result),
               "ERROR yields ERR result");
    TEST_CHECK(RF_LINK_UP == rf_engine_link_state(&eng),
               "ERROR keeps link UP (peer alive)");
}

static void test_engine_timeout_retry(void)
{
    rf_engine_t eng;
    engine_setup(&eng);   /* timeout 500, max_retry 3 */

    (void)rf_engine_start(&eng, RF_JOB_GET_STATUS, 1000U);
    uint8_t seq = cap_sent[0].seq;

    /* Before deadline: no retry. */
    rf_engine_tick(&eng, 1400U);
    TEST_CHECK(1U == cap_sent_count, "no retry before timeout");

    /* Deadlines at +500 each; 3 retries then drop. */
    rf_engine_tick(&eng, 1500U);   /* retry 1 */
    rf_engine_tick(&eng, 2000U);   /* retry 2 */
    rf_engine_tick(&eng, 2500U);   /* retry 3 */

    TEST_CHECK(4U == cap_sent_count,
               "3 retransmissions after the first send");
    TEST_CHECK(cap_sent[3].seq == seq,
               "retransmit reuses the same SEQ");
    TEST_CHECK(0U == cap_done_count,
               "job not finished while retries remain");

    rf_engine_tick(&eng, 3000U);   /* no retries left -> timeout */
    TEST_CHECK((1U == cap_done_count)
               && (RF_RESULT_TIMEOUT == cap_done_result)
               && !cap_done_had_rsp,
               "timeout after retries exhausted, no response");
    TEST_CHECK(!rf_engine_is_busy(&eng),
               "engine idle after timeout");
}

static void run_timeout_cycle(rf_engine_t *eng, uint32_t base_ms)
{
    (void)rf_engine_start(eng, RF_JOB_GET_STATUS, base_ms);
    rf_engine_tick(eng, base_ms + 500U);    /* retry 1 */
    rf_engine_tick(eng, base_ms + 1000U);   /* retry 2 */
    rf_engine_tick(eng, base_ms + 1500U);   /* retry 3 */
    rf_engine_tick(eng, base_ms + 2000U);   /* drop */
}

static void test_engine_link_down_after_n(void)
{
    rf_engine_t eng;
    engine_setup(&eng);   /* link_fail_n = 3 */

    run_timeout_cycle(&eng, 1000U);
    TEST_CHECK(RF_LINK_DOWN != rf_engine_link_state(&eng),
               "link not DOWN after 1 failure");

    run_timeout_cycle(&eng, 10000U);
    TEST_CHECK(RF_LINK_DOWN != rf_engine_link_state(&eng),
               "link not DOWN after 2 failures");

    run_timeout_cycle(&eng, 20000U);
    TEST_CHECK(RF_LINK_DOWN == rf_engine_link_state(&eng),
               "link DOWN after 3 consecutive failures");
}

int main(void)
{
    printf("=== rf_scp host tests ===\r\n");

    test_decode_status_ok();
    test_decode_status_rejects();
    test_engine_start_and_seq();
    test_engine_ack_result();
    test_engine_mismatch_ignored();
    test_engine_error_result();
    test_engine_timeout_retry();
    test_engine_link_down_after_n();

    printf("\r\n=== %u passed, %u failed ===\r\n", test_pass, test_fail);
    return (0U == test_fail) ? 0 : 1;
}

/*** end of file ***/
