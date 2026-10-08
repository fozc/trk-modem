/*
 * rf_apply.c
 *
 *  Created on: Oct 8, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Sequence existing RF groups after a successful settings save.
 */

#include "rf_apply.h"
#include "rf_config.h"
#include "rf_group.h"
#include "rf_comm.h"
#include "rf_inventory.h"
#include <stddef.h>

/* Cooperative context only. Web is the sole production settings writer
 * and rejects saves while running. Shell group starts are also excluded.
 * rf_group snapshots each feeder before transmitting any WRITE.
 */
static rf_apply_status_t batch;
static uint8_t next_group_id;
static uint8_t current_group_id;
static uint16_t checked_ids;

_Static_assert(MAX_POWER_LINE_COUNT <= 8U, "RF target bitmap capacity");

void rf_apply_init(void)
{
    batch = (rf_apply_status_t){0};
    next_group_id = 1U;
    current_group_id = 0U;
    checked_ids = 0U;
}

bool rf_apply_is_running(void)
{
    return RF_APPLY_RUNNING == batch.state;
}

void rf_apply_hub_restarted(void)
{
    if (rf_apply_is_running())
    {
        batch.state = RF_APPLY_STOPPED;
        batch.inventory_pending = false;
        batch.stop_reason = RF_APPLY_STOP_HUB_RESTARTED;
    }
}

bool rf_apply_can_save(void)
{
    return !rf_apply_is_running() && !rf_group_is_active() &&
           !rf_inventory_is_active();
}

void rf_apply_get_status(rf_apply_status_t *out)
{
    if (NULL != out)
    {
        *out = batch;
    }
}

rf_apply_save_result_t rf_apply_save(void)
{
    if (!rf_apply_can_save())
    {
        return RF_APPLY_SAVE_BUSY;
    }
    const int save_result = rf_store_sync();

    if (0 > save_result)
    {
        return RF_APPLY_SAVE_ERROR;
    }
    if (0 < save_result)
    {
        batch = (rf_apply_status_t){.state = RF_APPLY_STOPPED,
                                    .stop_reason = RF_APPLY_STOP_STORAGE};
        return RF_APPLY_SAVE_PRIMARY_ONLY;
    }
    batch = (rf_apply_status_t){.state = RF_APPLY_RUNNING};
    checked_ids = 0U;
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; index++)
    {
        const rf_feeder_t *feeder = rf_store_get((feeder_id_t)index);

        if ((NULL != feeder) && feeder->in_use)
        {
            batch.targets |= (uint8_t)(1U << index);
        }
    }
    if (!rf_inventory_matches_config())
    {
        if (!rf_inventory_start())
        {
            batch.state = RF_APPLY_STOPPED;
            batch.stop_reason = RF_APPLY_STOP_INVENTORY;
        }
        else
        {
            batch.inventory_pending = true;
        }
    }
    else if (0U == batch.targets)
    {
        batch.state = RF_APPLY_COMPLETE;
    }
    else
    {
        /* Existing accepted assignments can be configured directly. */
    }
    return RF_APPLY_SAVE_OK;
}

static void start_next_group(void)
{
    if (!scp_is_free())
    {
        return;
    }
    if (UINT8_MAX <= checked_ids)
    {
        batch.state = RF_APPLY_STOPPED;
        batch.stop_reason = RF_APPLY_STOP_ID_EXHAUSTED;
        return;
    }
    if (0U == next_group_id)
    {
        next_group_id = 1U;
    }
    current_group_id = next_group_id;
    next_group_id = (UINT8_MAX == next_group_id) ?
                    1U : (uint8_t)(next_group_id + 1U);
    checked_ids++;
    if (!rf_group_start((size_t)batch.line - 1U, current_group_id))
    {
        batch.state = RF_APPLY_STOPPED;
        batch.stop_reason = RF_APPLY_STOP_START_REJECTED;
        return;
    }
    batch.group_started = true;
}

void rf_apply_process(void)
{
    rf_group_status_t status;

    if (!rf_apply_is_running())
    {
        return;
    }
    if (batch.inventory_pending)
    {
        if (rf_inventory_is_active())
        {
            return;
        }
        batch.inventory_pending = false;
        if (!rf_inventory_matches_config())
        {
            batch.state = RF_APPLY_STOPPED;
            batch.stop_reason = RF_APPLY_STOP_INVENTORY;
            return;
        }
    }
    if (batch.group_started)
    {
        if (!rf_group_get_status(&status) ||
            (status.line != batch.line) ||
            (status.group_id != current_group_id))
        {
            batch.state = RF_APPLY_STOPPED;
            batch.stop_reason = RF_APPLY_STOP_STATE_CHANGED;
            return;
        }
        switch (status.state)
        {
            case RF_GROUP_APPLIED:
                batch.applied |= (uint8_t)(1U << (batch.line - 1U));
                batch.group_started = false;
                checked_ids = 0U;
                break;
            case RF_GROUP_ID_IN_USE:
                /* Skip terminal peer jobs; an active peer stays locked. */
                if (rf_group_is_active())
                {
                    batch.state = RF_APPLY_STOPPED;
                    batch.stop_reason = RF_APPLY_STOP_PEER_ACTIVE;
                    return;
                }
                batch.group_started = false;
                break;
            case RF_GROUP_CHECKING:
            case RF_GROUP_WRITING:
            case RF_GROUP_COMMITTING:
            case RF_GROUP_WAITING:
            case RF_GROUP_ABORTING:
                return;
            default:
                batch.state = RF_APPLY_STOPPED;
                batch.stop_reason = RF_APPLY_STOP_GROUP_ERROR;
                return;
        }
    }
    const uint8_t pending = (uint8_t)(batch.targets & (uint8_t)~batch.applied);

    if (0U == pending)
    {
        batch.state = RF_APPLY_COMPLETE;
        return;
    }
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; index++)
    {
        if (0U != (pending & (uint8_t)(1U << index)))
        {
            batch.line = (uint8_t)(index + 1U);
            break;
        }
    }
    start_next_group();
}

bool rf_apply_abort(void)
{
    /* The inventory service owns draining/upload; it has no cancel API. */
    if (batch.inventory_pending)
    {
        return false;
    }
    if (rf_apply_is_running() && !batch.group_started)
    {
        batch.state = RF_APPLY_STOPPED;
        batch.stop_reason = RF_APPLY_STOP_CANCELLED;
        return true;
    }
    if (!rf_group_abort())
    {
        return false;
    }
    if (rf_apply_is_running())
    {
        batch.state = RF_APPLY_STOPPED;
        batch.stop_reason = RF_APPLY_STOP_CANCELLED;
    }
    return true;
}

/*** end of file ***/
