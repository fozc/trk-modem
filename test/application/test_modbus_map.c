/*
 * test_modbus_map.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Feeder block semantics of the fixed register map (modbus_map.h):
 * fault-record mirroring, fider-to-line resolution, definedness quality
 * and block bounds, exercised through modbus_line_data_read().
 */

#include "unity.h"
#include "rf.h"
#include "modbus_map.h"
#include "mock_rf_inventory.h"
#include "mock_fault_log.h"
#include "mock_bsp.h"
#include "mock_breaker.h"
#include "mock_nvram.h"
#include "cp56time2a.h"
#include <string.h>
#include <math.h>

TEST_SOURCE_FILE("rf.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return 1000U;   /* frozen now: freshness deltas stay zero */
}

/* Feeder-block reader compiled into this TU; the RF store accessors that
 * normally live in rf_config.c are provided locally so no real NVRAM-backed
 * store is linked. */
#include "../../Application/modbus_line_data.c"

static rf_feeder_t feeder_slots[MAX_POWER_LINE_COUNT];
static uint16_t value;

const rf_feeder_t *rf_store_get(feeder_id_t line)
{
    return (MAX_POWER_LINE_COUNT > (uint32_t)line)
               ? &feeder_slots[(uint32_t)line] : NULL;
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

static bool no_binding(uint8_t source, rf_inventory_entry_t *out, int count)
{
    (void)source; (void)out; (void)count;
    return false;
}

static bool fake_read_nth(uint8_t feeder, uint8_t phase, fault_log_type_t type,
                          uint8_t n, fault_log_t *log, int call_count)
{
    (void)call_count;
    if ((0U == feeder) && (0U == phase) &&
        (FAULT_LOG_TYPE_TEMPORARY == type) && (0U == n))
    {
        /* Stored current is x10: 125 -> 12.5 A. */
        log->fault_current = 125.0f;
        log->fault_duration_ms = 1500U;
        log->info.power_status = 1U;
        log->info.nominal_current_status = 0U;
        log->tm = cp56time2a_make(0U, 0U, 0U, 1U, 0U, 1U, 26U);
        return true;
    }
    return false;
}

static bool binding(uint8_t source, rf_inventory_entry_t *out, int count)
{
    (void)count;
    if (5U != source)
    {
        return false;
    }
    *out = (rf_inventory_entry_t){.feeder = 1U, .phase = 1U, .zone = 1U};
    (void)memcpy(out->eui64, feeder_slots[0].r_eui64, 8U);
    return true;
}

static void live_from_source5(float amps)
{
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U, .uptime_sec = 10U,
                                  .current_amps = amps};
    TEST_ASSERT_TRUE(rf_handle_live(&sample, 1000U, NULL));
}

static bool read_reg(uint16_t logical)
{
    return modbus_line_data_read(logical, 1000U, &value);
}

void setUp(void)
{
    memset(feeder_slots, 0, sizeof(feeder_slots));
    value = 0U;
    rf_init();
    rf_inventory_get_binding_StubWithCallback(no_binding);
    fault_log_get_temp_count_IgnoreAndReturn(0U);
    fault_log_get_perm_count_IgnoreAndReturn(0U);
    fault_log_read_nth_IgnoreAndReturn(false);
}
void tearDown(void)
{
}

void test_addresses_outside_the_feeder_space_are_rejected(void)
{
    TEST_ASSERT_FALSE(read_reg(39999U));
    TEST_ASSERT_FALSE(read_reg(44000U));
    TEST_ASSERT_FALSE(read_reg(48999U));
    TEST_ASSERT_FALSE(modbus_line_data_read(40000U, 1000U, NULL));
}

void test_undefined_fider_reports_zero_quality_and_zero_values(void)
{
    /* No line is provisioned for Fider 1: whole block reads zero. */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 4U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 5U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 6U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    /* Fider 2 block behaves the same even though Fider 1 exists. */
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + MODBUS_MAP_LINE_STRIDE));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

void test_defined_fider_without_live_reports_defined_only_and_zeros(void)
{
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;

    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE));
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_FD_Q_DEFINED, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 4U));
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_PH_Q_DEFINED, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 5U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 15U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

void test_fider_two_resolves_the_line_provisioned_with_id_two(void)
{
    feeder_slots[3].in_use = true;
    feeder_slots[3].config.fider_id = 2U;

    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + MODBUS_MAP_LINE_STRIDE));
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_FD_Q_DEFINED, value);
    /* Fider 1 stays undefined. */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

void test_live_sample_publishes_values_and_full_quality(void)
{
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    feeder_slots[0].r_eui64[0] = 0x11U;
    rf_inventory_get_binding_StubWithCallback(binding);
    live_from_source5(1.25F);

    /* Fider and phase quality: defined + live + fresh + all values valid.
     * The harness feeds no sample clock, so the time bit stays clear. */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE));
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_FD_Q_DEFINED |
                             MODBUS_MAP_FD_Q_ANY_LIVE, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 4U));
    TEST_ASSERT_EQUAL_UINT16(0x3FU, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 5U));
    TEST_ASSERT_EQUAL_UINT16(0x3FA0U, value);   /* 1.25f high word */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 6U));
    TEST_ASSERT_EQUAL_UINT16(0x0000U, value);   /* 1.25f low word */
}

void test_valid_sample_clock_sets_time_bit_and_unix_words(void)
{
    const cp56time2a_t valid = cp56time2a_make(0U, 0U, 0U, 1U, 0U, 1U, 26U);
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U, .uptime_sec = 10U,
                                  .current_amps = 1.25F};
    const uint32_t expected_unix = 1767225600U;  /* 2026-01-01 00:00:00 */

    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    feeder_slots[0].r_eui64[0] = 0x11U;
    rf_inventory_get_binding_StubWithCallback(binding);
    TEST_ASSERT_TRUE(rf_handle_live(&sample, 1000U, &valid));

    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 4U));
    TEST_ASSERT_EQUAL_UINT16(0x7FU, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 7U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(expected_unix >> 16), value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 8U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(expected_unix & 0xFFFFU), value);
}

void test_trip_failure_alarm_publishes_without_any_live_sample(void)
{
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    feeder_slots[0].r_eui64[0] = 0x11U;
    rf_inventory_get_binding_StubWithCallback(binding);

    TEST_ASSERT_TRUE(rf_open_trip_failure_alarm(1U, 1U, 1U));
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 18U));
    TEST_ASSERT_EQUAL_UINT16(1U, value);
    /* No live sample ever arrived: quality carries the defined bit only. */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 4U));
    TEST_ASSERT_EQUAL_UINT16(MODBUS_MAP_PH_Q_DEFINED, value);
}

void test_fault_record_mirrors_log_fields_and_time(void)
{
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    fault_log_read_nth_StubWithCallback(fake_read_nth);

    /* Temporary region, Fider 1, L1, record 0: logical 40100..40107. */
    const uint32_t expected_unix = 1767225600U;  /* 2026-01-01 00:00:00 */

    TEST_ASSERT_TRUE(read_reg(40100U));
    TEST_ASSERT_EQUAL_UINT16(0x4148U, value);   /* 12.5f high word */
    TEST_ASSERT_TRUE(read_reg(40101U));
    TEST_ASSERT_EQUAL_UINT16(0x0000U, value);   /* 12.5f low word */
    TEST_ASSERT_TRUE(read_reg(40102U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(1500U >> 16), value);
    TEST_ASSERT_TRUE(read_reg(40103U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(1500U & 0xFFFFU), value);
    TEST_ASSERT_TRUE(read_reg(40104U));
    TEST_ASSERT_EQUAL_UINT16(1U, value);        /* power_status */
    TEST_ASSERT_TRUE(read_reg(40105U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);        /* nominal_current_status */
    TEST_ASSERT_TRUE(read_reg(40106U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(expected_unix >> 16), value);
    TEST_ASSERT_TRUE(read_reg(40107U));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(expected_unix & 0xFFFFU), value);
}

void test_fault_slot_without_record_reads_zero(void)
{
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;

    /* Record 1 of the same region: fake_read_nth only serves record 0. */
    fault_log_read_nth_StubWithCallback(fake_read_nth);
    for (uint16_t reg = 40108U; reg < 40116U; reg++)
    {
        TEST_ASSERT_TRUE(read_reg(reg));
        TEST_ASSERT_EQUAL_UINT16(0U, value);
    }
}

void test_undefined_fider_fault_region_reads_zero_without_log_access(void)
{
    /* No provisioning: counts and records are zero, fault log untouched. */
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 76U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
    TEST_ASSERT_TRUE(read_reg(MODBUS_MAP_LINE_BASE + 100U));
    TEST_ASSERT_EQUAL_UINT16(0U, value);
}

void test_every_phase_timestamp_preserves_calendar_boundaries(void)
{
    static const struct
    {
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint32_t unix_seconds;
    } dates[] = {
        {0U, 1U, 1U, 946771199U},
        {28U, 2U, 29U, 1835481599U},
        {99U, 12U, 31U, 4102444799U}
    };
    static const uint16_t time_offsets[] = {
        MODBUS_MAP_PHASE_OFF(anlik_zaman),
        MODBUS_MAP_PHASE_OFF(enerji_zaman),
        MODBUS_MAP_PHASE_OFF(yuk_zaman),
        MODBUS_MAP_PHASE_OFF(rf_zaman),
        MODBUS_MAP_PHASE_OFF(trip_zaman)
    };
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    feeder_slots[0].r_eui64[0] = 0x11U;
    rf_inventory_get_binding_StubWithCallback(binding);

    for (size_t index = 0U; index < (sizeof(dates) / sizeof(dates[0])); index++)
    {
        const cp56time2a_t timestamp = cp56time2a_make(59999U, 59U, 23U,
            dates[index].day, 0U, dates[index].month, dates[index].year);
        const rf_scp_live_t sample = {
            .source = 5U, .seq = 1U, .uptime_sec = 10U,
            .current_amps = 1.25F
        };
        rf_init();
        TEST_ASSERT_TRUE(rf_handle_live(&sample, 1000U, &timestamp));
        const uint16_t phase_base = MODBUS_MAP_LINE_BASE +
                                    MODBUS_MAP_FEEDER_OFF(faz);
        TEST_ASSERT_TRUE(read_reg(phase_base));
        TEST_ASSERT_EQUAL_UINT16(0x7FU, value);
        for (size_t field = 0U; field < (sizeof(time_offsets) /
                                        sizeof(time_offsets[0])); field++)
        {
            const uint16_t address = phase_base + time_offsets[field];
            TEST_ASSERT_TRUE(read_reg(address));
            TEST_ASSERT_EQUAL_UINT16(dates[index].unix_seconds >> 16U, value);
            TEST_ASSERT_TRUE(read_reg(address + 1U));
            TEST_ASSERT_EQUAL_UINT16(dates[index].unix_seconds & 0xFFFFU, value);
        }
    }
}

void test_invalid_live_timestamp_keeps_current_and_clears_all_time_words(void)
{
    static const cp56time2a_t invalid[] = {
        {.iv_bit = 1U, .day = 1U, .month = 1U, .year = 26U},
        {.day = 30U, .month = 2U, .year = 28U},
        {.day = 1U, .month = 1U, .year = 100U}
    };
    static const uint16_t time_offsets[] = {3U, 6U, 9U, 12U, 15U};
    feeder_slots[0].in_use = true;
    feeder_slots[0].config.fider_id = 1U;
    feeder_slots[0].r_eui64[0] = 0x11U;
    rf_inventory_get_binding_StubWithCallback(binding);
    for (size_t index = 0U; index < (sizeof(invalid) / sizeof(invalid[0]));
         index++)
    {
        const rf_scp_live_t sample = {
            .source = 5U, .seq = 1U, .uptime_sec = 10U,
            .current_amps = 1.25F
        };
        rf_init();
        TEST_ASSERT_TRUE(rf_handle_live(&sample, 1000U, &invalid[index]));
        TEST_ASSERT_TRUE(read_reg(40004U));
        TEST_ASSERT_EQUAL_UINT16(0x3FU, value);
        TEST_ASSERT_TRUE(read_reg(40005U));
        TEST_ASSERT_EQUAL_UINT16(0x3FA0U, value);
        for (size_t field = 0U; field < (sizeof(time_offsets) /
                                        sizeof(time_offsets[0])); field++)
        {
            TEST_ASSERT_TRUE(read_reg(40004U + time_offsets[field]));
            TEST_ASSERT_EQUAL_UINT16(0U, value);
            TEST_ASSERT_TRUE(read_reg(40005U + time_offsets[field]));
            TEST_ASSERT_EQUAL_UINT16(0U, value);
        }
    }
}

/*** end of file ***/
