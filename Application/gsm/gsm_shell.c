/*
 * gsm_shell.c
 *
 *  Created on: Jan 24, 2026
 *      Author: fatih
 */
#include "gsm_shell.h"
#include "bsp.h"
#include "shell.h"
#include "contiki.h"
#include "contiki_process.h"
#include "gsm_engine.h"
#include "gsm_info.h"
#include "gsm_process.h"
#include "gsm_listener_process.h"
#include "gsm_socket.h"
#include "gsm_log.h"
#include "modem_config.h"
#include "at_engine2.h"
#include "datetime.h"
#include <string.h>
#include <stddef.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define GSM_SHELL_AT_TIMEOUT_MS  10000U
#define GSM_SHELL_AT_CMD_MAX_LEN 32U

/* ============================================================================
 * PRIVATE STATE
 * ============================================================================ */

typedef struct
{
    char at_cmd[GSM_SHELL_AT_CMD_MAX_LEN];  /* Copy of last AT command sent */
} gsm_shell_state_t;

static gsm_shell_state_t s_state;

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static int  gsm_shell_cmd_handler(int argc, char *argv[]);
static void cmd_at(const char *at_cmd);
static void cmd_status(const char *arg);
static void cmd_log(const char *arg);
static void print_help(const char *arg);

/* ============================================================================
 * GSM SHELL SEND PROCESS
 *
 * Polls the AT engine result every 50 ms after a command is dispatched.
 * Prints the response (or error tag) once the engine is done.
 * ============================================================================ */

PROCESS(gsm_shell_send, "gsm_shell_send");
PROCESS_THREAD(gsm_shell_send, ev, data)
{
    (void)data;
    static struct etimer timer;

    PROCESS_BEGIN();

    etimer_set(&timer, 50);

    /* Phase 1: wait until the AT engine is free */
    while (at_engine_is_busy() || gsm_is_busy())
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_reset(&timer);
    }

    /* Send the queued command */
    {
    	uint16_t cmd_len = (uint16_t)strlen(s_state.at_cmd);
		uint32_t move_len = 2U; /* +2 for "AT" prefix */

		if(cmd_len + move_len + 2U > GSM_SHELL_AT_CMD_MAX_LEN)
		{
			SHELL_LOG("[ERROR] AT command too long to send\r\n");
			PROCESS_EXIT();
		}

		if(s_state.at_cmd[0] != '#' && s_state.at_cmd[0] != '+'){
			move_len = 3U;
		}

		memmove(s_state.at_cmd + move_len, s_state.at_cmd, cmd_len + 1U);

		s_state.at_cmd[0] = 'a';
		s_state.at_cmd[1] = 't';
		cmd_len += 2U;
		if(move_len == 3U){
			s_state.at_cmd[2] = '+';
			cmd_len += 1U;
		}
		s_state.at_cmd[cmd_len++] = '\r';
		s_state.at_cmd[cmd_len] = '\0';

	    /* Lock the GSM module before sending. */
	    gsm_set_busy();
        bool sent = at_engine_send_at_command(s_state.at_cmd, cmd_len,
        									  "OK\r\n", 4U,
											  1U, GSM_SHELL_AT_TIMEOUT_MS);

        if (!sent)
        {
            SHELL_LOG("[ERROR] Failed to queue AT command\r\n");
            gsm_set_free();
            PROCESS_EXIT();
        }

        SHELL_LOG("Sent: %s\r\n", s_state.at_cmd);
    }

    /* Phase 2: wait for result */
    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_reset(&timer);

        at_engine_result_t result = at_engine_get_result();
        if (result == AT_ENGINE_RESULT_NONE)
        {
            continue; /* Still in progress */
        }

        uint16_t resp_len = 0U;
        const uint8_t *p_resp = at_engine_get_response(&resp_len);

        if ((p_resp != NULL) && (resp_len > 0U))
        {
            SHELL_LOG("%.*s", (int)resp_len, (const char *)p_resp);
        }

        switch (result)
        {
            case AT_ENGINE_RESULT_OK:
            case AT_ENGINE_RESULT_ERROR:
                /* "OK" / "ERROR" already present in the modem response buffer */
                break;
            case AT_ENGINE_RESULT_TIMEOUT:
                SHELL_LOG("[TIMEOUT]\r\n");
                break;
            case AT_ENGINE_RESULT_NO_SIM:
                SHELL_LOG("[NO SIM]\r\n");
                break;
            default:
                break;
        }

        /* Clean up AT engine state and release the GSM module. */
        at_engine_clear_buff();
        at_engine_reset();
        gsm_set_free();

        PROCESS_EXIT(); /* Done — kill this process instance */
    }

    PROCESS_END();
}

/* ============================================================================
 * SUBCOMMAND IMPLEMENTATIONS
 * ============================================================================ */

static void cmd_at(const char *at_cmd)
{
    if ((at_cmd == NULL) || (at_cmd[0] == '\0'))
    {
        SHELL_LOG("Usage: gsm at <AT_COMMAND>\r\n");
        SHELL_LOG("Example: gsm at AT+CSQ\r\n");
        return;
    }

    if (process_is_running(&gsm_shell_send))
    {
        SHELL_LOG("[WARN] AT response pending, command ignored\r\n");
        return;
    }

    size_t cmd_len = strlen(at_cmd);
    if (cmd_len >= GSM_SHELL_AT_CMD_MAX_LEN)
    {
        SHELL_LOG("[ERROR] AT command too long (max %u chars)\r\n",
                  (unsigned int)(GSM_SHELL_AT_CMD_MAX_LEN - 1U));
        return;
    }

    memcpy(s_state.at_cmd, at_cmd, cmd_len + 1U);
    process_start(&gsm_shell_send, NULL);
}

static const char *sim_state_str(gsm_sim_state_t s)
{
    switch (s)
    {
        case GSM_SIM_STATE_READY:        return "READY";
        case GSM_SIM_STATE_PIN_REQUIRED: return "PIN";
        case GSM_SIM_STATE_PUK_REQUIRED: return "PUK";
        case GSM_SIM_STATE_BUSY:         return "BUSY";
        case GSM_SIM_STATE_NOT_INSERTED: return "NOT INSERTED";
        case GSM_SIM_STATE_FAILURE:      return "FAILURE";
        case GSM_SIM_STATE_ERROR:        return "ERROR";
        case GSM_SIM_STATE_UNKNOWN:
        default:                         return "UNKNOWN";
    }
}

static const char *net_reg_str(gsm_net_reg_state_t s)
{
    switch (s)
    {
        case GSM_NET_REG_NOT_REGISTERED: return "NOT REG";
        case GSM_NET_REG_REGISTERED:     return "REGISTERED";
        case GSM_NET_REG_SEARCHING:      return "SEARCHING";
        case GSM_NET_REG_DENIED:         return "DENIED";
        case GSM_NET_REG_ROAMING:        return "ROAMING";
        case GSM_NET_REG_UNKNOWN:
        default:                         return "UNKNOWN";
    }
}

static const char *rat_str(uint8_t rat)
{
    switch (rat)
    {
        case 2:  return "2G";
        case 3:  return "3G";
        case 4:  return "4G";
        default: return "-";
    }
}

static const char *main_state_str(uint8_t s)
{
    switch (s)
    {
        case GSM_COMMON_INIT_MODE:   return "INIT";
        case GSM_MODULE_INIT_MODE:   return "MODULE INIT";
        case GSM_SIM_ERROR_MODE:     return "SIM ERROR";
        case GSM_NORMAL_MODE:        return "NORMAL";
        case GSM_POWER_OUTAGE_MODE:  return "POWER OUT";
        case GSM_POWER_SAVING_MODE:  return "POWER SAVE";
        default:                     return "?";
    }
}

static const char *module_model_str(gsm_module_model_t m)
{
    switch (m)
    {
        case GSM_MODULE_LE910R1:   return "LE910R1";
        case GSM_MODULE_UNDEFINED:
        default:                   return "UNDEFINED";
    }
}

static const char *socket_state_str(uint8_t s)
{
    switch (s)
    {
        case SOCKET_CLOSED:    return "CLOSED";
        case SOCKET_OPENED:    return "OPENED";
        case SOCKET_HAS_DATA:  return "HAS_DATA";
        case SOCKET_CONNECTED: return "CONNECTED";
        case SOCKET_DATA_MODE: return "DATA_MODE";
        case SOCKET_LISTENING: return "LISTENING";
        case SOCKET_FAIL:      return "FAIL";
        default:               return "?";
    }
}

static const char *ls_state_str(uint8_t s)
{
    switch (s)
    {
        case GSM_LS_IDLE:              return "IDLE";
        case GSM_LS_CHECK_SOCKET_INFO: return "CHECK_INFO";
        case GSM_LS_DATA_MODE:         return "DATA_MODE";
        case GSM_LS_SEND_DATA:         return "SEND_DATA";
        case GSM_LS_CHECK_SOCKET:      return "CHECK_SOCKET";
        case GSM_LS_CLOSE_SOCKET:      return "CLOSE_SOCKET";
        case GSM_LS_OPEN_SOCKET:       return "OPEN_SOCKET";
        case GSM_LS_READ_SOCKET_DATA:  return "READ_DATA";
        case GSM_LS_ACCEPT_CONNECTION: return "ACCEPT_CONN";
        case GSM_LS_CHECK_NET:         return "CHECK_NET";
        default:                       return "?";
    }
}

static const char *disconnect_reason_str(uint8_t r)
{
    switch ((sock_disconnect_reason_t)r)
    {
        case SOCK_DISC_NONE:          return "-";
        case SOCK_DISC_NO_CARRIER:    return "NO_CARRIER";
        case SOCK_DISC_TIMEOUT:       return "TIMEOUT";
        case SOCK_DISC_MANUAL:        return "MANUAL";
        case SOCK_DISC_ERROR:         return "AT_ERROR";
        case SOCK_DISC_SOCKET_CLOSED: return "SOCK_CLOSED";
        default:                      return "?";
    }
}

/**
 * @brief Format milliseconds into human-readable duration (e.g. "2h13m05s").
 */
static void fmt_duration(uint32_t ms, char *buf, size_t buf_size)
{
    uint32_t sec = ms / 1000U;
    uint32_t min = sec / 60U;
    uint32_t hr  = min / 60U;

    sec %= 60U;
    min %= 60U;

    if (hr > 0U)
    {
        xsnprintf(buf, (int)buf_size, "%luh%02lum%02lus",
                  (unsigned long)hr, (unsigned long)min, (unsigned long)sec);
    }
    else if (min > 0U)
    {
        xsnprintf(buf, (int)buf_size, "%lum%02lus",
                  (unsigned long)min, (unsigned long)sec);
    }
    else
    {
        xsnprintf(buf, (int)buf_size, "%lus", (unsigned long)sec);
    }
}

static const char *gsm_log_level_name(gsm_log_level_t lvl);

static void cmd_status(const char *arg)
{
    (void)arg;
    uint32_t ip   = gsm_info_get_ip();
    uint8_t  rat  = gsm_info_get_access_technology();
    uint8_t  csq  = gsm_info_get_signal_quality();
    uint8_t  gprs = gsm_info_get_gprs_state();

    gsm_info_cell_t cell = {0};
    gsm_info_get_cell(&cell);

    uint32_t tx_cnt = 0U;
    uint32_t rx_cnt = 0U;
    gsm_info_get_rxtx_counters(&tx_cnt, &rx_cnt);

    phone_number_t phone = {0};
    modem_config_get_simcard_phone_number(&phone);

    uint32_t now = bsp_get_tick();

    /* ════════════════════════════════════════════════════════════════ */
    SHELL_LOG("\r\n");
    SHELL_LOG("  ╔══════════════════════════════════════╗\r\n");
    SHELL_LOG("  ║        GSM MODULE STATUS             ║\r\n");
    SHELL_LOG("  ╚══════════════════════════════════════╝\r\n");
    SHELL_LOG("\r\n");
    SHELL_LOG("  ── MODEM ─────────────────────────────────\r\n");
    SHELL_LOG("  State      : %s\r\n", main_state_str(gsm_get_main_state()));
    SHELL_LOG("  Log level  : %s\r\n", gsm_log_level_name(gsm_log_get_level()));
    SHELL_LOG("  Module     : %s\r\n", module_model_str(gsm_info_get_module_model()));
    SHELL_LOG("  Firmware   : %s\r\n", gsm_info_get_fw_version());
    SHELL_LOG("  IMEI       : %s\r\n", gsm_info_get_imei());
    SHELL_LOG("  ICCID      : %s\r\n", gsm_info_get_iccid());
    SHELL_LOG("  IMSI       : %s\r\n", gsm_info_get_imsi());
    SHELL_LOG("  Phone      : %.10s\r\n", phone.number);
    SHELL_LOG("  SIM        : %s\r\n", sim_state_str(gsm_info_get_sim_state()));
    SHELL_LOG("  APN        : %s\r\n", modem_config_get_sim_apn());

    SHELL_LOG("\r\n");
    SHELL_LOG("  ── NETWORK ───────────────────────────────\r\n");
    SHELL_LOG("  RAT        : %s\r\n", rat_str(rat));
    SHELL_LOG("  CSQ        : %u\r\n", (unsigned)csq);
    SHELL_LOG("  CESQ 2G    : %u\r\n", (unsigned)gsm_info_get_signal_quality_2G());
    SHELL_LOG("  CESQ 3G    : %u\r\n", (unsigned)gsm_info_get_signal_quality_3G());
    SHELL_LOG("  CESQ 4G    : %u\r\n", (unsigned)gsm_info_get_signal_quality_4G());
    SHELL_LOG("  CREG       : %s\r\n", net_reg_str(gsm_info_get_creg()));
    SHELL_LOG("  CGREG      : %s\r\n", net_reg_str(gsm_info_get_cgreg()));
    SHELL_LOG("  CEREG      : %s\r\n", net_reg_str(gsm_info_get_cereg()));
    SHELL_LOG("  GPRS       : %s\r\n", gprs ? "ATTACHED" : "DETACHED");
    SHELL_LOG("  IP         : %lu.%lu.%lu.%lu\r\n",
              (ip >> 24U) & 0xFFUL, (ip >> 16U) & 0xFFUL,
              (ip >>  8U) & 0xFFUL,  ip         & 0xFFUL);
    SHELL_LOG("  MCC        : %u\r\n", (unsigned)cell.mcc);
    SHELL_LOG("  MNC        : %u\r\n", (unsigned)cell.mnc);
    SHELL_LOG("  LAC        : %u\r\n", (unsigned)cell.lac);
    SHELL_LOG("  CI         : %u\r\n", (unsigned)cell.ci);
    SHELL_LOG("  TX bytes   : %lu\r\n", tx_cnt);
    SHELL_LOG("  RX bytes   : %lu\r\n", rx_cnt);

    SHELL_LOG("\r\n");
    SHELL_LOG("  ── PROCESSES ─────────────────────────────\r\n");
    SHELL_LOG("  %-10s %-12s %-12s %3s %3s %3s\r\n",
              "Name", "Socket", "Process", "NC", "RX", "NEW");
    SHELL_LOG("  ---------- ------------ ------------ --- --- ---\r\n");
    SHELL_LOG("  %-10s %-12s %-12s %3u %3u %3u\r\n",
              "Web",
              socket_state_str(gsm_get_listener_socket_state()),
              ls_state_str(gsm_listener_get_state(GSM_LISTENER_WEB)),
              (unsigned)gsm_listener_get_no_carrier(GSM_LISTENER_WEB),
              (unsigned)gsm_listener_get_rx_available(GSM_LISTENER_WEB),
              (unsigned)gsm_listener_get_new_conn_req(GSM_LISTENER_WEB));
    SHELL_LOG("  %-10s %-12s %-12s %3u %3u %3u\r\n",
              "IEC104",
              socket_state_str(gsm_get_socket_state(IEC104_LISTENER_SOCKET)),
              ls_state_str(gsm_listener_get_state(GSM_LISTENER_IEC104)),
              (unsigned)gsm_listener_get_no_carrier(GSM_LISTENER_IEC104),
              (unsigned)gsm_listener_get_rx_available(GSM_LISTENER_IEC104),
              (unsigned)gsm_listener_get_new_conn_req(GSM_LISTENER_IEC104));

    /* ── Per-socket details ─────────────────────────────────────── */
    SHELL_LOG("\r\n");
    SHELL_LOG("  ── SOCKET DETAILS ────────────────────────\r\n");
    {
        /* Show only Web and IEC104 sockets (Dialer excluded) */
        static const char * const s_names[] = { "Web", "IEC104" };
        static const uint8_t s_idx[]    = { LISTENER_SOCKET, IEC104_LISTENER_SOCKET };
        static const uint8_t s_conn_id[] = { 1U, 3U };
        static const uint8_t s_count = (uint8_t)(sizeof(s_idx) / sizeof(s_idx[0]));

        for (uint8_t n = 0U; n < s_count; n++)
        {
            uint8_t i = s_idx[n];
            const socket_si_info_t   *p_si   = gsm_get_si_info(s_conn_id[n]);
            const gsm_socket_stats_t *p_stat = gsm_socket_get_stats(i);
            uint32_t last = gsm_socket_get_last_activity(i);

            SHELL_LOG("\r\n");
            SHELL_LOG("  [%s] connId=%u\r\n", s_names[n], (unsigned)s_conn_id[n]);
            SHELL_LOG("  ......................................\r\n");

            /* SI data */
            if ((p_si != NULL) && p_si->is_valid)
            {
                SHELL_LOG("    TX         : %lu\r\n", (unsigned long)p_si->sent);
                SHELL_LOG("    RX         : %lu\r\n", (unsigned long)p_si->received);
                SHELL_LOG("    Buf in     : %lu\r\n", (unsigned long)p_si->buff_in);
                SHELL_LOG("    Ack wait   : %lu\r\n", (unsigned long)p_si->ack_waiting);
            }
            else
            {
                SHELL_LOG("    TX/RX      : n/a\r\n");
            }

            /* Last activity — only meaningful if there has been at least one session */
            if ((p_stat != NULL) && (p_stat->total_sessions > 0U) && (last != 0U))
            {
                uint32_t ago_ms = (now >= last) ? (now - last) : (now + (UINT32_MAX - last) + 1U);
                uint32_t ago_s  = ago_ms / 1000U;
                uint32_t epoch_now = bsp_get_epoch_time();
                uint32_t act_epoch = (epoch_now > ago_s) ? (epoch_now - ago_s) : 0U;
                datetime_t dt_act;
                char ago_buf[16];
                dt_conv_from_epoch(act_epoch, &dt_act);
                fmt_duration(ago_ms, ago_buf, sizeof(ago_buf));
                SHELL_LOG("    Last act.  : %04u-%02u-%02u %02u:%02u:%02u (%s ago)\r\n",
                          (unsigned)dt_act.date.year,
                          (unsigned)dt_act.date.month,
                          (unsigned)dt_act.date.day,
                          (unsigned)dt_act.time.hour,
                          (unsigned)dt_act.time.minute,
                          (unsigned)dt_act.time.second,
                          ago_buf);
            }
            else
            {
                SHELL_LOG("    Last act.  : -\r\n");
            }

            /* Session stats */
            if (p_stat != NULL)
            {
                if (p_stat->is_connected)
                {
                    uint32_t session_ms = (now >= p_stat->connect_tick)
                        ? (now - p_stat->connect_tick)
                        : (now + (UINT32_MAX - p_stat->connect_tick) + 1U);
                    char dur_buf[16];
                    datetime_t dt_conn;
                    dt_conv_from_epoch(p_stat->connect_epoch, &dt_conn);
                    fmt_duration(session_ms, dur_buf, sizeof(dur_buf));
                    SHELL_LOG("    Session    : ACTIVE\r\n");
                    SHELL_LOG("    Since      : %04u-%02u-%02u %02u:%02u:%02u\r\n",
                              (unsigned)dt_conn.date.year,
                              (unsigned)dt_conn.date.month,
                              (unsigned)dt_conn.date.day,
                              (unsigned)dt_conn.time.hour,
                              (unsigned)dt_conn.time.minute,
                              (unsigned)dt_conn.time.second);
                    SHELL_LOG("    Duration   : %s\r\n", dur_buf);
                }
                else if (p_stat->total_sessions > 0U)
                {
                    char dur_buf[16];
                    datetime_t dt_conn, dt_disc;
                    dt_conv_from_epoch(p_stat->last_connect_epoch, &dt_conn);
                    dt_conv_from_epoch(p_stat->last_disconnect_epoch, &dt_disc);
                    fmt_duration(p_stat->last_session_ms, dur_buf, sizeof(dur_buf));
                    SHELL_LOG("    Session    : CLOSED\r\n");
                    SHELL_LOG("    Connected  : %04u-%02u-%02u %02u:%02u:%02u\r\n",
                              (unsigned)dt_conn.date.year,
                              (unsigned)dt_conn.date.month,
                              (unsigned)dt_conn.date.day,
                              (unsigned)dt_conn.time.hour,
                              (unsigned)dt_conn.time.minute,
                              (unsigned)dt_conn.time.second);
                    SHELL_LOG("    Disconn.   : %04u-%02u-%02u %02u:%02u:%02u\r\n",
                              (unsigned)dt_disc.date.year,
                              (unsigned)dt_disc.date.month,
                              (unsigned)dt_disc.date.day,
                              (unsigned)dt_disc.time.hour,
                              (unsigned)dt_disc.time.minute,
                              (unsigned)dt_disc.time.second);
                    SHELL_LOG("    Duration   : %s\r\n", dur_buf);
                    SHELL_LOG("    Reason     : %s\r\n",
                              disconnect_reason_str(p_stat->last_disconnect_reason));
                }
                else
                {
                    SHELL_LOG("    Session    : NO SESSION YET\r\n");
                }

                /* Lifetime stats */
                char total_dur[16];
                char max_dur[16];
                fmt_duration(p_stat->total_connected_ms, total_dur, sizeof(total_dur));
                fmt_duration(p_stat->max_session_ms, max_dur, sizeof(max_dur));
                SHELL_LOG("    Sessions   : %lu\r\n", (unsigned long)p_stat->total_sessions);
                SHELL_LOG("    Disconnects: %lu\r\n", (unsigned long)p_stat->total_disconnects);
                SHELL_LOG("    Errors     : %lu\r\n", (unsigned long)p_stat->error_count);
                SHELL_LOG("    Attempts   : %lu\r\n", (unsigned long)p_stat->connect_attempts);
                SHELL_LOG("    Total time : %s\r\n", total_dur);
                SHELL_LOG("    Max session: %s\r\n", max_dur);
            }
        }
    }
    SHELL_LOG("\r\n");
    SHELL_LOG("  ══════════════════════════════════════════\r\n");
}

static const char *gsm_log_level_name(gsm_log_level_t lvl)
{
    switch (lvl)
    {
        case GSM_LOG_OFF:     return "OFF";
        case GSM_LOG_NORMAL:  return "NORMAL";
        case GSM_LOG_VERBOSE: return "VERBOSE";
        default:              return "?";
    }
}

static void cmd_log(const char *arg)
{
    if (arg == NULL)
    {
        SHELL_LOG("GSM log level: %s (%u)\r\n",
                  gsm_log_level_name(gsm_log_get_level()),
                  (unsigned)gsm_log_get_level());
        SHELL_LOG("Usage: gsm log <off|on|verbose>\r\n");
        return;
    }

    if (strcmp(arg, "off") == 0)
    {
        gsm_log_set_level(GSM_LOG_OFF);
        SHELL_LOG("GSM log: OFF\r\n");
        return;
    }

    if (strcmp(arg, "on") == 0)
    {
        gsm_log_set_level(GSM_LOG_NORMAL);
        SHELL_LOG("GSM log: NORMAL (warnings/errors)\r\n");
        return;
    }

    if (strcmp(arg, "verbose") == 0)
    {
        gsm_log_set_level(GSM_LOG_VERBOSE);
        SHELL_LOG("GSM log: VERBOSE (all)\r\n");
        return;
    }

    SHELL_LOG("Unknown level '%s'. Use: off | on | verbose\r\n", arg);
}

static void print_help(const char *arg)
{
    (void)arg;
    SHELL_LOG("GSM shell commands:\r\n");
    SHELL_LOG("  gsm at <CMD>       Send raw AT command (e.g. gsm at AT+CSQ)\r\n");
    SHELL_LOG("  gsm status         Show GSM modem state\r\n");
    SHELL_LOG("  gsm log [off|on|verbose]  Set/show GSM log level\r\n");
    SHELL_LOG("  gsm help           Show this help\r\n");
}

/* ============================================================================
 * COMMAND DISPATCH TABLE
 * ============================================================================ */

typedef struct
{
    const char *subcmd;
    void (*fn)(const char *arg);
} gsm_subcmd_t;

static const gsm_subcmd_t s_subcmds[] =
{
    { "at",     cmd_at      },
    { "status", cmd_status  },
    { "log",    cmd_log     },
    { "help",   print_help  },
};

#define GSM_SUBCMD_COUNT  (sizeof(s_subcmds) / sizeof(s_subcmds[0]))

static int gsm_shell_cmd_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        print_help(NULL);
        return -1;
    }

    const char *subcmd = argv[1];

    for (size_t i = 0U; i < GSM_SUBCMD_COUNT; i++)
    {
        if (strcmp(subcmd, s_subcmds[i].subcmd) == 0)
        {
            s_subcmds[i].fn((argc >= 3) ? argv[2] : NULL);
            return 0;
        }
    }

    SHELL_CLOG(XCOLOR_YELLOW, "Unknown subcommand: %s\r\n", subcmd);
    print_help(NULL);
    return -1;
}

/* ============================================================================
 * INIT
 * ============================================================================ */

void gsm_shell_init(void)
{
    memset(s_state.at_cmd, 0, sizeof(s_state.at_cmd));

    shell_register_command(&(shell_cmd_t){
        .cmd   = "gsm",
        .desc  = "GSM modem commands. Type 'gsm help' for usage.\r\n",
        .level = SHELL_LVL_USER,
        .func  = gsm_shell_cmd_handler
    });
}
