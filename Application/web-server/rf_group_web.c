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
#include "rf_inventory.h"
#include "modem_types.h"
#include "web_json.h"

static const char *state_name(rf_group_state_t state)
{
    switch (state)
    {
    case RF_GROUP_IDLE:
        return "idle";
    case RF_GROUP_CHECKING:
        return "checking";
    case RF_GROUP_WRITING:
        return "writing";
    case RF_GROUP_COMMITTING:
        return "committing";
    case RF_GROUP_WAITING:
        return "waiting";
    case RF_GROUP_APPLIED:
        return "applied";
    case RF_GROUP_FAILED:
        return "failed";
    case RF_GROUP_UNCERTAIN:
        return "uncertain";
    case RF_GROUP_ID_IN_USE:
        return "id_in_use";
    case RF_GROUP_MISMATCH:
        return "mismatch";
    case RF_GROUP_ABORTING:
        return "aborting";
    case RF_GROUP_CANCELLED:
        return "cancelled";
    case RF_GROUP_RESTARTED:
        return "restarted";
    default:
        return "unknown";
    }
}

static const char *stop_reason_name(rf_apply_stop_reason_t reason)
{
    switch (reason)
    {
    case RF_APPLY_STOP_NONE:
        return "none";
    case RF_APPLY_STOP_START_REJECTED:
        return "start_rejected";
    case RF_APPLY_STOP_ID_EXHAUSTED:
        return "id_exhausted";
    case RF_APPLY_STOP_GROUP_ERROR:
        return "group_error";
    case RF_APPLY_STOP_PEER_ACTIVE:
        return "peer_active";
    case RF_APPLY_STOP_HUB_RESTARTED:
        return "hub_restarted";
    case RF_APPLY_STOP_CANCELLED:
        return "cancelled";
    case RF_APPLY_STOP_STATE_CHANGED:
        return "state_changed";
    case RF_APPLY_STOP_STORAGE:
        return "storage";
    case RF_APPLY_STOP_INVENTORY:
        return "inventory";
    default:
        return "unknown";
    }
}

static const char *inventory_state_name(rf_inventory_status_t state)
{
    switch (state)
    {
    case RF_INVENTORY_LOADING:
        return "loading";
    case RF_INVENTORY_EMPTY:
        return "empty";
    case RF_INVENTORY_PARTIAL:
        return "partial";
    case RF_INVENTORY_READY:
        return "ready";
    case RF_INVENTORY_ERROR:
        return "error";
    case RF_INVENTORY_DRAINING:
        return "draining";
    default:
        return "idle";
    }
}

bool rf_group_status_json_build(char *buffer, size_t capacity, size_t *length)
{
    rf_group_status_t group;
    rf_apply_status_t batch;
    rf_inventory_wait_t wait;
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
    rf_inventory_get_wait(&wait);
    switch (batch.state)
    {
    case RF_APPLY_RUNNING:
        batch_name = "running";
        break;
    case RF_APPLY_COMPLETE:
        batch_name = "complete";
        break;
    case RF_APPLY_PARTIAL:
        batch_name = "partial";
        break;
    case RF_APPLY_STOPPED:
        batch_name = "stopped";
        break;
    default:
        batch_name = "idle";
        break;
    }
    json_writer_t writer;
    if (!web_json_init(&writer, buffer, capacity))
    {
        return false;
    }
    (void)json_write_object_begin(&writer);
    web_json_string(&writer, "State", state_name(group.state));
    web_json_uint32(&writer, "Line", (uint32_t)((unsigned)group.line));
    web_json_uint32(&writer, "Feeder", (uint32_t)((unsigned)group.feeder));
    web_json_uint32(&writer, "GroupId", (uint32_t)((unsigned)group.group_id));
    web_json_uint32(&writer, "WritesAcked",
                    (uint32_t)((unsigned)group.writes_acked));
    web_json_uint32(&writer, "ExpectedCRC",
                    (uint32_t)((unsigned)group.expected_crc));
    web_json_bool(&writer, "MatchesDesired", rf_group_matches_config());
    web_json_bool(&writer, "HasReport", group.has_report);
    web_json_uint32(&writer, "MHState",
                    (uint32_t)((unsigned)group.report.state));
    web_json_uint32(&writer, "MemberBitmap",
                    (uint32_t)((unsigned)group.report.member_bitmap));
    web_json_uint32(&writer, "Reason",
                    (uint32_t)((unsigned)group.report.reason));
    web_json_uint32(&writer, "ReportedCRC",
                    (uint32_t)((unsigned)group.report.config_crc));
    web_json_uint32(&writer, "Attempts",
                    (uint32_t)((unsigned)group.report.attempts));
    web_json_string(&writer, "BatchState", batch_name);
    web_json_uint32(&writer, "Targets", (uint32_t)((unsigned)batch.targets));
    web_json_uint32(&writer, "Applied", (uint32_t)((unsigned)batch.applied));
    web_json_uint32(&writer, "Skipped", (uint32_t)((unsigned)batch.skipped));
    web_json_uint32(&writer, "BatchLine", (uint32_t)((unsigned)batch.line));
    web_json_bool(&writer, "GroupStarted", batch.group_started);
    web_json_bool(&writer, "SaveBlocked", !(rf_apply_can_save()));
    web_json_bool(&writer, "InventoryPending", batch.inventory_pending);
    web_json_bool(&writer, "CanAbort", rf_apply_can_abort());
    web_json_string(&writer, "InventoryState",
                    inventory_state_name(rf_inventory_get_status()));
    web_json_bool(&writer, "InventoryRecoveryPending",
                  rf_inventory_has_pending());
    web_json_string(&writer, "StopReason", stop_reason_name(batch.stop_reason));
    web_json_string(&writer, "WaitReason", wait.reason);
    web_json_uint32(&writer, "WaitTotalMs",
                    (uint32_t)((unsigned)wait.total_ms));
    web_json_uint32(&writer, "WaitReasonMs",
                    (uint32_t)((unsigned)wait.reason_ms));
    (void)json_write_object_end(&writer);

    return web_json_finish(&writer, buffer, length);
}

/*** end of file ***/
