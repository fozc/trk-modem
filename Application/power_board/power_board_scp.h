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
