/*
 * modbus_rf_snapshot_fixture.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Shared real-RF / FC03 fixture for the fixed feeder blocks (modbus_map.h).
 * The including test must also include mock_fault_log.h (the feeder block
 * mirrors the fault log through its API).
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
#include "../../Application/modbus_line_data.c"

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

/*
 * Feeder-block register offsets used by the checks below (PDU addresses,
 * logical - 40000). Derived from modbus_map.h layouts; keeping them as named
 * constants makes the intent of each check readable.
 */
#define FD1_KALITE       0U
#define FD1_PH1_KALITE   4U
#define FD1_PH1_AKIM     5U
#define FD1_PH1_ENERJI   9U
#define FD1_PH1_YUK      12U
#define FD1_PH1_RF       15U
#define FD2_KALITE       1000U

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
    rf_init();
    rf_inventory_get_binding_StubWithCallback(binding);
    breaker_get_feeder_data_StubWithCallback(get_legacy);
    fault_log_get_temp_count_IgnoreAndReturn(0U);
    fault_log_get_perm_count_IgnoreAndReturn(0U);
    fault_log_read_nth_IgnoreAndReturn(false);
    modbus_system_stats_read_IgnoreAndReturn(false);
    modbus_power_stats_read_IgnoreAndReturn(false);
    modbus_bms_stats_read_IgnoreAndReturn(false);
    modbus_gsm_stats_read_IgnoreAndReturn(false);
    libmodbusrtu_slave_init(&receiver, 1U, get_tick, capture);
    libmodbusrtu_register_read_callback(&receiver, fc03_read_callback);
}

/*
 * Expected quality words for the sample shapes used below (modbus_map.h
 * bits): fresh valid sample -> all six bits (0x3F); fresh sample with an
 * unusable RMS -> 0x37; stale sample -> freshness bit clear (0x3B);
 * stalled AY uptime -> only defined/live/fresh (0x07); defined feeder that
 * has never delivered a sample -> defined bit only (0x01).
 */
static void check_live_current_link_and_quality(void)
{
    read_regs(FD1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_FD_Q_DEFINED, word(0U));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_PH_Q_DEFINED, word(0U));
    live(1.25F);
    read_regs(FD1_PH1_AKIM, 2U);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, current());
    read_regs(FD1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_FD_Q_DEFINED | MODBUS_MAP_FD_Q_ANY_LIVE,
                             word(0U));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0x3FU, word(0U));
}

static void check_missing_and_stale(void)
{
    /* Defined feeder, no sample ever: values are zero, never "real" data. */
    read_regs(FD1_PH1_AKIM, 2U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, word(1U));
    read_regs(FD1_PH1_RF, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));

    live(1.25F);
    tick += 30000U;   /* sample ages out: value kept, freshness drops */
    read_regs(FD1_PH1_AKIM, 2U);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, current());
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0x3BU, word(0U));
    read_regs(FD1_PH1_RF, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
}

static void check_invalid_current_keeps_link_online(void)
{
    const rf_scp_live_t invalid = {.source = 5U, .seq = 2U,
        .uptime_sec = 20U, .current_amps = -1.0F};
    TEST_ASSERT_TRUE(rf_handle_live(&invalid, tick, NULL));
    read_regs(FD1_PH1_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0x37U, word(0U));
    read_regs(FD1_PH1_RF, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
}

static void check_freshness_flips_at_timeout_boundary(void)
{
    live(1.25F);
    tick = 29999U - MODBUS_TIMEOUT_MS;
    tick_step = 1U;
    /* One window covers kalite + both current words on the same snapshot. */
    read_regs(FD1_PH1_KALITE, 3U);
    TEST_ASSERT_TRUE(0U != (word(0U) & MODBUS_MAP_PH_Q_FRESH));
    const uint32_t fresh_bits = ((uint32_t)word(1U) << 16U) | word(2U);
    TEST_ASSERT_EQUAL_HEX32(0x3FA00000UL, fresh_bits);   /* 1.25f */

    /* Each further poll burns ticks; at 30 s the value stays but the
     * freshness bit drops. */
    read_regs(FD1_PH1_KALITE, 3U);
    TEST_ASSERT_TRUE(30000U <= tick);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U) & MODBUS_MAP_PH_Q_FRESH);
    const uint32_t aged_bits = ((uint32_t)word(1U) << 16U) | word(2U);
    TEST_ASSERT_EQUAL_HEX32(0x3FA00000UL, aged_bits);
}

static void check_mh_restart_drops_freshness(void)
{
    live(1.25F);
    rf_hub_restarted();
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U) & MODBUS_MAP_PH_Q_FRESH);
    read_regs(FD1_PH1_RF, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
}

static void check_energy_and_load_flags_follow_live_sample(void)
{
    rf_scp_live_t sample = {.source = 5U, .seq = 1U,
        .uptime_sec = 10U, .current_amps = -1.0F, .flags = 1U};

    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    read_regs(FD1_PH1_ENERJI, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(FD1_PH1_YUK, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));

    sample.current_amps = 100.0F;
    sample.flags = 3U;
    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, NULL));
    read_regs(FD1_PH1_ENERJI, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(FD1_PH1_YUK, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
}

static void check_undefined_feeder_reports_zero_quality(void)
{
    read_regs(FD2_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    read_regs(FD2_KALITE + 4U, 1U);   /* Fider 2 L1 phase quality */
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    read_regs(FD2_KALITE + 5U, 2U);   /* current words stay zero */
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, word(1U));
}

static void check_reserved_gaps_read_zero_and_write_rejected(void)
{
    read_regs(1U, 3U);                /* feeder header padding */
    for (size_t index = 0U; 3U > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT16(0U, word(index));
    }
    read_regs(82U, 10U);              /* between counts and fault region */
    for (size_t index = 0U; 10U > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT16(0U, word(index));
    }
    read_regs(946U, 10U);             /* between temporary and permanent */
    for (size_t index = 0U; 10U > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT16(0U, word(index));
    }

    /* Every feeder register is read-only for FC06. */
    TEST_ASSERT_EQUAL_INT(MODBUS_REG_ERR_ADDRESS,
                          fc06_write_callback(40005U, 1U));
    TEST_ASSERT_EQUAL_INT(MODBUS_REG_ERR_VALUE,
                          fc06_write_callback(50001U, 2U));
    uint16_t value = 0xFFFFU;
    TEST_ASSERT_EQUAL_INT(MODBUS_REG_OK, fc03_read_callback(50000U, &value));
    TEST_ASSERT_EQUAL_UINT16(0U, value);   /* no power summary: no warning */
    TEST_ASSERT_EQUAL_INT(MODBUS_REG_OK, fc03_read_callback(50001U, &value));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

static void check_stalled_uptime_clears_validity_but_keeps_link(void)
{
    live(1.25F);
    tick = 5000U;
    live(1.25F);
    read_regs(FD1_PH1_AKIM, 2U);
    TEST_ASSERT_TRUE(isnan(current()));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_PH_Q_DEFINED |
                             MODBUS_MAP_PH_Q_HAS_LIVE |
                             MODBUS_MAP_PH_Q_FRESH, word(0U));
    read_regs(FD1_PH1_RF, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
}

static void check_valid_sample_time_sets_time_bit_and_unix(void)
{
    /* 2026-01-01 00:00:00 = unix 1767225600. */
    const cp56time2a_t valid =
        cp56time2a_make(0U, 0U, 0U, 1U, 0U, 1U, 26U);
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U, .uptime_sec = 10U,
                                  .current_amps = 1.25F};

    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, &valid));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0x7FU, word(0U));
    read_regs(FD1_PH1_KALITE + 3U, 2U);   /* anlik_zaman */
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(1767225600U >> 16), word(0U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(1767225600U & 0xFFFFU), word(1U));

    /* Same measurement accepted without a clock: time reads 0, bit 6
     * clears (uptime advances so the sample is not marked stalled). */
    tick += 1000U;
    const rf_scp_live_t no_clock = {.source = 5U, .seq = 2U,
                                    .uptime_sec = 11U,
                                    .current_amps = 1.25F};
    TEST_ASSERT_TRUE(rf_handle_live(&no_clock, tick, NULL));
    read_regs(FD1_PH1_KALITE + 3U, 2U);
    TEST_ASSERT_EQUAL_UINT16(0U, word(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, word(1U));
    read_regs(FD1_PH1_KALITE, 1U);
    TEST_ASSERT_EQUAL_UINT16(0x3FU, word(0U));
}

static void check_trip_failure_publishes_without_live(void)
{
    /* BQ-01/BQ-16: the alarm event opens the flag with no LIVE sample. */
    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    read_regs(FD1_PH1_KALITE + 14U, 1U);   /* trip_failed */
    TEST_ASSERT_EQUAL_UINT16(1U, word(0U));
    read_regs(FD1_PH1_KALITE, 1U);         /* quality: defined only */
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_PH_Q_DEFINED, word(0U));
}

#endif

/*** end of file ***/
