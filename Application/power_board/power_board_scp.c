/*
 * power_board_scp.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Own SCP power-board data; I2C snapshots never enter this model.
 */

#include "power_board_scp.h"
#include "power_board_control.h"
#include "elog.h"
#include "shell.h"
#include "stm32u3xx_hal.h"
#include <string.h>

#define SUMMARY_TIMEOUT_MS 30000U

/* Cooperative RF process and consumer context only. No ISR access. */
static power_board_snapshot_t snapshot;
static uint32_t summary_ms;
static uint32_t alarm_ms;
static bool summary_current;
static bool alarms_current;
static bool have_alarm_seq;
static bool have_restart_session;
static uint8_t restart_session;

static int power_board_scp_shell(int argc, char **argv);

void power_board_scp_init(void)
{
    power_board_control_init();
    (void)memset(&snapshot, 0, sizeof(snapshot));
    summary_current = false;
    alarms_current = false;
    have_alarm_seq = false;
    have_restart_session = false;
    (void)shell_register_command(&(shell_cmd_t)
    {
        .cmd = "pwrboard",
        .desc = "SCP power summary, quality and alarms\r\n"
                "\tpwrboard [show] - current SCP snapshot\r\n"
                "\tpwrboard alarms - alarm mask and last-gasp session\r\n"
                "\tpwrboard control/cfg-read/cfg-set/battery-replaced/"
                "cancel/result-get/raw-get/raw",
        .level = SHELL_LVL_USER, .func = power_board_scp_shell
    });
}

void power_board_scp_hub_restarted(void)
{
    summary_current = false;
    alarms_current = false;
    have_alarm_seq = false;
    snapshot.active_alarms = 0U;
    /* The power-board boot session survives an MH-only restart. */
}

static void store_alarm(uint8_t source, const rf_scp_message_t *message,
                        uint32_t previous)
{
    uint8_t info[16] = {0};

    info[0] = source;
    if (RF_SCP_CMD_PWR_ALARM == source)
    {
        info[1] = message->body.alarm.code;
        info[2] = message->body.alarm.state;
        info[3] = message->body.alarm.seq;
        info[4] = message->body.alarm.suppressed;
        info[6] = message->body.alarm.value0;
        info[7] = message->body.alarm.value1;
    }
    else
    {
        info[3] = message->body.power.seq;
        info[5] = message->body.power.session;
    }
    for (size_t index = 0U; index < 4U; index++)
    {
        info[8U + index] = (uint8_t)(snapshot.active_alarms >> (index * 8U));
        info[12U + index] = (uint8_t)(previous >> (index * 8U));
    }
    const uint32_t critical = 0x000101C3U;
    elog_level_t level = (0U != (snapshot.active_alarms & critical)) ?
                        ELOG_LEVEL_ERROR : ELOG_LEVEL_INFO;
    if ((ELOG_LEVEL_INFO == level) && (0U != snapshot.active_alarms))
    {
        level = ELOG_LEVEL_WARN;
    }
    if ((RF_SCP_CMD_PWR_ALARM == source) &&
        (0x20U == message->body.alarm.code))
    {
        level = (1U == message->body.alarm.value0) ? ELOG_LEVEL_ERROR :
                                                  ELOG_LEVEL_WARN;
    }
    elog_add(ELOG_PWR_SCP_ALARM, level, info, sizeof(info));
}

bool power_board_handle_summary(const rf_scp_message_t *message,
                                 uint32_t now_ms)
{
    if ((NULL == message) || (RF_SCP_CMD_PWR_SUMMARY != message->cmd) ||
        (SCP_TYPE_SET != message->type))
    {
        return false;
    }
    const uint32_t previous = snapshot.active_alarms;

    if (snapshot.has_summary &&
        (snapshot.summary.session != message->body.power.session))
    {
        have_restart_session = false;
    }
    snapshot.summary = message->body.power;
    snapshot.has_summary = true;
    snapshot.active_alarms = message->body.power.active_alarms;
    summary_ms = now_ms;
    alarm_ms = now_ms;
    summary_current = true;
    alarms_current = true;
    if (previous != snapshot.active_alarms)
    {
        store_alarm(RF_SCP_CMD_PWR_SUMMARY, message, previous);
    }
    return true;
}

bool power_board_handle_alarm(const rf_scp_message_t *message,
                               uint32_t now_ms)
{
    if ((NULL == message) || (RF_SCP_CMD_PWR_ALARM != message->cmd) ||
        (SCP_TYPE_SET != message->type) || (3U < message->body.alarm.state))
    {
        return false;
    }
    const uint32_t previous = snapshot.active_alarms;
    bool duplicate = have_alarm_seq &&
                     (snapshot.alarm_seq == message->body.alarm.seq);
    bool log_event = !duplicate;

    if ((0xFFU == message->body.alarm.code) &&
        (3U == message->body.alarm.state))
    {
        if (duplicate)
        {
            return true;
        }
        snapshot.active_alarms = 0U;
        snapshot.alarm_seq = message->body.alarm.seq;
        have_alarm_seq = true;
        alarms_current = true;
        alarm_ms = now_ms;
        store_alarm(RF_SCP_CMD_PWR_ALARM, message, previous);
        return true;
    }
    if (duplicate)
    {
        return true;
    }
    if ((2U == message->body.alarm.state) &&
        (((0x20U == message->body.alarm.code) && snapshot.has_last_gasp &&
          !snapshot.last_gasp_cancelled &&
          (snapshot.last_gasp_session == message->body.alarm.value1) &&
          (snapshot.last_gasp_cause == message->body.alarm.value0)) ||
         ((0x21U == message->body.alarm.code) && snapshot.last_gasp_cancelled &&
          (snapshot.last_gasp_session == message->body.alarm.value1)) ||
         ((0x22U == message->body.alarm.code) && have_restart_session &&
          (restart_session == message->body.alarm.value1))))
    {
        return true;
    }
    snapshot.active_alarms = message->body.alarm.active;
    snapshot.alarm_seq = message->body.alarm.seq;
    snapshot.suppressed = message->body.alarm.suppressed;
    have_alarm_seq = true;
    alarm_ms = now_ms;
    alarms_current = true;
    if ((0x20U == message->body.alarm.code) &&
        (2U == message->body.alarm.state))
    {
        log_event = !snapshot.has_last_gasp || snapshot.last_gasp_cancelled ||
                    (snapshot.last_gasp_cause != message->body.alarm.value0) ||
                    (snapshot.last_gasp_session != message->body.alarm.value1);
        snapshot.has_last_gasp = true;
        snapshot.last_gasp_cancelled = false;
        snapshot.last_gasp_session = message->body.alarm.value1;
        snapshot.last_gasp_cause = message->body.alarm.value0;
    }
    else if ((0x21U == message->body.alarm.code) &&
             (2U == message->body.alarm.state))
    {
        if (snapshot.has_last_gasp &&
            (snapshot.last_gasp_session == message->body.alarm.value1))
        {
            log_event = !snapshot.last_gasp_cancelled;
            snapshot.last_gasp_cancelled = true;
        }
    }
    else if ((0x22U == message->body.alarm.code) &&
             (2U == message->body.alarm.state))
    {
        log_event = !have_restart_session ||
                    (restart_session != message->body.alarm.value1);
        have_restart_session = true;
        restart_session = message->body.alarm.value1;
    }
    else
    {
        log_event = (2U == message->body.alarm.state) ||
                    (previous != snapshot.active_alarms);
    }
    if (log_event || (previous != snapshot.active_alarms))
    {
        store_alarm(RF_SCP_CMD_PWR_ALARM, message, previous);
    }
    return true;
}

static uint16_t valid_fields(const rf_scp_power_summary_t *data)
{
    uint16_t fields = 0U;

    if (summary_current && (0U != (data->flags & 1U)) &&
        (255U != data->age_sec))
    {
        fields = POWER_VALID_SUMMARY;
        if (0U == (data->flags2 & 0x80U))
        {
            fields |= POWER_VALID_ADC;
            if (INT8_MIN != data->board_temperature)
            {
                fields |= POWER_VALID_BOARD_TEMP;
            }
        }
        if (0U == (data->flags & 4U))
        {
            fields |= POWER_VALID_CHARGER;
            if (0U == (data->flags & 8U))
            {
                fields |= POWER_VALID_DISCHARGE;
            }
            if (0U != (data->flags & 2U))
            {
                fields |= POWER_VALID_POWER;
                if (1U == (data->flags2 & 3U))
                {
                    fields |= POWER_VALID_SYSTEM_POWER;
                }
            }
            if ((0U != (data->soc_flags & 2U)) &&
                (-1000 <= data->soc_tenths) && (1000 >= data->soc_tenths))
            {
                fields |= POWER_VALID_SOC;
                if (0U != (data->soc_flags & 1U))
                {
                    fields |= POWER_SOC_ABSOLUTE;
                }
            }
            if (100U >= data->soh_percent)
            {
                fields |= POWER_VALID_SOH;
            }
            if (INT8_MIN != data->battery_temperature)
            {
                fields |= POWER_VALID_BATTERY_TEMP;
            }
            if (5U >= data->charge_phase)
            {
                fields |= POWER_VALID_CHARGE_PHASE;
            }
            if (0U != (data->flags & 0xC0U))
            {
                fields |= POWER_VALID_CHARGE_VERDICT;
            }
        }
        if ((0U != data->capacity_ah) &&
            !((7U == data->capacity_ah) && (0U == (data->flags2 & 0x0CU))))
        {
            fields |= POWER_VALID_CAPACITY;
        }
        if ((0U != (fields & POWER_VALID_ADC)) &&
            ((0U == data->source) ||
             ((4U >= data->source) &&
              (0U != (fields & POWER_VALID_CHARGER)))))
        {
            fields |= POWER_VALID_SOURCE;
        }
    }
    if (alarms_current)
    {
        fields |= POWER_VALID_ALARMS;
    }
    return fields;
}

void power_board_scp_process(uint32_t now_ms)
{
    const uint32_t age_ms = now_ms - summary_ms;

    if (SUMMARY_TIMEOUT_MS <= age_ms)
    {
        summary_current = false;
    }
    if (SUMMARY_TIMEOUT_MS <= (uint32_t)(now_ms - alarm_ms))
    {
        alarms_current = false;
    }
}

bool power_board_get_snapshot(uint32_t now_ms, power_board_snapshot_t *out)
{
    if (NULL == out)
    {
        return false;
    }
    const uint32_t age_ms = now_ms - summary_ms;

    power_board_scp_process(now_ms);
    *out = snapshot;
    out->received_age_ms = snapshot.has_summary ? age_ms : UINT32_MAX;
    const uint32_t age_sec = snapshot.has_summary ?
        (uint32_t)snapshot.summary.age_sec + age_ms / 1000U : 255U;

    out->telemetry_age_sec = (uint8_t)((255U < age_sec) ? 255U : age_sec);
    out->valid_fields = valid_fields(&snapshot.summary);
    return true;
}

static int power_board_scp_shell(int argc, char **argv)
{
    power_board_snapshot_t power;

    if ((1 > argc) || (NULL == argv) ||
        ((2 <= argc) && (NULL == argv[1])))
    {
        SHELL_LOG("Usage: pwrboard [show|alarms]\r\n");
        return -1;
    }
    if ((2 < argc) || ((2 == argc) &&
        (0 != strcmp(argv[1], "show")) &&
        (0 != strcmp(argv[1], "alarms"))))
    {
        return power_board_control_shell(argc, argv);
    }
    (void)power_board_get_snapshot(HAL_GetTick(), &power);
    SHELL_LOG("SCP summary=%u quality=0x%04X age=%us session=%u\r\n",
              (unsigned)power.has_summary, (unsigned)power.valid_fields,
              (unsigned)power.telemetry_age_sec,
              (unsigned)power.summary.session);
    SHELL_LOG("PV=%umV DC=%umV BAT=%umV IBAT=%dmA SOC=%d/10%% "
              "phase=%u\r\n",
              (unsigned)power.summary.pv_mv, (unsigned)power.summary.dc_mv,
              (unsigned)power.summary.battery_mv, power.summary.battery_ma,
              power.summary.soc_tenths, (unsigned)power.summary.charge_phase);
    SHELL_LOG("Alarms=0x%08lX lastgasp=%u cause=%u cancelled=%u\r\n",
              (unsigned long)power.active_alarms,
              (unsigned)power.last_gasp_session,
              (unsigned)power.last_gasp_cause,
              (unsigned)power.last_gasp_cancelled);
    return 0;
}

/*** end of file ***/
