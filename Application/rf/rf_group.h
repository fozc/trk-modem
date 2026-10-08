/*
 * rf_group.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Apply one feeder configuration to its three acknowledged members.
 */

#ifndef RF_GROUP_H
#define RF_GROUP_H

#include "rf_scp_codec.h"
#include <stddef.h>

typedef enum
{
    RF_GROUP_IDLE,
    RF_GROUP_CHECKING,
    RF_GROUP_WRITING,
    RF_GROUP_COMMITTING,
    RF_GROUP_WAITING,
    RF_GROUP_APPLIED,
    RF_GROUP_FAILED,
    RF_GROUP_UNCERTAIN,
    RF_GROUP_ID_IN_USE,
    RF_GROUP_MISMATCH,
    RF_GROUP_ABORTING,
    RF_GROUP_CANCELLED,
    RF_GROUP_RESTARTED
} rf_group_state_t;

typedef struct
{
    rf_group_state_t state;
    uint8_t group_id;
    uint8_t feeder;
    uint8_t line;
    uint8_t writes_acked;
    uint16_t expected_crc;
    bool has_report;
    rf_scp_config_status_t report;
    /* Member bitmap bits follow this accepted WRITE order, not phase ID. */
    uint8_t members[3][8];
} rf_group_status_t;

void rf_group_init(void);
void rf_group_hub_restarted(void);

/* Snapshot the existing store; no NVRAM changes or automatic restart.
 * After an uncertain pre-COMMIT WRITE, explicitly start the same line/ID
 * with unchanged members/block to rewrite all three members (BQ-11).
 */
bool rf_group_start(size_t line_index, uint8_t group_id);
bool rf_group_get_status(rf_group_status_t *out);
bool rf_group_matches_config(void);
bool rf_group_is_active(void);
void rf_group_process(uint32_t now_ms);
bool rf_group_handle_status(const rf_scp_message_t *message);

/* Allowed only after this COMMIT's group identity is known to the MH.
 * Pre-COMMIT ABORT is unsupported by the current MH (BQ-11).
 * A job with no transmitted WRITE can be cancelled locally, without ABORT.
 */
bool rf_group_abort(void);

#endif /* RF_GROUP_H */

/*** end of file ***/
