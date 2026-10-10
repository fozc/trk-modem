/*
 * test_modbus_request_length.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Check malformed requests through the production RTU receive path.
 */
#include "unity.h"
#include "modbus_rtu_slave.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct
{
    uint8_t before[16];
    modbus_slave_t context;
    uint8_t after[16];
} guarded;
static uint32_t fake_tick;
static uint32_t read_calls;
static uint32_t write_calls;
static uint8_t request[9];
static uint8_t response[MODBUS_BUFFER_SIZE];
static uint16_t read_span;
static bool inject_receive;
static uint16_t response_len;
static uint32_t response_count;

static uint16_t frame_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length; index++)
    {
        /* XOR of a 16-bit CRC and one byte fits in uint16_t. */
        crc = (uint16_t)(crc ^ data[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    return crc;
}

static uint32_t get_tick(void)
{
    return fake_tick;
}

static void capture_response(const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(sizeof(response), length);
    memcpy(response, data, length);
    response_len = length;
    response_count++;
}

static modbus_reg_status_t read_register(uint16_t address, uint16_t *value)
{
    TEST_ASSERT_EQUAL_UINT32(MODBUS_HOLDING_REG_BASE + read_calls, address);
    TEST_ASSERT_TRUE(read_span > read_calls);
    if (inject_receive)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, 0xFFU);
    }
    read_calls++;
    *value = 0x1357U;
    return MODBUS_REG_OK;
}

static modbus_reg_status_t write_register(uint16_t address, uint16_t value)
{
    TEST_ASSERT_EQUAL_UINT16(MODBUS_HOLDING_REG_BASE, address);
    TEST_ASSERT_EQUAL_UINT16(1U, value);
    write_calls++;
    return MODBUS_REG_OK;
}

static void clear_observations(void)
{
    read_calls = 0U;
    write_calls = 0U;
    response_len = 0U;
    response_count = 0U;
    memset(response, 0, sizeof(response));
}

static void receive_request(uint8_t function, uint8_t address, size_t length,
                            bool valid_crc)
{
    TEST_ASSERT_TRUE((4U <= length) && (length <= sizeof(request)));
    memset(request, 0, sizeof(request));
    request[0] = address;
    request[1] = function;
    request[5] = 1U;
    request[6] = 0xAAU;
    const uint16_t crc = frame_crc(request, length - 2U);
    /* Each masked or shifted CRC byte fits in uint8_t. */
    request[length - 2U] = (uint8_t)(crc & 0xFFU);
    request[length - 1U] = (uint8_t)(crc >> 8U);
    if (!valid_crc)
    {
        request[length - 1U] ^= 1U;
    }

    for (size_t index = 0U; index < length; index++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, request[index]);
    }
}

static void send_request(uint8_t function, uint8_t address, size_t length,
                         bool valid_crc)
{
    receive_request(function, address, length, valid_crc);
    fake_tick += MODBUS_TIMEOUT_MS + 1U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&guarded.context));
}

static void assert_next_valid_request_succeeds(uint8_t function)
{
    clear_observations();
    send_request(function, 1U, 8U, true);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
    TEST_ASSERT_EQUAL_UINT8(function, response[1]);
    if (MODBUS_FC_READ_HOLDING_REGISTERS == function)
    {
        TEST_ASSERT_EQUAL_UINT32(1U, read_calls);
        TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
        TEST_ASSERT_EQUAL_UINT16(7U, response_len);
        TEST_ASSERT_EQUAL_UINT8(2U, response[2]);
        TEST_ASSERT_EQUAL_HEX8(0x13U, response[3]);
        TEST_ASSERT_EQUAL_HEX8(0x57U, response[4]);
    }
    else
    {
        TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
        TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
        TEST_ASSERT_EQUAL_UINT16(8U, response_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(request, response, 8U);
    }
}

static void assert_bad_lengths_are_rejected(uint8_t function)
{
    const size_t lengths[] = {4U, 7U, 9U};
    for (size_t index = 0U; index < 3U; index++)
    {
        clear_observations();
        send_request(function, 1U, lengths[index], true);
        TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
        TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
        TEST_ASSERT_EQUAL_UINT32(1U, response_count);
        TEST_ASSERT_EQUAL_UINT16(5U, response_len);
        TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
        TEST_ASSERT_EQUAL_UINT8(function | 0x80U, response[1]);
        TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                                response[2]);
        const uint16_t crc = frame_crc(response, 3U);
        TEST_ASSERT_EQUAL_HEX8(crc & 0xFFU, response[3]);
        TEST_ASSERT_EQUAL_HEX8(crc >> 8U, response[4]);
        TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
            libmodbusrtu_modbus_get_last_exception(&guarded.context));
        assert_next_valid_request_succeeds(function);
    }
}

static void assert_bad_lengths_are_silent(uint8_t address, bool valid_crc)
{
    const uint8_t functions[] =
    {
        MODBUS_FC_READ_HOLDING_REGISTERS,
        MODBUS_FC_WRITE_SINGLE_REGISTER
    };
    const size_t lengths[] = {4U, 7U, 9U};
    for (size_t function = 0U; function < 2U; function++)
    {
        for (size_t index = 0U; index < 3U; index++)
        {
            clear_observations();
            send_request(functions[function], address, lengths[index],
                         valid_crc);
            TEST_ASSERT_EQUAL_UINT32(0U, response_count);
            TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
            TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
            assert_next_valid_request_succeeds(functions[function]);
        }
    }
}

void setUp(void)
{
    (void)memset(&guarded, 0xA5, sizeof(guarded));
    fake_tick = 0U;
    read_span = 1U;
    inject_receive = false;
    clear_observations();
    libmodbusrtu_slave_init(&guarded.context, 1U, get_tick, capture_response);
    libmodbusrtu_register_read_callback(&guarded.context, read_register);
    libmodbusrtu_register_write_callback(&guarded.context, write_register);
}

void tearDown(void)
{
    for (size_t index = 0U; sizeof(guarded.before) > index; index++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.before[index]);
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.after[index]);
    }
}

void test_fc03_bad_lengths_return_exception_and_next_request_succeeds(void)
{
    assert_bad_lengths_are_rejected(MODBUS_FC_READ_HOLDING_REGISTERS);
}

void test_fc06_bad_lengths_return_exception_and_next_request_succeeds(void)
{
    assert_bad_lengths_are_rejected(MODBUS_FC_WRITE_SINGLE_REGISTER);
}

void test_bad_length_broadcast_is_silent_and_does_not_call_handlers(void)
{
    assert_bad_lengths_are_silent(0U, true);
}

void test_bad_length_for_other_slave_is_silent_and_does_not_call_handlers(void)
{
    assert_bad_lengths_are_silent(2U, true);
}

void test_bad_length_with_bad_crc_is_silent_and_does_not_call_handlers(void)
{
    assert_bad_lengths_are_silent(1U, false);
}

static void assert_waits_for_idle_gap(void)
{
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_RECEIVING,
                          libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
    fake_tick += MODBUS_TIMEOUT_MS - 1U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_RECEIVING,
                          libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
    fake_tick++;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
}

void test_supported_request_waits_for_idle_gap_not_eight_byte_length(void)
{
    const uint8_t functions[] =
    {
        MODBUS_FC_READ_HOLDING_REGISTERS,
        MODBUS_FC_WRITE_SINGLE_REGISTER
    };
    for (size_t index = 0U; index < 2U; index++)
    {
        clear_observations();
        receive_request(functions[index], 1U, 8U, true);
        assert_waits_for_idle_gap();
        TEST_ASSERT_EQUAL_UINT8(functions[index], response[1]);
        TEST_ASSERT_EQUAL_UINT32(1U, read_calls + write_calls);
    }
}

void test_unsupported_functions_wait_then_return_illegal_function(void)
{
    const uint8_t functions[] =
    {
        MODBUS_FC_READ_COILS,
        MODBUS_FC_WRITE_MULTIPLE_COILS
    };
    for (size_t index = 0U; index < 2U; index++)
    {
        clear_observations();
        receive_request(functions[index], 1U, 8U + index, true);
        assert_waits_for_idle_gap();
        TEST_ASSERT_EQUAL_UINT32(0U, read_calls + write_calls);
        TEST_ASSERT_EQUAL_UINT16(5U, response_len);
        TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
        TEST_ASSERT_EQUAL_UINT8(functions[index] | 0x80U, response[1]);
        TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                                response[2]);
        const uint16_t crc = frame_crc(response, 3U);
        TEST_ASSERT_EQUAL_HEX8(crc & 0xFFU, response[3]);
        TEST_ASSERT_EQUAL_HEX8(crc >> 8U, response[4]);
        assert_next_valid_request_succeeds(
            MODBUS_FC_READ_HOLDING_REGISTERS);
    }
}

void test_idle_gap_is_measured_from_last_received_byte(void)
{
    receive_request(MODBUS_FC_READ_HOLDING_REGISTERS, 1U, 8U, true);
    fake_tick += MODBUS_TIMEOUT_MS - 1U;
    /* A ninth byte must restart the idle timer, not complete an 8-byte frame. */
    libmodbusrtu_modbus_rx_byte(&guarded.context, 1U);
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_RECEIVING,
                          libmodbusrtu_modbus_process(&guarded.context));
    fake_tick++;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_RECEIVING,
                          libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    fake_tick += MODBUS_TIMEOUT_MS - 1U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_EQUAL_UINT32(0U, read_calls + write_calls);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_CRC_ERROR,
        libmodbusrtu_modbus_get_last_exception(&guarded.context));
    assert_next_valid_request_succeeds(MODBUS_FC_READ_HOLDING_REGISTERS);
}

static void send_custom_frame(uint16_t address, uint16_t count)
{
    uint8_t frame[8] = {1U, MODBUS_FC_READ_HOLDING_REGISTERS};
    frame[2] = (uint8_t)(address >> 8U);
    frame[3] = (uint8_t)(address & 0xFFU);
    frame[4] = (uint8_t)(count >> 8U);
    frame[5] = (uint8_t)(count & 0xFFU);
    const uint16_t crc = frame_crc(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);
    for (size_t index = 0U; sizeof(frame) > index; index++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, frame[index]);
    }
    fake_tick += MODBUS_TIMEOUT_MS;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
        libmodbusrtu_modbus_process(&guarded.context));
}

void test_maximum_fc03_response_contains_all_values_and_valid_crc(void)
{
    read_span = MODBUS_MAX_READ_REGISTERS;
    send_custom_frame(0U, read_span);
    TEST_ASSERT_EQUAL_UINT32(125U, read_calls);
    TEST_ASSERT_EQUAL_UINT16(255U, response_len);
    TEST_ASSERT_EQUAL_UINT8(250U, response[2]);
    for (size_t offset = 3U; response_len - 2U > offset; offset += 2U)
    {
        TEST_ASSERT_EQUAL_HEX8(0x13U, response[offset]);
        TEST_ASSERT_EQUAL_HEX8(0x57U, response[offset + 1U]);
    }
    const uint16_t crc = frame_crc(response, response_len - 2U);
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFFU, response[response_len - 2U]);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8U, response[response_len - 1U]);
}

void test_zero_and_oversized_counts_do_not_read_any_register(void)
{
    const uint16_t counts[] = {0U, 126U, UINT16_MAX};
    for (size_t index = 0U; 3U > index; index++)
    {
        clear_observations();
        send_custom_frame(0U, counts[index]);
        TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
        TEST_ASSERT_EQUAL_UINT16(5U, response_len);
        TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
            response[2]);
        assert_next_valid_request_succeeds(MODBUS_FC_READ_HOLDING_REGISTERS);
    }
}

void test_overflowing_wire_address_is_rejected_before_register_callback(void)
{
    send_custom_frame(UINT16_MAX, 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, read_calls);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, response[2]);
    assert_next_valid_request_succeeds(MODBUS_FC_READ_HOLDING_REGISTERS);
}

void test_rx_overflow_recovers_on_next_valid_frame_without_memory_overrun(void)
{
    for (size_t index = 0U; MODBUS_BUFFER_SIZE > index; index++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, 0xFFU);
    }
    TEST_ASSERT_EQUAL_UINT16(MODBUS_BUFFER_SIZE, guarded.context.rx_count);
    assert_next_valid_request_succeeds(MODBUS_FC_READ_HOLDING_REGISTERS);
    TEST_ASSERT_EQUAL_UINT16(0U, guarded.context.rx_count);
    TEST_ASSERT_FALSE(guarded.context.busy);
}

void test_receive_during_callback_does_not_modify_the_frame_being_parsed(void)
{
    inject_receive = true;
    assert_next_valid_request_succeeds(MODBUS_FC_READ_HOLDING_REGISTERS);
    TEST_ASSERT_EQUAL_UINT16(0U, guarded.context.rx_count);
    TEST_ASSERT_FALSE(guarded.context.busy);
}

void test_idle_gap_handles_tick_wrap_and_dispatches_exactly_once(void)
{
    fake_tick = UINT32_MAX - 5U;
    receive_request(MODBUS_FC_WRITE_SINGLE_REGISTER, 1U, 8U, true);
    assert_waits_for_idle_gap();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_IDLE,
        libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
}

/*** end of file ***/
