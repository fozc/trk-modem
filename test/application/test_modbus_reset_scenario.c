/*
 * test_modbus_reset_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify relay control and reset through the real Modbus RTU core.
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_reboot.h"
#include "mock_modbus_config.h"
#include "mock_relay.h"
#include "mock_modbus_line_data.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_power_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_power_board_scp.h"
#include "modbus_rtu_slave.h"

/* DMA hardware is outside this command/response scenario. */
#define MODBUS_TX_USE_DMA 0
#include "../../Application/modbus_process.c"

static modbus_slave_t receiver;
static uint32_t fake_tick;
static uint32_t reset_requests;
static bool relay_save_fails;
static uint8_t response[16];
static bool relay_states[RELAY_CH_COUNT];
static uint32_t relay_writes;
static uint16_t response_len;

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

static uint32_t get_tick(void)
{
    return fake_tick;
}

static void schedule_reset(uint32_t delay_ms, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(1000U, delay_ms);
    reset_requests++;
}

static void capture_response(const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(sizeof(response), length);
    memcpy(response, data, length);
    response_len = length;
}

static uint16_t frame_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length; index++)
    {
        /* XOR of a 16-bit CRC and one byte remains in uint16_t range. */
        crc = (uint16_t)(crc ^ data[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    return crc;
}

static void send_request(uint8_t slave_id, uint8_t function,
                         uint16_t wire_address, uint16_t value)
{
    uint8_t frame[8] =
    {
        slave_id, function,
        (uint8_t)(wire_address >> 8U),
        (uint8_t)(wire_address & 0xFFU),
        (uint8_t)(value >> 8U), (uint8_t)(value & 0xFFU), 0U, 0U
    };
    const uint16_t crc = frame_crc(frame, 6U);
    /* Each masked CRC byte is in the uint8_t range. */
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);

    response_len = 0U;
    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        libmodbusrtu_modbus_rx_byte(&receiver, frame[index]);
    }
    fake_tick += MODBUS_TIMEOUT_MS + 1U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&receiver));
    if (0U != response_len)
    {
        const uint16_t reply_crc = frame_crc(response, response_len - 2U);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(reply_crc & 0xFFU),
                              response[response_len - 2U]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(reply_crc >> 8U),
                              response[response_len - 1U]);
    }
    const bool valid_write =
        ((10001U == wire_address) && (1U == value)) ||
        (((10002U == wire_address) || (10003U == wire_address)) &&
         (1U >= value));
    if ((1U == slave_id) && (6U == function) && valid_write &&
        !relay_save_fails)
    {
        TEST_ASSERT_EQUAL_UINT16(sizeof(frame), response_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, response, sizeof(frame));
    }
}

static void send_write(uint8_t slave_id, uint16_t wire_address,
                       uint16_t value)
{
    send_request(slave_id, 6U, wire_address, value);
}

static bool set_relay(relay_channel_t channel, bool on, int call_count)
{
    (void)call_count;
    if (relay_save_fails)
    {
        return false;
    }
    TEST_ASSERT_TRUE(RELAY_CH_COUNT > channel);
    relay_states[channel] = on;
    relay_writes++;
    return true;
}

static bool get_relay(relay_channel_t channel, int call_count)
{
    (void)call_count;
    TEST_ASSERT_TRUE(RELAY_CH_COUNT > channel);
    return relay_states[channel];
}

void setUp(void)
{
    modbus_system_stats_invalidate_snapshot_Ignore();
    modbus_power_stats_invalidate_snapshot_Ignore();
    fake_tick = 0U;
    reset_requests = 0U;
    relay_writes = 0U;
    relay_save_fails = false;
    memset(relay_states, 0, sizeof(relay_states));
    relay_set_StubWithCallback(set_relay);
    relay_is_on_StubWithCallback(get_relay);
    response_len = 0U;
    memset(response, 0, sizeof(response));
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture_response);
    libmodbusrtu_register_read_callback(&receiver, fc03_read_callback);
    libmodbusrtu_register_write_callback(&receiver, fc06_write_callback);
    reboot_system_delayed_Stub(schedule_reset);
}

void tearDown(void)
{
}

void test_reset_write_schedules_one_second_reset_and_echoes_request(void)
{
    send_write(1U, 10001U, 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_requests);
}

void test_broadcast_reset_schedules_reset_without_response(void)
{
    send_write(0U, 10001U, 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_requests);
    TEST_ASSERT_EQUAL_UINT16(0U, response_len);
}

void test_invalid_reset_value_returns_exception_without_scheduling_reset(void)
{
    send_write(1U, 10001U, 2U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_requests);
    TEST_ASSERT_EQUAL_UINT16(5U, response_len);
    TEST_ASSERT_EQUAL_HEX8(0x86U, response[1]);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, response[2]);
}

void test_other_register_returns_exception_without_scheduling_reset(void)
{
    send_write(1U, 9999U, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_requests);
    TEST_ASSERT_EQUAL_UINT16(5U, response_len);
    TEST_ASSERT_EQUAL_HEX8(0x86U, response[1]);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, response[2]);
}

void test_relay_channels_write_on_off_and_read_back_independently(void)
{
    send_request(1U, 3U, 10002U, 2U);
    TEST_ASSERT_EQUAL_UINT16(9U, response_len);
    const uint8_t initial_states[] = {1U, 3U, 4U, 0U, 0U, 0U, 0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(initial_states, response,
                                 sizeof(initial_states));
    send_write(1U, 10002U, 1U);
    TEST_ASSERT_TRUE(relay_states[RELAY_CH_1]);
    TEST_ASSERT_FALSE(relay_states[RELAY_CH_2]);
    send_request(1U, 3U, 10002U, 2U);
    TEST_ASSERT_EQUAL_UINT16(9U, response_len);
    const uint8_t first_states[] = {1U, 3U, 4U, 0U, 1U, 0U, 0U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first_states, response, sizeof(first_states));

    send_write(1U, 10003U, 1U);
    send_write(1U, 10002U, 0U);
    send_request(1U, 3U, 10002U, 2U);
    const uint8_t second_states[] = {1U, 3U, 4U, 0U, 0U, 0U, 1U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second_states, response, sizeof(second_states));
    send_write(1U, 10003U, 0U);
    send_request(1U, 3U, 10003U, 1U);
    TEST_ASSERT_EQUAL_UINT16(7U, response_len);
    TEST_ASSERT_EQUAL_UINT8(0U, response[3]);
    TEST_ASSERT_EQUAL_UINT8(0U, response[4]);
    TEST_ASSERT_EQUAL_UINT32(4U, relay_writes);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_requests);
}

void test_relay_read_reflects_state_changed_outside_modbus(void)
{
    relay_states[RELAY_CH_2] = true;
    send_request(1U, 3U, 10003U, 1U);
    TEST_ASSERT_EQUAL_UINT16(7U, response_len);
    TEST_ASSERT_EQUAL_UINT8(1U, response[4]);
    TEST_ASSERT_EQUAL_UINT32(0U, relay_writes);
}

void test_invalid_relay_values_preserve_both_channels(void)
{
    relay_states[RELAY_CH_1] = true;
    const uint16_t invalid[] = {2U, 0xFFFFU};
    for (size_t channel = 0U; channel < RELAY_CH_COUNT; channel++)
    {
        for (size_t index = 0U; index < 2U; index++)
        {
            send_write(1U, (uint16_t)(10002U + channel), invalid[index]);
            TEST_ASSERT_EQUAL_UINT16(5U, response_len);
            TEST_ASSERT_EQUAL_HEX8(0x86U, response[1]);
            TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                                    response[2]);
            TEST_ASSERT_TRUE(relay_states[RELAY_CH_1]);
            TEST_ASSERT_FALSE(relay_states[RELAY_CH_2]);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0U, relay_writes);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_requests);
}

void test_broadcast_relay_write_changes_state_without_response(void)
{
    send_write(0U, 10003U, 1U);
    TEST_ASSERT_TRUE(relay_states[RELAY_CH_2]);
    TEST_ASSERT_EQUAL_UINT16(0U, response_len);
    send_write(0U, 10003U, 2U);
    TEST_ASSERT_TRUE(relay_states[RELAY_CH_2]);
    TEST_ASSERT_EQUAL_UINT16(0U, response_len);
    TEST_ASSERT_EQUAL_UINT32(1U, relay_writes);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_requests);
}

void test_unmapped_control_address_rejects_read_and_write(void)
{
    send_write(1U, 10004U, 1U);
    TEST_ASSERT_EQUAL_UINT16(5U, response_len);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, response[2]);
    modbus_system_stats_read_IgnoreAndReturn(false);
    modbus_power_stats_read_IgnoreAndReturn(false);
    modbus_bms_stats_read_IgnoreAndReturn(false);
    modbus_gsm_stats_read_IgnoreAndReturn(false);
    send_request(1U, 3U, 10004U, 1U);
    TEST_ASSERT_EQUAL_UINT16(5U, response_len);
    TEST_ASSERT_EQUAL_HEX8(0x83U, response[1]);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, response[2]);
    TEST_ASSERT_EQUAL_UINT32(0U, relay_writes);
}

void test_relay_save_failure_returns_device_exception(void)
{
    relay_save_fails = true;
    send_write(1U, 10002U, 1U);
    TEST_ASSERT_EQUAL_UINT16(5U, response_len);
    TEST_ASSERT_EQUAL_UINT8(0x86U, response[1]);
    TEST_ASSERT_EQUAL_UINT8(0x04U, response[2]);
    TEST_ASSERT_EQUAL_UINT32(0U, relay_writes);
}

/*** end of file ***/
