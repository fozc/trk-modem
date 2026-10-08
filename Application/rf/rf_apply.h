/*
 * rf_apply.h
 *
 *  Created on: Oct 8, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Save desired settings and apply every active feeder in sequence.
 */

#ifndef RF_APPLY_H
#define RF_APPLY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RF_APPLY_IDLE,
    RF_APPLY_RUNNING,
    RF_APPLY_COMPLETE,
    RF_APPLY_STOPPED
} rf_apply_state_t;

typedef enum
{
    RF_APPLY_SAVE_BUSY,
    RF_APPLY_SAVE_ERROR,
    RF_APPLY_SAVE_OK
} rf_apply_save_result_t;

typedef enum
{
    RF_APPLY_STOP_NONE,
    RF_APPLY_STOP_START_REJECTED,
    RF_APPLY_STOP_ID_EXHAUSTED,
    RF_APPLY_STOP_GROUP_ERROR,
    RF_APPLY_STOP_PEER_ACTIVE,
    RF_APPLY_STOP_HUB_RESTARTED,
    RF_APPLY_STOP_CANCELLED,
    RF_APPLY_STOP_STATE_CHANGED
} rf_apply_stop_reason_t;

typedef struct
{
    rf_apply_state_t state;
    uint8_t targets;
    uint8_t applied;
    uint8_t line;
    bool group_started;
    rf_apply_stop_reason_t stop_reason;
} rf_apply_status_t;

void rf_apply_init(void);
void rf_apply_hub_restarted(void);
bool rf_apply_is_running(void);
bool rf_apply_can_save(void);
/* Called after parsing/staging succeeds; RF starts only after sync succeeds. */
rf_apply_save_result_t rf_apply_save(void);
void rf_apply_process(void);
bool rf_apply_abort(void);
void rf_apply_get_status(rf_apply_status_t *out);

#endif /* RF_APPLY_H */

/*** end of file ***/
