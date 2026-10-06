/*
 * test_modbus_power_snapshot_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Characterize canonical PowerBoard sampling through production FC03.
 */
/* Keep log arguments compiled; LTO removes unused shell handlers. */
#undef NO_SHELL_LOG
#include "unity.h"
#include "mock_bsp.h"
#include "power_board_scp.h"
#include "mock_power_board_control.h"
#include "mock_elog.h"
#include "mock_shell.h"

TEST_SOURCE_FILE("power_board_scp.c")
#include "mock_modbus_config.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_breaker.h"
#include "mock_rf.h"
#include "mock_modbus_rf_stats.h"
#include "mock_nvram.h"
#include "modbus_rtu_slave.h"
#include "modbus_power_stats.h"

#define MODBUS_TX_USE_DMA 0
uint32_t HAL_GetTick(void);
#include "../../Application/modbus_process.c"

static modbus_slave_t receiver;
static uint32_t fake_tick;
static uint8_t response[5U + 2U * MODBUS_PWR_STATS_REG_COUNT];
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

static void prepare_sample(uint8_t sequence, uint16_t voltage)
{
    rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_PWR_SUMMARY, .type = SCP_TYPE_SET,
        .body.power =
        {
            .flags = 0x83U, .flags2 = 5U, .soc_flags = 3U,
            .source = 1U, .session = 7U, .charge_phase = 2U,
            .battery_ma = -321, .soc_tenths = -125,
            .battery_temperature = 24, .board_temperature = 31,
            .capacity_ah = 12U, .soh_percent = 96U
        }
    };
    message.body.power.seq = sequence;
    message.body.power.battery_mv = voltage;
    TEST_ASSERT_TRUE(power_board_handle_summary(&message, fake_tick));
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
    modbus_rf_stats_read_IgnoreAndReturn(false);
    power_board_control_init_Ignore();
    fake_tick = 0U;
    response_len = 0U;
    memset(response, 0, sizeof(response));
    shell_register_command_IgnoreAndReturn(0);
    power_board_scp_init();
    elog_add_Ignore();
    prepare_sample(7U, 24000U);
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture_response);
    libmodbusrtu_register_read_callback(&receiver, fc03_read_callback);
    modbus_config_get_addr_aku_uyarisi_IgnoreAndReturn(50000U);
    modbus_config_get_addr_modem_reset_IgnoreAndReturn(50001U);
    modbus_system_stats_read_IgnoreAndReturn(false);
}

void tearDown(void)
{
}

void test_full_scp_power_block_preserves_signed_quantities_and_quality(void)
{
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_UINT16(1U, response_register(0U));
    TEST_ASSERT_TRUE(0U != (response_register(1U) & POWER_VALID_SUMMARY));
    TEST_ASSERT_EQUAL_UINT16(7U, response_register(2U));
    TEST_ASSERT_EQUAL_UINT16(24000U, response_register(13U));
    TEST_ASSERT_EQUAL_HEX16((uint16_t)-321, response_register(14U));
    TEST_ASSERT_EQUAL_HEX16((uint16_t)-125, response_register(18U));
    TEST_ASSERT_EQUAL_UINT16(1U, response_register(37U));
}

void test_stale_power_block_retains_raw_values_but_clears_quality(void)
{
    fake_tick = 30000U;
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_UINT16(0U, response_register(1U));
    TEST_ASSERT_EQUAL_UINT16(24000U, response_register(13U));
    prepare_sample(8U, 28000U);
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_UINT16(8U, response_register(2U));
    TEST_ASSERT_EQUAL_UINT16(28000U, response_register(13U));
    TEST_ASSERT_TRUE(0U != (response_register(1U) & POWER_VALID_SUMMARY));
}

void test_power_read_rejects_out_of_range_and_null_without_write(void)
{
    uint16_t value = 0xA55AU;

    TEST_ASSERT_FALSE(modbus_power_stats_read(
        MODBUS_PWR_STATS_ADDR_BASE - 1U, &value));
    TEST_ASSERT_EQUAL_HEX16(0xA55AU, value);
    TEST_ASSERT_FALSE(modbus_power_stats_read(
        MODBUS_PWR_STATS_ADDR_BASE + MODBUS_PWR_STATS_REG_COUNT, &value));
    TEST_ASSERT_EQUAL_HEX16(0xA55AU, value);
    TEST_ASSERT_FALSE(modbus_power_stats_read(
        MODBUS_PWR_STATS_ADDR_BASE, NULL));
}

void test_missing_summary_has_no_quality_and_unknown_age(void)
{
    power_board_scp_init();
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_UINT16(0U, response_register(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, response_register(1U));
    TEST_ASSERT_EQUAL_UINT16(255U, response_register(5U));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFU, response_register(34U));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFU, response_register(35U));
}

void test_alarm_words_preserve_all_bits_in_fc03_response(void)
{
    const rf_scp_message_t message =
    {
        .cmd = RF_SCP_CMD_PWR_ALARM, .type = SCP_TYPE_SET,
        .body.alarm = {.active = 0xA1234567U, .seq = 8U, .state = 1U}
    };

    TEST_ASSERT_TRUE(power_board_handle_alarm(&message, fake_tick));
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_HEX16(0xA123U, response_register(25U));
    TEST_ASSERT_EQUAL_HEX16(0x4567U, response_register(26U));
}

void test_summary_age_uses_high_word_first_in_fc03_response(void)
{
    fake_tick = 0x12345678U;
    send_read(MODBUS_PWR_STATS_REG_COUNT);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, response_register(34U));
    TEST_ASSERT_EQUAL_HEX16((uint16_t)fake_tick, response_register(35U));
    TEST_ASSERT_EQUAL_UINT16(0U, response_register(1U));
}

/*** end of file ***/
