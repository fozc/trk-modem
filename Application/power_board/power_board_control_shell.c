/*
 * power_board_control_shell.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Operator access to SCP customer settings and diagnostic reports.
 */

#include "power_board_control.h"
#include "power_board_scp.h"
#include "shell.h"
#include <string.h>

#ifndef DISABLE_SHELL_LOG
static const char *settings_state_name(power_settings_state_t state)
{
    switch (state)
    {
        case POWER_SETTINGS_IDLE: return "idle";
        case POWER_SETTINGS_READING: return "reading";
        case POWER_SETTINGS_WRITING: return "writing";
        case POWER_SETTINGS_WAIT_ECHO: return "stored; waiting for echo";
        case POWER_SETTINGS_WAIT_APPLY: return "waiting for verification";
        case POWER_SETTINGS_ECHO_ACCEPTED: return "echo accepted; unset value";
        case POWER_SETTINGS_APPLIED: return "applied";
        case POWER_SETTINGS_REJECTED: return "rejected";
        case POWER_SETTINGS_CONFLICT: return "GEN conflict; read and retry";
        case POWER_SETTINGS_UNCERTAIN: return "write outcome unknown";
        case POWER_SETTINGS_RESTARTED: return "MH restarted";
        default: return "unknown";
    }
}

static const char *command_state_name(power_command_state_t state)
{
    switch (state)
    {
        case POWER_COMMAND_IDLE: return "idle";
        case POWER_COMMAND_SENDING: return "sending";
        case POWER_COMMAND_ACCEPTED: return "accepted; result pending";
        case POWER_COMMAND_MONITORING: return "cancelled; outcome monitored";
        case POWER_COMMAND_APPLIED: return "applied";
        case POWER_COMMAND_REJECTED: return "rejected";
        case POWER_COMMAND_UNCERTAIN: return "outcome unknown; query result";
        case POWER_COMMAND_RESTARTED: return "MH restarted";
        case POWER_COMMAND_NO_RESPONSE: return "finished without result";
        case POWER_COMMAND_CANCELLED: return "cancelled; counters preserved";
        case POWER_COMMAND_CANCEL_NO_EFFECT: return "cancel had no effect";
        default: return "unknown";
    }
}
#endif

static void print_status(void)
{
    power_board_control_status_t control;

    (void)power_board_control_get_status(&control);
    SHELL_LOG("Settings: %s; config=%u gen=%u written_gen=%u\r\n",
        settings_state_name(control.settings_state),
        (unsigned)control.has_config, (unsigned)control.config[1],
        (unsigned)control.written_gen);
    SHELL_LOG("cap=%uAh rate=%u/1000 period=%us raw_period=0x%02X "
              "echo=%u/%u m1=0x%02X m2=0x%02X\r\n",
        (unsigned)control.config[13], (unsigned)control.config[9],
        (unsigned)(control.config[14] & 0x3FU),
        (unsigned)control.config[14], (unsigned)control.echo_valid,
        (unsigned)control.echo_gen, (unsigned)control.mask1,
        (unsigned)control.mask2);
    SHELL_LOG("error=%u write_reject=0x%04X historical_reject=0x%04X\r\n",
        (unsigned)control.error_code, (unsigned)control.rejected_fields,
        (unsigned)control.last_rejected);
    SHELL_LOG("Command: %s; ack=%u seq=%u report=%u cmd=%u seq=%u "
              "result=%u transmissions=%u flags=0x%02X\r\n",
        command_state_name(control.command_state),
        (unsigned)control.has_command_ack, (unsigned)control.command_seq,
        (unsigned)control.has_result, (unsigned)control.result_command,
        (unsigned)control.result_seq, (unsigned)control.result,
        (unsigned)control.transmissions, (unsigned)control.command_flags);
}

static bool parse_byte(const char *text, uint8_t *out)
{
    uint16_t value = 0U;

    if ((NULL == text) || ('\0' == text[0]))
    {
        return false;
    }
    for (size_t index = 0U; '\0' != text[index]; index++)
    {
        if (('0' > text[index]) || ('9' < text[index]) || (25U < value))
        {
            return false;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(text[index] - '0'));
        if (255U < value)
        {
            return false;
        }
    }
    *out = (uint8_t)value;
    return true;
}

static bool write_setting(const char *field, const char *text)
{
    power_settings_request_t request = {0};
    uint8_t value;

    if ((NULL == field) || !parse_byte(text, &value))
    {
        return false;
    }
    if (0 == strcmp(field, "capacity"))
    {
        request.mask = POWER_SETTING_CAPACITY;
        request.capacity_ah = value;
    }
    else if (0 == strcmp(field, "rate"))
    {
        request.mask = POWER_SETTING_RATE;
        request.rate_permille = value;
    }
    else if (0 == strcmp(field, "period"))
    {
        request.mask = POWER_SETTING_PERIOD;
        request.period_sec = value;
    }
    else
    {
        return false;
    }
    return power_board_write_settings(&request);
}

static void print_raw(void)
{
    power_board_control_status_t control;

    (void)power_board_control_get_status(&control);
    if (!control.has_raw)
    {
        SHELL_LOG("No SCP raw telemetry; use pwrboard raw-get\r\n");
        return;
    }
    for (size_t index = 0U; index < sizeof(control.raw); index++)
    {
        SHELL_LOG("%02X%s", (unsigned)control.raw[index],
            (15U == (index % 16U)) ? "\r\n" : " ");
    }
}

#ifndef DISABLE_SHELL_LOG
static uint16_t raw_u16(const power_summary_raw_t *pkt, uint8_t offset)
{
    return (uint16_t)((uint16_t)pkt->raw[offset] |
                      ((uint16_t)pkt->raw[offset + 1U] << 8));
}

static uint32_t raw_u32(const power_summary_raw_t *pkt, uint8_t offset)
{
    return (uint32_t)pkt->raw[offset] |
           ((uint32_t)pkt->raw[offset + 1U] << 8) |
           ((uint32_t)pkt->raw[offset + 2U] << 16) |
           ((uint32_t)pkt->raw[offset + 3U] << 24);
}

static void print_summary_packet(void)
{
    power_summary_raw_t pkt;

    if (!power_board_get_summary_raw(&pkt))
    {
        SHELL_LOG("No 0xE1 summary packet yet\r\n");
        return;
    }
    SHELL_LOG("0xE1 PWR_SUMMARY %u B\r\n", (unsigned)pkt.len);
    SHELL_LOG("recv 20%02u/%02u/%02u %02u:%02u:%02u\r\n",
              (unsigned)pkt.year, (unsigned)pkt.month, (unsigned)pkt.day,
              (unsigned)pkt.hour, (unsigned)pkt.minute,
              (unsigned)pkt.second);
    for (uint8_t row = 0U; row < pkt.len; row += 16U)
    {
        for (uint8_t col = 0U; (col < 16U) && ((row + col) < pkt.len);
             col++)
        {
            SHELL_LOG("%02X ", (unsigned)pkt.raw[row + col]);
        }
        SHELL_LOG("\r\n");
    }
    SHELL_LOG("ver=%u sira=%u durum=0x%02X durum2=0x%02X tlm_yas=%u\r\n",
              (unsigned)pkt.raw[0], (unsigned)pkt.raw[1],
              (unsigned)pkt.raw[2], (unsigned)pkt.raw[3],
              (unsigned)pkt.raw[4]);
    SHELL_LOG("kaynak=%u sarj=%u oturum=%u | vpv=%u vdc=%u ibus=%d "
              "vsys=%u vbat=%u ibat=%d (mA/mV)\r\n",
              (unsigned)pkt.raw[5], (unsigned)pkt.raw[6],
              (unsigned)pkt.raw[7], (unsigned)raw_u16(&pkt, 8U),
              (unsigned)raw_u16(&pkt, 10U),
              (int)(int16_t)raw_u16(&pkt, 12U),
              (unsigned)raw_u16(&pkt, 14U), (unsigned)raw_u16(&pkt, 16U),
              (int)(int16_t)raw_u16(&pkt, 18U));
    SHELL_LOG("pin=%d pbat=%d psys=%d soc=%d soh=%u aku=%d kart=%d "
              "cap=%u crate=%u lg=%u\r\n",
              (int)(int16_t)raw_u16(&pkt, 20U),
              (int)(int16_t)raw_u16(&pkt, 22U),
              (int)(int16_t)raw_u16(&pkt, 24U),
              (int)(int16_t)raw_u16(&pkt, 26U),
              (unsigned)pkt.raw[28], (int)(int8_t)pkt.raw[29],
              (int)(int8_t)pkt.raw[30], (unsigned)pkt.raw[31],
              (unsigned)pkt.raw[32], (unsigned)pkt.raw[33]);
    SHELL_LOG("alarm=0x%08lX soc_capa=0x%02X",
              (unsigned long)raw_u32(&pkt, 34U), (unsigned)pkt.raw[38]);
    if (pkt.len >= 45U)
    {
        SHELL_LOG(" efc=%u kalan_efc=%u kalan_yil_x10=%u",
                  (unsigned)raw_u16(&pkt, 39U),
                  (unsigned)raw_u16(&pkt, 41U),
                  (unsigned)raw_u16(&pkt, 43U));
    }
    SHELL_LOG("\r\n");
    SHELL_LOG("aktif:");
    {
        const uint32_t mask = raw_u32(&pkt, 34U);
        bool any = false;

        for (uint8_t code = 0U; code < 32U; code++)
        {
            if (0U != (mask & ((uint32_t)1U << code)))
            {
                const char *name = power_board_alarm_name(code);

                SHELL_LOG(" %s", (NULL != name) ? name : "?");
                any = true;
            }
        }
        if (!any)
        {
            SHELL_LOG(" yok");
        }
        SHELL_LOG("\r\n");
    }
}
#endif /* !DISABLE_SHELL_LOG */

int power_board_control_shell(int argc, char **argv)
{
    bool started = false;

    if ((NULL == argv) || (2 > argc) || (NULL == argv[1]))
    {
        return -1;
    }
    if ((2 == argc) && (0 == strcmp(argv[1], "control")))
    {
        print_status();
        return 0;
    }
#ifndef DISABLE_SHELL_LOG
    if ((2 == argc) && (0 == strcmp(argv[1], "pkt")))
    {
        print_summary_packet();
        return 0;
    }
#endif
    if ((2 == argc) && (0 == strcmp(argv[1], "raw")))
    {
        print_raw();
        return 0;
    }
    if ((4 == argc) && (0 == strcmp(argv[1], "cfg-set")))
    {
        started = write_setting(argv[2], argv[3]);
    }
    else if ((2 == argc) && (0 == strcmp(argv[1], "cfg-read")))
    {
        started = power_board_read_settings();
    }
    else if ((2 == argc) && (0 == strcmp(argv[1], "battery-replaced")))
    {
        started = power_board_battery_replaced();
    }
    else if ((2 == argc) && (0 == strcmp(argv[1], "cancel")))
    {
        started = power_board_cancel_command();
    }
    else if ((2 == argc) && (0 == strcmp(argv[1], "result-get")))
    {
        started = power_board_read_command_result();
    }
    else if ((2 == argc) && (0 == strcmp(argv[1], "raw-get")))
    {
        started = power_board_read_raw();
    }
    else
    {
        SHELL_LOG("Usage: pwrboard control|pkt|cfg-read|cfg-set "
                  "<capacity|rate|period> <value>|battery-replaced|cancel|"
                  "result-get|raw-get|raw\r\n");
        return -1;
    }
    SHELL_LOG("%s; inspect pwrboard control\r\n",
        started ? "Request started" : "Not started: invalid/busy/unready");
    return started ? 0 : -1;
}

/*** end of file ***/
