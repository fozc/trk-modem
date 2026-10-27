/*
 * power_board_scp.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Own SCP power-board data; I2C snapshots never enter this model.
 */

#define CSLOG_MODULE LOG_MOD_RF
#include "power_board_scp.h"
#include "power_board_control.h"
#include "elog.h"
#include "shell.h"
#include "stm32u3xx_hal.h"
#include "rtc.h"
#include "console_logger.h"
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
#if defined(BSP_CONSOLE_LOG)
static uint32_t announced_alarms;
#endif

static int power_board_scp_shell(int argc, char **argv);

void power_board_scp_init(void)
{
    power_board_control_init();
    (void)memset(&snapshot, 0, sizeof(snapshot));
    snapshot.summary.equivalent_cycles = UINT16_MAX;
    snapshot.summary.remaining_cycles = UINT16_MAX;
    snapshot.summary.remaining_years_x10 = UINT16_MAX;
    summary_current = false;
    alarms_current = false;
    have_alarm_seq = false;
    have_restart_session = false;
#if defined(BSP_CONSOLE_LOG)
    announced_alarms = 0U;
#endif
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
    if ((source == RF_SCP_CMD_PWR_ALARM) && (message->body.alarm.code == 13U) &&
        (message->body.alarm.state == 1U) && (message->body.alarm.value0 == 2U))
    {
        level = ELOG_LEVEL_ERROR;
    }
    elog_add(ELOG_PWR_SCP_ALARM, level, info, sizeof(info));
}

/* R2 bolum 5.3 seviye alarm kodlari; maske bit n = kod n. */
static const char *const alarm_names[] =
{
    "AKU_COK_DUSUK", "BESLEME_COKUYOR", "AKU_SOGUK", "AKU_SICAK",
    "AKU_DUSUK", "AKU_YUKSEK", "GIRIS_ASIRI_GERILIM",
    "AKU_OVP_SARJ_YASAGI", "AKU_OVP_KALICI", "KART_SICAK", "SOGUK",
    "SARJ_DENETLEYICI_HATA", "OLCUM_BAYAT", "TELEMETRI_YOK",
    "MH_I2C_KAPALI", "SARJ_DENETLEYICI_OKUNAMADI", "KOR",
    "STM_OLCUM_BAYAT", "SOC_DUSUK", "KART_YUVA_KAPALI", "AKU_YOK"
};

const char *power_board_alarm_name(uint8_t code)
{
    const size_t count = sizeof(alarm_names) / sizeof(alarm_names[0]);
    return ((size_t)code < count) ? alarm_names[code] : NULL;
}

static void announce_alarm_changes(void)
{
#if defined(BSP_CONSOLE_LOG)
    if (!console_logger_level_enabled(LOG_MOD_RF, LOG_LVL_NORMAL))
    {
        return;
    }
    const uint32_t changed = snapshot.active_alarms ^ announced_alarms;

    for (uint8_t code = 0U; code < 32U; code++)
    {
        const uint32_t bit = (uint32_t)1U << code;
        if (0U != (changed & bit))
        {
            const char *name = power_board_alarm_name(code);
            const char *state = (0U != (snapshot.active_alarms & bit)) ?
                                "basladi" : "kapandi";

            CSLOG_WARN("[PB] alarm %s: %s (kod=%u maske=0x%08lX)\r\n",
                       state, (NULL != name) ? name : "?", (unsigned)code,
                       (unsigned long)snapshot.active_alarms);
        }
    }
    announced_alarms = snapshot.active_alarms;
#endif
}

static power_summary_raw_t last_summary_raw;

void power_board_capture_summary_raw(const uint8_t *data, uint8_t len,
                                     uint32_t now_ms)
{
    if ((NULL == data) || (0U == len) || (POWER_SUMMARY_RAW_MAX < len))
    {
        return;
    }
    (void)memcpy(last_summary_raw.raw, data, len);
    last_summary_raw.len = len;
    last_summary_raw.tick_ms = now_ms;
    last_summary_raw.second = rtc_get_second();
    last_summary_raw.minute = rtc_get_minute();
    last_summary_raw.hour = rtc_get_hour();
    last_summary_raw.day = rtc_get_day();
    last_summary_raw.month = rtc_get_month();
    last_summary_raw.year = rtc_get_year();
}

bool power_board_get_summary_raw(power_summary_raw_t *out)
{
    if ((NULL == out) || (0U == last_summary_raw.len))
    {
        return false;
    }
    *out = last_summary_raw;
    return true;
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
    announce_alarm_changes();
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
        announce_alarm_changes();
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
        /* SCP 5.3: predicted and actual drops with one session are one outage. */
        log_event = !snapshot.has_last_gasp ||
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
                    (previous != snapshot.active_alarms) ||
                    ((message->body.alarm.code == 13U) && (message->body.alarm.state == 1U) &&
                     (message->body.alarm.value0 == 2U));
    }
    if (log_event || (previous != snapshot.active_alarms))
    {
        store_alarm(RF_SCP_CMD_PWR_ALARM, message, previous);
    }
    announce_alarm_changes();
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

static void print_lifetime(const char *name, uint16_t value)
{
#ifdef DISABLE_SHELL_LOG
    (void)name;
#endif
    if (value == UINT16_MAX)
    {
        SHELL_LOG("%s=unknown\r\n", name);
    }
    else
    {
        SHELL_LOG("%s=%u\r\n", name, (unsigned)value);
    }
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
    const bool has_lifetime = ((power.valid_fields & POWER_VALID_SUMMARY) != 0U) &&
                              (power.telemetry_age_sec <= 30U);
    print_lifetime("EFC (cycles)", has_lifetime ? power.summary.equivalent_cycles : UINT16_MAX);
    print_lifetime("Remaining cycles", has_lifetime ? power.summary.remaining_cycles : UINT16_MAX);
    print_lifetime("Remaining life (0.1 year)", has_lifetime ? power.summary.remaining_years_x10 : UINT16_MAX);
    return 0;
}

/*** end of file ***/
