/*
 * power_board_scp.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Canonical SCP power-board snapshot and per-field validity.
 */

#ifndef POWER_BOARD_SCP_H
#define POWER_BOARD_SCP_H

#include "rf_scp_codec.h"

#define POWER_VALID_SUMMARY          (1U << 0U)
#define POWER_VALID_ADC              (1U << 1U)
#define POWER_VALID_CHARGER          (1U << 2U)
#define POWER_VALID_POWER            (1U << 3U)
#define POWER_VALID_SYSTEM_POWER     (1U << 4U)
#define POWER_VALID_SOC              (1U << 5U)
#define POWER_SOC_ABSOLUTE           (1U << 6U)
#define POWER_VALID_SOH              (1U << 7U)
#define POWER_VALID_BATTERY_TEMP     (1U << 8U)
#define POWER_VALID_BOARD_TEMP       (1U << 9U)
#define POWER_VALID_CHARGE_PHASE     (1U << 10U)
#define POWER_VALID_CAPACITY         (1U << 11U)
#define POWER_VALID_SOURCE           (1U << 12U)
#define POWER_VALID_ALARMS           (1U << 13U)
#define POWER_VALID_DISCHARGE        (1U << 14U)
#define POWER_VALID_CHARGE_VERDICT   (1U << 15U)

typedef struct
{
    rf_scp_power_summary_t summary;
    bool has_summary;
    uint16_t valid_fields;
    uint32_t received_age_ms;
    uint8_t telemetry_age_sec;
    uint32_t active_alarms;
    uint8_t alarm_seq;
    uint8_t suppressed;
    bool has_last_gasp;
    bool last_gasp_cancelled;
    uint8_t last_gasp_session;
    uint8_t last_gasp_cause;
} power_board_snapshot_t;

/**
 * @brief Last received 0xE1 PWR_SUMMARY packet (R2: 45 B, R1: 39 B).
 */
#define POWER_SUMMARY_RAW_MAX 45U

typedef struct
{
    uint8_t len;                   /**< Body length as received. */
    uint32_t tick_ms;              /**< HAL tick at reception. */
    uint8_t year;                  /**< RTC year at reception (two digits). */
    uint8_t month;                 /**< RTC month at reception. */
    uint8_t day;                   /**< RTC day at reception. */
    uint8_t hour;                  /**< RTC hour at reception. */
    uint8_t minute;                /**< RTC minute at reception. */
    uint8_t second;                /**< RTC second at reception. */
    uint8_t raw[POWER_SUMMARY_RAW_MAX]; /**< Raw body bytes. */
} power_summary_raw_t;

/**
 * @brief Name of a level alarm code (R2 section 5.3, codes 0..20).
 *
 * @param code  Alarm code; equals the bit index in the alarm mask.
 *
 * @return Static ASCII name, or NULL for an undefined code.
 */
const char *power_board_alarm_name(uint8_t code);
/**
 * @brief Retain the last 0xE1 PWR_SUMMARY body with its reception time.
 *
 * @param data     Raw packet body from the SCP layer.
 * @param len      Body length; kept only when 1..POWER_SUMMARY_RAW_MAX.
 * @param now_ms   HAL tick at reception.
 *
 * @note Called from the RF receive path; keeps the previous packet when
 *       the length is out of range.
 */
void power_board_capture_summary_raw(const uint8_t *data, uint8_t len,
                                     uint32_t now_ms);
/**
 * @brief Copy the retained last summary packet.
 *
 * @param[out] out  Destination for the retained packet and its time.
 *
 * @return true when a packet has been received since boot, else false.
 */
bool power_board_get_summary_raw(power_summary_raw_t *out);

void power_board_scp_init(void);
void power_board_scp_hub_restarted(void);
void power_board_scp_process(uint32_t now_ms);
bool power_board_handle_summary(const rf_scp_message_t *message,
                                 uint32_t now_ms);
bool power_board_handle_alarm(const rf_scp_message_t *message,
                               uint32_t now_ms);
bool power_board_get_snapshot(uint32_t now_ms, power_board_snapshot_t *out);

#endif /* POWER_BOARD_SCP_H */

/*** end of file ***/
