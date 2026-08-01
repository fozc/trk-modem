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
 * @brief Main Parsed BMS Data Structure
 * @note All raw registers are mapped to physical floating/signed units here.
 */
typedef struct
{
    /* Cell Voltages */
    uint16_t cell_voltage_mv[BMS_MAX_CELL_COUNT]; /*!< Individual cell voltages in mV */
    uint8_t  active_cell_count;                   /*!< Configured active battery cell count */

    /* Temperatures */
    int16_t  temperatures_celsius[BMS_MAX_TEMP_SENSORS]; /*!< NTC temperatures (-40 to +100 °C) */
    int16_t  mos_temperature_celsius;                     /*!< Power MOS temperature (°C) */
    int16_t  ambient_temperature_celsius;                 /*!< Ambient temperature (°C) */

    /* Electrical Measurements */
    float    total_voltage_v;   /*!< Total Pack Voltage in Volts (Resolution 0.1V) */
    float    current_a;         /*!< Pack Current (+: Charging, -: Discharging) in Amps */
    float    soc_percent;       /*!< State of Charge (%) */
    float    soh_percent;       /*!< State of Health (%) */
    float    remaining_cap_ah;  /*!< Remaining Capacity in Ah */

    /* Min/Max Extremes */
    uint16_t max_cell_mv;       /*!< Highest cell voltage in mV */
    uint16_t min_cell_mv;       /*!< Lowest cell voltage in mV */
    uint16_t cell_diff_mv;      /*!< Cell voltage delta (Max - Min) in mV */

    int16_t  max_temp_celsius;  /*!< Maximum cell temperature in °C */
    int16_t  min_temp_celsius;  /*!< Minimum cell temperature in °C */

    /* Status & Flags */
    bms_work_state_t work_state; /*!< Stationary / Charging / Discharging */
    uint16_t wake_source_flags;  /*!< Bitmask for wakeup source */
    uint16_t mos_status_flags;   /*!< Bitmask for Charge/Discharge MOS status */
    uint16_t fault_codes[7];     /*!< Fault/Alarm code registers (0x6D..0x73) */

    bool is_soh_valid;           /*!< SOH data parsed successfully flag */
    bool is_data_valid;          /*!< Main map data valid flag */
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
