/*
 * test_bms_full_map.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify full BMS UART response validation before telemetry is changed.
 */
#include "unity.h"
#include "bms.h"
#include <string.h>

static uint8_t frame[BMS_FULL_MAP_FRAME_LEN + 1U];
static bms_data_t data;

static void seal_frame(size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length - 2U; index++)
    {
        crc = (uint16_t)(crc ^ frame[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    frame[length - 2U] = (uint8_t)(crc & 0xFFU);
    frame[length - 1U] = (uint8_t)(crc >> 8U);
}

static void assert_rejected_unchanged(uint16_t length, bms_status_t status)
{
    const bms_data_t before = data;
    TEST_ASSERT_EQUAL_INT(status, BMS_ParseFullMapResponse(&data,
                                                          frame, length));
    TEST_ASSERT_EQUAL_MEMORY(&before, &data, sizeof(data));
}

void setUp(void)
{
    memset(frame, 0, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(BMS_OK, BMS_Init(&data));
    data.is_data_valid = true;
    data.is_soh_valid = true;
    data.soh_percent = 88.0f;
    data.cell_voltage_mv[0] = 3000U;
    frame[0] = BMS_SLAVE_RESP_ADDR;
    frame[1] = 3U;
    frame[2] = BMS_FULL_MAP_PAYLOAD_LEN;
    frame[3] = 0x0CU;
    frame[4] = 0xE4U;
    seal_frame(BMS_FULL_MAP_FRAME_LEN);
}

void tearDown(void)
{
}

void test_valid_full_map_decodes_and_preserves_separate_soh(void)
{
    TEST_ASSERT_EQUAL_INT(BMS_OK, BMS_ParseFullMapResponse(&data, frame,
                                              BMS_FULL_MAP_FRAME_LEN));
    TEST_ASSERT_EQUAL_UINT16(3300U, data.cell_voltage_mv[0]);
    TEST_ASSERT_TRUE(data.is_data_valid);
    TEST_ASSERT_TRUE(data.is_soh_valid);
    TEST_ASSERT_EQUAL_FLOAT(88.0f, data.soh_percent);
}

void test_raw_payload_cannot_be_marked_valid_without_header_and_crc(void)
{
    memmove(frame, &frame[3], BMS_FULL_MAP_PAYLOAD_LEN);
    assert_rejected_unchanged(BMS_FULL_MAP_PAYLOAD_LEN,
                              BMS_ERROR_INVALID_LEN);
}

void test_short_full_map_is_rejected_without_changing_previous_sample(void)
{
    assert_rejected_unchanged(0U, BMS_ERROR_INVALID_LEN);
    assert_rejected_unchanged(2U, BMS_ERROR_INVALID_LEN);
    assert_rejected_unchanged(BMS_FULL_MAP_FRAME_LEN - 1U,
                              BMS_ERROR_INVALID_LEN);
}

void test_extra_bytes_are_rejected_even_with_matching_crc(void)
{
    seal_frame(sizeof(frame));
    assert_rejected_unchanged(sizeof(frame), BMS_ERROR_INVALID_LEN);
}

void test_wrong_address_and_command_cannot_enter_raw_fallback(void)
{
    frame[0] = 0U;
    seal_frame(BMS_FULL_MAP_FRAME_LEN);
    assert_rejected_unchanged(BMS_FULL_MAP_FRAME_LEN,
                              BMS_ERROR_INVALID_ADDR);
    frame[0] = BMS_SLAVE_RESP_ADDR;
    frame[1] = 4U;
    seal_frame(BMS_FULL_MAP_FRAME_LEN);
    assert_rejected_unchanged(BMS_FULL_MAP_FRAME_LEN,
                              BMS_ERROR_INVALID_CMD);
}

void test_wrong_byte_count_is_rejected_even_with_matching_crc(void)
{
    frame[2] = 2U;
    seal_frame(BMS_FULL_MAP_FRAME_LEN);
    assert_rejected_unchanged(BMS_FULL_MAP_FRAME_LEN,
                              BMS_ERROR_INVALID_LEN);
}

void test_bad_crc_is_rejected_without_changing_previous_sample(void)
{
    frame[BMS_FULL_MAP_FRAME_LEN - 1U] ^= 1U;
    assert_rejected_unchanged(BMS_FULL_MAP_FRAME_LEN,
                              BMS_ERROR_CRC_MISMATCH);
}

void test_soh_crc_validation_and_decode_remain_unchanged(void)
{
    frame[2] = 2U;
    frame[3] = 0x03U;
    frame[4] = 0xE8U;
    seal_frame(BMS_SOH_FRAME_LEN);
    TEST_ASSERT_EQUAL_INT(BMS_OK,
        BMS_ParseSOHResponse(&data, frame, BMS_SOH_FRAME_LEN));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, data.soh_percent);
    const bms_data_t before = data;
    frame[6] ^= 1U;
    TEST_ASSERT_EQUAL_INT(BMS_ERROR_CRC_MISMATCH,
        BMS_ParseSOHResponse(&data, frame, BMS_SOH_FRAME_LEN));
    TEST_ASSERT_EQUAL_MEMORY(&before, &data, sizeof(data));
}

/*** end of file ***/
