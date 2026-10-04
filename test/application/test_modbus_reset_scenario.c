/*
 * test_modbus_reset_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify the production reset callback through the real Modbus RTU core.
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_reboot.h"
#include "mock_modbus_config.h"
#include "modbus_rtu_slave.h"

/* DMA hardware is outside this command/response scenario. */
#define MODBUS_TX_USE_DMA 0
#include "../../Application/modbus_process.c"

static modbus_slave_t receiver;
static uint32_t fake_tick;
static uint32_t reset_requests;
static uint8_t response[8];
static uint16_t response_len;

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
    if (8U == length)
    {
        TEST_ASSERT_EQUAL_UINT32(1U, reset_requests);
    }
    memcpy(response, data, length);
    response_len = length;
}

static void send_write(uint8_t slave_id, uint16_t wire_address,
                       uint16_t value)
{
    uint8_t frame[8] =
    {
        slave_id, 6U,
        (uint8_t)(wire_address >> 8U),
        (uint8_t)(wire_address & 0xFFU),
        (uint8_t)(value >> 8U), (uint8_t)(value & 0xFFU), 0U, 0U
    };
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < 6U; index++)
    {
        /* XOR of a 16-bit CRC and one byte remains in uint16_t range. */
        crc = (uint16_t)(crc ^ frame[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    /* Each masked CRC byte is in the uint8_t range. */
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);

    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        libmodbusrtu_modbus_rx_byte(&receiver, frame[index]);
    }
    fake_tick += MODBUS_TIMEOUT_MS + 1U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&receiver));
    if ((1U == slave_id) && (1U == value) && (10001U == wire_address))
    {
        TEST_ASSERT_EQUAL_UINT16(sizeof(frame), response_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, response, sizeof(frame));
    }
}

void setUp(void)
{
    fake_tick = 0U;
    reset_requests = 0U;
    response_len = 0U;
    memset(response, 0, sizeof(response));
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture_response);
    libmodbusrtu_register_write_callback(&receiver, fc06_write_callback);
    modbus_config_get_addr_modem_reset_IgnoreAndReturn(50001U);
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

/*** end of file ***/
