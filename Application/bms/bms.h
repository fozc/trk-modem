/*
 * @file    bms_modbus.h
 * @brief   BMS (Battery Management System) Modbus RTU Parser Engine
 * @note    Designed for 32-bit Microcontrollers (STM32, ESP32, AVR, MSP430)
 *          Calculations apply strict scaling factors & offsets according to protocol.
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */
#ifndef BMS_MODBUS_H
#define BMS_MODBUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           DEFINES & CONSTANTS                              */
/* ========================================================================== */

#define BMS_MAX_CELL_COUNT         (16U)   /*!< Max supported battery cells (0x00..0x0F) */
#define BMS_MAX_TEMP_SENSORS       (8U)    /*!< Max supported temp sensors (0x30..0x37) */

#define BMS_SLAVE_REQ_ADDR         (0x81U) /*!< Master request slave address */
#define BMS_SLAVE_RESP_ADDR        (0x51U) /*!< Slave response address */

#define BMS_FULL_MAP_PAYLOAD_LEN   (254U)  /*!< 127 Regs * 2 Bytes */
#define BMS_FULL_MAP_FRAME_LEN     (259U)  /*!< ADDR(1) + CMD(1) + LEN(1) + DATA(254) + CRC(2) */

#define BMS_SOH_FRAME_LEN          (7U)    /*!< ADDR(1) + CMD(1) + LEN(1) + DATA(2) + CRC(2) */

/* ========================================================================== */
/*                             DATA STRUCTURES                                */
/* ========================================================================== */

/**
 * @brief BMS Error/Return Status Codes
 */
typedef enum
{
    BMS_OK                  = 0x00U,
    BMS_ERROR_NULL_PTR      = 0x01U,
    BMS_ERROR_INVALID_ADDR  = 0x02U,
    BMS_ERROR_INVALID_CMD   = 0x03U,
    BMS_ERROR_INVALID_LEN   = 0x04U,
    BMS_ERROR_CRC_MISMATCH  = 0x05U,
    BMS_ERROR_UNSUPPORTED   = 0x06U
} bms_status_t;

/**
 * @brief Charge / Discharge MOS & Operation States
 */
typedef enum
{
    BMS_STATE_STATIONARY = 0U,
    BMS_STATE_CHARGING   = 1U,
    BMS_STATE_DISCHARGING= 2U
} bms_work_state_t;

/**
 * @brief BMS Real-Time Clock (registers 0x61..0x63, packed byte pairs).
 */
typedef struct
{
    uint8_t year;   /*!< Two-digit year; absolute year = 2000 + year */
    uint8_t month;  /*!< 1..12 */
    uint8_t day;    /*!< 1..31 */
    uint8_t hour;   /*!< 0..23 */
    uint8_t minute; /*!< 0..59 */
    uint8_t second; /*!< 0..59 */
} bms_rtc_t;

/**
 * @brief Main Parsed BMS Data Structure
 * @note  Fields follow the protocol register order (0x00..0x7E) plus the
 *        separately-polled SOH (0x117). Raw registers are decoded to physical
 *        units; the source register address is noted per field.
 */
typedef struct
{
    /* 0x00..0x2F: per-cell voltage, 1 mV/bit (only the active cells are used). */
    uint16_t cell_voltage_mv[BMS_MAX_CELL_COUNT];

    /* 0x30..0x37: battery temperatures, degC (raw - 40). */
    int16_t  temperatures_celsius[BMS_MAX_TEMP_SENSORS];

    float    total_voltage_v;   /*!< 0x38: pack voltage, 0.1 V/bit */
    float    current_a;         /*!< 0x39: current, 0.1 A, +charge / -discharge */
    float    soc_percent;       /*!< 0x3A: state of charge, % */
    uint16_t life_heartbeat;    /*!< 0x3B: LIFE heartbeat counter */
    uint8_t  active_cell_count; /*!< 0x3C: battery (cell) quantity */
    uint8_t  temp_sensor_count; /*!< 0x3D: temperature sensor quantity */

    uint16_t max_cell_mv;       /*!< 0x3E: highest cell voltage, mV */
    uint16_t max_cell_index;    /*!< 0x3F: highest cell serial number */
    uint16_t min_cell_mv;       /*!< 0x40: lowest cell voltage, mV */
    uint16_t min_cell_index;    /*!< 0x41: lowest cell serial number */
    uint16_t cell_diff_mv;      /*!< 0x42: cell voltage delta (max - min), mV */

    int16_t  max_temp_celsius;  /*!< 0x43: max cell temperature, degC (raw - 40) */
    uint16_t max_temp_index;    /*!< 0x44: max temperature sensor serial number */
    int16_t  min_temp_celsius;  /*!< 0x45: min cell temperature, degC (raw - 40) */
    uint16_t min_temp_index;    /*!< 0x46: min temperature sensor serial number */
    int16_t  temp_diff_celsius; /*!< 0x47: max - min temperature delta, degC */

    bms_work_state_t work_state;/*!< 0x48: 0 stationary / 1 charging / 2 discharging */
    uint16_t charger_status;    /*!< 0x49: 0 no charger / 1 charger detected */
    uint16_t load_status;       /*!< 0x4A: 0 no load / 1 load detected */
    float    remaining_cap_ah;  /*!< 0x4B: remaining capacity, 0.1 Ah/bit */
    uint16_t cycle_count;       /*!< 0x4C: battery cycle times */
    uint16_t balance_state;     /*!< 0x4D: 0 off / 1 passive / 2 active balancing */
    uint16_t balance_position[3];/*!< 0x4F..0x51: per-cell balance bitmask */

    uint16_t mos_status_flags;  /*!< 0x52..0x56 packed: b0 charge, b1 discharge, b2 precharge, b3 heating, b4 fan */
    uint16_t average_voltage_mv;/*!< 0x57: average cell voltage, mV */
    uint16_t power_w;           /*!< 0x58: power, W */
    uint16_t energy_wh;         /*!< 0x59: energy, 1 Wh/bit */

    int16_t  mos_temperature_celsius;     /*!< 0x5A: MOS temperature, degC (raw - 40) */
    int16_t  ambient_temperature_celsius; /*!< 0x5B: ambient temperature, degC (raw - 40) */
    int16_t  heating_temperature_celsius; /*!< 0x5C: heating temperature, degC (raw - 40) */
    uint16_t heating_current_a;           /*!< 0x5D: heating current, 1 A/bit */

    uint16_t current_limit_state;/*!< 0x5F: 1 current limiting on / 0 off */
    float    current_limit_a;    /*!< 0x60: current limit, 0.1 A/bit, 30000 offset */

    bms_rtc_t rtc;               /*!< 0x61..0x63: real-time clock */
    uint16_t remaining_charge_min;/*!< 0x64: remaining charging time, min */
    uint16_t dido_status;        /*!< 0x65: DI1..8 (low byte), DO1..8 (high byte) */

    uint16_t wake_source_flags;  /*!< 0x6B: wake-up source bitmask */
    uint16_t fault_codes[7];     /*!< 0x6D..0x73: new fault/alarm code words */
    uint16_t comm_interface_type;/*!< 0x7E: 1 = RS485, 2 = UART */

    float    soh_percent;        /*!< 0x117: state of health, % */

    bool     is_soh_valid;       /*!< SOH frame parsed successfully flag */
    bool     is_data_valid;      /*!< Full-map data valid flag */
} bms_data_t;

/* ========================================================================== */
/*                             API FUNCTIONS                                  */
/* ========================================================================== */

/**
 * @brief  Initializes the BMS Data Structure to safe defaults.
 * @param  p_bms: Pointer to bms_data_t structure.
 * @return bms_status_t Status code.
 */
bms_status_t BMS_Init(bms_data_t *p_bms);

/**
 * @brief  Parses Method 1 Full Map Response Frame (0x0000..0x007E - 127 Regs).
 * @param  p_bms: Pointer to target bms_data_t structure.
 * @param  p_frame: Raw Rx buffer containing response frame.
 * @param  length: Total byte length of p_frame buffer.
 * @return bms_status_t BMS_OK if CRC and data parse succeeds.
 */
bms_status_t BMS_ParseFullMapResponse(bms_data_t *p_bms, const uint8_t *p_frame, uint16_t length);

/**
 * @brief  Parses SOH Response Frame (Address 0x0117).
 * @param  p_bms: Pointer to target bms_data_t structure.
 * @param  p_frame: Raw Rx buffer containing response frame.
 * @param  length: Total byte length of p_frame buffer.
 * @return bms_status_t BMS_OK if CRC and data parse succeeds.
 */
bms_status_t BMS_ParseSOHResponse(bms_data_t *p_bms, const uint8_t *p_frame, uint16_t length);

/* ----- Public Getter APIs for Safe External Access ----- */

float    BMS_GetTotalVoltage(const bms_data_t *p_bms);
float    BMS_GetCurrent(const bms_data_t *p_bms);
float    BMS_GetSOC(const bms_data_t *p_bms);
float    BMS_GetSOH(const bms_data_t *p_bms);
uint16_t BMS_GetCellVoltage(const bms_data_t *p_bms, uint8_t cell_index);
int16_t  BMS_GetTemperature(const bms_data_t *p_bms, uint8_t sensor_index);
bool     BMS_IsDataValid(const bms_data_t *p_bms);

#ifdef __cplusplus
}
#endif

#endif /* BMS_MODBUS_H */
