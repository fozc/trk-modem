/*
 * rf_group.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Check group identity, WRITE three identical blocks, COMMIT and verify.
 */

#include "rf_group.h"
#include "rf_comm.h"
#include "rf_inventory.h"
#include "rf_config.h"
#include "stm32u3xx_hal.h"
#include <string.h>

#define GROUP_STATUS_PERIOD_MS 5000U

typedef struct
{
    rf_group_status_t status;
    rf_feeder_config_t block;
    uint8_t zone;
    bool command_pending;
    bool write_sent;
    bool commit_sent;
    bool group_known;
    uint8_t pending_command;
    uint32_t poll_ms;
} rf_group_t;

/* All accesses run in cooperative process/shell context, never the ISR. */
static rf_group_t group;

void rf_group_init(void)
{
    (void)memset(&group, 0, sizeof(group));
}

void rf_group_hub_restarted(void)
{
    if (RF_GROUP_IDLE != group.status.state)
    {
        group.status.state = RF_GROUP_RESTARTED;
    }
    group.command_pending = false;
    group.write_sent = false;
    group.group_known = false;
    group.status.has_report = false;
}

bool rf_group_get_status(rf_group_status_t *out)
{
    if (NULL == out)
    {
        return false;
    }
    *out = group.status;
    return true;
}

static bool can_start(void)
{
    switch (group.status.state)
    {
        case RF_GROUP_IDLE:
        case RF_GROUP_APPLIED:
        case RF_GROUP_FAILED:
        case RF_GROUP_CANCELLED:
        case RF_GROUP_RESTARTED:
            return !group.command_pending;
        case RF_GROUP_ID_IN_USE:
            return !group.command_pending &&
                (!group.status.has_report ||
                 ((1U != group.status.report.state) &&
                  (2U != group.status.report.state)));
        case RF_GROUP_MISMATCH:
            return !group.command_pending && group.status.has_report &&
                   (3U == group.status.report.state) &&
                   (7U == group.status.report.member_bitmap);
        case RF_GROUP_UNCERTAIN:
            return !group.command_pending && !group.write_sent &&
                   !group.commit_sent;
        default:
            return false;
    }
}

bool rf_group_matches_config(void)
{
    if ((0U == group.status.line) ||
        (MAX_POWER_LINE_COUNT < group.status.line))
    {
        return false;
    }
    const rf_feeder_t *feeder =
        rf_store_get((feeder_id_t)(group.status.line - 1U));

    return (NULL != feeder) && feeder->in_use &&
        (feeder->config.fider_id == group.status.feeder) &&
        (feeder->config.zone_id == group.zone) &&
        (rf_config_writable_crc(&feeder->config) == group.status.expected_crc) &&
        (0 == memcmp(feeder->r_eui64, group.status.members[0], 8U)) &&
        (0 == memcmp(feeder->s_eui64, group.status.members[1], 8U)) &&
        (0 == memcmp(feeder->t_eui64, group.status.members[2], 8U));
}

bool rf_group_is_active(void)
{
    if ((RF_GROUP_ID_IN_USE == group.status.state) && group.status.has_report &&
        ((1U == group.status.report.state) || (2U == group.status.report.state)))
    {
        return true;
    }
    return !can_start();
}

static bool bindings_match(const rf_group_t *job)
{
    for (size_t index = 0U; index < 3U; index++)
    {
        const uint8_t source = (uint8_t)((job->status.feeder << 2U) |
                                         (uint8_t)(index + 1U));
        rf_inventory_entry_t entry;

        if (!rf_inventory_get_binding(source, &entry) ||
            (entry.zone != job->zone) ||
            (0 != memcmp(entry.eui64, job->status.members[index], 8U)))
        {
            return false;
        }
    }
    return true;
}

bool rf_group_start(size_t line_index, uint8_t group_id)
{
    rf_group_t candidate = {0};
    const bool retry_write = (RF_GROUP_UNCERTAIN == group.status.state) &&
        group.write_sent && !group.commit_sent && !group.command_pending;

    /* BOLATeX BQ-11: group id zero is not a valid commit identity; every
     * COMMIT must carry a fresh non-zero value. */
    if ((0U == group_id) || (MAX_POWER_LINE_COUNT <= line_index) ||
        (!can_start() && !retry_write) || !scp_is_free() ||
        !rf_inventory_is_loaded() ||
        !rf_comm_can_load_inventory())
    {
        return false;
    }
    const rf_feeder_t *feeder = rf_store_get((feeder_id_t)line_index);

    if ((NULL == feeder) || !feeder->in_use ||
        !rf_config_for_write(&feeder->config, NULL, &candidate.block))
    {
        return false;
    }
    (void)memcpy(candidate.status.members[0], feeder->r_eui64, 8U);
    (void)memcpy(candidate.status.members[1], feeder->s_eui64, 8U);
    (void)memcpy(candidate.status.members[2], feeder->t_eui64, 8U);
    candidate.status.feeder = feeder->config.fider_id;
    candidate.status.line = (uint8_t)(line_index + 1U);
    if (!rf_inventory_epoch_ready(candidate.status.feeder))
    {
        return false;
    }
    candidate.zone = feeder->config.zone_id;
    for (size_t index = 0U; index < 3U; index++)
    {
        if (rf_eui64_is_zero(candidate.status.members[index]))
        {
            return false;
        }
        for (size_t other = 0U; other < index; other++)
        {
            if (0 == memcmp(candidate.status.members[index],
                            candidate.status.members[other], 8U))
            {
                return false;
            }
        }
    }
    if (!bindings_match(&candidate))
    {
        return false;
    }
    candidate.block.zone_id = 0U;
    candidate.block.phase_id = 0U;
    candidate.status.expected_crc = rf_config_writable_crc(&candidate.block);
    candidate.status.group_id = group_id;
    if (retry_write)
    {
        /* BQ-11: rewrite the same frozen members/block before COMMIT.
         * Reuse the checked ID: it has not been committed to the MH yet.
         * Keep the lock if the operator selects any other job or settings.
         */
        if ((candidate.status.line != group.status.line) ||
            (group_id != group.status.group_id) ||
            (candidate.zone != group.zone) ||
            (0 != memcmp(candidate.status.members, group.status.members,
                         sizeof(group.status.members))) ||
            (0 != memcmp(&candidate.block, &group.block, sizeof(group.block))))
        {
            return false;
        }
        candidate.status.state = RF_GROUP_WRITING;
        candidate.write_sent = true;
    }
    else
    {
        candidate.status.state = RF_GROUP_CHECKING;
    }
    group = candidate;
    return true;
}

bool rf_group_handle_status(const rf_scp_message_t *message)
{
    if ((NULL == message) ||
        (!group.commit_sent && (RF_GROUP_ID_IN_USE != group.status.state)) ||
        !(((RF_SCP_CMD_CFG_STATUS_NOTIFY == message->cmd) &&
           (SCP_TYPE_SET == message->type)) ||
          ((RF_SCP_CMD_CFG_STATUS_GET == message->cmd) &&
           (SCP_TYPE_ACK == message->type))) ||
        (group.status.group_id != message->body.config.group_id) ||
        (4U < message->body.config.state) ||
        (7U < message->body.config.member_bitmap) ||
        (RF_GROUP_APPLIED == group.status.state) ||
        (RF_GROUP_FAILED == group.status.state) ||
        ((RF_GROUP_MISMATCH == group.status.state) &&
         (SCP_TYPE_ACK != message->type)) ||
        (RF_GROUP_CANCELLED == group.status.state))
    {
        return false;
    }
    if (RF_GROUP_RESTARTED == group.status.state)
    {
        if ((RF_SCP_CMD_CFG_STATUS_GET != message->cmd) ||
            (SCP_TYPE_ACK != message->type) ||
            (4U != message->body.config.state) ||
            (8U != message->body.config.reason))
        {
            return false;
        }
        group.status.report = message->body.config;
        group.status.has_report = true;
        return true;
    }
    if (RF_GROUP_ID_IN_USE == group.status.state)
    {
        /* Observe the existing MH job, never apply it to this unsent job. */
        group.status.report = message->body.config;
        group.status.has_report = true;
        return true;
    }
    group.group_known = true;
    group.status.report = message->body.config;
    group.status.has_report = true;
    switch (message->body.config.state)
    {
        case 1U:
        case 2U:
            /* Delayed progress must not undo a terminal result. */
            if ((RF_GROUP_COMMITTING != group.status.state) &&
                (RF_GROUP_ABORTING != group.status.state))
            {
                group.status.state = RF_GROUP_WAITING;
            }
            break;
        case 3U:
            group.status.state =
                ((7U == message->body.config.member_bitmap) &&
                 (group.status.expected_crc == message->body.config.config_crc))
                ? RF_GROUP_APPLIED : RF_GROUP_MISMATCH;
            break;
        case 4U:
            group.status.state = (10U == message->body.config.reason) ?
                                 RF_GROUP_CANCELLED : RF_GROUP_FAILED;
            break;
        default:
            group.status.state = RF_GROUP_UNCERTAIN;
            break;
    }
    if ((3U == message->body.config.state) ||
        (4U == message->body.config.state))
    {
        rf_inventory_config_finished(group.status.feeder,
            (3U == message->body.config.state) ? 0U :
                                                message->body.config.reason);
    }
    return true;
}

static bool handle_status_response(const scp_packet_t *packet)
{
    rf_scp_message_t message;

    return (NULL != packet) &&
        (RF_CMD_OK == rf_scp_decode_message(packet, &message)) &&
        rf_group_handle_status(&message);
}

/* Keep status decode scratch out of WRITE/COMMIT callback paths. */
static __attribute__((noinline)) void check_group_response(
    scp_cmd_result_t result, const scp_packet_t *packet)
{
    rf_scp_message_t message;
    if ((SCP_CMD_ERR == result) && (NULL != packet) &&
        (1U <= packet->data_len) &&
        (RF_SCP_ERR_INVALID_PARAM == packet->data[0]))
    {
        group.status.state = RF_GROUP_WRITING;
    }
    else if ((SCP_CMD_OK == result) && (NULL != packet) &&
             (RF_CMD_OK == rf_scp_decode_message(packet, &message)) &&
             (group.status.group_id == message.body.config.group_id))
    {
        group.status.state = (0U == message.body.config.state) ?
                             RF_GROUP_WRITING : RF_GROUP_ID_IN_USE;
        group.status.report = message.body.config;
        group.status.has_report = true;
        group.poll_ms = HAL_GetTick();
    }
    else
    {
        group.status.state = RF_GROUP_UNCERTAIN;
    }
}

static void command_done(scp_cmd_result_t result, const scp_packet_t *packet)
{
    const rf_group_state_t state = group.status.state;

    group.command_pending = false;
    if (SCP_CMD_RESTARTED == result)
    {
        rf_group_hub_restarted();
        return;
    }
    if (RF_GROUP_CHECKING == state)
    {
        check_group_response(result, packet);
        return;
    }
    if (RF_GROUP_RESTARTED == state)
    {
        if (SCP_CMD_OK == result)
        {
            (void)handle_status_response(packet);
        }
        group.commit_sent = false;
        return;
    }
    if (RF_GROUP_ID_IN_USE == state)
    {
        /* A failed probe does not prove that the observed peer stopped. */
        if (SCP_CMD_OK == result)
        {
            (void)handle_status_response(packet);
        }
        return;
    }
    if ((RF_SCP_CMD_CFG_ABORT == group.pending_command) &&
        (SCP_CMD_OK == result))
    {
        group.status.state = RF_GROUP_CANCELLED;
        rf_inventory_config_finished(group.status.feeder, 10U);
        return;
    }
    if ((RF_GROUP_APPLIED == state) || (RF_GROUP_FAILED == state) ||
        (RF_GROUP_CANCELLED == state) ||
        ((RF_GROUP_MISMATCH == state) &&
         ((RF_SCP_CMD_CFG_STATUS_GET != group.pending_command) ||
          (SCP_CMD_OK != result))))
    {
        return;
    }
    if (SCP_CMD_OK != result)
    {
        group.status.state = RF_GROUP_UNCERTAIN;
        return;
    }
    if (RF_GROUP_WRITING == state)
    {
        group.group_known = true;
        group.status.writes_acked++;
        if (3U == group.status.writes_acked)
        {
            group.status.state = RF_GROUP_COMMITTING;
        }
    }
    else if (RF_GROUP_COMMITTING == state)
    {
        group.group_known = true;
        group.status.state = RF_GROUP_WAITING;
        group.poll_ms = HAL_GetTick();
    }
    else if (RF_GROUP_ABORTING == state)
    {
        group.status.state = RF_GROUP_CANCELLED;
    }
    else if (!handle_status_response(packet))
    {
        group.status.state = RF_GROUP_UNCERTAIN;
    }
    else
    {
        /* Status GET uses the same report path as an unsolicited SET. */
    }
}

static bool send_request(uint8_t command)
{
    scp_packet_t request = {.cmd = command};

    if (RF_SCP_CMD_CFG_WRITE == command)
    {
        request.type = SCP_TYPE_SET;
        request.data_len = 104U;
        (void)memcpy(request.data,
                     group.status.members[group.status.writes_acked], 8U);
        (void)memcpy(&request.data[8], &group.block, sizeof(group.block));
    }
    else
    {
        request.type = (RF_SCP_CMD_CFG_STATUS_GET == command) ?
                       SCP_TYPE_GET : SCP_TYPE_SET;
        request.data_len = 1U;
        request.data[0] = group.status.group_id;
    }
    if (!scp_send_request(&request, command_done))
    {
        return false;
    }
    group.command_pending = true;
    group.pending_command = command;
    if (RF_SCP_CMD_CFG_WRITE == command)
    {
        group.write_sent = true;
    }
    if (RF_SCP_CMD_CFG_COMMIT == command)
    {
        group.commit_sent = true;
    }
    return true;
}

bool rf_group_abort(void)
{
    if (!group.write_sent && !group.commit_sent &&
        ((RF_GROUP_CHECKING == group.status.state) ||
         (RF_GROUP_UNCERTAIN == group.status.state)))
    {
        group.status.state = RF_GROUP_CANCELLED;
        return true;
    }
    if ((!group.group_known && !group.write_sent) || !scp_is_free() ||
        group.command_pending ||
        ((RF_GROUP_WAITING != group.status.state) &&
         (RF_GROUP_WRITING != group.status.state) &&
         (RF_GROUP_COMMITTING != group.status.state) &&
         (RF_GROUP_UNCERTAIN != group.status.state) &&
         (RF_GROUP_MISMATCH != group.status.state)))
    {
        return false;
    }
    if (!send_request(RF_SCP_CMD_CFG_ABORT))
    {
        return false;
    }
    group.status.state = RF_GROUP_ABORTING;
    return true;
}

void rf_group_process(uint32_t now_ms)
{
    if (group.command_pending || !scp_is_free())
    {
        return;
    }
    switch (group.status.state)
    {
        case RF_GROUP_CHECKING:
            (void)send_request(RF_SCP_CMD_CFG_STATUS_GET);
            break;
        case RF_GROUP_WRITING:
        case RF_GROUP_COMMITTING:
            if (!rf_inventory_is_loaded() || !bindings_match(&group) ||
                !rf_inventory_epoch_ready(group.status.feeder))
            {
                group.status.state = RF_GROUP_UNCERTAIN;
            }
            else
            {
                (void)send_request((RF_GROUP_WRITING == group.status.state)
                    ? RF_SCP_CMD_CFG_WRITE : RF_SCP_CMD_CFG_COMMIT);
            }
            break;
        case RF_GROUP_WAITING:
        case RF_GROUP_ID_IN_USE:
            if (rf_group_is_active() &&
                (GROUP_STATUS_PERIOD_MS <=
                 (uint32_t)(now_ms - group.poll_ms)))
            {
                if (send_request(RF_SCP_CMD_CFG_STATUS_GET))
                {
                    group.poll_ms = now_ms;
                }
            }
            break;
        case RF_GROUP_RESTARTED:
            if (group.commit_sent && rf_comm_can_load_inventory())
            {
                (void)send_request(RF_SCP_CMD_CFG_STATUS_GET);
            }
            break;
        default:
            break;
    }
}

/*** end of file ***/
