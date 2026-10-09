/*
 * power_board_control.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Read, write and verify SCP settings; track one-shot command outcomes.
 */

#include "power_board_control.h"
#include "power_board_scp.h"
#include "rf_comm.h"
#include "scp_endian.h"
#include "stm32u3xx_hal.h"
#include <string.h>

/* Cooperative RF process/shell context only; no ISR accesses. */
static power_board_control_status_t status;
static power_settings_request_t desired;
static bool request_pending;
static bool write_after_read;
static bool verifying;
static bool verification_report_ready;
static bool awaiting_command;
static bool cancel_verification_pending;
static uint32_t command_ack_ms;
static uint8_t request_cmd;
static uint8_t request_type;
static uint8_t sent_command;
static uint32_t write_ms;
static uint32_t verify_delay_ms;

static void request_done(scp_cmd_result_t result, const scp_packet_t *packet);

void power_board_control_init(void)
{
    (void)memset(&status, 0, sizeof(status));
    (void)memset(&desired, 0, sizeof(desired));
    request_pending = false;
    write_after_read = false;
    verifying = false;
    verification_report_ready = false;
    awaiting_command = false;
    cancel_verification_pending = false;
    command_ack_ms = 0U;
}

void power_board_control_hub_restarted(void)
{
    if (POWER_SETTINGS_IDLE != status.settings_state)
    {
        status.settings_state = POWER_SETTINGS_RESTARTED;
    }
    if (POWER_COMMAND_IDLE != status.command_state)
    {
        status.command_state = POWER_COMMAND_RESTARTED;
    }
    status.has_config = false;
    status.has_result = false;
    status.has_raw = false;
    status.has_command_ack = false;
    request_pending = false;
    write_after_read = false;
    verifying = false;
    verification_report_ready = false;
    awaiting_command = false;
    cancel_verification_pending = false;
    command_ack_ms = 0U;
}

bool power_board_control_get_status(power_board_control_status_t *out)
{
    if (NULL == out)
    {
        return false;
    }
    *out = status;
    return true;
}

static bool send_request(uint8_t cmd, uint8_t type,
                         const uint8_t *data, uint8_t length)
{
    scp_packet_t packet = {.cmd = cmd, .type = type, .data_len = length};

    if (request_pending || !rf_comm_can_load_inventory() || !scp_is_free())
    {
        return false;
    }
    if (cancel_verification_pending && (RF_SCP_CMD_PWR_RESULT != cmd))
    {
        return false;
    }
    if (0U != length)
    {
        (void)memcpy(packet.data, data, length);
    }
    if (!scp_send_request(&packet, request_done))
    {
        return false;
    }
    request_cmd = cmd;
    request_type = type;
    request_pending = true;
    status.error_code = 0U;
    return true;
}

bool power_board_read_settings(void)
{
    return send_request(RF_SCP_CMD_PWR_CFG2, SCP_TYPE_GET, NULL, 0U);
}

static bool settings_active(void)
{
    return write_after_read || verifying;
}

static bool valid_setting(uint8_t value, uint8_t minimum, uint8_t maximum)
{
    return (0U == value) || (255U == value) ||
           ((minimum <= value) && (maximum >= value));
}

bool power_board_write_settings(const power_settings_request_t *request)
{
    if ((NULL == request) || settings_active() || awaiting_command ||
        (0U == request->mask) || (0U != (request->mask & ~0x6200U)) ||
        ((0U != (request->mask & POWER_SETTING_RATE)) &&
         !valid_setting(request->rate_permille, 20U, 200U)) ||
        ((0U != (request->mask & POWER_SETTING_CAPACITY)) &&
         ((7U > request->capacity_ah) || (54U < request->capacity_ah))) ||
        ((0U != (request->mask & POWER_SETTING_PERIOD)) &&
         ((1U > request->period_sec) || (10U < request->period_sec))))
    {
        return false;
    }
    if (!power_board_read_settings())
    {
        return false;
    }
    desired = *request;
    status.settings_state = POWER_SETTINGS_READING;
    status.error_code = 0U;
    status.rejected_fields = 0U;
    write_after_read = true;
    return true;
}

static bool send_settings(void)
{
    uint8_t data[16] = {0};

    if ((0U == status.config[0]) &&
        ((0U == (desired.mask & POWER_SETTING_CAPACITY)) ||
         (7U > desired.capacity_ah) || (54U < desired.capacity_ah)))
    {
        status.settings_state = POWER_SETTINGS_REJECTED;
        status.rejected_fields = POWER_SETTING_CAPACITY;
        write_after_read = false;
        return false;
    }
    data[0] = status.config[1];
    data[1] = (uint8_t)desired.mask;
    data[2] = (uint8_t)(desired.mask >> 8U);
    data[10] = desired.rate_permille;
    data[14] = desired.capacity_ah;
    data[15] = desired.period_sec;
    if (0U != (desired.mask & POWER_SETTING_PERIOD))
    {
        /* Both operands occupy at most one byte. */
        data[15] = (uint8_t)(data[15] | (status.config[14] & 0x80U));
    }
    if (!send_request(RF_SCP_CMD_PWR_CFG2, SCP_TYPE_SET, data, sizeof(data)))
    {
        return false;
    }
    write_after_read = false;
    status.settings_state = POWER_SETTINGS_WRITING;
    return true;
}

static void check_settings(void)
{
    const uint32_t now_ms = HAL_GetTick();
    const uint32_t elapsed = now_ms - write_ms;
    if (!verifying || !verification_report_ready || !status.has_config ||
        (elapsed < verify_delay_ms))
    {
        return;
    }
    if (status.config[1] != status.written_gen)
    {
        status.settings_state = POWER_SETTINGS_CONFLICT;
        verifying = false;
        return;
    }
    if (!status.echo_valid || (status.echo_gen != status.written_gen))
    {
        return;
    }
    if ((0U != (status.mask1 & 0xDFU)) || (0U != (status.mask2 & 0x7FU)))
    {
        status.settings_state = POWER_SETTINGS_REJECTED;
        verifying = false;
        return;
    }
    const bool capacity = (0U != (desired.mask & POWER_SETTING_CAPACITY));
    const bool rate = (0U != (desired.mask & POWER_SETTING_RATE)) &&
                     (0U != desired.rate_permille) &&
                     (255U != desired.rate_permille);
    status.settings_state = POWER_SETTINGS_WAIT_APPLY;
    if (capacity || rate)
    {
        power_board_snapshot_t power;
        (void)power_board_get_snapshot(now_ms, &power);
        const uint8_t verdict = (power.summary.flags2 >> 4U) & 7U;
        if ((0U == (power.valid_fields & POWER_VALID_SUMMARY)) ||
            ((0U != verdict) && (2U != verdict)) ||
            (capacity && ((power.summary.capacity_ah != desired.capacity_ah) ||
             (power.received_age_ms > elapsed - verify_delay_ms))) ||
            (rate && (power.summary.charge_rate_permille != desired.rate_permille)))
        {
            return;
        }
    }
    const bool unset_rate = (0U != (desired.mask & POWER_SETTING_RATE)) && !rate;
    status.settings_state = unset_rate ? POWER_SETTINGS_ECHO_ACCEPTED :
                                        POWER_SETTINGS_APPLIED;
    verifying = false;
}

static void store_config(const rf_scp_message_t *message)
{
    (void)memcpy(status.config, message->body.power_config.block, 16U);
    status.has_config = true;
    status.echo_valid = message->body.power_config.echo_valid;
    status.echo_gen = message->body.power_config.echo_gen;
    status.mask1 = message->body.power_config.mask1;
    status.mask2 = message->body.power_config.mask2;
    status.last_rejected = message->body.power_config.rejected;
}

bool power_board_handle_command_result(const rf_scp_message_t *message)
{
    if ((NULL == message) || (RF_SCP_CMD_PWR_RESULT != message->cmd) ||
        ((SCP_TYPE_SET != message->type) && (SCP_TYPE_ACK != message->type)))
    {
        return false;
    }
    status.has_result = true;
    status.result_command = message->body.command_result.command;
    status.result_seq = message->body.command_result.seq;
    status.result = message->body.command_result.result;
    status.transmissions = message->body.command_result.transmissions;
    status.command_flags = message->body.command_result.flags;
    if (cancel_verification_pending && (SCP_TYPE_ACK == message->type))
    {
        cancel_verification_pending = false;
        if ((0U != status.transmissions) &&
            (0U == (status.command_flags & 0x0EU)))
        {
            status.command_state = POWER_COMMAND_CANCEL_NO_EFFECT;
            awaiting_command = status.has_command_ack && (255U == status.result);
            return true;
        }
        if ((0U == status.transmissions) &&
            (0U == (status.command_flags & 0x0EU)))
        {
            status.command_state = POWER_COMMAND_CANCEL_NO_EFFECT;
            awaiting_command = false;
            return true;
        }
    }
    if (!status.has_command_ack || (5U != status.result_command))
    {
        return true;
    }
    if (0U != (status.command_flags & 2U))
    {
        status.command_state = POWER_COMMAND_MONITORING;
    }
    else if ((0U == status.result) ||
             (0U != (status.command_flags & 8U)))
    {
        status.command_state = POWER_COMMAND_APPLIED;
        awaiting_command = false;
    }
    else if (0U != (status.command_flags & 4U))
    {
        status.command_state = POWER_COMMAND_CANCELLED;
        awaiting_command = false;
    }
    else if (255U != status.result)
    {
        status.command_state = POWER_COMMAND_REJECTED;
        awaiting_command = false;
    }
    else if (((0U == (status.command_flags & 0x10U)) &&
              (1U <= status.transmissions)) ||
             ((SCP_TYPE_ACK == message->type) &&
              (300000U <= (uint32_t)(HAL_GetTick() - command_ack_ms))))
    {
        status.command_state = POWER_COMMAND_NO_RESPONSE;
        awaiting_command = false;
    }
    else
    {
        status.command_state = POWER_COMMAND_ACCEPTED;
    }
    return true;
}

bool power_board_handle_raw(const rf_scp_message_t *message)
{
    if ((NULL == message) || (RF_SCP_CMD_PWR_TELEMETRY != message->cmd) ||
        ((SCP_TYPE_SET != message->type) && (SCP_TYPE_ACK != message->type)))
    {
        return false;
    }
    (void)memcpy(status.raw, message->body.telemetry.bytes, sizeof(status.raw));
    status.has_raw = true;
    return true;
}

static void request_failed(scp_cmd_result_t result,
                           const rf_scp_message_t *message)
{
    status.error_code = (NULL != message) ? message->body.error.code : 0U;
    if (RF_SCP_CMD_PWR_CFG2 == request_cmd)
    {
        write_after_read = false;
        if (verifying && (SCP_TYPE_GET == request_type))
        {
            /* A failed verification GET does not reject an accepted SET. */
            status.settings_state = POWER_SETTINGS_WAIT_APPLY;
            verification_report_ready = false;
            return;
        }
        if (SCP_TYPE_SET == request_type)
        {
            status.settings_state = (SCP_CMD_ERR == result) ?
                POWER_SETTINGS_REJECTED : POWER_SETTINGS_UNCERTAIN;
            if ((NULL != message) && (RF_SCP_ERR_BUSY == status.error_code) &&
                (1U == message->body.error.extra_len))
            {
                status.settings_state = POWER_SETTINGS_CONFLICT;
            }
            if ((NULL != message) && (2U == message->body.error.extra_len))
            {
                status.rejected_fields =
                    scp_unpack_u16(message->body.error.extra);
            }
        }
        else
        {
            status.settings_state = POWER_SETTINGS_REJECTED;
        }
        verifying = false;
    }
    else if (RF_SCP_CMD_PWR_COMMAND == request_cmd)
    {
        status.command_state = (SCP_CMD_ERR == result) ?
            POWER_COMMAND_REJECTED : POWER_COMMAND_UNCERTAIN;
        if (5U == sent_command)
        {
            awaiting_command = false;
        }
    }
    else
    {
        /* Failed read preserves the last valid report/raw block. */
    }
}

static void settings_written(const rf_scp_message_t *message)
{
    status.written_gen = message->body.config_ack.gen;
    status.settings_state = POWER_SETTINGS_WAIT_ECHO;
    verifying = true;
    verification_report_ready = false;
    write_ms = HAL_GetTick();
    uint8_t period = status.config[14] & 0x3FU;

    if (0U != (desired.mask & POWER_SETTING_PERIOD))
    {
        period = desired.period_sec;
    }
    if ((1U > period) || (10U < period))
    {
        period = 1U;
    }
    verify_delay_ms = ((uint32_t)period * 3U + 6U) * 1000U;
    if (20000U > verify_delay_ms)
    {
        verify_delay_ms = 20000U;
    }
}

static void command_accepted(const rf_scp_message_t *message)
{
    if ((sent_command != message->body.command_ack.command) ||
        ((0U == sent_command) && (0U != message->body.command_ack.seq)))
    {
        status.command_state = POWER_COMMAND_UNCERTAIN;
        return;
    }
    if (5U == sent_command)
    {
        status.command_seq = message->body.command_ack.seq;
        status.has_command_ack = true;
        status.command_state = POWER_COMMAND_ACCEPTED;
        awaiting_command = true;
        command_ack_ms = HAL_GetTick();
        status.has_result = false; /* Only reports after the ACK are evidence. */
    }
    else
    {
        /* Cancel ACK does not prove that counters were not reset. */
        cancel_verification_pending = true;
        status.command_state = POWER_COMMAND_UNCERTAIN;
    }
}

static void request_done(scp_cmd_result_t result, const scp_packet_t *packet)
{
    rf_scp_message_t message;

    request_pending = false;
    if (SCP_CMD_RESTARTED == result)
    {
        power_board_control_hub_restarted();
        return;
    }
    const bool decoded = (NULL != packet) &&
        (RF_CMD_OK == rf_scp_decode_message(packet, &message));
    if ((SCP_CMD_OK != result) || !decoded)
    {
        cancel_verification_pending = false;
        request_failed(result, decoded ? &message : NULL);
        return;
    }
    if (RF_SCP_CMD_PWR_CFG2 == request_cmd)
    {
        if (SCP_TYPE_GET == request_type)
        {
            store_config(&message);
            verification_report_ready = verifying;
            if (write_after_read)
            {
                (void)send_settings();
            }
            else
            {
                check_settings();
            }
        }
        else
        {
            settings_written(&message);
        }
    }
    else if (RF_SCP_CMD_PWR_COMMAND == request_cmd)
    {
        command_accepted(&message);
    }
    else if (RF_SCP_CMD_PWR_RESULT == request_cmd)
    {
        (void)power_board_handle_command_result(&message);
    }
    else
    {
        (void)power_board_handle_raw(&message);
    }
}

void power_board_control_process(uint32_t now_ms)
{
    if (request_pending)
    {
        return;
    }
    if (cancel_verification_pending)
    {
        if (!power_board_read_command_result())
        {
            return;
        }
    }
    else if (write_after_read)
    {
        (void)send_settings();
    }
    else if (verifying && (POWER_SETTINGS_WAIT_ECHO == status.settings_state) &&
             ((uint32_t)(now_ms - write_ms) >= verify_delay_ms))
    {
        if (power_board_read_settings())
        {
            status.settings_state = POWER_SETTINGS_WAIT_APPLY;
        }
    }
    else if (verifying && (POWER_SETTINGS_WAIT_APPLY == status.settings_state))
    {
        check_settings();
    }
    else
    {
        /* No speculative background polls; operator GET refreshes reports. */
    }
}

static bool send_command(uint8_t command, uint8_t parameter)
{
    const uint8_t data[2] = {command, parameter};
    const bool battery_change =
        ((0U != (desired.mask & POWER_SETTING_CAPACITY)) &&
         (7U <= desired.capacity_ah) && (54U >= desired.capacity_ah)) ||
        ((0U != (desired.mask & POWER_SETTING_RATE)) &&
         (20U <= desired.rate_permille) && (200U >= desired.rate_permille));

    if ((5U == command) && (settings_active() || awaiting_command ||
        (POWER_SETTINGS_UNCERTAIN == status.settings_state) ||
        (battery_change &&
         (POWER_SETTINGS_APPLIED != status.settings_state))))
    {
        return false;
    }
    if (!send_request(RF_SCP_CMD_PWR_COMMAND, SCP_TYPE_SET, data, sizeof(data)))
    {
        return false;
    }
    sent_command = command;
    if (5U == command)
    {
        status.command_state = POWER_COMMAND_SENDING;
        status.has_result = false;
        status.has_command_ack = false;
    }
    return true;
}

bool power_board_battery_replaced(void)
{
    return send_command(5U, 0xA5U);
}

bool power_board_cancel_command(void)
{
    return send_command(0U, 0U);
}

bool power_board_read_command_result(void)
{
    return send_request(RF_SCP_CMD_PWR_RESULT, SCP_TYPE_GET, NULL, 0U);
}

bool power_board_read_raw(void)
{
    return send_request(RF_SCP_CMD_PWR_TELEMETRY, SCP_TYPE_GET, NULL, 0U);
}

/*** end of file ***/
