/*
 * test_modbus_power_snapshot_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Characterize PowerBoard sampling through the production FC03 path.
 */
/* Keep log arguments compiled; LTO removes unused shell handlers. */
#undef NO_SHELL_LOG
#include "unity.h"
#include "mock_bsp.h"
#include "mock_i2c_slave.h"
#include "mock_modbus_config.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_breaker.h"
#include "mock_nvram.h"
#include "modbus_rtu_slave.h"
#include "modbus_power_stats.h"
#include "power_board_decode.h"

#define MODBUS_TX_USE_DMA 0
#include "../../Application/power_board/power_board.c"
#include "../../Application/modbus_process.c"

static modbus_slave_t receiver;
static uint32_t fake_tick;
static uint32_t snapshot_calls;
static bool change_sample;
static uint8_t sample[96];
static uint8_t response[69];
static uint16_t response_len;

static uint32_t get_tick(void)
{
    return fake_tick;
}

static void prepare_sample(uint8_t sequence, uint16_t voltage)
{
    memset(sample, 0, sizeof(sample));
    sample[0x1DU] = sequence;
    sample[0x1EU] = POWER_BOARD_PROT_VER;
    sample[0x10U] = (uint8_t)(voltage >> 8U);
    sample[0x11U] = (uint8_t)(voltage & 0xFFU);
    sample[95U] = power_board_xsum(sample, 95U);
}

static void copy_snapshot(uint8_t *destination, uint8_t start,
                          uint8_t length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT8(POWER_BOARD_TLM_BASE, start);
    TEST_ASSERT_EQUAL_UINT8(sizeof(sample), length);
    snapshot_calls++;
    memcpy(destination, sample, sizeof(sample));
    /* Simulate a complete new I2C sample after register SEQ was read. */
    if (change_sample && (3U == snapshot_calls))
    {
        prepare_sample(8U, 28000U);
    }
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
        crc = (uint16_t)(crc ^ data[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    return crc;
}

static void send_read(uint16_t count)
{
    /* Logical 49200 maps to wire address 9200 (0x23F0). */
    uint8_t frame[8] = {1U, 3U, 0x23U, 0xF0U, 0U, 0U, 0U, 0U};
    frame[4] = (uint8_t)(count >> 8U);
    frame[5] = (uint8_t)(count & 0xFFU);
    const uint16_t crc = frame_crc(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);
    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        libmodbusrtu_modbus_rx_byte(&receiver, frame[index]);
    }
    fake_tick += MODBUS_TIMEOUT_MS;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
                          libmodbusrtu_modbus_process(&receiver));
    TEST_ASSERT_EQUAL_UINT16(5U + (2U * count), response_len);
    TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
    TEST_ASSERT_EQUAL_UINT8(3U, response[1]);
    TEST_ASSERT_EQUAL_UINT8(2U * count, response[2]);
    const uint16_t response_crc = frame_crc(response, response_len - 2U);
    TEST_ASSERT_EQUAL_HEX8(response_crc & 0xFFU,
                          response[response_len - 2U]);
    TEST_ASSERT_EQUAL_HEX8(response_crc >> 8U, response[response_len - 1U]);
}

static uint16_t response_register(size_t index)
{
    const size_t offset = 3U + (2U * index);
    return (uint16_t)(((uint16_t)response[offset] << 8U) |
                       response[offset + 1U]);
}

void setUp(void)
{
    fake_tick = 0U;
    snapshot_calls = 0U;
    change_sample = false;
    response_len = 0U;
    memset(response, 0, sizeof(response));
    prepare_sample(7U, 24000U);
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture_response);
    libmodbusrtu_register_read_callback(&receiver, fc03_read_callback);
    modbus_config_get_addr_aku_uyarisi_IgnoreAndReturn(50000U);
    modbus_config_get_addr_modem_reset_IgnoreAndReturn(50001U);
    modbus_system_stats_read_IgnoreAndReturn(false);
    i2c_slave_snapshot_Stub(copy_snapshot);
}

void tearDown(void)
{
}

void test_full_power_block_takes_one_snapshot_per_register(void)
{
    send_read(32U);
    TEST_ASSERT_EQUAL_UINT32(32U, snapshot_calls);
    TEST_ASSERT_EQUAL_UINT16(1U, response_register(0U));
    TEST_ASSERT_EQUAL_UINT16(7U, response_register(2U));
    TEST_ASSERT_EQUAL_UINT16(24000U, response_register(10U));
}

void test_bulk_response_can_mix_two_valid_power_samples(void)
{
    change_sample = true;
    send_read(11U);
    TEST_ASSERT_EQUAL_UINT32(11U, snapshot_calls);
    TEST_ASSERT_EQUAL_UINT16(1U, response_register(0U));
    TEST_ASSERT_EQUAL_UINT16(7U, response_register(2U));
    TEST_ASSERT_EQUAL_UINT16(28000U, response_register(10U));
    power_board_telemetry_t latest;
    TEST_ASSERT_TRUE(power_board_decode_telemetry(sample, &latest));
    TEST_ASSERT_EQUAL_UINT8(8U, latest.seq);
    TEST_ASSERT_EQUAL_UINT16(28000U, latest.vbat_mv);
}

/*** end of file ***/
