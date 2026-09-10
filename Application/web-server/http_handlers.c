/**
 * @file http_handlers.c
 * @brief HTTP endpoint handlers implementation
 *
 *  Created on: Apr 18, 2026
 *      Author: Fatih Ozcan
 */

#define CSLOG_MODULE LOG_MOD_HTTP
#include "http_handlers.h"
#include "http_response.h"
#include "http_request_parser.h"
#include "index_html.h"
#include "fw_update_html.h"
#include "web_shell.h"
#include "json_config.h"
#include "xprintf.h"
#include <string.h>
#include "../utils.h"
#include "version.h"
#include "boot.h"
#include "app_ipc.h"
#include "reboot.h"

#include "system_status.h"
#include "index_html.h"
#include "modbus_config.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "rf.h"
#include "rf_config.h"
#include "rf_discovery.h"
#include "rf_json.h"
#include "gsm_engine.h"
#include "elog.h"
#include "fault_log.h"
#include "cp56time2a.h"
#include "datetime.h"
#include "bsp.h"
#include "rtc.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define USER_ROLE_ADMIN "admin"
#define USER_ROLE_USER "user"

/** Session idle timeout: if no authenticated request is received within this
 *  period the session token is invalidated and re-login is required. */
#define HTTP_SESSION_TIMEOUT_MS (15UL * 60UL * 1000UL)

/* ============================================================================
 * MODULE STATE
 * ============================================================================ */

static struct {
    char *tx_buffer;
    int tx_buffer_size;
    const char *current_query_string;  /* Current request query string */
    bool is_authenticated;             /* Per-request auth flag, set by set_auth_from_token() */
    char username[16];                 /* Logged-in role: "admin" or "user" */
    uint32_t session_token;            /* 0 = no active session */
    uint32_t last_activity_tick;       /* bsp_get_tick() of last authenticated request */
} handler_state;


/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void http_handlers_init(char *tx_buffer_ptr, int tx_buffer_size)
{
    handler_state.tx_buffer = tx_buffer_ptr;
    handler_state.tx_buffer_size = tx_buffer_size;
    handler_state.current_query_string = NULL;
    handler_state.is_authenticated = false;
    handler_state.session_token = 0U;
    handler_state.last_activity_tick = 0U;
}

void http_handlers_set_query_string(const char *query_string)
{
    handler_state.current_query_string = query_string;
}

void http_handlers_reset(void)
{
    /* Clear the per-request auth flag and stale query string pointer.
     * session_token and username are intentionally preserved so the client
     * can continue using the same token after a socket reconnect. */
    handler_state.is_authenticated = false;
    handler_state.current_query_string = NULL;
    CSLOG("[HTTP] Connection closed, session preserved\r\n");
}

char* http_handlers_get_tx_buffer(int *size)
{
    if (size) {
        *size = handler_state.tx_buffer_size;
    }
    return handler_state.tx_buffer;
}

const char* http_handlers_get_query_string(void)
{
    return handler_state.current_query_string;
}

bool http_handlers_session_active(void)
{
    return handler_state.session_token != 0U;
}

bool http_handlers_is_authenticated(void)
{
    return handler_state.is_authenticated;
}

bool http_handlers_is_admin(void)
{
    return handler_state.is_authenticated && 
           strcmp(handler_state.username, USER_ROLE_ADMIN) == 0;
}


/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* ============================================================================
 * TOKEN AUTHENTICATION
 * ============================================================================ */

/**
 * @brief Parse the 8-char hex session token from a query string.
 *
 * Looks for the "t=" parameter (e.g. "?config=1&t=A3F2B1C8").
 * Parsing is done without sscanf or strtoul to stay stdlib-free.
 *
 * @param qs Query string (may be NULL)
 * @return Parsed token value, or 0U on any error
 */
static uint32_t parse_token_from_query(const char *qs)
{
    if (!qs) {
        return 0U;
    }

    /* Find "t=" as a standalone parameter key, not a substring of another key.
     * Valid positions: start of string ("t=...") or after '&' ("...&t=..."). */
    const char *t_ptr = NULL;
    if (qs[0] == 't' && qs[1] == '=') {
        t_ptr = qs + 2;
    } else {
        const char *amp = strstr(qs, "&t=");
        if (amp) {
            t_ptr = amp + 3;
        }
    }
    if (!t_ptr) {
        return 0U;
    }

    uint32_t result = 0U;
    uint8_t  digits = 0U;
    while (digits < 8U) {
        char     c      = t_ptr[digits];
        uint32_t nibble = 0U;
        if (c >= '0' && c <= '9') {
            nibble = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint32_t)(c - 'A') + 10U;
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint32_t)(c - 'a') + 10U;
        } else {
            return 0U; /* short or invalid token */
        }
        result = (result << 4U) | nibble;
        digits++;
    }
    return result;
}

/**
 * @brief Validate the session token from the request query string.
 *
 * Must be called at the start of every request, before the auth gate.
 * Sets handler_state.is_authenticated accordingly.
 *
 * @param query_string Request query string (may be NULL)
 */
void http_handlers_set_auth_from_token(const char *query_string)
{
    /* No active session */
    if (handler_state.session_token == 0U) {
        handler_state.is_authenticated = false;
        return;
    }

    uint32_t received = parse_token_from_query(query_string);
    if (received != handler_state.session_token) {
        handler_state.is_authenticated = false;
        return;
    }

    /* Check idle TTL */
    if ((bsp_get_tick() - handler_state.last_activity_tick) > HTTP_SESSION_TIMEOUT_MS) {
        handler_state.is_authenticated = false;
        handler_state.session_token    = 0U;
        handler_state.username[0]      = '\0';
        CSLOG("[HTTP] Session expired\r\n");
        return;
    }

    /* Token valid and within TTL */
    handler_state.is_authenticated   = true;
    handler_state.last_activity_tick = bsp_get_tick();
}


/* ============================================================================
 * GET HANDLERS
 * ============================================================================ */

void handle_get_index(void)
{
    /* Serve index.html from flash */
    http_send_html_resource(get_index_html());
}


void handle_get_fw_update(void)
{
    /* Serve fw_update.html from flash */
    http_send_html_resource(get_fw_update_html());
}


/* ============================================================================
 * POST HANDLERS
 * ============================================================================ */

/**
 * @brief Handle POST /auth/login - Simple IP-based authentication
 */
void handle_post_login(const char *json_body)
{
    CSLOG("[HTTP] POST /auth/login - Login attempt\r\n");
    
    if (!json_body) {
        http_send_json("{\"success\":false,\"error\":\"No body\"}", 35);
        return;
    }
    
    /* Parse username and password from JSON */
    char username[32] = {0};
    char password[64] = {0};
    
    /* Simple JSON parsing using strstr */
    const char *user_start = strstr(json_body, "\"username\":\"");
    const char *pass_start = strstr(json_body, "\"password\":\"");
    
    if (user_start && pass_start) {
        user_start += 12; /* Skip \"username\":" */
        const char *user_end = strchr(user_start, '\"');
        if (user_end && (user_end - user_start) < sizeof(username)) {
            strncpy(username, user_start, user_end - user_start);
        }
        
        pass_start += 12; /* Skip \"password\":" */
        const char *pass_end = strchr(pass_start, '\"');
        if (pass_end && (pass_end - pass_start) < sizeof(password)) {
            strncpy(password, pass_start, pass_end - pass_start);
        }
    }
    
    CSLOG("[HTTP] Login attempt - Username: %s\r\n", username);
    
    /* Get device IP address */
    uint32_t ip = gsm_get_ip_addr();
    uint8_t ip_a = (ip >> 24) & 0xFF;  /* First octet */
    uint8_t ip_d = ip & 0xFF;           /* Last octet */
    
    CSLOG("[HTTP] Device IP octets - First: %d, Last: %d\r\n", ip_a, ip_d);
    
    /* Calculate expected passwords based on IP.
     * 12 bytes covers the longest value ("admin256" + NUL);
     * see doc/Web_Giris_Sifresi_Plani.md. */
    char expected_admin_pass[12] = {0};
    char expected_user_pass[12] = {0};
    unsigned int admin_len = xsnprintf(expected_admin_pass, sizeof(expected_admin_pass),
                                       "admin%d", ip_d + 1);
    unsigned int user_len = xsnprintf(expected_user_pass, sizeof(expected_user_pass),
                                      "user%d", ip_a + 1);
    if ((admin_len >= sizeof(expected_admin_pass)) ||
        (user_len >= sizeof(expected_user_pass)))
    {
        CSLOG_ERR("[HTTP] Login password truncated - increase buffer!\r\n");
    }
    
    /* Validate credentials */
    bool valid = false;
    if (strcmp(username, USER_ROLE_ADMIN) == 0 && strcmp(password, expected_admin_pass) == 0) {
        valid = true;
        strncpy(handler_state.username, USER_ROLE_ADMIN, sizeof(handler_state.username) - 1);
        handler_state.username[sizeof(handler_state.username) - 1] = '\0';
        CSLOG("[HTTP] Admin login successful\r\n");
    } else if (strcmp(username, USER_ROLE_USER) == 0 && strcmp(password, expected_user_pass) == 0) {
        valid = true;
        strncpy(handler_state.username, USER_ROLE_USER, sizeof(handler_state.username) - 1);
        handler_state.username[sizeof(handler_state.username) - 1] = '\0';
        CSLOG("[HTTP] User login successful\r\n");
    } else {
        CSLOG_ERR("[HTTP] Login failed - Invalid credentials\r\n");
        elog_log_web_login_fail(gsm_get_web_client_ip());
    }
    
    /* Send response */
    if (valid) {
        /* Generate session token */
        uint32_t new_token = bsp_get_tick() ^ 0x5A5A0000UL ^ (uint32_t)(uint8_t)handler_state.username[0];
        if (new_token == 0U) {
            new_token = 0xDEADBEEFUL;
        }
        handler_state.session_token      = new_token;
        handler_state.last_activity_tick = bsp_get_tick();
        handler_state.is_authenticated   = true;

        CSLOG("[HTTP] Session authenticated, token: %08lX\r\n", (unsigned long)new_token);

        char *buf = handler_state.tx_buffer;
        int   pos = xsnprintf(buf, handler_state.tx_buffer_size,
                              "{\"success\":true,\"token\":\"%08lX\",\"role\":\"%s\"}",
                              (unsigned long)new_token, handler_state.username);
        http_send_json(buf, pos);
    } else {
        const char *error_response = "{\"success\":false,\"error\":\"Invalid credentials\"}";
        http_send_json(error_response, strlen(error_response));
    }
}

/**
 * @brief Handle POST /auth/logout - Clear session
 */
void handle_post_logout(void)
{
    CSLOG("[HTTP] POST /auth/logout - Logout request\r\n");
    
    /* Clear authentication state */
    handler_state.is_authenticated = false;
    handler_state.session_token    = 0U;
    memset(handler_state.username, 0, sizeof(handler_state.username));

    CSLOG("[HTTP] Session cleared\r\n");
    
    /* Send success response */
    const char *success_response = "{\"success\":true}";
    http_send_json(success_response, strlen(success_response));
}

void handle_post_echo(const char *body, int body_length)
{
    /* Limit echo size */
    if (body_length > 512) {
        http_send_error(413, "Payload Too Large");
        return;
    }

    http_send_ok(body, body_length);
}

void handle_post_web_shell(const char *json_body)
{
    if (!json_body) {
        http_send_bad_request();
        return;
    }

    /* Build {"rx":"..."}: capture terminal output directly into the
     * response buffer while the command runs (no intermediate copy). */
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = xsnprintf(buf, buf_size, "{\"rx\":\"");

    /* Parse "tx" field from JSON: {"tx":"..."} */
    int flush_len = 0;
    const char *tx_start = strstr(json_body, "\"tx\":\"");
    if (tx_start) {
        tx_start += 6; /* skip '"tx":"' */
        char *tx_end = strchr(tx_start, '\"');
        if (tx_end) {
            *tx_end = '\0';
            uint16_t len = (uint16_t)(tx_end - tx_start);
            if (len > 0) {
                web_shell_capture_begin(buf + pos, buf_size - pos - 2);
                web_shell_on_rx((const uint8_t *)tx_start, len);
                flush_len = web_shell_capture_end();
            }
        }
    }

    if (flush_len == 0) {
        /* No immediate data; client must poll GET /serial */
        http_send_json("{\"pending\":true}", 16);
        return;
    }

    pos += flush_len;
    pos += xsnprintf(buf + pos, buf_size - pos, "\"}");
    http_send_json(buf, pos);
}

void handle_get_web_shell(void)
{
    /* Capture is request-scoped: the POST response already carried the
     * command output, background output is never captured. */
    http_send_json("{\"rx\":\"\"}", 9);
}


/* ============================================================================
 * JSON CONFIGURATION HANDLERS
 * ============================================================================ */

/**
 * @brief Handle GET /r?deviceConfig - Read device configuration as JSON
 */
void handle_get_device_config_json(void)
{
    CSLOG("[HTTP] GET /r?deviceConfig - Reading device config\r\n");
    const modem_config_t *config = get_device_config();
    if (!config) {
        CSLOG_ERR("[HTTP] ERROR: Config not available\r\n");
        http_send_error(500, "Config not available");
        return;
    }
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    pos += xsnprintf(buf + pos, buf_size - pos, "{");
    
    /* RO Fields */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SeriNumarasi\":%u,", config->serial_number);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"CihazKoordinati\":{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"MCC\":\"%s\",", config->coordinates.mcc);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"MNC\":\"%s\",", config->coordinates.mnc);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"LAC\":\"%s\",", config->coordinates.lac);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"CI\":\"%s\"", config->coordinates.ci);
    pos += xsnprintf(buf + pos, buf_size - pos, "},");
    /* TODO: Convert epoch to human-readable date format */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"UretimTarihi\":%u,", config->production_date);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"LifeTime\":%u,", config->lifetime);

    /* Son resetten bu yana gecen sure (saniye) - bsp_get_run_time uzerinden
     * ayri cagri (shell'deki "Run Time" satiri ile ayni kaynak). NVRAM'de
     * birikmez; tick acilista sifirdan baslar, ~49.7 gunde sarar. */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"RunTime\":%u,",
                     (unsigned)(bsp_get_run_time() / 1000U));

    fw_info_t fw = *boot_get_installed_fw_info();

    pos += xsnprintf(buf + pos, buf_size - pos,
                     "\"ModemYazilimVeriyonu\":\"v%u.%u.%u (%04u-%02u-%02u %02u:%02u:%02u %s)\",",
                     (unsigned)VERSION_MAJOR,
                     (unsigned)VERSION_MINOR,
                     (unsigned)VERSION_PATCH,
                     (unsigned)fw.year, (unsigned)fw.month, (unsigned)fw.day,
                     (unsigned)fw.hour, (unsigned)fw.minute, (unsigned)fw.second,
                     (const char *)fw.short_commit_hash);

    /* Kurulum zamani: ham epoch sayisi (UI fmtEpoch ile cevirir; 0 ->
     * '-----'). Bootloader bu alani henuz RTC ile doldurmuyor. */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"KurulumTarihi\":%lu,",
                     (unsigned long)fw.installation_date);

    pos += xsnprintf(buf + pos, buf_size - pos, "\"RFYazilimVeriyonu\":\"v%u.%u.%u\",",
                     config->rf_firmware_version[0],
                     config->rf_firmware_version[1],
                     config->rf_firmware_version[2]);

    /* RW Fields */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"WebArayuzuPortu\":%u,", config->web_interface_port);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SimKartPin\":%u,", config->sim_card_pin);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SimKartAPN\":\"%s\",", config->apn.apn);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SimKartAPNUsername\":\"%s\",", config->apn.user_name);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SimKartAPNSifresi\":\"%s\",", config->apn.user_pass);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"NtpServer\":\"%s\",", config->ntp_server);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"NtpServerPortu\":%u,", config->ntp_server_port);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Time\":%u,", rtc_get_epoch());
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TimeZone\":%d,", config->time_zone);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"PeriyodikModemResetPeriyodu\":%u,", config->periodic_modem_reset_period);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"WebIlkVeriZamanAsimi\":%u,", config->web_first_data_timeout_sec);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"WebBostaKalmaZamanAsimi\":%u,", config->web_idle_timeout_sec);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IEC104IlkVeriZamanAsimi\":%u,", config->iec104_first_data_timeout_sec);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IEC104BostaKalmaZamanAsimi\":%u,", config->iec104_idle_timeout_sec);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"DevreyeAlinmaZamani\":%u", config->commissioning_time);
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    CSLOG("[HTTP] JSON response size: %d bytes\r\n", pos);
    if (pos >= buf_size - 1) {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    http_send_json(buf, pos);
}

/**
 * @brief Handle POST /w?deviceConfig - Write device configuration from JSON
 */
void handle_post_device_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?deviceConfig - Updating device config\r\n");
    
    if (!json_body) {
    	CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }
    
    CSLOG("[HTTP] JSON body: %.100s...\r\n", json_body);
    
    modem_config_t config;

    /* Load current config as base - preserves RO fields and non-updated values */
    config = *get_device_config();

    if (!parse_device_config(json_body, &config)) {
    	CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }
    
    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] SimKartAPN: %s\r\n", config.apn.apn);
    
    /* Save config via data_model */
    int res = set_device_config(&config);

    elog_log_config_change(ELOG_CONFIG_DEVICE_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "device", (res == 0));
    CSLOG("[HTTP] Config %s to data_model\r\n",
          (res == 0) ? "saved" : "save FAILED (kept in RAM)");

    if (res != 0) {
        http_send_error(500, "Failed to save configuration");
        return;
    }

    const char *response_body = "{\"message\":\"Device configuration saved\",\"success\":true}";
    http_send_json(response_body, strlen(response_body));
}

/**
 * @brief Handle GET /r?boardStatus - Read board status as JSON
 */
void handle_get_board_status_json(void)
{
    CSLOG("[HTTP] GET /r?boardStatus - Reading board status\r\n");
    
    const system_status_t *status = system_status_get();


    if (!status) {
        CSLOG_ERR("[HTTP] ERROR: Board status not available\r\n");
        http_send_error(500, "Board status not available");
        return;
    }
    
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    
    pos += xsnprintf(buf + pos, buf_size - pos, "{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"DIN\":[%u,%u,%u,%u],", 
                    status->din[0], status->din[1], status->din[2], status->din[3]);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"RLY\":[%u,%u],", status->rly[0], status->rly[1]);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"3V3\":%u,", status->v3v3);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"3V8\":%u,", status->v3v8);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"5V\":%u,", status->v5v);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ChargeState\":%u,", status->charge_state);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Temp\":%d,", status->temp);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TempMax\":%d,", status->temp_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TempMin\":%d,", status->temp_min);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"PanelAkimi\":%d,", status->panel_current);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"PanelVoltaji\":%u,", status->panel_voltage);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BataryaVoltaji\":%u,", status->battery_voltage);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TDIE\":%d,", status->tdie_temp);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TDIEMax\":%d,", status->tdie_temp_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"TDIEMin\":%d,", status->tdie_temp_min);
    /* x10 alanlari ham gonderilir (995 = %99.5, 245 = 24.5C);
     * donusum istemci tarafinda gosterim aninda yapilir. */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ChargePertance\":%d,", status->battery_charge_x10);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Capacity\":%u,", status->battery_capacity);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"GsmSig\":%d,", status->gsm_signal);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"GsmRAT\":%u,", status->gsm_rat);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BataryaAkimi\":%d,", status->battery_current);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BatteryTemp\":%d,", status->battery_temp_x10);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BatterySOC\":%d,", status->battery_soc_x10);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BatterySOH\":%u,", status->battery_soh_x10);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"OrtamSicakligi\":%d,", status->ambient_temp);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"HeaterState\":%u,", status->heater_state);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"HeaterPower\":%u", status->heater_power);
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    CSLOG("[HTTP] Board status JSON size: %d bytes\r\n", pos);
    
    /* Buffer overflow check */
    if (pos >= buf_size - 1) {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    
    http_send_json(buf, pos);
}

/**
 * @brief Handle GET /syslogs?offset=X&limit=Y - Read system logs as JSON
 */
void handle_get_syslogs_json(void)
{
    CSLOG("[HTTP] GET /syslogs - Reading system logs\r\n");
    
    /* Parse query parameters */
    uint32_t offset = 0;
    uint32_t limit = 30;  /* Default: 30 records per page */
    
    if (handler_state.current_query_string) {
        const char *offset_ptr = strstr(handler_state.current_query_string, "offset=");
        if (offset_ptr) {
            offset = (uint32_t)xstrtoi(offset_ptr + 7);
        }
        
        const char *limit_ptr = strstr(handler_state.current_query_string, "limit=");
        if (limit_ptr) {
            limit = (uint32_t)xstrtoi(limit_ptr + 6);
            if (limit > 100) limit = 100;  /* Cap at 100 to prevent buffer overflow */
        }
    }
    
    /* Exact stored count (ring scan): with the deferred-erase policy the
     * ring holds up to every sector's worth of records; torn holes reduce
     * it. Used for pagination windowing and the "t" JSON field. */
    uint32_t available_entries = elog_get_stored_count();

    CSLOG("[HTTP] System Logs - offset=%lu, limit=%lu, available=%lu\r\n",
            offset, limit, available_entries);
    
    /* Sanitize offset */
    if (offset >= available_entries) {
        offset = 0;
    }
    
    /* Calculate actual records to return */
    uint32_t end_idx = offset + limit;
    if (end_idx > available_entries) {
        end_idx = available_entries;
    }
    uint32_t count = end_idx - offset;
    
    /* Build JSON response */
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    
    /* Start JSON: {"recs":"..." */
    pos += xsnprintf(buf + pos, buf_size - pos, "{\"recs\":\"");

    /* Read the requested window (entries after skipping the `offset` newest)
     * in chunks and format newest-first, one entry per line. */
    uint32_t emitted = 0;
    uint32_t skip = offset;
    uint8_t first_line = 1;

    /* Stop appending entries before the tail room is gone: one decoded
     * entry line is well under this margin and the JSON wrapper needs a
     * few bytes. Whatever did not fit is served by the next paged
     * request (the frontend counts returned lines, not the limit). */
#define SYSLOGS_HEADROOM_BYTES 256

    while (emitted < count)
    {
        elog_entry_t chunk[8];
        uint32_t chunk_len = 0;
        uint32_t want = count - emitted;
        uint8_t headroom_left = 1;

        if (want > (uint32_t)(sizeof(chunk) / sizeof(chunk[0]))) {
            want = (uint32_t)(sizeof(chunk) / sizeof(chunk[0]));
        }

        if (elog_read_recent(skip, want, chunk, &chunk_len) != 0 || chunk_len == 0) {
            break;
        }

        /* elog_read_recent returns newest first; display index 1 is the
         * newest log overall, so the number continues across chunks. */
        for (uint32_t i = 0; i < chunk_len; i++)
        {
            if (pos > (buf_size - SYSLOGS_HEADROOM_BYTES)) {
                headroom_left = 0;
                break;
            }

            const elog_entry_t *entry = &chunk[i];
            uint32_t display_idx = offset + emitted + 1;

            if (!first_line) {
                pos += xsnprintf(buf + pos, buf_size - pos, "\\n");
            }
            first_line = 0;

            /* Format: "#INDEX TS:timestamp LVL:level CODE:code INFO text\n" */
            /* Info is decoded by elog (per-event text), JSON-escaped here */

            datetime_t dt;
            dt_conv_from_epoch(entry->timestamp, &dt);

            pos += xsnprintf(buf + pos, buf_size - pos,
                            "#%lu TS:%04u-%02u-%02u %02u:%02u:%02u LVL:%u CODE:%s INFO:",
                            display_idx,
                            dt.date.year, dt.date.month, dt.date.day, dt.time.hour, dt.time.minute, dt.time.second,
                            entry->level, elog_code_to_string((elog_code_t)entry->code));

            for (const char *q = elog_info_to_text(entry); *q != '\0'; q++) {
                if ((*q == '"') || (*q == '\\')) {
                    pos += xsnprintf(buf + pos, buf_size - pos, "\\");
                }
                pos += xsnprintf(buf + pos, buf_size - pos, "%c", *q);
            }

            emitted++;
        }

        skip += chunk_len;

        if (headroom_left == 0U) {
            break;
        }
    }

    /* Close "recs" field and add total count */
    pos += xsnprintf(buf + pos, buf_size - pos, "\",\"t\":%u", available_entries);
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    CSLOG("[HTTP] System Logs JSON size: %d bytes\r\n", pos);
    
    /* Buffer overflow check */
    if (pos >= buf_size - 1) {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    
    http_send_json(buf, pos);
}

/**
 * @brief Handle GET /r?iecConfig - Read IEC104 config as JSON
 */
void handle_get_iec_config_json(void)
{
    CSLOG("[HTTP] GET /r?iecConfig - Reading IEC config\r\n");
    
    const iec104_config_t *config = iec104_config_get();
    if (!config) {
    	CSLOG_ERR("[HTTP] ERROR: IEC config not available\r\n");
        http_send_error(500, "IEC config not available");
        return;
    }
    
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    
    // Convert IP from uint32_t to string
    uint32_t ip = config->scada_ip_address;
    int ip_a = (ip >> 24) & 0xFF;
    int ip_b = (ip >> 16) & 0xFF;
    int ip_c = (ip >> 8) & 0xFF;
    int ip_d = ip & 0xFF;
    
    pos += xsnprintf(buf + pos, buf_size - pos, "{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ScadaIPAdresi\":\"%d.%d.%d.%d\",", ip_a, ip_b, ip_c, ip_d);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"PeriodicSend\":%u,", config->periodical_send_interval);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Port\":%u,", config->scada_port);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"T0\":%u,", config->t0_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"T1\":%u,", config->t1_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"T2\":%u,", config->t2_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"T3\":%u,", config->t3_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"K\":%u,", config->k_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"W\":%u,", config->w_max);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"OriginatorAddr\":%u,", config->originator_address);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"CommonAddr\":%u,", config->common_address);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SBO\":%s,", config->is_sbo_active ? "true" : "false");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SBOTimeout\":%u,", config->sbo_execute_timeout);
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"AkuUyarisi\":%u,", iec104_ioa_3byte_to_uint32(config->ioa_aku_uyarisi));
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ModemReset\":%u,", iec104_ioa_3byte_to_uint32(config->ioa_modem_reset));

    /* Hatlar bilgisi */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Hatlar\":{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"inUse\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%s%s", (line && line->in_use) ? "true" : "false", (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_R_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L1]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_S_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L2]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"IOA_T_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L3]) : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "]");
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    CSLOG("[HTTP] IEC config JSON size: %d bytes\r\n", pos);
    
    /* Buffer overflow check */
    if (pos >= buf_size - 1) {
    	CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    
    http_send_json(buf, pos);
}

/**
 * @brief Handle POST /w?iecConfig - Write IEC104 configuration from JSON
 */
void handle_post_iec_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?iecConfig - Updating IEC config\r\n");
    
    if (!json_body) {
    	CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }
    
    int body_len = strlen(json_body);
    CSLOG("[HTTP] JSON body length: %d bytes\r\n", body_len);
    CSLOG("[HTTP] JSON body (full): %s\r\n", json_body);
    
    // Create local config from current NVRAM state
    jiec_config_t config = {0};
    const iec104_config_t *nvram_config = iec104_config_get();
    if (!nvram_config) {
        CSLOG_ERR("[HTTP] ERROR: Cannot read current config\r\n");
        http_send_error(500, "Config read error");
        return;
    }
    
    // Convert NVRAM config to JSON config format (for partial updates)
    config.periodical_send_interval = nvram_config->periodical_send_interval;
    uint32_t ip = nvram_config->scada_ip_address;
    xsnprintf(config.scada_ip_address, sizeof(config.scada_ip_address), "%d.%d.%d.%d",
              (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF), 
              (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
    config.scada_port = nvram_config->scada_port;
    config.t0_timeout = (uint8_t)nvram_config->t0_max;
    config.t1_timeout = (uint8_t)nvram_config->t1_max;
    config.t2_timeout = (uint8_t)nvram_config->t2_max;
    config.t3_timeout = (uint8_t)nvram_config->t3_max;
    config.k_max = nvram_config->k_max;
    config.w_max = nvram_config->w_max;
    config.sbo_timeout = nvram_config->sbo_execute_timeout;
    config.sbo_active = nvram_config->is_sbo_active != 0;
    config.originator_address = nvram_config->originator_address;
    config.common_address = nvram_config->common_address;
    config.ioa_aku_uyarisi = iec104_ioa_3byte_to_uint32(nvram_config->ioa_aku_uyarisi);
    config.ioa_modem_reset = iec104_ioa_3byte_to_uint32(nvram_config->ioa_modem_reset);
    
    // Convert line configs
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        if (line && line->in_use) {
            config.line.in_use[i] = true;
            config.line.ioa_r_ariza_akimi[i] = iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L1]);
            config.line.ioa_s_ariza_akimi[i] = iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L2]);
            config.line.ioa_t_ariza_akimi[i] = iec104_ioa_3byte_to_uint32(line->ariza_akimi[PHASE_L3]);
            config.line.ioa_r_ariza_suresi[i] = iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L1]);
            config.line.ioa_s_ariza_suresi[i] = iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L2]);
            config.line.ioa_t_ariza_suresi[i] = iec104_ioa_3byte_to_uint32(line->ariza_suresi[PHASE_L3]);
            config.line.ioa_r_ariza_turu[i] = iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L1]);
            config.line.ioa_s_ariza_turu[i] = iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L2]);
            config.line.ioa_t_ariza_turu[i] = iec104_ioa_3byte_to_uint32(line->ariza_kalicimi[PHASE_L3]);
            config.line.ioa_r_anlik_akim[i] = iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L1]);
            config.line.ioa_s_anlik_akim[i] = iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L2]);
            config.line.ioa_t_anlik_akim[i] = iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L3]);
            config.line.ioa_r_enerji_varyok[i] = iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L1]);
            config.line.ioa_s_enerji_varyok[i] = iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L2]);
            config.line.ioa_t_enerji_varyok[i] = iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L3]);
            config.line.ioa_r_nominal_akim_varyok[i] = iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L1]);
            config.line.ioa_s_nominal_akim_varyok[i] = iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L2]);
            config.line.ioa_t_nominal_akim_varyok[i] = iec104_ioa_3byte_to_uint32(line->nominal_akim_varyok[PHASE_L3]);
            config.line.ioa_r_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L1]);
            config.line.ioa_s_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L2]);
            config.line.ioa_t_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[PHASE_L3]);
        } else {
            config.line.in_use[i] = false;
        }
    }
    
    if (!parse_iec_config(json_body, &config)) {
    	CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }
    
    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] ScadaIPAdresi: %s\r\n", config.scada_ip_address);
    CSLOG("[HTTP] ScadaPort: %u\r\n", config.scada_port);
    CSLOG("[HTTP] PeriyodikGonderimZamani: %u\r\n", config.periodical_send_interval);
    
    /* Save config to persistent storage */
    int res = set_iec_config(&config);

    elog_log_config_change(ELOG_CONFIG_IEC104_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "iec104", (res == 0));
    CSLOG("[HTTP] IEC config %s\r\n",
          (res == 0) ? "saved" : "save FAILED (kept in RAM)");

    if (res != 0) {
        http_send_error(500, "Failed to save configuration");
        return;
    }

    const char *response_body = "{\"message\":\"IEC104 configuration saved\",\"success\":true}";
    http_send_json(response_body, strlen(response_body));

}

/**
 * @brief Handle GET /r?modbusConfigs - Read Modbus configuration as JSON
 */
void handle_get_modbus_config_json(void)
{
    CSLOG("[HTTP] GET /r?modbusConfigs - Reading Modbus config\r\n");
    
    const modbus_configs_t *config = modbus_config_get();
    if (!config) {
        CSLOG_ERR("[HTTP] ERROR: Modbus config not available\r\n");
        http_send_error(500, "Modbus config not available");
        return;
    }
    
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    
    pos += xsnprintf(buf + pos, buf_size - pos, "{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"CihazID\":%u,", config->device_addr);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SonHataKodu\":%u,", config->last_error_code);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"SonHataZamani\":%u,", modbus_config_get_last_error_time());
    pos += xsnprintf(buf + pos, buf_size - pos, "\"BaudRate\":%u,", config->baud_rate);
    
    /* Hat bilgisi */
    pos += xsnprintf(buf + pos, buf_size - pos, "\"Hat\":{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"inUse\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%s%s", (line && line->in_use) ? "true" : "false", (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_akimi[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_akimi[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_ArizaAkimi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_akimi[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_suresi[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_suresi[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_ArizaSuresi\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_suresi[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_kalicimi[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_kalicimi[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_ArizaTuru\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->ariza_kalicimi[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->anlik_akim[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->anlik_akim[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_AnlikAkim\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->anlik_akim[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->enerji_varyok[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->enerji_varyok[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_EnerjiVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->enerji_varyok[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->nominal_akim_varyok[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->nominal_akim_varyok[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_NominalAkimVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->nominal_akim_varyok[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_R_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->rf_haberlesme_varyok[PHASE_L1] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_S_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->rf_haberlesme_varyok[PHASE_L2] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "],");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "\"ADDR_T_RfhabVarYok\":[");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        pos += xsnprintf(buf + pos, buf_size - pos, "%u%s", line ? line->rf_haberlesme_varyok[PHASE_L3] : 0, (i < MAX_ARRAYS - 1) ? "," : "");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "]");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    CSLOG("[HTTP] Modbus config JSON size: %d bytes\r\n", pos);
    
    /* Buffer overflow check */
    if (pos >= buf_size - 1) {
    	CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    
    http_send_json(buf, pos);
}

/**
 * @brief Handle POST /w?modbusConfigs - Write Modbus configuration from JSON
 */
void handle_post_modbus_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?modbusConfigs - Updating Modbus config\r\n");
    
    if (!json_body) {
        CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }
    
    int body_len = strlen(json_body);
    CSLOG("[HTTP] JSON body length: %d bytes\r\n", body_len);
    
    // Create local config from current NVRAM state
    jmodbus_configs_t config = {0};
    const modbus_configs_t *nvram_config = modbus_config_get();
    if (!nvram_config) {
        CSLOG_ERR("[HTTP] ERROR: Cannot read current config\r\n");
        http_send_error(500, "Config read error");
        return;
    }
    
    // Convert NVRAM config to JSON config format (for partial updates)
    config.device_addr = nvram_config->device_addr;
    config.last_error_code = nvram_config->last_error_code;
    config.baud_rate = nvram_config->baud_rate;
    config.addr_aku_uyarisi = nvram_config->addr_aku_uyarisi;
    config.addr_modem_reset = nvram_config->addr_modem_reset;
    
    // Convert line configs
    for (int i = 0; i < MAX_ARRAYS; i++) {
        const modbus_line_config_t *line = modbus_get_line_config(i);
        if (line && line->in_use) {
            config.line.in_use[i] = true;
            config.line.addr_r_ariza_akimi[i] = line->ariza_akimi[PHASE_L1];
            config.line.addr_s_ariza_akimi[i] = line->ariza_akimi[PHASE_L2];
            config.line.addr_t_ariza_akimi[i] = line->ariza_akimi[PHASE_L3];
            config.line.addr_r_ariza_suresi[i] = line->ariza_suresi[PHASE_L1];
            config.line.addr_s_ariza_suresi[i] = line->ariza_suresi[PHASE_L2];
            config.line.addr_t_ariza_suresi[i] = line->ariza_suresi[PHASE_L3];
            config.line.addr_r_ariza_turu[i] = line->ariza_kalicimi[PHASE_L1];
            config.line.addr_s_ariza_turu[i] = line->ariza_kalicimi[PHASE_L2];
            config.line.addr_t_ariza_turu[i] = line->ariza_kalicimi[PHASE_L3];
            config.line.addr_r_anlik_akim[i] = line->anlik_akim[PHASE_L1];
            config.line.addr_s_anlik_akim[i] = line->anlik_akim[PHASE_L2];
            config.line.addr_t_anlik_akim[i] = line->anlik_akim[PHASE_L3];
            config.line.addr_r_enerji_varyok[i] = line->enerji_varyok[PHASE_L1];
            config.line.addr_s_enerji_varyok[i] = line->enerji_varyok[PHASE_L2];
            config.line.addr_t_enerji_varyok[i] = line->enerji_varyok[PHASE_L3];
            config.line.addr_r_nominal_akim_varyok[i] = line->nominal_akim_varyok[PHASE_L1];
            config.line.addr_s_nominal_akim_varyok[i] = line->nominal_akim_varyok[PHASE_L2];
            config.line.addr_t_nominal_akim_varyok[i] = line->nominal_akim_varyok[PHASE_L3];
            config.line.addr_r_rfhab_varyok[i] = line->rf_haberlesme_varyok[PHASE_L1];
            config.line.addr_s_rfhab_varyok[i] = line->rf_haberlesme_varyok[PHASE_L2];
            config.line.addr_t_rfhab_varyok[i] = line->rf_haberlesme_varyok[PHASE_L3];
        } else {
            config.line.in_use[i] = false;
        }
    }
    
    if (!parse_modbus_config(json_body, &config)) {
        CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }
    
    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] CihazID: %u\r\n", config.device_addr);
    CSLOG("[HTTP] BaudRate: %u\r\n", config.baud_rate);
    
    int res = set_modbus_config(&config);

    elog_log_config_change(ELOG_CONFIG_MODBUS_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "modbus", (res == 0));

    CSLOG("[HTTP] Modbus config %s\r\n", res == 0 ? "saved" : "save failed");

    const char *response_body = res == 0 ? "{\"message\":\"Modbus configuration saved\",\"success\":true}"
    		: "{\"message\":\"Failed to save Modbus configuration\",\"success\":false}";

    http_send_json(response_body, strlen(response_body));
}

/**
 * @brief Handle GET /discovery/rf - sadece kesif listesi (Unassigned).
 *
 * Tam config degil; yalnizca atanmamis EUI-64 listesi doner.
 * Hafif yanit: Refresh butonu icin tasarlandi (GSM trafiği tasarrufu).
 */
void handle_get_rf_discovery_json(void)
{
    uint8_t  count;
    uint8_t  k;
    int      pos = 0;
    int      emitted = 0;
    char     hex[RF_EUI64_HEX_LEN];
    uint8_t  eui[RF_EUI64_LEN];

    CSLOG("[HTTP] GET /discovery/rf\r\n");

    count = rf_discovery_get_count();

    pos += xsnprintf(&handler_state.tx_buffer[pos],
                     (unsigned int)(handler_state.tx_buffer_size - pos),
                     "{\"success\":true,\"data\":{\"Unassigned\":[");

    for (k = 0U; k < count; k++)
    {
        bool assigned = false;
        int  i;

        if (!rf_discovery_get_device(k, eui))
        {
            break;
        }

        for (i = 0; i < MAX_POWER_LINE_COUNT; i++)
        {
            const rf_feeder_t *feeder = rf_store_get((feeder_id_t)i);

            if (feeder != NULL)
            {
                if ((memcmp(eui, feeder->r_eui64, RF_EUI64_LEN) == 0) ||
                    (memcmp(eui, feeder->s_eui64, RF_EUI64_LEN) == 0) ||
                    (memcmp(eui, feeder->t_eui64, RF_EUI64_LEN) == 0))
                {
                    assigned = true;
                    break;
                }
            }
        }

        if (assigned)
        {
            continue;
        }

        rf_eui64_to_hex(eui, hex);
        pos += xsnprintf(&handler_state.tx_buffer[pos],
                         (unsigned int)(handler_state.tx_buffer_size - pos),
                         "%s{\"EUI64\":\"%s\",\"FiderID\":0}",
                         (emitted > 0) ? "," : "", hex);
        emitted++;
    }

    pos += xsnprintf(&handler_state.tx_buffer[pos],
                     (unsigned int)(handler_state.tx_buffer_size - pos),
                     "]}}");

    http_send_json(handler_state.tx_buffer, (size_t)pos);
}

/**
 * @brief Handle GET /r?ayiriciRFConfig - Read RF configuration as JSON (14 array format)
 */
void handle_get_rf_config_json(void)
{
    char *buf = handler_state.tx_buffer;
    int   buf_size = handler_state.tx_buffer_size;
    int   pos;

    CSLOG("[HTTP] GET /r?ayiriciRFConfig - Reading RF config\r\n");

    /* Govde uretimi tablo-tabanli rf_json modulundedir (kod hafizasi). */
    pos = rf_json_config_build(buf, buf_size);
    CSLOG("[HTTP] RF config JSON size: %d bytes\r\n", pos);

    if (pos >= (buf_size - 1))
    {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n",
                 pos, buf_size);
    }

    http_send_json(buf, pos);
}

/**
 * @brief Handle POST /w?ayiriciRFConfig - Write RF configuration from JSON (14 array format)
 */
void handle_post_rf_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?ayiriciRFConfig - Updating RF config\r\n");
    
    if (!json_body) {
    	CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }
    
    int body_len = strlen(json_body);
    CSLOG("[HTTP] JSON body length: %d bytes\r\n", body_len);
    
    jayirici_rf_config_t config;

    /* Staging: parse yarida kalirsa yarim yazma kalici store'a gecmez. */
    if (!rf_store_stage_begin()) {
    	CSLOG_ERR("[HTTP] ERROR: RF staging busy\r\n");
        http_send_error(500, "RF config staging busy");
        return;
    }

    // Parse JSON - writes go to the staging copy via rf_store_get_mutable()
    if (!parse_rf_config(json_body, &config)) {
    	rf_store_stage_abort();
    	CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }
    rf_store_stage_commit();

    CSLOG("[HTTP] JSON parsed successfully\r\n");
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (config.in_use[i]) {
            CSLOG("[HTTP] Line %d: HatID=%u, ZoneID=%u, EUI-64=[%s, %s, %s]\r\n",
                   i+1, config.hat_id[i], config.zone_id[i],
                   config.r_eui64[i], config.s_eui64[i], config.t_eui64[i]);
        }
    }
    
    // Sync to persistent storage (data is already in breaker_config)
    if (rf_store_sync() != 0) {
        CSLOG_ERR("[HTTP] ERROR: Failed to sync RF config to storage\r\n");
        elog_log_config_change(ELOG_CONFIG_RF_CHANGED, ELOG_SOURCE_WEB,
                               gsm_get_web_client_ip(), "rf", false);
        http_send_error(500, "Failed to save configuration");
        return;
    }
    
    CSLOG("[HTTP] RF config saved\r\n");
    elog_log_config_change(ELOG_CONFIG_RF_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "rf", true);

    const char *response_body = "{\"message\":\"RF configuration saved\",\"success\":true}";
    http_send_json(response_body, strlen(response_body));
}

/**
 * @brief Send RF monitor JSON for a specific line range
 * 
 * @param line_filter Line index (0..MAX_POWER_LINE_COUNT-1) or -1 for all lines
 */
static void send_rf_monitor_json(int line_filter)
{
    if (line_filter >= MAX_POWER_LINE_COUNT) {
        CSLOG_ERR("[HTTP] ERROR: Invalid line index: %d\r\n", line_filter);
        http_send_error(400, "Invalid line index (out of range)");
        return;
    }

    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    pos += xsnprintf(buf + pos, buf_size - pos, "{\"lines\":[");
    int start_line = (line_filter >= 0) ? line_filter : 0;
    int end_line = (line_filter >= 0) ? line_filter + 1 : MAX_POWER_LINE_COUNT;
    for (int i = start_line; i < end_line; i++) {
        const rf_monitor_t *monitor = rf_get_monitor(i);
        if (!monitor) {
            CSLOG_ERR("[HTTP] ERROR: RF monitor data not available for line %d\r\n", i);
            continue;
        }
        
        if (i > start_line) pos += xsnprintf(buf + pos, buf_size - pos, ",");
        pos += xsnprintf(buf + pos, buf_size - pos, "{");
        pos += xsnprintf(buf + pos, buf_size - pos, "\"LineId\":%d,", i + 1);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"DEVICEID\":[%u,%u,%u],",
            monitor->device_id[PHASE_L1], monitor->device_id[PHASE_L2], monitor->device_id[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"FazID\":[1,2,3],");
        pos += xsnprintf(buf + pos, buf_size - pos, "\"HatID\":%u,", monitor->hat_id[PHASE_L1]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"ZoneID\":%u,", monitor->zone_id[PHASE_L1]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"CalismaModu\":[%u,%u,%u],",
            monitor->calisma_modu[PHASE_L1], monitor->calisma_modu[PHASE_L2], monitor->calisma_modu[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"HatFrekansi\":[%u,%u,%u],",
            monitor->hat_frekansi[PHASE_L1], monitor->hat_frekansi[PHASE_L2], monitor->hat_frekansi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"SistemSicakligi\":[%d,%d,%d],",
            monitor->sistem_sicakligi[PHASE_L1], monitor->sistem_sicakligi[PHASE_L2], monitor->sistem_sicakligi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"SistemDCGerilimi\":[%u,%u,%u],",
            monitor->sistem_dc_gerilimi[PHASE_L1], monitor->sistem_dc_gerilimi[PHASE_L2], monitor->sistem_dc_gerilimi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"5Vdc\":[%u,%u,%u],",
            monitor->v5vdc[PHASE_L1], monitor->v5vdc[PHASE_L2], monitor->v5vdc[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"3V3dc\":[%u,%u,%u],",
            monitor->v3v3dc[PHASE_L1], monitor->v3v3dc[PHASE_L2], monitor->v3v3dc[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"ActirmaDCGerilimi\":[%u,%u,%u],",
            monitor->actirma_dc_gerilimi[PHASE_L1], monitor->actirma_dc_gerilimi[PHASE_L2], monitor->actirma_dc_gerilimi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"FazAkimi\":[%u,%u,%u],",
            monitor->faz_akimi[PHASE_L1], monitor->faz_akimi[PHASE_L2], monitor->faz_akimi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"FazHataAkimi\":[%u,%u,%u],",
            monitor->faz_hata_akimi[PHASE_L1], monitor->faz_hata_akimi[PHASE_L2], monitor->faz_hata_akimi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"AktifSifirlamaZamanlayiciDurumu\":[%u,%u,%u],",
            monitor->aktif_sifirlama_zamanlayici_durumu[PHASE_L1], monitor->aktif_sifirlama_zamanlayici_durumu[PHASE_L2], monitor->aktif_sifirlama_zamanlayici_durumu[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"AktifArizaSayaci\":[%u,%u,%u],",
            monitor->aktif_ariza_sayaci[PHASE_L1], monitor->aktif_ariza_sayaci[PHASE_L2], monitor->aktif_ariza_sayaci[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"GecmisAcmaSayisi\":[%u,%u,%u],",
            monitor->gecmis_acma_sayisi[PHASE_L1], monitor->gecmis_acma_sayisi[PHASE_L2], monitor->gecmis_acma_sayisi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"LastTx\":[%u,%u,%u],",
            monitor->last_tx[PHASE_L1], monitor->last_tx[PHASE_L2], monitor->last_tx[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"RSSI\":[%u,%u,%u],", monitor->rssi[PHASE_L1],
        		monitor->rssi[PHASE_L2], monitor->rssi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "\"LQI\":[%u,%u,%u]", monitor->lqi[PHASE_L1],
        		monitor->lqi[PHASE_L2], monitor->lqi[PHASE_L3]);
        pos += xsnprintf(buf + pos, buf_size - pos, "}");
    }
    pos += xsnprintf(buf + pos, buf_size - pos, "]}");
    CSLOG("[HTTP] RF monitor JSON size: %d bytes (lines: %d-%d)\r\n", pos, start_line + 1, end_line);
    if (pos >= buf_size - 1) {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    http_send_json(buf, pos);
}

/**
 * @brief Send RF monitor JSON for a specific line
 * 
 * @param line_id Line index (0..MAX_POWER_LINE_COUNT-1)
 */
void handle_get_rf_monitor_line_json(int line_id)
{
    CSLOG("[HTTP] GET RF monitor for line %d\r\n", line_id);
    send_rf_monitor_json(line_id);
}

/**
 * @brief Handle GET /r?ayiriciRFMonitor - Read RF monitor data (read-only)
 * 
 * Supports optional ?line=X parameter to fetch single line:
 * - /r?ayiriciRFMonitor -> all lines (0..MAX_POWER_LINE_COUNT-1)
 * - /r?ayiriciRFMonitor&line=0 -> only line 1 (index 0)
 * - /r?ayiriciRFMonitor&line=N -> only line N+1 (index N)
 */
void handle_get_rf_monitor_json(void)
{
    CSLOG("[HTTP] GET /r?ayiriciRFMonitor - Reading RF monitor data\r\n");
    
    int line_filter = -1;  /* -1 = all lines */
    if (handler_state.current_query_string) {
        char line_value[8];
        if (http_get_query_param(handler_state.current_query_string, "line", line_value, sizeof(line_value))) {
            line_filter = xstrtoi(line_value);
            CSLOG("[HTTP] Line filter: %d\r\n", line_filter);
            if (line_filter < 0 || line_filter >= MAX_POWER_LINE_COUNT) {
                CSLOG_ERR("[HTTP] ERROR: Invalid line index: %d\r\n", line_filter);
                http_send_error(400, "Invalid line index (out of range)");
                return;
            }
        }
    }
    
    send_rf_monitor_json(line_filter);
}

/* ============================================================================
 * FIRMWARE UPDATE HANDLERS
 * ============================================================================ */

/* Firmware update state */
static fw_update_callbacks_t fw_callbacks = {0};
static bool fw_callbacks_registered = false;
static struct {
    uint32_t total_size;
    uint32_t received_bytes;
    uint32_t total_chunks;
    uint32_t received_chunks;
    uint32_t checksum_errors;
    uint32_t retry_count;
    uint32_t start_time;        /* Transfer start timestamp (tick count) */
    uint32_t last_chunk_time;   /* Last chunk receive time */
    uint32_t first_chunk_size;  /* Size of first chunk (for chunk_num calculation) */
    uint32_t file_hash;         /* Fletcher-16 of first 64 bytes - file identity for resume detection */
    bool in_progress;
} fw_state = {0};

/* FW Update timeout configuration (in milliseconds) */
#define FW_UPDATE_CHUNK_TIMEOUT_MS   30000   /* 30 seconds between chunks */
#define FW_UPDATE_TOTAL_TIMEOUT_MS  600000   /* 10 minutes total transfer */
#define FW_UPDATE_CHUNK_SIZE          4096   /* Recommended chunk size for client */
#define FW_UPDATE_DEFAULT_CHUNK_SIZE  1024   /* Default chunk size if not detected */

/* Firmware version info - can be overridden by application */
static const char *fw_version = "1.0.0";
static const char *fw_build_date = __DATE__;
static const char *fw_hardware = "TROIKA-SCB-v1";

void fw_update_register_callbacks(const fw_update_callbacks_t *callbacks) {
    if (callbacks) {
        fw_callbacks = *callbacks;  /* Copy the structure */
        fw_callbacks_registered = true;
        CSLOG("[FW] Firmware update callbacks registered\r\n");
    }
}

void fw_update_set_version_info(const char *version, const char *build_date, const char *hardware) {
    if (version) fw_version = version;
    if (build_date) fw_build_date = build_date;
    if (hardware) fw_hardware = hardware;
}

void handle_get_fault_records_json(void)
{
    CSLOG("[HTTP] GET /faults - Reading IEC104 fault records\r\n");

    int feeder = -1;
    if (handler_state.current_query_string) {
        const char *fp = strstr(handler_state.current_query_string, "feeder=");
        if (fp) {
            feeder = xstrtoi(fp + 7);
        }
    }

    if (feeder < 0 || feeder >= MAX_POWER_LINE_COUNT) {
        CSLOG("[HTTP] /faults: invalid feeder=%d\r\n", feeder);
        http_send_bad_request();
        return;
    }

    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;

    uint8_t tc[3], pc[3];
    for (int ph = 0; ph < 3; ph++) 
    {
        tc[ph] = fault_log_get_temp_count((uint8_t)feeder, (uint8_t)ph);
        pc[ph] = fault_log_get_perm_count((uint8_t)feeder, (uint8_t)ph);
    }

    pos += xsnprintf(buf + pos, buf_size - pos, "{\"feeder\":%d,", feeder);

    static const char * const fnames[3][2] = {
        {"L1T", "L1P"}, {"L2T", "L2P"}, {"L3T", "L3P"}
    };

    for (int ph = 0; ph < 3; ph++) 
    {
        for (int ty = 0; ty < 2; ty++) 
        {
            uint8_t count = (ty == 0) ? tc[ph] : pc[ph];
            fault_log_type_t log_type = (ty == 0) ? FAULT_LOG_TYPE_TEMPORARY : FAULT_LOG_TYPE_PERMANENT;

            pos += xsnprintf(buf + pos, buf_size - pos, "\"%s\":\"", fnames[ph][ty]);

            fault_log_t log;
            for (uint8_t n = 0; n < count; n++) 
            {
                if (!fault_log_read_nth((uint8_t)feeder, (uint8_t)ph, log_type, n, &log)) {
                    continue;
                }
                uint16_t yr = 2000u + cp56time2a_get_year(&log.tm);
                uint8_t  mo = cp56time2a_get_month(&log.tm);
                uint8_t  dy = cp56time2a_get_day(&log.tm);
                uint8_t  hr = cp56time2a_get_hour(&log.tm);
                uint8_t  mn = cp56time2a_get_minute(&log.tm);
                uint8_t  sc = cp56time2a_get_second(&log.tm);
                uint16_t ms = cp56time2a_get_ms(&log.tm);

                pos += xsnprintf(buf + pos, buf_size - pos,
                    "%u %04u/%02u/%02u %02u:%02u:%02u:%03u %.3fA %ums %u %u\\n",
                    (unsigned)(n + 1u),
                    (unsigned)yr, (unsigned)mo, (unsigned)dy,
                    (unsigned)hr, (unsigned)mn, (unsigned)sc, (unsigned)ms,
                    log.fault_current,
                    (unsigned)log.fault_duration_ms,
                    (unsigned)log.info.nominal_current_status,
                    (unsigned)log.info.power_status);
            }

            if (ph == 2 && ty == 1) {
                pos += xsnprintf(buf + pos, buf_size - pos, "\"");
            } else {
                pos += xsnprintf(buf + pos, buf_size - pos, "\",");
            }
        }
    }

    pos += xsnprintf(buf + pos, buf_size - pos,
        ",\"tc\":[%u,%u,%u],\"pc\":[%u,%u,%u]}",
        (unsigned)tc[0], (unsigned)tc[1], (unsigned)tc[2],
        (unsigned)pc[0], (unsigned)pc[1], (unsigned)pc[2]);

    CSLOG("[HTTP] /faults JSON size: %d bytes (feeder %d)\r\n", pos, feeder);
    if (pos >= buf_size - 1) {
        CSLOG_WARN("[HTTP] WARNING: /faults buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    http_send_json(buf, pos);
}

void handle_get_fw_version(void) {
    CSLOG("[FW] GET /r?fwVersion\r\n");
    
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;
    
    pos += xsnprintf(buf + pos, buf_size - pos, "{");
    pos += xsnprintf(buf + pos, buf_size - pos, "\"version\":\"%s\",", fw_version);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"buildDate\":\"%s\",", fw_build_date);
    pos += xsnprintf(buf + pos, buf_size - pos, "\"hardware\":\"%s\"", fw_hardware);
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    http_send_json(buf, pos);
}

void handle_get_fw_status(void) {
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos = 0;

    /* TODO: fw_state yalniz RAM'de - reset sonrasi "ready" durumu kaybolur.
     * NVRAM rfwu_nvram_t'ye su alanlari kalici yap:
     *   - received_bytes (4 KB hizali kontrol noktasi - yarida kalan
     *     transfer kaldigi yerden devam etsin)
     *   - total_size + file_hash (dosya kimligi - ayni dosya mi kontrolu)
     *   - tamamlanmis indirme isareti (acilista "apply hazir" geri yukle)
     * Resume akisi: fw_start'ta hash eslesirse received_bytes'tan devam;
     * eslesmezse (farkli dosya) sifirdan basla. */

    if (fw_state.in_progress) {
        pos += xsnprintf(buf + pos, buf_size - pos,
                         "{\"active\":true,\"received\":%u,\"total\":%u,\"fh\":%u}",
                         fw_state.received_bytes, fw_state.total_size, fw_state.file_hash);
    }
    else if ((fw_state.total_size > 0U) &&
             (fw_state.received_bytes == fw_state.total_size))
    {
        /* Transfer bitti, apply bekliyor - UI butonu gostersin */
        pos += xsnprintf(buf + pos, buf_size - pos,
                         "{\"active\":false,\"ready\":true,\"received\":%u,\"total\":%u,\"fh\":%u}",
                         fw_state.received_bytes, fw_state.total_size, fw_state.file_hash);
    }
    else {
        pos += xsnprintf(buf + pos, buf_size - pos, "{\"active\":false}");
    }

    http_send_json(buf, pos);
}

void handle_fw_start(const char *json_body) {
    CSLOG("[FW] POST /s (fw_start)\r\n");

    if (!fw_callbacks_registered || !fw_callbacks.fw_init) {
        CSLOG_ERR("[FW] ERROR: Callbacks not registered\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"not supported\"}", 41);
        return;
    }

    /* Parse JSON: { "s": X, "c": Y, "h": Z } */
    uint32_t size = 0U, chunks = 0U, hash = 0U;

    const char *size_ptr = strstr(json_body, "\"s\"");
    if (!size_ptr) { size_ptr = strstr(json_body, "\"size\""); }

    const char *chunks_ptr = strstr(json_body, "\"c\"");
    if (!chunks_ptr) { chunks_ptr = strstr(json_body, "\"chunks\""); }

    /* "h" key: file identity hash (Fletcher-16 of first 64 bytes) */
    const char *hash_ptr = strstr(json_body, "\"h\"");

    if (size_ptr) {
        size_ptr = strchr(size_ptr, ':');
        if (size_ptr) { size = (uint32_t)xstrtoi(size_ptr + 1); }
    }
    if (chunks_ptr) {
        chunks_ptr = strchr(chunks_ptr, ':');
        if (chunks_ptr) { chunks = (uint32_t)xstrtoi(chunks_ptr + 1); }
    }
    if (hash_ptr) {
        hash_ptr = strchr(hash_ptr, ':');
        if (hash_ptr) { hash = (uint32_t)xstrtoi(hash_ptr + 1); }
    }

    if (size == 0U) {
        CSLOG_ERR("[FW] ERROR: Invalid size\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"invalid size\"}", 40);
        return;
    }

    /* --- Resume detection ---
     * If an upload for the exact same file is already in progress
     * (matching total_size and file_hash), skip flash re-init and return
     * the current progress so the client continues from where it stopped.
     * hash == 0 means legacy client (no hash sent): fall back to
     * size-only matching for backward compatibility. */
    if (fw_state.in_progress &&
        (fw_state.total_size == size) &&
        (hash == 0U || fw_state.file_hash == hash))
    {
        CCSLOG(XCOLOR_GREEN, "[FW] RESUME: same file detected, continuing from %u/%u bytes\r\n",
                 fw_state.received_bytes, fw_state.total_size);
        char *buf = handler_state.tx_buffer;
        int len = xsnprintf(buf, handler_state.tx_buffer_size,
                            "{\"status\":\"ok\",\"cs\":4096,\"received\":%u}",
                            fw_state.received_bytes);
        http_send_json(buf, len);
        return;
    }

    /* --- Fresh start ---
     * Different file (size or hash mismatch) or no transfer in progress:
     * reset all state and re-initialise flash. */
    if (fw_state.in_progress) {
        CSLOG_WARN("[FW] Different file - resetting previous upload (was %u/%u bytes)\r\n",
                 fw_state.received_bytes, fw_state.total_size);
    }

    CCSLOG(XCOLOR_CYAN, "[FW] ========== FIRMWARE UPDATE START ==========\r\n");
    CSLOG("[FW] Total Size: %u bytes\r\n", size);
    CSLOG("[FW] Total Chunks: %u\r\n", chunks);
    CSLOG("[FW] File Hash: 0x%04X\r\n", hash);

    int result = fw_callbacks.fw_init(size);
    if (result != 0) {
        CSLOG_ERR("[FW] ERROR: Flash init failed: %d\r\n", result);
        http_send_json("{\"status\":\"error\",\"error\":\"flash init\"}", 38);
        return;
    }

    fw_state.total_size       = size;
    fw_state.total_chunks     = chunks;
    fw_state.received_bytes   = 0U;
    fw_state.received_chunks  = 0U;
    fw_state.checksum_errors  = 0U;
    fw_state.retry_count      = 0U;
    fw_state.start_time       = 0U;
    fw_state.last_chunk_time  = 0U;
    fw_state.first_chunk_size = 0U;
    fw_state.file_hash        = hash;
    fw_state.in_progress      = true;

    CCSLOG(XCOLOR_GREEN, "[FW] Flash initialized, ready for chunks\r\n");
    http_send_json("{\"status\":\"ok\",\"cs\":4096,\"received\":0}", 38);
}

/* Fletcher-16 checksum - fast, no lookup table, good error detection */
static uint16_t fletcher16(const uint8_t *data, uint32_t len) {
    uint16_t sum1 = 0, sum2 = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

void handle_fw_chunk(uint32_t offset, const uint8_t *data, uint32_t size, 
                     bool has_checksum, uint16_t expected_checksum) {
    if (!fw_callbacks_registered || !fw_callbacks.fw_write) {
        http_send_json("{\"status\":\"error\",\"error\":\"not supported\"}", 41);
        return;
    }
    
    if (!fw_state.in_progress) {
        CSLOG_ERR("[FW] ERROR: No update in progress\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"no update\"}", 37);
        return;
    }
    
    /* Remember first chunk size for accurate chunk number calculation */
    if (offset == 0 && size > 0) {
        fw_state.first_chunk_size = size;
        CSLOG("[FW] First chunk size: %u bytes\r\n", size);
    }
    
    /* Calculate chunk number - use received_chunks for current position
     * This is the most reliable method as it doesn't depend on chunk size calculation.
     * For retries (offset < received_bytes), calculate from offset. */
    uint32_t chunk_num;
    if (offset < fw_state.received_bytes) {
        /* This is a retry - calculate chunk number from offset */
        uint32_t chunk_size = fw_state.first_chunk_size;
        if (chunk_size == 0) chunk_size = FW_UPDATE_DEFAULT_CHUNK_SIZE;
        chunk_num = offset / chunk_size;
    } else {
        /* Normal flow - use received_chunks counter */
        chunk_num = fw_state.received_chunks;
    }
    
    char *buf = handler_state.tx_buffer;
    int len;
    
    /* Verify checksum if provided - ALWAYS log checksum info */
    uint16_t calc_checksum = fletcher16(data, size);
    
    if (has_checksum) {
        if (calc_checksum != expected_checksum) {
            fw_state.checksum_errors++;
            CSLOG_ERR("[FW] CHECKSUM FAIL! Chunk #%u @offset=%u\r\n", chunk_num, offset);
            CSLOG_ERR("[FW]   Expected: 0x%04X\r\n", expected_checksum);
            CSLOG_ERR("[FW]   Received: 0x%04X\r\n", calc_checksum);
            CSLOG_ERR("[FW]   Size: %u bytes, Total CS errors: %u\r\n", 
                     size, fw_state.checksum_errors);
            
            /* Request retry - send error with expected chunk number */
            len = xsnprintf(buf, handler_state.tx_buffer_size,
                           "{\"status\":\"error\",\"error\":\"checksum\",\"chunk\":%u,\"retry\":true}",
                           chunk_num);
            http_send_json(buf, len);
            return;
        }
        /* Checksum OK - log for debugging */
        CSLOG("[FW] Chunk #%u: offset=%u, size=%u, CS=0x%04X OK\r\n", 
                chunk_num, offset, size, calc_checksum);
    } else {
        /* No checksum provided - log warning */
        CSLOG_WARN("[FW] Chunk #%u: offset=%u, size=%u (NO CHECKSUM)\r\n",
                 chunk_num, offset, size);
    }
    
    /* Handle retry: if offset is before received_bytes, it's a retry */
    if (offset < fw_state.received_bytes) {
        fw_state.retry_count++;
        /* Check if it's the previous chunk being retried */
        if (offset + size == fw_state.received_bytes) {
            CSLOG_WARN("[FW] Retry chunk #%u accepted (total retries: %u)\r\n", 
                     chunk_num, fw_state.retry_count);
            len = xsnprintf(buf, handler_state.tx_buffer_size, 
                            "{\"status\":\"ok\",\"chunk\":%u}", chunk_num);
            http_send_json(buf, len);
            return;
        }
        /* Offset is in the middle of received data - could be partial retry */
        CSLOG_WARN("[FW] Late retry? offset=%u < received=%u (chunk #%u)\r\n", 
                 offset, fw_state.received_bytes, chunk_num);
        /* Don't update received_bytes, just ACK */
        len = xsnprintf(buf, handler_state.tx_buffer_size, 
                        "{\"status\":\"ok\",\"chunk\":%u}", chunk_num);
        http_send_json(buf, len);
        return;
    }
    
    /* Check for gap in data (offset > received_bytes) */
    if (offset > fw_state.received_bytes) {
        uint32_t expected_chunk = fw_state.received_chunks;
        CSLOG_ERR("[FW] GAP DETECTED! offset=%u, expected=%u\r\n", 
                 offset, fw_state.received_bytes);
        CSLOG_ERR("[FW]   Missing %u bytes (chunk #%u expected)\r\n",
                 offset - fw_state.received_bytes, expected_chunk);
        len = xsnprintf(buf, handler_state.tx_buffer_size,
                       "{\"status\":\"error\",\"error\":\"gap\",\"expected\":%u}",
                       fw_state.received_bytes);
        http_send_json(buf, len);
        return;
    }
    
    if (offset + size > fw_state.total_size) {
        CSLOG_ERR("[FW] OVERFLOW! offset=%u + size=%u > total=%u\r\n", 
                 offset, size, fw_state.total_size);
        http_send_json("{\"status\":\"error\",\"error\":\"overflow\"}", 36);
        return;
    }
    
    /* Write chunk to flash */
    int result = fw_callbacks.fw_write(offset, data, size);
    if (result != 0) {
        CSLOG_ERR("[FW] FLASH WRITE FAIL! offset=%u, error=%d\r\n", offset, result);
        fw_state.in_progress = false;
        http_send_json("{\"status\":\"error\",\"error\":\"write fail\"}", 38);
        return;
    }
    
    fw_state.received_bytes += size;
    fw_state.received_chunks++;
    
    /* Log progress every 10% or every chunk in verbose mode */
    uint32_t progress = (fw_state.received_bytes * 100) / fw_state.total_size;
    static uint32_t last_progress = 0;
    if (progress / 10 != last_progress / 10) {
        CCSLOG(XCOLOR_GREEN, "[FW] Progress: %u%% (%u/%u bytes, chunk %u/%u)\r\n", 
                 progress, fw_state.received_bytes, fw_state.total_size,
                 fw_state.received_chunks, fw_state.total_chunks);
        last_progress = progress;
    }
    
    /* Send OK response with chunk number for client verification */
    len = xsnprintf(buf, handler_state.tx_buffer_size, 
                    "{\"status\":\"ok\",\"chunk\":%u}", chunk_num);
    http_send_json(buf, len);
}

void handle_fw_finish(const char *json_body) {
    CCSLOG(XCOLOR_CYAN, "[FW] ========== FIRMWARE TRANSFER FINISH ==========\r\n");
    
    if (!fw_callbacks_registered || !fw_callbacks.fw_finish) {
        CSLOG_ERR("[FW] ERROR: Callbacks not registered\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"not supported\"}", 41);
        return;
    }
    
    if (!fw_state.in_progress) {
        CSLOG_ERR("[FW] ERROR: No update in progress\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"no update\"}", 37);
        return;
    }
    
    /* Print detailed transfer summary */
    CSLOG("[FW] ---- Transfer Summary ----\r\n");
    CSLOG("[FW] Total Size:      %u bytes\r\n", fw_state.total_size);
    CSLOG("[FW] Received:        %u bytes\r\n", fw_state.received_bytes);
    CSLOG("[FW] Expected Chunks: %u\r\n", fw_state.total_chunks);
    CSLOG("[FW] Received Chunks: %u\r\n", fw_state.received_chunks);
    CSLOG("[FW] Checksum Errors: %u\r\n", fw_state.checksum_errors);
    CSLOG("[FW] Retry Count:     %u\r\n", fw_state.retry_count);
    
    if (fw_state.received_bytes != fw_state.total_size) {
        uint32_t missing = fw_state.total_size - fw_state.received_bytes;
        CSLOG_ERR("[FW] ERROR: Incomplete transfer!\r\n");
        CSLOG_ERR("[FW]   Missing: %u bytes (%.1f%%)\r\n", 
                 missing, (missing * 100.0f) / fw_state.total_size);
        
        char *buf = handler_state.tx_buffer;
        int len = xsnprintf(buf, handler_state.tx_buffer_size,
                           "{\"status\":\"error\",\"error\":\"incomplete\","
                           "\"received\":%u,\"expected\":%u}",
                           fw_state.received_bytes, fw_state.total_size);
        http_send_json(buf, len);
        return;
    }
    
    CSLOG("[FW] Transfer complete, verifying...\r\n");
    
    /* Finalize - verification is done by callback if needed */
    int result = fw_callbacks.fw_finish(fw_state.total_size);
    if (result != 0) {
        CSLOG_ERR("[FW] ERROR: Verification failed! Code: %d\r\n", result);
        fw_state.in_progress = false;
        http_send_json("{\"status\":\"error\",\"error\":\"verify fail\"}", 39);
        return;
    }
    
    fw_state.in_progress = false;
    
    /* Print success summary */
    CCSLOG(XCOLOR_GREEN, "[FW] ========================================\r\n");
    CCSLOG(XCOLOR_GREEN, "[FW] FW TRANSFER COMPLETE - IMAGE VERIFIED\r\n");
    CCSLOG(XCOLOR_GREEN, "[FW] ========================================\r\n");
    CSLOG("[FW] Total: %u bytes in %u chunks\r\n", 
            fw_state.total_size, fw_state.received_chunks);
    if (fw_state.checksum_errors > 0 || fw_state.retry_count > 0) {
        CSLOG_WARN("[FW] Note: %u checksum errors, %u retries during transfer\r\n",
                 fw_state.checksum_errors, fw_state.retry_count);
    }
    CSLOG("[FW] Image NOT applied - use Apply Firmware (/fw_apply)\r\n");
    
    http_send_json("{\"status\":\"ok\"}", 15);
}

void handle_fw_reboot(void) {
    CCSLOG(XCOLOR_CYAN, "[FW] ========== REBOOT REQUEST ==========\r\n");
    
    if (!fw_callbacks_registered || !fw_callbacks.fw_reboot) {
        CSLOG_ERR("[FW] ERROR: Reboot callback not registered\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"not supported\"}", 41);
        return;
    }

    /* Send response before reboot */
    http_send_json("{\"status\":\"ok\"}", 15);

    fw_callbacks.fw_reboot();
}

void handle_fw_apply(void) {
    CCSLOG(XCOLOR_CYAN, "[FW] ========== APPLY FW REQUEST ==========\r\n");

    if (!fw_state.in_progress && (0U == fw_state.total_size)) {
        CSLOG_ERR("[FW] ERROR: No firmware downloaded\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"no firmware\"}", 43);
        return;
    }

    if (fw_state.received_bytes != fw_state.total_size) {
        CSLOG_ERR("[FW] ERROR: Download incomplete\r\n");
        http_send_json("{\"status\":\"error\",\"error\":\"incomplete\"}", 42);
        return;
    }

    CSLOG_WARN("[FW] Requesting bootloader update mode via IPC...\r\n");

    elog_log_fw_update(ELOG_FW_SRC_RFWU, ELOG_FW_RESULT_START, 0U);
    int result = app_ipc_request_update(false);

    if(result != APP_IPC_OK)
    {
        CSLOG_ERR("[FW] ERROR: IPC request failed (%d)\r\n", result);
        http_send_json("{\"status\":\"error\",\"error\":\"ipc failed\"}", 39);
        return;
    }

    CCSLOG(XCOLOR_GREEN, "[FW] Update armed - reset in 10 s\r\n");
    /* Response goes out before the countdown starts. */
    http_send_json("{\"status\":\"ok\",\"reset_in\":10}", 29);
    reboot_system_delayed(10000U);
}
