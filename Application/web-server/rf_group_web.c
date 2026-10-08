/*
 * rf_group_web.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Report verified RF group and save/application sequence state.
 */

#include "rf_group_web.h"
#include "rf_group.h"
#include "rf_apply.h"
#include "modem_types.h"
#include "xprintf.h"

static const char *state_name(rf_group_state_t state)
{
    switch (state)
    {
        case RF_GROUP_IDLE: return "idle";
        case RF_GROUP_CHECKING: return "checking";
        case RF_GROUP_WRITING: return "writing";
        case RF_GROUP_COMMITTING: return "committing";
        case RF_GROUP_WAITING: return "waiting";
        case RF_GROUP_APPLIED: return "applied";
        case RF_GROUP_FAILED: return "failed";
        case RF_GROUP_UNCERTAIN: return "uncertain";
        case RF_GROUP_ID_IN_USE: return "id_in_use";
        case RF_GROUP_MISMATCH: return "mismatch";
        case RF_GROUP_ABORTING: return "aborting";
        case RF_GROUP_CANCELLED: return "cancelled";
        case RF_GROUP_RESTARTED: return "restarted";
        default: return "unknown";
    }
}

static const char *stop_reason_name(rf_apply_stop_reason_t reason)
{
    switch (reason)
    {
        case RF_APPLY_STOP_NONE: return "none";
        case RF_APPLY_STOP_START_REJECTED: return "start_rejected";
        case RF_APPLY_STOP_ID_EXHAUSTED: return "id_exhausted";
        case RF_APPLY_STOP_GROUP_ERROR: return "group_error";
        case RF_APPLY_STOP_PEER_ACTIVE: return "peer_active";
        case RF_APPLY_STOP_HUB_RESTARTED: return "hub_restarted";
        case RF_APPLY_STOP_CANCELLED: return "cancelled";
        case RF_APPLY_STOP_STATE_CHANGED: return "state_changed";
        case RF_APPLY_STOP_STORAGE: return "storage";
        case RF_APPLY_STOP_INVENTORY: return "inventory";
        default: return "unknown";
    }
}

bool rf_group_status_json_build(char *buffer, size_t capacity, size_t *length)
{
    rf_group_status_t group;
    rf_apply_status_t batch;
    const char *batch_name;

    if ((NULL == buffer) || (NULL == length) || (0U == capacity) ||
        (UINT32_MAX < capacity))
    {
        return false;
    }
    *length = 0U;
    buffer[0] = '\0';
    if (!rf_group_get_status(&group))
    {
        return false;
    }
    rf_apply_get_status(&batch);
    switch (batch.state)
    {
        case RF_APPLY_RUNNING: batch_name = "running"; break;
        case RF_APPLY_COMPLETE: batch_name = "complete"; break;
        case RF_APPLY_STOPPED: batch_name = "stopped"; break;
        default: batch_name = "idle"; break;
    }
    const uint32_t count = xsnprintf(buffer, (unsigned int)capacity,
        "{\"State\":\"%s\",\"Line\":%u,\"Feeder\":%u,\"GroupId\":%u,"
        "\"WritesAcked\":%u,\"ExpectedCRC\":%u,\"MatchesDesired\":%s,"
        "\"HasReport\":%s,\"MHState\":%u,\"MemberBitmap\":%u,"
        "\"Reason\":%u,\"ReportedCRC\":%u,\"Attempts\":%u,"
        "\"BatchState\":\"%s\",\"Targets\":%u,\"Applied\":%u,"
        "\"BatchLine\":%u,\"GroupStarted\":%s,\"SaveBlocked\":%s,"
        "\"InventoryPending\":%s,\"StopReason\":\"%s\"}",
        state_name(group.state), (unsigned)group.line, (unsigned)group.feeder,
        (unsigned)group.group_id, (unsigned)group.writes_acked,
        (unsigned)group.expected_crc,
        rf_group_matches_config() ? "true" : "false",
        group.has_report ? "true" : "false",
        (unsigned)group.report.state, (unsigned)group.report.member_bitmap,
        (unsigned)group.report.reason, (unsigned)group.report.config_crc,
        (unsigned)group.report.attempts, batch_name,
        (unsigned)batch.targets, (unsigned)batch.applied,
        (unsigned)batch.line, batch.group_started ? "true" : "false",
        rf_apply_can_save() ? "false" : "true",
        batch.inventory_pending ? "true" : "false",
        stop_reason_name(batch.stop_reason));

    if ((size_t)count >= capacity - 1U)
    {
        buffer[0] = '\0';
        return false;
    }
    *length = (size_t)count;
    return true;
}

/*** end of file ***/
