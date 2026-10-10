/*
 * test_iec104_config_edges.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify configuration ownership, failure paths and fault IOA boundaries.
 */

#include "unity.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "mock_nvram.h"
#include <string.h>

TEST_SOURCE_FILE("iec104_util.c")

static iec104_config_t config;
static breaker_t breaker;

void setUp(void)
{
    (void)memset(&config, 0, sizeof(config));
    (void)memset(&breaker, 0, sizeof(breaker));
    nvram_get_iec104_config_rw_IgnoreAndReturn(&config);
    nvram_get_breaker_rw_IgnoreAndReturn(&breaker);
}

void tearDown(void)
{
}

void test_config_null_preserves_owned_copy_and_sync_returns_failure(void)
{
    const iec104_config_t input =
    {
        .scada_ip_address = 0xC0A80101U, .scada_port = 2404U,
        .common_address = 7U, .k_max = 12U, .w_max = 8U
    };
    iec104_config_set(&input);
    iec104_config_set(NULL);
    TEST_ASSERT_EQUAL_PTR(&config, iec104_config_get());
    TEST_ASSERT_EQUAL_MEMORY(&input, &config, sizeof(input));
    nvram_sync_ExpectAndReturn(false, -7);
    TEST_ASSERT_EQUAL_INT(-7, iec104_config_sync());
    nvram_sync_ExpectAndReturn(false, 0);
    TEST_ASSERT_EQUAL_INT(0, iec104_config_sync());
}

void test_config_scalar_fields_preserve_zero_and_full_width_values(void)
{
    for (uint8_t index = 0U; 2U > index; index++)
    {
        const uint32_t word = (0U == index) ? 0U : UINT32_MAX;
        const uint16_t half = (0U == index) ? 0U : UINT16_MAX;
        const uint8_t byte = (0U == index) ? 0U : UINT8_MAX;
        const ioa_3byte_t ioa = iec104_make_ioa_3byte(word);

        iec104_config_set_scada_ip(word);
        iec104_config_set_scada_port(half);
        iec104_config_set_periodical_send_interval(word);
        iec104_config_set_t0_max(half);
        iec104_config_set_t1_max(half);
        iec104_config_set_t2_max(half);
        iec104_config_set_t3_max(half);
        iec104_config_set_k_max(byte);
        iec104_config_set_w_max(byte);
        iec104_config_set_sbo_execute_timeout(half);
        iec104_config_set_is_sbo_active(byte);
        iec104_config_set_originator_address(byte);
        iec104_config_set_common_address(half);
        iec104_config_set_ioa_aku_uyarisi(ioa);
        iec104_config_set_ioa_modem_reset(ioa);

        TEST_ASSERT_EQUAL_UINT32(word, iec104_config_get_scada_ip());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_scada_port());
        TEST_ASSERT_EQUAL_UINT32(word,
            iec104_config_get_periodical_send_interval());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_t0_max());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_t1_max());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_t2_max());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_t3_max());
        TEST_ASSERT_EQUAL_UINT8(byte, iec104_config_get_k_max());
        TEST_ASSERT_EQUAL_UINT8(byte, iec104_config_get_w_max());
        TEST_ASSERT_EQUAL_UINT16(half,
            iec104_config_get_sbo_execute_timeout());
        TEST_ASSERT_EQUAL_UINT8(byte, iec104_config_get_is_sbo_active());
        TEST_ASSERT_EQUAL_UINT8(byte, iec104_config_get_originator_address());
        TEST_ASSERT_EQUAL_UINT16(half, iec104_config_get_common_address());
        TEST_ASSERT_TRUE(iec104_ioa_3byte_equals(ioa,
            iec104_config_get_ioa_aku_uyarisi()));
        TEST_ASSERT_TRUE(iec104_ioa_3byte_equals(ioa,
            iec104_config_get_ioa_modem_reset()));
    }
}

void test_line_rejects_invalid_index_null_and_busy_without_mutation(void)
{
    const iec104_line_config_t input = {.in_use = 1U};
    const breaker_t before = breaker;

    TEST_ASSERT_FALSE(iec104_set_line_config(MAX_POWER_LINE_COUNT, &input));
    TEST_ASSERT_FALSE(iec104_set_line_config(UINT32_MAX, &input));
    TEST_ASSERT_FALSE(iec104_set_line_config(0U, NULL));
    TEST_ASSERT_NULL(iec104_get_line_config(MAX_POWER_LINE_COUNT));
    TEST_ASSERT_FALSE(iec104_is_line_in_use(UINT32_MAX));
    nvram_is_busy_ExpectAndReturn(true);
    TEST_ASSERT_FALSE(iec104_set_line_config(0U, &input));
    TEST_ASSERT_EQUAL_MEMORY(&before, &breaker, sizeof(before));
}

void test_all_line_copies_are_independent_and_zero_disables_use(void)
{
    for (uint32_t feeder = 0U; MAX_POWER_LINE_COUNT > feeder; feeder++)
    {
        iec104_line_config_t input = {.in_use = 2U};
        input.temporary_fault = iec104_make_ioa_3byte(100000U + feeder);
        nvram_is_busy_ExpectAndReturn(false);
        TEST_ASSERT_TRUE(iec104_set_line_config(feeder, &input));
        TEST_ASSERT_EQUAL_MEMORY(&input, iec104_get_line_config(feeder),
                                 sizeof(input));
        TEST_ASSERT_TRUE(iec104_is_line_in_use(feeder));
        input.in_use = 0U;
        TEST_ASSERT_TRUE(iec104_is_line_in_use(feeder));
    }
    const iec104_line_config_t empty = {0};
    nvram_is_busy_ExpectAndReturn(false);
    TEST_ASSERT_TRUE(iec104_set_line_config(0U, &empty));
    TEST_ASSERT_FALSE(iec104_is_line_in_use(0U));
    TEST_ASSERT_TRUE(iec104_is_line_in_use(MAX_POWER_LINE_COUNT - 1U));
}

typedef ioa_3byte_t (*fault_ioa_fn_t)(uint32_t, uint8_t, uint8_t);

static const fault_ioa_fn_t fault_fields[2][4] =
{
    {iec104_get_feeder_temporary_fault_ariza_akimi_ioa,
     iec104_get_feeder_temporary_fault_ariza_suresi_ioa,
     iec104_get_feeder_temporary_fault_enerji_varyok_ioa,
     iec104_get_feeder_temporary_fault_yuk_akimi_varyok_ioa},
    {iec104_get_feeder_permanent_fault_ariza_akimi_ioa,
     iec104_get_feeder_permanent_fault_ariza_suresi_ioa,
     iec104_get_feeder_permanent_fault_enerji_varyok_ioa,
     iec104_get_feeder_permanent_fault_yuk_akimi_varyok_ioa}
};

static void check_fault_record(uint32_t feeder, uint8_t phase,
                               uint8_t record, uint8_t kind)
{
    for (uint8_t field = 0U; 4U > field; field++)
    {
        const uint32_t expected = ((0U == kind) ? 100000U : 200000U) +
            feeder * 180U + (uint32_t)phase * 60U +
            (uint32_t)record * 4U + field;
        TEST_ASSERT_EQUAL_UINT32(expected, iec104_ioa_3byte_to_uint32(
            fault_fields[kind][field](feeder, phase, record)));
    }
}

static void check_fault_phase(uint32_t feeder, uint8_t phase)
{
    TEST_ASSERT_EQUAL_UINT32(100000U + (uint32_t)phase * 60U,
        iec104_ioa_3byte_to_uint32(
            iec104_get_feeder_temporary_fault_base_ioa(feeder, phase)));
    TEST_ASSERT_EQUAL_UINT32(200000U + (uint32_t)phase * 60U,
        iec104_ioa_3byte_to_uint32(
            iec104_get_feeder_permanent_fault_base_ioa(feeder, phase)));
    for (uint8_t record = 0U; 15U > record; record++)
    {
        check_fault_record(feeder, phase, record, 0U);
        check_fault_record(feeder, phase, record, 1U);
    }
}

void test_fault_ioas_cover_every_feeder_phase_record_and_field(void)
{
    for (uint32_t feeder = 0U; MAX_POWER_LINE_COUNT > feeder; feeder++)
    {
        breaker.line[feeder].iec104.temporary_fault =
            iec104_make_ioa_3byte(100000U);
        breaker.line[feeder].iec104.permanent_fault =
            iec104_make_ioa_3byte(200000U);
        for (uint8_t phase = 0U; PHASE_MAX > phase; phase++)
        {
            check_fault_phase(feeder, phase);
        }
    }
}

void test_fault_ioas_reject_each_out_of_range_dimension(void)
{
    for (uint8_t kind = 0U; 2U > kind; kind++)
    {
        for (uint8_t field = 0U; 4U > field; field++)
        {
            const fault_ioa_fn_t get_ioa = fault_fields[kind][field];
            TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
                get_ioa(MAX_POWER_LINE_COUNT, 0U, 0U)));
            TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
                get_ioa(0U, PHASE_MAX, 0U)));
            TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
                get_ioa(0U, 0U, 15U)));
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
        iec104_get_feeder_temporary_fault_base_ioa(UINT32_MAX, 0U)));
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
        iec104_get_feeder_temporary_fault_base_ioa(0U, UINT8_MAX)));
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
        iec104_get_feeder_permanent_fault_base_ioa(UINT32_MAX, 0U)));
    TEST_ASSERT_EQUAL_UINT32(0U, iec104_ioa_3byte_to_uint32(
        iec104_get_feeder_permanent_fault_base_ioa(0U, UINT8_MAX)));
}

/*** end of file ***/
