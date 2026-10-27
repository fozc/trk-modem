/*
 * test_rf_scp_codec.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify R2 payload codecs with captured and invalid boundary inputs.
 */

#include "unity.h"
#include "rf_scp_codec.h"
#include "scp_endian.h"
#include "../fixtures/rf_scp_vectors.h"
#include <math.h>
#include <string.h>

TEST_SOURCE_FILE("rf_scp.c")

#define VECTOR_COUNT (sizeof(rf_scp_vectors) / sizeof(rf_scp_vectors[0]))

static scp_packet_t packet_from_vector(size_t index)
{
    const uint8_t *data = rf_scp_vectors[index].logical;
    scp_packet_t packet = {0};

    packet.dst = data[0];
    packet.src = data[1];
    packet.type = data[2];
    packet.cmd = data[3];
    packet.seq = data[4];
    packet.data_len = data[5];
    (void)memcpy(packet.data, &data[7], packet.data_len);
    return packet;
}

static scp_packet_t first_packet(uint8_t cmd, uint8_t type)
{
    for (size_t index = 0U; index < VECTOR_COUNT; index++)
    {
        scp_packet_t packet = packet_from_vector(index);

        if ((cmd == packet.cmd) && (type == packet.type))
        {
            return packet;
        }
    }
    TEST_FAIL_MESSAGE("Required command/type is missing from R1 captures");
    scp_packet_t empty = {0};
    return empty;
}

static void store_float(uint8_t *data, float value)
{
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    data[0] = (uint8_t)(bits & 0xFFU);
    data[1] = (uint8_t)((bits >> 8U) & 0xFFU);
    data[2] = (uint8_t)((bits >> 16U) & 0xFFU);
    data[3] = (uint8_t)((bits >> 24U) & 0xFFU);
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_all_incoming_captures_decode(void)
{
    for (size_t index = 0U; index < VECTOR_COUNT; index++)
    {
        scp_packet_t packet = packet_from_vector(index);
        rf_scp_message_t message;

        if (RF_SCP_ADDR_RTU == packet.dst)
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(RF_CMD_OK,
                rf_scp_decode_message(&packet, &message), rf_scp_vectors[index].source);
            TEST_ASSERT_EQUAL_HEX8(packet.cmd, message.cmd);
            TEST_ASSERT_EQUAL_HEX8(packet.type, message.type);
            if ((RF_SCP_CMD_LOG_READ_RECORD == packet.cmd) ||
                (RF_SCP_CMD_LOG_READ_RANGE == packet.cmd))
            {
                for (size_t offset = 0U; offset < packet.data_len;
                     offset += RF_SCP_EVENT_SIZE)
                {
                    rf_scp_event_t event;

                    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                        rf_scp_decode_event(&packet.data[offset],
                                               RF_SCP_EVENT_SIZE, &event));
                }
                TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data,
                    message.body.log_batch.records, packet.data_len);
            }
        }
    }
}

void test_outgoing_captures_preserve_valid_body_and_seq(void)
{
    for (size_t index = 0U; index < VECTOR_COUNT; index++)
    {
        scp_packet_t packet = packet_from_vector(index);
        scp_packet_t out;

        if (RF_SCP_ADDR_HUB == packet.dst)
        {
            if ((SCP_TYPE_ACK == packet.type) ||
                (RF_SCP_CMD_PWR_COMMAND == packet.cmd))
            {
                TEST_ASSERT_NOT_EQUAL(RF_CMD_OK,
                                      rf_scp_build_packet(&packet, &out));
            }
            else
            {
                TEST_ASSERT_EQUAL_INT_MESSAGE(RF_CMD_OK,
                    rf_scp_build_packet(&packet, &out),
                    rf_scp_vectors[index].source);
                TEST_ASSERT_EQUAL_HEX8(packet.seq, out.seq);
                TEST_ASSERT_EQUAL_HEX8(packet.cmd, out.cmd);
                TEST_ASSERT_EQUAL_HEX8(packet.type, out.type);
                TEST_ASSERT_EQUAL_UINT8(packet.data_len, out.data_len);
                if (0U < packet.data_len)
                {
                    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data, out.data,
                                                 packet.data_len);
                }
            }
        }
    }
}

void test_incoming_length_errors_preserve_previous_output(void)
{
    for (size_t index = 0U; index < VECTOR_COUNT; index++)
    {
        scp_packet_t packet = packet_from_vector(index);
        rf_scp_message_t out;
        rf_scp_message_t previous;

        if (RF_SCP_ADDR_RTU == packet.dst)
        {
            (void)memset(&out, 0xA5, sizeof(out));
            (void)memcpy(&previous, &out, sizeof(out));
            if (0U < packet.data_len)
            {
                packet.data_len--;
                TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                                      rf_scp_decode_message(&packet, &out));
                TEST_ASSERT_EQUAL_MEMORY(&previous, &out, sizeof(out));
                packet.data_len++;
            }
            packet.data_len++;
            if (((RF_SCP_CMD_LOG_READ_HEAD == packet.cmd) &&
                 (SCP_TYPE_ACK == packet.type)) ||
                (((RF_SCP_CMD_PWR_SUMMARY == packet.cmd) ||
                  (RF_SCP_CMD_PWR_ALARM == packet.cmd)) &&
                 (SCP_TYPE_SET == packet.type)))
            {
                TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                                      rf_scp_decode_message(&packet, &out));
                continue;
            }
            TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                                  rf_scp_decode_message(&packet, &out));
            TEST_ASSERT_EQUAL_MEMORY(&previous, &out, sizeof(out));
        }
    }
}

void test_address_type_version_and_null_rejection_preserve_output(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_SUMMARY, SCP_TYPE_SET);
    rf_scp_message_t out;
    rf_scp_message_t previous;
    static const uint8_t invalid_sources[] = {0U, 2U, 0xFFU};

    (void)memset(&out, 0x5A, sizeof(out));
    (void)memcpy(&previous, &out, sizeof(out));
    for (size_t index = 0U; index < sizeof(invalid_sources); index++)
    {
        packet.src = invalid_sources[index];
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_ADDRESS,
                              rf_scp_decode_message(&packet, &out));
    }
    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_ADDRESS,
                          rf_scp_decode_message(&packet, &out));
    packet.dst = RF_SCP_ADDR_RTU;
    packet.type = SCP_TYPE_ACK;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_TYPE, rf_scp_decode_message(&packet, &out));
    packet.type = SCP_TYPE_SET;
    packet.data[0] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_VERSION,
                          rf_scp_decode_message(&packet, &out));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL, rf_scp_decode_message(NULL, &out));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL, rf_scp_decode_message(&packet, NULL));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &out, sizeof(out));
}

void test_live_sample_keeps_float_values_signed_rssi_and_flags(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LIVE_DATA, SCP_TYPE_SET);
    rf_scp_message_t message;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(6U, message.body.live.source);
    TEST_ASSERT_EQUAL_UINT32(766990U, message.body.live.seq);
    TEST_ASSERT_EQUAL_UINT32(1336U, message.body.live.uptime_sec);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 31.641f,
                            message.body.live.trip_voltage);
    TEST_ASSERT_EQUAL_INT16(21, message.body.live.temperature);
    TEST_ASSERT_EQUAL_INT8(-53, message.body.live.rssi);
    TEST_ASSERT_EQUAL_UINT8(78U, message.body.live.log_pending);
    TEST_ASSERT_FALSE(message.body.live.fsm_error);
    TEST_ASSERT_FALSE(message.body.live.trip_failed);
    packet.data[0] = 0x11U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(4U, (message.body.live.source >> 2U) & 0x07U);
    packet.data[27] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_power_sample_preserves_units_sign_and_unreliable_flags(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_SUMMARY, SCP_TYPE_SET);
    rf_scp_message_t message;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_HEX8(0x43U, message.body.power.flags);
    TEST_ASSERT_EQUAL_UINT8(69U, message.body.power.session);
    TEST_ASSERT_EQUAL_UINT16(12962U, message.body.power.battery_mv);
    TEST_ASSERT_EQUAL_INT16(-121, message.body.power.battery_ma);
    TEST_ASSERT_EQUAL_INT16(-157, message.body.power.system_power_10mw);
    TEST_ASSERT_EQUAL_INT16(396, message.body.power.soc_tenths);
    TEST_ASSERT_EQUAL_UINT8(54U, message.body.power.capacity_ah);
    TEST_ASSERT_EQUAL_UINT32(0U, message.body.power.active_alarms);
    packet.data[2] = 0x04U;
    packet.data[3] = 0x80U;
    packet.data[4] = 255U;
    packet.data[29] = 128U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(255U, message.body.power.age_sec);
    TEST_ASSERT_EQUAL_INT8(-128, message.body.power.battery_temperature);
    TEST_ASSERT_EQUAL_HEX8(0x80U, message.body.power.flags2);
}

void test_event_keeps_unknown_id_invalid_clock_nan_and_32_bit_duration(void)
{
    /* Independent Python binascii.crc_hqx reference for these 58 bytes. */
    static const uint8_t raw[60] =
    {
        [7] = 255U, [17] = 0xC0U, [18] = 0x7FU,
        [23] = 0x78U, [24] = 0x56U, [25] = 0x34U, [26] = 0x12U,
        [58] = 0xF5U, [59] = 0x0AU
    };
    rf_scp_event_t event;
    rf_scp_event_t previous;
    uint8_t bad[60];

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_event(raw, sizeof(raw), &event));
    TEST_ASSERT_EQUAL_UINT8(255U, event.event);
    TEST_ASSERT_EQUAL_UINT32(0x12345678U, event.duration_ms);
    TEST_ASSERT_EQUAL_UINT8(0U, event.clock_quality);
    TEST_ASSERT_EQUAL_UINT8(0U, event.feeder);
    TEST_ASSERT_TRUE(isnan(event.current_amps));
    (void)memcpy(&previous, &event, sizeof(event));
    (void)memcpy(bad, raw, sizeof(bad));
    bad[7] ^= 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_CRC,
                          rf_scp_decode_event(bad, sizeof(bad), &event));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &event, sizeof(event));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_event(raw, 59U, &event));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
                          rf_scp_decode_event(NULL, 60U, &event));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
                          rf_scp_decode_event(raw, 60U, NULL));
}

void test_config_float_limits_nan_inf_and_dependent_threshold(void)
{
    static const size_t offsets[] = {3U, 7U, 11U, 15U, 19U, 32U, 47U, 53U};
    static const float invalid[] = {NAN, INFINITY, -INFINITY, -1.0f, 3000.0f};
    scp_packet_t packet = first_packet(RF_SCP_CMD_CFG_WRITE, SCP_TYPE_SET);
    uint8_t config[96];

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_validate_config(&packet.data[8], 96U));
    for (size_t field = 0U; field < sizeof(offsets) / sizeof(offsets[0]);
         field++)
    {
        for (size_t value = 0U; value < sizeof(invalid) / sizeof(invalid[0]);
             value++)
        {
            (void)memcpy(config, &packet.data[8], sizeof(config));
            store_float(&config[offsets[field]], invalid[value]);
            TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                                  rf_scp_validate_config(config, 96U));
        }
    }
    (void)memcpy(config, &packet.data[8], sizeof(config));
    store_float(&config[3], 10.0f);
    store_float(&config[7], 11.0f);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_validate_config(config, 96U));
    store_float(&config[7], 12.0f);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_validate_config(config, 96U));
    config[0] = 255U;
    config[2] = 255U;
    config[67] = 255U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_validate_config(config, 96U));
    config[1] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_validate_config(config, 96U));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_validate_config(config, 95U));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
                          rf_scp_validate_config(NULL, 96U));
}

void test_requests_reject_invalid_ranges_without_changing_output(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_INVENTORY_SET, SCP_TYPE_SET);
    scp_packet_t out;
    scp_packet_t previous;

    (void)memset(&out, 0xA5, sizeof(out));
    (void)memcpy(&previous, &out, sizeof(out));
    packet.data[1] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &out, sizeof(out));
    packet.cmd = RF_SCP_CMD_INVENTORY_UPDATE;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    packet.cmd = RF_SCP_CMD_EPOCH_REFRESH;
    packet.data_len = 1U;
    packet.data[0] = 5U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.cmd = RF_SCP_CMD_LOG_READ_RANGE;
    packet.type = SCP_TYPE_GET;
    packet.data_len = 4U;
    scp_pack_u16(packet.data, 99U);
    scp_pack_u16(&packet.data[2], 65535U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    scp_pack_u16(&packet.data[2], 0U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.type = SCP_TYPE_SET;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_TYPE,
                          rf_scp_build_packet(&packet, &out));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
                          rf_scp_build_packet(NULL, &out));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
                          rf_scp_build_packet(&packet, NULL));
}

void test_power_request_customer_mask_unset_values_and_command_gate(void)
{
    scp_packet_t packet = {0};
    scp_packet_t out;

    packet.cmd = RF_SCP_CMD_PWR_CFG2;
    packet.type = SCP_TYPE_SET;
    packet.data_len = 16U;
    scp_pack_u16(&packet.data[1], 0x6200U);
    packet.data[10] = 22U;
    packet.data[14] = 54U;
    packet.data[15] = 0x8AU;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    packet.data[15] = 0x4AU;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[10] = 255U;
    packet.data[14] = 0U;
    packet.data[15] = 255U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    scp_pack_u16(&packet.data[1], 0x0200U); /* Only rate may be unset. */
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    scp_pack_u16(&packet.data[1], 0x0004U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.cmd = RF_SCP_CMD_PWR_COMMAND;
    packet.data_len = 2U;
    packet.data[0] = 5U;
    packet.data[1] = 0xA5U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    packet.data[1] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[0] = 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[0] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
}

void test_fram_degraded_and_extended_errors_preserve_meaning(void)
{
    scp_packet_t packet = {0};
    rf_scp_message_t message;

    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.type = SCP_TYPE_ACK;
    packet.cmd = RF_SCP_CMD_GET_FRAM_STATS;
    packet.data_len = 8U;
    (void)memset(packet.data, 0xFF, 6U);
    packet.data[6] = 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_TRUE(message.body.fram.degraded);
    TEST_ASSERT_EQUAL_UINT16(0U, message.body.fram.head);
    packet.data[6] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.type = SCP_TYPE_ERROR;
    packet.cmd = RF_SCP_CMD_PWR_CFG2;
    packet.data_len = 3U;
    packet.data[0] = RF_SCP_ERR_INVALID_PARAM;
    packet.data[1] = 0U;
    packet.data[2] = 0x20U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(2U, message.body.error.extra_len);
    TEST_ASSERT_EQUAL_UINT16(0x2000U,
                            scp_unpack_u16(message.body.error.extra));
    packet.cmd = RF_SCP_CMD_CFG_READ_ALL;
    packet.data_len = 1U;
    packet.data[0] = RF_SCP_ERR_NOT_AVAILABLE;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(RF_SCP_ERR_NOT_AVAILABLE, message.body.error.code);
}

void test_raw_telemetry_ack_and_notification_preserve_big_endian_bytes(void)
{
    scp_packet_t packet = {0};
    rf_scp_message_t message;

    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.type = SCP_TYPE_ACK;
    packet.cmd = RF_SCP_CMD_PWR_TELEMETRY;
    packet.data_len = 96U;
    for (size_t index = 0U; index < 96U; index++)
    {
        packet.data[index] = (uint8_t)index;
    }
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data, message.body.telemetry.bytes,
                                 96U);
    packet.type = SCP_TYPE_SET;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet.data, message.body.telemetry.bytes,
                                 96U);
}

void test_config_integer_boundaries_for_every_writable_field(void)
{
    static const uint16_t limits[][4] =
    {
        {24U, 20U, 140U, 2U}, {26U, 10U, 300U, 2U},
        {28U, 30U, 600U, 2U}, {30U, 20U, 80U, 2U},
        {36U, 80U, 200U, 2U}, {38U, 0U, 100U, 2U},
        {40U, 20U, 120U, 2U}, {51U, 100U, 12000U, 2U},
        {42U, 1U, 4U, 1U}, {43U, 0U, 1U, 1U},
        {44U, 0U, 1U, 1U}, {45U, 10U, 40U, 1U},
        {46U, 0U, 1U, 1U}
    };
    scp_packet_t packet = first_packet(RF_SCP_CMD_CFG_WRITE, SCP_TYPE_SET);
    uint8_t config[96];

    for (size_t field = 0U; field < sizeof(limits) / sizeof(limits[0]);
         field++)
    {
        for (uint8_t boundary = 0U; boundary < 3U; boundary++)
        {
            uint16_t value = limits[field][1];

            if (1U == boundary)
            {
                value = limits[field][2];
            }
            else if (2U == boundary)
            {
                value = (uint16_t)(limits[field][2] + 1U);
            }
            else
            {
                /* Lower bound. */
            }
            (void)memcpy(config, &packet.data[8], sizeof(config));
            if (2U == limits[field][3])
            {
                scp_pack_u16(&config[limits[field][0]], value);
            }
            else
            {
                config[limits[field][0]] = (uint8_t)value;
            }
            TEST_ASSERT_EQUAL_INT((2U == boundary) ? RF_CMD_ERR_PARAM
                                                    : RF_CMD_OK,
                                  rf_scp_validate_config(config, 96U));
        }
    }
    (void)memcpy(config, &packet.data[8], sizeof(config));
    config[23] = 60U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_validate_config(config, 96U));
    config[23] = 51U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_validate_config(config, 96U));
}

void test_config_float_endpoints_including_nominal_200_threshold_240(void)
{
    static const size_t offsets[] = {3U, 7U, 11U, 15U, 19U, 32U, 47U, 53U};
    static const float limits[][2] =
    {
        {2.0f, 200.0f}, {5.0f, 240.0f}, {0.1f, 0.3f},
        {1.0f, 2200.0f}, {0.3f, 5.0f}, {1.0f, 15.0f},
        {1.0f, 3.0f}, {24.0f, 45.0f}
    };
    scp_packet_t packet = first_packet(RF_SCP_CMD_CFG_WRITE, SCP_TYPE_SET);
    uint8_t config[96];

    for (size_t field = 0U; field < sizeof(offsets) / sizeof(offsets[0]);
         field++)
    {
        for (size_t bound = 0U; bound < 2U; bound++)
        {
            (void)memcpy(config, &packet.data[8], sizeof(config));
            store_float(&config[3], 2.0f);
            store_float(&config[7], 240.0f);
            store_float(&config[offsets[field]], limits[field][bound]);
            TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                                  rf_scp_validate_config(config, 96U));
        }
    }
}

void test_all_request_commands_have_exact_type_and_body_length(void)
{
    static const uint8_t requests[][3] =
    {
        {0x00U, SCP_TYPE_PING, 0U}, {0x01U, SCP_TYPE_GET, 0U},
        {0x02U, SCP_TYPE_GET, 0U}, {0x03U, SCP_TYPE_SET, 0U},
        {0x04U, SCP_TYPE_SET, 12U}, {0x05U, SCP_TYPE_SET, 0U},
        {0x06U, SCP_TYPE_SET, 12U}, {0x07U, SCP_TYPE_SET, 7U},
        {0x20U, SCP_TYPE_GET, 8U}, {0x22U, SCP_TYPE_SET, 104U},
        {0x24U, SCP_TYPE_SET, 1U}, {0x26U, SCP_TYPE_SET, 1U},
        {0x28U, SCP_TYPE_GET, 1U}, {0x2AU, SCP_TYPE_SET, 1U},
        {0x40U, SCP_TYPE_GET, 0U}, {0x42U, SCP_TYPE_GET, 2U},
        {0x44U, SCP_TYPE_GET, 4U}, {0x46U, SCP_TYPE_SET, 2U},
        {0xE5U, SCP_TYPE_GET, 0U}, {0xE5U, SCP_TYPE_SET, 16U},
        {0xE6U, SCP_TYPE_SET, 2U}, {0xE7U, SCP_TYPE_GET, 0U},
        {0xE8U, SCP_TYPE_GET, 0U}
    };
    scp_packet_t config = first_packet(RF_SCP_CMD_CFG_WRITE, SCP_TYPE_SET);

    for (size_t index = 0U; index < sizeof(requests) / sizeof(requests[0]);
         index++)
    {
        scp_packet_t packet = {0};
        scp_packet_t out;

        packet.cmd = requests[index][0];
        packet.type = requests[index][1];
        packet.data_len = requests[index][2];
        packet.seq = 255U;
        packet.data[0] = 1U;
        packet.data[1] = 1U;
        packet.data[2] = 1U;
        if (RF_SCP_CMD_CFG_WRITE == packet.cmd)
        {
            (void)memcpy(packet.data, config.data, packet.data_len);
        }
        else if (RF_SCP_CMD_TIME_SYNC == packet.cmd)
        {
            packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
        }
        else if (RF_SCP_CMD_PWR_CFG2 == packet.cmd)
        {
            (void)memset(packet.data, 0, sizeof(packet.data));
        }
        else if ((RF_SCP_CMD_LOG_READ_RECORD == packet.cmd) ||
                 (RF_SCP_CMD_LOG_CONSUME_TO == packet.cmd) ||
                 (RF_SCP_CMD_LOG_READ_RANGE == packet.cmd))
        {
            scp_pack_u16(packet.data, 99U);
            scp_pack_u16(&packet.data[2], 4U);
        }
        else if (RF_SCP_CMD_PWR_COMMAND == packet.cmd)
        {
            packet.data[0] = 0U;
        }
        else
        {
            /* Valid default body for remaining requests. */
        }
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                              rf_scp_build_packet(&packet, &out));
        TEST_ASSERT_EQUAL_UINT8(RF_SCP_ADDR_HUB, out.dst);
        TEST_ASSERT_EQUAL_UINT8(RF_SCP_ADDR_RTU, out.src);
        TEST_ASSERT_EQUAL_UINT8(packet.seq, out.seq);
        packet.data_len++;
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                              rf_scp_build_packet(&packet, &out));
        packet.type = SCP_TYPE_ERROR;
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_TYPE,
                              rf_scp_build_packet(&packet, &out));
    }
}

void test_time_invalid_and_manufacturer_packets_are_rejected(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    scp_packet_t out;
    rf_scp_message_t message;

    packet.data[2] |= 0x80U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[2] &= 0x7FU;
    packet.data[5] = 13U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.src = RF_SCP_ADDR_HUB;
    packet.dst = RF_SCP_ADDR_RTU;
    packet.cmd = 0xF0U;
    packet.type = SCP_TYPE_ERROR;
    packet.data_len = 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_UNSUPPORTED,
                          rf_scp_decode_message(&packet, &message));
}

void test_cfg2_keeps_internal_big_endian_bytes_and_echo_ret_bits(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_CFG2, SCP_TYPE_ACK);
    rf_scp_message_t message;

    packet.data[12] = 0x12U;
    packet.data[13] = 0x34U;
    packet.data[19] = 0x20U;
    packet.data[20] = 0x80U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(0x12U, message.body.power_config.block[11]);
    TEST_ASSERT_EQUAL_UINT8(0x34U, message.body.power_config.block[12]);
    TEST_ASSERT_EQUAL_UINT8(0x20U, message.body.power_config.mask1);
    TEST_ASSERT_EQUAL_UINT8(0x80U, message.body.power_config.mask2);
    packet.data[0] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_VERSION,
                          rf_scp_decode_message(&packet, &message));
}

void test_four_record_batch_preserves_slots_and_detects_bad_record_crc(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_message_t message;
    rf_scp_event_t event;

    for (size_t offset = 60U; offset < 240U; offset += 60U)
    {
        (void)memcpy(&packet.data[offset], packet.data, 60U);
    }
    packet.data_len = 240U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(4U, message.body.log_batch.count);
    for (size_t offset = 0U; offset < 240U; offset += 60U)
    {
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
            rf_scp_decode_event(&message.body.log_batch.records[offset],
                                   60U, &event));
    }
    packet.data[120] ^= 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_CRC,
        rf_scp_decode_event(&message.body.log_batch.records[120],
                               60U, &event));
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(message.body.log_batch.records, 60U, &event));
    packet.cmd = RF_SCP_CMD_LOG_READ_RECORD;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
}

void test_captured_event_record_keeps_all_documented_fields(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_event_t event;
    static const uint8_t timestamp[] =
    {
        0xABU, 0x5EU, 0x32U, 0x14U, 0xE4U, 0x0AU, 0x1AU
    };

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, packet.data_len, &event));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(timestamp, event.timestamp, 7U);
    TEST_ASSERT_EQUAL_UINT8(1U, event.event);
    TEST_ASSERT_EQUAL_UINT8(1U, event.zone);
    TEST_ASSERT_EQUAL_UINT8(1U, event.feeder);
    TEST_ASSERT_EQUAL_UINT8(1U, event.phase);
    TEST_ASSERT_EQUAL_UINT8(3U, event.fault_count);
    TEST_ASSERT_EQUAL_UINT8(0U, event.soh_ratio);
    TEST_ASSERT_EQUAL_UINT8(0U, event.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT8(0U, event.energy_status);
    TEST_ASSERT_FLOAT_WITHIN(0.00001F, 15.1033916F, event.current_amps);
    TEST_ASSERT_FLOAT_WITHIN(0.0001F, 276.901337F, event.di_dt);
    TEST_ASSERT_EQUAL_UINT32(280U, event.duration_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.00001F, 31.7817631F, event.trip_voltage);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, event.harvest_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.000001F, 1.62020326F,
                             event.reference_voltage);
    TEST_ASSERT_EQUAL_INT16(21, event.temperature);
    TEST_ASSERT_EQUAL_UINT16(0U, event.permanent_faults);
    TEST_ASSERT_EQUAL_UINT16(0U, event.temporary_faults);
    TEST_ASSERT_FLOAT_WITHIN(0.0000001F, 0.0266894698F, event.soh_idc);
    TEST_ASSERT_EQUAL_UINT16(3060U, event.boot_counter);
    TEST_ASSERT_EQUAL_UINT32(6359U, event.uptime_sec);
    TEST_ASSERT_EQUAL_UINT8(1U, event.clock_quality);
    TEST_ASSERT_EQUAL_HEX16(0x8159U, event.crc);
}

void test_short_event_batch_is_valid_but_partial_record_is_rejected(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_message_t message;
    rf_scp_message_t previous;

    (void)memcpy(&packet.data[60], packet.data, 60U);
    packet.data_len = 120U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(2U, message.body.log_batch.count);
    previous = message;
    packet.data_len = 119U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &message, sizeof(message));
    packet.data_len = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &message, sizeof(message));
}

void test_mh_pending_and_consume_left_accept_99_and_reject_100(void)
{
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .cmd = RF_SCP_CMD_LOG_AVAILABLE,
        .type = SCP_TYPE_SET, .data_len = 4U};
    rf_scp_message_t message;
    scp_pack_u16(packet.data, 99U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    rf_scp_message_t previous = message;
    scp_pack_u16(packet.data, 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &message, sizeof(message));
    packet.cmd = RF_SCP_CMD_LOG_CONSUME_TO;
    packet.type = SCP_TYPE_ACK;
    scp_pack_u16(packet.data, 0U);
    scp_pack_u16(&packet.data[2], 99U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    previous = message;
    scp_pack_u16(&packet.data[2], 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &message, sizeof(message));
}

void test_customer_power_config_rejects_unset_capacity_and_bad_period(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_PWR_CFG2,
        .type = SCP_TYPE_SET, .data_len = 16U};
    scp_packet_t output;
    scp_pack_u16(&packet.data[1], 0x2000U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &output));
    packet.data[14] = 255U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &output));
    packet.data[14] = 24U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &output));
    scp_pack_u16(&packet.data[1], 0x4000U);
    packet.data[15] = 255U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &output));
    packet.data[15] = 0x8AU;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &output));
    packet.data[15] = 0xCAU;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &output));
}

void test_r2_summary_accepts_tail_extension_and_rejects_short_body(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_SUMMARY, SCP_TYPE_SET);
    packet.data_len = 45U;
    rf_scp_message_t original;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_message(&packet, &original));
    for (uint8_t length = 45U; 47U >= length; length++)
    {
        packet.data_len = length;
        rf_scp_message_t decoded;
        TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
            rf_scp_decode_message(&packet, &decoded));
        TEST_ASSERT_EQUAL_MEMORY(&original.body.power, &decoded.body.power,
                                 sizeof(original.body.power));
    }
    packet.data_len = 38U;
    rf_scp_message_t output = original;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
        rf_scp_decode_message(&packet, &output));
    TEST_ASSERT_EQUAL_MEMORY(&original, &output, sizeof(output));
}

void test_r2_source_hash_matches_delivered_eui_and_event_vector(void)
{
    const uint8_t eui[8] = {0x00U, 0x12U, 0x4BU, 0x00U,
                            0x38U, 0xC9U, 0xF1U, 0xC3U};
    TEST_ASSERT_EQUAL_HEX16(0x1787U, rf_scp_eui_hash(eui));
    TEST_ASSERT_EQUAL_HEX16(0U, rf_scp_eui_hash(NULL));
    const uint8_t raw[60] =
    {
        0xE8U, 0x80U, 0x0AU, 0x10U, 0xA9U, 0x0AU, 0x1AU, 0x8FU,
        1U, 1U, 2U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0x10U, 0x4FU, 0x0CU, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 4U, 0U, 0U, 0U, 0U, 0U, 0xCEU, 0U,
        0U, 0U, 0U, 0U, 0U, 0x87U, 0x17U, 0x5EU, 0x89U
    };
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_event(raw, 60U, &event));
    TEST_ASSERT_EQUAL_HEX16(0x1787U, event.src_eui_hash);
    TEST_ASSERT_EQUAL_UINT16(206U, event.boot_counter);
}

void test_r2_live_boot_counter_uses_payload_offset_31(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LIVE_DATA, SCP_TYPE_SET);
    scp_pack_u16(&packet.data[31], 206U);
    rf_scp_message_t message;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT16(206U, message.body.live.boot_counter);
}

void test_r2_conditional_consume_preserves_full_ordinal_and_validates_ack(void)
{
    scp_packet_t request = {.cmd = RF_SCP_CMD_LOG_CONSUME_IF,
        .type = SCP_TYPE_SET, .data_len = 4U, .seq = 13U,
        .data = {0xFEU, 0xDCU, 0xBAU, 0x98U}};
    scp_packet_t output;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_build_packet(&request, &output));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(request.data, output.data, 4U);
    scp_packet_t ack = {.cmd = RF_SCP_CMD_LOG_CONSUME_IF,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U,
        .data = {99U, 0U, 2U, 0U, 0xFEU, 0xDCU, 0xBAU, 0x98U}};
    rf_scp_message_t message;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&ack, &message));
    TEST_ASSERT_EQUAL_HEX32(0x98BADCFEU, message.body.log_consume.tail_seq);
    const rf_scp_message_t previous = message;
    ack.data_len = 7U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
        rf_scp_decode_message(&ack, &message));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &message, sizeof(message));
}

void test_r2_every_event_bit_is_protected_by_record_crc(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_event_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (size_t byte = 0U; RF_SCP_EVENT_SIZE > byte; byte++)
    {
        for (uint8_t bit = 0U; 8U > bit; bit++)
        {
            packet.data[byte] ^= (uint8_t)(1U << bit);
            rf_scp_event_t output = previous;
            TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_CRC,
                rf_scp_decode_event(packet.data, RF_SCP_EVENT_SIZE, &output));
            TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
            packet.data[byte] ^= (uint8_t)(1U << bit);
        }
    }
}

void test_r2_event_all_short_lengths_preserve_output(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_event_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (size_t length = 0U; RF_SCP_EVENT_SIZE > length; length++)
    {
        rf_scp_event_t output = previous;
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
            rf_scp_decode_event(packet.data, length, &output));
        TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
    }
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
        rf_scp_decode_event(NULL, RF_SCP_EVENT_SIZE, &previous));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_NULL,
        rf_scp_decode_event(packet.data, RF_SCP_EVENT_SIZE, NULL));
}

void test_r2_summary_every_supported_tail_length_keeps_known_fields(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_SUMMARY, SCP_TYPE_SET);
    packet.data_len = 39U;
    rf_scp_message_t previous;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_message(&packet, &previous));
    (void)memset(&packet.data[39], 0xA5, sizeof(packet.data) - 39U);
    for (size_t length = 0U; sizeof(packet.data) >= length; length++)
    {
        packet.data_len = (uint8_t)length;
        rf_scp_message_t output = previous;
        const rf_cmd_status_t expected = (39U > length) ?
            RF_CMD_ERR_LEN : RF_CMD_OK;
        TEST_ASSERT_EQUAL_INT(expected,
            rf_scp_decode_message(&packet, &output));
        rf_scp_message_t expected_output = previous;
        if (length >= 45U)
        {
            expected_output.body.power.equivalent_cycles = 0xA5A5U;
            expected_output.body.power.remaining_cycles = 0xA5A5U;
            expected_output.body.power.remaining_years_x10 = 0xA5A5U;
        }
        TEST_ASSERT_EQUAL_MEMORY(&expected_output, &output, sizeof(output));
    }
}

void test_r23_summary_lifetime_fields_are_little_endian_and_optional(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_SUMMARY, SCP_TYPE_SET);
    rf_scp_message_t output;
    packet.data_len = 45U;
    scp_pack_u16(&packet.data[39], 0x1234U);
    scp_pack_u16(&packet.data[41], 0U);
    scp_pack_u16(&packet.data[43], 120U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &output));
    TEST_ASSERT_EQUAL_HEX16(0x1234U, output.body.power.equivalent_cycles);
    TEST_ASSERT_EQUAL_UINT16(0U, output.body.power.remaining_cycles);
    TEST_ASSERT_EQUAL_UINT16(120U, output.body.power.remaining_years_x10);
    packet.data_len = 39U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &output));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, output.body.power.equivalent_cycles);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, output.body.power.remaining_cycles);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, output.body.power.remaining_years_x10);
    packet.data_len = 45U;
    (void)memset(&packet.data[39], 0xFF, 4U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &output));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, output.body.power.equivalent_cycles);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, output.body.power.remaining_cycles);
    TEST_ASSERT_EQUAL_UINT16(120U, output.body.power.remaining_years_x10);
}

void test_r2_consume_ack_all_tail_and_pending_values_are_range_checked(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_LOG_CONSUME_IF,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    rf_scp_message_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (size_t offset = 0U; 4U > offset; offset += 2U)
    {
        for (uint16_t value = 0U; 256U > value; value++)
        {
            (void)memset(packet.data, 0, sizeof(packet.data));
            scp_pack_u16(&packet.data[offset], value);
            rf_scp_message_t output = previous;
            const rf_cmd_status_t expected = (100U > value) ?
                RF_CMD_OK : RF_CMD_ERR_PARAM;
            TEST_ASSERT_EQUAL_INT(expected,
                rf_scp_decode_message(&packet, &output));
            if (RF_CMD_OK != expected)
            {
                TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
            }
        }
    }
}

void test_r2_all_live_boolean_and_state_values_are_validated(void)
{
    const size_t offsets[] = {9U, 27U, 28U, 29U};
    const uint8_t maxima[] = {6U, 1U, 1U, 99U};
    const scp_packet_t original =
        first_packet(RF_SCP_CMD_LIVE_DATA, SCP_TYPE_SET);
    rf_scp_message_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (size_t field = 0U; 4U > field; field++)
    {
        for (uint16_t value = 0U; 256U > value; value++)
        {
            scp_packet_t packet = original;
            packet.data[offsets[field]] = (uint8_t)value;
            rf_scp_message_t output = previous;
            const rf_cmd_status_t expected = (maxima[field] >= value) ?
                RF_CMD_OK : RF_CMD_ERR_PARAM;
            TEST_ASSERT_EQUAL_INT(expected,
                rf_scp_decode_message(&packet, &output));
            if (RF_CMD_OK != expected)
            {
                TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
            }
        }
    }
}

void test_r2_every_short_capture_body_preserves_decoded_output(void)
{
    for (size_t index = 0U; VECTOR_COUNT > index; index++)
    {
        scp_packet_t packet = packet_from_vector(index);
        if (RF_SCP_ADDR_RTU != packet.dst)
        {
            continue;
        }
        const uint8_t original_length = packet.data_len;
        rf_scp_message_t previous;
        (void)memset(&previous, 0xA5, sizeof(previous));
        for (uint8_t length = 0U; original_length > length; length++)
        {
            packet.data_len = length;
            rf_scp_message_t output = previous;
            const rf_cmd_status_t result =
                rf_scp_decode_message(&packet, &output);
            if (RF_CMD_OK != result)
            {
                TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
            }
            else
            {
                TEST_ASSERT_TRUE((RF_SCP_CMD_LOG_READ_RANGE == packet.cmd) ||
                    (RF_SCP_CMD_PWR_CFG2 == packet.cmd) ||
                    (RF_SCP_CMD_PWR_SUMMARY == packet.cmd) ||
                    (RF_SCP_CMD_LOG_READ_HEAD == packet.cmd));
            }
        }
    }
}

void test_r2_head_cannot_have_more_pending_records_than_lifetime_total(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_HEAD, SCP_TYPE_ACK);
    scp_pack_u16(packet.data, 1U);
    scp_pack_u16(&packet.data[2], 0U);
    (void)memset(&packet.data[4], 0, 4U);
    scp_pack_u16(&packet.data[8], 0U);
    rf_scp_message_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    rf_scp_message_t output = previous;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
        rf_scp_decode_message(&packet, &output));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
}

void test_r2_fram_free_slots_uses_the_99_record_capacity(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_GET_FRAM_STATS,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    rf_scp_message_t previous;
    (void)memset(&previous, 0xA5, sizeof(previous));
    for (uint16_t value = 0U; 256U > value; value++)
    {
        scp_pack_u16(&packet.data[4], value);
        rf_scp_message_t output = previous;
        TEST_ASSERT_EQUAL_INT((100U > value) ? RF_CMD_OK : RF_CMD_ERR_PARAM,
            rf_scp_decode_message(&packet, &output));
        if (100U <= value)
        {
            TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
        }
    }
}

void test_zero_crc_eui_hash_uses_ffff_instead_of_unknown_identity(void)
{
    const uint8_t eui[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0x0EU, 0x10U};
    TEST_ASSERT_EQUAL_HEX16(UINT16_MAX, rf_scp_eui_hash(eui));
    TEST_ASSERT_EQUAL_HEX16(0U, rf_scp_eui_hash(NULL));
}

/* Independent CRC-16/CCITT-FALSE used to re-seal mutated event records. */
static uint16_t event_record_crc(const uint8_t *data)
{
    uint16_t crc = 0xFFFFU;

    for (size_t index = 0U; 58U > index; index++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint32_t)data[index] << 8U));
        for (uint8_t bit = 0U; 8U > bit; bit++)
        {
            crc = (uint16_t)((0U != (crc & 0x8000U)) ?
                (((uint32_t)crc << 1U) ^ 0x1021U) : ((uint32_t)crc << 1U));
        }
    }
    return crc;
}

void test_event_clock_quality_above_two_is_rejected(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_LOG_READ_RANGE,
                                       SCP_TYPE_ACK);
    rf_scp_event_t event;

    packet.data[55] = 3U;
    scp_pack_u16(&packet.data[58], event_record_crc(packet.data));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
        rf_scp_decode_event(packet.data, RF_SCP_EVENT_SIZE, &event));
    packet.data[55] = 255U;
    scp_pack_u16(&packet.data[58], event_record_crc(packet.data));
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
        rf_scp_decode_event(packet.data, RF_SCP_EVENT_SIZE, &event));
    packet.data[55] = 2U;
    scp_pack_u16(&packet.data[58], event_record_crc(packet.data));
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
        rf_scp_decode_event(packet.data, RF_SCP_EVENT_SIZE, &event));
    TEST_ASSERT_EQUAL_UINT8(2U, event.clock_quality);
}

void test_power_config_rate_mask_keeps_zero_and_255_special_values(void)
{
    scp_packet_t packet = {0};
    scp_packet_t out;
    static const uint8_t rates[] = {19U, 20U, 200U, 201U, 0U, 255U};
    static const rf_cmd_status_t expected[] =
    {
        RF_CMD_ERR_PARAM, RF_CMD_OK, RF_CMD_OK, RF_CMD_ERR_PARAM,
        RF_CMD_OK, RF_CMD_OK
    };

    packet.type = SCP_TYPE_SET;
    packet.cmd = RF_SCP_CMD_PWR_CFG2;
    packet.data_len = 16U;
    packet.data[0] = 1U;
    scp_pack_u16(&packet.data[1], 0x0200U);
    for (size_t index = 0U; index < sizeof(rates); index++)
    {
        packet.data[10] = rates[index];
        TEST_ASSERT_EQUAL_INT(expected[index],
                              rf_scp_build_packet(&packet, &out));
    }
}

void test_incoming_ping_echo_decodes_and_unknown_command_is_rejected(void)
{
    scp_packet_t packet = {.cmd = 0x00U, .type = SCP_TYPE_ACK,
        .src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU, .data_len = 0U};
    rf_scp_message_t message;

    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_HEX8(0x00U, message.cmd);
    packet.type = SCP_TYPE_PING;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_TYPE,
                          rf_scp_decode_message(&packet, &message));
    packet.type = SCP_TYPE_ACK;
    packet.cmd = 0x29U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_UNSUPPORTED,
                          rf_scp_decode_message(&packet, &message));
}

void test_trip_notify_invalid_source_or_zone_is_rejected(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_TRIP_NOTIFY,
        .type = SCP_TYPE_SET, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 12U};
    rf_scp_message_t message;

    packet.data[2] = 1U;
    packet.data[3] = 0x09U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(0x09U, message.body.trip.source);
    packet.data[3] = 0x08U;   /* phase 0 */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.data[3] = 0x09U;
    packet.data[2] = 8U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_anomaly_report_invalid_source_state_or_path_is_rejected(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_ANOMALY_REPORT,
        .type = SCP_TYPE_SET, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 10U};
    rf_scp_message_t message;

    packet.data[0] = 0xFFU;
    packet.data[1] = 2U;
    packet.data[8] = 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(0xFFU, message.body.anomaly.source);
    packet.data[0] = 0x08U;   /* not 0xFF and phase 0 */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.data[0] = 0xFFU;
    packet.data[1] = 3U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.data[1] = 2U;
    packet.data[8] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_power_alarm_version_and_state_bounds_are_rejected(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_PWR_ALARM,
        .type = SCP_TYPE_SET, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 11U};
    rf_scp_message_t message;

    packet.data[0] = 1U;
    packet.data[4] = 3U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(3U, message.body.alarm.state);
    packet.data[4] = 4U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.data[4] = 3U;
    packet.data[0] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_VERSION,
                          rf_scp_decode_message(&packet, &message));
}

void test_power_alarm_ignores_appended_fields_and_preserves_output_on_short_frames(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_PWR_ALARM, SCP_TYPE_SET);
    rf_scp_message_t previous;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &previous));
    (void)memset(&packet.data[11], 0xA5, sizeof(packet.data) - 11U);
    for (size_t length = 0U; length <= sizeof(packet.data); length++)
    {
        packet.data_len = (uint8_t)length;
        rf_scp_message_t output = previous;
        TEST_ASSERT_EQUAL_INT((length < 11U) ? RF_CMD_ERR_LEN : RF_CMD_OK,
                              rf_scp_decode_message(&packet, &output));
        TEST_ASSERT_EQUAL_MEMORY(&previous, &output, sizeof(output));
    }
    packet.data[0] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_VERSION, rf_scp_decode_message(&packet, &previous));
}

void test_cfg2_response_rejects_invalid_echo_flag(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_PWR_CFG2,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 23U};
    rf_scp_message_t message;

    packet.data[0] = 1U;
    packet.data[17] = 1U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_TRUE(message.body.power_config.echo_valid);
    packet.data[17] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_fram_stats_degraded_byte_must_be_boolean(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_GET_FRAM_STATS,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    rf_scp_message_t message;

    packet.data[6] = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_config_status_state_and_bitmap_bounds_are_rejected(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_CFG_STATUS_NOTIFY,
        .type = SCP_TYPE_SET, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 8U};
    rf_scp_message_t message;

    packet.data[0] = 7U;
    packet.data[1] = 4U;
    packet.data[2] = 7U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(4U, message.body.config.state);
    packet.data[1] = 5U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.data[1] = 4U;
    packet.data[2] = 8U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_error_frame_length_rules(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_PWR_CFG2,
        .type = SCP_TYPE_ERROR, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU};
    rf_scp_message_t message;

    packet.data[0] = RF_SCP_ERR_BUSY;
    packet.data_len = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    packet.data[0] = RF_SCP_ERR_INVALID_PARAM;
    packet.data_len = 3U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    packet.data_len = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    packet.data_len = 4U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    packet.data_len = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    packet.data[0] = RF_SCP_ERR_BUSY;
    packet.data_len = 3U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    /* Extended error bodies are reserved for the powerboard config pair. */
    packet.cmd = RF_SCP_CMD_CFG_READ_ALL;
    packet.data_len = 2U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
}

void test_time_sync_rejects_every_out_of_range_calendar_field(void)
{
    scp_packet_t packet;
    scp_packet_t out;

    /* Milliseconds 60000 is one past the valid maximum. */
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    scp_pack_u16(packet.data, 60000U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    /* Minute 60, hour 24, day 0, month 13 and year 100 are rejected. */
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    packet.data[2] = 60U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    packet.data[3] = 24U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    packet.data[4] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    packet.data[5] = 13U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    packet.data[6] = 100U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    /* The unchanged captured timestamp stays valid. */
    packet = first_packet(RF_SCP_CMD_TIME_SYNC, SCP_TYPE_SET);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
}

void test_inventory_body_rejects_zone_feeder_and_phase_bounds(void)
{
    scp_packet_t packet = first_packet(RF_SCP_CMD_INVENTORY_SET,
                                       SCP_TYPE_SET);
    scp_packet_t out;

    packet.data[0] = 8U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_INVENTORY_SET, SCP_TYPE_SET);
    packet.data[1] = 8U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_INVENTORY_SET, SCP_TYPE_SET);
    packet.data[2] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet = first_packet(RF_SCP_CMD_INVENTORY_SET, SCP_TYPE_SET);
    packet.data[2] = 4U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    /* UPDATE may address an unassigned feeder; SET may not. */
    packet = first_packet(RF_SCP_CMD_INVENTORY_UPDATE, SCP_TYPE_SET);
    packet.data[1] = 0U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
}

void test_log_requests_reject_slot_hundred_and_empty_count(void)
{
    scp_packet_t packet = {.type = SCP_TYPE_SET};
    scp_packet_t out;

    packet.cmd = RF_SCP_CMD_LOG_CONSUME_TO;
    packet.data_len = 2U;
    scp_pack_u16(packet.data, 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.type = SCP_TYPE_GET;
    packet.cmd = RF_SCP_CMD_LOG_READ_RECORD;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    scp_pack_u16(packet.data, 99U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_build_packet(&packet, &out));
    packet.type = SCP_TYPE_GET;
    packet.cmd = RF_SCP_CMD_LOG_READ_RANGE;
    packet.data_len = 4U;
    scp_pack_u16(packet.data, 100U);
    scp_pack_u16(&packet.data[2], 1U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    scp_pack_u16(packet.data, 36U);
    scp_pack_u16(&packet.data[2], 0U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
}

void test_log_batch_length_rules_for_range_and_single_record(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_LOG_READ_RANGE,
        .type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU};
    rf_scp_message_t message;

    packet.data_len = 246U;   /* Above the 240-byte transport limit. */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    packet.data_len = 61U;    /* Within the limit, not a record multiple. */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
    packet.cmd = RF_SCP_CMD_LOG_READ_RECORD;
    packet.data_len = 120U;   /* Multi-record body is not a single read. */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_LEN,
                          rf_scp_decode_message(&packet, &message));
}

void test_power_command_rejects_manufacturer_service_codes(void)
{
    scp_packet_t packet = {.type = SCP_TYPE_SET, .cmd = RF_SCP_CMD_PWR_COMMAND,
        .data_len = 2U};
    scp_packet_t out;

    packet.data[0] = 1U;
    packet.data[1] = 0xA5U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[0] = 4U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
    packet.data[0] = 5U;
    packet.data[1] = 0x11U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
}

void test_power_config_rejects_period_with_reserved_bit_set(void)
{
    scp_packet_t packet = {0};
    scp_packet_t out;

    packet.type = SCP_TYPE_SET;
    packet.cmd = RF_SCP_CMD_PWR_CFG2;
    packet.data_len = 16U;
    packet.data[0] = 1U;
    scp_pack_u16(&packet.data[1], 0x4000U);
    packet.data[15] = 0xC0U;   /* Bit 7 is reserved and must be zero. */
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_build_packet(&packet, &out));
}

void test_live_notification_rejects_log_pending_above_capacity(void)
{
    scp_packet_t packet = {.cmd = RF_SCP_CMD_LIVE_DATA,
        .type = SCP_TYPE_SET, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU, .data_len = 33U};
    rf_scp_message_t message;

    packet.data[0] = 0x05U;
    packet.data[29] = 99U;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK,
                          rf_scp_decode_message(&packet, &message));
    TEST_ASSERT_EQUAL_UINT8(99U, message.body.live.log_pending);
    for (uint16_t value = 100U; 255U >= value; value++)
    {
        packet.data[29] = (uint8_t)value;
        TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                              rf_scp_decode_message(&packet, &message));
    }
}

void test_hub_counters_reject_slot_hundred_everywhere(void)
{
    scp_packet_t packet = {.type = SCP_TYPE_ACK, .src = RF_SCP_ADDR_HUB,
        .dst = RF_SCP_ADDR_RTU};
    rf_scp_message_t message;

    packet.cmd = RF_SCP_CMD_LOG_AVAILABLE;
    packet.type = SCP_TYPE_SET;
    packet.data_len = 4U;
    scp_pack_u16(&packet.data[2], 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.type = SCP_TYPE_ACK;
    packet.cmd = RF_SCP_CMD_LOG_READ_HEAD;
    packet.data_len = 10U;
    scp_pack_u16(packet.data, 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
    packet.cmd = RF_SCP_CMD_GET_FRAM_STATS;
    packet.data_len = 8U;
    scp_pack_u16(packet.data, 100U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_ERR_PARAM,
                          rf_scp_decode_message(&packet, &message));
}

void test_document_live_offsets_preserve_every_field_from_distinct_wire_values(void)
{
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .type = SCP_TYPE_SET, .cmd = RF_SCP_CMD_LIVE_DATA, .data_len = 33U,
        .data = {0x13U, 0x12U, 0x34U, 0x56U, 0x78U, 0x04U, 0x03U, 0x02U, 0x01U,
            5U, 2U, 0U, 0U, 0x48U, 0x41U, 0U, 0U, 0xFCU, 0x41U, 0U, 0U, 0xA0U, 0x3FU,
            0x85U, 0xFFU, 0xD6U, 0xA5U, 1U, 1U, 98U, 0xFEU, 0xCDU, 0xABU}};
    rf_scp_message_t message;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    const rf_scp_live_t *live = &message.body.live;
    TEST_ASSERT_EQUAL_UINT8(0x13U, live->source);
    TEST_ASSERT_EQUAL_HEX32(0x78563412U, live->seq);
    TEST_ASSERT_EQUAL_HEX32(0x01020304U, live->uptime_sec);
    TEST_ASSERT_EQUAL_UINT8(5U, live->state);
    TEST_ASSERT_EQUAL_UINT8(2U, live->fault_count);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, live->current_amps);
    TEST_ASSERT_EQUAL_FLOAT(31.5f, live->trip_voltage);
    TEST_ASSERT_EQUAL_FLOAT(1.25f, live->harvest_voltage);
    TEST_ASSERT_EQUAL_INT16(-123, live->temperature);
    TEST_ASSERT_EQUAL_INT8(-42, live->rssi);
    TEST_ASSERT_EQUAL_HEX8(0xA5U, live->flags);
    TEST_ASSERT_TRUE(live->fsm_error);
    TEST_ASSERT_TRUE(live->trip_failed);
    TEST_ASSERT_EQUAL_UINT8(98U, live->log_pending);
    TEST_ASSERT_EQUAL_HEX8(0xFEU, live->log_wrap_low);
    TEST_ASSERT_EQUAL_HEX16(0xABCDU, live->boot_counter);
}

void test_document_power_offsets_preserve_units_signs_and_lifetime_fields(void)
{
    scp_packet_t packet = {.src = RF_SCP_ADDR_HUB, .dst = RF_SCP_ADDR_RTU,
        .type = SCP_TYPE_SET, .cmd = RF_SCP_CMD_PWR_SUMMARY, .data_len = 45U,
        .data = {1U, 82U, 0xC3U, 0x45U, 3U, 2U, 4U, 9U,
            0x34U, 0x12U, 0x45U, 0x23U, 0xBFU, 0xFEU, 0x56U, 0x34U, 0x67U, 0x45U,
            0x18U, 0xFCU, 0xD2U, 0x04U, 0xD7U, 0xF6U, 0x80U, 0xF2U, 0x38U, 0xFEU,
            98U, 0xE7U, 45U, 54U, 100U, 250U, 0x78U, 0x56U, 0x34U, 0x12U, 3U,
            0x34U, 0x12U, 0xC4U, 0x09U, 120U, 0U}};
    rf_scp_message_t message;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_message(&packet, &message));
    const rf_scp_power_summary_t *power = &message.body.power;
    TEST_ASSERT_EQUAL_UINT8(82U, power->seq);
    TEST_ASSERT_EQUAL_HEX8(0xC3U, power->flags);
    TEST_ASSERT_EQUAL_HEX8(0x45U, power->flags2);
    TEST_ASSERT_EQUAL_UINT8(3U, power->age_sec);
    TEST_ASSERT_EQUAL_UINT8(2U, power->source);
    TEST_ASSERT_EQUAL_UINT8(4U, power->charge_phase);
    TEST_ASSERT_EQUAL_UINT8(9U, power->session);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, power->pv_mv);
    TEST_ASSERT_EQUAL_HEX16(0x2345U, power->dc_mv);
    TEST_ASSERT_EQUAL_INT16(-321, power->input_ma);
    TEST_ASSERT_EQUAL_HEX16(0x3456U, power->system_mv);
    TEST_ASSERT_EQUAL_HEX16(0x4567U, power->battery_mv);
    TEST_ASSERT_EQUAL_INT16(-1000, power->battery_ma);
    TEST_ASSERT_EQUAL_INT16(1234, power->input_power_10mw);
    TEST_ASSERT_EQUAL_INT16(-2345, power->battery_power_10mw);
    TEST_ASSERT_EQUAL_INT16(-3456, power->system_power_10mw);
    TEST_ASSERT_EQUAL_INT16(-456, power->soc_tenths);
    TEST_ASSERT_EQUAL_UINT8(98U, power->soh_percent);
    TEST_ASSERT_EQUAL_INT8(-25, power->battery_temperature);
    TEST_ASSERT_EQUAL_INT8(45, power->board_temperature);
    TEST_ASSERT_EQUAL_UINT8(54U, power->capacity_ah);
    TEST_ASSERT_EQUAL_UINT8(100U, power->charge_rate_permille);
    TEST_ASSERT_EQUAL_UINT8(250U, power->last_gasp_count);
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, power->active_alarms);
    TEST_ASSERT_EQUAL_UINT8(3U, power->soc_flags);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, power->equivalent_cycles);
    TEST_ASSERT_EQUAL_UINT16(2500U, power->remaining_cycles);
    TEST_ASSERT_EQUAL_UINT16(120U, power->remaining_years_x10);
}

void test_document_event_offsets_preserve_complete_record_and_independent_crc(void)
{
    /* CRC reference: Python binascii.crc_hqx over the first 58 bytes. */
    static const uint8_t raw[60] = {
        0xD2U, 0x47U, 0x04U, 0x0EU, 0x5BU, 0x0AU, 0x1AU, 0x5AU, 0x07U, 0x04U,
        0x03U, 0x02U, 0x55U, 0x01U, 0x01U, 0x00U, 0x00U, 0x48U, 0x41U, 0x00U,
        0x00U, 0x80U, 0x3EU, 0x78U, 0x56U, 0x34U, 0x12U, 0x00U, 0x00U, 0xFCU,
        0x41U, 0x00U, 0x00U, 0xA0U, 0x3FU, 0x00U, 0x00U, 0x50U, 0x40U, 0x85U,
        0xFFU, 0x21U, 0x43U, 0x32U, 0x54U, 0x00U, 0x00U, 0x00U, 0x41U, 0xCDU,
        0xABU, 0x04U, 0x03U, 0x02U, 0x01U, 0x01U, 0x87U, 0x17U, 0x75U, 0xB2U};
    rf_scp_event_t event;
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_decode_event(raw, sizeof(raw), &event));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, event.timestamp, 7U);
    TEST_ASSERT_EQUAL_UINT8(90U, event.event);
    TEST_ASSERT_EQUAL_UINT8(7U, event.zone);
    TEST_ASSERT_EQUAL_UINT8(4U, event.feeder);
    TEST_ASSERT_EQUAL_UINT8(3U, event.phase);
    TEST_ASSERT_EQUAL_UINT8(2U, event.fault_count);
    TEST_ASSERT_EQUAL_UINT8(85U, event.soh_ratio);
    TEST_ASSERT_EQUAL_UINT8(1U, event.nominal_current_status);
    TEST_ASSERT_EQUAL_UINT8(1U, event.energy_status);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, event.current_amps);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, event.di_dt);
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, event.duration_ms);
    TEST_ASSERT_EQUAL_FLOAT(31.5f, event.trip_voltage);
    TEST_ASSERT_EQUAL_FLOAT(1.25f, event.harvest_voltage);
    TEST_ASSERT_EQUAL_FLOAT(3.25f, event.reference_voltage);
    TEST_ASSERT_EQUAL_INT16(-123, event.temperature);
    TEST_ASSERT_EQUAL_HEX16(0x4321U, event.permanent_faults);
    TEST_ASSERT_EQUAL_HEX16(0x5432U, event.temporary_faults);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, event.soh_idc);
    TEST_ASSERT_EQUAL_HEX16(0xABCDU, event.boot_counter);
    TEST_ASSERT_EQUAL_HEX32(0x01020304U, event.uptime_sec);
    TEST_ASSERT_EQUAL_UINT8(1U, event.clock_quality);
    TEST_ASSERT_EQUAL_HEX16(0x1787U, event.src_eui_hash);
    TEST_ASSERT_EQUAL_HEX16(0xB275U, event.crc);
}

/*** end of file ***/
