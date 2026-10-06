/*
 * modbus_rf_snapshot_fixture.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Shared real RF/FC03 fixture for fixed and configurable address maps.
 */
#ifndef MODBUS_RF_SNAPSHOT_FIXTURE_H
#define MODBUS_RF_SNAPSHOT_FIXTURE_H
static uint32_t tick;
static uint32_t tick_step;
static rf_feeder_t feeder;
static feeder_data_t legacy;
static modbus_slave_t receiver;
static uint8_t response[64];
static uint16_t response_len;

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    const uint32_t now = tick;
    tick += tick_step;
    return now;
}

#define MODBUS_TX_USE_DMA 0
#include "../../Application/modbus_process.c"

const rf_feeder_t *rf_store_get(feeder_id_t line)
{
    return (0U == line) ? &feeder : NULL;
}

bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN])
{
    uint8_t combined = 0U;

    for (size_t index = 0U; index < RF_EUI64_LEN; index++)
    {
        combined |= eui[index];
    }
    return 0U == combined;
}

static bool binding(uint8_t source, rf_inventory_entry_t *out, int count)
{
    (void)count;
    if (5U != source)
    {
        return false;
    }
    *out = (rf_inventory_entry_t){.feeder = 1U, .phase = 1U, .zone = 1U};
    (void)memcpy(out->eui64, feeder.r_eui64, 8U);
    return true;
}

static bool in_use(uint32_t line, int count)
{
    (void)count;
    return 0U == line;
}

static const feeder_data_t *get_legacy(uint32_t line, int count)
{
    (void)count;
    return (0U == line) ? &legacy : NULL;
}

static uint32_t get_tick(void)
{
    return tick;
}

static void capture(const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_TRUE(sizeof(response) >= length);
    (void)memcpy(response, data, length);
    response_len = length;
}

static uint16_t crc16(const uint8_t *data, size_t length)
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

static void read_regs(uint16_t offset, uint16_t count)
{
    uint8_t frame[8] = {1U, 3U, 0U, 0U, 0U, 0U, 0U, 0U};

    frame[2] = (uint8_t)(offset >> 8U);
    frame[3] = (uint8_t)offset;
    frame[4] = (uint8_t)(count >> 8U);
    frame[5] = (uint8_t)count;
    const uint16_t crc = crc16(frame, 6U);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8U);
    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        libmodbusrtu_modbus_rx_byte(&receiver, frame[index]);
    }
    tick += MODBUS_TIMEOUT_MS;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&receiver));
    TEST_ASSERT_EQUAL_UINT16(5U + 2U * count, response_len);
    TEST_ASSERT_EQUAL_UINT8(3U, response[1]);
    const uint16_t reply_crc = crc16(response, response_len - 2U);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)reply_crc, response[response_len - 2U]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(reply_crc >> 8U),
                          response[response_len - 1U]);
}

static uint16_t word(size_t index)
{
    const size_t offset = 3U + index * 2U;
    return (uint16_t)(((uint16_t)response[offset] << 8U) |
                       response[offset + 1U]);
}

static float current(void)
{
    const uint32_t bits = ((uint32_t)word(0U) << 16U) | word(1U);
    float value;
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static void live(float amps)
{
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U, .uptime_sec = 10U,
                                 .current_amps = amps};
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
}

static void reset_fixture(void)
{
    tick = 0U;
    tick_step = 0U;
    response_len = 0U;
    (void)memset(&feeder, 0, sizeof(feeder));
    (void)memset(&legacy, 0, sizeof(legacy));
    feeder.in_use = true;
    feeder.config.fider_id = 1U;
    feeder.r_eui64[0] = 0x11U;
    legacy.phase[0].anlik_akim = 99.0F;
    legacy.phase[0].rf_haberlesme_varyok = 1U;
    rf_init();
    rf_inventory_get_binding_StubWithCallback(binding);
    modbus_is_line_in_use_StubWithCallback(in_use);
    breaker_get_feeder_data_StubWithCallback(get_legacy);
    modbus_config_get_addr_aku_uyarisi_IgnoreAndReturn(50000U);
    modbus_config_get_addr_modem_reset_IgnoreAndReturn(50001U);
    modbus_system_stats_read_IgnoreAndReturn(false);
    modbus_power_stats_read_IgnoreAndReturn(false);
    modbus_bms_stats_read_IgnoreAndReturn(false);
    modbus_gsm_stats_read_IgnoreAndReturn(false);
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture);
    libmodbusrtu_register_read_callback(&receiver, fc03_read_callback);
}


static void check_real_scp_current_replaces_dummy_and_rf_link_is_online(void)
{
    live(1.25F);
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, current());
    read_regs(MODBUS_OFF_RF_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
}

static void check_missing_and_stale_current_are_nan_and_link_is_offline(void)
{
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
    read_regs(MODBUS_OFF_RF_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    live(1.25F);
    tick += 30000U;
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
    read_regs(MODBUS_OFF_RF_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
}

static void check_invalid_current_does_not_turn_fresh_rf_link_offline(void)
{
    live(-1.0F);
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
    read_regs(MODBUS_OFF_RF_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
}

static void check_float_word_age_at_timeout_boundary(void)
{
    live(1.25F);
    tick = 29999U - MODBUS_TIMEOUT_MS;
    tick_step = 1U;
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, current());
    TEST_ASSERT_TRUE(30000U <= tick);
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
}

static void check_mh_restart_does_not_reuse_dummy_or_old_rf_value(void)
{
    live(1.25F);
    rf_hub_restarted();
    read_regs(MODBUS_OFF_ANLIK_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
}


static void check_energy_and_quality_use_the_same_real_live_sample(void)
{
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U,
        .uptime_sec = 10U, .current_amps = 1.25F, .flags = 1U};

    read_regs(MODBUS_RF_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE, 21U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, word(20U));
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    read_regs(MODBUS_OFF_ENERJI_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(MODBUS_RF_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE, 1U);
    TEST_ASSERT_EQUAL_UINT16(7U, word(0U));
    tick += 30000U;
    read_regs(MODBUS_OFF_ENERJI_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(MODBUS_RF_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    live(-1.0F);
    read_regs(MODBUS_RF_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE, 1U);
    TEST_ASSERT_EQUAL_UINT16(6U, word(0U));
}

static void check_load_indicator_uses_mh_flag(void)
{
    rf_scp_live_t sample = {.source = 5U, .seq = 1U,
        .uptime_sec = 10U, .current_amps = -1.0F, .flags = 2U};

    read_regs(MODBUS_OFF_YUK_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    read_regs(MODBUS_OFF_YUK_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(MODBUS_OFF_ENERJI_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    read_regs(MODBUS_RF_STATS_ADDR_BASE - MODBUS_HOLDING_REG_BASE, 1U);
    TEST_ASSERT_EQUAL_UINT16(6U, word(0U));
    sample.current_amps = 100.0F;
    sample.flags = 1U;
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    read_regs(MODBUS_OFF_YUK_VARYOK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
}

static void check_quality_block_is_read_only_and_bounds_preserve_value(void)
{
    uint16_t value = 0xA55AU;

    TEST_ASSERT_FALSE(modbus_rf_stats_read(49499U, tick, &value));
    TEST_ASSERT_FALSE(modbus_rf_stats_read(49521U, tick, &value));
    TEST_ASSERT_EQUAL_HEX16(0xA55AU, value);
    TEST_ASSERT_FALSE(modbus_rf_stats_read(49500U, tick, NULL));
    modbus_config_get_addr_modem_reset_StopIgnore();
    /* Even a bad preexisting configuration cannot turn quality into reset. */
    TEST_ASSERT_EQUAL_INT(MODBUS_REG_ERR_ADDRESS,
                          fc06_write_callback(49500U, 1U));
}

#endif

/*** end of file ***/
