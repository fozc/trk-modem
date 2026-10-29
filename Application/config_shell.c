/*
 * config_shell.c
 *
 *  Created on: Oct 29, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Production and service configuration through the shell.
 */
#include "config_shell.h"
#include "datetime.h"
#include "modem_config.h"
#include "nvram.h"
#include "rtc.h"
#include "shell.h"
#include "xscanf.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool parse_number(const char *text, uint32_t maximum, uint32_t *value)
{
    uint32_t number;
    xscanf_result_t result;
    const size_t length = strlen(text);
    if (!xscanf_isdigit(text[0]) ||
        (xscanf_ex(&result, text, length, "%u32", &number) != 1) ||
        (result.error != XSCANF_OK) || (result.input_pos != length) || (number > maximum))
    {
        return false;
    }
    *value = number;
    return true;
}

static bool parse_calendar(const char *date, const char *time, datetime_t *out)
{
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    xscanf_result_t result;
    if ((strlen(date) != 10U) || (strlen(time) != 8U) ||
        (strspn(date, "0123456789-") != 10U) || (strspn(time, "0123456789:") != 8U) ||
        (date[4] != '-') || (date[7] != '-') || (time[2] != ':') || (time[5] != ':'))
    {
        return false;
    }
    if ((xscanf_ex(&result, date, 10U, "%4u16-%2u8-%2u8", &year, &month, &day) != 3) ||
        (result.error != XSCANF_OK) || (result.input_pos != 10U) ||
        (year < 2026U) || (year > 2099U))
    {
        return false;
    }
    if ((xscanf_ex(&result, time, 8U, "%2u8:%2u8:%2u8", &hour, &minute, &second) != 3) ||
        (result.error != XSCANF_OK) || (result.input_pos != 8U))
    {
        return false;
    }
    return dt_init(year, month, day, hour, minute, second, out) == 0;
}

static bool copy_text(char *destination, size_t capacity, const char *value)
{
    const size_t length = strlen(value);
    if (length >= capacity)
    {
        return false;
    }
    for (size_t index = 0U; index < length; index++)
    {
        /* The GSM service inserts these fields into quoted AT arguments. */
        if ((value[index] < ' ') || (value[index] > '~') ||
            (value[index] == '"') || (value[index] == '\\'))
        {
            return false;
        }
    }
    (void)memset(destination, 0, capacity);
    (void)memcpy(destination, value, length);
    return true;
}

static int save_config(const modem_config_t *candidate, bool gsm_changed)
{
    if (nvram_is_busy())
    {
        SHELL_LOG("Configuration storage busy; unchanged.\r\n");
        return -1;
    }
    modem_config_t *current = nvram_get_modem_config_rw();
    const modem_config_t previous = *current;
    *current = *candidate;
    const nvram_save_result_t result = nvram_save(false);
    if (result == NVRAM_SAVE_FAILED)
    {
        *current = previous;
        SHELL_LOG("Save failed; previous configuration restored in RAM.\r\n");
        return -1;
    }
    SHELL_LOG("Saved.%s\r\n", result == NVRAM_SAVE_PRIMARY_ONLY ? " Backup copy incomplete." : "");
    if (gsm_changed)
    {
        SHELL_LOG("GSM settings apply on the next modem initialization; no connection was restarted.\r\n");
    }
    return 0;
}

static void password_received(const char *value, size_t length)
{
    if (shell_get_session_level() != SHELL_LVL_SUPER_USER)
    {
        SHELL_LOG("Permission denied.\r\n");
        return;
    }
    modem_config_t candidate = *modem_config_get();
    if ((length != strlen(value)) || (length >= MAX_APN_PASSWORD_LEN) ||
        !copy_text(candidate.apn.user_pass, MAX_APN_PASSWORD_LEN, value))
    {
        SHELL_LOG("Invalid password; unchanged (maximum 15 characters, no quotes/backslash).\r\n");
        return;
    }
    (void)save_config(&candidate, true);
}

static void show_epoch(const char *name, uint32_t epoch)
{
    if (epoch == 0U)
    {
        SHELL_LOG("%s: unset\r\n", name);
        return;
    }
    datetime_t date = {0};
    dt_conv_from_epoch(epoch, &date);
    SHELL_LOG("%s: %04u-%02u-%02u %02u:%02u:%02u UTC\r\n", name,
              (unsigned)date.date.year, (unsigned)date.date.month, (unsigned)date.date.day,
              (unsigned)date.time.hour, (unsigned)date.time.minute, (unsigned)date.time.second);
}

static void show_config(void)
{
    const modem_config_t *config = modem_config_get();
    SHELL_LOG("serial: %lu\r\n", (unsigned long)config->serial_number);
    show_epoch("production-date", config->production_date);
    show_epoch("commissioning-time", config->commissioning_time);
    show_epoch("time", rtc_get_unix_epoch());
    SHELL_LOG("RTC valid: %s\r\n", rtc_hw_is_valid() ? "yes" : "no");
    SHELL_LOG("apn: %s\r\napn-user: %s\r\napn-password: %s\r\nsim-pin: ****\r\n",
              config->apn.apn, config->apn.user_name, config->apn.user_pass[0] == '\0' ? "unset" : "****");
    SHELL_LOG("ntp-server: %s\r\nntp-port: %u\r\ntimezone: %ld\r\n",
              config->ntp_server, (unsigned)config->ntp_server_port, (long)config->time_zone);
}

static bool set_number(modem_config_t *config, const char *key, const char *value)
{
    uint32_t number;
    if (strcmp(key, "timezone") == 0)
    {
        int32_t timezone;
        xscanf_result_t result;
        const size_t length = strlen(value);
        if ((xscanf_ex(&result, value, length, "%s32", &timezone) != 1) ||
            (result.error != XSCANF_OK) || (result.input_pos != length) ||
            (timezone < -12) || (timezone > 14))
        {
            return false;
        }
        config->time_zone = timezone;
    }
    else if (strcmp(key, "serial") == 0)
    {
        if (!parse_number(value, 99999999U, &number))
        {
            return false;
        }
        config->serial_number = number;
    }
    else if (strcmp(key, "ntp-port") == 0)
    {
        if (!parse_number(value, UINT16_MAX, &number) || (number == 0U))
        {
            return false;
        }
        config->ntp_server_port = (uint16_t)number;
    }
    else if (strcmp(key, "sim-pin") == 0)
    {
        if ((strlen(value) != 4U) || !parse_number(value, 9999U, &number))
        {
            return false;
        }
        config->sim_card_pin = (uint16_t)number;
    }
    else
    {
        return false;
    }
    return true;
}

static int set_calendar(int argc, char *argv[], modem_config_t *config)
{
    const bool production = strcmp(argv[2], "production-date") == 0;
    datetime_t date = {0};
    if ((argc != (production ? 4 : 5)) || !parse_calendar(argv[3], production ? "00:00:00" : argv[4], &date))
    {
        SHELL_LOG("Use YYYY-MM-DD%s, UTC, years 2026..2099.\r\n", production ? "" : " HH:MM:SS");
        return -1;
    }
    if (strcmp(argv[2], "time") == 0)
    {
        const rtc_t clock = {.year = (uint8_t)(date.date.year - 2000U), .month = date.date.month,
                             .day = date.date.day, .hour = date.time.hour, .minute = date.time.minute,
                             .second = date.time.second, .millisec = 0U};
        if (!rtc_sync(&clock))
        {
            SHELL_LOG("RTC update failed.\r\n");
            return -1;
        }
        SHELL_LOG("RTC updated (UTC).\r\n");
        return 0;
    }
    if (production)
    {
        config->production_date = dt_conv_to_epoch(&date);
    }
    else
    {
        config->commissioning_time = dt_conv_to_epoch(&date);
    }
    return save_config(config, false);
}

static int config_command(int argc, char *argv[])
{
    if ((argc == 2) && (strcmp(argv[1], "show") == 0))
    {
        show_config();
        return 0;
    }
    if ((argc < 3) || (strcmp(argv[1], "set") != 0))
    {
        SHELL_LOG("Usage: config show | config set <field> <value> (config help)\r\n");
        return -1;
    }
    if (shell_get_session_level() != SHELL_LVL_SUPER_USER)
    {
        SHELL_LOG("Permission denied.\r\n");
        return -1;
    }
    const char *key = argv[2];
    if (strcmp(key, "apn-password") == 0)
    {
        if ((argc != 3) || !shell_read_secret(password_received))
        {
            SHELL_LOG("Use config set apn-password on the local serial console, without a value.\r\n");
            return -1;
        }
        SHELL_LOG("APN password (empty clears; disable terminal local echo):\r\n");
        return 0;
    }
    modem_config_t candidate = *modem_config_get();
    if ((strcmp(key, "time") == 0) || (strcmp(key, "production-date") == 0) ||
        (strcmp(key, "commissioning-time") == 0))
    {
        return set_calendar(argc, argv, &candidate);
    }
    bool valid = false;
    if ((argc == 3) || (argc == 4))
    {
        const char *value = argc == 4 ? argv[3] : "";
        if (strcmp(key, "apn") == 0)
        {
            valid = copy_text(candidate.apn.apn, MAX_APN_LEN, value);
        }
        else if (strcmp(key, "apn-user") == 0)
        {
            valid = copy_text(candidate.apn.user_name, MAX_APN_USER_NAME_LEN, value);
        }
        else if (strcmp(key, "ntp-server") == 0)
        {
            valid = copy_text(candidate.ntp_server, sizeof(candidate.ntp_server), value);
        }
        else
        {
            valid = set_number(&candidate, key, value);
        }
    }
    if (!valid)
    {
        SHELL_LOG("Unknown field or invalid value; unchanged. See config help.\r\n");
        return -1;
    }
    const bool gsm_changed = (strcmp(key, "apn") == 0) || (strcmp(key, "apn-user") == 0) ||
                             (strcmp(key, "sim-pin") == 0);
    return save_config(&candidate, gsm_changed);
}

void config_shell_init(void)
{
    const shell_cmd_t command =
    {
        .cmd = "config",
        .desc = "Production/service settings (UTC)\r\n"
                "\tconfig show\r\n"
                "\tconfig set serial <0..99999999>\r\n"
                "\tconfig set production-date <YYYY-MM-DD>\r\n"
                "\tconfig set commissioning-time <YYYY-MM-DD> <HH:MM:SS>\r\n"
                "\tconfig set time <YYYY-MM-DD> <HH:MM:SS>\r\n"
                "\tconfig set apn|apn-user [value] (max 15; omitted clears)\r\n"
                "\tconfig set apn-password (local hidden input; empty clears)\r\n"
                "\tconfig set sim-pin <0000..9999>\r\n"
                "\tconfig set ntp-server [value] (omitted disables NTP)\r\n"
                "\tconfig set ntp-port <1..65535>\r\n"
                "\tconfig set timezone <-12..14>\r\n"
                "\tWrites require admin; each setting is saved immediately.",
        .level = SHELL_LVL_USER,
        .func = config_command
    };
    (void)shell_register_command(&command);
}

/*** end of file ***/
