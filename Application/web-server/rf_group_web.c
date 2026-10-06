/*
 * rf_group_web.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Adapt web apply requests and report verified RF group state.
 */

#include "rf_group_web.h"
#include "rf_group.h"
#include "modem_types.h"
#include "xprintf.h"

static bool parse_byte(const char **cursor, uint8_t *value)
{
    const char *text = *cursor;
    uint16_t number = 0U;

    if (('0' > *text) || ('9' < *text))
    {
        return false;
    }
    do
    {
        if (25U < number)
        {
            return false;
        }
        number = (uint16_t)(number * 10U + (uint16_t)(*text - '0'));
        if (255U < number)
        {
            return false;
        }
        text++;
    } while (('0' <= *text) && ('9' >= *text));
    *value = (uint8_t)number;
    *cursor = text;
    return true;
}

rf_web_apply_result_t rf_web_start_apply(const char *suffix)
{
    uint8_t line;
    uint8_t group_id;

    if ((NULL == suffix) || !parse_byte(&suffix, &line) ||
        (0U == line) || (MAX_POWER_LINE_COUNT < line) || ('/' != *suffix))
    {
        return RF_WEB_APPLY_INVALID;
    }
    suffix++;
    if (!parse_byte(&suffix, &group_id) || ('\0' != *suffix))
    {
        return RF_WEB_APPLY_INVALID;
    }
    return rf_group_start((size_t)line - 1U, group_id) ?
        RF_WEB_APPLY_STARTED : RF_WEB_APPLY_NOT_STARTED;
}

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

bool rf_group_status_json_build(char *buffer, size_t capacity, size_t *length)
{
    rf_group_status_t group;

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
    const uint32_t count = xsnprintf(buffer, (unsigned int)capacity,
        "{\"State\":\"%s\",\"Line\":%u,\"Feeder\":%u,\"GroupId\":%u,"
        "\"WritesAcked\":%u,\"ExpectedCRC\":%u,\"MatchesDesired\":%s,"
        "\"HasReport\":%s,\"MHState\":%u,\"MemberBitmap\":%u,"
        "\"Reason\":%u,\"ReportedCRC\":%u,\"Attempts\":%u}",
        state_name(group.state), (unsigned)group.line, (unsigned)group.feeder,
        (unsigned)group.group_id, (unsigned)group.writes_acked,
        (unsigned)group.expected_crc,
        rf_group_matches_config() ? "true" : "false",
        group.has_report ? "true" : "false",
        (unsigned)group.report.state, (unsigned)group.report.member_bitmap,
        (unsigned)group.report.reason, (unsigned)group.report.config_crc,
        (unsigned)group.report.attempts);

    if ((size_t)count >= capacity - 1U)
    {
        buffer[0] = '\0';
        return false;
    }
    *length = (size_t)count;
    return true;
}

/*** end of file ***/
