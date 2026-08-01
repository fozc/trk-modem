/*
 * @file    bms.c
 * @brief   BMS Modbus RTU Frame Parser & CRC Engine Implementation
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */
#include "bms.h"

/* ========================================================================== */
/*                           PRIVATE HELPER FUNCTIONS                         */
/* ========================================================================== */

/**
 * @brief  Standard Modbus CRC-16 (Polynomial 0xA001) Calculation.
 */
static uint16_t BMS_CalculateCRC16(const uint8_t *p_data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    if (p_data == NULL) {
        return 0U;
    }

    for (uint16_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)p_data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc >>= 1U;
                crc ^= 0xA001U;
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

/**
 * @brief Helper to combine Big-Endian Bytes to 16-bit word.
 */
static inline uint16_t BMS_BytesToUint16(const uint8_t *p_bytes)
{
    return (uint16_t)(((uint16_t)p_bytes[0] << 8U) | (uint16_t)p_bytes[1]);
}

/* ========================================================================== */
/*                             PUBLIC API DEFINITIONS                         */
/* ========================================================================== */

bms_status_t BMS_Init(bms_data_t *p_bms)
{
    if (p_bms == NULL) {
        return BMS_ERROR_NULL_PTR;
    }

    for (uint8_t i = 0U; i < BMS_MAX_CELL_COUNT; i++) {
        p_bms->cell_voltage_mv[i] = 0U;
    }
    for (uint8_t i = 0U; i < BMS_MAX_TEMP_SENSORS; i++) {
        p_bms->temperatures_celsius[i] = -40;
    }

    p_bms->total_voltage_v = 0.0f;
    p_bms->current_a = 0.0f;
    p_bms->soc_percent = 0.0f;
    p_bms->soh_percent = 0.0f;
    p_bms->remaining_cap_ah = 0.0f;
    p_bms->active_cell_count = 0U;
    p_bms->is_data_valid = false;
    p_bms->is_soh_valid = false;

    return BMS_OK;
}

bms_status_t BMS_ParseFullMapResponse(bms_data_t *p_bms, const uint8_t *p_frame, uint16_t length)
{
    if ((p_bms == NULL) || (p_frame == NULL)) {
        return BMS_ERROR_NULL_PTR;
    }

    /* Support frames with or without Slave Address Header (51 03 FE) */
    uint16_t data_start_offset = 0U;
    uint16_t payload_len = 0U;

    if ((length >= BMS_FULL_MAP_FRAME_LEN) && (p_frame[0] == BMS_SLAVE_RESP_ADDR) && (p_frame[1] == 0x03U))
    {
        /* Full Frame with Header: Verify CRC */
        uint16_t calc_crc = BMS_CalculateCRC16(p_frame, length - 2U);
        uint16_t recv_crc = (uint16_t)p_frame[length - 2U] | ((uint16_t)p_frame[length - 1U] << 8U);

        if (calc_crc != recv_crc) {
            return BMS_ERROR_CRC_MISMATCH;
        }
        data_start_offset = 3U; /* Header offset: ADDR (1) + CMD (1) + LEN (1) */
        payload_len = (uint16_t)p_frame[2];
    }
    else if (length >= BMS_FULL_MAP_PAYLOAD_LEN)
    {
        /* Raw payload stripped header case */
        data_start_offset = 0U;
        payload_len = length - 2U; /* Exclude trailing CRC */
    }
    else
    {
        return BMS_ERROR_INVALID_LEN;
    }

    const uint8_t *p_regs = &p_frame[data_start_offset];

    /* --- 1. Cell Voltages (0x0000 ~ 0x002F) --- */
    for (uint8_t i = 0U; i < BMS_MAX_CELL_COUNT; i++) {
        p_bms->cell_voltage_mv[i] = BMS_BytesToUint16(&p_regs[i * 2U]);
    }

    /* --- 2. Temperatures (0x0030 ~ 0x0037) --- Offset = 40 */
    uint16_t temp_base = 0x30U * 2U;
    for (uint8_t i = 0U; i < BMS_MAX_TEMP_SENSORS; i++)
    {
        uint16_t raw_temp = BMS_BytesToUint16(&p_regs[temp_base + (i * 2U)]);
        p_bms->temperatures_celsius[i] = (int16_t)raw_temp - 40;
    }

    /* --- 3. Electrical Data --- */
    /* 0x0038: Total Battery Voltage (Resolution 0.1V) */
    uint16_t raw_total_v = BMS_BytesToUint16(&p_regs[0x38U * 2U]);
    p_bms->total_voltage_v = (float)raw_total_v * 0.1f;

    /* 0x0039: Current Data (Offset 30000, Resolution 0.1A) */
    uint16_t raw_current = BMS_BytesToUint16(&p_regs[0x39U * 2U]);
    p_bms->current_a = ((float)raw_current - 30000.0f) * 0.1f;

    /* 0x003A: SOC (Resolution 0.001 -> %) */
    uint16_t raw_soc = BMS_BytesToUint16(&p_regs[0x3AU * 2U]);
    p_bms->soc_percent = ((float)raw_soc / 1000.0f) * 100.0f;

    /* 0x003C & 0x003D: Quantities */
    p_bms->active_cell_count = (uint8_t)BMS_BytesToUint16(&p_regs[0x3CU * 2U]);

    /* --- 4. Cell Extremes (0x003E ~ 0x0042) --- */
    p_bms->max_cell_mv  = BMS_BytesToUint16(&p_regs[0x3EU * 2U]);
    p_bms->min_cell_mv  = BMS_BytesToUint16(&p_regs[0x40U * 2U]);
    p_bms->cell_diff_mv = BMS_BytesToUint16(&p_regs[0x42U * 2U]);

    /* --- 5. Temperature Extremes (0x0043 ~ 0x0047) --- */
    p_bms->max_temp_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x43U * 2U]) - 40;
    p_bms->min_temp_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x45U * 2U]) - 40;

    /* --- 6. Work Status & MOS States (0x0048, 0x0052) --- */
    p_bms->work_state = (bms_work_state_t)BMS_BytesToUint16(&p_regs[0x48U * 2U]);
    p_bms->mos_status_flags = BMS_BytesToUint16(&p_regs[0x52U * 2U]);

    /* --- 7. Auxiliary Temperatures (0x005A, 0x005B) --- */
    p_bms->mos_temperature_celsius     = (int16_t)BMS_BytesToUint16(&p_regs[0x5AU * 2U]) - 40;
    p_bms->ambient_temperature_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x5BU * 2U]) - 40;

    /* --- 8. Wake-Up Source (0x006B) --- */
    p_bms->wake_source_flags = BMS_BytesToUint16(&p_regs[0x6BU * 2U]);

    /* --- 9. Fault Codes (0x006D ~ 0x0073) --- */
    for (uint8_t i = 0U; i < 7U; i++) {
        p_bms->fault_codes[i] = BMS_BytesToUint16(&p_regs[(0x6DU + i) * 2U]);
    }

    p_bms->is_data_valid = true;
    return BMS_OK;
}

bms_status_t BMS_ParseSOHResponse(bms_data_t *p_bms, const uint8_t *p_frame, uint16_t length)
{
    if ((p_bms == NULL) || (p_frame == NULL)) {
        return BMS_ERROR_NULL_PTR;
    }

    if (length < BMS_SOH_FRAME_LEN) {
        return BMS_ERROR_INVALID_LEN;
    }

    /* CRC Check */
    uint16_t calc_crc = BMS_CalculateCRC16(p_frame, length - 2U);
    uint16_t recv_crc = (uint16_t)p_frame[length - 2U] | ((uint16_t)p_frame[length - 1U] << 8U);

    if (calc_crc != recv_crc) {
        return BMS_ERROR_CRC_MISMATCH;
    }

    if ((p_frame[0] != BMS_SLAVE_RESP_ADDR) || (p_frame[1] != 0x03U) || (p_frame[2] != 0x02U)) {
        return BMS_ERROR_INVALID_CMD;
    }

    /* 0x0117 Register: SOH value (Resolution 0.001 -> %) */
    uint16_t raw_soh = BMS_BytesToUint16(&p_frame[3]);
    p_bms->soh_percent = ((float)raw_soh / 1000.0f) * 100.0f;
    p_bms->is_soh_valid = true;

    return BMS_OK;
}

/* ========================================================================== */
/*                             EXTERNAL API GETTERS                           */
/* ========================================================================== */

float BMS_GetTotalVoltage(const bms_data_t *p_bms) {
    return (p_bms != NULL) ? p_bms->total_voltage_v : 0.0f;
}

float BMS_GetCurrent(const bms_data_t *p_bms) {
    return (p_bms != NULL) ? p_bms->current_a : 0.0f;
}

float BMS_GetSOC(const bms_data_t *p_bms) {
    return (p_bms != NULL) ? p_bms->soc_percent : 0.0f;
}

float BMS_GetSOH(const bms_data_t *p_bms) {
    return (p_bms != NULL) ? p_bms->soh_percent : 0.0f;
}

uint16_t BMS_GetCellVoltage(const bms_data_t *p_bms, uint8_t cell_index) {
    if ((p_bms != NULL) && (cell_index < BMS_MAX_CELL_COUNT)) {
        return p_bms->cell_voltage_mv[cell_index];
    }
    return 0U;
}

int16_t BMS_GetTemperature(const bms_data_t *p_bms, uint8_t sensor_index) {
    if ((p_bms != NULL) && (sensor_index < BMS_MAX_TEMP_SENSORS)) {
        return p_bms->temperatures_celsius[sensor_index];
    }
    return -40;
}

bool BMS_IsDataValid(const bms_data_t *p_bms) {
    return (p_bms != NULL) ? p_bms->is_data_valid : false;
}

