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

    /* Zero every field, then apply the only non-zero default (temperatures are
     * decoded as (raw - 40), so an unpopulated sensor should read -40 degC). */
    *p_bms = (bms_data_t){ 0 };
    for (uint8_t i = 0U; i < BMS_MAX_TEMP_SENSORS; i++) {
        p_bms->temperatures_celsius[i] = -40;
    }

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

    /* ---------------------------------------------------------------------
     * Scalar registers, decoded in ascending protocol-address order.
     * ------------------------------------------------------------------- */

    /* 0x38: total pack voltage (0.1 V/bit). */
    p_bms->total_voltage_v = (float)BMS_BytesToUint16(&p_regs[0x38U * 2U]) * 0.1f;

    /* 0x39: current (0.1 A/bit, 30000 offset; +charge / -discharge). */
    p_bms->current_a = ((float)BMS_BytesToUint16(&p_regs[0x39U * 2U]) - 30000.0f) * 0.1f;

    /* 0x3A: state of charge (0.001 -> %). */
    p_bms->soc_percent = ((float)BMS_BytesToUint16(&p_regs[0x3AU * 2U]) / 1000.0f) * 100.0f;

    /* 0x3B: LIFE heartbeat counter. */
    p_bms->life_heartbeat = BMS_BytesToUint16(&p_regs[0x3BU * 2U]);

    /* 0x3C / 0x3D: cell and temperature-sensor quantities. */
    p_bms->active_cell_count = (uint8_t)BMS_BytesToUint16(&p_regs[0x3CU * 2U]);
    p_bms->temp_sensor_count = (uint8_t)BMS_BytesToUint16(&p_regs[0x3DU * 2U]);

    /* 0x3E..0x42: cell-voltage extremes and their cell serial numbers. */
    p_bms->max_cell_mv    = BMS_BytesToUint16(&p_regs[0x3EU * 2U]);
    p_bms->max_cell_index = BMS_BytesToUint16(&p_regs[0x3FU * 2U]);
    p_bms->min_cell_mv    = BMS_BytesToUint16(&p_regs[0x40U * 2U]);
    p_bms->min_cell_index = BMS_BytesToUint16(&p_regs[0x41U * 2U]);
    p_bms->cell_diff_mv   = BMS_BytesToUint16(&p_regs[0x42U * 2U]);

    /* 0x43..0x47: temperature extremes (raw - 40) and their sensor serials. */
    p_bms->max_temp_celsius  = (int16_t)BMS_BytesToUint16(&p_regs[0x43U * 2U]) - 40;
    p_bms->max_temp_index    = BMS_BytesToUint16(&p_regs[0x44U * 2U]);
    p_bms->min_temp_celsius  = (int16_t)BMS_BytesToUint16(&p_regs[0x45U * 2U]) - 40;
    p_bms->min_temp_index    = BMS_BytesToUint16(&p_regs[0x46U * 2U]);
    p_bms->temp_diff_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x47U * 2U]);

    /* 0x48..0x4A: charge/discharge, charger and load status. */
    p_bms->work_state     = (bms_work_state_t)BMS_BytesToUint16(&p_regs[0x48U * 2U]);
    p_bms->charger_status = BMS_BytesToUint16(&p_regs[0x49U * 2U]);
    p_bms->load_status    = BMS_BytesToUint16(&p_regs[0x4AU * 2U]);

    /* 0x4B..0x4D: remaining capacity (0.1 Ah), cycle count, balance state. */
    p_bms->remaining_cap_ah = (float)BMS_BytesToUint16(&p_regs[0x4BU * 2U]) * 0.1f;
    p_bms->cycle_count      = BMS_BytesToUint16(&p_regs[0x4CU * 2U]);
    p_bms->balance_state    = BMS_BytesToUint16(&p_regs[0x4DU * 2U]);

    /* 0x4F..0x51: per-cell balance position bitmask (each bit = one cell). */
    for (uint8_t i = 0U; i < 3U; i++) {
        p_bms->balance_position[i] = BMS_BytesToUint16(&p_regs[(0x4FU + i) * 2U]);
    }

    /* 0x52..0x56: individual MOS on/off registers packed into one bitmask
     * (bit0 charge, bit1 discharge, bit2 precharge, bit3 heating, bit4 fan). */
    uint16_t mos_flags = 0U;
    if (BMS_BytesToUint16(&p_regs[0x52U * 2U]) != 0U) { mos_flags |= (uint16_t)(UINT16_C(1) << 0U); }
    if (BMS_BytesToUint16(&p_regs[0x53U * 2U]) != 0U) { mos_flags |= (uint16_t)(UINT16_C(1) << 1U); }
    if (BMS_BytesToUint16(&p_regs[0x54U * 2U]) != 0U) { mos_flags |= (uint16_t)(UINT16_C(1) << 2U); }
    if (BMS_BytesToUint16(&p_regs[0x55U * 2U]) != 0U) { mos_flags |= (uint16_t)(UINT16_C(1) << 3U); }
    if (BMS_BytesToUint16(&p_regs[0x56U * 2U]) != 0U) { mos_flags |= (uint16_t)(UINT16_C(1) << 4U); }
    p_bms->mos_status_flags = mos_flags;

    /* 0x57..0x59: average cell voltage (mV), power (W), energy (Wh). */
    p_bms->average_voltage_mv = BMS_BytesToUint16(&p_regs[0x57U * 2U]);
    p_bms->power_w            = BMS_BytesToUint16(&p_regs[0x58U * 2U]);
    p_bms->energy_wh          = BMS_BytesToUint16(&p_regs[0x59U * 2U]);

    /* 0x5A..0x5D: MOS/ambient/heating temperatures (raw - 40) and heating current. */
    p_bms->mos_temperature_celsius     = (int16_t)BMS_BytesToUint16(&p_regs[0x5AU * 2U]) - 40;
    p_bms->ambient_temperature_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x5BU * 2U]) - 40;
    p_bms->heating_temperature_celsius = (int16_t)BMS_BytesToUint16(&p_regs[0x5CU * 2U]) - 40;
    p_bms->heating_current_a           = BMS_BytesToUint16(&p_regs[0x5DU * 2U]);

    /* 0x5F / 0x60: current-limit state and limit value (0.1 A/bit, 30000 offset). */
    p_bms->current_limit_state = BMS_BytesToUint16(&p_regs[0x5FU * 2U]);
    p_bms->current_limit_a     = ((float)BMS_BytesToUint16(&p_regs[0x60U * 2U]) - 30000.0f) * 0.1f;

    /* 0x61..0x63: real-time clock (Year|Month, Day|Hour, Minute|Second). */
    uint16_t rtc_ym = BMS_BytesToUint16(&p_regs[0x61U * 2U]);
    uint16_t rtc_dh = BMS_BytesToUint16(&p_regs[0x62U * 2U]);
    uint16_t rtc_ms = BMS_BytesToUint16(&p_regs[0x63U * 2U]);
    p_bms->rtc.year   = (uint8_t)(rtc_ym >> 8U);
    p_bms->rtc.month  = (uint8_t)(rtc_ym & 0xFFU);
    p_bms->rtc.day    = (uint8_t)(rtc_dh >> 8U);
    p_bms->rtc.hour   = (uint8_t)(rtc_dh & 0xFFU);
    p_bms->rtc.minute = (uint8_t)(rtc_ms >> 8U);
    p_bms->rtc.second = (uint8_t)(rtc_ms & 0xFFU);

    /* 0x64 / 0x65: remaining charge time (min) and DI/DO status word. */
    p_bms->remaining_charge_min = BMS_BytesToUint16(&p_regs[0x64U * 2U]);
    p_bms->dido_status          = BMS_BytesToUint16(&p_regs[0x65U * 2U]);

    /* 0x6B: wake-up source bitmask. */
    p_bms->wake_source_flags = BMS_BytesToUint16(&p_regs[0x6BU * 2U]);

    /* 0x6D..0x73: new fault-code words. */
    for (uint8_t i = 0U; i < 7U; i++) {
        p_bms->fault_codes[i] = BMS_BytesToUint16(&p_regs[(0x6DU + i) * 2U]);
    }

    /* 0x7E: communication interface type (1 = RS485, 2 = UART). */
    p_bms->comm_interface_type = BMS_BytesToUint16(&p_regs[0x7EU * 2U]);

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

