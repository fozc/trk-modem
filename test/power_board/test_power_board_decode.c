/*
 * test_power_board_decode.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies PowerBoard wire integrity and signed field decoding.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "power_board_decode.h"

static void put_u16(uint8_t *buffer, uint8_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)(value >> 8);
    buffer[offset + 1U] = (uint8_t)value;
}

static void put_u32(uint8_t *buffer, uint8_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t)(value >> 24);
    buffer[offset + 1U] = (uint8_t)(value >> 16);
    buffer[offset + 2U] = (uint8_t)(value >> 8);
    buffer[offset + 3U] = (uint8_t)value;
}

static void seal_telemetry(uint8_t *frame)
{
    frame[95U] = power_board_xsum(frame, 95U);
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_power_board_xsum_uses_protocol_salt(void)
{
    static const uint8_t bytes[] = {0x01U, 0x02U};

    TEST_ASSERT_EQUAL_HEX8(0x59U,
                           power_board_xsum(bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_HEX8(POWER_BOARD_XSUM_SALT,
                           power_board_xsum(NULL, 0U));
}

void test_power_board_telemetry_requires_version_and_integrity(void)
{
    uint8_t frame[POWER_BOARD_TLM_SIZE] = {0U};
    power_board_telemetry_t output;

    frame[0x1EU] = POWER_BOARD_PROT_VER;
    seal_telemetry(frame);
    TEST_ASSERT_TRUE(power_board_decode_telemetry(frame, &output));

    frame[0x1EU] = (uint8_t)(POWER_BOARD_PROT_VER - 1U);
    seal_telemetry(frame);
    TEST_ASSERT_FALSE(power_board_decode_telemetry(frame, &output));
    TEST_ASSERT_EQUAL_HEX8(POWER_BOARD_PROT_VER - 1U, output.prot_ver);

    frame[0x1EU] = POWER_BOARD_PROT_VER;
    seal_telemetry(frame);
    frame[0x10U] ^= 0x01U;
    TEST_ASSERT_FALSE(power_board_decode_telemetry(frame, &output));
}

void test_power_board_telemetry_decodes_signed_values(void)
{
    uint8_t frame[POWER_BOARD_TLM_SIZE] = {0U};
    power_board_telemetry_t output;

    frame[0x1EU] = POWER_BOARD_PROT_VER;
    put_u16(frame, 0x1AU, (uint16_t)(int16_t)-1000);
    put_u16(frame, 0x56U, (uint16_t)(int16_t)-1234);
    put_u16(frame, 0x58U, (uint16_t)(int16_t)-250);
    seal_telemetry(frame);

    TEST_ASSERT_TRUE(power_board_decode_telemetry(frame, &output));
    TEST_ASSERT_EQUAL_INT16(-1000, output.soc_x10);
    TEST_ASSERT_EQUAL_INT16(-1234, output.ibat_ma);
    TEST_ASSERT_EQUAL_INT16(-250, output.ibus_ma);
}

void test_power_board_power_block_decodes_big_endian_signed_values(void)
{
    uint8_t frame[POWER_BOARD_PWR_LEN] = {0U};
    power_board_power_t output;

    put_u32(frame, 0U, 5000U);
    put_u32(frame, 8U, (uint32_t)(int32_t)-680);
    put_u32(frame, 12U, 700U);
    frame[POWER_BOARD_PWR_LEN - 1U] = power_board_xsum(
        frame, (uint8_t)(POWER_BOARD_PWR_LEN - 1U));

    TEST_ASSERT_TRUE(power_board_decode_power(frame, &output));
    TEST_ASSERT_EQUAL_INT32(5000, output.ppv_mw);
    TEST_ASSERT_EQUAL_INT32(-680, output.psys_mw);
    TEST_ASSERT_EQUAL_INT32(700, output.pbat_mw);
}

void test_power_board_lastgasp_requires_marker_and_integrity(void)
{
    uint8_t frame[POWER_BOARD_LG_LEN] = {0U};
    power_board_lastgasp_t output;

    frame[0U] = POWER_BOARD_LG_MARKER;
    frame[1U] = 2U;
    put_u16(frame, 2U, 870U);
    put_u16(frame, 22U, 11800U);
    frame[25U] = 42U;
    frame[POWER_BOARD_LG_LEN - 1U] = power_board_xsum(
        frame, (uint8_t)(POWER_BOARD_LG_LEN - 1U));

    TEST_ASSERT_TRUE(power_board_decode_lastgasp(frame, &output));
    TEST_ASSERT_EQUAL_UINT8(2U, output.reason);
    TEST_ASSERT_EQUAL_UINT16(870U, output.soh_x10);
    TEST_ASSERT_EQUAL_UINT16(11800U, output.vbat_mv);
    TEST_ASSERT_EQUAL_UINT8(42U, output.soc_pct);

    frame[0U] = 0U;
    frame[POWER_BOARD_LG_LEN - 1U] = power_board_xsum(
        frame, (uint8_t)(POWER_BOARD_LG_LEN - 1U));
    TEST_ASSERT_FALSE(power_board_decode_lastgasp(frame, &output));
}

void test_power_board_decoders_reject_null_arguments(void)
{
    uint8_t telemetry[POWER_BOARD_TLM_SIZE] = {0U};
    power_board_telemetry_t output;

    TEST_ASSERT_FALSE(power_board_decode_telemetry(NULL, &output));
    TEST_ASSERT_FALSE(power_board_decode_telemetry(telemetry, NULL));
    TEST_ASSERT_FALSE(power_board_decode_power(NULL, NULL));
    TEST_ASSERT_FALSE(power_board_decode_lastgasp(NULL, NULL));
}

void test_power_board_telemetry_decodes_multibyte_boundaries(void)
{
    uint8_t frame[POWER_BOARD_TLM_SIZE] = {0U};
    power_board_telemetry_t output;

    frame[0x1EU] = POWER_BOARD_PROT_VER;
    put_u16(frame, 0x04U, 0xFFFFU);
    put_u32(frame, 0x08U, 0x01234567U);
    put_u32(frame, 0x0CU, 0x89ABCDEFU);
    put_u16(frame, 0x10U, 0x1234U);
    put_u16(frame, 0x1AU, 0x7FFFU);
    frame[0x22U] = 0x80U;
    put_u16(frame, 0x24U, 0x8000U);
    put_u32(frame, 0x33U, 0x80000000U);
    put_u32(frame, 0x37U, 0x7FFFFFFFU);
    put_u32(frame, 0x3BU, 0xFFFFFFFFU);
    put_u16(frame, 0x50U, 0xFEDCU);
    frame[0x5EU] = 0xFFU;
    seal_telemetry(frame);

    TEST_ASSERT_TRUE(power_board_decode_telemetry(frame, &output));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, output.soh_x10);
    TEST_ASSERT_EQUAL_HEX32(0x01234567U, output.equiv_hours);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFU, output.gross_mah);
    TEST_ASSERT_EQUAL_UINT16(0x1234U, output.vbat_mv);
    TEST_ASSERT_EQUAL_INT16(32767, output.soc_x10);
    TEST_ASSERT_EQUAL_INT8(-128, output.board_temp_c);
    TEST_ASSERT_EQUAL_INT16(-32768, output.batt_temp_x10);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, output.delta_uwh);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, output.total_mwh);
    TEST_ASSERT_EQUAL_INT32(-1, output.total_mah);
    TEST_ASSERT_EQUAL_UINT16(0xFEDCU, output.bq_vsys_mv);
    TEST_ASSERT_EQUAL_INT8(-1, output.bq_tdie_c);
}

void test_power_board_invalid_telemetry_still_decodes_fields(void)
{
    uint8_t frame[POWER_BOARD_TLM_SIZE] = {0U};
    power_board_telemetry_t output;

    frame[0x1EU] = POWER_BOARD_PROT_VER;
    put_u16(frame, 0x10U, 12000U);
    seal_telemetry(frame);
    frame[0x00U] ^= 0x01U;

    TEST_ASSERT_FALSE(power_board_decode_telemetry(frame, &output));
    TEST_ASSERT_FALSE(output.valid);
    TEST_ASSERT_EQUAL_UINT16(12000U, output.vbat_mv);
}

void test_power_board_power_block_rejects_corruption_but_decodes_fields(void)
{
    uint8_t frame[POWER_BOARD_PWR_LEN] = {0U};
    power_board_power_t output;

    put_u32(frame, 0U, (uint32_t)(int32_t)INT32_MIN);
    put_u32(frame, 4U, (uint32_t)(int32_t)INT32_MAX);
    put_u32(frame, 8U, (uint32_t)(int32_t)-1);
    put_u32(frame, 12U, 0U);
    put_u32(frame, 16U, 0x12345678U);
    frame[POWER_BOARD_PWR_LEN - 1U] = power_board_xsum(
        frame, (uint8_t)(POWER_BOARD_PWR_LEN - 1U));
    frame[POWER_BOARD_PWR_LEN - 1U] ^= 0x01U;

    TEST_ASSERT_FALSE(power_board_decode_power(frame, &output));
    TEST_ASSERT_FALSE(output.valid);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, output.ppv_mw);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, output.pdc_mw);
    TEST_ASSERT_EQUAL_INT32(-1, output.psys_mw);
    TEST_ASSERT_EQUAL_INT32(0, output.pbat_mw);
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, (uint32_t)output.pin_mw);
}

void test_power_board_lastgasp_decodes_all_multibyte_fields(void)
{
    uint8_t frame[POWER_BOARD_LG_LEN] = {0U};
    power_board_lastgasp_t output;

    frame[0U] = POWER_BOARD_LG_MARKER;
    frame[1U] = 0xA5U;
    put_u16(frame, 2U, 0x1234U);
    put_u16(frame, 4U, 0x5678U);
    put_u32(frame, 6U, 0x01234567U);
    put_u32(frame, 10U, 0x89ABCDEFU);
    put_u32(frame, 14U, 0xFEDCBA98U);
    put_u32(frame, 18U, 0x76543210U);
    put_u16(frame, 22U, 0xBEEFU);
    frame[24U] = 50U;
    frame[25U] = 100U;
    frame[POWER_BOARD_LG_LEN - 1U] = power_board_xsum(
        frame, (uint8_t)(POWER_BOARD_LG_LEN - 1U));

    TEST_ASSERT_TRUE(power_board_decode_lastgasp(frame, &output));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, output.reason);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, output.soh_x10);
    TEST_ASSERT_EQUAL_HEX16(0x5678U, output.efc);
    TEST_ASSERT_EQUAL_HEX32(0x01234567U, output.equiv_hours);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFU, output.gross_mah);
    TEST_ASSERT_EQUAL_HEX32(0xFEDCBA98U, output.total_mwh);
    TEST_ASSERT_EQUAL_HEX32(0x76543210U, output.total_mah);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFU, output.vbat_mv);
    TEST_ASSERT_EQUAL_UINT8(50U, output.cap_ah);
    TEST_ASSERT_EQUAL_UINT8(100U, output.soc_pct);

    frame[POWER_BOARD_LG_LEN - 1U] ^= 0x01U;
    TEST_ASSERT_FALSE(power_board_decode_lastgasp(frame, &output));
    TEST_ASSERT_FALSE(output.valid);
}

void test_power_board_decoders_reject_each_null_argument(void)
{
    uint8_t power[POWER_BOARD_PWR_LEN] = {0U};
    uint8_t lastgasp[POWER_BOARD_LG_LEN] = {0U};
    power_board_power_t power_output;
    power_board_lastgasp_t lastgasp_output;

    TEST_ASSERT_FALSE(power_board_decode_power(NULL, &power_output));
    TEST_ASSERT_FALSE(power_board_decode_power(power, NULL));
    TEST_ASSERT_FALSE(power_board_decode_lastgasp(NULL,
                                                  &lastgasp_output));
    TEST_ASSERT_FALSE(power_board_decode_lastgasp(lastgasp, NULL));
}

/*** end of file ***/
