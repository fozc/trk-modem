/*
 * test_rf_r23_capture.c
 *
 *  Created on: Oct 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Replay delivered R2.3 messages through production codecs and fault tracking.
 */
#include "unity.h"
#include "rf_scp_codec.h"
#include "rf_faults.h"
#include "rf_types.h"
#include "mock_rf_inventory.h"
#include "mock_bsp.h"
#include "../fixtures/rf_scp_r23_vectors.h"
#include <string.h>

TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("rf_faults.c")
TEST_SOURCE_FILE("cp56time2a.c")

static rf_feeder_t feeder;

const rf_feeder_t *rf_store_get(feeder_id_t index);
const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    (void)index;
    return &feeder;
}

static bool get_binding(uint8_t source, rf_inventory_entry_t *out, int call_count)
{
    (void)call_count;
    if ((source < 5U) || (source > 7U))
    {
        return false;
    }
    *out = (rf_inventory_entry_t){.zone = 1U, .feeder = 1U,
        .phase = (uint8_t)(source & 3U), .line_index = 0U};
    out->eui64[0] = source;
    return true;
}

static scp_packet_t packet_from_vector(const rf_scp_r23_vector_t *vector)
{
    const uint8_t *frame = vector->logical;
    scp_packet_t packet = {.dst = frame[0], .src = frame[1], .type = frame[2],
        .cmd = frame[3], .seq = frame[4], .data_len = frame[5]};
    (void)memcpy(packet.data, &frame[7], packet.data_len);
    return packet;
}

void setUp(void)
{
    rf_faults_init();
    feeder = (rf_feeder_t){.in_use = true};
    rf_inventory_get_binding_StubWithCallback(get_binding);
}

void tearDown(void)
{
}

void test_all_r23_incoming_payloads_and_event_records_decode(void)
{
    size_t decoded = 0U;
    for (size_t index = 0U; index < sizeof(rf_scp_r23_vectors) / sizeof(rf_scp_r23_vectors[0]); index++)
    {
        const rf_scp_r23_vector_t *vector = &rf_scp_r23_vectors[index];
        const scp_packet_t packet = packet_from_vector(vector);
        if (RF_SCP_ADDR_RTU != packet.dst)
        {
            continue;
        }
        rf_scp_message_t message;
        TEST_ASSERT_EQUAL_INT_MESSAGE(RF_CMD_OK, rf_scp_decode_message(&packet, &message), vector->source);
        decoded++;
        if ((RF_SCP_CMD_LOG_READ_RANGE == packet.cmd) && (SCP_TYPE_ACK == packet.type))
        {
            for (size_t offset = 0U; offset < packet.data_len; offset += RF_SCP_EVENT_SIZE)
            {
                rf_scp_event_t event;
                TEST_ASSERT_EQUAL_INT_MESSAGE(RF_CMD_OK,
                    rf_scp_decode_event(&packet.data[offset], RF_SCP_EVENT_SIZE, &event), vector->source);
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_size_t(900U, decoded);
}

static size_t replay_faults(const char *source, uint32_t offset_ms)
{
    size_t records = 0U;
    uint64_t head_version = 0U;
    for (size_t index = 0U; index < sizeof(rf_scp_r23_vectors) / sizeof(rf_scp_r23_vectors[0]); index++)
    {
        const rf_scp_r23_vector_t *vector = &rf_scp_r23_vectors[index];
        if (strcmp(source, vector->source) != 0)
        {
            continue;
        }
        const scp_packet_t packet = packet_from_vector(vector);
        if (RF_SCP_ADDR_HUB == packet.dst)
        {
            if (RF_SCP_CMD_LOG_READ_HEAD == packet.cmd)
            {
                head_version = rf_faults_head_started();
            }
            continue;
        }
        rf_scp_message_t message;
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
        if ((RF_SCP_CMD_LOG_READ_RANGE == packet.cmd) && (SCP_TYPE_ACK == packet.type))
        {
            for (size_t offset = 0U; offset < packet.data_len; offset += RF_SCP_EVENT_SIZE)
            {
                rf_scp_event_t event;
                TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                    rf_scp_decode_event(&packet.data[offset], RF_SCP_EVENT_SIZE, &event));
                (void)rf_faults_classify(&event);
                rf_faults_record_stored(&event);
                records++;
            }
        }
        else if (RF_SCP_CMD_LIVE_DATA == packet.cmd)
        {
            rf_faults_live(&message.body.live, offset_ms + vector->elapsed_ms);
        }
        else if ((RF_SCP_CMD_LOG_READ_HEAD == packet.cmd) &&
                 (message.body.log_head.head == message.body.log_head.tail))
        {
            rf_faults_ring_empty(head_version, offset_ms + vector->elapsed_ms);
        }
        else
        {
            /* Notifications and consume ACKs do not prove a fresh empty HEAD. */
        }
    }
    return records;
}

static rf_fault_stats_t counts(void)
{
    rf_fault_stats_t result;
    TEST_ASSERT_TRUE(rf_faults_get_stats(1U, 1U, &result));
    return result;
}

void test_r23_two_phase_temporary_fault_is_one_feeder_fault_and_waits_for_third_phase_idle(void)
{
    TEST_ASSERT_EQUAL_size_t(17U, replay_faults("AY_R23_06_gecici_ariza_iki_faz_kapanis.csv", 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts().temporary);
    TEST_ASSERT_EQUAL_UINT32(0U, counts().permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts().uncertain);
    /* The delivered phase 3 LIVE has state 0, not the required idle state 1. */
    TEST_ASSERT_EQUAL_UINT32(1U, counts().pending);
    TEST_ASSERT_FALSE(counts().is_final);
}

void test_r23_separate_temporary_fault_keeps_a_separate_feeder_count(void)
{
    (void)replay_faults("AY_R23_06_gecici_ariza_iki_faz_kapanis.csv", 0U);
    TEST_ASSERT_EQUAL_size_t(8U, replay_faults("AY_R23_07_gecici_ariza_ikinci_kapanis.csv", 860000U));
    TEST_ASSERT_EQUAL_UINT32(2U, counts().temporary);
    TEST_ASSERT_EQUAL_UINT32(0U, counts().permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts().uncertain);
    TEST_ASSERT_FALSE(counts().is_final);
}

void test_r23_real_opening_records_do_not_become_a_temporary_fault(void)
{
    TEST_ASSERT_EQUAL_size_t(18U, replay_faults("AY_R23_01_acma_bildirimi_kayit_cekme.csv", 0U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts().permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts().temporary);
}

/*** end of file ***/
